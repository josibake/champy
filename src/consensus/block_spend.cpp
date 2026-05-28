// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_spend.h>

#include <consensus/consensus.h>
#include <consensus/predicates.h>
#include <consensus/sequence_locks.h>
#include <consensus/sigops.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Consensus {

namespace {

std::string FormatConsensusAmount(CAmount amount)
{
    static_assert(COIN > 1);

    int64_t quotient{amount / COIN};
    int64_t remainder{amount % COIN};
    if (amount < 0) {
        quotient = -quotient;
        remainder = -remainder;
    }

    std::string remainder_part{std::to_string(remainder)};
    remainder_part.insert(0, 8 - remainder_part.size(), '0');

    std::string formatted{std::to_string(quotient) + "." + remainder_part};
    const std::size_t decimal_position{formatted.find('.')};
    while (formatted.size() > decimal_position + 3 && formatted.back() == '0') {
        formatted.pop_back();
    }

    if (amount < 0) {
        formatted.insert(uint32_t{0}, 1, '-');
    }
    return formatted;
}

BlockSpendError InvalidBlockForSpend(const std::string& reject_reason, const std::string& debug_message)
{
    return BlockSpendError{
        .issue = BlockConsensusIssue::Consensus,
        .reject_reason = reject_reason,
        .debug_message = debug_message,
    };
}

BlockSpendError InvalidTransactionForBlock(const CTransaction& tx, const std::string& reject_reason, const std::string& debug_message)
{
    return InvalidBlockForSpend(reject_reason, debug_message + " in transaction " + tx.GetHash().ToString());
}

std::vector<SequenceLockInputContext> BuildSequenceLockInputContext(const CTransaction& tx, const std::vector<CoinSnapshot>& coins, const TransactionSpendContext& context, int locktime_flags)
{
    const bool enforce_sequence_locks{tx.version >= 2 && locktime_flags & LOCKTIME_VERIFY_SEQUENCE};
    std::vector<SequenceLockInputContext> input_contexts;
    input_contexts.reserve(tx.vin.size());

    assert(coins.size() == tx.vin.size());
    for (std::size_t i{0}; i < tx.vin.size(); ++i) {
        const CTxIn& txin{tx.vin[i]};
        const int coin_height{coins[i].height};
        int64_t previous_median_time_past{0};
        if (enforce_sequence_locks &&
            HasRelativeLocktime(txin) &&
            RelativeLocktimeIsTime(txin)) {
            previous_median_time_past = context.sequence_lock_times.PreviousMedianTimePast(txin.prevout, coin_height);
        }
        input_contexts.push_back({
            .height = coin_height,
            .previous_median_time_past = previous_median_time_past,
        });
    }

    return input_contexts;
}

BlockSpendResult<CAmount> CheckTransactionInputValuesForBlock(const CTransaction& tx, const std::vector<CoinSnapshot>& coins, int spend_height)
{
    assert(coins.size() == tx.vin.size());

    CAmount value_in{0};
    for (const CoinSnapshot& coin : coins) {
        if (coin.is_coinbase && spend_height - coin.height < COINBASE_MATURITY) {
            return Consensus::Unexpected<BlockSpendError>{InvalidTransactionForBlock(
                tx,
                "bad-txns-premature-spend-of-coinbase",
                "tried to spend coinbase at depth " + std::to_string(spend_height - coin.height))};
        }

        value_in += coin.output.nValue;
        if (!MoneyRange(coin.output.nValue) || !MoneyRange(value_in)) {
            return Consensus::Unexpected<BlockSpendError>{InvalidTransactionForBlock(tx, "bad-txns-inputvalues-outofrange", "")};
        }
    }

    const CAmount value_out{tx.GetValueOut()};
    if (value_in < value_out) {
        return Consensus::Unexpected<BlockSpendError>{InvalidTransactionForBlock(
            tx,
            "bad-txns-in-belowout",
            "value in (" + FormatConsensusAmount(value_in) + ") < value out (" + FormatConsensusAmount(value_out) + ")")};
    }

    const CAmount tx_fee{value_in - value_out};
    if (!MoneyRange(tx_fee)) {
        return Consensus::Unexpected<BlockSpendError>{InvalidTransactionForBlock(tx, "bad-txns-fee-outofrange", "")};
    }

    return tx_fee;
}

BlockSpendResult<std::vector<CoinSnapshot>> GetTransactionInputCoinsForBlock(const CTransaction& tx, const SpendStateView& spend_state)
{
    std::vector<CoinSnapshot> coins;
    coins.reserve(tx.vin.size());

    for (const CTxIn& txin : tx.vin) {
        const auto coin{spend_state.GetCoin(txin.prevout)};
        if (!coin) {
            return Consensus::Unexpected<BlockSpendError>{InvalidTransactionForBlock(tx, "bad-txns-inputs-missingorspent", "CheckTxInputs: inputs missing/spent")};
        }
        coins.push_back(*coin);
    }

    return coins;
}

int64_t GetTransactionSigOpCostForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, script_verify_flags flags)
{
    int64_t sigop_cost{Consensus::GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR};

    if (IsCoinbase(tx)) {
        assert(input_coins.empty());
        return sigop_cost;
    }

    assert(input_coins.size() == tx.vin.size());
    for (std::size_t input_index{0}; input_index < tx.vin.size(); ++input_index) {
        const CTxIn& txin{tx.vin[input_index]};
        const CTxOut& prevout{input_coins[input_index].output};
        if (flags & SCRIPT_VERIFY_P2SH && IsPayToScriptHash(prevout.scriptPubKey)) {
            sigop_cost += prevout.scriptPubKey.GetSigOpCount(txin.scriptSig) * WITNESS_SCALE_FACTOR;
        }
        sigop_cost += CountWitnessSigOps(txin.scriptSig, prevout.scriptPubKey, txin.scriptWitness, flags);
    }

    return sigop_cost;
}

} // namespace

BlockSpendResult<void> CheckBlockNoUnspentOutputOverwrite(std::span<const CTransactionRef> transactions, const SpendStateView& spend_state)
{
    for (const auto& tx : transactions) {
        const Txid txid{tx->GetHash()};
        for (std::size_t output_index{0}; output_index < tx->vout.size(); ++output_index) {
            if (spend_state.HaveCoin(COutPoint{txid, static_cast<uint32_t>(output_index)})) {
                return Consensus::Unexpected<BlockSpendError>{InvalidBlockForSpend("bad-txns-BIP30", "tried to overwrite transaction")};
            }
        }
    }

    return {};
}

BlockSpendResult<void> CheckCoinbasePaysNoMoreThan(const CTransaction& coinbase, CAmount max_reward)
{
    assert(IsCoinbase(coinbase));

    const CAmount coinbase_value{coinbase.GetValueOut()};
    if (coinbase_value > max_reward) {
        return Consensus::Unexpected<BlockSpendError>{InvalidBlockForSpend(
            "bad-cb-amount",
            "coinbase pays too much (actual=" + std::to_string(coinbase_value) + " vs limit=" + std::to_string(max_reward) + ")")};
    }

    return {};
}

BlockSpendResult<TransactionInputCheck> CheckTransactionInputsForBlock(const CTransaction& tx, const SpendStateView& spend_state, const TransactionSpendContext& context, int locktime_flags)
{
    assert(!IsCoinbase(tx));

    auto coins{GetTransactionInputCoinsForBlock(tx, spend_state)};
    if (!coins) return Consensus::Unexpected<BlockSpendError>{std::move(coins).error()};

    const auto tx_fee{CheckTransactionInputValuesForBlock(tx, *coins, context.block_height)};
    if (!tx_fee) return Consensus::Unexpected<BlockSpendError>{tx_fee.error()};

    const std::vector<SequenceLockInputContext> sequence_lock_inputs{BuildSequenceLockInputContext(tx, *coins, context, locktime_flags)};
    const Consensus::SequenceLockContext sequence_context{
        .block_height = context.block_height,
        .previous_median_time_past = context.previous_median_time_past,
        .inputs = sequence_lock_inputs,
    };

    const auto lock_pair{Consensus::CalculateSequenceLocks(tx, locktime_flags, sequence_context)};
    if (!Consensus::EvaluateSequenceLocks(sequence_context, lock_pair)) {
        return Consensus::Unexpected<BlockSpendError>{InvalidBlockForSpend(
            "bad-txns-nonfinal",
            "contains a non-BIP68-final transaction " + tx.GetHash().ToString())};
    }

    return TransactionInputCheck{
        .fee = *tx_fee,
        .input_coins = std::move(*coins),
    };
}

BlockSpendResult<CAmount> AddTransactionFeeForBlock(CAmount block_fees, CAmount tx_fee)
{
    const CAmount total_fees{block_fees + tx_fee};
    if (!MoneyRange(total_fees)) {
        return Consensus::Unexpected<BlockSpendError>{InvalidBlockForSpend(
            "bad-txns-accumulated-fee-outofrange",
            "accumulated fee in the block out of range")};
    }

    return total_fees;
}

BlockSpendResult<int64_t> AddTransactionSigOpCostForBlock(const CTransaction& tx, std::span<const CoinSnapshot> input_coins, script_verify_flags flags, int64_t block_sigop_cost)
{
    const int64_t total_sigop_cost{block_sigop_cost + GetTransactionSigOpCostForBlock(tx, input_coins, flags)};
    if (total_sigop_cost > MAX_BLOCK_SIGOPS_COST) {
        return Consensus::Unexpected<BlockSpendError>{InvalidBlockForSpend("bad-blk-sigops", "too many sigops")};
    }

    return total_sigop_cost;
}

TransactionScriptCheckPlan BuildTransactionScriptCheckPlan(const CTransactionRef& tx_ref, std::span<const CoinSnapshot> input_coins, script_verify_flags flags)
{
    const CTransaction& tx{*tx_ref};
    assert(!IsCoinbase(tx));
    assert(input_coins.size() == tx.vin.size());

    std::vector<CTxOut> spent_outputs;
    spent_outputs.reserve(input_coins.size());
    for (const CoinSnapshot& coin : input_coins) {
        spent_outputs.push_back(coin.output);
    }

    return TransactionScriptCheckPlan{
        .tx = tx_ref,
        .flags = flags,
        .spent_outputs = std::move(spent_outputs),
    };
}

BlockSpendResult<TransactionSpendResult> ValidateTransactionSpendForBlock(const CTransactionRef& tx_ref, const SpendStateView& spend_state, BlockScriptChecker& script_checker, const TransactionSpendContext& spend_context, const BlockSpendConsensusOptions& options, const BlockSpendAccounting& accounting)
{
    const CTransaction& tx{*tx_ref};
    BlockSpendAccounting result{accounting};
    std::vector<CoinSnapshot> input_coins;

    if (!IsCoinbase(tx)) {
        auto input_check{CheckTransactionInputsForBlock(tx, spend_state, spend_context, options.locktime_flags)};
        if (!input_check) {
            return Consensus::Unexpected<BlockSpendError>{std::move(input_check).error()};
        }

        const auto fees{AddTransactionFeeForBlock(result.fees, input_check->fee)};
        if (!fees) {
            return Consensus::Unexpected<BlockSpendError>{fees.error()};
        }
        result.fees = *fees;
        input_coins = std::move(input_check->input_coins);
    }

    const auto sigop_cost{AddTransactionSigOpCostForBlock(tx, input_coins, options.script_flags, result.sigop_cost)};
    if (!sigop_cost) {
        return Consensus::Unexpected<BlockSpendError>{sigop_cost.error()};
    }
    result.sigop_cost = *sigop_cost;

    if (!IsCoinbase(tx) && script_checker.WantsChecks()) {
        const TransactionScriptCheckPlan script_check_plan{BuildTransactionScriptCheckPlan(tx_ref, input_coins, options.script_flags)};
        const auto script_check{script_checker.Check(script_check_plan)};
        if (!script_check) {
            return Consensus::Unexpected<BlockSpendError>{script_check.error()};
        }
    }

    TransactionCoinEffects coin_effects{BuildTransactionCoinEffectsForBlock(tx, input_coins, spend_context.block_height)};

    return TransactionSpendResult{
        .accounting = result,
        .coin_effects = std::move(coin_effects),
    };
}

BlockSpendResult<BlockSpendEffects> ValidateAndStageBlockTransactions(std::span<const CTransactionRef> transactions, BlockSpendWorkspace& workspace, BlockScriptChecker& script_checker, const BlockSpendContext& spend_context, const BlockSpendConsensusOptions& options)
{
    assert(!transactions.empty());

    if (options.check_no_unspent_output_overwrite) {
        const auto overwrite_check{CheckBlockNoUnspentOutputOverwrite(transactions, workspace.StagedSpendView())};
        if (!overwrite_check) {
            return Consensus::Unexpected<BlockSpendError>{overwrite_check.error()};
        }
    }

    BlockSpendEffects effects;
    effects.transaction_effects.reserve(transactions.size());
    const TransactionSpendContext transaction_context{
        .block_height = spend_context.block_height,
        .previous_median_time_past = spend_context.previous_median_time_past,
        .sequence_lock_times = workspace.SequenceLockTimes(),
    };
    for (unsigned int i = 0; i < transactions.size(); i++) {
        const CTransactionRef& tx{transactions[i]};

        effects.inputs += tx->vin.size();

        auto spend_result{ValidateTransactionSpendForBlock(tx, workspace.StagedSpendView(), script_checker, transaction_context, options, BlockSpendAccounting{.fees = effects.fees, .sigop_cost = effects.sigop_cost})};
        if (!spend_result) {
            return Consensus::Unexpected<BlockSpendError>{spend_result.error()};
        }
        effects.fees = spend_result->accounting.fees;
        effects.sigop_cost = spend_result->accounting.sigop_cost;

        effects.transaction_effects.push_back(std::move(spend_result->coin_effects));
        const auto stage_effects{workspace.StageTransactionEffectsForIntraBlockView(effects.transaction_effects.back(), i)};
        if (!stage_effects) {
            return Consensus::Unexpected<BlockSpendError>{stage_effects.error()};
        }
    }

    return effects;
}

} // namespace Consensus
