// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_DETAIL_HASH_WRITER_H
#define BITCOIN_BITCOIN_PROTOCOL_DETAIL_HASH_WRITER_H

#include <bitcoin/protocol/hash.h>

#include <cstddef>
#include <span>

namespace bitcoin::protocol_detail {

[[nodiscard]] hash256 double_sha256(std::span<const std::byte> bytes);

} // namespace bitcoin::protocol_detail

#endif // BITCOIN_BITCOIN_PROTOCOL_DETAIL_HASH_WRITER_H
