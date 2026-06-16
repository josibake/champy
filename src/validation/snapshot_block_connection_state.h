// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_SNAPSHOT_BLOCK_CONNECTION_STATE_H
#define BITCOIN_VALIDATION_SNAPSHOT_BLOCK_CONNECTION_STATE_H

#include <consensus/snapshot_spend_state.h>
#include <uint256.h>
#include <validation/block_connection_state.h>

#include <memory>
#include <optional>

class COutPoint;

namespace validation {

/**
 * In-memory block connection state backed by Consensus::SnapshotSpendState.
 *
 * This is useful for tests, conformance checks, and experimental spend-state
 * backends where validation should run without Core's coins cache.
 */
class SnapshotBlockConnectionState final : public BlockConnectionState
{
public:
    [[nodiscard]] uint256 BestBlock() const override;
    void SetBestBlock(const uint256& block_hash) override;
    [[nodiscard]] std::unique_ptr<BlockConnectionAttemptGuard> BeginConnectionAttempt() override;
    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<BlockConnectionSpendState>> BeginBlockSpend(
        const Consensus::BlockSpendContext& context,
        std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times) override;

    [[nodiscard]] std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const;
    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin);
    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin, int64_t previous_median_time_past);
    [[nodiscard]] Consensus::SpendCommitter& Committer();

private:
    uint256 m_best_block;
    Consensus::SnapshotSpendState m_spend_state;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_SNAPSHOT_BLOCK_CONNECTION_STATE_H
