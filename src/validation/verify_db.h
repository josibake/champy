// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_VERIFY_DB_H
#define BITCOIN_VALIDATION_VERIFY_DB_H

#include <kernel/cs_main.h>
#include <validation/block_replay.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class BlockDataAvailability;
class BlockDataReader;
class BlockIndexLookup;
class BlockIndexValidityCommitter;
class BlockUndoReader;
class BlockUndoWriter;
class CBlockIndex;
class CChain;
class CCoinsView;
class CCoinsViewCache;
class Chainstate;
class CoreReplayRuntime;
namespace Consensus {
struct Params;
} // namespace Consensus
namespace kernel {
class Notifications;
} // namespace kernel
namespace util {
class SignalInterrupt;
} // namespace util
enum class VerifyDBResult {
    SUCCESS,
    CORRUPTED_BLOCK_DB,
    INTERRUPTED,
    SKIPPED_L3_CHECKS,
    SKIPPED_MISSING_BLOCKS,
};

class VerifyDBCoins
{
public:
    virtual ~VerifyDBCoins() = default;

    [[nodiscard]] virtual std::unique_ptr<CCoinsViewCache> MakeCache() = 0;
    [[nodiscard]] virtual size_t TipMemoryUsage() const = 0;
    [[nodiscard]] virtual size_t CacheBudgetBytes() const = 0;
};

class CoreVerifyDBCoins final : public VerifyDBCoins
{
public:
    CoreVerifyDBCoins(CCoinsView& coins_view, CCoinsViewCache& coins_tip, size_t coins_tip_cache_size_bytes);

    [[nodiscard]] std::unique_ptr<CCoinsViewCache> MakeCache() override;
    [[nodiscard]] size_t TipMemoryUsage() const override;
    [[nodiscard]] size_t CacheBudgetBytes() const override;

private:
    CCoinsView& m_coins_view;
    CCoinsViewCache& m_coins_tip;
    size_t m_coins_tip_cache_size_bytes{0};
};

struct VerifyDBBlock {
    BlockReplayBlock replay;
    uint32_t status{0};
};

class VerifyDBChain
{
public:
    virtual ~VerifyDBChain() = default;

    [[nodiscard]] virtual std::optional<VerifyDBBlock> Tip() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual int Height() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::optional<VerifyDBBlock> Previous(const VerifyDBBlock& block) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual std::optional<VerifyDBBlock> Next(const VerifyDBBlock& block) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    [[nodiscard]] virtual CBlockIndex* CoreBlockIndexForConnection(const VerifyDBBlock& block) const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
};

class CoreVerifyDBChain final : public VerifyDBChain
{
public:
    explicit CoreVerifyDBChain(const CChain& chain);

    [[nodiscard]] std::optional<VerifyDBBlock> Tip() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] int Height() const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::optional<VerifyDBBlock> Previous(const VerifyDBBlock& block) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] std::optional<VerifyDBBlock> Next(const VerifyDBBlock& block) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    [[nodiscard]] CBlockIndex* CoreBlockIndexForConnection(const VerifyDBBlock& block) const override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    const CChain& m_chain;
};

struct VerifyDBRequest {
    VerifyDBChain& chain;
    const Consensus::Params& consensus_params;
    VerifyDBCoins& coins;
    BlockDataReader& block_reader;
    BlockUndoReader& undo_reader;
    BlockUndoWriter& undo_writer;
    BlockDataAvailability& block_data_availability;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    CoreReplayRuntime& replay_runtime;
    std::optional<const char*>& last_script_check_reason_logged;
    const util::SignalInterrupt& interrupt;
};

class CVerifyDB
{
private:
    kernel::Notifications& m_notifications;

public:
    explicit CVerifyDB(kernel::Notifications& notifications);
    ~CVerifyDB();

    [[nodiscard]] VerifyDBResult VerifyDB(
        VerifyDBRequest request,
        int nCheckLevel,
        int nCheckDepth) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    [[nodiscard]] VerifyDBResult VerifyDB(
        Chainstate& chainstate,
        const Consensus::Params& consensus_params,
        CCoinsView& coinsview,
        int nCheckLevel,
        int nCheckDepth) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

#endif // BITCOIN_VALIDATION_VERIFY_DB_H
