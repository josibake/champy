// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/codec.h>

#include <bitcoin/protocol/detail/byte_reader.h>
#include <bitcoin/protocol/detail/compact_size.h>
#include <bitcoin/protocol/detail/endian.h>
#include <bitcoin/protocol/detail/hash_writer.h>

#include <crypto/sha256.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace bitcoin {
namespace {

[[nodiscard]] std::array<std::byte, 32> sha256_output_bytes(std::span<const unsigned char, CSHA256::OUTPUT_SIZE> source) noexcept
{
    std::array<std::byte, 32> result{};
    std::ranges::transform(source, result.begin(), [](unsigned char value) {
        return static_cast<std::byte>(value);
    });
    return result;
}

class counting_sink
{
public:
    void write(std::span<const std::byte> bytes) noexcept { m_size += bytes.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

private:
    std::size_t m_size{0};
};

class double_sha256_sink
{
public:
    void write(std::span<const std::byte> bytes)
    {
        m_writer.Write(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    }

    [[nodiscard]] hash256 finish()
    {
        std::array<unsigned char, CSHA256::OUTPUT_SIZE> first{};
        std::array<unsigned char, CSHA256::OUTPUT_SIZE> second{};
        m_writer.Finalize(first.data());
        CSHA256().Write(first.data(), first.size()).Finalize(second.data());
        return hash256{sha256_output_bytes(second)};
    }

private:
    CSHA256 m_writer;
};

void write_bytes(byte_sink_ref sink, std::span<const std::byte> bytes)
{
    sink.write(bytes);
}

void write_byte(byte_sink_ref sink, std::byte value)
{
    sink.write(std::span<const std::byte>{&value, 1});
}

template <typename UInt>
void write_little(byte_sink_ref sink, UInt value)
{
    std::array<std::byte, sizeof(UInt)> bytes{};
    protocol_detail::write_little_endian(value, std::span<std::byte>{bytes});
    write_bytes(sink, bytes);
}

void write_compact_size(byte_sink_ref sink, std::uint64_t value)
{
    if (value < 253U) {
        write_byte(sink, static_cast<std::byte>(value));
        return;
    }
    if (value <= 0xffffU) {
        write_byte(sink, std::byte{253});
        write_little<std::uint16_t>(sink, static_cast<std::uint16_t>(value));
        return;
    }
    if (value <= 0xffffffffULL) {
        write_byte(sink, std::byte{254});
        write_little<std::uint32_t>(sink, static_cast<std::uint32_t>(value));
        return;
    }
    write_byte(sink, std::byte{255});
    write_little<std::uint64_t>(sink, value);
}

template <typename Int>
[[nodiscard]] Int read_little(protocol_detail::byte_reader& reader)
{
    using UInt = std::make_unsigned_t<Int>;
    const auto bytes{reader.read(sizeof(Int))};
    if (!reader.ok()) {
        return {};
    }
    return static_cast<Int>(protocol_detail::read_little_endian<UInt>(bytes));
}

[[nodiscard]] bool fits_size(std::uint64_t value) noexcept
{
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

[[nodiscard]] bool require_count_fits_remaining(
    protocol_detail::byte_reader& reader,
    std::uint64_t count,
    std::size_t minimum_item_bytes)
{
    if (!fits_size(count)) {
        reader.fail(parse_failure_code::compact_size_overflow);
        return false;
    }
    if (minimum_item_bytes > 0 && count > reader.remaining() / minimum_item_bytes) {
        reader.fail(parse_failure_code::truncated);
        return false;
    }
    return true;
}

[[nodiscard]] std::array<std::byte, 32> read_hash_bytes(protocol_detail::byte_reader& reader)
{
    std::array<std::byte, 32> bytes{};
    const auto source{reader.read(bytes.size())};
    if (reader.ok()) {
        std::ranges::copy(source, bytes.begin());
    }
    return bytes;
}

[[nodiscard]] script read_script(protocol_detail::byte_reader& reader)
{
    const auto size{protocol_detail::read_compact_size(reader)};
    if (!reader.ok()) {
        return {};
    }
    if (!fits_size(size)) {
        reader.fail(parse_failure_code::compact_size_overflow);
        return {};
    }
    return script{reader.read(static_cast<std::size_t>(size))};
}

struct input_parts {
    outpoint previous_output;
    script script_sig;
    std::uint32_t sequence{0};
    std::vector<witness_item> witness;
};

[[nodiscard]] input_parts read_input_parts(protocol_detail::byte_reader& reader)
{
    input_parts input;
    input.previous_output = outpoint{
        txid{read_hash_bytes(reader)},
        tx_output_index{read_little<std::uint32_t>(reader)}};
    input.script_sig = read_script(reader);
    input.sequence = read_little<std::uint32_t>(reader);
    return input;
}

[[nodiscard]] tx_output read_output(protocol_detail::byte_reader& reader)
{
    const auto value{read_little<std::int64_t>(reader)};
    auto locking_script{read_script(reader)};
    return tx_output{amount{value}, std::move(locking_script)};
}

[[nodiscard]] std::vector<witness_item> read_witness(protocol_detail::byte_reader& reader)
{
    std::vector<witness_item> witness;
    const auto count{protocol_detail::read_compact_size(reader)};
    if (!reader.ok()) {
        return witness;
    }
    if (!require_count_fits_remaining(reader, count, /*minimum_item_bytes=*/1)) {
        return witness;
    }
    witness.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i{0}; i < count && reader.ok(); ++i) {
        const auto size{protocol_detail::read_compact_size(reader)};
        if (!reader.ok()) {
            break;
        }
        if (!fits_size(size)) {
            reader.fail(parse_failure_code::compact_size_overflow);
            break;
        }
        witness.emplace_back(reader.read(static_cast<std::size_t>(size)));
    }
    return witness;
}

[[nodiscard]] transaction read_transaction(protocol_detail::byte_reader& reader)
{
    const auto version{read_little<std::int32_t>(reader)};
    const auto first_input_count{protocol_detail::read_compact_size(reader)};
    if (!reader.ok()) {
        return {};
    }

    auto input_count{first_input_count};
    bool with_witness{false};
    if (input_count == 0 && !reader.empty() && reader.peek() != std::byte{0}) {
        const auto flags{std::to_integer<unsigned char>(reader.read(1)[0])};
        if ((flags & 1U) == 0 || (flags & ~1U) != 0) {
            reader.fail(parse_failure_code::invalid_witness_marker);
            return {};
        }
        with_witness = true;
        input_count = protocol_detail::read_compact_size(reader);
        if (input_count == 0 && reader.ok()) {
            reader.fail(parse_failure_code::invalid_witness_marker);
        }
    }
    if (!reader.ok()) {
        return {};
    }
    if (!require_count_fits_remaining(reader, input_count, /*minimum_item_bytes=*/41)) {
        return {};
    }

    std::vector<input_parts> parts;
    parts.reserve(static_cast<std::size_t>(input_count));
    for (std::uint64_t i{0}; i < input_count && reader.ok(); ++i) {
        parts.push_back(read_input_parts(reader));
    }

    const auto output_count{protocol_detail::read_compact_size(reader)};
    if (!reader.ok()) {
        return {};
    }
    if (!require_count_fits_remaining(reader, output_count, /*minimum_item_bytes=*/9)) {
        return {};
    }
    std::vector<tx_output> outputs;
    outputs.reserve(static_cast<std::size_t>(output_count));
    for (std::uint64_t i{0}; i < output_count && reader.ok(); ++i) {
        outputs.push_back(read_output(reader));
    }

    if (with_witness) {
        bool has_witness_data{false};
        for (auto& input : parts) {
            input.witness = read_witness(reader);
            has_witness_data = has_witness_data || !input.witness.empty();
            if (!reader.ok()) {
                break;
            }
        }
        if (!has_witness_data && reader.ok()) {
            reader.fail(parse_failure_code::invalid_witness_marker);
        }
    }

    const auto locktime{read_little<std::uint32_t>(reader)};
    if (!reader.ok()) {
        return {};
    }

    std::vector<tx_input> inputs;
    inputs.reserve(parts.size());
    for (auto& input : parts) {
        inputs.emplace_back(
            input.previous_output,
            std::move(input.script_sig),
            input.sequence,
            std::move(input.witness));
    }
    return transaction{version, std::move(inputs), std::move(outputs), locktime};
}

[[nodiscard]] block_header read_block_header(protocol_detail::byte_reader& reader)
{
    const auto version{read_little<std::int32_t>(reader)};
    auto previous{block_hash{read_hash_bytes(reader)}};
    auto merkle{hash256{read_hash_bytes(reader)}};
    const auto timestamp{read_little<std::uint32_t>(reader)};
    const auto bits{read_little<std::uint32_t>(reader)};
    const auto nonce{read_little<std::uint32_t>(reader)};
    return block_header{
        version,
        previous,
        merkle,
        block_time{timestamp},
        bits,
        nonce};
}

[[nodiscard]] block read_block(protocol_detail::byte_reader& reader)
{
    auto header{read_block_header(reader)};
    const auto transaction_count{protocol_detail::read_compact_size(reader)};
    if (!reader.ok()) {
        return {};
    }
    if (!require_count_fits_remaining(reader, transaction_count, /*minimum_item_bytes=*/10)) {
        return {};
    }
    std::vector<transaction> transactions;
    transactions.reserve(static_cast<std::size_t>(transaction_count));
    for (std::uint64_t i{0}; i < transaction_count && reader.ok(); ++i) {
        transactions.push_back(read_transaction(reader));
    }
    return block{header, std::move(transactions)};
}

template <typename T, typename Read>
[[nodiscard]] parse_result<T> parse_exact(std::span<const std::byte> raw, Read read)
{
    protocol_detail::byte_reader reader{raw};
    auto value{read(reader)};
    if (!reader.ok()) {
        return parse_result<T>::malformed(reader.failure());
    }
    if (!reader.empty()) {
        return parse_result<T>::malformed(
            malformed_parse{parse_failure_code::trailing_data, reader.offset()});
    }
    return parse_result<T>::parsed(std::move(value));
}

void serialize_transaction(const transaction& value, byte_sink_ref sink, bool include_witness)
{
    write_little<std::int32_t>(sink, value.version());
    const bool write_witness{include_witness && has_witness(value) && !value.inputs().empty()};
    if (write_witness) {
        write_byte(sink, std::byte{0});
        write_byte(sink, std::byte{1});
    }

    write_compact_size(sink, value.inputs().size());
    for (const auto& input : value.inputs()) {
        write_bytes(sink, as_bytes(input.previous_output().txid()));
        write_little<std::uint32_t>(sink, input.previous_output().index().value());
        write_compact_size(sink, as_bytes(input.script()).size());
        write_bytes(sink, as_bytes(input.script()));
        write_little<std::uint32_t>(sink, input.sequence());
    }

    write_compact_size(sink, value.outputs().size());
    for (const auto& output : value.outputs()) {
        write_little<std::int64_t>(sink, output.value().satoshis());
        write_compact_size(sink, as_bytes(output.script()).size());
        write_bytes(sink, as_bytes(output.script()));
    }

    if (write_witness) {
        for (const auto& input : value.inputs()) {
            write_compact_size(sink, input.witness().size());
            for (const auto& item : input.witness()) {
                write_compact_size(sink, as_bytes(item).size());
                write_bytes(sink, as_bytes(item));
            }
        }
    }

    write_little<std::uint32_t>(sink, value.locktime());
}

} // namespace

namespace protocol_detail {

hash256 double_sha256(std::span<const std::byte> bytes)
{
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> first{};
    std::array<unsigned char, CSHA256::OUTPUT_SIZE> second{};
    CSHA256()
        .Write(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size())
        .Finalize(first.data());
    CSHA256().Write(first.data(), first.size()).Finalize(second.data());

    return hash256{sha256_output_bytes(second)};
}

} // namespace protocol_detail

parse_result<transaction> parse_transaction(std::span<const std::byte> raw)
{
    return parse_exact<transaction>(raw, read_transaction);
}

parse_result<block_header> parse_block_header(std::span<const std::byte> raw)
{
    return parse_exact<block_header>(raw, read_block_header);
}

parse_result<block> parse_block(std::span<const std::byte> raw)
{
    return parse_exact<block>(raw, read_block);
}

void serialize(const transaction& value, byte_sink_ref sink)
{
    serialize_transaction(value, sink, true);
}

void serialize(const block_header& value, byte_sink_ref sink)
{
    write_little<std::int32_t>(sink, value.version());
    write_bytes(sink, as_bytes(value.previous_block_hash()));
    write_bytes(sink, as_bytes(value.merkle_root()));
    write_little<std::uint32_t>(sink, value.time().seconds_since_epoch());
    write_little<std::uint32_t>(sink, value.bits());
    write_little<std::uint32_t>(sink, value.nonce());
}

void serialize(const block& value, byte_sink_ref sink)
{
    serialize(value.header(), sink);
    write_compact_size(sink, value.transactions().size());
    for (const auto& tx : value.transactions()) {
        serialize(tx, sink);
    }
}

void serialize_stripped(const block& value, byte_sink_ref sink)
{
    serialize(value.header(), sink);
    write_compact_size(sink, value.transactions().size());
    for (const auto& tx : value.transactions()) {
        serialize_transaction(tx, sink, false);
    }
}

bool has_witness(const transaction& value) noexcept
{
    return std::ranges::any_of(value.inputs(), [](const tx_input& input) {
        return !input.witness().empty();
    });
}

std::size_t serialized_size(const transaction& value)
{
    counting_sink sink;
    serialize(value, byte_sink_ref{sink});
    return sink.size();
}

std::size_t stripped_serialized_size(const transaction& value)
{
    counting_sink sink;
    serialize_transaction(value, byte_sink_ref{sink}, false);
    return sink.size();
}

std::size_t serialized_size(const block_header&)
{
    return 80;
}

std::size_t serialized_size(const block& value)
{
    counting_sink sink;
    serialize(value, byte_sink_ref{sink});
    return sink.size();
}

std::size_t stripped_serialized_size(const block& value)
{
    counting_sink sink;
    serialize_stripped(value, byte_sink_ref{sink});
    return sink.size();
}

std::size_t weight(const block& value)
{
    return stripped_serialized_size(value) * 3 + serialized_size(value);
}

txid transaction::id() const
{
    double_sha256_sink sink;
    serialize_transaction(*this, byte_sink_ref{sink}, false);
    return txid{as_bytes(sink.finish())};
}

wtxid transaction::witness_id() const
{
    double_sha256_sink sink;
    serialize_transaction(*this, byte_sink_ref{sink}, true);
    return wtxid{as_bytes(sink.finish())};
}

block_hash block_header::hash() const
{
    double_sha256_sink sink;
    serialize(*this, byte_sink_ref{sink});
    return block_hash{as_bytes(sink.finish())};
}

block_hash block::hash() const
{
    return header().hash();
}

} // namespace bitcoin
