// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_BLOCK_CONNECTION_H
#define BITCOIN_VALIDATION_BLOCK_CONNECTION_H

#include <consensus/block_check.h>
#include <kernel/cs_main.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class Chainstate;
class BlockValidationState;

/**
 * ConnectBlock adapter options.
 *
 * These keep block-check policy, script-cache policy, and commit behavior
 * explicit at Core's existing validation entry point.
 */
struct ConnectBlockOptions {
    Consensus::BlockCheckOptions block_check_options{};
    bool cache_script_results{false};
    bool commit{true};
};

namespace validation {

/**
 * Core block-connection request.
 *
 * This is still a Core validation request: it carries Core's current chainstate,
 * block index, and coins cache capabilities. The request exists so the legacy
 * ConnectBlock entry point can become a thin adapter and block connection can
 * move toward a smaller validation engine.
 */
struct BlockConnectionRequest {
    Chainstate& chainstate;
    const CBlock& block;
    CBlockIndex& block_index;
    CCoinsViewCache& coins_view;
    ConnectBlockOptions options{};
};

class BlockConnectionEngine final {
public:
    [[nodiscard]] bool Connect(const BlockConnectionRequest& request, BlockValidationState& state) const
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

} // namespace validation

#endif // BITCOIN_VALIDATION_BLOCK_CONNECTION_H
