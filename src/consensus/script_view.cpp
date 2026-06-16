// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/script_view.h>

#include <consensus/validation.h>
#include <script/script.h>

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>

namespace Consensus {

namespace {

bool IsSmallIntegerOpcode(unsigned char opcode) noexcept
{
    return opcode == OP_0 || (opcode >= OP_1 && opcode <= OP_16);
}

int DecodeSmallIntegerOpcode(unsigned char opcode) noexcept
{
    assert(IsSmallIntegerOpcode(opcode));
    return opcode == OP_0 ? 0 : static_cast<int>(opcode - (OP_1 - 1));
}

} // namespace

ScriptView::ScriptView(std::span<const unsigned char> bytes) noexcept : m_bytes{bytes} {}

ScriptView::ScriptView(const CScript& script) noexcept : ScriptView{std::span{script}} {}

std::span<const unsigned char> AsBytes(ScriptView script) noexcept
{
    return script.m_bytes;
}

bool IsUnspendable(ScriptView script) noexcept
{
    const auto bytes{AsBytes(script)};
    return (!bytes.empty() && bytes.front() == OP_RETURN) || bytes.size() > MAX_SCRIPT_SIZE;
}

bool IsPayToScriptHash(ScriptView script) noexcept
{
    const auto bytes{AsBytes(script)};
    return bytes.size() == 23 &&
           bytes[0] == OP_HASH160 &&
           bytes[1] == 0x14 &&
           bytes[22] == OP_EQUAL;
}

std::optional<WitnessProgramView> GetWitnessProgram(ScriptView script) noexcept
{
    const auto bytes{AsBytes(script)};
    if (bytes.size() < 4 || bytes.size() > 42) return std::nullopt;
    if (!IsSmallIntegerOpcode(bytes[0])) return std::nullopt;
    if (static_cast<std::size_t>(bytes[1]) + 2 != bytes.size()) return std::nullopt;

    return WitnessProgramView{
        .version = DecodeSmallIntegerOpcode(bytes[0]),
        .program = bytes.subspan(2),
    };
}

bool HasWitnessCommitmentPrefix(ScriptView script) noexcept
{
    const auto bytes{AsBytes(script)};
    return bytes.size() >= MINIMUM_WITNESS_COMMITMENT &&
           bytes[0] == OP_RETURN &&
           bytes[1] == 0x24 &&
           bytes[2] == 0xaa &&
           bytes[3] == 0x21 &&
           bytes[4] == 0xa9 &&
           bytes[5] == 0xed;
}

std::span<const unsigned char> WitnessCommitmentHash(ScriptView script) noexcept
{
    assert(HasWitnessCommitmentPrefix(script));
    return AsBytes(script).subspan(6, 32);
}

} // namespace Consensus
