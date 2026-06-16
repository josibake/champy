// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/api.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

std::byte byte(unsigned char value) noexcept
{
    return static_cast<std::byte>(value);
}

std::vector<std::byte> bytes(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (auto value : values) {
        result.push_back(byte(value));
    }
    return result;
}

void append(std::vector<std::byte>& target, std::span<const std::byte> source)
{
    target.insert(target.end(), source.begin(), source.end());
}

void append_large_compact_size(std::vector<std::byte>& target)
{
    append(target, bytes({
        0xff,
        0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff,
    }));
}

bool equal_bytes(std::span<const std::byte> left, std::span<const std::byte> right)
{
    return std::ranges::equal(left, right);
}

template <typename T>
std::vector<std::byte> serialized(const T& value)
{
    class sink
    {
    public:
        void write(std::span<const std::byte> bytes) { append(m_bytes, bytes); }
        std::vector<std::byte> m_bytes;
    };

    sink out;
    bitcoin::serialize(value, bitcoin::byte_sink_ref{out});
    return out.m_bytes;
}

class fixed_buffer_sink
{
public:
    explicit fixed_buffer_sink(std::span<std::byte> buffer) noexcept : m_buffer{buffer} {}

    void write(std::span<const std::byte> source)
    {
        if (source.size() > m_buffer.size() - m_size) {
            m_overflow = true;
            return;
        }
        std::ranges::copy(source, m_buffer.subspan(m_size, source.size()).begin());
        m_size += source.size();
    }

    [[nodiscard]] bool overflow() const noexcept { return m_overflow; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_buffer.first(m_size); }

private:
    std::span<std::byte> m_buffer;
    std::size_t m_size{0};
    bool m_overflow{false};
};

class counting_sink
{
public:
    void write(std::span<const std::byte> source) noexcept { m_size += source.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

private:
    std::size_t m_size{0};
};

std::uint32_t fingerprint(std::span<const std::byte> source) noexcept
{
    std::uint32_t value{0x811c9dc5U};
    for (auto byte : source) {
        value = ((value << 5U) | (value >> 27U)) ^ std::to_integer<unsigned char>(byte);
    }
    return value;
}

class hashing_sink_stub
{
public:
    void write(std::span<const std::byte> source) noexcept
    {
        m_size += source.size();
        for (auto byte : source) {
            m_fingerprint = ((m_fingerprint << 5U) | (m_fingerprint >> 27U)) ^ std::to_integer<unsigned char>(byte);
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::uint32_t value() const noexcept { return m_fingerprint; }

private:
    std::size_t m_size{0};
    std::uint32_t m_fingerprint{0x811c9dc5U};
};

class span_reader
{
public:
    explicit span_reader(std::span<const std::byte> source) noexcept : m_source{source} {}

    [[nodiscard]] std::size_t read(std::span<std::byte> out) noexcept
    {
        const auto count{std::min(out.size(), m_source.size() - m_offset)};
        std::ranges::copy(m_source.subspan(m_offset, count), out.begin());
        m_offset += count;
        return count;
    }

private:
    std::span<const std::byte> m_source;
    std::size_t m_offset{0};
};

static_assert(bitcoin::byte_sink<fixed_buffer_sink>);
static_assert(bitcoin::byte_sink<counting_sink>);
static_assert(bitcoin::byte_sink<hashing_sink_stub>);
static_assert(bitcoin::byte_reader<span_reader>);

std::vector<std::byte> genesis_header_bytes()
{
    return bytes({
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
        0x29, 0xab, 0x5f, 0x49,
        0xff, 0xff, 0x00, 0x1d,
        0x1d, 0xac, 0x2b, 0x7c,
    });
}

std::array<std::byte, 32> genesis_hash_wire_order()
{
    return {
        byte(0x6f), byte(0xe2), byte(0x8c), byte(0x0a),
        byte(0xb6), byte(0xf1), byte(0xb3), byte(0x72),
        byte(0xc1), byte(0xa6), byte(0xa2), byte(0x46),
        byte(0xae), byte(0x63), byte(0xf7), byte(0x4f),
        byte(0x93), byte(0x1e), byte(0x83), byte(0x65),
        byte(0xe1), byte(0x5a), byte(0x08), byte(0x9c),
        byte(0x68), byte(0xd6), byte(0x19), byte(0x00),
        byte(0x00), byte(0x00), byte(0x00), byte(0x00),
    };
}

std::vector<std::byte> legacy_transaction_bytes()
{
    auto tx{bytes({
        0x01, 0x00, 0x00, 0x00,
        0x01,
    })};
    for (unsigned char i{1}; i <= 32; ++i) {
        tx.push_back(byte(i));
    }
    append(tx, bytes({
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x51,
        0xff, 0xff, 0xff, 0xff,
        0x01,
        0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x51,
        0x00, 0x00, 0x00, 0x00,
    }));
    return tx;
}

std::vector<std::byte> witness_transaction_bytes()
{
    auto tx{bytes({
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x01,
        0x01,
    })};
    for (unsigned char i{1}; i <= 32; ++i) {
        tx.push_back(byte(i));
    }
    append(tx, bytes({
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x51,
        0xff, 0xff, 0xff, 0xff,
        0x01,
        0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x51,
        0x01, 0x02, 0xaa, 0xbb,
        0x00, 0x00, 0x00, 0x00,
    }));
    return tx;
}

std::vector<std::byte> transaction_with_large_input_count()
{
    auto tx{bytes({
        0x01, 0x00, 0x00, 0x00,
    })};
    append_large_compact_size(tx);
    return tx;
}

std::vector<std::byte> transaction_with_large_output_count()
{
    auto tx{bytes({
        0x01, 0x00, 0x00, 0x00,
        0x01,
    })};
    for (unsigned char i{1}; i <= 32; ++i) {
        tx.push_back(byte(i));
    }
    append(tx, bytes({
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0xff, 0xff, 0xff, 0xff,
    }));
    append_large_compact_size(tx);
    return tx;
}

std::vector<std::byte> witness_transaction_with_large_witness_count()
{
    auto tx{bytes({
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x01,
        0x01,
    })};
    for (unsigned char i{1}; i <= 32; ++i) {
        tx.push_back(byte(i));
    }
    append(tx, bytes({
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0xff, 0xff, 0xff, 0xff,
        0x00,
    }));
    append_large_compact_size(tx);
    append(tx, bytes({
        0x00, 0x00, 0x00, 0x00,
    }));
    return tx;
}

std::vector<std::byte> block_with_large_transaction_count()
{
    auto block{genesis_header_bytes()};
    append_large_compact_size(block);
    return block;
}

} // namespace

int main()
{
    const auto header_bytes{genesis_header_bytes()};
    span_reader reader_source{header_bytes};
    bitcoin::byte_reader_ref reader{reader_source};
    std::array<std::byte, 4> version_bytes{};
    if (auto failure{check(reader.read(version_bytes) == version_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(equal_bytes(version_bytes, std::span<const std::byte>{header_bytes}.first(4)), __LINE__)}) return failure;

    auto header_result{bitcoin::parse_block_header(header_bytes)};
    if (auto failure{check(header_result.has_value(), __LINE__)}) return failure;
    const auto& header{header_result.assume_value()};
    if (auto failure{check(header.version() == 1, __LINE__)}) return failure;
    if (auto failure{check(serialized(header) == header_bytes, __LINE__)}) return failure;
    if (auto failure{check(bitcoin::serialized_size(header) == header_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(equal_bytes(as_bytes(header.hash()), std::span<const std::byte, 32>{genesis_hash_wire_order()}), __LINE__)}) return failure;

    auto truncated_header{header_bytes};
    truncated_header.pop_back();
    auto truncated{bitcoin::parse_block_header(truncated_header)};
    if (auto failure{check(!truncated.has_value(), __LINE__)}) return failure;
    if (auto failure{check(truncated.assume_failure().code() == bitcoin::parse_failure_code::truncated, __LINE__)}) return failure;

    auto trailing_header{header_bytes};
    trailing_header.push_back(byte(0x00));
    auto trailing{bitcoin::parse_block_header(trailing_header)};
    if (auto failure{check(!trailing.has_value(), __LINE__)}) return failure;
    if (auto failure{check(trailing.assume_failure().code() == bitcoin::parse_failure_code::trailing_data, __LINE__)}) return failure;

    const auto legacy_bytes{legacy_transaction_bytes()};
    auto legacy_result{bitcoin::parse_transaction(legacy_bytes)};
    if (auto failure{check(legacy_result.has_value(), __LINE__)}) return failure;
    const auto& legacy_tx{legacy_result.assume_value()};
    if (auto failure{check(!bitcoin::has_witness(legacy_tx), __LINE__)}) return failure;
    if (auto failure{check(serialized(legacy_tx) == legacy_bytes, __LINE__)}) return failure;
    if (auto failure{check(bitcoin::serialized_size(legacy_tx) == legacy_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(bitcoin::stripped_serialized_size(legacy_tx) == legacy_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(equal_bytes(as_bytes(legacy_tx.id()), as_bytes(legacy_tx.witness_id())), __LINE__)}) return failure;

    auto truncated_tx{legacy_bytes};
    truncated_tx.pop_back();
    auto truncated_tx_result{bitcoin::parse_transaction(truncated_tx)};
    if (auto failure{check(!truncated_tx_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(truncated_tx_result.assume_failure().code() == bitcoin::parse_failure_code::truncated, __LINE__)}) return failure;

    auto trailing_tx{legacy_bytes};
    trailing_tx.push_back(byte(0x00));
    auto trailing_tx_result{bitcoin::parse_transaction(trailing_tx)};
    if (auto failure{check(!trailing_tx_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(trailing_tx_result.assume_failure().code() == bitcoin::parse_failure_code::trailing_data, __LINE__)}) return failure;

    const auto witness_bytes{witness_transaction_bytes()};
    auto witness_result{bitcoin::parse_transaction(witness_bytes)};
    if (auto failure{check(witness_result.has_value(), __LINE__)}) return failure;
    const auto& witness_tx{witness_result.assume_value()};
    if (auto failure{check(bitcoin::has_witness(witness_tx), __LINE__)}) return failure;
    if (auto failure{check(serialized(witness_tx) == witness_bytes, __LINE__)}) return failure;
    if (auto failure{check(bitcoin::serialized_size(witness_tx) == witness_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(bitcoin::stripped_serialized_size(witness_tx) == legacy_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(!equal_bytes(as_bytes(witness_tx.id()), as_bytes(witness_tx.witness_id())), __LINE__)}) return failure;

    counting_sink counted;
    bitcoin::serialize(witness_tx, bitcoin::byte_sink_ref{counted});
    if (auto failure{check(counted.size() == bitcoin::serialized_size(witness_tx), __LINE__)}) return failure;

    hashing_sink_stub hashed;
    bitcoin::serialize(witness_tx, bitcoin::byte_sink_ref{hashed});
    if (auto failure{check(hashed.size() == witness_bytes.size(), __LINE__)}) return failure;
    if (auto failure{check(hashed.value() == fingerprint(witness_bytes), __LINE__)}) return failure;

    std::array<std::byte, 128> fixed_buffer{};
    fixed_buffer_sink fixed_out{fixed_buffer};
    bitcoin::serialize(witness_tx, bitcoin::byte_sink_ref{fixed_out});
    if (auto failure{check(!fixed_out.overflow(), __LINE__)}) return failure;
    if (auto failure{check(equal_bytes(fixed_out.bytes(), witness_bytes), __LINE__)}) return failure;

    auto non_canonical{bytes({0x01, 0x00, 0x00, 0x00, 0xfd, 0xfc, 0x00})};
    auto compact_result{bitcoin::parse_transaction(non_canonical)};
    if (auto failure{check(!compact_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(compact_result.assume_failure().code() == bitcoin::parse_failure_code::non_canonical_compact_size, __LINE__)}) return failure;

    auto truncated_compact{bytes({0x01, 0x00, 0x00, 0x00, 0xfd, 0xfc})};
    auto truncated_compact_result{bitcoin::parse_transaction(truncated_compact)};
    if (auto failure{check(!truncated_compact_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(truncated_compact_result.assume_failure().code() == bitcoin::parse_failure_code::truncated, __LINE__)}) return failure;

    auto bad_marker{bytes({0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00})};
    auto marker_result{bitcoin::parse_transaction(bad_marker)};
    if (auto failure{check(!marker_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(marker_result.assume_failure().code() == bitcoin::parse_failure_code::invalid_witness_marker, __LINE__)}) return failure;

    auto bad_witness_flag{witness_bytes};
    bad_witness_flag[5] = byte(0x02);
    auto flag_result{bitcoin::parse_transaction(bad_witness_flag)};
    if (auto failure{check(!flag_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(flag_result.assume_failure().code() == bitcoin::parse_failure_code::invalid_witness_marker, __LINE__)}) return failure;

    auto superfluous_witness{witness_bytes};
    superfluous_witness[60] = byte(0x00);
    superfluous_witness.erase(superfluous_witness.begin() + 61, superfluous_witness.begin() + 64);
    auto superfluous_result{bitcoin::parse_transaction(superfluous_witness)};
    if (auto failure{check(!superfluous_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(superfluous_result.assume_failure().code() == bitcoin::parse_failure_code::invalid_witness_marker, __LINE__)}) return failure;

    if (auto failure{check(!bitcoin::parse_transaction(transaction_with_large_input_count()).has_value(), __LINE__)}) return failure;
    if (auto failure{check(!bitcoin::parse_transaction(transaction_with_large_output_count()).has_value(), __LINE__)}) return failure;
    if (auto failure{check(!bitcoin::parse_transaction(witness_transaction_with_large_witness_count()).has_value(), __LINE__)}) return failure;
    if (auto failure{check(!bitcoin::parse_block(block_with_large_transaction_count()).has_value(), __LINE__)}) return failure;

    auto block_bytes{header_bytes};
    block_bytes.push_back(byte(0x01));
    append(block_bytes, witness_bytes);
    auto block_result{bitcoin::parse_block(block_bytes)};
    if (auto failure{check(block_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(serialized(block_result.assume_value()) == block_bytes, __LINE__)}) return failure;
    if (auto failure{check(bitcoin::serialized_size(block_result.assume_value()) == block_bytes.size(), __LINE__)}) return failure;

    auto truncated_block{block_bytes};
    truncated_block.pop_back();
    auto truncated_block_result{bitcoin::parse_block(truncated_block)};
    if (auto failure{check(!truncated_block_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(truncated_block_result.assume_failure().code() == bitcoin::parse_failure_code::truncated, __LINE__)}) return failure;

    auto trailing_block{block_bytes};
    trailing_block.push_back(byte(0x00));
    auto trailing_block_result{bitcoin::parse_block(trailing_block)};
    if (auto failure{check(!trailing_block_result.has_value(), __LINE__)}) return failure;
    if (auto failure{check(trailing_block_result.assume_failure().code() == bitcoin::parse_failure_code::trailing_data, __LINE__)}) return failure;

    return 0;
}
