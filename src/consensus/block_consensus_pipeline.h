// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_CONSENSUS_PIPELINE_H
#define BITCOIN_CONSENSUS_BLOCK_CONSENSUS_PIPELINE_H

#include <consensus/amount.h>
#include <consensus/block_commit.h>
#include <consensus/block_check.h>
#include <consensus/block_spend.h>
#include <primitives/transaction.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

class CBlock;
class CBlockHeader;

namespace Consensus {

enum class BlockConsensusStage {
    Structural,
    Contextual,
    Spend,
    Commit,
};

[[nodiscard]] std::string_view BlockConsensusStageName(BlockConsensusStage stage);

struct BlockConsensusStageError {
    BlockConsensusStage stage{BlockConsensusStage::Structural};
    std::optional<BlockConsensusIssue> issue;
    std::string reject_reason;
    std::string debug_message;
};

template <typename T>
using BlockConsensusStageResult = Consensus::Expected<T, BlockConsensusStageError>;

struct BlockConsensusContext {
    BlockSpendContext spend;
    BlockCommitContext commit;
    CAmount block_subsidy{0};
};

struct BlockContextualBodyOptions {
    BlockContextualTransactionOptions transactions;
    bool expect_witness_commitment{false};
};

struct BlockContextualConsensusOptions {
    BlockContextualHeaderOptions header;
    BlockContextualBodyOptions body;
};

struct BlockContextualBodyValidation {
    BlockFacts facts;
    bool checked_witness_commitment{false};
};

struct BlockStructuralConsensusOptions {
    bool check_merkle_root{true};
};

// Validation views borrow block storage for one validation call. They compute
// their facts from the borrowed transaction range, so callers cannot construct
// a view whose facts drift from its transaction storage.
class BlockStructuralValidationView {
public:
    explicit BlockStructuralValidationView(const CBlock& block);

    [[nodiscard]] const CBlockHeader& Header() const noexcept { return m_header; }
    [[nodiscard]] std::span<const CTransactionRef> Transactions() const noexcept { return m_transactions; }
    [[nodiscard]] const BlockStructuralFacts& Facts() const noexcept { return m_facts; }

private:
    struct TrustedFactsTag {};
    BlockStructuralValidationView(const CBlockHeader& header, std::span<const CTransactionRef> transactions, BlockStructuralFacts facts, TrustedFactsTag);

    const CBlockHeader& m_header;
    std::span<const CTransactionRef> m_transactions;
    BlockStructuralFacts m_facts;

    friend class BlockPrecommitValidationView;
};

class BlockContextualBodyValidationView {
public:
    explicit BlockContextualBodyValidationView(const CBlock& block);

    [[nodiscard]] std::span<const CTransactionRef> Transactions() const noexcept { return m_transactions; }
    [[nodiscard]] const BlockFacts& Facts() const noexcept { return m_facts; }

private:
    struct TrustedFactsTag {};
    BlockContextualBodyValidationView(std::span<const CTransactionRef> transactions, BlockFacts facts, TrustedFactsTag);

    std::span<const CTransactionRef> m_transactions;
    BlockFacts m_facts;

    friend class BlockPrecommitValidationView;
};

class BlockContextualValidationView {
public:
    explicit BlockContextualValidationView(const CBlock& block);

    [[nodiscard]] const CBlockHeader& Header() const noexcept { return m_header; }
    [[nodiscard]] const BlockContextualBodyValidationView& Body() const noexcept { return m_body; }

private:
    struct TrustedBodyTag {};
    BlockContextualValidationView(const CBlockHeader& header, BlockContextualBodyValidationView body, TrustedBodyTag);

    const CBlockHeader& m_header;
    BlockContextualBodyValidationView m_body;

    friend class BlockPrecommitValidationView;
};

class BlockPrecommitValidationView {
public:
    explicit BlockPrecommitValidationView(const CBlock& block);

    [[nodiscard]] const CBlockHeader& Header() const noexcept { return m_header; }
    [[nodiscard]] std::span<const CTransactionRef> Transactions() const noexcept { return m_transactions; }
    [[nodiscard]] const BlockFacts& Facts() const noexcept { return m_facts; }
    [[nodiscard]] BlockStructuralValidationView StructuralView() const;
    [[nodiscard]] BlockContextualValidationView ContextualView() const;

private:
    const CBlockHeader& m_header;
    std::span<const CTransactionRef> m_transactions;
    BlockFacts m_facts;
};

[[nodiscard]] BlockSpendContext BuildBlockSpendContext(const BlockHeaderContext& headers);
[[nodiscard]] BlockCommitContext BuildBlockCommitContext(const uint256& new_best_block);
[[nodiscard]] BlockCommitContext BuildBlockCommitContext(const BlockHeaderContext& headers, const uint256& new_best_block);
[[nodiscard]] BlockConsensusContext BuildBlockConsensusContext(const BlockHeaderContext& headers, const uint256& new_best_block, CAmount block_subsidy);
[[nodiscard]] BlockContextualBodyOptions BuildBlockContextualBodyOptions(const CBlockHeader& block, const BlockHeaderContext& headers);
[[nodiscard]] BlockContextualConsensusOptions BuildBlockContextualConsensusOptions(const CBlockHeader& block, const BlockHeaderContext& headers, const Params& params, int64_t max_block_time);
[[nodiscard]] BlockPrecommitValidationView BuildBlockPrecommitValidationView(const CBlock& block);
[[nodiscard]] BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockCheckError& error);
[[nodiscard]] BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockSpendError& error);
[[nodiscard]] BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockCommitError& error);
[[nodiscard]] BlockCheckResult<void> ValidateBlockStructuralStage(const BlockStructuralValidationView& input, const BlockStructuralConsensusOptions& options);
[[nodiscard]] BlockCheckResult<void> ValidateBlockStructuralStage(const CBlock& block, const BlockStructuralConsensusOptions& options);
[[nodiscard]] BlockCheckResult<BlockContextualBodyValidation> ValidateBlockContextualBodyStage(const BlockContextualBodyValidationView& input, const BlockContextualBodyOptions& options, const char* debug_context);
[[nodiscard]] BlockCheckResult<BlockContextualBodyValidation> ValidateBlockContextualBodyStage(const CBlock& block, const BlockContextualBodyOptions& options, const char* debug_context);
[[nodiscard]] BlockCheckResult<void> ValidateBlockContextualStage(const BlockContextualValidationView& input, const BlockContextualConsensusOptions& options);
[[nodiscard]] BlockCheckResult<void> ValidateBlockContextualStage(const CBlock& block, const BlockContextualConsensusOptions& options);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommit(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommit(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommitStages(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommitStages(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options);
[[nodiscard]] BlockConsensusStageResult<void> CommitBlockStageEffects(const BlockCommitContext& commit_context, const BlockSpendEffects& effects, BlockRevertDataWriter& revert_data_writer, BlockSpendStateCommitter& spend_state_committer, BlockMetadataCommitter& metadata_committer);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlockStages(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlock(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlock(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer);
[[nodiscard]] BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlockStages(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer);

class BlockConsensusPipeline {
public:
    BlockConsensusPipeline(std::span<const CTransactionRef> transactions, BlockConsensusContext context);
    BlockConsensusPipeline(const CBlock& block, BlockConsensusContext context);

    [[nodiscard]] BlockSpendResult<BlockSpendEffects> ValidateAndStageSpend(BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendConsensusOptions& options) const;
    [[nodiscard]] BlockSpendResult<BlockSpendEffects> ValidateAndCompleteSpendStage(BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendConsensusOptions& options) const;
    [[nodiscard]] BlockSpendResult<BlockSpendEffects> CompleteSpendStage(BlockSpendResult<BlockSpendEffects> spend_effects, BlockScriptChecker& script_checker) const;
    [[nodiscard]] BlockSpendResult<void> CheckCoinbaseReward(const BlockSpendEffects& effects) const;
    [[nodiscard]] BlockSpendResult<void> CompleteScriptChecks(BlockScriptChecker& script_checker) const;

private:
    std::span<const CTransactionRef> m_transactions;
    BlockConsensusContext m_context;
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_CONSENSUS_PIPELINE_H
