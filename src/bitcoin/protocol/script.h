// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_SCRIPT_H
#define BITCOIN_BITCOIN_PROTOCOL_SCRIPT_H

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace bitcoin {

class script;

class script_ref
{
public:
    constexpr script_ref() noexcept = default;
    constexpr explicit script_ref(std::span<const std::byte> bytes) noexcept : m_bytes{bytes} {}
    script_ref(const script& value) noexcept;
    script_ref(script&&) = delete;
    script_ref(const script&&) = delete;

    [[nodiscard]] constexpr bool empty() const noexcept { return m_bytes.empty(); }

    [[nodiscard]] friend constexpr std::span<const std::byte> as_bytes(script_ref value) noexcept
    {
        return value.m_bytes;
    }

    friend bool operator==(script_ref left, script_ref right) noexcept
    {
        return std::ranges::equal(as_bytes(left), as_bytes(right));
    }

private:
    std::span<const std::byte> m_bytes{};
};

class script
{
public:
    script() noexcept = default;
    explicit script(std::span<const std::byte> bytes) : m_bytes{bytes.begin(), bytes.end()} {}
    explicit script(script_ref value) : script{as_bytes(value)} {}

    [[nodiscard]] bool empty() const noexcept { return m_bytes.empty(); }

    [[nodiscard]] friend std::span<const std::byte> as_bytes(const script& value) noexcept
    {
        return value.m_bytes;
    }

    friend bool operator==(const script&, const script&) noexcept = default;

private:
    std::vector<std::byte> m_bytes;
};

inline script_ref::script_ref(const script& value) noexcept : m_bytes{as_bytes(value)} {}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_SCRIPT_H
