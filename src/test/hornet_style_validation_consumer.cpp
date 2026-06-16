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
namespace hornet_shape {

struct header_record {
    std::int32_t version{0};
    std::array<std::byte, 32> parent{};
    std::array<std::byte, 32> merkle{};
    std::int64_t timestamp{0};
    std::uint32_t work_bits{0};
    std::uint32_t nonce{0};
};

class ancestry_snapshot
{
public:
    explicit ancestry_snapshot(std::vector<header_record> path) : m_path{std::move(path)} {}

    [[nodiscard]] std::span<const header_record> path() const noexcept { return m_path; }

private:
    std::vector<header_record> m_path;
};

enum class utxo_status {
    hit,
    missing,
    spent,
    waiting_for_parent,
    corrupt_table_entry,
    stopped,
    read_error,
};

struct output_detail {
    std::int64_t satoshis{0};
    std::vector<std::byte> script;
    std::int32_t height{0};
    bool coinbase{false};
    std::int64_t previous_mtp{0};
};

struct query_result {
    utxo_status status{utxo_status::missing};
    output_detail output;
    std::string_view reason;
};

class unspent_outputs_view
{
public:
    [[nodiscard]] query_result query(const bitcoin::outpoint& point) const
    {
        switch (point.index().value()) {
        case 0:
            return query_result{
                utxo_status::hit,
                output_detail{1000, {std::byte{0x51}}, 20, true, 1231006505},
                {}};
        case 1:
            return query_result{utxo_status::missing};
        case 2:
            return query_result{utxo_status::spent};
        case 3:
            return query_result{utxo_status::waiting_for_parent, {}, "parent output not visible"};
        case 4:
            return query_result{utxo_status::corrupt_table_entry, {}, "output payload corrupt"};
        case 5:
            return query_result{utxo_status::stopped, {}, "pipeline interrupted"};
        default:
            return query_result{utxo_status::read_error, {}, "segment read failed"};
        }
    }
};

} // namespace hornet_shape

[[nodiscard]] bitcoin::block_header header_from(const hornet_shape::header_record& value)
{
    return bitcoin::block_header{
        value.version,
        bitcoin::block_hash{std::span<const std::byte, 32>{value.parent}},
        bitcoin::hash256{std::span<const std::byte, 32>{value.merkle}},
        bitcoin::block_time{static_cast<std::uint32_t>(value.timestamp)},
        value.work_bits,
        value.nonce};
}

[[nodiscard]] bitcoin::coin coin_from(const hornet_shape::output_detail& value)
{
    return bitcoin::coin{
        bitcoin::tx_output{
            bitcoin::amount{value.satoshis},
            bitcoin::script{std::span<const std::byte>{value.script.data(), value.script.size()}}},
        bitcoin::block_height{value.height},
        value.coinbase,
        bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{value.previous_mtp}}}};
}

class hornet_chain_view : public std::ranges::view_base
{
public:
    explicit hornet_chain_view(const hornet_shape::ancestry_snapshot& snapshot)
    {
        const auto path{snapshot.path()};
        m_headers.reserve(path.size());
        for (const auto& record : path) {
            m_headers.push_back(header_from(record));
        }
    }

    [[nodiscard]] auto begin() const noexcept { return m_headers.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_headers.end(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_headers.size(); }

private:
    std::vector<bitcoin::block_header> m_headers;
};

class hornet_coin_index
{
public:
    explicit hornet_coin_index(const hornet_shape::unspent_outputs_view& view) noexcept : m_view{view} {}

    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint& point) const
    {
        const auto result{m_view.query(point)};
        switch (result.status) {
        case hornet_shape::utxo_status::hit:
            return coin_from(result.output);
        case hornet_shape::utxo_status::missing:
        case hornet_shape::utxo_status::spent:
            return std::nullopt;
        case hornet_shape::utxo_status::waiting_for_parent:
        case hornet_shape::utxo_status::corrupt_table_entry:
        case hornet_shape::utxo_status::stopped:
        case hornet_shape::utxo_status::read_error:
            throw std::runtime_error{"Hornet UTXO query failed"};
        }
        throw std::runtime_error{"unknown Hornet UTXO status"};
    }

private:
    const hornet_shape::unspent_outputs_view& m_view;
};

class hornet_coin_source
{
public:
    explicit hornet_coin_source(const hornet_shape::unspent_outputs_view& view) noexcept : m_view{view} {}

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        const auto result{m_view.query(point)};
        switch (result.status) {
        case hornet_shape::utxo_status::hit:
            return bitcoin::coin_lookup_result::found(coin_from(result.output));
        case hornet_shape::utxo_status::missing:
            return bitcoin::coin_lookup_result::missing();
        case hornet_shape::utxo_status::spent:
            return bitcoin::coin_lookup_result::spent();
        case hornet_shape::utxo_status::waiting_for_parent:
            return bitcoin::coin_lookup_result::unavailable("parent output not visible");
        case hornet_shape::utxo_status::corrupt_table_entry:
            return bitcoin::coin_lookup_result::malformed_stored_data("output payload corrupt");
        case hornet_shape::utxo_status::stopped:
            return bitcoin::coin_lookup_result::interrupted("pipeline interrupted");
        case hornet_shape::utxo_status::read_error:
            return bitcoin::coin_lookup_result::io_failure("segment read failed");
        }
        return bitcoin::coin_lookup_result::io_failure("unknown Hornet UTXO status");
    }

private:
    const hornet_shape::unspent_outputs_view& m_view;
};

static_assert(bitcoin::chain_view<hornet_chain_view>);
static_assert(bitcoin::coin_index<hornet_coin_index>);
static_assert(!bitcoin::coin_index<hornet_coin_source>);
static_assert(bitcoin::fallible_coin_source<hornet_coin_source>);

using header_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::block_header&>(),
    std::declval<const hornet_chain_view&>(),
    std::declval<bitcoin::validation_time>(),
    std::declval<const bitcoin::consensus_params&>()));
static_assert(std::same_as<header_verify_result, bitcoin::evidence_verify_result<bitcoin::header_facts>>);

using transaction_verify_result = decltype(bitcoin::verify(
    std::declval<const bitcoin::transaction&>(),
    std::declval<const bitcoin::spend_context&>(),
    std::declval<const hornet_coin_index&>()));
static_assert(std::same_as<transaction_verify_result, bitcoin::verify_result<bitcoin::spend_facts>>);

[[nodiscard]] bitcoin::outpoint point(std::uint32_t index) noexcept
{
    return bitcoin::outpoint{bitcoin::txid{}, bitcoin::tx_output_index{index}};
}

} // namespace

int main()
{
    hornet_shape::ancestry_snapshot snapshot{std::vector<hornet_shape::header_record>{
        hornet_shape::header_record{1, {}, {}, 1231006505, 0x1d00ffff, 1},
        hornet_shape::header_record{2, {}, {}, 1231006605, 0x1d00ffff, 2},
        hornet_shape::header_record{3, {}, {}, 1231006705, 0x1d00ffff, 3},
    }};
    const hornet_chain_view chain{snapshot};

    const hornet_shape::unspent_outputs_view utxos;
    const hornet_coin_index coins{utxos};
    const hornet_coin_source source{utxos};

    if (std::ranges::size(chain) != 3) {
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
