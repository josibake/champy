// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_CORE_COINS_BLOCK_CONNECTION_STATE_H
#define BITCOIN_VALIDATION_CORE_COINS_BLOCK_CONNECTION_STATE_H

#include <validation/block_connection_state.h>

class CCoinsViewCache;

namespace validation {

class CoreCoinsBlockConnectionState final : public BlockConnectionState {
public:
    explicit CoreCoinsBlockConnectionState(CCoinsViewCache& coins) : m_coins{coins} {}

    [[nodiscard]] uint256 BestBlock() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void SetBestBlock(const uint256& block_hash) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::unique_ptr<BlockConnectionAttemptGuard> BeginConnectionAttempt() override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<BlockConnectionSpendState>> BeginBlockSpend(
        const Consensus::BlockSpendContext& context,
        std::shared_ptr<const Consensus::SequenceLockTimeView> sequence_lock_times) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    CCoinsViewCache& m_coins;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_CORE_COINS_BLOCK_CONNECTION_STATE_H
