// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SCRIPT_VIEW_H
#define BITCOIN_CONSENSUS_SCRIPT_VIEW_H

#include <cstddef>
#include <optional>
#include <span>

class CScript;

namespace Consensus {

class ScriptView {
public:
    ScriptView() noexcept = default;
    explicit ScriptView(std::span<const unsigned char> bytes) noexcept;
    explicit ScriptView(const CScript& script) noexcept;

    [[nodiscard]] bool Empty() const noexcept { return m_bytes.empty(); }

private:
    std::span<const unsigned char> m_bytes;

    friend std::span<const unsigned char> AsBytes(ScriptView script) noexcept;
};

struct WitnessProgramView {
    int version{0};
    std::span<const unsigned char> program;
};

[[nodiscard]] std::span<const unsigned char> AsBytes(ScriptView script) noexcept;
[[nodiscard]] bool IsUnspendable(ScriptView script) noexcept;
[[nodiscard]] bool IsPayToScriptHash(ScriptView script) noexcept;
[[nodiscard]] std::optional<WitnessProgramView> GetWitnessProgram(ScriptView script) noexcept;
[[nodiscard]] bool HasWitnessCommitmentPrefix(ScriptView script) noexcept;
[[nodiscard]] std::span<const unsigned char> WitnessCommitmentHash(ScriptView script) noexcept;

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SCRIPT_VIEW_H
