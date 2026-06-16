// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/api.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace {

static_assert(bitcoin::chain_view<std::span<const bitcoin::block_header>>);
static_assert(bitcoin::chain_view<const std::span<const bitcoin::block_header>&>);
static_assert(bitcoin::chain_view<bitcoin::any_chain_view>);
static_assert(!bitcoin::chain_view<std::span<const bitcoin::transaction>>);
static_assert(!bitcoin::chain_view<std::vector<bitcoin::block_header>>);
static_assert(std::ranges::view<bitcoin::any_chain_view>);
static_assert(std::ranges::random_access_range<const bitcoin::any_chain_view>);
static_assert(std::ranges::sized_range<const bitcoin::any_chain_view>);
static_assert(std::copy_constructible<bitcoin::any_chain_view>);

[[nodiscard]] bitcoin::block_header header(std::int32_t version, std::uint32_t nonce)
{
    return bitcoin::block_header{
        version,
        bitcoin::block_hash{},
        bitcoin::hash256{},
        bitcoin::block_time{1231006505 + nonce},
        0x1d00ffff,
        nonce};
}

class proxy_header
{
public:
    explicit proxy_header(bitcoin::block_header value) noexcept : m_value{value} {}

    [[nodiscard]] operator bitcoin::block_header() const noexcept { return m_value; }

private:
    bitcoin::block_header m_value;
};

static_assert(bitcoin::chain_view<std::span<const proxy_header>>);

class hooked_chain_view : public std::ranges::view_base
{
public:
    hooked_chain_view(
        std::span<const bitcoin::block_header> headers,
        int& starts_with_calls,
        int& mismatch_calls) noexcept :
        m_headers{headers},
        m_starts_with_calls{&starts_with_calls},
        m_mismatch_calls{&mismatch_calls}
    {
    }

    [[nodiscard]] auto begin() const noexcept { return m_headers.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_headers.end(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_headers.size(); }

    [[nodiscard]] bitcoin::block_header operator[](std::size_t index) const noexcept
    {
        return m_headers[index];
    }

    [[nodiscard]] bool starts_with(const hooked_chain_view& prefix) const
    {
        ++*m_starts_with_calls;
        if (size() < prefix.size()) {
            return false;
        }
        return std::ranges::equal(prefix.m_headers, m_headers.first(prefix.size()));
    }

    [[nodiscard]] std::ranges::mismatch_result<
        std::span<const bitcoin::block_header>::iterator,
        std::span<const bitcoin::block_header>::iterator>
    mismatch(const hooked_chain_view& other) const
    {
        ++*m_mismatch_calls;
        return std::ranges::mismatch(m_headers, other.m_headers);
    }

private:
    std::span<const bitcoin::block_header> m_headers;
    int* m_starts_with_calls;
    int* m_mismatch_calls;
};

static_assert(bitcoin::chain_view<hooked_chain_view>);

[[nodiscard]] std::uint32_t tip_nonce(bitcoin::any_chain_view chain)
{
    return chain[chain.height()].nonce();
}

} // namespace

int main()
{
    std::array headers{
        header(1, 1),
        header(2, 2),
        header(3, 3),
    };

    std::span<const bitcoin::block_header> span_chain{headers};
    if (span_chain.size() != 3) {
        return 1;
    }
    if (span_chain[0].version() != 1 || span_chain[2].nonce() != 3) {
        return 2;
    }

    auto subrange_chain{std::ranges::subrange{headers.begin(), headers.end()}};
    static_assert(bitcoin::chain_view<decltype(subrange_chain)>);
    if (std::ranges::size(subrange_chain) != span_chain.size()) {
        return 3;
    }

    bitcoin::any_chain_view empty;
    bitcoin::any_chain_view erased_span{span_chain};
    if (!empty.empty() || empty.size() != 0) {
        return 4;
    }
    if (erased_span.size() != 3 || erased_span.height() != 2) {
        return 5;
    }
    if (erased_span[1].version() != 2 || (*erased_span.begin()).nonce() != 1 || tip_nonce(erased_span) != 3) {
        return 6;
    }

    auto copied{erased_span};
    headers[1] = header(20, 20);
    if (copied[1].version() != 20 || erased_span[1].nonce() != 20) {
        return 7;
    }

    std::array prefix_headers{
        headers[0],
        headers[1],
    };
    std::array fork_headers{
        headers[0],
        headers[1],
        header(30, 30),
    };
    bitcoin::any_chain_view prefix{std::span<const bitcoin::block_header>{prefix_headers}};
    bitcoin::any_chain_view fork{std::span<const bitcoin::block_header>{fork_headers}};
    bitcoin::any_chain_view genesis{std::span<const bitcoin::block_header>{prefix_headers}.first(1)};

    if (!erased_span.starts_with(empty) || !empty.starts_with(empty)) {
        return 8;
    }
    if (empty.starts_with(genesis) || !erased_span.starts_with(prefix) || prefix.starts_with(erased_span)) {
        return 9;
    }
    if (genesis.height() != 0) {
        return 10;
    }

    const auto prefix_mismatch{erased_span.mismatch(prefix)};
    if (prefix_mismatch.in1 - erased_span.begin() != 2 || prefix_mismatch.in2 != prefix.end()) {
        return 11;
    }

    const auto fork_mismatch{erased_span.mismatch(fork)};
    if (fork_mismatch.in1 - erased_span.begin() != 2 ||
        fork_mismatch.in2 - fork.begin() != 2 ||
        (*fork_mismatch.in1).nonce() != 3 ||
        (*fork_mismatch.in2).nonce() != 30) {
        return 12;
    }

    std::array proxy_headers{
        proxy_header{header(40, 40)},
        proxy_header{header(41, 41)},
    };
    bitcoin::any_chain_view proxy_chain{std::span<const proxy_header>{proxy_headers}};
    if (proxy_chain.size() != 2 || proxy_chain[1].version() != 41) {
        return 13;
    }

    int starts_with_calls{0};
    int mismatch_calls{0};
    const hooked_chain_view hooked{span_chain, starts_with_calls, mismatch_calls};
    const hooked_chain_view hooked_prefix{std::span<const bitcoin::block_header>{prefix_headers}, starts_with_calls, mismatch_calls};
    bitcoin::any_chain_view erased_hooked{hooked};
    bitcoin::any_chain_view erased_hooked_prefix{hooked_prefix};
    if (!erased_hooked.starts_with(erased_hooked_prefix) || starts_with_calls != 1) {
        return 14;
    }
    (void)erased_hooked.mismatch(erased_hooked_prefix);
    if (mismatch_calls != 1) {
        return 15;
    }

    return 0;
}
