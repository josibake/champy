// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_COIN_INDEX_H
#define BITCOIN_BITCOIN_PROTOCOL_COIN_INDEX_H

#include <bitcoin/protocol/chain_view.h>
#include <bitcoin/protocol/result.h>
#include <bitcoin/protocol/transaction.h>

#include <concepts>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace bitcoin {

class coin
{
public:
    coin() noexcept = default;
    coin(tx_output output, block_height height, bool coinbase, median_time_past previous_mtp) :
        m_output{std::move(output)}, m_height{height}, m_coinbase{coinbase}, m_previous_mtp{previous_mtp}
    {
    }

    [[nodiscard]] const tx_output& output() const noexcept { return m_output; }
    [[nodiscard]] block_height height() const noexcept { return m_height; }
    [[nodiscard]] bool coinbase() const noexcept { return m_coinbase; }
    [[nodiscard]] median_time_past previous_median_time_past() const noexcept { return m_previous_mtp; }

    friend bool operator==(const coin&, const coin&) noexcept = default;

private:
    tx_output m_output;
    block_height m_height;
    bool m_coinbase{false};
    median_time_past m_previous_mtp;
};

enum class coin_lookup_state {
    found,
    missing,
    spent,
    unavailable,
    malformed_stored_data,
    interrupted,
    io_failure,
};

enum class coin_lookup_failure {
    unavailable,
    malformed_stored_data,
    interrupted,
    io_failure,
};

[[nodiscard]] constexpr coin_lookup_state state_for(coin_lookup_failure failure) noexcept
{
    switch (failure) {
    case coin_lookup_failure::unavailable:
        return coin_lookup_state::unavailable;
    case coin_lookup_failure::malformed_stored_data:
        return coin_lookup_state::malformed_stored_data;
    case coin_lookup_failure::interrupted:
        return coin_lookup_state::interrupted;
    case coin_lookup_failure::io_failure:
        return coin_lookup_state::io_failure;
    }
    return coin_lookup_state::io_failure;
}

[[nodiscard]] constexpr bool is_failure(coin_lookup_state state) noexcept
{
    return state == coin_lookup_state::unavailable ||
           state == coin_lookup_state::malformed_stored_data ||
           state == coin_lookup_state::interrupted ||
           state == coin_lookup_state::io_failure;
}

class coin_lookup_error
{
public:
    [[nodiscard]] constexpr coin_lookup_failure failure() const noexcept { return m_failure; }
    [[nodiscard]] constexpr std::string_view reason() const noexcept { return m_reason.view(); }

    friend constexpr bool operator==(const coin_lookup_error&, const coin_lookup_error&) noexcept = default;

private:
    friend class coin_lookup_result;

    constexpr coin_lookup_error(coin_lookup_failure failure, static_text reason) noexcept :
        m_failure{failure}, m_reason{reason}
    {
    }

    coin_lookup_failure m_failure;
    static_text m_reason;
};

class coin_lookup_result
{
public:
    [[nodiscard]] static coin_lookup_result found(coin value)
    {
        return coin_lookup_result{std::move(value)};
    }

    [[nodiscard]] static coin_lookup_result missing()
    {
        return coin_lookup_result{missing_tag{}};
    }

    [[nodiscard]] static coin_lookup_result spent()
    {
        return coin_lookup_result{spent_tag{}};
    }

    [[nodiscard]] static coin_lookup_result unavailable(static_text reason)
    {
        return failed(coin_lookup_failure::unavailable, reason);
    }

    [[nodiscard]] static coin_lookup_result malformed_stored_data(static_text reason)
    {
        return failed(coin_lookup_failure::malformed_stored_data, reason);
    }

    [[nodiscard]] static coin_lookup_result interrupted(static_text reason)
    {
        return failed(coin_lookup_failure::interrupted, reason);
    }

    [[nodiscard]] static coin_lookup_result io_failure(static_text reason)
    {
        return failed(coin_lookup_failure::io_failure, reason);
    }

    [[nodiscard]] coin_lookup_state state() const noexcept
    {
        if (std::holds_alternative<coin>(m_value)) return coin_lookup_state::found;
        if (std::holds_alternative<missing_tag>(m_value)) return coin_lookup_state::missing;
        if (std::holds_alternative<spent_tag>(m_value)) return coin_lookup_state::spent;
        return state_for(std::get<coin_lookup_error>(m_value).failure());
    }

    [[nodiscard]] bool is_found() const noexcept { return std::holds_alternative<coin>(m_value); }
    [[nodiscard]] bool is_missing() const noexcept { return std::holds_alternative<missing_tag>(m_value); }
    [[nodiscard]] bool is_spent() const noexcept { return std::holds_alternative<spent_tag>(m_value); }
    [[nodiscard]] bool is_failure() const noexcept { return std::holds_alternative<coin_lookup_error>(m_value); }

    [[nodiscard]] const coin& assume_value() const& { return std::get<coin>(m_value); }
    [[nodiscard]] coin&& assume_value() && { return std::get<coin>(std::move(m_value)); }
    [[nodiscard]] coin_lookup_failure assume_failure() const { return assume_error().failure(); }
    [[nodiscard]] std::string_view assume_reason() const { return assume_error().reason(); }
    [[nodiscard]] const coin_lookup_error& assume_error() const& { return std::get<coin_lookup_error>(m_value); }

    friend bool operator==(const coin_lookup_result&, const coin_lookup_result&) noexcept = default;

private:
    struct missing_tag {
        friend constexpr bool operator==(missing_tag, missing_tag) noexcept = default;
    };
    struct spent_tag {
        friend constexpr bool operator==(spent_tag, spent_tag) noexcept = default;
    };

    explicit coin_lookup_result(coin value) : m_value{std::move(value)} {}
    explicit coin_lookup_result(missing_tag tag) : m_value{tag} {}
    explicit coin_lookup_result(spent_tag tag) : m_value{tag} {}
    explicit coin_lookup_result(coin_lookup_error error) : m_value{error} {}

    [[nodiscard]] static coin_lookup_result failed(coin_lookup_failure failure, static_text reason)
    {
        return coin_lookup_result{coin_lookup_error{failure, reason}};
    }

    std::variant<coin, missing_tag, spent_tag, coin_lookup_error> m_value;
};

// A pure coin index is the std-bitcoin vocabulary abstraction: a deterministic,
// borrowed partial map from outpoint to coin. `std::nullopt` has exactly one
// meaning: no coin is present for that outpoint. Operational failures belong to
// an operation boundary outside this pure partial-map contract.
template <typename T>
concept pure_coin_index =
    requires(const T& index, const outpoint& point) {
        { index(point) } -> std::convertible_to<std::optional<coin>>;
    };

// A fallible coin source is the adapter-facing shape used when a caller must
// report unavailable, malformed, interrupted, or I/O-failed state access
// distinctly from absence. It is not a storage API and it must not make
// consensus decisions such as uniqueness, unspentness, or script validity.
template <typename T>
concept fallible_coin_source = requires(const T& index, const outpoint& point) {
    { index.lookup(point) } -> std::same_as<coin_lookup_result>;
};

// The primary protocol `coin_index` is the pure partial map only. Fallible
// adapter/kernel lookups must use a separate operation result or
// `fallible_coin_source`; unavailable data must not masquerade as absence.
template <typename T>
concept coin_index = pure_coin_index<T>;

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_COIN_INDEX_H
