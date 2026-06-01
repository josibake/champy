// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_BLOCK_SPEND_H
#define BITCOIN_CONSENSUS_BLOCK_SPEND_H

#include <consensus/amount.h>
#include <consensus/coin_effects.h>
#include <consensus/diagnostics.h>
#include <consensus/expected.h>
#include <consensus/spend_state.h>
#include <primitives/transaction.h>
#include <script/verify_flags.h>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Consensus {

struct TransactionSpendContext {
    int block_height;
    int64_t previous_median_time_past;
    const SequenceLockTimeView& sequence_lock_times;
};

struct BlockSpendContext {
    int block_height;
    int64_t previous_median_time_past;
};

struct TransactionInputCheck {
    CAmount fee{0};
    std::vector<CoinSnapshot> input_coins;
};

struct BlockSpendConsensusOptions {
    int locktime_flags{0};
    script_verify_flags script_flags{};
    bool check_no_unspent_output_overwrite{false};
};

struct BlockSpendAccounting {
    CAmount fees{0};
    int64_t sigop_cost{0};
};

struct TransactionSpendResult {
    BlockSpendAccounting accounting;
    TransactionCoinEffects coin_effects;
};

struct TransactionScriptCheckPlan {
    CTransactionRef tx;
    script_verify_flags flags{};
    std::vector<CTxOut> spent_outputs;
};

struct BlockSpendEffects {
    // Ordered one-to-one with the block transaction list. Entry 0 is the
    // coinbase. Commit adapters use this as the portable proof of spent coins,
    // created coins, fees, input count, and sigop accounting.
    std::vector<TransactionCoinEffects> transaction_effects;
    CAmount fees{0};
    int inputs{0};
    int64_t sigop_cost{0};
};

struct BlockSpendStageResult {
    // Spend effects are attempt-local until a committer publishes them.
    // Script checks are value plans and may be executed by a separate runtime.
    BlockSpendEffects effects;
    std::vector<TransactionScriptCheckPlan> script_checks;
};

enum class ScriptCheckPlanCollection {
    Skip,
    Collect,
};

struct BlockSpendError {
    BlockConsensusIssue issue{BlockConsensusIssue::Consensus};
    std::string reject_reason;
    std::string debug_message;
};

template <typename T>
using BlockSpendResult = Consensus::Expected<T, BlockSpendError>;

class BlockScriptChecker {
public:
    virtual ~BlockScriptChecker() = default;

    [[nodiscard]] virtual bool WantsChecks() const { return true; }
    [[nodiscard]] virtual BlockSpendResult<void> Check(const TransactionScriptCheckPlan& check) = 0;
    [[nodiscard]] virtual BlockSpendResult<void> Complete() = 0;
};

class BlockSpendWorkspace {
public:
    virtual ~BlockSpendWorkspace() = default;

    [[nodiscard]] virtual const SpendStateView& StagedSpendView() const = 0;
    [[nodiscard]] virtual const SequenceLockTimeView& SequenceLockTimes() const = 0;
    // Updates this validation attempt's intra-block view only. Final
    // persistence is handled later by BlockSpendStateCommitter. Failure must
    // leave the workspace in a state that can be discarded without changing the
    // parent backend.
    [[nodiscard]] virtual BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const TransactionCoinEffects& coin_effects, unsigned int transaction_index) = 0;
};

class BlockSpendBackend {
public:
    virtual ~BlockSpendBackend() = default;

    // Creates a block-local workspace before any transaction reads or
    // intra-block staging happen. Dropping the workspace must leave the parent
    // spend state unchanged. Backends may choose any internal state model as
    // long as the workspace exposes the same spend, sequence-lock, and staging
    // semantics.
    [[nodiscard]] virtual BlockSpendResult<std::unique_ptr<BlockSpendWorkspace>> BeginBlockSpend(const BlockSpendContext& context) = 0;
};

[[nodiscard]] BlockSpendResult<void> CheckBlockNoUnspentOutputOverwrite(std::span<const CTransactionRef> transactions, const SpendStateView& spend_state);
[[nodiscard]] BlockSpendResult<void> CheckCoinbasePaysNoMoreThan(const CTransaction& coinbase, CAmount max_reward);
[[nodiscard]] BlockSpendResult<CAmount> CheckTransactionInputCoinsForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, const TransactionSpendContext& context, int locktime_flags);
[[nodiscard]] BlockSpendResult<TransactionInputCheck> CheckTransactionInputsForBlock(const CTransaction& tx, const SpendStateView& spend_state, const TransactionSpendContext& context, int locktime_flags);
[[nodiscard]] BlockSpendResult<CAmount> AddTransactionFeeForBlock(CAmount block_fees, CAmount tx_fee);
[[nodiscard]] BlockSpendResult<int64_t> AddTransactionSigOpCostForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, script_verify_flags flags, int64_t block_sigop_cost);
[[nodiscard]] TransactionScriptCheckPlan BuildTransactionScriptCheckPlan(const CTransactionRef& tx, std::span<const CoinSnapshot> input_coins, script_verify_flags flags);
[[nodiscard]] BlockSpendResult<TransactionSpendResult> EvaluateTransactionSpendForBlock(const CTransactionRef& tx, std::span<const CoinSnapshot> input_coins, const TransactionSpendContext& spend_context, const BlockSpendConsensusOptions& options, const BlockSpendAccounting& accounting);
[[nodiscard]] BlockSpendResult<void> SubmitTransactionScriptCheckForBlock(const CTransactionRef& tx, std::span<const CoinSnapshot> input_coins, BlockScriptChecker& script_checker, script_verify_flags flags);
[[nodiscard]] BlockSpendResult<TransactionSpendResult> ValidateTransactionSpendForBlock(const CTransactionRef& tx, std::span<const CoinSnapshot> input_coins, BlockScriptChecker& script_checker, const TransactionSpendContext& spend_context, const BlockSpendConsensusOptions& options, const BlockSpendAccounting& accounting);
[[nodiscard]] BlockSpendResult<TransactionSpendResult> ValidateTransactionSpendForBlock(const CTransactionRef& tx, const SpendStateView& spend_state, BlockScriptChecker& script_checker, const TransactionSpendContext& spend_context, const BlockSpendConsensusOptions& options, const BlockSpendAccounting& accounting);
[[nodiscard]] BlockSpendResult<void> SubmitBlockScriptChecksForSpendStage(std::span<const TransactionScriptCheckPlan> script_checks, BlockScriptChecker& script_checker);
// Validates each transaction against the current staged view and stages its
// coin effects so later transactions in the same block can spend them.
[[nodiscard]] BlockSpendResult<BlockSpendStageResult> ValidateAndStageBlockTransactions(std::span<const CTransactionRef> transactions, BlockSpendWorkspace& workspace, const BlockSpendContext& spend_context, const BlockSpendConsensusOptions& options, ScriptCheckPlanCollection script_check_plans);
[[nodiscard]] BlockSpendResult<BlockSpendEffects> ValidateAndStageBlockTransactions(std::span<const CTransactionRef> transactions, BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendContext& spend_context, const BlockSpendConsensusOptions& options);

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_BLOCK_SPEND_H
