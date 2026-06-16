// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_PROTOCOL_CODEC_H
#define BITCOIN_BITCOIN_PROTOCOL_CODEC_H

#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>

#include <bitcoin/protocol/block.h>
#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/result.h>
#include <bitcoin/protocol/transaction.h>

namespace bitcoin {

template <typename Sink>
concept byte_sink = requires(Sink& sink, std::span<const std::byte> bytes) {
    sink.write(bytes);
};

template <typename Reader>
concept byte_reader = requires(Reader& reader, std::span<std::byte> out) {
    { reader.read(out) } -> std::same_as<std::size_t>;
};

class byte_sink_ref
{
public:
    template <byte_sink Sink>
        requires(!std::same_as<std::remove_cvref_t<Sink>, byte_sink_ref>)
    explicit byte_sink_ref(Sink& sink) noexcept :
        m_object{&sink},
        m_write{[](void* object, std::span<const std::byte> bytes) {
            static_cast<Sink*>(object)->write(bytes);
        }}
    {
    }

    void write(std::span<const std::byte> bytes) const { m_write(m_object, bytes); }

private:
    using write_fn = void (*)(void*, std::span<const std::byte>);

    void* m_object;
    write_fn m_write;
};

class byte_reader_ref
{
public:
    template <byte_reader Reader>
        requires(!std::same_as<std::remove_cvref_t<Reader>, byte_reader_ref>)
    explicit byte_reader_ref(Reader& reader) noexcept :
        m_object{&reader},
        m_read{[](void* object, std::span<std::byte> out) -> std::size_t {
            return static_cast<Reader*>(object)->read(out);
        }}
    {
    }

    [[nodiscard]] std::size_t read(std::span<std::byte> out) const { return m_read(m_object, out); }

private:
    using read_fn = std::size_t (*)(void*, std::span<std::byte>);

    void* m_object;
    read_fn m_read;
};

[[nodiscard]] parse_result<transaction> parse_transaction(std::span<const std::byte> raw);
[[nodiscard]] parse_result<block_header> parse_block_header(std::span<const std::byte> raw);
[[nodiscard]] parse_result<block> parse_block(std::span<const std::byte> raw);

void serialize(const transaction& value, byte_sink_ref sink);
void serialize(const block_header& value, byte_sink_ref sink);
void serialize(const block& value, byte_sink_ref sink);

[[nodiscard]] bool has_witness(const transaction& value) noexcept;
[[nodiscard]] std::size_t serialized_size(const transaction& value);
[[nodiscard]] std::size_t stripped_serialized_size(const transaction& value);
[[nodiscard]] std::size_t serialized_size(const block_header& value);
[[nodiscard]] std::size_t serialized_size(const block& value);
[[nodiscard]] std::size_t stripped_serialized_size(const block& value);
[[nodiscard]] std::size_t weight(const block& value);

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_PROTOCOL_CODEC_H
