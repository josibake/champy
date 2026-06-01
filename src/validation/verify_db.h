// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_VERIFY_DB_H
#define BITCOIN_VALIDATION_VERIFY_DB_H

#include <kernel/cs_main.h>

#include <cstddef>
#include <optional>

class BlockDataAvailability;
class BlockDataReader;
class BlockIndexLookup;
class BlockIndexValidityCommitter;
class BlockUndoReader;
class BlockUndoWriter;
class CCoinsView;
class CCoinsViewCache;
class Chainstate;
class CoreChainValidationContext;
namespace Consensus {
struct Params;
} // namespace Consensus
namespace kernel {
class Notifications;
} // namespace kernel
namespace util {
class SignalInterrupt;
} // namespace util
namespace validation {
class ActiveChainView;
class ScriptCheckScheduler;
} // namespace validation

enum class VerifyDBResult {
    SUCCESS,
    CORRUPTED_BLOCK_DB,
    INTERRUPTED,
    SKIPPED_L3_CHECKS,
    SKIPPED_MISSING_BLOCKS,
};

struct VerifyDBRequest {
    validation::ActiveChainView& active_chain;
    const Consensus::Params& consensus_params;
    CCoinsView& coins_view;
    CCoinsViewCache& coins_tip;
    size_t coins_tip_cache_size_bytes;
    BlockDataReader& block_reader;
    BlockUndoReader& undo_reader;
    BlockUndoWriter& undo_writer;
    BlockDataAvailability& block_data_availability;
    BlockIndexLookup& block_index_lookup;
    BlockIndexValidityCommitter& block_index_committer;
    CoreChainValidationContext& validation_context;
    validation::ScriptCheckScheduler& script_check_scheduler;
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
