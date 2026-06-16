// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace libbitcoin_shape {

struct hash_digest {
    std::array<std::byte, 32> bytes{};
};

struct header {
    std::int32_t version{0};
    hash_digest previous_block;
    hash_digest merkle_root;
    std::int64_t timestamp{0};
    std::uint32_t bits{0};
    std::uint32_t nonce{0};
};

struct output {
    std::int64_t satoshis{0};
    std::vector<std::byte> locking_script;
};

enum class spend_state {
    confirmed,
    absent,
    spent,
    database_busy,
    corrupt_record,
    stopped,
    disk_error,
};

struct spend_record {
    spend_state state{spend_state::absent};
    output prevout;
    std::int32_t height{0};
    bool coinbase{false};
    std::int64_t previous_mtp{0};
    std::string_view reason;
};

class history_database
{
public:
    [[nodiscard]] spend_record lookup(std::uint32_t index) const
    {
        switch (index) {
        case 0:
            return spend_record{
                spend_state::confirmed,
                output{2500, {std::byte{0x51}}},
                10,
                false,
                1231006505,
                {}};
        case 1:
            return spend_record{spend_state::absent};
        case 2:
            return spend_record{spend_state::spent};
        case 3:
            return spend_record{spend_state::database_busy, {}, 0, false, 0, "compaction in progress"};
        case 4:
            return spend_record{spend_state::corrupt_record, {}, 0, false, 0, "bad coin encoding"};
        case 5:
            return spend_record{spend_state::stopped, {}, 0, false, 0, "stop requested"};
        default:
            return spend_record{spend_state::disk_error, {}, 0, false, 0, "read failed"};
        }
    }
};

} // namespace libbitcoin_shape

template <typename Hash>
[[nodiscard]] Hash hash_from(const libbitcoin_shape::hash_digest& value) noexcept
{
    return Hash{std::span<const std::byte, 32>{value.bytes}};
}

[[nodiscard]] bitcoin::block_header header_from(const libbitcoin_shape::header& value)
{
    return bitcoin::block_header{
        value.version,
        hash_from<bitcoin::block_hash>(value.previous_block),
        hash_from<bitcoin::hash256>(value.merkle_root),
        bitcoin::block_time{static_cast<std::uint32_t>(value.timestamp)},
        value.bits,
        value.nonce};
}

[[nodiscard]] bitcoin::coin coin_from(const libbitcoin_shape::spend_record& value)
{
    return bitcoin::coin{
        bitcoin::tx_output{
            bitcoin::amount{value.prevout.satoshis},
            bitcoin::script{std::span<const std::byte>{value.prevout.locking_script.data(), value.prevout.locking_script.size()}}},
        bitcoin::block_height{value.height},
        value.coinbase,
        bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{value.previous_mtp}}}};
}

class libbitcoin_chain_view : public std::ranges::view_base
{
public:
    explicit libbitcoin_chain_view(std::span<const libbitcoin_shape::header> headers)
    {
        m_headers.reserve(headers.size());
        for (const auto& header : headers) {
            m_headers.push_back(header_from(header));
        }
    }

    [[nodiscard]] auto begin() const noexcept { return m_headers.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_headers.end(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_headers.size(); }

private:
    std::vector<bitcoin::block_header> m_headers;
};

class libbitcoin_coin_index
{
public:
    explicit libbitcoin_coin_index(const libbitcoin_shape::history_database& database) noexcept :
        m_database{database}
    {
    }

    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint& point) const
    {
        const auto record{m_database.lookup(point.index().value())};
        switch (record.state) {
        case libbitcoin_shape::spend_state::confirmed:
            return coin_from(record);
        case libbitcoin_shape::spend_state::absent:
        case libbitcoin_shape::spend_state::spent:
            return std::nullopt;
        case libbitcoin_shape::spend_state::database_busy:
        case libbitcoin_shape::spend_state::corrupt_record:
        case libbitcoin_shape::spend_state::stopped:
        case libbitcoin_shape::spend_state::disk_error:
            throw std::runtime_error{"libbitcoin history lookup failed"};
        }
        throw std::runtime_error{"unknown libbitcoin spend state"};
    }

private:
    const libbitcoin_shape::history_database& m_database;
};

class libbitcoin_coin_source
{
public:
    explicit libbitcoin_coin_source(const libbitcoin_shape::history_database& database) noexcept :
        m_database{database}
    {
    }

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        const auto record{m_database.lookup(point.index().value())};
        switch (record.state) {
        case libbitcoin_shape::spend_state::confirmed:
            return bitcoin::coin_lookup_result::found(coin_from(record));
        case libbitcoin_shape::spend_state::absent:
            return bitcoin::coin_lookup_result::missing();
        case libbitcoin_shape::spend_state::spent:
            return bitcoin::coin_lookup_result::spent();
        case libbitcoin_shape::spend_state::database_busy:
            return bitcoin::coin_lookup_result::unavailable("compaction in progress");
        case libbitcoin_shape::spend_state::corrupt_record:
            return bitcoin::coin_lookup_result::malformed_stored_data("bad coin encoding");
        case libbitcoin_shape::spend_state::stopped:
            return bitcoin::coin_lookup_result::interrupted("stop requested");
        case libbitcoin_shape::spend_state::disk_error:
            return bitcoin::coin_lookup_result::io_failure("read failed");
        }
        return bitcoin::coin_lookup_result::io_failure("unknown libbitcoin spend state");
    }

private:
    const libbitcoin_shape::history_database& m_database;
};

static_assert(bitcoin::chain_view<libbitcoin_chain_view>);
static_assert(bitcoin::coin_index<libbitcoin_coin_index>);
static_assert(!bitcoin::coin_index<libbitcoin_coin_source>);
static_assert(bitcoin::fallible_coin_source<libbitcoin_coin_source>);

using header_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<const libbitcoin_chain_view&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_verify_result, bitcoin::evidence_verify_result<bitcoin::header_facts>>);

using transaction_spend_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::spend_context&>(),
    std::declval<const libbitcoin_coin_index&>()));
static_assert(std::same_as<transaction_spend_result, bitcoin::verify_result<bitcoin::spend_facts>>);

using block_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block&>()));
static_assert(std::same_as<block_verify_result, bitcoin::verify_result<bitcoin::block_facts>>);

[[nodiscard]] bitcoin::outpoint point(std::uint32_t index) noexcept
{
    return bitcoin::outpoint{bitcoin::txid{}, bitcoin::tx_output_index{index}};
}

} // namespace

int main()
{
    const std::array headers{
        libbitcoin_shape::header{1, {}, {}, 1231006505, 0x1d00ffff, 1},
        libbitcoin_shape::header{2, {}, {}, 1231006605, 0x1d00ffff, 2},
    };
    const libbitcoin_chain_view chain{std::span<const libbitcoin_shape::header>{headers}};

    const libbitcoin_shape::history_database database;
    const libbitcoin_coin_index coins{database};
    const libbitcoin_coin_source source{database};

    if (std::ranges::size(chain) != 2) {
        return 1;
    }
    if (!coins(point(0)).has_value()) {
        return 2;
    }
    if (coins(point(1)).has_value()) {
        return 3;
    }
    if (coins(point(2)).has_value()) {
        return 4;
    }
    if (source.lookup(point(3)).assume_failure() != bitcoin::coin_lookup_failure::unavailable) {
        return 5;
    }
    if (source.lookup(point(4)).assume_failure() != bitcoin::coin_lookup_failure::malformed_stored_data) {
        return 6;
    }
    if (source.lookup(point(5)).assume_failure() != bitcoin::coin_lookup_failure::interrupted) {
        return 7;
    }
    if (source.lookup(point(6)).assume_failure() != bitcoin::coin_lookup_failure::io_failure) {
        return 8;
    }

    return 0;
}
