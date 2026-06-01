// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/snapshot_block_connection_state.h>

#include <consensus/expected.h>

#include <memory>
#include <utility>

namespace validation {
namespace {

class NoopBlockConnectionAttemptGuard final : public BlockConnectionAttemptGuard
{
public:
    void Commit() override {}
};

class SnapshotBlockConnectionSpendState final : public BlockConnectionSpendState
{
public:
    SnapshotBlockConnectionSpendState(
        std::unique_ptr<Consensus::BlockSpendWorkspace> workspace)
        : m_workspace{std::move(workspace)}
    {
    }

    [[nodiscard]] Consensus::BlockSpendWorkspace& Workspace() override { return *m_workspace; }

private:
    std::unique_ptr<Consensus::BlockSpendWorkspace> m_workspace;
};

} // namespace

uint256 SnapshotBlockConnectionState::BestBlock() const
{
    return m_best_block;
}

void SnapshotBlockConnectionState::SetBestBlock(const uint256& block_hash)
{
    m_best_block = block_hash;
}

std::unique_ptr<BlockConnectionAttemptGuard> SnapshotBlockConnectionState::BeginConnectionAttempt()
{
    return std::make_unique<NoopBlockConnectionAttemptGuard>();
}

Consensus::BlockSpendResult<std::unique_ptr<BlockConnectionSpendState>> SnapshotBlockConnectionState::BeginBlockSpend(
    const Consensus::BlockSpendContext& context,
    std::shared_ptr<const Consensus::SequenceLockTimeView>)
{
    auto workspace{m_spend_state.BeginBlockSpend(context)};
    if (!workspace) return Consensus::Unexpected<Consensus::BlockSpendError>{workspace.error()};

    std::unique_ptr<BlockConnectionSpendState> spend_state{
        std::make_unique<SnapshotBlockConnectionSpendState>(std::move(*workspace))};
    return std::move(spend_state);
}

std::optional<Consensus::CoinSnapshot> SnapshotBlockConnectionState::GetCoin(const COutPoint& outpoint) const
{
    return m_spend_state.GetCoin(outpoint);
}

void SnapshotBlockConnectionState::AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin)
{
    m_spend_state.AddCoin(outpoint, std::move(coin));
}

void SnapshotBlockConnectionState::AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin, int64_t previous_median_time_past)
{
    m_spend_state.AddCoin(outpoint, std::move(coin), previous_median_time_past);
}

Consensus::BlockSpendStateCommitter& SnapshotBlockConnectionState::Committer()
{
    return m_spend_state;
}

} // namespace validation
