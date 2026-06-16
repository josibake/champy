// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_CHAIN_VIEW_H
#define BITCOIN_BITCOIN_PROTOCOL_CHAIN_VIEW_H

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <bitcoin/protocol/block_header.h>

namespace bitcoin {

class chain_work
{
public:
    constexpr chain_work() noexcept = default;
    constexpr explicit chain_work(std::array<std::byte, 32> value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr const std::array<std::byte, 32>& value() const noexcept { return m_value; }

    friend constexpr bool operator==(const chain_work&, const chain_work&) noexcept = default;

    friend constexpr auto operator<=>(const chain_work& left, const chain_work& right) noexcept
    {
        for (std::size_t index{left.m_value.size()}; index > 0; --index) {
            const auto left_byte{std::to_integer<unsigned int>(left.m_value[index - 1])};
            const auto right_byte{std::to_integer<unsigned int>(right.m_value[index - 1])};
            if (left_byte != right_byte) {
                return left_byte <=> right_byte;
            }
        }
        return std::strong_ordering::equal;
    }

private:
    std::array<std::byte, 32> m_value{};
};

class block_height
{
public:
    constexpr block_height() noexcept = default;
    constexpr explicit block_height(std::int32_t value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr std::int32_t value() const noexcept { return m_value; }

    friend constexpr bool operator==(const block_height&, const block_height&) noexcept = default;
    friend constexpr auto operator<=>(const block_height&, const block_height&) noexcept = default;

private:
    std::int32_t m_value{0};
};

class median_time_past
{
public:
    constexpr median_time_past() noexcept = default;
    constexpr explicit median_time_past(std::chrono::sys_seconds value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr std::chrono::sys_seconds value() const noexcept { return m_value; }

    friend constexpr bool operator==(const median_time_past&, const median_time_past&) noexcept = default;
    friend constexpr auto operator<=>(const median_time_past&, const median_time_past&) noexcept = default;

private:
    std::chrono::sys_seconds m_value{};
};

template <typename T>
concept chain_view =
    std::ranges::view<std::remove_cvref_t<T>> &&
    std::ranges::random_access_range<const std::remove_cvref_t<T>> &&
    std::ranges::sized_range<const std::remove_cvref_t<T>> &&
    std::convertible_to<std::ranges::range_reference_t<const std::remove_cvref_t<T>>, block_header>;

class any_chain_view;

namespace protocol_detail {

template <typename Chain, bool Self>
struct any_chain_view_source_impl : std::false_type {
};

template <typename Chain>
struct any_chain_view_source_impl<Chain, false> :
    std::bool_constant<chain_view<std::remove_cvref_t<Chain>>> {
};

template <typename Chain>
concept any_chain_view_source =
    any_chain_view_source_impl<
        Chain,
        std::same_as<std::remove_cvref_t<Chain>, any_chain_view>>::value;

} // namespace protocol_detail

class any_chain_view : public std::ranges::view_interface<any_chain_view>
{
public:
    class iterator
    {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = block_header;
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;

        constexpr iterator() noexcept = default;

        [[nodiscard]] block_header operator*() const
        {
            assert(m_view != nullptr);
            return (*m_view)[m_index];
        }

        [[nodiscard]] block_header operator[](difference_type offset) const
        {
            assert(m_view != nullptr);
            return (*m_view)[advanced_index(offset)];
        }

        iterator& operator++() noexcept
        {
            ++m_index;
            return *this;
        }

        iterator operator++(int) noexcept
        {
            auto value{*this};
            ++*this;
            return value;
        }

        iterator& operator--() noexcept
        {
            --m_index;
            return *this;
        }

        iterator operator--(int) noexcept
        {
            auto value{*this};
            --*this;
            return value;
        }

        iterator& operator+=(difference_type offset) noexcept
        {
            m_index = advanced_index(offset);
            return *this;
        }

        iterator& operator-=(difference_type offset) noexcept
        {
            return *this += -offset;
        }

        friend iterator operator+(iterator value, difference_type offset) noexcept
        {
            value += offset;
            return value;
        }

        friend iterator operator+(difference_type offset, iterator value) noexcept
        {
            value += offset;
            return value;
        }

        friend iterator operator-(iterator value, difference_type offset) noexcept
        {
            value -= offset;
            return value;
        }

        friend difference_type operator-(iterator left, iterator right) noexcept
        {
            assert(left.m_view == right.m_view);
            return static_cast<difference_type>(left.m_index) -
                   static_cast<difference_type>(right.m_index);
        }

        friend bool operator==(iterator left, iterator right) noexcept
        {
            assert(left.m_view == right.m_view);
            return left.m_index == right.m_index;
        }

        friend auto operator<=>(iterator left, iterator right) noexcept
        {
            assert(left.m_view == right.m_view);
            return left.m_index <=> right.m_index;
        }

    private:
        friend class any_chain_view;

        constexpr iterator(const any_chain_view* view, std::size_t index) noexcept :
            m_view{view}, m_index{index}
        {
        }

        [[nodiscard]] constexpr std::size_t advanced_index(difference_type offset) const noexcept
        {
            if (offset >= 0) {
                return m_index + static_cast<std::size_t>(offset);
            }
            return m_index - static_cast<std::size_t>(-offset);
        }

        const any_chain_view* m_view{nullptr};
        std::size_t m_index{0};
    };

    using const_iterator = iterator;

    any_chain_view() noexcept = default;
    any_chain_view(const any_chain_view&) noexcept = default;
    any_chain_view(any_chain_view&&) noexcept = default;
    any_chain_view& operator=(const any_chain_view&) noexcept = default;
    any_chain_view& operator=(any_chain_view&&) noexcept = default;

    template <typename Chain>
        requires protocol_detail::any_chain_view_source<Chain>
    explicit any_chain_view(Chain&& chain) :
        m_model{std::make_shared<model<std::remove_cvref_t<Chain>>>(std::forward<Chain>(chain))}
    {
    }

    [[nodiscard]] iterator begin() const noexcept { return iterator{this, 0}; }
    [[nodiscard]] iterator end() const { return iterator{this, size()}; }

    [[nodiscard]] block_header operator[](std::size_t index) const
    {
        assert(m_model != nullptr);
        return m_model->at(index);
    }

    [[nodiscard]] std::size_t size() const
    {
        return m_model == nullptr ? 0 : m_model->size();
    }

    [[nodiscard]] std::size_t height() const
    {
        assert(size() != 0);
        return size() - 1;
    }

    [[nodiscard]] std::ranges::mismatch_result<iterator, iterator> mismatch(const any_chain_view& other) const
    {
        if (m_model != nullptr && other.m_model != nullptr && m_model->same_type(*other.m_model)) {
            if (const auto index{m_model->mismatch_same_type(*other.m_model)}) {
                return {iterator{this, *index}, iterator{&other, *index}};
            }
        }
        return std::ranges::mismatch(*this, other);
    }

    [[nodiscard]] bool starts_with(const any_chain_view& prefix) const
    {
        if (prefix.size() == 0) {
            return true;
        }
        if (size() < prefix.size()) {
            return false;
        }
        if (m_model != nullptr && prefix.m_model != nullptr && m_model->same_type(*prefix.m_model)) {
            if (const auto result{m_model->starts_with_same_type(*prefix.m_model)}) {
                return *result;
            }
        }
        return mismatch(prefix).in2 == prefix.end();
    }

private:
    class model_base
    {
    public:
        virtual ~model_base() = default;

        [[nodiscard]] virtual const std::type_info& type() const noexcept = 0;
        [[nodiscard]] virtual block_header at(std::size_t index) const = 0;
        [[nodiscard]] virtual std::size_t size() const = 0;
        [[nodiscard]] virtual std::optional<bool> starts_with_same_type(const model_base&) const = 0;
        [[nodiscard]] virtual std::optional<std::size_t> mismatch_same_type(const model_base&) const = 0;

        [[nodiscard]] bool same_type(const model_base& other) const noexcept
        {
            return type() == other.type();
        }
    };

    template <chain_view Chain>
    class model final : public model_base
    {
    public:
        explicit model(Chain chain) : m_chain{std::move(chain)} {}

        [[nodiscard]] const std::type_info& type() const noexcept override { return typeid(model); }

        [[nodiscard]] block_header at(std::size_t index) const override
        {
            return std::ranges::begin(m_chain)[static_cast<std::ranges::range_difference_t<const Chain>>(index)];
        }

        [[nodiscard]] std::size_t size() const override
        {
            return std::ranges::size(m_chain);
        }

        [[nodiscard]] std::optional<bool> starts_with_same_type(const model_base& other) const override
        {
            if constexpr (requires(const Chain& chain) {
                              { chain.starts_with(chain) } -> std::same_as<bool>;
                          }) {
                return m_chain.starts_with(static_cast<const model&>(other).m_chain);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::size_t> mismatch_same_type(const model_base& other) const override
        {
            if constexpr (requires(const Chain& chain) { chain.mismatch(chain); }) {
                using actual_result = std::remove_cvref_t<decltype(std::declval<const Chain&>().mismatch(
                    std::declval<const Chain&>()))>;
                using expected_result = std::ranges::mismatch_result<
                    std::ranges::iterator_t<const Chain>,
                    std::ranges::iterator_t<const Chain>>;
                static_assert(std::same_as<actual_result, expected_result>);

                const auto& right{static_cast<const model&>(other).m_chain};
                const auto result{m_chain.mismatch(right)};
                const auto left_index{result.in1 - std::ranges::begin(m_chain)};
                const auto right_index{result.in2 - std::ranges::begin(right)};
                assert(left_index == right_index);
                assert(left_index >= decltype(left_index){0});
                return static_cast<std::size_t>(left_index);
            }
            return std::nullopt;
        }

    private:
        Chain m_chain;
    };

    std::shared_ptr<const model_base> m_model;
};

static_assert(chain_view<any_chain_view>);

// `chain_view` is a structural range concept only. It does not prove that the
// headers form a contiguous ancestor chain. Validation APIs that depend on
// ancestry require an explicit validated witness.

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_CHAIN_VIEW_H
