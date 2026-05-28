// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit.

#ifndef BITCOIN_CONSENSUS_EXPECTED_H
#define BITCOIN_CONSENSUS_EXPECTED_H

#include <cassert>
#include <exception>
#include <utility>
#include <variant>

namespace Consensus {

template <class E>
class Unexpected
{
public:
    constexpr explicit Unexpected(E e) : m_error{std::move(e)} {}

    constexpr const E& error() const& noexcept { return m_error; }
    constexpr E& error() & noexcept { return m_error; }
    constexpr E&& error() && noexcept { return std::move(m_error); }

private:
    E m_error;
};

struct BadExpectedAccess : std::exception {
    const char* what() const noexcept override { return "Bad Consensus::Expected access"; }
};

template <class T, class E>
class Expected
{
private:
    std::variant<T, E> m_data;

public:
    constexpr Expected() : m_data{std::in_place_index<0>, T{}} {}
    constexpr Expected(T v) : m_data{std::in_place_index<0>, std::move(v)} {}
    template <class Err>
    constexpr Expected(Unexpected<Err> u) : m_data{std::in_place_index<1>, std::move(u).error()}
    {
    }

    constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr const T& value() const&
    {
        if (!has_value()) throw BadExpectedAccess{};
        return std::get<0>(m_data);
    }
    constexpr T& value() &
    {
        if (!has_value()) throw BadExpectedAccess{};
        return std::get<0>(m_data);
    }
    constexpr T&& value() && { return std::move(value()); }

    template <class U>
    T value_or(U&& default_value) const&
    {
        return has_value() ? value() : std::forward<U>(default_value);
    }
    template <class U>
    T value_or(U&& default_value) &&
    {
        return has_value() ? std::move(value()) : std::forward<U>(default_value);
    }

    constexpr const E& error() const& noexcept
    {
        const E* error{std::get_if<1>(&m_data)};
        assert(error);
        return *error;
    }
    constexpr E& error() & noexcept
    {
        E* error{std::get_if<1>(&m_data)};
        assert(error);
        return *error;
    }
    constexpr E&& error() && noexcept { return std::move(error()); }

    constexpr void swap(Expected& other) noexcept { m_data.swap(other.m_data); }

    constexpr T& operator*() & noexcept { return value(); }
    constexpr const T& operator*() const& noexcept { return value(); }
    constexpr T&& operator*() && noexcept { return std::move(value()); }

    constexpr T* operator->() noexcept { return &value(); }
    constexpr const T* operator->() const noexcept { return &value(); }
};

template <class E>
class Expected<void, E>
{
private:
    std::variant<std::monostate, E> m_data;

public:
    constexpr Expected() : m_data{std::in_place_index<0>, std::monostate{}} {}
    template <class Err>
    constexpr Expected(Unexpected<Err> u) : m_data{std::in_place_index<1>, std::move(u).error()}
    {
    }

    constexpr bool has_value() const noexcept { return m_data.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr void operator*() const noexcept { return value(); }
    constexpr void value() const
    {
        if (!has_value()) throw BadExpectedAccess{};
    }

    constexpr const E& error() const& noexcept
    {
        const E* error{std::get_if<1>(&m_data)};
        assert(error);
        return *error;
    }
    constexpr E& error() & noexcept
    {
        E* error{std::get_if<1>(&m_data)};
        assert(error);
        return *error;
    }
    constexpr E&& error() && noexcept { return std::move(error()); }
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_EXPECTED_H
