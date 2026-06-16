// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/serialization.h>

#include <serialize.h>

#include <cstring>
#include <ios>
#include <utility>

namespace Consensus {
namespace {

class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) noexcept : m_bytes{bytes} {}

    [[nodiscard]] bool Empty() const noexcept { return m_bytes.empty(); }

    void read(std::span<std::byte> dst)
    {
        if (dst.size() > m_bytes.size()) {
            throw std::ios_base::failure{"ByteReader::read(): end of data"};
        }
        if (!dst.empty()) {
            std::memcpy(dst.data(), m_bytes.data(), dst.size());
            m_bytes = m_bytes.subspan(dst.size());
        }
    }

    template <typename T>
    ByteReader& operator>>(T&& value)
    {
        ::Unserialize(*this, value);
        return *this;
    }

private:
    std::span<const std::byte> m_bytes;
};

class ByteSinkStream
{
public:
    explicit ByteSinkStream(ByteSinkRef out) noexcept : m_out{out} {}

    void write(std::span<const std::byte> bytes) { m_out.Write(bytes); }

    template <typename T>
    ByteSinkStream& operator<<(const T& value)
    {
        ::Serialize(*this, value);
        return *this;
    }

private:
    ByteSinkRef m_out;
};

template <typename T, typename ReadFn>
std::optional<T> ParseExact(std::span<const std::byte> bytes, ReadFn read)
{
    ByteReader reader{bytes};
    T value;
    try {
        read(reader, value);
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
    if (!reader.Empty()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<CBlock> ParseBlock(std::span<const std::byte> bytes)
{
    return ParseExact<CBlock>(bytes, [](ByteReader& reader, CBlock& block) {
        reader >> TX_WITH_WITNESS(block);
    });
}

std::optional<CBlockHeader> ParseBlockHeader(std::span<const std::byte> bytes)
{
    return ParseExact<CBlockHeader>(bytes, [](ByteReader& reader, CBlockHeader& header) {
        reader >> header;
    });
}

std::optional<CTransaction> ParseTransaction(std::span<const std::byte> bytes)
{
    ByteReader reader{bytes};
    CMutableTransaction tx;
    try {
        reader >> TX_WITH_WITNESS(tx);
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
    if (!reader.Empty()) {
        return std::nullopt;
    }
    return CTransaction{std::move(tx)};
}

std::optional<CTxOut> ParseTxOut(std::span<const std::byte> bytes)
{
    return ParseExact<CTxOut>(bytes, [](ByteReader& reader, CTxOut& txout) {
        reader >> txout;
    });
}

void SerializeBlock(const CBlock& block, ByteSinkRef out)
{
    ByteSinkStream writer{out};
    writer << TX_WITH_WITNESS(block);
}

void SerializeBlockHeader(const CBlockHeader& header, ByteSinkRef out)
{
    ByteSinkStream writer{out};
    writer << header;
}

void SerializeTransaction(const CTransaction& tx, ByteSinkRef out)
{
    ByteSinkStream writer{out};
    writer << TX_WITH_WITNESS(tx);
}

void SerializeTxOut(const CTxOut& txout, ByteSinkRef out)
{
    ByteSinkStream writer{out};
    writer << txout;
}

std::size_t SerializedSize(const CBlock& block)
{
    return ::GetSerializeSize(TX_WITH_WITNESS(block));
}

std::size_t SerializedSize(const CBlockHeader& header)
{
    return ::GetSerializeSize(header);
}

std::size_t SerializedSize(const CTransaction& tx)
{
    return ::GetSerializeSize(TX_WITH_WITNESS(tx));
}

std::size_t SerializedSize(const CTxOut& txout)
{
    return ::GetSerializeSize(txout);
}

std::size_t StrippedSerializedSize(const CBlock& block)
{
    return ::GetSerializeSize(TX_NO_WITNESS(block));
}

std::size_t StrippedSerializedSize(const CTransaction& tx)
{
    return ::GetSerializeSize(TX_NO_WITNESS(tx));
}

std::size_t SerializedInputBaseSize(const CTxIn& txin)
{
    return ::GetSerializeSize(TX_NO_WITNESS(txin));
}

std::size_t SerializedInputWitnessStackSize(const CTxIn& txin)
{
    return ::GetSerializeSize(txin.scriptWitness.stack);
}

} // namespace Consensus
