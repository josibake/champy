// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_TRANSACTION_H
#define BITCOIN_BITCOIN_PROTOCOL_TRANSACTION_H

#include <compare>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <bitcoin/protocol/amount.h>
#include <bitcoin/protocol/hash.h>
#include <bitcoin/protocol/script.h>

namespace bitcoin {

class tx_output_index
{
public:
    constexpr tx_output_index() noexcept = default;
    constexpr explicit tx_output_index(std::uint32_t value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return m_value; }

    friend constexpr bool operator==(const tx_output_index&, const tx_output_index&) noexcept = default;
    friend constexpr auto operator<=>(const tx_output_index&, const tx_output_index&) noexcept = default;

private:
    std::uint32_t m_value{0};
};

class outpoint
{
public:
    constexpr outpoint() noexcept = default;
    constexpr outpoint(bitcoin::txid id, tx_output_index index) noexcept : m_txid{id}, m_index{index} {}

    [[nodiscard]] static constexpr outpoint null() noexcept
    {
        return outpoint{bitcoin::txid{}, tx_output_index{0xffffffffU}};
    }

    [[nodiscard]] constexpr bitcoin::txid txid() const noexcept { return m_txid; }
    [[nodiscard]] constexpr tx_output_index index() const noexcept { return m_index; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return *this == null(); }

    friend constexpr bool operator==(const outpoint&, const outpoint&) noexcept = default;
    friend constexpr auto operator<=>(const outpoint&, const outpoint&) noexcept = default;

private:
    bitcoin::txid m_txid;
    tx_output_index m_index;
};

class witness_item
{
public:
    witness_item() noexcept = default;
    explicit witness_item(std::span<const std::byte> bytes) : m_bytes{bytes.begin(), bytes.end()} {}

    [[nodiscard]] bool empty() const noexcept { return m_bytes.empty(); }

    [[nodiscard]] friend std::span<const std::byte> as_bytes(const witness_item& value) noexcept
    {
        return value.m_bytes;
    }

    friend bool operator==(const witness_item&, const witness_item&) noexcept = default;

private:
    std::vector<std::byte> m_bytes;
};

class tx_input
{
public:
    tx_input() noexcept = default;
    tx_input(outpoint previous_output, script script_sig, std::uint32_t sequence, std::vector<witness_item> witness = {}) :
        m_previous_output{previous_output},
        m_script{std::move(script_sig)},
        m_sequence{sequence},
        m_witness{std::move(witness)}
    {
    }

    [[nodiscard]] bitcoin::outpoint previous_output() const noexcept { return m_previous_output; }
    [[nodiscard]] script_ref script() const noexcept { return script_ref{m_script}; }
    [[nodiscard]] std::uint32_t sequence() const noexcept { return m_sequence; }
    [[nodiscard]] std::span<const witness_item> witness() const noexcept { return m_witness; }

    friend bool operator==(const tx_input&, const tx_input&) noexcept = default;

private:
    bitcoin::outpoint m_previous_output;
    bitcoin::script m_script;
    std::uint32_t m_sequence{0};
    std::vector<witness_item> m_witness;
};

class tx_output
{
public:
    tx_output() noexcept = default;
    tx_output(bitcoin::amount value, bitcoin::script script_pubkey) :
        m_value{value}, m_script{std::move(script_pubkey)}
    {
    }

    [[nodiscard]] bitcoin::amount value() const noexcept { return m_value; }
    [[nodiscard]] script_ref script() const noexcept { return script_ref{m_script}; }

    friend bool operator==(const tx_output&, const tx_output&) noexcept = default;

private:
    bitcoin::amount m_value;
    bitcoin::script m_script;
};

class transaction
{
public:
    transaction() noexcept = default;
    transaction(std::int32_t version, std::vector<tx_input> inputs, std::vector<tx_output> outputs, std::uint32_t locktime) :
        m_version{version}, m_inputs{std::move(inputs)}, m_outputs{std::move(outputs)}, m_locktime{locktime}
    {
    }

    [[nodiscard]] bitcoin::txid id() const;
    [[nodiscard]] bitcoin::wtxid witness_id() const;
    [[nodiscard]] std::int32_t version() const noexcept { return m_version; }
    [[nodiscard]] std::uint32_t locktime() const noexcept { return m_locktime; }
    [[nodiscard]] std::span<const tx_input> inputs() const noexcept { return m_inputs; }
    [[nodiscard]] std::span<const tx_output> outputs() const noexcept { return m_outputs; }

    friend bool operator==(const transaction&, const transaction&) noexcept = default;

private:
    std::int32_t m_version{0};
    std::vector<tx_input> m_inputs;
    std::vector<tx_output> m_outputs;
    std::uint32_t m_locktime{0};
};

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_TRANSACTION_H
