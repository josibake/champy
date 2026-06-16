// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <chainstate.h>
#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_spend.h>
#include <consensus/consensus.h>
#include <consensus/pow.h>
#include <flatfile.h>
#include <kernel/chainparams.h>
#include <kernel/notifications_interface.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <signet.h>
#include <tinyformat.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/log.h>
#include <util/moneystr.h>
#include <util/signalinterrupt.h>
#include <util/time.h>
#include <util/trace.h>
#include <util/translation.h>
#include <validation/active_chain.h>
#include <validation/block_connection.h>
#include <validation/block_data_adapters.h>
#include <validation/block_data_admission.h>
#include <validation/block_header_context_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_replay.h>
#include <validation/block_validation_adapters.h>
#include <validation/block_validation_error.h>
#include <validation/block_validation_internal.h>
#include <validation/block_validation_policy.h>
#include <validation/coins_view_spend_state.h>
#include <validation/core_block_commit_adapters.h>
#include <validation/core_block_connection_setup.h>
#include <validation/core_block_policy.h>
#include <validation/core_chain_validation_runtimes.h>
#include <validation/core_validation_event_snapshot.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/script_task_executor.h>
#include <validation/validation_event_queue.h>
#include <validation/verify_db.h>
#include <validation_state.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

using kernel::Notifications;

namespace {

struct BlockHeaderAcceptanceResult {
    bool accepted{false};
    CBlockIndex* block_index{nullptr};
};

ChainWorkBlockSnapshot MakeChainWorkBlockSnapshot(const CBlockIndex& block)
{
    return {
        .hash = block.GetBlockHash(),
        .parent_hash = block.pprev ? block.pprev->GetBlockHash() : uint256{},
        .height = block.nHeight,
        .chain_work = block.nChainWork,
    };
}

AcceptedBlockHeaderSnapshot MakeAcceptedBlockHeaderSnapshot(const CBlockIndex& block)
{
    return {
        .block = MakeChainWorkBlockSnapshot(block),
        .block_time = block.GetBlockTime(),
    };
}

Consensus::DifficultyAdjustmentContext BuildDifficultyAdjustmentContext(const CBlockIndex& last_block, const Consensus::Params& params)
{
    const int difficulty_adjustment_interval{static_cast<int>(params.DifficultyAdjustmentInterval())};
    const int next_height{last_block.nHeight + 1};
    const unsigned int proof_of_work_limit{UintToArith256(params.powLimit).GetCompact()};

    Consensus::DifficultyAdjustmentContext context{
        .next_height = next_height,
        .last_bits = last_block.nBits,
        .last_block_time = last_block.GetBlockTime(),
        .first_period_bits = last_block.nBits,
        .first_period_block_time = last_block.GetBlockTime(),
        .last_non_min_difficulty_bits = last_block.nBits,
    };

    if (next_height % difficulty_adjustment_interval == 0) {
        const int first_height{last_block.nHeight - (difficulty_adjustment_interval - 1)};
        assert(first_height >= 0);
        const CBlockIndex* first_block{last_block.GetAncestor(first_height)};
        assert(first_block);
        context.first_period_bits = first_block->nBits;
        context.first_period_block_time = first_block->GetBlockTime();
    }

    if (params.fPowAllowMinDifficultyBlocks && next_height % difficulty_adjustment_interval != 0) {
        const CBlockIndex* cursor{&last_block};
        while (cursor->pprev && cursor->nHeight % difficulty_adjustment_interval != 0 && cursor->nBits == proof_of_work_limit) {
            cursor = cursor->pprev;
        }
        context.last_non_min_difficulty_bits = cursor->nBits;
    }

    return context;
}

unsigned int ExpectedDifficultyBits(const CBlockIndex& previous_block, const CBlockHeader& candidate, const Consensus::Params& params)
{
    return Consensus::GetNextWorkRequired(BuildDifficultyAdjustmentContext(previous_block, params), candidate.GetBlockTime(), params);
}

std::optional<ChainWorkBlockSnapshot> MaybeChainWorkBlockSnapshot(const CBlockIndex* block)
{
    if (!block) return std::nullopt;
    return MakeChainWorkBlockSnapshot(*block);
}

class CoreActiveChainView final : public validation::ActiveChainView
{
public:
    explicit CoreActiveChainView(const CChain& chain) : m_chain{chain} {}

    [[nodiscard]] CBlockIndex* Tip() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return m_chain.Tip(); }
    [[nodiscard]] int Height() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return m_chain.Height(); }
    [[nodiscard]] CBlockIndex* Next(const CBlockIndex& block_index) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main) { return m_chain.Next(block_index); }

private:
    const CChain& m_chain;
};

} // namespace

template <typename CoreRuntime>
static CoreBlockConnectionRuntimeInputs MakeCoreBlockConnectionRuntimeInputs(
    CoreRuntime& runtime,
    validation::ScriptTaskExecutor& script_task_executor)
{
    return {
        .notifications = runtime.Notifications(),
        .script_task_executor = script_task_executor,
        .validation_cache = runtime.ScriptValidationCache(),
    };
}

static bool CheckMerkleRoot(const CBlock& block, const Consensus::BlockStructuralFacts& facts, BlockValidationState& state)
{
    if (const auto merkle_check{Consensus::CheckBlockMerkleRoot(block, facts)}; !merkle_check) {
        return ApplyBlockCheckError(state, merkle_check.error());
    }

    return true;
}

/** CheckWitnessMalleation performs checks for block malleation with regard to
 * its witnesses.
 *
 * Note: If the witness commitment is expected (i.e. `expect_witness_commitment`
 * is true), then the block is required to have at least one transaction and the
 * first transaction needs to have at least one input. */
static bool CheckWitnessMalleation(const CBlock& block, const Consensus::BlockFacts& facts, bool expect_witness_commitment, BlockValidationState& state)
{
    if (const auto witness_check{Consensus::CheckBlockWitnessMalleation(block, facts, {.expect_witness_commitment = expect_witness_commitment})}; !witness_check) {
        return ApplyBlockCheckError(state, witness_check.error());
    }

    return true;
}

bool CheckBlock(const CBlock& block, BlockValidationState& state, const Consensus::Params& consensusParams, const Consensus::BlockCheckOptions& options)
{
    // These are checks that are independent of context.

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (const auto header_check{Consensus::CheckBlockHeader(block, consensusParams, {.check_pow = options.check_pow})}; !header_check) {
        return ApplyBlockCheckError(state, header_check.error());
    }

    // Signet only: check block solution
    if (consensusParams.signet_blocks && options.check_pow && !CheckSignetBlockSolution(block, consensusParams)) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-signet-blksig", "signet block signature validation failure");
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.
    const Consensus::BlockStructuralConsensusOptions structural_options{
        .check_merkle_root = options.check_merkle_root,
    };
    if (const auto structural_check{Consensus::ValidateBlockStructuralStage(block, structural_options)}; !structural_check) {
        return ApplyBlockCheckError(state, structural_check.error());
    }

    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    return Consensus::CalculateBlockSubsidy(nHeight, consensusParams);
}

bool HasValidProofOfWork(std::span<const CBlockHeader> headers, const Consensus::Params& consensusParams)
{
    return std::ranges::all_of(headers,
                               [&](const auto& header) { return CheckProofOfWork(header.GetHash(), header.nBits, consensusParams); });
}

bool IsBlockMutated(const CBlock& block, BlockMutationOptions options)
{
    BlockValidationState state;
    const Consensus::BlockFacts facts{Consensus::ComputeBlockFacts(block)};
    if (!CheckMerkleRoot(block, facts.structure, state)) {
        LogDebug(BCLog::VALIDATION, "Block mutated: %s\n", FormatValidationStateForLog(state));
        return true;
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        // Consider the block mutated if any transaction is 64 bytes in size (see 3.1
        // in "Weaknesses in Bitcoin's Merkle Root Construction":
        // https://lists.linuxfoundation.org/pipermail/bitcoin-dev/attachments/20190225/a27d8837/attachment-0001.pdf).
        //
        // Note: This is not a consensus change as this only applies to blocks that
        // don't have a coinbase transaction and would therefore already be invalid.
        return std::any_of(block.vtx.begin(), block.vtx.end(),
                           [](auto& tx) { return GetSerializeSize(TX_NO_WITNESS(tx)) == 64; });
    } else {
        // Theoretically it is still possible for a block with a 64 byte
        // coinbase transaction to be mutated but we neglect that possibility
        // here as it requires at least 224 bits of work.
    }

    if (!CheckWitnessMalleation(block, facts, options.check_witness_root, state)) {
        LogDebug(BCLog::VALIDATION, "Block mutated: %s\n", FormatValidationStateForLog(state));
        return true;
    }

    return false;
}

arith_uint256 CalculateClaimedHeadersWork(std::span<const CBlockHeader> headers)
{
    arith_uint256 total_work{0};
    for (const CBlockHeader& header : headers) {
        total_work += GetBlockProof(header);
    }
    return total_work;
}

static bool ContextualCheckBlockHeader(
    const CBlockHeader& block,
    BlockValidationState& state,
    const Consensus::Params& consensus_params,
    const BlockHeaderContextProvider& header_context_provider,
    const CBlockIndex* pindexPrev,
    int64_t max_future_block_time) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    AssertLockHeld(::cs_main);
    assert(pindexPrev != nullptr);
    const Consensus::BlockHeaderContext headers{header_context_provider.BuildContext(pindexPrev)};

    const Consensus::BlockHeaderAdmissionOptions header_options{
        .expected_difficulty_bits = ExpectedDifficultyBits(*pindexPrev, block, consensus_params),
        .contextual = Consensus::BuildBlockContextualHeaderOptions(headers, consensus_params, max_future_block_time),
    };
    if (const auto header_check{Consensus::CheckBlockHeaderAdmissionRules(block, header_options)}; !header_check) {
        return ApplyBlockCheckError(state, header_check.error());
    }

    return true;
}

/** NOTE: This function is not currently invoked by block connection, so we
 *  should consider upgrade issues if we change which consensus rules are
 *  enforced in this function (eg by adding a new consensus rule). See comment
 *  during block connection.
 *  Note that -reindex-chainstate skips the validation that happens here!
 */
static bool ContextualCheckBlock(const CBlock& block, BlockValidationState& state, const BlockHeaderContextProvider& header_context_provider, const CBlockIndex* pindexPrev)
{
    const Consensus::BlockHeaderContext headers{header_context_provider.BuildContext(pindexPrev)};
    const Consensus::BlockContextualBodyOptions options{
        Consensus::BuildBlockContextualBodyOptions(block, headers)};

    const auto contextual_check{Consensus::ValidateBlockContextualBodyStage(block, options, __func__)};
    if (!contextual_check) {
        return ApplyBlockCheckError(state, contextual_check.error());
    }

    return true;
}

static BlockHeaderAcceptanceResult AcceptBlockHeader(
    BlockIndexHeaderStore& block_index,
    const Consensus::Params& consensus_params,
    const BlockHeaderContextProvider& header_context_provider,
    const CBlockHeader& block,
    BlockValidationState& state,
    BlockHeaderAcceptanceOptions options,
    int64_t max_future_block_time) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = block.GetHash();
    if (hash != consensus_params.hashGenesisBlock) {
        if (CBlockIndex * pindex{block_index.LookupBlockIndex(hash)}) {
            // Block header is already known.
            if (pindex->nStatus & BLOCK_FAILED_VALID) {
                LogDebug(BCLog::VALIDATION, "%s: block %s is marked invalid\n", __func__, hash.ToString());
                state.Invalid(BlockValidationResult::BLOCK_CACHED_INVALID, "duplicate-invalid",
                              strprintf("block %s was previously marked invalid", hash.ToString()));
                return {.block_index = pindex};
            }
            return {.accepted = true, .block_index = pindex};
        }

        if (const auto header_check{Consensus::CheckBlockHeader(block, consensus_params, {.check_pow = true})}; !header_check) {
            ApplyBlockCheckError(state, header_check.error());
            LogDebug(BCLog::VALIDATION, "%s: Consensus::CheckBlockHeader: %s, %s\n", __func__, hash.ToString(), FormatValidationStateForLog(state));
            return {};
        }

        // Get prev block index
        CBlockIndex* pindexPrev{block_index.LookupBlockIndex(block.hashPrevBlock)};
        if (!pindexPrev) {
            LogDebug(BCLog::VALIDATION, "header %s has prev block not found: %s\n", hash.ToString(), block.hashPrevBlock.ToString());
            state.Invalid(BlockValidationResult::BLOCK_MISSING_PREV, "prev-blk-not-found");
            return {};
        }
        if (pindexPrev->nStatus & BLOCK_FAILED_VALID) {
            LogDebug(BCLog::VALIDATION, "header %s has prev block invalid: %s\n", hash.ToString(), block.hashPrevBlock.ToString());
            state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, "bad-prevblk");
            return {};
        }
        if (!ContextualCheckBlockHeader(block, state, consensus_params, header_context_provider, pindexPrev, max_future_block_time)) {
            LogDebug(BCLog::VALIDATION, "%s: Consensus::ContextualCheckBlockHeader: %s, %s\n", __func__, hash.ToString(), FormatValidationStateForLog(state));
            return {};
        }
    }
    if (!options.min_pow_checked) {
        LogDebug(BCLog::VALIDATION, "%s: not adding new block header %s, missing anti-dos proof-of-work validation\n", __func__, hash.ToString());
        state.Invalid(BlockValidationResult::BLOCK_HEADER_LOW_WORK, "too-little-chainwork");
        return {};
    }
    CBlockIndex* pindex{block_index.AddToBlockIndex(block)};

    return {.accepted = true, .block_index = pindex};
}

// Exposed wrapper for AcceptBlockHeader
NewBlockHeadersResult ProcessNewBlockHeaders(CoreHeaderAdmissionRuntime& runtime, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state)
{
    AssertLockNotHeld(cs_main);
    NewBlockHeadersResult result{.accepted = true};
    {
        LOCK(cs_main);
        CoreBlockIndexStore block_index{runtime.MakeBlockIndexStore()};
        const CoreBlockHeaderContextProvider header_context{runtime.MakeHeaderContextProvider()};
        const Consensus::Params& consensus_params{runtime.ConsensusParams()};
        for (const CBlockHeader& header : headers) {
            const BlockHeaderAcceptanceResult accepted_header{AcceptBlockHeader(block_index, consensus_params, header_context, header, state, options, time.MaxFutureBlockTimeSeconds())};
            runtime.CheckBlockIndex();

            if (!accepted_header.accepted) {
                return {.last_accepted = result.last_accepted};
            }
            result.last_accepted = MakeAcceptedBlockHeaderSnapshot(*Assert(accepted_header.block_index));
        }
    }
    if (runtime.NotifyHeaderTip()) {
        if (runtime.IsInitialBlockDownload() && result.last_accepted) {
            const AcceptedBlockHeaderSnapshot& last_accepted{*result.last_accepted};
            const NodeSeconds current_time{time.CurrentTime()};
            const NodeSeconds last_accepted_time{std::chrono::seconds{last_accepted.block_time}};
            int64_t blocks_left{(current_time - last_accepted_time) / runtime.ConsensusParams().PowTargetSpacing()};
            blocks_left = std::max<int64_t>(0, blocks_left);
            const double progress{100.0 * last_accepted.block.height / (last_accepted.block.height + blocks_left)};
            LogInfo("Synchronizing blockheaders, height: %d (~%.2f%%)\n", last_accepted.block.height, progress);
        }
    }
    return result;
}

static BlockAcceptanceResult BlockAcceptanceResultFromDataAdmission(BlockDataAdmissionResult result, ChainWorkBlockSnapshot block)
{
    switch (result) {
    case BlockDataAdmissionResult::STORE_BLOCK_DATA:
        return BlockAcceptanceResult::Stored(std::move(block));
    case BlockDataAdmissionResult::ALREADY_HAVE_DATA:
        return BlockAcceptanceResult::AlreadyKnown(std::move(block));
    case BlockDataAdmissionResult::UNREQUESTED_PREVIOUSLY_PROCESSED:
        return BlockAcceptanceResult::UnrequestedPreviouslyProcessed(std::move(block));
    case BlockDataAdmissionResult::UNREQUESTED_LESS_WORK_THAN_TIP:
        return BlockAcceptanceResult::UnrequestedLessWorkThanTip(std::move(block));
    case BlockDataAdmissionResult::UNREQUESTED_TOO_FAR_AHEAD:
        return BlockAcceptanceResult::UnrequestedTooFarAhead(std::move(block));
    case BlockDataAdmissionResult::UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK:
        return BlockAcceptanceResult::UnrequestedBelowMinimumChainWork(std::move(block));
    }
    assert(false);
}

static bool StoreBlockData(
    Notifications& notifications,
    BlockDataWriter& block_writer,
    BlockIndexDataReceiver& block_index_data,
    const CBlock& block,
    CBlockIndex& block_index,
    const FlatFilePos* existing_block_pos,
    BlockValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    try {
        FlatFilePos block_pos{};
        if (existing_block_pos) {
            block_pos = *existing_block_pos;
            block_writer.UpdateBlockInfo(block, block_index.nHeight, block_pos);
        } else {
            block_pos = block_writer.WriteBlock(block, block_index.nHeight);
            if (block_pos.IsNull()) {
                return state.Error("AcceptBlock: Failed to find position to write new block to disk");
            }
        }
        block_index_data.MarkBlockDataReceived(block, block_index, block_pos);
    } catch (const std::exception& e) {
        FatalError(notifications, state, strprintf(_("System error while saving block to disk: %s"), e.what()));
        return false;
    } catch (...) {
        FatalError(notifications, state, _("Unknown system error while saving block to disk"));
        return false;
    }
    return true;
}

NewBlockStructuralCheckResult CheckNewBlockStructural(const Consensus::Params& consensus_params, const std::shared_ptr<const CBlock>& block, BlockValidationState& state)
{
    AssertLockNotHeld(cs_main);
    assert(block);

    if (!CheckBlock(*block, state, consensus_params)) {
        return {};
    }
    return {.proof = BlockStructuralCheckProof{.block_hash = block->GetHash()}};
}

BlockAcceptanceResult AcceptBlock(CoreBlockDataAdmissionRuntime& runtime, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time)
{
    const CBlock& block = *pblock;

    AssertLockHeld(cs_main);

    CoreBlockIndexStore block_index{runtime.MakeBlockIndexStore()};
    const CoreBlockHeaderContextProvider header_context{runtime.MakeHeaderContextProvider()};
    const Consensus::Params& consensus_params{runtime.ConsensusParams()};
    const BlockHeaderAcceptanceResult accepted_header{AcceptBlockHeader(block_index, consensus_params, header_context, block, state, options.header, time.MaxFutureBlockTimeSeconds())};
    runtime.CheckBlockIndex();

    if (!accepted_header.accepted) {
        return BlockAcceptanceResult::HeaderRejected(MaybeChainWorkBlockSnapshot(accepted_header.block_index));
    }
    CBlockIndex& block_index_entry{*Assert(accepted_header.block_index)};
    const ChainWorkBlockSnapshot accepted_block{MakeChainWorkBlockSnapshot(block_index_entry)};

    // Core adapter note: node code still decides when to force block-data
    // storage for downloaded blocks.
    // Keep ForceStore until node can ask validation whether a block is in a
    // chain leading to a candidate tip without breaking getblockfrompeer.

    const CBlockIndex* active_tip{runtime.ActiveTip()};
    const BlockDataAdmissionResult block_data_admission{GetBlockDataAdmissionResult({
        .already_have_data = bool(block_index_entry.nStatus & BLOCK_HAVE_DATA),
        .storage_mode = options.block_data_storage,
        .block_data_previously_processed = block_index_entry.nTx != 0,
        .block_height = block_index_entry.nHeight,
        .max_unrequested_height = runtime.ActiveHeight() + int(MIN_BLOCKS_TO_KEEP),
        .block_chain_work = block_index_entry.nChainWork,
        .active_tip_chain_work = active_tip ? std::optional{active_tip->nChainWork} : std::nullopt,
        .minimum_chain_work = runtime.MinimumChainWork(),
    })};
    if (!ShouldStoreBlockData(block_data_admission)) {
        return BlockAcceptanceResultFromDataAdmission(block_data_admission, accepted_block);
    }

    const bool structural_check_required{!options.structural_check || !options.structural_check->Matches(block)};
    if ((structural_check_required && !CheckBlock(block, state, consensus_params)) ||
        !ContextualCheckBlock(block, state, header_context, block_index_entry.pprev)) {
        if (Assume(state.IsInvalid())) {
            runtime.MarkInvalidBlockFound(block_index_entry, state);
        }
        LogError("%s: %s\n", __func__, FormatValidationStateForLog(state));
        return BlockAcceptanceResult::BlockRejected(accepted_block);
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    auto& validation_events{runtime.ValidationEvents()};
    if (!runtime.IsInitialBlockDownload() && runtime.ActiveTip() == block_index_entry.pprev) {
        validation_events.NewPoWValidBlock({
            .block = pblock,
            .block_info = validation::SnapshotCoreValidationBlockInfo(block_index_entry),
        });
    }

    CoreBlockDataStore block_store{runtime.MakeBlockDataStore()};
    if (!StoreBlockData(runtime.Notifications(), block_store, block_index, block, block_index_entry, options.existing_block_pos, state)) {
        return BlockAcceptanceResult::StorageFailed(accepted_block);
    }

    // Core adapter note: Core's runtime still handles mixed block and
    // chainstate flush decisions.
    // For now, since FlushStateMode::NONE is used, all that can happen is that
    // the block files may be pruned, so we can just call this on one
    // chainstate (particularly if we haven't implemented pruning with
    // background validation yet).
    runtime.FlushActiveChainstateToDisk(state, FlushStateMode::NONE);

    runtime.CheckBlockIndex();

    return BlockAcceptanceResult::Stored(accepted_block);
}

BlockAcceptanceResult AcceptNewBlockData(CoreBlockDataAdmissionRuntime& runtime, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time)
{
    AssertLockNotHeld(cs_main);
    assert(block);
    LOCK(cs_main);
    return AcceptBlock(runtime, block, state, options, time);
}

std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(
    const Consensus::Params& consensus_params,
    BlockIndexLookup& block_index,
    const BlockHeaderContextProvider& header_context,
    const uint256& block_hash)
{
    AssertLockNotHeld(cs_main);
    LOCK(cs_main);

    const CBlockIndex* block_index_entry{block_index.LookupBlockIndex(block_hash)};
    if (!block_index_entry) return std::nullopt;

    const Consensus::BlockHeaderContext headers{header_context.BuildContext(block_index_entry->pprev)};
    const bool has_spend_stage{block_index_entry->pprev != nullptr};
    return NewBlockCandidateContextSnapshot{
        .block = MakeChainWorkBlockSnapshot(*block_index_entry),
        .previous_block_hash = block_index_entry->pprev ? block_index_entry->pprev->GetBlockHash() : uint256{},
        .previous_block_height = block_index_entry->pprev ? block_index_entry->pprev->nHeight : -1,
        .previous_median_time_past = headers.PreviousMedianTimePast(),
        .previous_block_time = headers.PreviousBlockTime(),
        .deployments = headers.Deployments(),
        .spend_options = has_spend_stage ? BuildCoreBlockSpendConsensusOptions(*block_index_entry, consensus_params, headers.Deployments()) : Consensus::BlockSpendConsensusOptions{},
        .block_subsidy = Consensus::CalculateBlockSubsidy(block_index_entry->nHeight, consensus_params),
        .has_spend_stage = has_spend_stage,
    };
}

std::optional<NewBlockCandidateContextSnapshot> SnapshotAcceptedBlockContext(CoreAcceptedContextReader& reader, const uint256& block_hash)
{
    CoreBlockIndexStore block_index{reader.MakeBlockIndexStore()};
    const CoreBlockHeaderContextProvider header_context{reader.MakeHeaderContextProvider()};
    return SnapshotAcceptedBlockContext(reader.ConsensusParams(), block_index, header_context, block_hash);
}

BlockActivationResult ActivateAcceptedBlock(CoreActivationRuntime& runtime, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockValidationTime time)
{
    AssertLockNotHeld(cs_main);
    assert(block);

    (void)runtime.NotifyHeaderTip();
    return runtime.ActivateBestChain(state, time.CurrentTime(), block, chain_events);
}

BlockActivationResult ActivateAcceptedTipCandidate(CoreActivationRuntime& runtime, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, BlockValidationState& state, BlockValidationTime time)
{
    AssertLockNotHeld(cs_main);
    assert(block);

    (void)runtime.NotifyHeaderTip();

    CBlockIndex* block_index{nullptr};
    {
        LOCK(cs_main);
        CoreBlockIndexStore block_index_store{runtime.MakeBlockIndexStore()};
        block_index = block_index_store.LookupBlockIndex(block->GetHash());
    }
    if (!block_index) return BlockActivationResult::Completed();

    return runtime.ActivateMostWorkTipBlock(state, time.CurrentTime(), *block_index, block, chain_events);
}

void ReportBlockChecked(validation::ValidationEventQueue& events, const std::shared_ptr<const CBlock>& block, const BlockValidationState& state)
{
    LOCK(cs_main);
    events.BlockChecked({.block = block, .state = state});
}

static void FinishNewBlockProcessingTiming(NewBlockProcessingResult& result, std::chrono::steady_clock::time_point start) noexcept
{
    result.timings.total = std::chrono::steady_clock::now() - start;
}

NewBlockProcessingResult ProcessNewBlock(
    CoreBlockDataAdmissionRuntime& admission_runtime,
    CoreAcceptedContextReader& context_reader,
    CoreActivationRuntime& activation_runtime,
    ChainstateEventSink* chain_events,
    const std::shared_ptr<const CBlock>& block,
    NewBlockProcessingOptions options,
    BlockValidationTime time)
{
    AssertLockNotHeld(cs_main);

    const auto total_start{std::chrono::steady_clock::now()};
    NewBlockProcessingResult result{};
    BlockValidationState state;
    // Skipping AcceptBlock() for CheckBlock() failures means that we will never mark a block as invalid if
    // CheckBlock() fails.  This is protective against consensus failure if there are any unknown forms of block
    // malleability that cause CheckBlock() to fail; see e.g. CVE-2012-2459 and
    // https://lists.linuxfoundation.org/pipermail/bitcoin-dev/2019-February/016697.html.  Because CheckBlock() is
    // not very expensive, the anti-DoS benefits of caching failure (of a definitely-invalid block) are not substantial.
    const bool has_structural_proof{options.structural_check && options.structural_check->Matches(*block)};
    const auto structural_start{std::chrono::steady_clock::now()};
    const NewBlockStructuralCheckResult structural_check{
        has_structural_proof ? NewBlockStructuralCheckResult{.proof = options.structural_check} :
                               CheckNewBlockStructural(admission_runtime.ConsensusParams(), block, state)};
    result.timings.structural_check = has_structural_proof ? std::chrono::nanoseconds{0} :
                                                             std::chrono::steady_clock::now() - structural_start;
    if (!structural_check.passed()) {
        result.MarkStructuralRejected(state);
        ReportBlockChecked(activation_runtime.ValidationEvents(), block, state);
        LogError("%s: AcceptBlock FAILED (%s)\n", __func__, FormatValidationStateForLog(state));
        FinishNewBlockProcessingTiming(result, total_start);
        return result;
    }

    const auto accept_start{std::chrono::steady_clock::now()};
    const BlockAcceptanceResult acceptance{AcceptNewBlockData(
        admission_runtime,
        block,
        state,
        {
            .block_data_storage = options.block_data_storage,
            .header = options.header,
            .structural_check = structural_check.proof,
        },
        time)};
    result.timings.block_acceptance = std::chrono::steady_clock::now() - accept_start;
    if (!acceptance.ShouldAttemptActivation()) {
        result.MarkNotAccepted(acceptance, state);
        ReportBlockChecked(activation_runtime.ValidationEvents(), block, state);
        LogError("%s: AcceptBlock FAILED (%s)\n", __func__, FormatValidationStateForLog(state));
        FinishNewBlockProcessingTiming(result, total_start);
        return result;
    }
    result.MarkAcceptedCandidate(acceptance);

    const auto snapshot_start{std::chrono::steady_clock::now()};
    result.SetCandidateContext(SnapshotAcceptedBlockContext(context_reader, block->GetHash()));
    result.timings.context_snapshot = std::chrono::steady_clock::now() - snapshot_start;

    BlockValidationState activate_state; // Only used to report errors, not invalidity - ignore it
    const auto activation_start{std::chrono::steady_clock::now()};
    const BlockActivationResult activation{ActivateAcceptedBlock(activation_runtime, chain_events, block, activate_state, time)};
    result.timings.activation = std::chrono::steady_clock::now() - activation_start;
    result.timings.spend_join = activation.timings.spend_join;
    result.timings.script_validation = activation.timings.script_validation;
    result.activated_blocks = activation.connected_blocks;
    if (!activation.Succeeded()) {
        result.MarkActivationFailed(activate_state);
        LogError("%s: ActivateBestChain failed (%s)\n", __func__, FormatValidationStateForLog(activate_state));
        FinishNewBlockProcessingTiming(result, total_start);
        return result;
    }

    result.MarkProcessed();
    FinishNewBlockProcessingTiming(result, total_start);
    return result;
}

NewBlockProcessingResult ProcessNewBlock(CoreBlockDataAdmissionRuntime& admission_runtime, CoreAcceptedContextReader& context_reader, CoreActivationRuntime& activation_runtime, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time)
{
    return ProcessNewBlock(admission_runtime, context_reader, activation_runtime, /*chain_events=*/nullptr, block, options, time);
}

BlockValidationState TestBlockValidity(
    TestBlockValidityRequest request,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time)
{
    // Keep the tip stable while the block is checked against the current chain.
    AssertLockHeld(cs_main);

    BlockValidationState state;
    CBlockIndex* tip{Assert(request.active_chain.Tip())};

    if (block.hashPrevBlock != *Assert(tip->phashBlock)) {
        state.Invalid({}, "inconclusive-not-best-prevblk");
        return state;
    }

    // For signets CheckBlock() verifies the challenge iff check_pow is set.
    if (!CheckBlock(block, state, request.consensus_params, options)) {
        // This should never happen, but belt-and-suspenders don't approve the
        // block if it does.
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    /**
     * At this point ProcessNewBlock would call AcceptBlock(), but we
     * don't want to store the block or its header. Run individual checks
     * instead:
     * - skip AcceptBlockHeader() because:
     *   - we don't want to update the block index
     *   - we do not care about duplicates
     *   - we already ran CheckBlockHeader() via CheckBlock()
     *   - we already checked for prev-blk-not-found
     *   - we know the tip is valid, so no need to check bad-prevblk
     * - we already ran CheckBlock()
     * - do run ContextualCheckBlockHeader()
     * - do run ContextualCheckBlock()
     */

    if (!ContextualCheckBlockHeader(block, state, request.consensus_params, request.header_context, tip, time.MaxFutureBlockTimeSeconds())) {
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    if (!ContextualCheckBlock(block, state, request.header_context, tip)) {
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    // Test validation uses a dummy block index. The caller supplies the
    // block-local connection state so this path is not tied to Core's coins
    // cache implementation.
    CBlockIndex index_dummy{block};
    uint256 block_hash(block.GetHash());
    index_dummy.pprev = tip;
    index_dummy.nHeight = tip->nHeight + 1;
    index_dummy.phashBlock = &block_hash;

    // Test validation uses the normal connection path and discards the commit
    // package. It may update reusable script caches, but staged coin effects
    // stay local to the caller-supplied connection attempt.
    const BlockConnectionOptions connect_options{
        .block_check_options = Consensus::BlockCheckOptions{
            .check_pow = false,
            .check_merkle_root = false,
        },
    };

    const Consensus::BlockHeaderContext headers{request.header_context.BuildContext(tip)};
    const validation::BlockConnectionContext connection_context{
        .consensus_params = request.consensus_params,
        .consensus_context = Consensus::BuildBlockConsensusContext(
            headers,
            block_hash,
            Consensus::CalculateBlockSubsidy(index_dummy.nHeight, request.consensus_params)),
        .sequence_lock_times = request.sequence_lock_times ? request.sequence_lock_times : std::make_shared<validation::CoinsViewSequenceLockTimeView>(index_dummy),
        .spend_options = BuildCoreBlockSpendConsensusOptions(index_dummy, request.consensus_params, headers.Deployments()),
    };
    const validation::BlockConnectionRequest connection_request{
        .runtime = {
            .notifications = request.notifications,
            .script_checker = request.script_checker,
            .trace = request.trace,
        },
        .context = connection_context,
        .block = block,
        .block_position = {
            .hash = block_hash,
            .parent_hash = tip->GetBlockHash(),
            .height = index_dummy.nHeight,
        },
        .connection_state = request.connection_state,
        .options = connect_options,
    };
    const validation::BlockConnectionResult connected{validation::BlockConnectionEngine{}.ConnectPrepared(connection_request, state)};
    if (!connected.Succeeded()) {
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    // Ensure no check returned successfully while also setting an invalid state.
    if (!state.IsValid()) NONFATAL_UNREACHABLE();

    return state;
}

BlockValidationState TestBlockValidity(
    Chainstate& chainstate,
    const CBlock& block,
    const Consensus::BlockCheckOptions& options,
    BlockValidationTime time)
{
    // Keep the tip stable while the block is checked against the current chain.
    AssertLockHeld(chainstate.m_chainman.GetMutex());

    CoreTestBlockValidityRuntime runtime{chainstate.m_chainman};
    CoreBlockHeaderContextProvider header_context{runtime.MakeHeaderContextProvider()};
    CoreBlockIndexStore block_index_store{runtime.MakeBlockIndexStore()};
    CoreActiveChainView active_chain{chainstate.m_chain};
    bool run_script_checks{true};
    CBlockIndex* tip{Assert(active_chain.Tip())};
    if (block.hashPrevBlock == *Assert(tip->phashBlock)) {
        uint256 block_hash{block.GetHash()};
        CBlockIndex script_policy_index{block};
        script_policy_index.pprev = tip;
        script_policy_index.nHeight = tip->nHeight + 1;
        script_policy_index.phashBlock = &block_hash;
        run_script_checks = DetermineCoreBlockScriptChecks(
                                {
                                    .assumed_valid_block = runtime.AssumedValidBlock(),
                                    .best_header = runtime.BestHeader(),
                                    .minimum_chain_work = runtime.MinimumChainWork(),
                                },
                                block_index_store,
                                script_policy_index,
                                chainstate.m_chainman.GetConsensus())
                                .run_script_checks;
    }
    CCoinsViewCache view_dummy(&chainstate.CoinsTip());
    validation::CoreCoinsBlockConnectionState connection_state{view_dummy};
    CoreBlockScriptChecks script_checks{
        runtime.ScriptTaskExecutor(),
        run_script_checks,
        /*cache_results=*/true,
        runtime.ScriptValidationCache()};
    BlockConnectionTrace trace{runtime.TraceCounters()};
    TestBlockValidityRequest request{
        .active_chain = active_chain,
        .consensus_params = runtime.ConsensusParams(),
        .header_context = header_context,
        .connection_state = connection_state,
        .notifications = runtime.Notifications(),
        .script_checker = script_checks.Checker(),
        .trace = trace,
    };
    return TestBlockValidity(request, block, options, time);
}

CVerifyDB::CVerifyDB(Notifications& notifications)
    : m_notifications{notifications}
{
    m_notifications.progress(_("Verifying blocks…"), 0, false);
}

CVerifyDB::~CVerifyDB()
{
    m_notifications.progress(bilingual_str{}, 100, false);
}

CoreVerifyDBCoins::CoreVerifyDBCoins(
    CCoinsView& coins_view,
    CCoinsViewCache& coins_tip,
    size_t coins_tip_cache_size_bytes)
    : m_coins_view{coins_view},
      m_coins_tip{coins_tip},
      m_coins_tip_cache_size_bytes{coins_tip_cache_size_bytes}
{
}

std::unique_ptr<CCoinsViewCache> CoreVerifyDBCoins::MakeCache()
{
    return std::make_unique<CCoinsViewCache>(&m_coins_view);
}

size_t CoreVerifyDBCoins::TipMemoryUsage() const
{
    return m_coins_tip.DynamicMemoryUsage();
}

size_t CoreVerifyDBCoins::CacheBudgetBytes() const
{
    return m_coins_tip_cache_size_bytes;
}

namespace {

VerifyDBBlock SnapshotVerifyDBBlock(const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    AssertLockHeld(::cs_main);
    return {
        .replay = SnapshotCoreBlockReplayBlock(block_index),
        .status = block_index.nStatus,
    };
}

bool SameVerifyDBBlock(const std::optional<VerifyDBBlock>& a, const std::optional<VerifyDBBlock>& b)
{
    if (!a || !b) return !a && !b;
    return a->replay.hash == b->replay.hash;
}

} // namespace

CoreVerifyDBChain::CoreVerifyDBChain(const CChain& chain)
    : m_chain{chain}
{
}

std::optional<VerifyDBBlock> CoreVerifyDBChain::Tip() const
{
    if (const CBlockIndex* tip{m_chain.Tip()}) return SnapshotVerifyDBBlock(*tip);
    return std::nullopt;
}

int CoreVerifyDBChain::Height() const
{
    return m_chain.Height();
}

std::optional<VerifyDBBlock> CoreVerifyDBChain::Previous(const VerifyDBBlock& block) const
{
    const CBlockIndex* block_index{m_chain[block.replay.height]};
    if (!block_index || block_index->GetBlockHash() != block.replay.hash || !block_index->pprev) return std::nullopt;
    return SnapshotVerifyDBBlock(*block_index->pprev);
}

std::optional<VerifyDBBlock> CoreVerifyDBChain::Next(const VerifyDBBlock& block) const
{
    const CBlockIndex* block_index{m_chain[block.replay.height]};
    if (!block_index || block_index->GetBlockHash() != block.replay.hash) return std::nullopt;
    if (const CBlockIndex* next{m_chain.Next(*block_index)}) return SnapshotVerifyDBBlock(*next);
    return std::nullopt;
}

CBlockIndex* CoreVerifyDBChain::CoreBlockIndexForConnection(const VerifyDBBlock& block) const
{
    CBlockIndex* block_index{m_chain[block.replay.height]};
    if (!block_index || block_index->GetBlockHash() != block.replay.hash) return nullptr;
    return block_index;
}

VerifyDBResult CVerifyDB::VerifyDB(
    VerifyDBRequest request,
    int nCheckLevel, int nCheckDepth)
{
    AssertLockHeld(cs_main);

    std::optional<VerifyDBBlock> tip{request.chain.Tip()};
    if (!tip || tip->replay.height <= 0) {
        return VerifyDBResult::SUCCESS;
    }

    // Verify blocks in the best chain
    const int chain_height{request.chain.Height()};
    if (nCheckDepth <= 0 || nCheckDepth > chain_height) {
        nCheckDepth = chain_height;
    }
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogInfo("Verifying last %i blocks at level %i", nCheckDepth, nCheckLevel);
    std::unique_ptr<CCoinsViewCache> coins{request.coins.MakeCache()};
    std::optional<VerifyDBBlock> pindex;
    std::optional<VerifyDBBlock> pindexFailure;
    int nGoodTransactions = 0;
    BlockValidationState state;
    int reportDone = 0;
    bool skipped_no_block_data{false};
    bool skipped_l3_checks{false};
    const CoreBlockConnectionRuntimeInputs runtime_inputs{
        MakeCoreBlockConnectionRuntimeInputs(request.replay_runtime, request.replay_runtime.ScriptTaskExecutor())};
    LogInfo("Verification progress: 0%%");

    for (pindex = tip; pindex && pindex->replay.height > 0; pindex = request.chain.Previous(*pindex)) {
        const int percentageDone = std::max(1, std::min(99, (int)(((double)(chain_height - pindex->replay.height)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone / 10) {
            // report every 10% step
            LogInfo("Verification progress: %d%%", percentageDone);
            reportDone = percentageDone / 10;
        }
        m_notifications.progress(_("Verifying blocks…"), percentageDone, false);
        if (pindex->replay.height <= chain_height - nCheckDepth) {
            break;
        }
        if (request.block_data_availability.IsPruneMode() && !(pindex->status & BLOCK_HAVE_DATA)) {
            // If pruning, only go back as far as we have data.
            LogInfo("Block verification stopping at height %d (no data). This could be due to pruning.", pindex->replay.height);
            skipped_no_block_data = true;
            break;
        }
        // check level 0: read from disk
        auto block_result{request.block_reader.ReadBlock(pindex->replay.block_read)};
        if (!block_result) {
            LogError("Verification error: ReadBlock failed at %d, hash=%s", pindex->replay.height, pindex->replay.hash.ToString());
            return VerifyDBResult::CORRUPTED_BLOCK_DB;
        }
        CBlock block{std::move(*block_result)};
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, request.consensus_params)) {
            LogError("Verification error: found bad block at %d, hash=%s (%s)",
                     pindex->replay.height, pindex->replay.hash.ToString(), FormatValidationStateForLog(state));
            return VerifyDBResult::CORRUPTED_BLOCK_DB;
        }
        // check level 2: verify undo validity
        if (nCheckLevel >= 2) {
            if (pindex->replay.undo_read && !pindex->replay.undo_read->position.IsNull()) {
                auto undo_result{request.undo_reader.ReadBlockUndo(*pindex->replay.undo_read)};
                if (!undo_result) {
                    LogError("Verification error: found bad undo data at %d, hash=%s", pindex->replay.height, pindex->replay.hash.ToString());
                    return VerifyDBResult::CORRUPTED_BLOCK_DB;
                }
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        const size_t curr_coins_usage{coins->DynamicMemoryUsage() + request.coins.TipMemoryUsage()};

        if (nCheckLevel >= 3) {
            if (curr_coins_usage <= request.coins.CacheBudgetBytes()) {
                assert(coins->GetBestBlock() == pindex->replay.hash);
                DisconnectResult res = DisconnectBlock(request.undo_reader, block, pindex->replay, *coins);
                if (res == DISCONNECT_FAILED) {
                    LogError("Verification error: irrecoverable inconsistency in block data at %d, hash=%s", pindex->replay.height, pindex->replay.hash.ToString());
                    return VerifyDBResult::CORRUPTED_BLOCK_DB;
                }
                if (res == DISCONNECT_UNCLEAN) {
                    nGoodTransactions = 0;
                    pindexFailure = pindex;
                } else {
                    nGoodTransactions += block.vtx.size();
                }
            } else {
                skipped_l3_checks = true;
            }
        }
        if (request.interrupt) return VerifyDBResult::INTERRUPTED;
    }
    if (pindexFailure) {
        LogError("Verification error: coin database inconsistencies found (last %i blocks, %i good transactions before that)", chain_height - pindexFailure->replay.height + 1, nGoodTransactions);
        return VerifyDBResult::CORRUPTED_BLOCK_DB;
    }
    if (skipped_l3_checks) {
        LogWarning("Skipped verification of level >=3 (insufficient database cache size). Consider increasing -dbcache.");
    }

    // store block count as we move pindex at check level >= 4
    int block_count = chain_height - (pindex ? pindex->replay.height : 0);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4 && !skipped_l3_checks) {
        while (!SameVerifyDBBlock(pindex, tip)) {
            if (!pindex) {
                LogError("Verification error: missing reconnect start block");
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            const int percentageDone = std::max(1, std::min(99, 100 - (int)(((double)(chain_height - pindex->replay.height)) / (double)nCheckDepth * 50)));
            if (reportDone < percentageDone / 10) {
                // report every 10% step
                LogInfo("Verification progress: %d%%", percentageDone);
                reportDone = percentageDone / 10;
            }
            m_notifications.progress(_("Verifying blocks…"), percentageDone, false);
            pindex = request.chain.Next(*pindex);
            if (!pindex) {
                LogError("Verification error: missing next block during reconnect");
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            auto block_result{request.block_reader.ReadBlock(pindex->replay.block_read)};
            if (!block_result) {
                LogError("Verification error: ReadBlock failed at %d, hash=%s", pindex->replay.height, pindex->replay.hash.ToString());
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            CBlock block{std::move(*block_result)};
            CBlockIndex* block_index{request.chain.CoreBlockIndexForConnection(*pindex)};
            if (!block_index) {
                LogError("Verification error: missing Core block index at %d, hash=%s", pindex->replay.height, pindex->replay.hash.ToString());
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            BlockConnectionTrace trace{request.replay_runtime.TraceCounters()};
            CoreBlockConnectionPlan connection_plan{PlanCoreBlockConnection(
                request.replay_runtime.SnapshotConnectionPolicy(*block_index),
                request.block_index_lookup,
                *block_index)};
            MaybeLogCoreBlockConnectionScriptPolicy(
                request.last_script_check_reason_logged,
                *block_index,
                block.GetHash(),
                connection_plan);
            CoreBlockConnectionSetup connection_setup{
                runtime_inputs,
                std::move(connection_plan),
                trace,
                /*cache_script_results=*/false};
            validation::CoreCoinsBlockConnectionState connection_state{*coins};
            CoreBlockSpendEffectsCommitter spend_state_committer{*coins};
            CoreBlockConnectionCommitTarget commit_target{
                request.undo_writer,
                request.block_index_committer,
                connection_state,
                *block_index};
            const validation::BlockConnectionRequest connection_request{connection_setup.Request(block, connection_state)};
            validation::BlockConnectionEngine engine;
            auto connected{engine.ConnectPrepared(connection_request, state)};
            if (!connected.Succeeded()) {
                LogError("Verification error: found unconnectable block at %d, hash=%s (%s)", pindex->replay.height, pindex->replay.hash.ToString(), FormatValidationStateForLog(state));
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            assert(connected.commit_package);
            const validation::BlockConnectionCommitRequest commit_request{
                .runtime = {
                    .revert_data_writer = commit_target.RevertDataWriter(),
                    .spend_state_committer = spend_state_committer,
                    .metadata_committer = commit_target.MetadataCommitter(),
                    .trace = trace,
                },
                .context = {
                    .block = block,
                    .block_position = commit_target.BlockPosition(),
                    .connection_state = connection_state,
                },
            };
            if (!engine.Commit(commit_request, std::move(*connected.commit_package), state).Succeeded()) {
                LogError("Verification error: failed to commit block at %d, hash=%s (%s)", pindex->replay.height, pindex->replay.hash.ToString(), FormatValidationStateForLog(state));
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            if (request.interrupt) return VerifyDBResult::INTERRUPTED;
        }
    }

    LogInfo("Verification: No coin database inconsistencies in last %i blocks (%i transactions)", block_count, nGoodTransactions);

    if (skipped_l3_checks) {
        return VerifyDBResult::SKIPPED_L3_CHECKS;
    }
    if (skipped_no_block_data) {
        return VerifyDBResult::SKIPPED_MISSING_BLOCKS;
    }
    return VerifyDBResult::SUCCESS;
}

VerifyDBResult CVerifyDB::VerifyDB(
    Chainstate& chainstate,
    const Consensus::Params& consensus_params,
    CCoinsView& coinsview,
    int nCheckLevel, int nCheckDepth)
{
    AssertLockHeld(cs_main);

    CoreBlockDataStore block_store{chainstate.m_blockman};
    CoreBlockIndexStore block_index_store{chainstate.m_chainman};
    CoreReplayRuntime runtime{chainstate.m_chainman};
    CoreVerifyDBChain verify_chain{chainstate.m_chain};
    CoreVerifyDBCoins verify_coins{
        coinsview,
        chainstate.CoinsTip(),
        chainstate.m_coinstip_cache_size_bytes};
    VerifyDBRequest request{
        .chain = verify_chain,
        .consensus_params = consensus_params,
        .coins = verify_coins,
        .block_reader = block_store,
        .undo_reader = block_store,
        .undo_writer = block_store,
        .block_data_availability = block_store,
        .block_index_lookup = block_index_store,
        .block_index_committer = block_index_store,
        .replay_runtime = runtime,
        .last_script_check_reason_logged = chainstate.LastScriptCheckReasonLogged(),
        .interrupt = chainstate.m_chainman.m_interrupt,
    };
    return VerifyDB(request, nCheckLevel, nCheckDepth);
}
