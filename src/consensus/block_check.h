// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_CHECK_H
#define BITCOIN_CONSENSUS_BLOCK_CHECK_H

#include <consensus/block_facts.h>
#include <consensus/diagnostics.h>
#include <consensus/expected.h>
#include <consensus/params.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <span>
#include <string>

class CBlock;
class CBlockHeader;

namespace Consensus {
struct Params;

struct BlockCheckOptions {
    bool check_pow{true};
    bool check_merkle_root{true};
};

struct BlockHeaderCheckOptions {
    bool check_pow{true};
};

struct BlockContextualHeaderOptions {
    int block_height{0};
    int difficulty_adjustment_interval{0};
    int64_t previous_median_time_past{0};
    int64_t previous_block_time{0};
    int64_t max_block_time{0};
    bool enforce_timewarp_protection{false};
    bool height_in_coinbase_active{false};
    bool der_signature_active{false};
    bool cltv_active{false};
};

struct BlockContextualTransactionOptions {
    int block_height{0};
    int64_t locktime_cutoff{0};
    bool enforce_coinbase_height{false};
};

struct BlockDeploymentContext {
    bool height_in_coinbase_active{false};
    bool der_signature_active{false};
    bool cltv_active{false};
    bool csv_active{false};
    bool segwit_active{false};
};

class BlockHeaderContext {
public:
    BlockHeaderContext() = default;
    BlockHeaderContext(int block_height, int64_t previous_median_time_past, int64_t previous_block_time, BlockDeploymentContext deployments = {}) noexcept;

    [[nodiscard]] int Height() const noexcept { return m_block_height; }
    [[nodiscard]] int64_t PreviousMedianTimePast() const noexcept { return m_previous_median_time_past; }
    [[nodiscard]] int64_t PreviousBlockTime() const noexcept { return m_previous_block_time; }
    [[nodiscard]] BlockDeploymentContext Deployments() const noexcept { return m_deployments; }

    [[nodiscard]] bool HeightInCoinbaseActive() const noexcept { return m_deployments.height_in_coinbase_active; }
    [[nodiscard]] bool DerSignatureActive() const noexcept { return m_deployments.der_signature_active; }
    [[nodiscard]] bool CltvActive() const noexcept { return m_deployments.cltv_active; }
    [[nodiscard]] bool CsvActive() const noexcept { return m_deployments.csv_active; }
    [[nodiscard]] bool SegwitActive() const noexcept { return m_deployments.segwit_active; }

private:
    int m_block_height{0};
    int64_t m_previous_median_time_past{0};
    int64_t m_previous_block_time{0};
    BlockDeploymentContext m_deployments;
};

struct BlockHeaderAdmissionOptions {
    unsigned int expected_difficulty_bits{0};
    BlockContextualHeaderOptions contextual;
};

struct BlockWitnessMalleationOptions {
    bool expect_witness_commitment{false};
};

struct BlockWitnessRulesOptions {
    bool expect_witness_commitment{false};
    const char* debug_context{""};
};

struct BlockCheckError {
    BlockConsensusIssue issue{BlockConsensusIssue::Consensus};
    std::string reject_reason;
    std::string debug_message;
};

template <typename T>
using BlockCheckResult = Consensus::Expected<T, BlockCheckError>;

[[nodiscard]] BlockCheckResult<void> CheckBlockHeader(const CBlockHeader& block, const Params& params, const BlockHeaderCheckOptions& options = {});
[[nodiscard]] BlockCheckResult<void> CheckBlockPreviousMedianTime(const CBlockHeader& block, int64_t previous_median_time_past);
[[nodiscard]] BlockCheckResult<void> CheckBlockTimewarp(const CBlockHeader& block, const BlockContextualHeaderOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockFutureTime(const CBlockHeader& block, int64_t max_block_time);
[[nodiscard]] BlockCheckResult<void> CheckBlockVersion(const CBlockHeader& block, const BlockDeploymentContext& deployments);
[[nodiscard]] BlockContextualHeaderOptions BuildBlockContextualHeaderOptions(const BlockHeaderContext& headers, const Params& params, int64_t max_block_time);
[[nodiscard]] BlockCheckResult<void> CheckBlockDifficultyBits(const CBlockHeader& block, unsigned int expected_difficulty_bits);
[[nodiscard]] BlockCheckResult<void> CheckBlockContextualHeaderRules(const CBlockHeader& block, const BlockContextualHeaderOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockHeaderAdmissionRules(const CBlockHeader& block, const BlockHeaderAdmissionOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockMerkleRoot(const CBlockHeader& block, const BlockStructuralFacts& facts);
[[nodiscard]] BlockCheckResult<void> CheckBlockTransactions(std::span<const CTransactionRef> transactions, const BlockStructuralFacts& facts);
[[nodiscard]] BlockCheckResult<void> CheckBlockBody(const CBlock& block, const BlockStructuralFacts& facts);
[[nodiscard]] BlockCheckResult<void> CheckBlockStructuralRules(const CBlock& block, const Params& params, const BlockCheckOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockWitnessMalleation(std::span<const CTransactionRef> transactions, const BlockFacts& facts, const BlockWitnessMalleationOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockWitnessMalleation(const CBlock& block, const BlockFacts& facts, const BlockWitnessMalleationOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockWeight(const BlockFacts& facts, const char* debug_context);
[[nodiscard]] BlockCheckResult<void> CheckBlockWitnessRules(std::span<const CTransactionRef> transactions, const BlockFacts& facts, const BlockWitnessRulesOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockWitnessRules(const CBlock& block, const BlockFacts& facts, const BlockWitnessRulesOptions& options);
[[nodiscard]] BlockContextualTransactionOptions BuildBlockContextualTransactionOptions(const CBlockHeader& block, const BlockHeaderContext& headers);
[[nodiscard]] bool ExpectWitnessCommitment(const BlockHeaderContext& headers);
[[nodiscard]] BlockCheckResult<void> CheckBlockFinalTransactions(std::span<const CTransactionRef> transactions, int block_height, int64_t locktime_cutoff);
[[nodiscard]] BlockCheckResult<void> CheckBlockFinalTransactions(const CBlock& block, int block_height, int64_t locktime_cutoff);
[[nodiscard]] BlockCheckResult<void> CheckBlockCoinbaseHeight(const CTransaction& coinbase, int block_height);
[[nodiscard]] BlockCheckResult<void> CheckBlockCoinbaseHeight(const CBlock& block, int block_height);
[[nodiscard]] BlockCheckResult<void> CheckBlockContextualTransactionRules(std::span<const CTransactionRef> transactions, const BlockContextualTransactionOptions& options);
[[nodiscard]] BlockCheckResult<void> CheckBlockContextualTransactionRules(const CBlock& block, const BlockContextualTransactionOptions& options);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_CHECK_H
