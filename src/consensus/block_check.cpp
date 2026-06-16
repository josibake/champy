// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_check.h>

#include <consensus/consensus.h>
#include <consensus/locktime.h>
#include <consensus/pow.h>
#include <consensus/predicates.h>
#include <consensus/script_view.h>
#include <consensus/sigops.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace Consensus {

namespace {

BlockCheckError InvalidBlockCheck(BlockConsensusIssue issue, const std::string& reject_reason, const std::string& debug_message)
{
    return BlockCheckError{
        .issue = issue,
        .reject_reason = reject_reason,
        .debug_message = debug_message,
    };
}

BlockCheckError InvalidValidationView(const std::string& debug_message)
{
    return InvalidBlockCheck(
        BlockConsensusIssue::Consensus,
        "bad-validation-view",
        debug_message);
}

BlockCheckResult<void> CheckTransactionView(std::span<const CTransactionRef> transactions)
{
    for (const auto& tx : transactions) {
        if (!tx) {
            return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("transaction view contains null transaction")};
        }
    }

    return {};
}

BlockCheckResult<void> CheckStructuralFactsMatchTransactions(std::span<const CTransactionRef> transactions, const BlockStructuralFacts& facts)
{
    if (facts.transaction_count != transactions.size()) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("structural facts do not match transaction view")};
    }

    return CheckTransactionView(transactions);
}

BlockCheckResult<void> CheckBlockFactsMatchTransactions(std::span<const CTransactionRef> transactions, const BlockFacts& facts)
{
    const auto structural_view{CheckStructuralFactsMatchTransactions(transactions, facts.structure)};
    if (!structural_view) return Consensus::Unexpected<BlockCheckError>{structural_view.error()};

    if (!facts.witness_commitment_index.has_value()) return {};

    if (transactions.empty()) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("witness commitment index without coinbase transaction")};
    }

    const CTransaction& coinbase{*transactions[0]};
    const int commitment_index{*facts.witness_commitment_index};
    if (commitment_index < 0 || static_cast<std::size_t>(commitment_index) >= coinbase.vout.size()) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("witness commitment index outside coinbase outputs")};
    }
    if (!HasWitnessCommitmentPrefix(ScriptView{coinbase.vout[commitment_index].scriptPubKey})) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("witness commitment index does not identify a commitment output")};
    }

    return {};
}

std::string FormatBlockVersion(std::string_view prefix, int version, std::string_view suffix)
{
    char version_buffer[9];
    std::snprintf(version_buffer, sizeof(version_buffer), "%08x", static_cast<unsigned int>(version));
    return std::string{prefix} + version_buffer + std::string{suffix};
}

} // namespace

BlockHeaderContext::BlockHeaderContext(int block_height, int64_t previous_median_time_past, int64_t previous_block_time, BlockDeploymentContext deployments) noexcept
    : m_block_height{block_height},
      m_previous_median_time_past{previous_median_time_past},
      m_previous_block_time{previous_block_time},
      m_deployments{deployments}
{
}

BlockCheckResult<void> CheckBlockHeader(const CBlockHeader& block, const Params& params, const BlockHeaderCheckOptions& options)
{
    if (options.check_pow && !CheckProofOfWorkHash(block.GetHash(), block.nBits, params)) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::InvalidHeader,
            "high-hash",
            "proof of work failed")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockPreviousMedianTime(const CBlockHeader& block, int64_t previous_median_time_past)
{
    if (block.GetBlockTime() <= previous_median_time_past) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::InvalidHeader,
            "time-too-old",
            "block's timestamp is too early")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockTimewarp(const CBlockHeader& block, const BlockContextualHeaderOptions& options)
{
    if (!options.enforce_timewarp_protection) return {};

    assert(options.difficulty_adjustment_interval > 0);
    if (options.block_height % options.difficulty_adjustment_interval != 0) return {};

    if (block.GetBlockTime() < options.previous_block_time - MAX_TIMEWARP) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::InvalidHeader,
            "time-timewarp-attack",
            "block's timestamp is too early on diff adjustment block")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockFutureTime(const CBlockHeader& block, int64_t max_block_time)
{
    if (block.GetBlockTime() > max_block_time) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::TimeFuture,
            "time-too-new",
            "block timestamp too far in the future")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockVersion(const CBlockHeader& block, const BlockDeploymentContext& deployments)
{
    if ((block.nVersion < 2 && deployments.height_in_coinbase_active) ||
        (block.nVersion < 3 && deployments.der_signature_active) ||
        (block.nVersion < 4 && deployments.cltv_active)) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::InvalidHeader,
            FormatBlockVersion("bad-version(0x", block.nVersion, ")"),
            FormatBlockVersion("rejected nVersion=0x", block.nVersion, " block"))};
    }

    return {};
}

BlockContextualHeaderOptions BuildBlockContextualHeaderOptions(const BlockHeaderContext& headers, const Params& params, int64_t max_block_time)
{
    return BlockContextualHeaderOptions{
        .block_height = headers.Height(),
        .difficulty_adjustment_interval = static_cast<int>(params.DifficultyAdjustmentInterval()),
        .previous_median_time_past = headers.PreviousMedianTimePast(),
        .previous_block_time = headers.PreviousBlockTime(),
        .max_block_time = max_block_time,
        .enforce_timewarp_protection = params.enforce_BIP94,
        .height_in_coinbase_active = headers.HeightInCoinbaseActive(),
        .der_signature_active = headers.DerSignatureActive(),
        .cltv_active = headers.CltvActive(),
    };
}

BlockCheckResult<void> CheckBlockDifficultyBits(const CBlockHeader& block, unsigned int expected_difficulty_bits)
{
    if (block.nBits != expected_difficulty_bits) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::InvalidHeader,
            "bad-diffbits",
            "incorrect proof of work")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockContextualHeaderRules(const CBlockHeader& block, const BlockContextualHeaderOptions& options)
{
    const auto median_time{CheckBlockPreviousMedianTime(block, options.previous_median_time_past)};
    if (!median_time) return Consensus::Unexpected<BlockCheckError>{median_time.error()};

    const auto timewarp{CheckBlockTimewarp(block, options)};
    if (!timewarp) return Consensus::Unexpected<BlockCheckError>{timewarp.error()};

    const auto future_time{CheckBlockFutureTime(block, options.max_block_time)};
    if (!future_time) return Consensus::Unexpected<BlockCheckError>{future_time.error()};

    return CheckBlockVersion(block, {
        .height_in_coinbase_active = options.height_in_coinbase_active,
        .der_signature_active = options.der_signature_active,
        .cltv_active = options.cltv_active});
}

BlockCheckResult<void> CheckBlockHeaderAdmissionRules(const CBlockHeader& block, const BlockHeaderAdmissionOptions& options)
{
    const auto difficulty{CheckBlockDifficultyBits(block, options.expected_difficulty_bits)};
    if (!difficulty) return Consensus::Unexpected<BlockCheckError>{difficulty.error()};

    return CheckBlockContextualHeaderRules(block, options.contextual);
}

BlockCheckResult<void> CheckBlockMerkleRoot(const CBlockHeader& block, const BlockStructuralFacts& facts)
{
    if (block.hashMerkleRoot != facts.merkle_root) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Mutated,
            "bad-txnmrklroot",
            "hashMerkleRoot mismatch")};
    }

    if (facts.merkle_mutated) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Mutated,
            "bad-txns-duplicate",
            "duplicate transaction")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockTransactions(std::span<const CTransactionRef> transactions, const BlockStructuralFacts& facts)
{
    const auto validation_view{CheckStructuralFactsMatchTransactions(transactions, facts)};
    if (!validation_view) return Consensus::Unexpected<BlockCheckError>{validation_view.error()};

    if (facts.transaction_count == 0 ||
        facts.transaction_count * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT ||
        facts.stripped_size * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Consensus,
            "bad-blk-length",
            "size limits failed")};
    }

    if (!IsCoinbase(*transactions[0])) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Consensus,
            "bad-cb-missing",
            "first tx is not coinbase")};
    }

    for (std::size_t i{1}; i < transactions.size(); ++i) {
        if (IsCoinbase(*transactions[i])) {
            return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
                BlockConsensusIssue::Consensus,
                "bad-cb-multiple",
                "more than one coinbase")};
        }
    }

    for (const auto& tx : transactions) {
        const auto tx_check{CheckTransaction(*tx)};
        if (!tx_check) {
            return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
                BlockConsensusIssue::Consensus,
                tx_check.error().reject_reason,
                "Transaction check failed (tx hash " + tx->GetHash().ToString() + ") " + tx_check.error().debug_message)};
        }
    }

    // This underestimates the number of sigops, because unlike full block connection it
    // does not count witness and p2sh sigops.
    unsigned int nSigOps = 0;
    for (const auto& tx : transactions) {
        nSigOps += Consensus::GetLegacySigOpCount(*tx);
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Consensus,
            "bad-blk-sigops",
            "out-of-bounds SigOpCount")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockBody(const CBlock& block, const BlockStructuralFacts& facts)
{
    return CheckBlockTransactions(block.vtx, facts);
}

BlockCheckResult<void> CheckBlockStructuralRules(const CBlock& block, const Params& params, const BlockCheckOptions& options)
{
    const auto header{CheckBlockHeader(block, params, {.check_pow = options.check_pow})};
    if (!header) return Consensus::Unexpected<BlockCheckError>{header.error()};

    const BlockStructuralFacts facts{ComputeBlockStructuralFacts(block)};
    if (options.check_merkle_root) {
        const auto merkle{CheckBlockMerkleRoot(block, facts)};
        if (!merkle) return Consensus::Unexpected<BlockCheckError>{merkle.error()};
    }

    return CheckBlockTransactions(block.vtx, facts);
}

BlockCheckResult<void> CheckBlockWitnessMalleation(std::span<const CTransactionRef> transactions, const BlockFacts& facts, const BlockWitnessMalleationOptions& options)
{
    const auto validation_view{CheckBlockFactsMatchTransactions(transactions, facts)};
    if (!validation_view) return Consensus::Unexpected<BlockCheckError>{validation_view.error()};

    if (options.expect_witness_commitment && facts.witness_commitment_index.has_value()) {
        const CTransaction& coinbase{*transactions[0]};
        if (coinbase.vin.empty()) {
            return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("witness commitment check requires coinbase input")};
        }
        const auto& witness_stack{coinbase.vin[0].scriptWitness.stack};

        if (witness_stack.size() != 1 || witness_stack[0].size() != 32) {
            return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
                BlockConsensusIssue::Mutated,
                "bad-witness-nonce-size",
                std::string{__func__} + " : invalid witness reserved value size")};
        }

        uint256 hash_witness{facts.witness_merkle_root};
        CHash256().Write(hash_witness).Write(witness_stack[0]).Finalize(hash_witness);
        const auto commitment_hash{WitnessCommitmentHash(ScriptView{coinbase.vout[*facts.witness_commitment_index].scriptPubKey})};
        if (std::memcmp(hash_witness.begin(), commitment_hash.data(), commitment_hash.size())) {
            return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
                BlockConsensusIssue::Mutated,
                "bad-witness-merkle-match",
                std::string{__func__} + " : witness merkle commitment mismatch")};
        }

        return {};
    }

    if (facts.has_witness) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Mutated,
            "unexpected-witness",
            std::string{__func__} + " : unexpected witness data found")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockWitnessMalleation(const CBlock& block, const BlockFacts& facts, const BlockWitnessMalleationOptions& options)
{
    return CheckBlockWitnessMalleation(block.vtx, facts, options);
}

BlockCheckResult<void> CheckBlockWeight(const BlockFacts& facts, const char* debug_context)
{
    if (facts.weight > MAX_BLOCK_WEIGHT) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Consensus,
            "bad-blk-weight",
            std::string{debug_context} + " : weight limit failed")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockWitnessRules(std::span<const CTransactionRef> transactions, const BlockFacts& facts, const BlockWitnessRulesOptions& options)
{
    const auto witness{CheckBlockWitnessMalleation(transactions, facts, {
        .expect_witness_commitment = options.expect_witness_commitment})};
    if (!witness) return Consensus::Unexpected<BlockCheckError>{witness.error()};

    return CheckBlockWeight(facts, options.debug_context);
}

BlockCheckResult<void> CheckBlockWitnessRules(const CBlock& block, const BlockFacts& facts, const BlockWitnessRulesOptions& options)
{
    return CheckBlockWitnessRules(block.vtx, facts, options);
}

BlockContextualTransactionOptions BuildBlockContextualTransactionOptions(const CBlockHeader& block, const BlockHeaderContext& headers)
{
    const bool enforce_locktime_median_time_past{headers.CsvActive()};

    return BlockContextualTransactionOptions{
        .block_height = headers.Height(),
        .locktime_cutoff = enforce_locktime_median_time_past ? headers.PreviousMedianTimePast() : block.GetBlockTime(),
        .enforce_coinbase_height = headers.HeightInCoinbaseActive(),
    };
}

bool ExpectWitnessCommitment(const BlockHeaderContext& headers)
{
    return headers.SegwitActive();
}

BlockCheckResult<void> CheckBlockFinalTransactions(std::span<const CTransactionRef> transactions, int block_height, int64_t locktime_cutoff)
{
    const auto validation_view{CheckTransactionView(transactions)};
    if (!validation_view) return Consensus::Unexpected<BlockCheckError>{validation_view.error()};

    for (const auto& tx : transactions) {
        if (!IsFinalTx(*tx, block_height, locktime_cutoff)) {
            return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
                BlockConsensusIssue::Consensus,
                "bad-txns-nonfinal",
                "non-final transaction")};
        }
    }

    return {};
}

BlockCheckResult<void> CheckBlockFinalTransactions(const CBlock& block, int block_height, int64_t locktime_cutoff)
{
    return CheckBlockFinalTransactions(block.vtx, block_height, locktime_cutoff);
}

BlockCheckResult<void> CheckBlockCoinbaseHeight(const CTransaction& coinbase, int block_height)
{
    if (!IsCoinbase(coinbase) || coinbase.vin.empty()) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("coinbase height check requires a coinbase transaction")};
    }

    const CScript expect = CScript{} << block_height;
    if (coinbase.vin[0].scriptSig.size() < expect.size() ||
        !std::equal(expect.begin(), expect.end(), coinbase.vin[0].scriptSig.begin())) {
        return Consensus::Unexpected<BlockCheckError>{InvalidBlockCheck(
            BlockConsensusIssue::Consensus,
            "bad-cb-height",
            "block height mismatch in coinbase")};
    }

    return {};
}

BlockCheckResult<void> CheckBlockCoinbaseHeight(const CBlock& block, int block_height)
{
    if (block.vtx.empty() || !block.vtx[0]) {
        return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("coinbase height check requires a transaction")};
    }
    return CheckBlockCoinbaseHeight(*block.vtx[0], block_height);
}

BlockCheckResult<void> CheckBlockContextualTransactionRules(std::span<const CTransactionRef> transactions, const BlockContextualTransactionOptions& options)
{
    const auto finality{CheckBlockFinalTransactions(transactions, options.block_height, options.locktime_cutoff)};
    if (!finality) return Consensus::Unexpected<BlockCheckError>{finality.error()};

    if (options.enforce_coinbase_height) {
        if (transactions.empty()) {
            return Consensus::Unexpected<BlockCheckError>{InvalidValidationView("coinbase height check requires a transaction")};
        }
        const auto coinbase_height{CheckBlockCoinbaseHeight(*transactions[0], options.block_height)};
        if (!coinbase_height) return Consensus::Unexpected<BlockCheckError>{coinbase_height.error()};
    }

    return {};
}

BlockCheckResult<void> CheckBlockContextualTransactionRules(const CBlock& block, const BlockContextualTransactionOptions& options)
{
    return CheckBlockContextualTransactionRules(block.vtx, options);
}

} // namespace Consensus
