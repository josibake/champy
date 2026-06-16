// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_BLOCK_H
#define BITCOIN_BITCOIN_PROTOCOL_BLOCK_H

#include <span>
#include <utility>
#include <vector>

#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/hash.h>
#include <bitcoin/protocol/transaction.h>

namespace bitcoin {

class block
{
public:
    block() noexcept = default;
    block(block_header header, std::vector<transaction> transactions) :
        m_header{header}, m_transactions{std::move(transactions)}
    {
    }

    [[nodiscard]] bitcoin::block_hash hash() const;
    [[nodiscard]] const block_header& header() const noexcept { return m_header; }
    [[nodiscard]] std::span<const transaction> transactions() const noexcept { return m_transactions; }

    friend bool operator==(const block&, const block&) noexcept = default;

private:
    block_header m_header;
    std::vector<transaction> m_transactions;
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_BLOCK_H
