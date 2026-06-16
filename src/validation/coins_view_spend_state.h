// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_VIEW_SPEND_STATE_H
#define BITCOIN_COINS_VIEW_SPEND_STATE_H

#include <consensus/block_spend.h>

#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

class CCoinsViewCache;
class COutPoint;
class CBlockIndex;

namespace validation {

class CoinsViewSpendState final : public Consensus::SpendStateView {
public:
    explicit CoinsViewSpendState(const CCoinsViewCache& coins);

    [[nodiscard]] bool HaveCoin(const COutPoint& outpoint) const override;
    [[nodiscard]] std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override;

private:
    const CCoinsViewCache& m_coins;
};

class CoinsViewSequenceLockTimeView final : public Consensus::SequenceLockTimeView {
public:
    explicit CoinsViewSequenceLockTimeView(int64_t previous_median_time_past);
    CoinsViewSequenceLockTimeView(
        int64_t previous_median_time_past,
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);
    explicit CoinsViewSequenceLockTimeView(const CBlockIndex& block_index);

    [[nodiscard]] int64_t PreviousMedianTimePast(const COutPoint& outpoint, int coin_height) const override;

private:
    int64_t m_previous_median_time_past{0};
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
    mutable std::unordered_map<int, int64_t> m_previous_median_time_past_by_coin_height;
    const CBlockIndex* m_block_index{nullptr};
};

class CoinsViewBlockSpendWorkspace final : public Consensus::BlockSpendWorkspace {
public:
    CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, int64_t previous_median_time_past);
    CoinsViewBlockSpendWorkspace(
        CCoinsViewCache& parent_coins,
        int64_t previous_median_time_past,
        std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);
    CoinsViewBlockSpendWorkspace(
        CCoinsViewCache& parent_coins,
        std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times);
    CoinsViewBlockSpendWorkspace(CCoinsViewCache& parent_coins, const CBlockIndex& block_index);
    ~CoinsViewBlockSpendWorkspace() override;

    [[nodiscard]] const Consensus::SpendStateView& StagedSpendView() const override;
    [[nodiscard]] const Consensus::SequenceLockTimeView& SequenceLockTimes() const override;
    [[nodiscard]] Consensus::BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(const Consensus::TransactionCoinEffects& coin_effects, unsigned int transaction_index) override;
    [[nodiscard]] CCoinsViewCache& StagedCoins();

private:
    std::unique_ptr<CCoinsViewCache> m_staged_coins;
    CoinsViewSpendState m_spend_view;
    std::shared_ptr<const Consensus::SequenceLockTimeView> m_sequence_lock_times;
};

class CoinsViewBlockSpendBackend final : public Consensus::BlockSpendBackend {
public:
    explicit CoinsViewBlockSpendBackend(CCoinsViewCache& parent_coins) : m_parent_coins{parent_coins} {}
    CoinsViewBlockSpendBackend(CCoinsViewCache& parent_coins, std::map<COutPoint, int64_t> previous_median_time_past_by_outpoint);

    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<Consensus::BlockSpendWorkspace>> BeginBlockSpend(const Consensus::BlockSpendContext& context) override;

private:
    CCoinsViewCache& m_parent_coins;
    std::map<COutPoint, int64_t> m_previous_median_time_past_by_outpoint;
};

} // namespace validation

#endif // BITCOIN_COINS_VIEW_SPEND_STATE_H
