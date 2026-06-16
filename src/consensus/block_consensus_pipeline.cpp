// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_consensus_pipeline.h>

#include <primitives/block.h>

#include <cassert>
#include <optional>
#include <utility>

namespace Consensus {

std::string_view BlockConsensusStageName(BlockConsensusStage stage)
{
    switch (stage) {
    case BlockConsensusStage::Structural:
        return "structural";
    case BlockConsensusStage::Contextual:
        return "contextual";
    case BlockConsensusStage::Spend:
        return "spend";
    case BlockConsensusStage::Commit:
        return "commit";
    }
    assert(false);
    return "unknown";
}

BlockSpendContext BuildBlockSpendContext(const BlockHeaderContext& headers)
{
    return BlockSpendContext{
        .block_height = headers.Height(),
        .previous_median_time_past = headers.PreviousMedianTimePast(),
    };
}

BlockCommitContext BuildBlockCommitContext(const uint256& new_best_block)
{
    return BlockCommitContext{
        .new_best_block = new_best_block,
    };
}

BlockCommitContext BuildBlockCommitContext(const BlockHeaderContext& headers, const uint256& new_best_block)
{
    return BlockCommitContext{
        .new_best_block = new_best_block,
        .block_height = headers.Height(),
        .previous_median_time_past = headers.PreviousMedianTimePast(),
    };
}

BlockConsensusContext BuildBlockConsensusContext(const BlockHeaderContext& headers, const uint256& new_best_block, CAmount block_subsidy)
{
    return BlockConsensusContext{
        .spend = BuildBlockSpendContext(headers),
        .commit = BuildBlockCommitContext(headers, new_best_block),
        .block_subsidy = block_subsidy,
    };
}

BlockContextualBodyOptions BuildBlockContextualBodyOptions(const CBlockHeader& block, const BlockHeaderContext& headers)
{
    return BlockContextualBodyOptions{
        .transactions = BuildBlockContextualTransactionOptions(block, headers),
        .expect_witness_commitment = ExpectWitnessCommitment(headers),
    };
}

BlockContextualConsensusOptions BuildBlockContextualConsensusOptions(const CBlockHeader& block, const BlockHeaderContext& headers, const Params& params, int64_t max_block_time)
{
    return BlockContextualConsensusOptions{
        .header = BuildBlockContextualHeaderOptions(headers, params, max_block_time),
        .body = BuildBlockContextualBodyOptions(block, headers),
    };
}

BlockStructuralValidationView::BlockStructuralValidationView(const CBlock& block)
    : BlockStructuralValidationView{block, block.vtx, ComputeBlockStructuralFacts(block), TrustedFactsTag{}}
{
}

BlockStructuralValidationView::BlockStructuralValidationView(const CBlockHeader& header, std::span<const CTransactionRef> transactions, BlockStructuralFacts facts, TrustedFactsTag)
    : m_header{header}, m_transactions{transactions}, m_facts{std::move(facts)}
{
}

BlockContextualBodyValidationView::BlockContextualBodyValidationView(const CBlock& block)
    : BlockContextualBodyValidationView{block.vtx, ComputeBlockFacts(block), TrustedFactsTag{}}
{
}

BlockContextualBodyValidationView::BlockContextualBodyValidationView(std::span<const CTransactionRef> transactions, BlockFacts facts, TrustedFactsTag)
    : m_transactions{transactions}, m_facts{std::move(facts)}
{
}

BlockContextualValidationView::BlockContextualValidationView(const CBlock& block)
    : BlockContextualValidationView{block, BlockContextualBodyValidationView{block}, TrustedBodyTag{}}
{
}

BlockContextualValidationView::BlockContextualValidationView(const CBlockHeader& header, BlockContextualBodyValidationView body, TrustedBodyTag)
    : m_header{header}, m_body{std::move(body)}
{
}

BlockPrecommitValidationView::BlockPrecommitValidationView(const CBlock& block)
    : m_header{block}, m_transactions{block.vtx}, m_facts{ComputeBlockFacts(block)}
{
}

BlockStructuralValidationView BlockPrecommitValidationView::StructuralView() const
{
    return BlockStructuralValidationView{
        m_header,
        m_transactions,
        m_facts.structure,
        BlockStructuralValidationView::TrustedFactsTag{}};
}

BlockContextualValidationView BlockPrecommitValidationView::ContextualView() const
{
    return BlockContextualValidationView{
        m_header,
        BlockContextualBodyValidationView{
            m_transactions,
            m_facts,
            BlockContextualBodyValidationView::TrustedFactsTag{}},
        BlockContextualValidationView::TrustedBodyTag{}};
}

BlockPrecommitValidationView BuildBlockPrecommitValidationView(const CBlock& block)
{
    return BlockPrecommitValidationView{block};
}

BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockCheckError& error)
{
    return BlockConsensusStageError{
        .stage = stage,
        .issue = std::make_optional(error.issue),
        .reject_reason = error.reject_reason,
        .debug_message = error.debug_message,
    };
}

BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockSpendError& error)
{
    return BlockConsensusStageError{
        .stage = stage,
        .issue = std::make_optional(error.issue),
        .reject_reason = error.reject_reason,
        .debug_message = error.debug_message,
    };
}

BlockConsensusStageError BuildBlockConsensusStageError(BlockConsensusStage stage, const BlockCommitError& error)
{
    return BlockConsensusStageError{
        .stage = stage,
        .issue = std::nullopt,
        .reject_reason = error.reject_reason,
        .debug_message = {},
    };
}

BlockCheckResult<void> ValidateBlockStructuralStage(const BlockStructuralValidationView& input, const BlockStructuralConsensusOptions& options)
{
    if (options.check_merkle_root) {
        const auto merkle{CheckBlockMerkleRoot(input.Header(), input.Facts())};
        if (!merkle) return Consensus::Unexpected<BlockCheckError>{merkle.error()};
    }

    return CheckBlockTransactions(input.Transactions(), input.Facts());
}

BlockCheckResult<void> ValidateBlockStructuralStage(const CBlock& block, const BlockStructuralConsensusOptions& options)
{
    return ValidateBlockStructuralStage(
        BlockStructuralValidationView{block},
        options);
}

BlockCheckResult<BlockContextualBodyValidation> ValidateBlockContextualBodyStage(const BlockContextualBodyValidationView& input, const BlockContextualBodyOptions& options, const char* debug_context)
{
    const auto transactions{CheckBlockContextualTransactionRules(input.Transactions(), options.transactions)};
    if (!transactions) return Consensus::Unexpected<BlockCheckError>{transactions.error()};

    const auto witness{CheckBlockWitnessRules(
        input.Transactions(),
        input.Facts(),
        {
            .expect_witness_commitment = options.expect_witness_commitment,
            .debug_context = debug_context,
        })};
    if (!witness) return Consensus::Unexpected<BlockCheckError>{witness.error()};

    return BlockContextualBodyValidation{
        .facts = input.Facts(),
        .checked_witness_commitment = options.expect_witness_commitment && input.Facts().witness_commitment_index.has_value(),
    };
}

BlockCheckResult<BlockContextualBodyValidation> ValidateBlockContextualBodyStage(const CBlock& block, const BlockContextualBodyOptions& options, const char* debug_context)
{
    return ValidateBlockContextualBodyStage(
        BlockContextualBodyValidationView{block},
        options,
        debug_context);
}

BlockCheckResult<void> ValidateBlockContextualStage(const BlockContextualValidationView& input, const BlockContextualConsensusOptions& options)
{
    const auto header{CheckBlockContextualHeaderRules(input.Header(), options.header)};
    if (!header) return Consensus::Unexpected<BlockCheckError>{header.error()};

    const auto body{ValidateBlockContextualBodyStage(input.Body(), options.body, __func__)};
    if (!body) return Consensus::Unexpected<BlockCheckError>{body.error()};

    return {};
}

BlockCheckResult<void> ValidateBlockContextualStage(const CBlock& block, const BlockContextualConsensusOptions& options)
{
    return ValidateBlockContextualStage(
        BlockContextualValidationView{block},
        options);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommitStages(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options)
{
    const auto structural{ValidateBlockStructuralStage(input.StructuralView(), structural_options)};
    if (!structural) {
        return Consensus::Unexpected<BlockConsensusStageError>{BuildBlockConsensusStageError(
            BlockConsensusStage::Structural,
            structural.error())};
    }

    const auto contextual{ValidateBlockContextualStage(input.ContextualView(), contextual_options)};
    if (!contextual) {
        return Consensus::Unexpected<BlockConsensusStageError>{BuildBlockConsensusStageError(
            BlockConsensusStage::Contextual,
            contextual.error())};
    }

    BlockConsensusPipeline pipeline{input.Transactions(), consensus_context};
    auto spend{pipeline.ValidateAndCompleteSpendStage(workspace, script_checker, spend_options)};
    if (!spend) {
        return Consensus::Unexpected<BlockConsensusStageError>{BuildBlockConsensusStageError(
            BlockConsensusStage::Spend,
            spend.error())};
    }

    return std::move(*spend);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommitStages(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options)
{
    const auto input{BuildBlockPrecommitValidationView(block)};
    return ValidateBlockPrecommitStages(
        input,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommit(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options)
{
    return ValidateBlockPrecommitStages(
        input,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateBlockPrecommit(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options)
{
    return ValidateBlockPrecommitStages(
        block,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options);
}

BlockConsensusStageResult<void> CommitBlockStageEffects(const BlockCommitContext& commit_context, const BlockSpendEffects& effects, BlockRevertDataWriter& revert_data_writer, BlockSpendStateCommitter& spend_state_committer, BlockMetadataCommitter& metadata_committer)
{
    const auto commit{CommitBlockEffects(commit_context, effects, revert_data_writer, spend_state_committer, metadata_committer)};
    if (!commit) {
        return Consensus::Unexpected<BlockConsensusStageError>{BuildBlockConsensusStageError(
            BlockConsensusStage::Commit,
            commit.error())};
    }
    return {};
}

BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlockStages(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer)
{
    auto effects{ValidateBlockPrecommitStages(
        input,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options)};
    if (!effects) {
        return Consensus::Unexpected<BlockConsensusStageError>{effects.error()};
    }

    const auto commit{CommitBlockStageEffects(consensus_context.commit, *effects, revert_data_writer, spend_state_committer, metadata_committer)};
    if (!commit) {
        return Consensus::Unexpected<BlockConsensusStageError>{commit.error()};
    }

    return std::move(*effects);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlockStages(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer)
{
    const auto input{BuildBlockPrecommitValidationView(block)};
    return ValidateAndCommitBlockStages(
        input,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options,
        revert_data_writer,
        spend_state_committer,
        metadata_committer);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlock(
    const BlockPrecommitValidationView& input,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer)
{
    return ValidateAndCommitBlockStages(
        input,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options,
        revert_data_writer,
        spend_state_committer,
        metadata_committer);
}

BlockConsensusStageResult<BlockSpendEffects> ValidateAndCommitBlock(
    const CBlock& block,
    const BlockStructuralConsensusOptions& structural_options,
    const BlockContextualConsensusOptions& contextual_options,
    const BlockConsensusContext& consensus_context,
    BlockSpendWorkspace& workspace,
    BlockScriptChecker& script_checker,
    const BlockSpendConsensusOptions& spend_options,
    BlockRevertDataWriter& revert_data_writer,
    BlockSpendStateCommitter& spend_state_committer,
    BlockMetadataCommitter& metadata_committer)
{
    return ValidateAndCommitBlockStages(
        block,
        structural_options,
        contextual_options,
        consensus_context,
        workspace,
        script_checker,
        spend_options,
        revert_data_writer,
        spend_state_committer,
        metadata_committer);
}

BlockConsensusPipeline::BlockConsensusPipeline(std::span<const CTransactionRef> transactions, BlockConsensusContext context)
    : m_transactions{transactions}, m_context{context}
{
}

BlockConsensusPipeline::BlockConsensusPipeline(const CBlock& block, BlockConsensusContext context)
    : BlockConsensusPipeline{block.vtx, context}
{
}

BlockSpendResult<BlockSpendEffects> BlockConsensusPipeline::ValidateAndStageSpend(BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendConsensusOptions& options) const
{
    return ValidateAndStageBlockTransactions(m_transactions, workspace, script_checker, m_context.spend, options);
}

BlockSpendResult<BlockSpendEffects> BlockConsensusPipeline::ValidateAndCompleteSpendStage(BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendConsensusOptions& options) const
{
    auto spend_effects{ValidateAndStageSpend(workspace, script_checker, options)};
    return CompleteSpendStage(std::move(spend_effects), script_checker);
}

BlockSpendResult<BlockSpendEffects> BlockConsensusPipeline::CompleteSpendStage(BlockSpendResult<BlockSpendEffects> spend_effects, BlockScriptChecker& script_checker) const
{
    std::optional<BlockSpendError> spend_error;
    if (!spend_effects) {
        spend_error = spend_effects.error();
    } else {
        const auto coinbase_check{CheckCoinbaseReward(*spend_effects)};
        if (!coinbase_check) spend_error = coinbase_check.error();
    }

    const auto script_checks{CompleteScriptChecks(script_checker)};
    if (!script_checks && !spend_error) {
        spend_error = script_checks.error();
    }

    if (spend_error) {
        return Consensus::Unexpected<BlockSpendError>{*spend_error};
    }
    return spend_effects;
}

BlockSpendResult<void> BlockConsensusPipeline::CheckCoinbaseReward(const BlockSpendEffects& effects) const
{
    const CAmount block_reward{effects.fees + m_context.block_subsidy};
    assert(!m_transactions.empty());
    const auto coinbase_check{CheckCoinbasePaysNoMoreThan(*m_transactions[0], block_reward)};
    if (!coinbase_check) {
        return Consensus::Unexpected<BlockSpendError>{coinbase_check.error()};
    }
    return {};
}

BlockSpendResult<void> BlockConsensusPipeline::CompleteScriptChecks(BlockScriptChecker& script_checker) const
{
    return script_checker.Complete();
}

} // namespace Consensus
