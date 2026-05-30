// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/core_coins_block_connection_state.h>

#include <coins.h>
#include <consensus/expected.h>
#include <validation/coins_view_spend_state.h>
#include <validation/core_block_commit_adapters.h>

#include <cassert>
#include <memory>
#include <utility>

namespace validation {
namespace {

class CoreCoinsBlockConnectionAttemptGuard final : public BlockConnectionAttemptGuard {
public:
    explicit CoreCoinsBlockConnectionAttemptGuard(CCoinsViewCache& coins) : m_coins{coins}, m_reset_guard{coins.CreateResetGuard()} {}

    void Commit() override
    {
        assert(!m_committed);
        m_coins.Flush(/*reallocate_cache=*/false);
        m_committed = true;
    }

private:
    CCoinsViewCache& m_coins;
    CCoinsViewCache::ResetGuard m_reset_guard;
    bool m_committed{false};
};

class CoreCoinsBlockConnectionSpendState final : public BlockConnectionSpendState {
public:
    CoreCoinsBlockConnectionSpendState(CCoinsViewCache& coins, std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times)
        : m_workspace{coins, std::move(sequence_lock_times)},
          m_committer{m_workspace.StagedCoins(), coins}
    {
    }

    [[nodiscard]] Consensus::BlockSpendWorkspace& Workspace() override { return m_workspace; }
    [[nodiscard]] Consensus::BlockSpendStateCommitter& Committer() override { return m_committer; }

private:
    CoinsViewBlockSpendWorkspace m_workspace;
    CoreBlockSpendStateCommitter m_committer;
};

} // namespace

uint256 CoreCoinsBlockConnectionState::BestBlock() const
{
    return m_coins.GetBestBlock();
}

void CoreCoinsBlockConnectionState::SetBestBlock(const uint256& block_hash)
{
    m_coins.SetBestBlock(block_hash);
}

std::unique_ptr<BlockConnectionAttemptGuard> CoreCoinsBlockConnectionState::BeginConnectionAttempt()
{
    return std::make_unique<CoreCoinsBlockConnectionAttemptGuard>(m_coins);
}

Consensus::BlockSpendResult<std::unique_ptr<BlockConnectionSpendState>> CoreCoinsBlockConnectionState::BeginBlockSpend(
    const Consensus::BlockSpendContext&,
    std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times)
{
    std::unique_ptr<BlockConnectionSpendState> state{std::make_unique<CoreCoinsBlockConnectionSpendState>(m_coins, std::move(sequence_lock_times))};
    return std::move(state);
}

} // namespace validation
