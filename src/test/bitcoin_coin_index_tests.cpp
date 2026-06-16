// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

class optional_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        return std::nullopt;
    }
};

class optional_lookup_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> lookup(const bitcoin::outpoint&) const
    {
        return std::nullopt;
    }
};

static_assert(bitcoin::pure_coin_index<optional_coin_index>);
static_assert(!bitcoin::pure_coin_index<optional_lookup_coin_index>);
static_assert(!bitcoin::fallible_coin_source<optional_coin_index>);
static_assert(bitcoin::coin_index<optional_coin_index>);
static_assert(!bitcoin::coin_index<optional_lookup_coin_index>);
static_assert(!std::default_initializable<bitcoin::coin_lookup_result>);
static_assert(std::copy_constructible<bitcoin::coin_lookup_result>);
static_assert(std::equality_comparable<bitcoin::coin_lookup_result>);

[[nodiscard]] bitcoin::txid txid_with(std::byte byte) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(byte);
    return bitcoin::txid{bytes};
}

[[nodiscard]] bitcoin::outpoint point(std::byte tx_byte, std::uint32_t index) noexcept
{
    return bitcoin::outpoint{txid_with(tx_byte), bitcoin::tx_output_index{index}};
}

[[nodiscard]] bitcoin::script script_with(std::byte byte)
{
    const std::array bytes{byte};
    return bitcoin::script{std::span<const std::byte>{bytes}};
}

[[nodiscard]] bitcoin::coin test_coin(std::byte script_byte, std::int32_t height, bool coinbase)
{
    return bitcoin::coin{
        bitcoin::tx_output{bitcoin::amount{5000}, script_with(script_byte)},
        bitcoin::block_height{height},
        coinbase,
        bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{1231006505}}}};
}

class in_memory_coin_index
{
public:
    void set(bitcoin::outpoint point, bitcoin::coin_lookup_result result)
    {
        m_entries.push_back(entry{point, std::move(result)});
    }

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == point;
        })};
        if (match == m_entries.end()) {
            return bitcoin::coin_lookup_result::missing();
        }
        return match->result;
    }

private:
    struct entry {
        bitcoin::outpoint point;
        bitcoin::coin_lookup_result result;
    };

    std::vector<entry> m_entries;
};

static_assert(!bitcoin::coin_index<in_memory_coin_index>);
static_assert(bitcoin::fallible_coin_source<in_memory_coin_index>);
static_assert(!bitcoin::pure_coin_index<in_memory_coin_index>);

[[nodiscard]] bool has_state(const bitcoin::coin_lookup_result& result, bitcoin::coin_lookup_state state)
{
    return result.state() == state && result.is_failure() == bitcoin::is_failure(state);
}

[[nodiscard]] bool has_failure(
    const bitcoin::coin_lookup_result& result,
    bitcoin::coin_lookup_failure failure,
    std::string_view reason)
{
    return result.is_failure() &&
           result.assume_failure() == failure &&
           result.assume_reason() == reason &&
           result.state() == bitcoin::state_for(failure);
}

} // namespace

int main()
{
    const optional_coin_index pure_index;
    if (pure_index(point(std::byte{0x00}, 0)).has_value()) {
        return 1;
    }

    in_memory_coin_index index;
    index.set(point(std::byte{0x01}, 0), bitcoin::coin_lookup_result::found(test_coin(std::byte{0x51}, 100, true)));
    index.set(point(std::byte{0x02}, 0), bitcoin::coin_lookup_result::spent());
    index.set(point(std::byte{0x03}, 0), bitcoin::coin_lookup_result::unavailable("snapshot not ready"));
    index.set(point(std::byte{0x04}, 0), bitcoin::coin_lookup_result::malformed_stored_data("coin record corrupt"));
    index.set(point(std::byte{0x05}, 0), bitcoin::coin_lookup_result::interrupted("stop requested"));
    index.set(point(std::byte{0x06}, 0), bitcoin::coin_lookup_result::io_failure("read failed"));

    const auto found{index.lookup(point(std::byte{0x01}, 0))};
    if (!found.is_found() || !has_state(found, bitcoin::coin_lookup_state::found)) {
        return 2;
    }
    if (found.assume_value().height() != bitcoin::block_height{100} || !found.assume_value().coinbase()) {
        return 3;
    }

    const auto missing{index.lookup(point(std::byte{0x07}, 0))};
    if (!missing.is_missing() || !has_state(missing, bitcoin::coin_lookup_state::missing)) {
        return 4;
    }

    const auto spent{index.lookup(point(std::byte{0x02}, 0))};
    if (!spent.is_spent() || !has_state(spent, bitcoin::coin_lookup_state::spent)) {
        return 5;
    }

    if (!has_failure(index.lookup(point(std::byte{0x03}, 0)), bitcoin::coin_lookup_failure::unavailable, "snapshot not ready")) {
        return 6;
    }
    if (!has_failure(index.lookup(point(std::byte{0x04}, 0)), bitcoin::coin_lookup_failure::malformed_stored_data, "coin record corrupt")) {
        return 7;
    }
    if (!has_failure(index.lookup(point(std::byte{0x05}, 0)), bitcoin::coin_lookup_failure::interrupted, "stop requested")) {
        return 8;
    }
    if (!has_failure(index.lookup(point(std::byte{0x06}, 0)), bitcoin::coin_lookup_failure::io_failure, "read failed")) {
        return 9;
    }

    return 0;
}
