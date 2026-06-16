// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/core_adapter/coin.h>

#include <bitcoin/core_adapter/transaction.h>

#include <chain.h>
#include <coins.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bitcoin::core_adapter {
namespace {

using previous_mtp_by_height = std::unordered_map<int, median_time_past>;

[[nodiscard]] median_time_past to_median_time_past(std::int64_t value) noexcept
{
    return median_time_past{std::chrono::sys_seconds{std::chrono::seconds{value}}};
}

[[nodiscard]] previous_mtp_by_height collect_previous_median_time_past_by_height(
    const CBlockIndex& block_index)
{
    previous_mtp_by_height result;
    if (block_index.nHeight >= 0) {
        result.reserve(static_cast<std::size_t>(block_index.nHeight) + 1);
    }

    const CBlockIndex* ancestor{&block_index};
    while (ancestor) {
        result.emplace(ancestor->nHeight, to_median_time_past(ancestor->GetMedianTimePast()));
        ancestor = ancestor->pprev;
    }

    return result;
}

[[nodiscard]] std::optional<median_time_past> previous_median_time_past_for_coin_height(
    const previous_mtp_by_height& witnesses,
    int coin_height)
{
    const int ancestor_height{std::max(coin_height - 1, 0)};
    if (const auto match{witnesses.find(ancestor_height)}; match != witnesses.end()) {
        return match->second;
    }
    return std::nullopt;
}

} // namespace

bitcoin::coin to_coin(const ::Coin& value, median_time_past previous_median_time_past)
{
    return bitcoin::coin{
        to_tx_output(value.out),
        block_height{static_cast<std::int32_t>(value.nHeight)},
        value.IsCoinBase(),
        previous_median_time_past};
}

core_coin_snapshot::core_coin_snapshot(const CCoinsView& view, median_time_past previous_median_time_past) noexcept :
    m_view{&view},
    m_fixed_previous_median_time_past{previous_median_time_past}
{
}

core_coin_snapshot::core_coin_snapshot(const CCoinsView& view, const CBlockIndex& block_index) :
    m_view{&view},
    m_uses_per_height_mtp{true},
    m_previous_median_time_past_by_ancestor_height{collect_previous_median_time_past_by_height(block_index)}
{
}

std::optional<coin> core_coin_snapshot::operator()(const outpoint& point) const
{
    auto lookup{lookup_status(point)};
    switch (lookup.state()) {
    case coin_lookup_state::found:
        return std::move(lookup).assume_value();
    case coin_lookup_state::missing:
    case coin_lookup_state::spent:
        return std::nullopt;
    case coin_lookup_state::unavailable:
    case coin_lookup_state::malformed_stored_data:
    case coin_lookup_state::interrupted:
    case coin_lookup_state::io_failure:
        throw std::runtime_error{std::string{lookup.assume_reason()}};
    }
    throw std::runtime_error{"unknown core coin lookup state"};
}

coin_lookup_result core_coin_snapshot::lookup_status(const outpoint& point) const
{
    std::optional<::Coin> value;
    try {
        value = m_view->GetCoin(to_core_outpoint(point));
    } catch (...) {
        return coin_lookup_result::io_failure("core coin lookup failed");
    }

    if (!value.has_value()) {
        return coin_lookup_result::missing();
    }
    if (value->IsSpent()) {
        return coin_lookup_result::spent();
    }
    auto previous_mtp{m_fixed_previous_median_time_past};
    if (m_uses_per_height_mtp) {
        const auto per_coin_mtp{
            previous_median_time_past_for_coin_height(
                m_previous_median_time_past_by_ancestor_height,
                static_cast<int>(value->nHeight))};
        if (!per_coin_mtp) {
            return coin_lookup_result::unavailable("coin height is not in supplied block index");
        }
        previous_mtp = *per_coin_mtp;
    }
    return coin_lookup_result::found(to_coin(*value, previous_mtp));
}

core_coin_source::core_coin_source(const CCoinsView& view, median_time_past previous_median_time_past) noexcept :
    m_snapshot{view, previous_median_time_past}
{
}

core_coin_source::core_coin_source(const CCoinsView& view, const CBlockIndex& block_index) :
    m_snapshot{view, block_index}
{
}

coin_lookup_result core_coin_source::lookup(const outpoint& point) const
{
    return m_snapshot.lookup_status(point);
}

} // namespace bitcoin::core_adapter
