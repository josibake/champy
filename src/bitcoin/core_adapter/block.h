// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_CORE_ADAPTER_BLOCK_H
#define BITCOIN_BITCOIN_CORE_ADAPTER_BLOCK_H

#include <bitcoin/chain_graph/chain_graph.h>
#include <bitcoin/core_adapter/transaction.h>
#include <bitcoin/protocol/block.h>
#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/protocol/hash.h>

#include <cstdint>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

namespace bitcoin::core_adapter {

[[nodiscard]] hash256 to_hash256(const uint256& value) noexcept;
[[nodiscard]] block_hash to_block_hash(const uint256& value) noexcept;
[[nodiscard]] chain_work to_chain_work(const arith_uint256& value) noexcept;
[[nodiscard]] block_header to_block_header(const CBlockHeader& header) noexcept;
[[nodiscard]] block to_block(const CBlock& block);
[[nodiscard]] uint256 to_uint256(hash256 value) noexcept;
[[nodiscard]] uint256 to_uint256(block_hash value) noexcept;
[[nodiscard]] CBlockHeader to_core_block_header(const block_header& header) noexcept;
[[nodiscard]] CBlock to_core_block(const block& block);
[[nodiscard]] graph_update_result add_header(block_index_graph& graph, const CBlockIndex& index);

// Copies Core status bits as well as headers/work. Callers taking a live node
// snapshot must hold Core's chainstate lock while passing CBlockIndex objects.
[[nodiscard]] block_index_graph to_chain_graph_snapshot(const CBlockIndex& tip);

} // namespace bitcoin::core_adapter

#endif // BITCOIN_BITCOIN_CORE_ADAPTER_BLOCK_H
