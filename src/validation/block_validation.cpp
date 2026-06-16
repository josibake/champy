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
#include <validation/core_block_connection_setup.h>
#include <validation/core_chain_validation_context.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/script_check_scheduler.h>
#include <validation/validation_event_queue.h>
#include <validation_state.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
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

static CoreBlockConnectionRuntimeInputs MakeCoreBlockConnectionRuntimeInputs(
    CoreChainValidationContext& context,
    BlockUndoWriter& undo_writer,
    BlockIndexValidityCommitter& block_index_committer,
    validation::ScriptCheckScheduler& script_check_scheduler) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    AssertLockHeld(cs_main);

    return {
        .notifications = context.Notifications(),
        .undo_writer = undo_writer,
        .block_index_committer = block_index_committer,
        .script_check_scheduler = script_check_scheduler,
        .validation_cache = context.ScriptValidationCache(),
        .trace_counters = context.TraceCounters(),
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
        LogDebug(BCLog::VALIDATION, "Block mutated: %s\n", state.ToString());
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
        LogDebug(BCLog::VALIDATION, "Block mutated: %s\n", state.ToString());
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

/** Context-dependent validity checks.
 *  By "context", we mean only the previous block headers, but not the UTXO
 *  set; UTXO-related validity checks are done during block connection.
 *  NOTE: This function is not currently invoked by block connection, so we
 *  should consider upgrade issues if we change which consensus rules are
 *  enforced in this function (eg by adding a new consensus rule). See comment
 *  during block connection.
 *  Note that -reindex-chainstate skips the validation that happens here!
 *
 *  NOTE: failing to check the header's height against the last checkpoint's opened a DoS vector between
 *  v0.12 and v0.15 (when no additional protection was in place) whereby an attacker could unboundedly
 *  grow our in-memory block index. See https://bitcoincore.org/en/2024/07/03/disclose-header-spam.
 */
static int64_t MaxFutureBlockTime(NodeClock::time_point now)
{
    return TicksSinceEpoch<std::chrono::seconds>(now + std::chrono::seconds{MAX_FUTURE_BLOCK_TIME});
}

BlockValidationTime CurrentBlockValidationTime()
{
    const auto now{NodeClock::now()};
    return {
        .current_time_seconds = TicksSinceEpoch<std::chrono::seconds>(now),
        .max_future_block_time = MaxFutureBlockTime(now),
    };
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
            LogDebug(BCLog::VALIDATION, "%s: Consensus::CheckBlockHeader: %s, %s\n", __func__, hash.ToString(), state.ToString());
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
            LogDebug(BCLog::VALIDATION, "%s: Consensus::ContextualCheckBlockHeader: %s, %s\n", __func__, hash.ToString(), state.ToString());
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
NewBlockHeadersResult ProcessNewBlockHeaders(CoreChainValidationContext& context, std::span<const CBlockHeader> headers, BlockHeaderAcceptanceOptions options, BlockValidationTime time, BlockValidationState& state)
{
    AssertLockNotHeld(cs_main);
    NewBlockHeadersResult result{.accepted = true};
    {
        LOCK(cs_main);
        CoreBlockIndexStore block_index{context.MakeBlockIndexStore()};
        const CoreBlockHeaderContextProvider header_context{context.MakeHeaderContextProvider()};
        const Consensus::Params& consensus_params{context.ConsensusParams()};
        for (const CBlockHeader& header : headers) {
            const BlockHeaderAcceptanceResult accepted_header{AcceptBlockHeader(block_index, consensus_params, header_context, header, state, options, time.max_future_block_time)};
            context.CheckBlockIndex();

            if (!accepted_header.accepted) {
                return {.last_accepted = result.last_accepted};
            }
            result.last_accepted = MakeAcceptedBlockHeaderSnapshot(*Assert(accepted_header.block_index));
        }
    }
    if (context.NotifyHeaderTip()) {
        if (context.IsInitialBlockDownload() && result.last_accepted) {
            const AcceptedBlockHeaderSnapshot& last_accepted{*result.last_accepted};
            const NodeSeconds current_time{std::chrono::seconds{time.current_time_seconds}};
            const NodeSeconds last_accepted_time{std::chrono::seconds{last_accepted.block_time}};
            int64_t blocks_left{(current_time - last_accepted_time) / context.ConsensusParams().PowTargetSpacing()};
            blocks_left = std::max<int64_t>(0, blocks_left);
            const double progress{100.0 * last_accepted.block.height / (last_accepted.block.height + blocks_left)};
            LogInfo("Synchronizing blockheaders, height: %d (~%.2f%%)\n", last_accepted.block.height, progress);
        }
    }
    return result;
}

static BlockAcceptanceStatus BlockAcceptanceStatusFromDataAdmission(BlockDataAdmissionResult result)
{
    switch (result) {
    case BlockDataAdmissionResult::STORE_BLOCK_DATA:
        return BlockAcceptanceStatus::BlockDataStored;
    case BlockDataAdmissionResult::ALREADY_HAVE_DATA:
        return BlockAcceptanceStatus::BlockDataAlreadyKnown;
    case BlockDataAdmissionResult::UNREQUESTED_PREVIOUSLY_PROCESSED:
        return BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed;
    case BlockDataAdmissionResult::UNREQUESTED_LESS_WORK_THAN_TIP:
        return BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip;
    case BlockDataAdmissionResult::UNREQUESTED_TOO_FAR_AHEAD:
        return BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead;
    case BlockDataAdmissionResult::UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK:
        return BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork;
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
    } catch (const std::runtime_error& e) {
        FatalError(notifications, state, strprintf(_("System error while saving block to disk: %s"), e.what()));
        return false;
    }
    return true;
}

BlockAcceptanceResult AcceptBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& pblock, BlockValidationState& state, BlockAcceptanceOptions options, BlockValidationTime time)
{
    const CBlock& block = *pblock;

    AssertLockHeld(cs_main);

    CoreBlockIndexStore block_index{context.MakeBlockIndexStore()};
    const CoreBlockHeaderContextProvider header_context{context.MakeHeaderContextProvider()};
    const Consensus::Params& consensus_params{context.ConsensusParams()};
    const BlockHeaderAcceptanceResult accepted_header{AcceptBlockHeader(block_index, consensus_params, header_context, block, state, options.header, time.max_future_block_time)};
    context.CheckBlockIndex();

    if (!accepted_header.accepted) {
        return {.block = MaybeChainWorkBlockSnapshot(accepted_header.block_index)};
    }
    CBlockIndex& block_index_entry{*Assert(accepted_header.block_index)};
    const ChainWorkBlockSnapshot accepted_block{MakeChainWorkBlockSnapshot(block_index_entry)};

    // Compatibility note: node code still decides when to force block-data
    // storage for downloaded blocks; see doc/legacy-compatibility.md.
    // Keep ForceStore until node can ask validation whether a block is in a
    // chain leading to a candidate tip without breaking getblockfrompeer.

    const CBlockIndex* active_tip{context.ActiveTip()};
    const BlockDataAdmissionResult block_data_admission{GetBlockDataAdmissionResult({
        .already_have_data = bool(block_index_entry.nStatus & BLOCK_HAVE_DATA),
        .storage_mode = options.block_data_storage,
        .block_data_previously_processed = block_index_entry.nTx != 0,
        .block_height = block_index_entry.nHeight,
        .max_unrequested_height = context.ActiveHeight() + int(MIN_BLOCKS_TO_KEEP),
        .block_chain_work = block_index_entry.nChainWork,
        .active_tip_chain_work = active_tip ? std::optional{active_tip->nChainWork} : std::nullopt,
        .minimum_chain_work = context.MinimumChainWork(),
    })};
    if (!ShouldStoreBlockData(block_data_admission)) {
        return {.status = BlockAcceptanceStatusFromDataAdmission(block_data_admission), .block = accepted_block};
    }

    if (!CheckBlock(block, state, consensus_params) ||
        !ContextualCheckBlock(block, state, header_context, block_index_entry.pprev)) {
        if (Assume(state.IsInvalid())) {
            context.MarkInvalidBlockFound(block_index_entry, state);
        }
        LogError("%s: %s\n", __func__, state.ToString());
        return {.status = BlockAcceptanceStatus::BlockRejected, .block = accepted_block};
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    auto& validation_events{context.ValidationEvents()};
    if (!context.IsInitialBlockDownload() && context.ActiveTip() == block_index_entry.pprev) {
        validation_events.NewPoWValidBlock(&block_index_entry, pblock);
    }

    CoreBlockDataStore block_store{context.MakeBlockDataStore()};
    if (!StoreBlockData(context.Notifications(), block_store, block_index, block, block_index_entry, options.existing_block_pos, state)) {
        return {.status = BlockAcceptanceStatus::StorageFailed, .block = accepted_block};
    }

    // Compatibility note: Core's runtime still handles mixed block and
    // chainstate flush decisions; see doc/legacy-compatibility.md.
    // For now, since FlushStateMode::NONE is used, all that can happen is that
    // the block files may be pruned, so we can just call this on one
    // chainstate (particularly if we haven't implemented pruning with
    // background validation yet).
    context.FlushActiveChainstateToDisk(state, FlushStateMode::NONE);

    context.CheckBlockIndex();

    return {.status = BlockAcceptanceStatus::BlockDataStored, .block = accepted_block};
}

NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, ChainstateEventSink* chain_events, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time)
{
    AssertLockNotHeld(cs_main);

    NewBlockProcessingResult result{};
    {
        BlockValidationState state;
        LOCK(cs_main);

        // Skipping AcceptBlock() for CheckBlock() failures means that we will never mark a block as invalid if
        // CheckBlock() fails.  This is protective against consensus failure if there are any unknown forms of block
        // malleability that cause CheckBlock() to fail; see e.g. CVE-2012-2459 and
        // https://lists.linuxfoundation.org/pipermail/bitcoin-dev/2019-February/016697.html.  Because CheckBlock() is
        // not very expensive, the anti-DoS benefits of caching failure (of a definitely-invalid block) are not substantial.
        bool ret = CheckBlock(*block, state, context.ConsensusParams());
        if (ret) {
            // Store to disk
            const BlockAcceptanceResult acceptance{AcceptBlock(
                context,
                block,
                state,
                {.block_data_storage = options.block_data_storage, .header = options.header},
                time)};
            result.block_acceptance_status = acceptance.status;
            ret = acceptance.accepted_for_processing();
            if (!ret) {
                result.status = NewBlockProcessingStatus::BlockNotAccepted;
            }
        }
        if (!ret) {
            auto& validation_events{context.ValidationEvents()};
            validation_events.BlockChecked(block, state);
            LogError("%s: AcceptBlock FAILED (%s)\n", __func__, state.ToString());
            return result;
        }
        result.status = NewBlockProcessingStatus::ActivationFailed;
    }

    context.NotifyHeaderTip();

    BlockValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!context.ActivateBestChain(state, block, chain_events)) {
        LogError("%s: ActivateBestChain failed (%s)\n", __func__, state.ToString());
        return result;
    }

    result.status = NewBlockProcessingStatus::Processed;
    return result;
}

NewBlockProcessingResult ProcessNewBlock(CoreChainValidationContext& context, const std::shared_ptr<const CBlock>& block, NewBlockProcessingOptions options, BlockValidationTime time)
{
    return ProcessNewBlock(context, /*chain_events=*/nullptr, block, options, time);
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

    if (!ContextualCheckBlockHeader(block, state, request.consensus_params, request.header_context, tip, time.max_future_block_time)) {
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    if (!ContextualCheckBlock(block, state, request.header_context, tip)) {
        if (state.IsValid()) NONFATAL_UNREACHABLE();
        return state;
    }

    // We don't want test validation to update the actual chainstate, so create
    // a cache on top of it, along with a dummy block index.
    CBlockIndex index_dummy{block};
    uint256 block_hash(block.GetHash());
    index_dummy.pprev = tip;
    index_dummy.nHeight = tip->nHeight + 1;
    index_dummy.phashBlock = &block_hash;
    CCoinsViewCache view_dummy(&request.coins_tip);
    validation::CoreCoinsBlockConnectionState connection_state{view_dummy};

    // Test validation uses the normal connection path with commit disabled. It
    // may update reusable script caches, but staged coin effects stay local to
    // view_dummy.
    const BlockConnectionOptions connect_options{
        .block_check_options = Consensus::BlockCheckOptions{
            .check_pow = false,
            .check_merkle_root = false,
        },
        .commit = false,
    };
    const CoreBlockConnectionRuntimeInputs runtime_inputs{
        MakeCoreBlockConnectionRuntimeInputs(request.validation_context, request.undo_writer, request.block_index_committer, request.script_check_scheduler)};
    CoreBlockConnectionSetup connection_setup{
        runtime_inputs,
        PlanCoreBlockConnection(SnapshotCoreBlockConnectionPolicy(request.validation_context, index_dummy), request.block_index_lookup, index_dummy),
        index_dummy,
        /*cache_script_results=*/true};
    connection_setup.MaybeLogScriptPolicy(request.last_script_check_reason_logged, block_hash);
    const validation::BlockConnectionRequest connection_request{connection_setup.Request(block, connection_state, connect_options)};
    if (!validation::BlockConnectionEngine{}.Connect(connection_request, state).Succeeded()) {
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

    CoreBlockHeaderContextProvider header_context{chainstate.m_chainman};
    CoreBlockDataStore block_store{chainstate.m_blockman};
    CoreBlockIndexStore block_index_store{chainstate.m_chainman};
    CoreChainValidationRuntime runtime{chainstate.m_chainman};
    CoreChainValidationContext context{chainstate.m_chainman, runtime};
    CoreActiveChainView active_chain{chainstate.m_chain};
    TestBlockValidityRequest request{
        .active_chain = active_chain,
        .consensus_params = chainstate.m_chainman.GetConsensus(),
        .header_context = header_context,
        .coins_tip = chainstate.CoinsTip(),
        .undo_writer = block_store,
        .block_index_lookup = block_index_store,
        .block_index_committer = block_index_store,
        .validation_context = context,
        .script_check_scheduler = context.ScriptCheckScheduler(),
        .last_script_check_reason_logged = chainstate.LastScriptCheckReasonLogged(),
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

VerifyDBResult CVerifyDB::VerifyDB(
    VerifyDBRequest request,
    int nCheckLevel, int nCheckDepth)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* tip{request.active_chain.Tip()};
    if (tip == nullptr || tip->pprev == nullptr) {
        return VerifyDBResult::SUCCESS;
    }

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > request.active_chain.Height()) {
        nCheckDepth = request.active_chain.Height();
    }
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogInfo("Verifying last %i blocks at level %i", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(&request.coins_view);
    CBlockIndex* pindex;
    CBlockIndex* pindexFailure = nullptr;
    int nGoodTransactions = 0;
    BlockValidationState state;
    int reportDone = 0;
    bool skipped_no_block_data{false};
    bool skipped_l3_checks{false};
    const CoreBlockConnectionRuntimeInputs runtime_inputs{
        MakeCoreBlockConnectionRuntimeInputs(request.validation_context, request.undo_writer, request.block_index_committer, request.script_check_scheduler)};
    LogInfo("Verification progress: 0%%");

    for (pindex = request.active_chain.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        const int percentageDone = std::max(1, std::min(99, (int)(((double)(request.active_chain.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone / 10) {
            // report every 10% step
            LogInfo("Verification progress: %d%%", percentageDone);
            reportDone = percentageDone / 10;
        }
        m_notifications.progress(_("Verifying blocks…"), percentageDone, false);
        if (pindex->nHeight <= request.active_chain.Height() - nCheckDepth) {
            break;
        }
        if (request.block_data_availability.IsPruneMode() && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, only go back as far as we have data.
            LogInfo("Block verification stopping at height %d (no data). This could be due to pruning.", pindex->nHeight);
            skipped_no_block_data = true;
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!request.block_reader.ReadBlock(block, *pindex)) {
            LogError("Verification error: ReadBlock failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            return VerifyDBResult::CORRUPTED_BLOCK_DB;
        }
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, request.consensus_params)) {
            LogError("Verification error: found bad block at %d, hash=%s (%s)",
                     pindex->nHeight, pindex->GetBlockHash().ToString(), state.ToString());
            return VerifyDBResult::CORRUPTED_BLOCK_DB;
        }
        // check level 2: verify undo validity
        if (nCheckLevel >= 2) {
            CBlockUndo undo;
            if (!pindex->GetUndoPos().IsNull()) {
                if (!request.undo_reader.ReadBlockUndo(undo, *pindex)) {
                    LogError("Verification error: found bad undo data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
                    return VerifyDBResult::CORRUPTED_BLOCK_DB;
                }
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        const size_t curr_coins_usage{coins.DynamicMemoryUsage() + request.coins_tip.DynamicMemoryUsage()};

        if (nCheckLevel >= 3) {
            if (curr_coins_usage <= request.coins_tip_cache_size_bytes) {
                assert(coins.GetBestBlock() == pindex->GetBlockHash());
                DisconnectResult res = DisconnectBlock(request.undo_reader, block, pindex, coins);
                if (res == DISCONNECT_FAILED) {
                    LogError("Verification error: irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
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
        LogError("Verification error: coin database inconsistencies found (last %i blocks, %i good transactions before that)", request.active_chain.Height() - pindexFailure->nHeight + 1, nGoodTransactions);
        return VerifyDBResult::CORRUPTED_BLOCK_DB;
    }
    if (skipped_l3_checks) {
        LogWarning("Skipped verification of level >=3 (insufficient database cache size). Consider increasing -dbcache.");
    }

    // store block count as we move pindex at check level >= 4
    int block_count = request.active_chain.Height() - pindex->nHeight;

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4 && !skipped_l3_checks) {
        while (pindex != request.active_chain.Tip()) {
            const int percentageDone = std::max(1, std::min(99, 100 - (int)(((double)(request.active_chain.Height() - pindex->nHeight)) / (double)nCheckDepth * 50)));
            if (reportDone < percentageDone / 10) {
                // report every 10% step
                LogInfo("Verification progress: %d%%", percentageDone);
                reportDone = percentageDone / 10;
            }
            m_notifications.progress(_("Verifying blocks…"), percentageDone, false);
            pindex = request.active_chain.Next(*pindex);
            CBlock block;
            if (!request.block_reader.ReadBlock(block, *pindex)) {
                LogError("Verification error: ReadBlock failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
                return VerifyDBResult::CORRUPTED_BLOCK_DB;
            }
            CoreBlockConnectionSetup connection_setup{
                runtime_inputs,
                PlanCoreBlockConnection(SnapshotCoreBlockConnectionPolicy(request.validation_context, *pindex), request.block_index_lookup, *pindex),
                *pindex,
                /*cache_script_results=*/false};
            connection_setup.MaybeLogScriptPolicy(request.last_script_check_reason_logged, block.GetHash());
            validation::CoreCoinsBlockConnectionState connection_state{coins};
            const validation::BlockConnectionRequest connection_request{connection_setup.Request(block, connection_state)};
            if (!validation::BlockConnectionEngine{}.Connect(connection_request, state).Succeeded()) {
                LogError("Verification error: found unconnectable block at %d, hash=%s (%s)", pindex->nHeight, pindex->GetBlockHash().ToString(), state.ToString());
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
    CoreChainValidationRuntime runtime{chainstate.m_chainman};
    CoreChainValidationContext context{chainstate.m_chainman, runtime};
    CoreActiveChainView active_chain{chainstate.m_chain};
    VerifyDBRequest request{
        .active_chain = active_chain,
        .consensus_params = consensus_params,
        .coins_view = coinsview,
        .coins_tip = chainstate.CoinsTip(),
        .coins_tip_cache_size_bytes = chainstate.m_coinstip_cache_size_bytes,
        .block_reader = block_store,
        .undo_reader = block_store,
        .undo_writer = block_store,
        .block_data_availability = block_store,
        .block_index_lookup = block_index_store,
        .block_index_committer = block_index_store,
        .validation_context = context,
        .script_check_scheduler = context.ScriptCheckScheduler(),
        .last_script_check_reason_logged = chainstate.LastScriptCheckReasonLogged(),
        .interrupt = chainstate.m_chainman.m_interrupt,
    };
    return VerifyDB(request, nCheckLevel, nCheckDepth);
}
