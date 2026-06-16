// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_TRANSACTION_H
#define BITCOIN_BITCOIN_VALIDATION_TRANSACTION_H

#include <bitcoin/protocol/transaction.h>
#include <bitcoin/validation/context.h>
#include <bitcoin/validation/result.h>

#include <cstddef>

namespace bitcoin {

enum class transaction_kind {
    ordinary,
    coinbase,
};

class transaction_facts
{
public:
    [[nodiscard]] txid id() const noexcept { return m_id; }
    [[nodiscard]] wtxid witness_id() const noexcept { return m_witness_id; }
    [[nodiscard]] amount output_value() const noexcept { return m_output_value; }
    [[nodiscard]] transaction_kind kind() const noexcept { return m_kind; }
    [[nodiscard]] bool coinbase() const noexcept { return m_kind == transaction_kind::coinbase; }
    [[nodiscard]] std::size_t stripped_weight() const noexcept { return m_stripped_weight; }

    friend constexpr bool operator==(const transaction_facts&, const transaction_facts&) noexcept = default;

private:
    friend verify_result<transaction_facts> assess_transaction_intrinsic(
        const transaction& tx,
        const transaction_context& context);

    constexpr transaction_facts(
        bitcoin::txid id,
        bitcoin::wtxid witness_id,
        amount output_value,
        transaction_kind kind,
        std::size_t stripped_weight) noexcept :
        m_id{id},
        m_witness_id{witness_id},
        m_output_value{output_value},
        m_kind{kind},
        m_stripped_weight{stripped_weight}
    {
    }

    bitcoin::txid m_id;
    bitcoin::wtxid m_witness_id;
    amount m_output_value;
    transaction_kind m_kind;
    std::size_t m_stripped_weight{0};
};

// Expert building block for intrinsic transaction rules. Prefer verify(tx)
// unless composing a larger validation operation with explicit context.
[[nodiscard]] verify_result<transaction_facts> assess_transaction_intrinsic(
    const transaction& tx,
    const transaction_context& context);

// Expert building block for contextual transaction finality. Prefer
// verify(tx, context) unless the stage distinction is part of the caller's
// contract.
[[nodiscard]] verify_result<transaction_facts> assess_transaction_finality(
    const transaction& tx,
    const transaction_finality_context& context);

[[nodiscard]] inline verify_result<transaction_facts> verify(const transaction& tx)
{
    return assess_transaction_intrinsic(tx, transaction_context{});
}

[[nodiscard]] inline verify_result<transaction_facts> verify(
    const transaction& tx,
    const transaction_finality_context& context)
{
    return assess_transaction_finality(tx, context);
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_TRANSACTION_H
