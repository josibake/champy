// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chainstate.h>

#include <arith_uint256.h>
#include <validation/core_coins_block_connection_state.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_replay.h>
#include <validation/block_validation.h>
#include <chain.h>
#include <validation/chain_validation.h>
#include <validation/core_chain_activation.h>
#include <validation/core_chain_lock.h>
#include <validation/core_chain_validation_context.h>
#include <validation/validation_commit_executor.h>
#include <validation/validation_event_queue.h>
#include <chainstate_event_sink.h>
#include <clientversion.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <validation_state.h>
#include <flatfile.h>
#include <kernel/chainparams.h>
#include <kernel/coinstats.h>
#include <kernel/messagestartchars.h>
#include <kernel/notifications_interface.h>
#include <kernel/warning.h>
#include <logging/timer.h>
#include <kernel/blockstorage.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <random.h>
#include <tinyformat.h>
#include <txdb.h>
#include <uint256.h>
#include <util/byte_units.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/hasher.h>
#include <util/log.h>
#include <util/moneystr.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>
#include <util/trace.h>
#include <util/translation.h>
#include <validationinterface.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using kernel::Notifications;

using fsbridge::FopenFn;
using kernel::BlockManager;
using kernel::BlockMap;
using kernel::CBlockIndexHeightOnlyComparator;
using kernel::CBlockIndexWorkComparator;

/** Time window to wait between writing blocks/block index and chainstate to disk.
 *  Randomize writing time inside the window to prevent a situation where the
 *  network over time settles into a few cohorts of synchronized writers.
*/
static constexpr auto DATABASE_WRITE_INTERVAL_MIN{50min};
static constexpr auto DATABASE_WRITE_INTERVAL_MAX{70min};
const std::vector<std::string> CHECKLEVEL_DOC {
    "level 0 reads the blocks from disk",
    "level 1 verifies block validity",
    "level 2 verifies undo data",
    "level 3 checks disconnection of tip blocks",
    "level 4 tries to reconnect the blocks",
    "each level includes the checks of the previous levels",
};
/** The number of blocks to keep below the deepest prune lock.
 *  There is nothing special about this number. It is higher than what we
 *  expect to see in regular mainnet reorgs, but not so high that it would
 *  noticeably interfere with the pruning mechanism.
 * */
static constexpr int PRUNE_LOCK_BUFFER{10};

TRACEPOINT_SEMAPHORE(utxocache, flush);

static ExternalCacheUsage ExternalCacheUsageForEvents(const ChainstateEventSink* chain_events)
{
    return chain_events ? chain_events->CacheUsage() : ExternalCacheUsage{};
}

static std::optional<validation::ChainBlockSnapshot> MakeChainBlockSnapshot(const CBlockIndex* block_index)
{
    if (!block_index) return std::nullopt;
    return validation::ChainBlockSnapshot{
        .hash = block_index->GetBlockHash(),
        .height = block_index->nHeight,
        .time = block_index->GetBlockTime(),
    };
}

static std::optional<validation::ActiveChainTipSnapshot> MakeActiveChainTipSnapshot(const CBlockIndex* block_index)
{
    if (!block_index) return std::nullopt;
    return validation::ActiveChainTipSnapshot{
        .hash = block_index->GetBlockHash(),
        .height = block_index->nHeight,
        .time = block_index->GetBlockTime(),
        .chain_work = block_index->nChainWork,
        .block_proof = GetBlockProof(*block_index),
    };
}

static std::optional<ChainWorkBlockSnapshot> MakeChainWorkBlockSnapshot(const CBlockIndex* block_index)
{
    if (!block_index) return std::nullopt;
    return ChainWorkBlockSnapshot{
        .hash = block_index->GetBlockHash(),
        .height = block_index->nHeight,
        .chain_work = block_index->nChainWork,
    };
}

static bool ContainsHash(std::span<const uint256> hashes, const uint256& hash)
{
    return std::find(hashes.begin(), hashes.end(), hash) != hashes.end();
}

static bool HeaderIsKnownToPeer(const CBlockIndex& header, const CBlockIndex* best_known_block, const CBlockIndex* best_header_sent)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    if (best_known_block && &header == best_known_block->GetAncestor(header.nHeight)) return true;
    if (best_header_sent && &header == best_header_sent->GetAncestor(header.nHeight)) return true;
    return false;
}

// NOLINTNEXTLINE(misc-no-recursion)
static ChainBlockQueryResult QueryChainBlock(
    const CBlockIndex* index,
    const ChainBlockQuery& query,
    UniqueLock<RecursiveMutex>& chain_lock,
    const CChain& active_chain,
    const BlockManager& blockman) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    ChainBlockQueryResult result;
    if (!index) return result;

    result.found = true;
    if (query.hash) result.hash = index->GetBlockHash();
    if (query.height) result.height = index->nHeight;
    if (query.time) result.time = index->GetBlockTime();
    if (query.max_time) result.max_time = index->GetBlockTimeMax();
    if (query.median_time_past) result.median_time_past = index->GetMedianTimePast();

    const bool in_active_chain{active_chain[index->nHeight] == index};
    if (query.in_active_chain) result.in_active_chain = in_active_chain;
    if (query.locator) result.locator = GetLocator(index);
    if (query.next_block) {
        result.next_block = std::make_unique<ChainBlockQueryResult>(QueryChainBlock(
            in_active_chain ? active_chain[index->nHeight + 1] : nullptr,
            *query.next_block,
            chain_lock,
            active_chain,
            blockman));
    }
    if (query.data) {
        CBlock data;
        {
            REVERSE_LOCK(chain_lock, ::cs_main);
            if (!blockman.ReadBlock(data, *index)) data.SetNull();
        }
        result.data = std::move(data);
    }
    return result;
}

static bool CanBeBlockIndexCandidate(const CBlockIndex& block_index)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    return block_index.IsValid(BLOCK_VALID_TRANSACTIONS) && block_index.HaveNumChainTxs();
}

static std::multimap<const arith_uint256, CBlockIndex*> CollectHighWorkOutOfChainHeaders(
    const std::vector<CBlockIndex*>& block_indices,
    const CChain& active_chain,
    const CBlockIndex& minimum_work_block)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    std::multimap<const arith_uint256, CBlockIndex*> candidates;
    const CBlockIndexWorkComparator work_comparator;
    for (CBlockIndex* candidate : block_indices) {
        if (!active_chain.Contains(*candidate) &&
            !work_comparator(candidate, &minimum_work_block) &&
            !(candidate->nStatus & BLOCK_FAILED_VALID)) {
            candidates.insert({candidate->nChainWork, candidate});
        }
    }
    return candidates;
}

static void AddMissingBlockIndexCandidates(
    const std::vector<CBlockIndex*>& block_indices,
    const CChain& active_chain,
    std::set<CBlockIndex*, CBlockIndexWorkComparator>& candidates)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    for (CBlockIndex* block_index : block_indices) {
        if (CanBeBlockIndexCandidate(*block_index) && !candidates.value_comp()(block_index, active_chain.Tip())) {
            candidates.insert(block_index);
        }
    }
}

const CBlockIndex* Chainstate::FindForkInGlobalIndex(const CBlockLocator& locator) const
{
    AssertLockHeld(cs_main);

    // Find the latest block common to locator and chain - we expect that
    // locator.vHave is sorted descending by height.
    CoreBlockIndexView block_index{m_chainman};
    for (const uint256& hash : locator.vHave) {
        const CBlockIndex* pindex{block_index.LookupBlockIndex(hash)};
        if (pindex) {
            if (m_chain.Contains(*pindex)) {
                return pindex;
            }
            if (pindex->GetAncestor(m_chain.Height()) == m_chain.Tip()) {
                return m_chain.Tip();
            }
        }
    }
    return m_chain.Genesis();
}


CoinsViews::CoinsViews(DBParams db_params, CoinsViewOptions options)
    : m_dbview{std::move(db_params), std::move(options)},
      m_catcherview(&m_dbview) {}

void CoinsViews::InitCache()
{
    AssertLockHeld(::cs_main);
    m_cacheview = std::make_unique<CCoinsViewCache>(&m_catcherview);
    m_block_connection_view = std::make_unique<CoinsViewOverlay>(&*m_cacheview);
}

Chainstate::Chainstate(
    BlockManager& blockman,
    ChainstateManager& chainman)
    : m_blockman(blockman),
      m_chainman(chainman) {}

fs::path Chainstate::StoragePath() const
{
    return m_chainman.m_options.datadir / "chainstate";
}

void Chainstate::InitCoinsDB(
    size_t cache_size_bytes,
    bool in_memory,
    bool should_wipe)
{
    m_coins_views = std::make_unique<CoinsViews>(
        DBParams{
            .path = StoragePath(),
            .cache_bytes = cache_size_bytes,
            .memory_only = in_memory,
            .wipe_data = should_wipe,
            .obfuscate = true,
            .options = m_chainman.m_options.coins_db},
        m_chainman.m_options.coins_view);

    m_coinsdb_cache_size_bytes = cache_size_bytes;
}

void Chainstate::InitCoinsCache(size_t cache_size_bytes)
{
    AssertLockHeld(::cs_main);
    assert(m_coins_views != nullptr);
    m_coinstip_cache_size_bytes = cache_size_bytes;
    m_coins_views->InitCache();
}

// Lock-free: depends on `m_cached_is_ibd`, which is latched by `UpdateIBDStatus()`.
bool ChainstateManager::IsInitialBlockDownload() const noexcept
{
    return m_cached_is_ibd.load(std::memory_order_relaxed);
}

void Chainstate::CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);

    if (m_chainman.m_best_invalid && m_chainman.m_best_invalid->nChainWork > m_chain.Tip()->nChainWork + (GetBlockProof(*m_chain.Tip()) * 6)) {
        LogWarning("Found invalid chain more than 6 blocks longer than our best chain. This could be due to database corruption or consensus incompatibility with peers.");
        m_chainman.GetNotifications().warningSet(
            kernel::Warning::LARGE_WORK_INVALID_CHAIN,
            _("Warning: Found invalid chain more than 6 blocks longer than our best chain. This could be due to database corruption or consensus incompatibility with peers."));
    } else {
        m_chainman.GetNotifications().warningUnset(kernel::Warning::LARGE_WORK_INVALID_CHAIN);
    }
}

// Called both upon regular invalid block discovery *and* InvalidateBlock
void Chainstate::InvalidChainFound(CBlockIndex* pindexNew)
{
    AssertLockHeld(cs_main);
    if (!m_chainman.m_best_invalid || pindexNew->nChainWork > m_chainman.m_best_invalid->nChainWork) {
        m_chainman.m_best_invalid = pindexNew;
    }
    SetBlockFailureFlags(pindexNew);
    if (m_chainman.m_best_header != nullptr && m_chainman.m_best_header->GetAncestor(pindexNew->nHeight) == pindexNew) {
        m_chainman.RecalculateBestHeader();
    }

    LogInfo("%s: invalid block=%s height=%d log2_work=%f date=%s", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), FormatISO8601DateTime(pindexNew->GetBlockTime()));
    CBlockIndex *tip = m_chain.Tip();
    assert (tip);
    LogInfo("%s: current best=%s height=%d log2_work=%f date=%s", __func__,
      tip->GetBlockHash().ToString(), m_chain.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      FormatISO8601DateTime(tip->GetBlockTime()));
    CheckForkWarningConditions();
}

// Same as InvalidChainFound, above, except not called directly from InvalidateBlock,
// which does its own setBlockIndexCandidates management.
void Chainstate::InvalidBlockFound(CBlockIndex* pindex, const BlockValidationState& state)
{
    AssertLockHeld(cs_main);
    if (state.GetResult() != BlockValidationResult::BLOCK_MUTATED) {
        CoreBlockIndexStore block_index_store{m_chainman};
        pindex->nStatus |= BLOCK_FAILED_VALID;
        block_index_store.MarkBlockIndexDirty(*pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}


ValidationCache::ValidationCache(const size_t script_execution_cache_bytes, const size_t signature_cache_bytes)
    : m_signature_cache{signature_cache_bytes}
{
    // Setup the salted hasher
    uint256 nonce = GetRandHash();
    // We want the nonce to be 64 bytes long to force the hasher to process
    // this chunk, which makes later hash computations more efficient. We
    // just write our 32-byte entropy twice to fill the 64 bytes.
    m_script_execution_cache_hasher.Write(nonce.begin(), 32);
    m_script_execution_cache_hasher.Write(nonce.begin(), 32);

    const auto [num_elems, approx_size_bytes] = m_script_execution_cache.setup_bytes(script_execution_cache_bytes);
    LogInfo("Using %zu MiB out of %zu MiB requested for script execution cache, able to store %zu elements",
              approx_size_bytes >> 20, script_execution_cache_bytes >> 20, num_elems);
}

bool ValidationCache::ContainsScriptExecution(const uint256& entry, bool erase) EXCLUSIVE_LOCKS_REQUIRED(!m_script_execution_cache_mutex)
{
    LOCK(m_script_execution_cache_mutex);
    return m_script_execution_cache.contains(entry, erase);
}

void ValidationCache::StoreScriptExecution(const uint256& entry) EXCLUSIVE_LOCKS_REQUIRED(!m_script_execution_cache_mutex)
{
    LOCK(m_script_execution_cache_mutex);
    m_script_execution_cache.insert(entry);
}

bool FatalError(Notifications& notifications, BlockValidationState& state, const bilingual_str& message)
{
    notifications.fatalError(message);
    return state.Error(message.original);
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When FAILED is returned, view is left in an indeterminate state. */



CoinsCacheSizeState Chainstate::GetCoinsCacheSizeState()
{
    AssertLockHeld(::cs_main);
    return this->GetCoinsCacheSizeState(
        m_coinstip_cache_size_bytes);
}

CoinsCacheSizeState Chainstate::GetCoinsCacheSizeState(
    size_t max_coins_cache_size_bytes,
    ExternalCacheUsage external_cache_usage)
{
    AssertLockHeld(::cs_main);
    int64_t cacheSize = CoinsTip().DynamicMemoryUsage();
    int64_t nTotalSpace =
        int64_t(max_coins_cache_size_bytes) +
        std::max<int64_t>(external_cache_usage.max_size_bytes - int64_t(external_cache_usage.usage_bytes), 0);

    if (cacheSize > nTotalSpace) {
        LogInfo("Cache size (%s) exceeds total space (%s)\n", cacheSize, nTotalSpace);
        return CoinsCacheSizeState::CRITICAL;
    } else if (cacheSize > LargeCoinsCacheThreshold(nTotalSpace)) {
        return CoinsCacheSizeState::LARGE;
    }
    return CoinsCacheSizeState::OK;
}

bool Chainstate::FlushStateToDisk(
    BlockValidationState &state,
    FlushStateMode mode,
    int nManualPruneHeight,
    ExternalCacheUsage external_cache_usage)
{
    LOCK(cs_main);
    assert(this->CanFlushToDisk());
    std::set<int> setFilesToPrune;
    bool full_flush_completed = false;

    [[maybe_unused]] const size_t coins_count{CoinsTip().GetCacheSize()};
    [[maybe_unused]] const size_t coins_mem_usage{CoinsTip().DynamicMemoryUsage()};

    try {
    {
        bool fFlushForPrune = false;

        CoinsCacheSizeState cache_state = GetCoinsCacheSizeState(m_coinstip_cache_size_bytes, external_cache_usage);
        LOCK(m_blockman.cs_LastBlockFile);
        if (m_blockman.IsPruneMode() && (m_blockman.m_check_for_pruning || nManualPruneHeight > 0) && m_chainman.m_blockman.m_blockfiles_indexed) {
            // make sure we don't prune above any of the prune locks bestblocks
            // pruning is height-based
            int last_prune{m_chain.Height()}; // last height we can prune
            std::optional<std::string> limiting_lock; // prune lock that actually was the limiting factor, only used for logging

            for (const auto& prune_lock : m_blockman.m_prune_locks) {
                if (prune_lock.second.height_first == std::numeric_limits<int>::max()) continue;
                // Remove the buffer and one additional block here to get actual height that is outside of the buffer
                const int lock_height{prune_lock.second.height_first - PRUNE_LOCK_BUFFER - 1};
                last_prune = std::max(1, std::min(last_prune, lock_height));
                if (last_prune == lock_height) {
                    limiting_lock = prune_lock.first;
                }
            }

            if (limiting_lock) {
                LogDebug(BCLog::PRUNE, "%s limited pruning to height %d\n", limiting_lock.value(), last_prune);
            }

            if (nManualPruneHeight > 0) {
                LOG_TIME_MILLIS_WITH_CATEGORY("find files to prune (manual)", BCLog::BENCH);

                m_blockman.FindFilesToPruneManual(
                    setFilesToPrune,
                    std::min(last_prune, nManualPruneHeight),
                    *this);
            } else {
                LOG_TIME_MILLIS_WITH_CATEGORY("find files to prune", BCLog::BENCH);

                m_blockman.FindFilesToPrune(setFilesToPrune, last_prune, *this, m_chainman);
                m_blockman.m_check_for_pruning = false;
            }
            if (!setFilesToPrune.empty()) {
                fFlushForPrune = true;
                if (!m_blockman.m_have_pruned) {
                    m_blockman.m_block_tree_db->WriteFlag("prunedblockfiles", true);
                    m_blockman.m_have_pruned = true;
                }
            }
        }
        const auto nNow{NodeClock::now()};
        // The cache is large and we're within 10% and 10 MiB of the limit, but we have time now (not in the middle of a block processing).
        bool fCacheLarge = mode == FlushStateMode::PERIODIC && cache_state >= CoinsCacheSizeState::LARGE;
        // The cache is over the limit, we have to write now.
        bool fCacheCritical = mode == FlushStateMode::IF_NEEDED && cache_state >= CoinsCacheSizeState::CRITICAL;
        // It's been a while since we wrote the block index and chain state to disk. Do this frequently, so we don't need to redownload or reindex after a crash.
        bool fPeriodicWrite = mode == FlushStateMode::PERIODIC && nNow >= m_next_write;
        const auto empty_cache{(mode == FlushStateMode::FORCE_FLUSH) || fCacheLarge || fCacheCritical};
        // Combine all conditions that result in a write to disk.
        bool should_write = (mode == FlushStateMode::FORCE_SYNC) || empty_cache || fPeriodicWrite || fFlushForPrune;
        // Write blocks, block index and best chain related state to disk.
        if (should_write) {
            LogDebug(BCLog::COINDB, "Writing chainstate to disk: flush mode=%s, prune=%d, large=%d, critical=%d, periodic=%d",
                     FlushStateModeNames[size_t(mode)], fFlushForPrune, fCacheLarge, fCacheCritical, fPeriodicWrite);

            // Ensure we can write block index
            if (!CheckDiskSpace(m_blockman.m_opts.blocks_dir)) {
                return FatalError(m_chainman.GetNotifications(), state, _("Disk space is too low!"));
            }
            {
                LOG_TIME_MILLIS_WITH_CATEGORY("write block and undo data to disk", BCLog::BENCH);

                // First make sure all block and undo data is flushed to disk.
                // TODO: Handle return error, or add detailed comment why it is
                // safe to not return an error upon failure.
                if (!m_blockman.FlushChainstateBlockFile(m_chain.Height())) {
                    LogWarning("%s: Failed to flush block file.\n", __func__);
                }
            }

            // Then update all block file information (which may refer to block and undo files).
            {
                LOG_TIME_MILLIS_WITH_CATEGORY("write block index to disk", BCLog::BENCH);

                m_blockman.WriteBlockIndexDB();
            }
            // Finally remove any pruned files
            if (fFlushForPrune) {
                LOG_TIME_MILLIS_WITH_CATEGORY("unlink pruned files", BCLog::BENCH);

                m_blockman.UnlinkPrunedFiles(setFilesToPrune);
            }

            if (!CoinsTip().GetBestBlock().IsNull()) {
                // Typical Coin structures on disk are around 48 bytes in size.
                // Pushing a new one to the database can cause it to be written
                // twice (once in the log, and once in the tables). This is already
                // an overestimation, as most will delete an existing entry or
                // overwrite one. Still, use a conservative safety factor of 2.
                if (!CheckDiskSpace(m_chainman.m_options.datadir, 48 * 2 * 2 * CoinsTip().GetDirtyCount())) {
                    return FatalError(m_chainman.GetNotifications(), state, _("Disk space is too low!"));
                }
                // Flush the chainstate (which may refer to block index entries).
                empty_cache ? CoinsTip().Flush() : CoinsTip().Sync();
                full_flush_completed = true;
                TRACEPOINT(utxocache, flush,
                    int64_t{Ticks<std::chrono::microseconds>(NodeClock::now() - nNow)},
                    (uint32_t)mode,
                    (uint64_t)coins_count,
                    (uint64_t)coins_mem_usage,
                    (bool)fFlushForPrune);
            }
        }

        if (should_write || m_next_write == NodeClock::time_point::max()) {
            constexpr auto range{DATABASE_WRITE_INTERVAL_MAX - DATABASE_WRITE_INTERVAL_MIN};
            m_next_write = FastRandomContext().rand_uniform_delay(NodeClock::now() + DATABASE_WRITE_INTERVAL_MIN, range);
        }
    }
    if (full_flush_completed) {
        // Update best block in wallet (so we can detect restored wallets).
        validation::CoreValidationEventQueue{m_chainman.m_options.signals}.ChainStateFlushed(GetLocator(m_chain.Tip()));
    }
    } catch (const std::runtime_error& e) {
        return FatalError(m_chainman.GetNotifications(), state, strprintf(_("System error while flushing: %s"), e.what()));
    }
    return true;
}

void Chainstate::ForceFlushStateToDisk(bool wipe_cache)
{
    BlockValidationState state;
    if (!this->FlushStateToDisk(state, wipe_cache ? FlushStateMode::FORCE_FLUSH : FlushStateMode::FORCE_SYNC)) {
        LogWarning("Failed to force flush state (%s)", state.ToString());
    }
}

void Chainstate::PruneAndFlush()
{
    BlockValidationState state;
    m_blockman.m_check_for_pruning = true;
    if (!this->FlushStateToDisk(state, FlushStateMode::NONE)) {
        LogWarning("Failed to flush state (%s)", state.ToString());
    }
}

static void UpdateTipLog(
    const ChainstateManager& chainman,
    const CCoinsViewCache& coins_tip,
    const CBlockIndex* tip,
    const std::string& func_name,
    const std::string& prefix,
    const std::string& warning_messages) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{

    AssertLockHeld(::cs_main);

    // Disable rate limiting in LogPrintLevel_ so this source location may log during IBD.
    LogPrintLevel_(BCLog::LogFlags::ALL, util::log::Level::Info, /*should_ratelimit=*/false, "%s%s: new best=%s height=%d version=0x%08x log2_work=%f tx=%lu date='%s' progress=%f cache=%.1fMiB(%utxo)%s\n",
                   prefix, func_name,
                   tip->GetBlockHash().ToString(), tip->nHeight, tip->nVersion,
                   log(tip->nChainWork.getdouble()) / log(2.0), tip->m_chain_tx_count,
                   FormatISO8601DateTime(tip->GetBlockTime()),
                   chainman.GuessVerificationProgress(tip),
                   coins_tip.DynamicMemoryUsage() / double(1_MiB),
                   coins_tip.GetCacheSize(),
                   !warning_messages.empty() ? strprintf(" warning='%s'", warning_messages) : "");
}

void Chainstate::UpdateTip(const CBlockIndex* pindexNew, ChainstateEventSink* chain_events)
{
    AssertLockHeld(::cs_main);
    const auto& coins_tip = this->CoinsTip();

    // New best block
    if (chain_events) {
        chain_events->TransactionsUpdated();
    }

    std::vector<bilingual_str> warning_messages;
    if (!m_chainman.IsInitialBlockDownload()) {
        auto bits = m_chainman.m_versionbitscache.CheckUnknownActivations(pindexNew, m_chainman.GetParams());
        for (auto [bit, active] : bits) {
            const bilingual_str warning = strprintf(_("Unknown new rules activated (versionbit %i)"), bit);
            if (active) {
                m_chainman.GetNotifications().warningSet(kernel::Warning::UNKNOWN_NEW_RULES_ACTIVATED, warning);
            } else {
                warning_messages.push_back(warning);
            }
        }
    }
    UpdateTipLog(m_chainman, coins_tip, pindexNew, __func__, "",
                 util::Join(warning_messages, Untranslated(", ")).original);
}

/** Disconnect m_chain's tip. */
bool Chainstate::DisconnectTip(
    BlockValidationState& state,
    ChainstateEventSink* chain_events)
{
    AssertLockHeld(cs_main);

    CBlockIndex *pindexDelete = m_chain.Tip();
    assert(pindexDelete);
    assert(pindexDelete->pprev);
    CoreBlockDataStore block_store{m_blockman};
    // Read block from disk.
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock& block = *pblock;
    if (!block_store.ReadBlock(block, *pindexDelete)) {
        LogError("DisconnectTip(): Failed to read block\n");
        return false;
    }
    // Apply the block atomically to the chain state.
    const auto time_start{SteadyClock::now()};
    {
        CCoinsViewCache view(&CoinsTip());
        assert(view.GetBestBlock() == pindexDelete->GetBlockHash());
        if (DisconnectBlock(block_store, block, pindexDelete, view) != DISCONNECT_OK) {
            LogError("DisconnectTip(): DisconnectBlock %s failed\n", pindexDelete->GetBlockHash().ToString());
            return false;
        }
        view.Flush(/*reallocate_cache=*/false); // local CCoinsViewCache goes out of scope
    }
    LogDebug(BCLog::BENCH, "- Disconnect block: %.2fms\n",
             Ticks<MillisecondsDouble>(SteadyClock::now() - time_start));

    {
        // Prune locks that began at or after the tip should be moved backward so they get a chance to reorg
        const int max_height_first{pindexDelete->nHeight - 1};
        for (auto& prune_lock : m_blockman.m_prune_locks) {
            if (prune_lock.second.height_first <= max_height_first) continue;

            prune_lock.second.height_first = max_height_first;
            LogDebug(BCLog::PRUNE, "%s prune lock moved back to %d\n", prune_lock.first, max_height_first);
        }
    }

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FlushStateMode::IF_NEEDED, /*nManualPruneHeight=*/0, ExternalCacheUsageForEvents(chain_events))) {
        return false;
    }

    if (chain_events) {
        chain_events->BlockDisconnected(block);
    }

    SetActiveChainTip(*pindexDelete->pprev);
    m_chainman.UpdateIBDStatus();

    UpdateTip(pindexDelete->pprev, chain_events);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    validation::CoreValidationEventQueue{m_chainman.m_options.signals}.BlockDisconnected(std::move(pblock), pindexDelete);
    return true;
}

bool Chainstate::ReplayBlocks()
{
    CoreBlockDataStore block_store{m_blockman};
    CoreBlockIndexStore block_index{m_chainman};
    const BlockReplayRequest request{
        .coins_db = CoinsDB(),
        .block_reader = block_store,
        .undo_reader = block_store,
        .block_index = block_index,
        .notifications = m_chainman.GetNotifications(),
    };
    return ::ReplayBlocks(request);
}

void Chainstate::SetActiveChainTip(CBlockIndex& block_index)
{
    AssertLockHeld(::cs_main);
    m_chain.SetTip(block_index);
    m_chainman.UpdateActiveTipSnapshot(&block_index);
}

void Chainstate::AdvanceActiveChainTip(CBlockIndex& block_index, ChainstateEventSink* chain_events)
{
    AssertLockHeld(cs_main);

    SetActiveChainTip(block_index);
    m_chainman.UpdateIBDStatus();
    UpdateTip(&block_index, chain_events);
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
CBlockIndex* Chainstate::FindMostWorkChain()
{
    AssertLockHeld(::cs_main);
    do {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return nullptr;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        bool fInvalidAncestor = false;
        for (CBlockIndex *pindexTest = pindexNew; pindexTest && !m_chain.Contains(*pindexTest); pindexTest = pindexTest->pprev) {
            assert(pindexTest->HaveNumChainTxs() || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_VALID;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (m_chainman.m_best_invalid == nullptr || pindexNew->nChainWork > m_chainman.m_best_invalid->nChainWork)) {
                    m_chainman.m_best_invalid = pindexNew;
                }
                // Remove the entire chain from the set.
                for (CBlockIndex *pindexFailed = pindexNew; pindexFailed != pindexTest; pindexFailed = pindexFailed->pprev) {
                    if (fMissingData && !fFailedChain) {
                        // If we're missing data and not a descendant of an invalid block,
                        // then add back to m_blocks_unlinked, so that if the block arrives in the future
                        // we can try adding to setBlockIndexCandidates again.
                        m_blockman.m_blocks_unlinked.insert(
                            std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
void Chainstate::PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, m_chain.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

static SynchronizationState GetSynchronizationState(bool init, bool blockfiles_indexed)
{
    if (!init) return SynchronizationState::POST_INIT;
    if (!blockfiles_indexed) return SynchronizationState::INIT_REINDEX;
    return SynchronizationState::INIT_DOWNLOAD;
}

void ChainstateManager::UpdateIBDStatus()
{
    AssertLockHeld(cs_main);
    if (!m_cached_is_ibd.load(std::memory_order_relaxed)) return;
    if (m_blockman.LoadingBlocks()) return;
    if (!ActiveChain().IsTipRecent(MinimumChainWork(), m_options.max_tip_age)) return;
    LogInfo("Leaving InitialBlockDownload (latching to false)");
    m_cached_is_ibd.store(false, std::memory_order_relaxed);
}

bool ChainstateManager::NotifyHeaderTip()
{
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    CBlockIndex* pindexHeader = nullptr;
    {
        LOCK(GetMutex());
        pindexHeader = m_best_header;

        if (pindexHeader != m_last_notified_header) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            m_last_notified_header = pindexHeader;
        }
    }
    // Send block tip changed notifications without the lock held
    if (fNotify) {
        GetNotifications().headerTip(GetSynchronizationState(fInitialBlockDownload, m_blockman.m_blockfiles_indexed), pindexHeader->nHeight, pindexHeader->nTime, false);
    }
    return fNotify;
}

static void LimitValidationInterfaceQueue(ValidationSignals& signals) LOCKS_EXCLUDED(cs_main)
{
    AssertLockNotHeld(cs_main);

    if (signals.CallbacksPending() > 10) {
        signals.SyncWithValidationInterfaceQueue();
    }
}

enum class ActivateBestChainLockedResult {
    Continue,
    NoWork,
    Interrupted,
    SystemError,
};

struct ActivateBestChainProgress {
    CBlockIndex* most_work{nullptr};
    CBlockIndex* new_tip{nullptr};
};

class ChainstateActivationOrchestrator final
{
public:
    ChainstateActivationOrchestrator(
        Chainstate& chainstate,
        BlockValidationState& state,
        std::shared_ptr<const CBlock> cached_block,
        ChainstateEventSink* chain_events)
        : m_chainstate{chainstate},
          m_state{state},
          m_cached_block{std::move(cached_block)},
          m_chain_events{chain_events},
          m_commit_executor{chainstate.m_chainstate_mutex}
    {
    }

    bool Activate()
    {
        return m_commit_executor.RunSerialized([&]() -> bool {
            return ActivateSerialized();
        });
    }

private:
    bool ActivateSerialized()
    {
        do {
            LimitValidationQueueIfNeeded();

            const ActivateBestChainLockedResult locked_result{
                m_commit_executor.RunChainstateCommitLockedWithUnlock([&](CoreChainLock& chain_lock) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
                    return ActivateLocked(chain_lock);
                })};

            if (locked_result == ActivateBestChainLockedResult::SystemError) return false;
            if (locked_result == ActivateBestChainLockedResult::NoWork) return true;
            if (locked_result == ActivateBestChainLockedResult::Interrupted) break;

            if (!FlushPeriodic()) return false;

            // Give activation a chance to run once before honoring interrupt so
            // genesis connection during LoadChainTip cannot leave a null best
            // block in the UTXO DB flush checks.
            if (m_chainstate.m_chainman.m_interrupt) break;
        } while (m_progress.new_tip != m_progress.most_work);

        m_chainstate.m_chainman.CheckBlockIndex();
        return true;
    }

    void LimitValidationQueueIfNeeded() const
    {
        // Block until the validation queue drains. This should rarely happen in
        // normal operation, but can happen during reindex and otherwise cause
        // memory blowup if activation runs too far ahead.
        if (m_chainstate.m_chainman.m_options.signals) {
            LimitValidationInterfaceQueue(*m_chainstate.m_chainman.m_options.signals);
        }
    }

    ActivateBestChainLockedResult ActivateLocked(CoreChainLock& chain_lock) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        CBlockIndex* starting_tip{m_chainstate.m_chain.Tip()};
        bool blocks_connected{false};
        CoreChainValidationRuntime validation_runtime{m_chainstate.m_chainman};
        validation::ValidationEventQueue& validation_events{validation_runtime.ValidationEvents()};
        do {
            std::vector<ConnectedBlock> connected_blocks; // Destructed before cs_main is unlocked.

            if (m_progress.most_work == nullptr) {
                m_progress.most_work = m_chainstate.FindMostWorkChain();
            }

            if (m_progress.most_work == nullptr || m_progress.most_work == m_chainstate.m_chain.Tip()) {
                break;
            }

            CoreBlockDataStore block_store{m_chainstate.m_blockman};
            CoreBlockIndexStore block_index_store{m_chainstate.m_chainman};
            validation::CoreCoinsBlockConnectionState connection_state{*m_chainstate.m_coins_views->m_block_connection_view};
            CoreChainActivationState activation_state{m_chainstate};
            CoreChainValidationContext validation_context{m_chainstate.m_chainman, validation_runtime};
            CoreConnectTipResources connection_resources{
                .context = validation_context,
                .block_reader = block_store,
                .undo_writer = block_store,
                .block_index_lookup = block_index_store,
                .block_index_committer = block_index_store,
                .connection_state = connection_state,
                .last_script_check_reason_logged = m_chainstate.LastScriptCheckReasonLogged(),
                .connected_blocks = connected_blocks,
                .chain_events = m_chain_events,
                .validation_events = validation_events,
                .timing = {
                    .time_connect_total = m_chainstate.m_chainman.TimeConnectTotal(),
                    .time_flush = m_chainstate.m_chainman.TimeFlush(),
                    .time_chainstate = m_chainstate.m_chainman.TimeChainstate(),
                    .time_post_connect = m_chainstate.m_chainman.TimePostConnect(),
                    .time_total = m_chainstate.m_chainman.TimeTotal(),
                    .blocks_total = m_chainstate.m_chainman.NumBlocksTotal(),
                },
                .chain_lock = &chain_lock,
            };
            const auto step_result{ActivateCoreBestChainStep(
                {
                    .active_chain = activation_state,
                    .connection = connection_resources,
                    .index_most_work = *m_progress.most_work,
                    .cached_best_block = CachedBlockForMostWork(),
                },
                m_state)};
            if (step_result.HasSystemError()) return ActivateBestChainLockedResult::SystemError;

            blocks_connected = true;
            if (step_result.FoundInvalidChain()) {
                m_progress.most_work = nullptr;
            }
            m_progress.new_tip = m_chainstate.m_chain.Tip();

            for (auto& [index, block] : std::move(connected_blocks)) {
                validation_events.BlockConnected(std::move(Assert(block)), Assert(index));
            }
        } while (!m_chainstate.m_chain.Tip() || (starting_tip && CBlockIndexWorkComparator()(m_chainstate.m_chain.Tip(), starting_tip)));

        if (!blocks_connected) return ActivateBestChainLockedResult::NoWork;
        return NotifyLocked(starting_tip, validation_events);
    }

    std::shared_ptr<const CBlock> CachedBlockForMostWork() const
    {
        if (!m_cached_block || !m_progress.most_work) return {};
        return m_cached_block->GetHash() == m_progress.most_work->GetBlockHash() ? m_cached_block : std::shared_ptr<const CBlock>{};
    }

    ActivateBestChainLockedResult NotifyLocked(
        CBlockIndex* starting_tip,
        validation::ValidationEventQueue& validation_events) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        const CBlockIndex* pindex_fork{starting_tip ? m_chainstate.m_chain.FindFork(*starting_tip) : nullptr};
        const bool still_in_ibd{m_chainstate.m_chainman.IsInitialBlockDownload()};

        // Enqueue while holding cs_main so UpdatedBlockTip is called in the
        // same order blocks are connected.
        if (&m_chainstate == &m_chainstate.m_chainman.ActiveChainstate() && pindex_fork != m_progress.new_tip) {
            validation_events.UpdatedBlockTip(m_progress.new_tip, pindex_fork, still_in_ibd);

            if (kernel::IsInterrupted(m_chainstate.m_chainman.GetNotifications().blockTip(
                    /*state=*/GetSynchronizationState(still_in_ibd, m_chainstate.m_chainman.m_blockman.m_blockfiles_indexed),
                    /*index=*/*m_progress.new_tip,
                    /*verification_progress=*/m_chainstate.m_chainman.GuessVerificationProgress(m_progress.new_tip)))) {
                return ActivateBestChainLockedResult::Interrupted;
            }
        }

        if (&m_chainstate == &m_chainstate.m_chainman.ActiveChainstate()) {
            validation_events.ActiveTipChange(*Assert(m_progress.new_tip), m_chainstate.m_chainman.IsInitialBlockDownload());
        }
        return ActivateBestChainLockedResult::Continue;
    }

    bool FlushPeriodic()
    {
        return m_commit_executor.RunStorageCoordinationLocked([&]() EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
            return m_chainstate.FlushStateToDisk(
                m_state,
                FlushStateMode::PERIODIC,
                /*nManualPruneHeight=*/0,
                ExternalCacheUsageForEvents(m_chain_events));
        });
    }

    Chainstate& m_chainstate;
    BlockValidationState& m_state;
    std::shared_ptr<const CBlock> m_cached_block;
    ChainstateEventSink* m_chain_events;
    validation::CoreValidationCommitExecutor m_commit_executor;
    ActivateBestChainProgress m_progress;
};

bool Chainstate::ActivateBestChain(
    BlockValidationState& state,
    std::shared_ptr<const CBlock> pblock,
    ChainstateEventSink* chain_events)
{
    AssertLockNotHeld(m_chainstate_mutex);

    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!
    AssertLockNotHeld(::cs_main);

    return ChainstateActivationOrchestrator{*this, state, std::move(pblock), chain_events}.Activate();
}

bool Chainstate::PreciousBlock(BlockValidationState& state, CBlockIndex* pindex, ChainstateEventSink* chain_events)
{
    AssertLockNotHeld(m_chainstate_mutex);
    AssertLockNotHeld(::cs_main);
    {
        LOCK(cs_main);
        if (pindex->nChainWork < m_chain.Tip()->nChainWork) {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (m_chain.Tip()->nChainWork > m_chainman.nLastPreciousChainwork) {
            // The chain has been extended since the last call, reset the counter.
            m_chainman.nBlockReverseSequenceId = -1;
        }
        m_chainman.nLastPreciousChainwork = m_chain.Tip()->nChainWork;
        setBlockIndexCandidates.erase(pindex);
        pindex->nSequenceId = m_chainman.nBlockReverseSequenceId;
        if (m_chainman.nBlockReverseSequenceId > std::numeric_limits<int32_t>::min()) {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            m_chainman.nBlockReverseSequenceId--;
        }
        if (CanBeBlockIndexCandidate(*pindex)) {
            setBlockIndexCandidates.insert(pindex);
            PruneBlockIndexCandidates();
        }
    }

    return ActivateBestChain(state, std::shared_ptr<const CBlock>(), chain_events);
}

bool Chainstate::InvalidateBlock(BlockValidationState& state, CBlockIndex* const pindex, ChainstateEventSink* chain_events)
{
    AssertLockNotHeld(m_chainstate_mutex);
    AssertLockNotHeld(::cs_main);

    // Genesis block can't be invalidated
    assert(pindex);
    if (pindex->nHeight == 0) return false;

    validation::CoreValidationCommitExecutor commit_executor{m_chainstate_mutex};
    return commit_executor.RunSerialized([&]() -> bool {
        // We do not allow ActivateBestChain() to run while InvalidateBlock() is
        // running, as that could cause the tip to change while we disconnect
        // blocks.

        // We'll be acquiring and releasing cs_main below, to allow the validation
        // callbacks to run. However, we should keep the block index in a
        // consistent state as we disconnect blocks -- in particular we need to
        // add equal-work blocks to setBlockIndexCandidates as we disconnect.
        // To avoid walking the block index repeatedly in search of candidates,
        // build a map once so that we can look up candidate blocks by chain
        // work as we go.
        std::multimap<const arith_uint256, CBlockIndex*> highpow_outofchain_headers;

        commit_executor.RunBlockIndexLocked([&]() EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
            CoreBlockIndexStore block_index{m_chainman};
            highpow_outofchain_headers = CollectHighWorkOutOfChainHeaders(
                block_index.SnapshotBlockIndices(),
                m_chain,
                *Assert(pindex->pprev));
        });

        CBlockIndex* to_mark_failed = pindex;
        bool pindex_was_in_chain = false;
        int disconnected = 0;

        // Disconnect (descendants of) pindex, and mark them invalid.
        while (true) {
            if (m_chainman.m_interrupt) break;

            // Make sure the queue of validation callbacks doesn't grow unboundedly.
            if (m_chainman.m_options.signals) LimitValidationInterfaceQueue(*m_chainman.m_options.signals);

            const auto disconnect_result{commit_executor.RunChainstateCommitLocked([&]() EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
                CoreBlockIndexStore block_index_store{m_chainman};
                ChainstateEventRecorder event_batch{chain_events ? chain_events->CacheUsage() : ExternalCacheUsage{}};
                ChainstateEventSink* repair_events{chain_events ? &event_batch : nullptr};
                if (!m_chain.Contains(*pindex)) return std::optional<bool>{};
                pindex_was_in_chain = true;
                CBlockIndex* const disconnected_tip{m_chain.Tip()};

                // ActivateBestChain considers blocks already in m_chain
                // unconditionally valid already, so force disconnect away from it.
                bool ret = DisconnectTip(state, repair_events);
                // Let the event sink repair node state for the new tip. Deep
                // invalidations skip restoration of disconnected transactions.
                if (chain_events) {
                    event_batch.ReorgCompleted(/*restore_disconnected_transactions=*/(++disconnected <= 10) && ret);
                    chain_events->ProcessEvents(event_batch.Events());
                }
                if (!ret) return std::optional<bool>{false};
                CBlockIndex* new_tip{m_chain.Tip()};
                assert(disconnected_tip->pprev == new_tip);

                // We immediately mark the disconnected blocks as invalid.
                // This prevents a case where pruned nodes may fail to invalidateblock
                // and be left unable to start as they have no tip candidates (as there
                // are no blocks that meet the "have data and are not invalid per
                // nStatus" criteria for inclusion in setBlockIndexCandidates).
                disconnected_tip->nStatus |= BLOCK_FAILED_VALID;
                block_index_store.MarkBlockIndexDirty(*disconnected_tip);
                setBlockIndexCandidates.erase(disconnected_tip);
                setBlockIndexCandidates.insert(new_tip);

                // Mark out-of-chain descendants of the invalidated block as invalid
                // Add any equal or more work headers that are not invalidated to setBlockIndexCandidates
                // Recalculate m_best_header if it became invalid.
                auto candidate_it = highpow_outofchain_headers.lower_bound(new_tip->nChainWork);

                const bool best_header_needs_update{m_chainman.m_best_header->GetAncestor(disconnected_tip->nHeight) == disconnected_tip};
                if (best_header_needs_update) {
                    // new_tip is definitely still valid at this point, but there may be better ones
                    m_chainman.SetBestHeader(new_tip);
                }

                while (candidate_it != highpow_outofchain_headers.end()) {
                    CBlockIndex* candidate{candidate_it->second};
                    if (candidate->GetAncestor(disconnected_tip->nHeight) == disconnected_tip) {
                        // Children of failed blocks are marked as BLOCK_FAILED_VALID.
                        candidate->nStatus |= BLOCK_FAILED_VALID;
                        block_index_store.MarkBlockIndexDirty(*candidate);
                        // If invalidated, the block is irrelevant for setBlockIndexCandidates
                        // and for m_best_header and can be removed from the cache.
                        candidate_it = highpow_outofchain_headers.erase(candidate_it);
                        continue;
                    }
                    if (!CBlockIndexWorkComparator()(candidate, new_tip) &&
                        candidate->IsValid(BLOCK_VALID_TRANSACTIONS) &&
                        candidate->HaveNumChainTxs()) {
                        setBlockIndexCandidates.insert(candidate);
                        // Do not remove candidate from the highpow_outofchain_headers cache, because it might be a descendant of the block being invalidated
                        // which needs to be marked failed later.
                    }
                    if (best_header_needs_update &&
                        m_chainman.m_best_header->nChainWork < candidate->nChainWork) {
                        m_chainman.SetBestHeader(candidate);
                    }
                    ++candidate_it;
                }

                // Track the last disconnected block to call InvalidChainFound on it.
                to_mark_failed = disconnected_tip;
                return std::optional<bool>{true};
            })};
            if (!disconnect_result) break;
            if (!*disconnect_result) return false;
        }

        m_chainman.CheckBlockIndex();

        const bool final_update_ok{commit_executor.RunBlockIndexLocked([&]() EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
            if (m_chain.Contains(*to_mark_failed)) {
                // If the to-be-marked invalid block is in the active chain, something is interfering and we can't proceed.
                return false;
            }

            // Mark pindex as invalid if it never was in the main chain
            if (!pindex_was_in_chain && !(pindex->nStatus & BLOCK_FAILED_VALID)) {
                CoreBlockIndexStore block_index_store{m_chainman};
                pindex->nStatus |= BLOCK_FAILED_VALID;
                block_index_store.MarkBlockIndexDirty(*pindex);
                setBlockIndexCandidates.erase(pindex);
            }

            // If any new blocks somehow arrived while we were disconnecting
            // (above), then the pre-calculation of what should go into
            // setBlockIndexCandidates may have missed entries. This would
            // technically be an inconsistency in the block index, but if we clean
            // it up here, this should be an essentially unobservable error.
            // Loop back over all block index entries and add any missing entries
            // to setBlockIndexCandidates.
            CoreBlockIndexStore block_index_store{m_chainman};
            AddMissingBlockIndexCandidates(block_index_store.SnapshotBlockIndices(), m_chain, setBlockIndexCandidates);

            InvalidChainFound(to_mark_failed);
            return true;
        })};
        if (!final_update_ok) return false;

        // Only notify about a new block tip if the active chain was modified.
        if (pindex_was_in_chain) {
            // Ignoring return value for now, this could be changed to bubble up
            // kernel::Interrupted value to the caller so the caller could
            // distinguish between completed and interrupted operations. It might
            // also make sense for the blockTip notification to have an enum
            // parameter indicating the source of the tip change so hooks can
            // distinguish user-initiated invalidateblock changes from other
            // changes.
            (void)m_chainman.GetNotifications().blockTip(
                /*state=*/GetSynchronizationState(m_chainman.IsInitialBlockDownload(), m_chainman.m_blockman.m_blockfiles_indexed),
                /*index=*/*to_mark_failed->pprev,
                /*verification_progress=*/WITH_LOCK(m_chainman.GetMutex(), return m_chainman.GuessVerificationProgress(to_mark_failed->pprev)));

            // Fire ActiveTipChange now for the current chain tip to make sure clients are notified.
            // ActivateBestChain may call this as well, but not necessarily.
            validation::CoreValidationEventQueue{m_chainman.m_options.signals}.ActiveTipChange(*Assert(m_chain.Tip()), m_chainman.IsInitialBlockDownload());
        }
        return true;
    });
}

void Chainstate::SetBlockFailureFlags(CBlockIndex* invalid_block)
{
    AssertLockHeld(cs_main);

    CoreBlockIndexStore block_index_store{m_chainman};
    for (CBlockIndex* block_index : block_index_store.SnapshotBlockIndices()) {
        if (invalid_block != block_index && block_index->GetAncestor(invalid_block->nHeight) == invalid_block) {
            block_index->nStatus |= BLOCK_FAILED_VALID;
            block_index_store.MarkBlockIndexDirty(*block_index);
        }
    }
}

void Chainstate::ResetBlockFailureFlags(CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants and ancestors.
    CoreBlockIndexStore block_index_store{m_chainman};
    for (CBlockIndex* block_index : block_index_store.SnapshotBlockIndices()) {
        if ((block_index->nStatus & BLOCK_FAILED_VALID) && (block_index->GetAncestor(nHeight) == pindex || pindex->GetAncestor(block_index->nHeight) == block_index)) {
            block_index->nStatus &= ~BLOCK_FAILED_VALID;
            block_index_store.MarkBlockIndexDirty(*block_index);
            if (CanBeBlockIndexCandidate(*block_index) && setBlockIndexCandidates.value_comp()(m_chain.Tip(), block_index)) {
                setBlockIndexCandidates.insert(block_index);
            }
            if (block_index == m_chainman.m_best_invalid) {
                // Reset invalid block marker if it was pointing to one of those.
                m_chainman.m_best_invalid = nullptr;
            }
        }
    }
}

void Chainstate::TryAddBlockIndexCandidate(CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    // The block only is a candidate for the most-work-chain if it has the same
    // or more work than our current tip.
    if (m_chain.Tip() != nullptr && setBlockIndexCandidates.value_comp()(pindex, m_chain.Tip())) {
        return;
    }

    setBlockIndexCandidates.insert(pindex);
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
void ChainstateManager::ReceivedBlockTransactions(const CBlock& block, CBlockIndex* pindexNew, const FlatFilePos& pos)
{
    AssertLockHeld(cs_main);
    pindexNew->nTx = block.vtx.size();
    // Typically m_chain_tx_count will be 0 at this point, but it can be nonzero if this
    // is a pruned block which is being downloaded again. If m_chain_tx_count is set,
    // assert that the value is correct.
    auto prev_tx_sum = [](CBlockIndex& block) { return block.nTx + (block.pprev ? block.pprev->m_chain_tx_count : 0); };
    if (!Assume(pindexNew->m_chain_tx_count == 0 || pindexNew->m_chain_tx_count == prev_tx_sum(*pindexNew))) {
        LogWarning("Internal bug detected: block %d has unexpected m_chain_tx_count %i that should be %i (%s %s). Please report this issue here: %s\n",
            pindexNew->nHeight, pindexNew->m_chain_tx_count, prev_tx_sum(*pindexNew), CLIENT_NAME, FormatFullVersion(), CLIENT_BUGREPORT);
        pindexNew->m_chain_tx_count = 0;
    }
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    if (DeploymentActiveAt(*pindexNew, *this, Consensus::DEPLOYMENT_SEGWIT)) {
        pindexNew->nStatus |= BLOCK_OPT_WITNESS;
    }
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    CoreBlockIndexStore block_index_store{*this};
    block_index_store.MarkBlockIndexDirty(*pindexNew);

    if (pindexNew->pprev == nullptr || pindexNew->pprev->HaveNumChainTxs()) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            // Before setting m_chain_tx_count, assert that it is 0 or already set to
            // the correct value.
            if (!Assume(pindex->m_chain_tx_count == 0 || pindex->m_chain_tx_count == prev_tx_sum(*pindex))) {
                LogWarning("Internal bug detected: block %d has unexpected m_chain_tx_count %i that should be %i (%s %s). Please report this issue here: %s\n",
                   pindex->nHeight, pindex->m_chain_tx_count, prev_tx_sum(*pindex), CLIENT_NAME, FormatFullVersion(), CLIENT_BUGREPORT);
            }
            pindex->m_chain_tx_count = prev_tx_sum(*pindex);
            pindex->nSequenceId = nBlockSequenceId++;
            if (m_chainstate) m_chainstate->TryAddBlockIndexCandidate(pindex);
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = m_blockman.m_blocks_unlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                m_blockman.m_blocks_unlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            m_blockman.m_blocks_unlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }
}






bool Chainstate::LoadChainTip()
{
    AssertLockHeld(cs_main);
    const CCoinsViewCache& coins_cache = CoinsTip();
    assert(!coins_cache.GetBestBlock().IsNull()); // Never called when the coins view is empty
    CBlockIndex* tip = m_chain.Tip();

    if (tip && tip->GetBlockHash() == coins_cache.GetBestBlock()) {
        return true;
    }

    // Load pointer to end of best chain
    CoreBlockIndexStore block_index{m_chainman};
    CBlockIndex* pindex = block_index.LookupBlockIndex(coins_cache.GetBestBlock());
    if (!pindex) {
        return false;
    }
    SetActiveChainTip(*pindex);
    m_chainman.UpdateIBDStatus();
    tip = m_chain.Tip();

    // nSequenceId is one of the keys used to sort setBlockIndexCandidates. Ensure the
    // candidate set is empty to avoid UB, as nSequenceId is about to be modified.
    assert(m_chainman.m_chainstate->setBlockIndexCandidates.empty());

    // Make sure our chain tip before shutting down scores better than any other candidate
    // to maintain a consistent best tip over reboots in case of a tie.
    auto target = tip;
    while (target) {
        target->nSequenceId = SEQ_ID_BEST_CHAIN_FROM_DISK;
        target = target->pprev;
    }

    LogInfo("Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f",
              tip->GetBlockHash().ToString(),
              m_chain.Height(),
              FormatISO8601DateTime(tip->GetBlockTime()),
              m_chainman.GuessVerificationProgress(tip));

    // Ensure KernelNotifications m_tip_block is set even if no new block arrives.
    {
        // Ignoring return value for now.
        (void)m_chainman.GetNotifications().blockTip(
            /*state=*/GetSynchronizationState(/*init=*/true, m_chainman.m_blockman.m_blockfiles_indexed),
            /*index=*/*pindex,
            /*verification_progress=*/m_chainman.GuessVerificationProgress(tip));
    }

    CheckForkWarningConditions();

    return true;
}



bool Chainstate::NeedsRedownload() const
{
    AssertLockHeld(cs_main);

    // At and above m_params.SegwitHeight, segwit consensus rules must be validated
    CBlockIndex* block{m_chain.Tip()};

    while (block != nullptr && DeploymentActiveAt(*block, m_chainman, Consensus::DEPLOYMENT_SEGWIT)) {
        if (!(block->nStatus & BLOCK_OPT_WITNESS)) {
            // block is insufficiently validated for a segwit client
            return true;
        }
        block = block->pprev;
    }

    return false;
}

void Chainstate::ClearBlockIndexCandidates()
{
    AssertLockHeld(::cs_main);
    setBlockIndexCandidates.clear();
}

void Chainstate::PopulateBlockIndexCandidates()
{
    AssertLockHeld(::cs_main);

    for (CBlockIndex* pindex : m_blockman.GetAllBlockIndices()) {
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) &&
                (pindex->HaveNumChainTxs() || pindex->pprev == nullptr)) {
            TryAddBlockIndexCandidate(pindex);
        }
    }
}

bool ChainstateManager::LoadBlockIndex()
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    AssertLockHeld(cs_main);
    // Load block index from databases
    if (m_blockman.m_blockfiles_indexed) {
        bool ret{m_blockman.LoadBlockIndexDB()};
        if (!ret) return false;

        m_blockman.ScanAndUnlinkAlreadyPrunedFiles();

        std::vector<CBlockIndex*> vSortedByHeight{m_blockman.GetAllBlockIndices()};
        std::sort(vSortedByHeight.begin(), vSortedByHeight.end(),
                  CBlockIndexHeightOnlyComparator());

        for (CBlockIndex* pindex : vSortedByHeight) {
            if (m_interrupt) return false;
            if (pindex->nStatus & BLOCK_FAILED_VALID && (!m_best_invalid || pindex->nChainWork > m_best_invalid->nChainWork)) {
                m_best_invalid = pindex;
            }
            if (pindex->IsValid(BLOCK_VALID_TREE) && (m_best_header == nullptr || CBlockIndexWorkComparator()(m_best_header, pindex))) {
                SetBestHeader(pindex);
            }
        }
    }
    return true;
}

bool Chainstate::LoadGenesisBlock()
{
    LOCK(cs_main);

    const CChainParams& params{m_chainman.GetParams()};
    CoreBlockDataStore block_store{m_blockman};
    CoreBlockIndexStore block_index{m_chainman};

    // Check whether we're already initialized by checking for genesis in the
    // block index. Note that we can't use m_chain here, since it is
    // set based on the coins db, not the block index db, which is the only
    // thing loaded at this point.
    if (block_index.LookupBlockIndex(params.GenesisBlock().GetHash()))
        return true;

    try {
        const CBlock& block = params.GenesisBlock();
        FlatFilePos blockPos{block_store.WriteBlock(block, 0)};
        if (blockPos.IsNull()) {
            LogError("%s: writing genesis block to disk failed\n", __func__);
            return false;
        }
        CBlockIndex* pindex = block_index.AddToBlockIndex(block);
        m_chainman.ReceivedBlockTransactions(block, pindex, blockPos);
    } catch (const std::runtime_error& e) {
        LogError("%s: failed to write genesis block: %s\n", __func__, e.what());
        return false;
    }

    return true;
}

void ChainstateManager::LoadExternalBlockFile(
    AutoFile& file_in,
    FlatFilePos* dbp,
    std::multimap<uint256, FlatFilePos>* blocks_with_unknown_parent)
{
    // Either both should be specified (-reindex), or neither (-loadblock).
    assert(!dbp == !blocks_with_unknown_parent);

    const auto start{SteadyClock::now()};
    const CChainParams& params{GetParams()};
    CoreBlockDataStore block_store{m_blockman};
    CoreBlockIndexStore block_index{*this};

    int nLoaded = 0;
    try {
        BufferedFile blkdat{file_in, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8};
        // nRewind indicates where to resume scanning in case something goes wrong,
        // such as a block fails to deserialize.
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            if (m_interrupt) return;

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                MessageStartChars buf;
                blkdat.FindByte(std::byte(params.MessageStart()[0]));
                nRewind = blkdat.GetPos() + 1;
                blkdat >> buf;
                if (buf != params.MessageStart()) {
                    continue;
                }
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                // (this happens at the end of every blk.dat file)
                break;
            }
            try {
                // read block header
                const uint64_t nBlockPos{blkdat.GetPos()};
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                CBlockHeader header;
                blkdat >> header;
                const uint256 hash{header.GetHash()};
                // Skip the rest of this block (this may read from disk into memory); position to the marker before the
                // next block, but it's still possible to rewind to the start of the current block (without a disk read).
                nRewind = nBlockPos + nSize;
                blkdat.SkipTo(nRewind);

                std::shared_ptr<CBlock> pblock{}; // needs to remain available after the cs_main lock is released to avoid duplicate reads from disk

                {
                    LOCK(cs_main);
                    // detect out of order blocks, and store them for later
                    if (hash != params.GetConsensus().hashGenesisBlock && !block_index.LookupBlockIndex(header.hashPrevBlock)) {
                        LogDebug(BCLog::REINDEX, "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                                 header.hashPrevBlock.ToString());
                        if (dbp && blocks_with_unknown_parent) {
                            blocks_with_unknown_parent->emplace(header.hashPrevBlock, *dbp);
                        }
                        continue;
                    }

                    // process in case the block isn't known yet
                    const CBlockIndex* pindex = block_index.LookupBlockIndex(hash);
                    if (!pindex || (pindex->nStatus & BLOCK_HAVE_DATA) == 0) {
                        // This block can be processed immediately; rewind to its start, read and deserialize it.
                        blkdat.SetPos(nBlockPos);
                        pblock = std::make_shared<CBlock>();
                        blkdat >> TX_WITH_WITNESS(*pblock);
                        nRewind = blkdat.GetPos();

                        BlockValidationState state;
                        if (ChainValidationService{*this}.AcceptBlock(
                                pblock,
                                state,
                                {.block_data_storage = BlockDataStorageMode::ForceStore, .existing_block_pos = dbp, .header = {.min_pow_checked = true}},
                                CurrentBlockValidationTime())
                                .accepted_for_processing()) {
                            nLoaded++;
                        }
                        if (state.IsError()) {
                            break;
                        }
                    } else if (hash != params.GetConsensus().hashGenesisBlock && pindex->nHeight % 1000 == 0) {
                        LogDebug(BCLog::REINDEX, "Block Import: already had block %s at height %d\n", hash.ToString(), pindex->nHeight);
                    }
                }

                // Activate the genesis block so normal node progress can continue
                // During first -reindex, this will only connect Genesis since
                // ActivateBestChain only connects blocks which are in the block tree db,
                // which only contains blocks whose parents are in it.
                // But do this only if genesis isn't activated yet, to avoid connecting many blocks
                // without assumevalid in the case of a continuation of a reindex that
                // was interrupted by the user.
                if (hash == params.GetConsensus().hashGenesisBlock && WITH_LOCK(::cs_main, return ActiveHeight()) == -1) {
                    BlockValidationState state;
                    if (!ActiveChainstate().ActivateBestChain(state, nullptr)) {
                        break;
                    }
                }

                if (block_store.IsPruneMode() && block_store.HasIndexedBlockFiles() && pblock) {
                    // must update the tip for pruning to work while importing with -loadblock.
                    // this is a tradeoff to conserve disk space at the expense of time
                    // spent updating the tip to be able to prune.
                    // otherwise, ActivateBestChain won't be called by the import process
                    // until after all of the block files are loaded. ActivateBestChain can be
                    // called by concurrent network message processing. but, that is not
                    // reliable for the purpose of pruning while importing.
                    if (auto result{ActivateBestChains()}; !result) {
                        LogDebug(BCLog::REINDEX, "%s\n", util::ErrorString(result).original);
                        break;
                    }
                }

                NotifyHeaderTip();

                if (!blocks_with_unknown_parent) continue;

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    auto range = blocks_with_unknown_parent->equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, FlatFilePos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        if (block_store.ReadBlockFromPosition(*pblockrecursive, it->second, {})) {
                            const auto& block_hash{pblockrecursive->GetHash()};
                            LogDebug(BCLog::REINDEX, "%s: Processing out of order child %s of %s", __func__, block_hash.ToString(), head.ToString());
                            LOCK(cs_main);
                            BlockValidationState dummy;
                            if (ChainValidationService{*this}.AcceptBlock(
                                    pblockrecursive,
                                    dummy,
                                    {.block_data_storage = BlockDataStorageMode::ForceStore, .existing_block_pos = &it->second, .header = {.min_pow_checked = true}},
                                    CurrentBlockValidationTime())
                                    .accepted_for_processing()) {
                                nLoaded++;
                                queue.push_back(block_hash);
                            }
                        }
                        range.first++;
                        blocks_with_unknown_parent->erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                // historical bugs added extra data to the block files that does not deserialize cleanly.
                // commonly this data is between readable blocks, but it does not really matter. such data is not fatal to the import process.
                // the code that reads the block files deals with invalid data by simply ignoring it.
                // it continues to search for the next {4 byte magic message start bytes + 4 byte length + block} that does deserialize cleanly
                // and passes all of the other block validation checks dealing with POW and the merkle root, etc...
                // we merely note with this informational log message when unexpected data is encountered.
                // we could also be experiencing a storage system read error, or a read of a previous bad write. these are possible, but
                // less likely scenarios. we don't have enough information to tell a difference here.
                // the reindex process is not the place to attempt to clean and/or compact the block files. if so desired, a studious node operator
                // may use knowledge of the fact that the block files are not entirely pristine in order to prepare a set of pristine, and
                // perhaps ordered, block files for later reindexing.
                LogDebug(BCLog::REINDEX, "%s: unexpected data at file offset 0x%x - %s. continuing\n", __func__, (nRewind - 1), e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        GetNotifications().fatalError(strprintf(_("System error while loading external block file: %s"), e.what()));
    }
    LogInfo("Loaded %i blocks from external file in %dms", nLoaded, Ticks<std::chrono::milliseconds>(SteadyClock::now() - start));
}

bool ChainstateManager::ShouldCheckBlockIndex() const
{
    // Assert to verify Flatten() has been called.
    if (!*Assert(m_options.check_block_index)) return false;
    if (FastRandomContext().randrange(*m_options.check_block_index) >= 1) return false;
    return true;
}

void ChainstateManager::CheckBlockIndex() const
{
    if (!ShouldCheckBlockIndex()) {
        return;
    }

    LOCK(cs_main);
    CoreBlockIndexView block_index_view{*this};
    const auto block_index_snapshot{block_index_view.SnapshotBlockIndices()};

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in the block index but no active chain. (A few of the
    // tests when iterating the block tree require that m_chain has been initialized.)
    if (ActiveChain().Height() < 0) {
        assert(block_index_snapshot.size() <= 1);
        return;
    }

    // Build forward-pointing data structure for the entire block tree.
    // For performance reasons, indexes of the best header chain are stored in a vector (within CChain).
    // All remaining blocks are stored in a multimap.
    // The best header chain can differ from the active chain: E.g. its entries may belong to blocks that
    // are not yet validated.
    CChain best_hdr_chain;
    assert(m_best_header);
    assert(!(m_best_header->nStatus & BLOCK_FAILED_VALID));
    best_hdr_chain.SetTip(*m_best_header);

    std::multimap<const CBlockIndex*, const CBlockIndex*> forward;
    for (const CBlockIndex* block_index : block_index_snapshot) {
        // Only save indexes in forward that are not part of the best header chain.
        if (!best_hdr_chain.Contains(*block_index)) {
            // Only genesis, which must be part of the best header chain, can have a nullptr parent.
            assert(block_index->pprev);
            forward.emplace(block_index->pprev, block_index);
        }
    }
    assert(forward.size() + best_hdr_chain.Height() + 1 == block_index_snapshot.size());

    const CBlockIndex* pindex = best_hdr_chain[0];
    assert(pindex);
    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    const CBlockIndex* pindexFirstInvalid = nullptr;              // Oldest ancestor of pindex which is invalid.
    const CBlockIndex* pindexFirstMissing = nullptr;              // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    const CBlockIndex* pindexFirstNeverProcessed = nullptr;       // Oldest ancestor of pindex for which nTx == 0.
    const CBlockIndex* pindexFirstNotTreeValid = nullptr;         // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    const CBlockIndex* pindexFirstNotTransactionsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    const CBlockIndex* pindexFirstNotChainValid = nullptr;        // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    const CBlockIndex* pindexFirstNotScriptsValid = nullptr;      // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).

    while (pindex != nullptr) {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            pindexFirstMissing = pindex;
        }
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;

        if (pindex->pprev != nullptr) {
            if (pindexFirstNotTransactionsValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) {
                pindexFirstNotTransactionsValid = pindex;
            }

            if (pindexFirstNotChainValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) {
                pindexFirstNotChainValid = pindex;
            }

            if (pindexFirstNotScriptsValid == nullptr &&
                    (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) {
                pindexFirstNotScriptsValid = pindex;
            }
        }

        // Begin: actual consistency checks.
        if (pindex->pprev == nullptr) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == GetConsensus().hashGenesisBlock); // Genesis block's hash must match.
            if (m_chainstate && m_chainstate->m_chain.Genesis() != nullptr) {
                assert(pindex == m_chainstate->m_chain.Genesis()); // The chain's genesis block must be this block.
            }
        }
        // nSequenceId can't be set higher than SEQ_ID_INIT_FROM_DISK{1} for blocks that aren't linked
        // (negative is used for preciousblock, SEQ_ID_BEST_CHAIN_FROM_DISK{0} for active chain when loaded from disk)
        if (!pindex->HaveNumChainTxs()) assert(pindex->nSequenceId <= SEQ_ID_INIT_FROM_DISK);
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!m_blockman.m_have_pruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        // There should only be an nTx value if we have
        // actually seen a block's transactions.
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to HaveNumChainTxs().
        assert((pindexFirstNeverProcessed == nullptr) == pindex->HaveNumChainTxs());
        assert((pindexFirstNotTransactionsValid == nullptr) == pindex->HaveNumChainTxs());
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == nullptr || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == nullptr); // All block index entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == nullptr); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == nullptr); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == nullptr); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == nullptr) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_VALID) == 0); // The failed flag cannot be set for blocks without invalid parents.
        } else {
            assert(pindex->nStatus & BLOCK_FAILED_VALID); // Invalid blocks and their descendants must be marked as invalid
        }
        // Make sure m_chain_tx_count sum is correctly computed.
        if (!pindex->pprev) {
            // If no previous block, nTx and m_chain_tx_count must be the same.
            assert(pindex->m_chain_tx_count == pindex->nTx);
        } else if (pindex->pprev->m_chain_tx_count > 0 && pindex->nTx > 0) {
            // If previous m_chain_tx_count is set and number of transactions in block is known, sum must be set.
            assert(pindex->m_chain_tx_count == pindex->nTx + pindex->pprev->m_chain_tx_count);
        } else {
            // Otherwise m_chain_tx_count must not be set.
            assert(pindex->m_chain_tx_count == 0);
        }
        // There should be no block with more work than m_best_header, unless it's known to be invalid
        assert((pindex->nStatus & BLOCK_FAILED_VALID) || pindex->nChainWork <= m_best_header->nChainWork);

        // Chainstate-specific checks on setBlockIndexCandidates
        if (m_chainstate && m_chainstate->m_chain.Tip() != nullptr) {
            const auto& c{*m_chainstate};
            // Two main factors determine whether pindex is a candidate in
            // setBlockIndexCandidates:
            //
            // - If pindex has less work than the chain tip, it should not be a
            //   candidate, and this will be asserted below. Otherwise it is a
            //   potential candidate.
            //
            // - If pindex or one of its parent blocks back to the genesis
            //   block never downloaded transactions (pindexFirstNeverProcessed
            //   is non-null), it should not be a candidate.
            if (!CBlockIndexWorkComparator()(pindex, c.m_chain.Tip()) && pindexFirstNeverProcessed == nullptr) {
                if (pindexFirstInvalid == nullptr) {
                    // If pindex and all its parents back to the genesis block
                    // downloaded transactions, and the transactions were not
                    // pruned (pindexFirstMissing is null), it is a potential
                    // candidate. If pindex is the chain tip, it also is a
                    // potential candidate.
                    if (pindexFirstMissing == nullptr || pindex == c.m_chain.Tip()) {
                        assert(c.setBlockIndexCandidates.contains(pindex));
                    }
                    // If some parent is missing, then it could be that this block was in
                    // setBlockIndexCandidates but had to be removed because of the missing data.
                    // In this case it must be in m_blocks_unlinked -- see test below.
                }
            } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
                assert(!c.setBlockIndexCandidates.contains(pindex));
            }
        }
        // Check whether this block is in m_blocks_unlinked.
        auto rangeUnlinked{m_blockman.m_blocks_unlinked.equal_range(pindex->pprev)};
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != nullptr && pindexFirstInvalid == nullptr) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in m_blocks_unlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in m_blocks_unlinked if we don't HAVE_DATA
        if (pindexFirstMissing == nullptr) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in m_blocks_unlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == nullptr && pindexFirstMissing != nullptr) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(m_blockman.m_have_pruned);
            // This block may have entered m_blocks_unlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between m_chain and the
            //    tip.
            // So if this block is itself better than any m_chain.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in m_blocks_unlinked.
            if (m_chainstate) {
                const auto& c{*m_chainstate};
                if (!CBlockIndexWorkComparator()(pindex, c.m_chain.Tip()) && !c.setBlockIndexCandidates.contains(pindex)) {
                    if (pindexFirstInvalid == nullptr) {
                        assert(foundInUnlinked);
                    }
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.


        // Try descending into the first subnode. Always process forks first and the best header chain after.
        auto range{forward.equal_range(pindex)};
        if (range.first != range.second) {
            // A subnode not part of the best header chain was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        } else if (best_hdr_chain.Contains(*pindex)) {
            // Descend further into best header chain.
            nHeight++;
            pindex = best_hdr_chain[nHeight];
            if (!pindex) break; // we are finished, since the best header chain is always processed last
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing) pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = nullptr;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            auto rangePar{forward.equal_range(pindexPar)};
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to a sibling not part of the best header chain.
                pindex = rangePar.first->second;
                break;
            } else if (pindexPar == best_hdr_chain[nHeight - 1]) {
                // Move to pindex's sibling on the best-chain, if it has one.
                pindex = best_hdr_chain[nHeight];
                // There will not be a next block if (and only if) parent block is the best header.
                assert((pindex == nullptr) == (pindexPar == best_hdr_chain.Tip()));
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire block index.
    assert(nNodes == forward.size() + best_hdr_chain.Height() + 1);
}

std::string Chainstate::ToString()
{
    AssertLockHeld(::cs_main);
    CBlockIndex* tip = m_chain.Tip();
    return strprintf("Chainstate @ height %d (%s)",
                     tip ? tip->nHeight : -1, tip ? tip->GetBlockHash().ToString() : "null");
}

bool Chainstate::ResizeCoinsCaches(size_t coinstip_size, size_t coinsdb_size)
{
    AssertLockHeld(::cs_main);
    if (coinstip_size == m_coinstip_cache_size_bytes &&
            coinsdb_size == m_coinsdb_cache_size_bytes) {
        // Cache sizes are unchanged, no need to continue.
        return true;
    }
    size_t old_coinstip_size = m_coinstip_cache_size_bytes;
    m_coinstip_cache_size_bytes = coinstip_size;
    m_coinsdb_cache_size_bytes = coinsdb_size;
    CoinsDB().ResizeCache(coinsdb_size);

    LogInfo("[%s] resized coinsdb cache to %.1f MiB",
        this->ToString(), coinsdb_size / double(1_MiB));
    LogInfo("[%s] resized coinstip cache to %.1f MiB",
        this->ToString(), coinstip_size / double(1_MiB));

    BlockValidationState state;
    bool ret;

    if (coinstip_size > old_coinstip_size) {
        // Likely no need to flush if cache sizes have grown.
        ret = FlushStateToDisk(state, FlushStateMode::IF_NEEDED);
    } else {
        // Otherwise, flush state to disk and deallocate the in-memory coins map.
        ret = FlushStateToDisk(state, FlushStateMode::FORCE_FLUSH);
    }
    return ret;
}

double ChainstateManager::GuessVerificationProgress(const CBlockIndex* pindex) const
{
    AssertLockHeld(GetMutex());
    const ChainTxData& data{GetParams().TxData()};
    if (pindex == nullptr) {
        return 0.0;
    }

    if (pindex->m_chain_tx_count == 0) {
        LogDebug(BCLog::VALIDATION, "Block %d has unset m_chain_tx_count. Unable to estimate verification progress.\n", pindex->nHeight);
        return 0.0;
    }

    const int64_t nNow{TicksSinceEpoch<std::chrono::seconds>(NodeClock::now())};
    const auto block_time{
        (Assume(m_best_header) && std::abs(nNow - pindex->GetBlockTime()) <= Ticks<std::chrono::seconds>(2h) &&
         Assume(m_best_header->nHeight >= pindex->nHeight)) ?
            // When the header is known to be recent, switch to a height-based
            // approach. This ensures the returned value is quantized when
            // close to "1.0", because some users expect it to be. This also
            // avoids relying too much on the exact miner-set timestamp, which
            // may be off.
            nNow - (m_best_header->nHeight - pindex->nHeight) * GetConsensus().nPowTargetSpacing :
            pindex->GetBlockTime(),
    };

    double fTxTotal;

    if (pindex->m_chain_tx_count <= data.tx_count) {
        fTxTotal = data.tx_count + (nNow - data.nTime) * data.dTxRate;
    } else {
        fTxTotal = pindex->m_chain_tx_count + (nNow - block_time) * data.dTxRate;
    }

    return std::min<double>(pindex->m_chain_tx_count / fTxTotal, 1.0);
}

Chainstate& ChainstateManager::InitializeChainstate()
{
    AssertLockHeld(::cs_main);
    assert(!m_chainstate);
    m_chainstate = std::make_unique<Chainstate>(m_blockman, *this);
    return *m_chainstate;
}


Chainstate& ChainstateManager::ActiveChainstate() const
{
    LOCK(::cs_main);
    assert(m_chainstate);
    return *m_chainstate;
}

std::optional<validation::ActiveChainTipSnapshot> ChainstateManager::ActiveTipSnapshot() const
    EXCLUSIVE_LOCKS_REQUIRED(!m_active_chain_snapshot_mutex)
{
    LOCK(m_active_chain_snapshot_mutex);
    return m_active_chain_tip_snapshot;
}

void ChainstateManager::UpdateActiveTipSnapshot(const CBlockIndex* tip)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_active_chain_snapshot_mutex)
{
    AssertLockHeld(::cs_main);
    const auto snapshot{MakeActiveChainTipSnapshot(tip)};
    LOCK(m_active_chain_snapshot_mutex);
    m_active_chain_tip_snapshot = snapshot;
}

std::optional<validation::BestHeaderSnapshot> ChainstateManager::BestHeaderSnapshot() const
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    LOCK(m_best_header_snapshot_mutex);
    return m_best_header_snapshot;
}

std::optional<ChainLocatorSnapshot> ChainstateManager::BestHeaderLocatorSnapshot() const
{
    LOCK(::cs_main);
    return BestHeaderLocatorSnapshotLocked();
}

std::optional<ChainLocatorSnapshot> ChainstateManager::BestHeaderLocatorSnapshotLocked() const
{
    AssertLockHeld(::cs_main);
    if (!m_best_header) return std::nullopt;
    return ChainLocatorSnapshot{
        .locator = GetLocator(m_best_header),
        .height = m_best_header->nHeight,
    };
}

std::optional<InitialHeadersSyncSnapshot> ChainstateManager::InitialHeadersSyncSnapshotLocked()
{
    AssertLockHeld(::cs_main);
    if (m_best_header == nullptr) {
        SetBestHeader(ActiveChain().Tip());
    }
    if (!m_best_header) return std::nullopt;

    const CBlockIndex* start{m_best_header};
    if (start->pprev) start = start->pprev;
    return InitialHeadersSyncSnapshot{
        .locator = GetLocator(start),
        .locator_height = start->nHeight,
        .best_header_time = m_best_header->GetBlockTime(),
    };
}

std::optional<HeadersSyncStartSnapshot> ChainstateManager::FindHeadersSyncStart(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return FindHeadersSyncStartLocked(block_hash);
}

std::optional<HeadersSyncStartSnapshot> ChainstateManager::FindHeadersSyncStartLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    return HeadersSyncStartSnapshot{
        .header = block->GetBlockHeader(),
        .height = block->nHeight,
        .chain_work = block->nChainWork,
        .median_time_past = block->GetMedianTimePast(),
        .locator = GetLocator(block),
    };
}

std::optional<validation::ChainBlockSnapshot> ChainstateManager::ActiveChainBlockSnapshot(int height) const
{
    LOCK(::cs_main);
    return MakeChainBlockSnapshot(ActiveChain()[height]);
}

std::optional<ChainWorkBlockSnapshot> ChainstateManager::ActiveTipChainWorkBlockSnapshot() const
{
    const auto snapshot{ActiveTipSnapshot()};
    if (!snapshot) return std::nullopt;
    return ChainWorkBlockSnapshot{
        .hash = snapshot->hash,
        .height = snapshot->height,
        .chain_work = snapshot->chain_work,
    };
}

std::optional<ChainWorkBlockSnapshot> ChainstateManager::ActiveTipChainWorkBlockSnapshotLocked() const
{
    AssertLockHeld(::cs_main);
    return MakeChainWorkBlockSnapshot(ActiveChain().Tip());
}

std::optional<ChainLocatorSnapshot> ChainstateManager::PreviousBlockLocatorSnapshot(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return PreviousBlockLocatorSnapshotLocked(block_hash);
}

std::optional<ChainLocatorSnapshot> ChainstateManager::PreviousBlockLocatorSnapshotLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    const CBlockIndex* previous{block->pprev};
    return ChainLocatorSnapshot{
        .locator = GetLocator(previous),
        .height = previous ? previous->nHeight : -1,
    };
}

std::optional<ChainLocatorSnapshot> ChainstateManager::BlockLocatorSnapshot(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return BlockLocatorSnapshotLocked(block_hash);
}

std::optional<ChainLocatorSnapshot> ChainstateManager::BlockLocatorSnapshotLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    return ChainLocatorSnapshot{
        .locator = GetLocator(block),
        .height = block->nHeight,
    };
}

std::optional<arith_uint256> ChainstateManager::ActiveTipWork() const
{
    const auto snapshot{ActiveTipSnapshot()};
    if (!snapshot) return std::nullopt;
    return snapshot->chain_work;
}

std::optional<arith_uint256> ChainstateManager::ActiveTipWorkLocked() const
{
    AssertLockHeld(::cs_main);
    const CBlockIndex* tip{ActiveChain().Tip()};
    if (!tip) return std::nullopt;
    return tip->nChainWork;
}

std::optional<int> ChainstateManager::ActiveTipHeight() const
{
    const auto snapshot{ActiveTipSnapshot()};
    if (!snapshot) return std::nullopt;
    return snapshot->height;
}

std::optional<int> ChainstateManager::ActiveTipHeightLocked() const
{
    AssertLockHeld(::cs_main);
    const CBlockIndex* tip{ActiveChain().Tip()};
    if (!tip) return std::nullopt;
    return tip->nHeight;
}

std::optional<uint256> ChainstateManager::ActiveTipHash() const
{
    const auto snapshot{ActiveTipSnapshot()};
    if (!snapshot) return std::nullopt;
    return snapshot->hash;
}

std::optional<uint256> ChainstateManager::ActiveTipHashLocked() const
{
    AssertLockHeld(::cs_main);
    const CBlockIndex* tip{ActiveChain().Tip()};
    if (!tip) return std::nullopt;
    return tip->GetBlockHash();
}

std::optional<arith_uint256> ChainstateManager::BlockChainWork(const uint256& block_hash) const
{
    LOCK(::cs_main);
    const auto block{FindChainWorkBlockSnapshotLocked(block_hash)};
    if (!block) return std::nullopt;
    return block->chain_work;
}

std::optional<ChainWorkBlockSnapshot> ChainstateManager::FindChainWorkBlockSnapshot(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return FindChainWorkBlockSnapshotLocked(block_hash);
}

std::optional<ChainWorkBlockSnapshot> ChainstateManager::FindChainWorkBlockSnapshotLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    return MakeChainWorkBlockSnapshot(block_index.LookupBlockIndex(block_hash));
}

std::optional<KnownBlockContext> ChainstateManager::FindKnownBlockContext(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return FindKnownBlockContextLocked(block_hash);
}

std::optional<KnownBlockContext> ChainstateManager::FindKnownBlockContextLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    return KnownBlockContext{
        .chain_work = block->nChainWork,
        .segwit_active_after = DeploymentActiveAfter(block, *this, Consensus::DEPLOYMENT_SEGWIT),
    };
}

bool ChainstateManager::ActiveChainContains(const CBlockIndex& block_index) const
{
    AssertLockHeld(::cs_main);
    return ActiveChain().Contains(block_index);
}

ActiveChainInventory ChainstateManager::FindActiveChainInventory(
    const CBlockLocator& locator,
    const uint256& stop_hash,
    int max_hashes,
    int pruned_block_depth) const
{
    LOCK(::cs_main);
    ActiveChainInventory result;
    if (max_hashes <= 0) return result;

    const CChain& active_chain{ActiveChain()};
    const CBlockIndex* block{ActiveChainstate().FindForkInGlobalIndex(locator)};
    if (block) block = active_chain.Next(*block);
    result.first_height = block ? block->nHeight : -1;

    const CBlockIndex* active_tip{active_chain.Tip()};
    const std::optional<int> active_tip_height{active_tip ? std::optional<int>{active_tip->nHeight} : std::nullopt};
    for (int remaining{max_hashes}; block; block = active_chain.Next(*block)) {
        if (block->GetBlockHash() == stop_hash) {
            result.stop_reason = ActiveChainInventoryStopReason::STOP_HASH;
            result.stop_height = block->nHeight;
            result.stop_hash = block->GetBlockHash();
            break;
        }
        if (m_blockman.IsPruneMode() &&
                (!active_tip_height || !(block->nStatus & BLOCK_HAVE_DATA) ||
                 block->nHeight <= *active_tip_height - pruned_block_depth)) {
            result.stop_reason = ActiveChainInventoryStopReason::PRUNED;
            result.stop_height = block->nHeight;
            result.stop_hash = block->GetBlockHash();
            break;
        }

        result.block_hashes.push_back(block->GetBlockHash());
        if (--remaining <= 0) {
            result.stop_reason = ActiveChainInventoryStopReason::LIMIT;
            result.stop_height = block->nHeight;
            result.stop_hash = block->GetBlockHash();
            result.continuation = block->GetBlockHash();
            break;
        }
    }
    return result;
}

ActiveChainHeaders ChainstateManager::FindActiveChainHeaders(
    const CBlockLocator& locator,
    const uint256& stop_hash,
    int max_headers,
    int64_t stale_relay_age_seconds) const
{
    LOCK(::cs_main);
    ActiveChainHeaders result;
    if (max_headers <= 0) return result;

    CoreBlockIndexView block_index{*this};
    const CChain& active_chain{ActiveChain()};
    const CBlockIndex* block{nullptr};
    if (locator.IsNull()) {
        block = block_index.LookupBlockIndex(stop_hash);
        if (!block) return result;
        const CBlockIndex* best_header{m_best_header};
        result.request_allowed =
            active_chain.Contains(*block) ||
            (block->IsValid(BLOCK_VALID_SCRIPTS) && best_header != nullptr &&
             best_header->GetBlockTime() - block->GetBlockTime() < stale_relay_age_seconds &&
             GetBlockProofEquivalentTime(*best_header, *block, *best_header, GetConsensus()) < stale_relay_age_seconds);
        if (!result.request_allowed) {
            result.found = true;
            return result;
        }
    } else {
        block = ActiveChainstate().FindForkInGlobalIndex(locator);
        if (block) block = active_chain.Next(*block);
    }

    result.found = true;
    result.first_height = block ? block->nHeight : -1;
    int remaining{max_headers};
    for (; block; block = active_chain.Next(*block)) {
        result.headers.emplace_back(block->GetBlockHeader());
        if (--remaining <= 0 || block->GetBlockHash() == stop_hash) break;
    }
    result.best_header_sent = MakeChainWorkBlockSnapshot(block ? block : active_chain.Tip());
    return result;
}

HeaderAnnouncementResponse ChainstateManager::FindHeaderAnnouncement(const HeaderAnnouncementRequest& request) const
{
    LOCK(::cs_main);
    return FindHeaderAnnouncementLocked(request);
}

HeaderAnnouncementResponse ChainstateManager::FindHeaderAnnouncementLocked(const HeaderAnnouncementRequest& request) const
{
    AssertLockHeld(::cs_main);
    HeaderAnnouncementResponse result;
    if (request.block_hashes.empty()) return result;

    CoreBlockIndexView block_index{*this};
    const CChain& active_chain{ActiveChain()};
    const CBlockIndex* peer_best_known_block{request.peer_best_known_block ? block_index.LookupBlockIndex(*request.peer_best_known_block) : nullptr};
    const CBlockIndex* peer_best_header_sent{request.peer_best_header_sent ? block_index.LookupBlockIndex(*request.peer_best_header_sent) : nullptr};

    result.fallback.hash = request.block_hashes.back();
    result.fallback.active_tip_hash = ActiveTipHashLocked();
    if (const CBlockIndex* fallback{block_index.LookupBlockIndex(result.fallback.hash)}) {
        result.fallback.found = true;
        result.fallback.in_active_chain = active_chain.Contains(*fallback);
        result.fallback.peer_has_header = HeaderIsKnownToPeer(*fallback, peer_best_known_block, peer_best_header_sent);
    }

    if (!request.try_headers) {
        result.revert_to_inv = true;
        return result;
    }

    bool found_starting_header{false};
    const CBlockIndex* best_index{nullptr};
    for (const uint256& hash : request.block_hashes) {
        const CBlockIndex* block{block_index.LookupBlockIndex(hash)};
        if (!block || !active_chain.Contains(*block)) {
            result.revert_to_inv = true;
            return result;
        }
        if (best_index != nullptr && block->pprev != best_index) {
            result.revert_to_inv = true;
            return result;
        }

        best_index = block;
        if (found_starting_header) {
            result.headers.emplace_back(block->GetBlockHeader());
        } else if (HeaderIsKnownToPeer(*block, peer_best_known_block, peer_best_header_sent)) {
            continue;
        } else if (block->pprev == nullptr || HeaderIsKnownToPeer(*block->pprev, peer_best_known_block, peer_best_header_sent)) {
            found_starting_header = true;
            result.headers.emplace_back(block->GetBlockHeader());
        } else {
            result.revert_to_inv = true;
            return result;
        }
    }

    result.best_header_sent = MakeChainWorkBlockSnapshot(best_index);
    if (request.include_best_block_pos && best_index && (best_index->nStatus & BLOCK_HAVE_DATA)) {
        result.best_block_pos = FlatFilePos{best_index->nFile, best_index->nDataPos};
    }
    return result;
}

std::optional<BlockTipAnnouncementFacts> ChainstateManager::FindBlockTipAnnouncementFacts(
    const uint256& new_tip_hash,
    const std::optional<uint256>& fork_hash,
    size_t max_announcements) const
{
    LOCK(::cs_main);
    return FindBlockTipAnnouncementFactsLocked(new_tip_hash, fork_hash, max_announcements);
}

std::optional<BlockTipAnnouncementFacts> ChainstateManager::FindBlockTipAnnouncementFactsLocked(
    const uint256& new_tip_hash,
    const std::optional<uint256>& fork_hash,
    size_t max_announcements) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* new_tip{block_index.LookupBlockIndex(new_tip_hash)};
    if (!new_tip) return std::nullopt;

    const CBlockIndex* fork{nullptr};
    if (fork_hash) {
        fork = block_index.LookupBlockIndex(*fork_hash);
        if (!fork) return std::nullopt;
    }

    BlockTipAnnouncementFacts result{
        .height = new_tip->nHeight,
        .block_time = new_tip->GetBlockTime(),
    };
    result.block_hashes.reserve(max_announcements);
    if (max_announcements == 0) return result;

    for (const CBlockIndex* block{new_tip}; block != fork; block = block->pprev) {
        if (!block) return std::nullopt;
        result.block_hashes.push_back(block->GetBlockHash());
        if (result.block_hashes.size() == max_announcements) break;
    }
    std::ranges::reverse(result.block_hashes);

    return result;
}

std::optional<bool> ChainstateManager::KnownBlockIsAncestorOfBestHeaderOrTip(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return KnownBlockIsAncestorOfBestHeaderOrTipLocked(block_hash);
}

std::optional<bool> ChainstateManager::KnownBlockIsAncestorOfBestHeaderOrTipLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    if (m_best_header != nullptr && block == m_best_header->GetAncestor(block->nHeight)) return true;
    return ActiveChain().Contains(*block);
}

bool ChainstateManager::KnownHeaderIsKnownToPeer(
    const uint256& header_hash,
    const std::optional<uint256>& peer_best_known_block,
    const std::optional<uint256>& peer_best_header_sent) const
{
    LOCK(::cs_main);
    return KnownHeaderIsKnownToPeerLocked(header_hash, peer_best_known_block, peer_best_header_sent);
}

bool ChainstateManager::KnownHeaderIsKnownToPeerLocked(
    const uint256& header_hash,
    const std::optional<uint256>& peer_best_known_block,
    const std::optional<uint256>& peer_best_header_sent) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* header{block_index.LookupBlockIndex(header_hash)};
    if (!header) return false;
    const CBlockIndex* best_known_block{peer_best_known_block ? block_index.LookupBlockIndex(*peer_best_known_block) : nullptr};
    const CBlockIndex* best_header_sent{peer_best_header_sent ? block_index.LookupBlockIndex(*peer_best_header_sent) : nullptr};
    return HeaderIsKnownToPeer(*header, best_known_block, best_header_sent);
}

BlockDownloadCandidates ChainstateManager::FindBlockDownloadCandidatesLocked(
    const uint256& best_known_hash,
    const std::optional<uint256>& last_common_hash,
    int download_window) const
{
    AssertLockHeld(::cs_main);
    BlockDownloadCandidates result;
    if (download_window < 0) return result;

    CoreBlockIndexView block_index{*this};
    const CBlockIndex* best_known{block_index.LookupBlockIndex(best_known_hash)};
    const CBlockIndex* active_tip{ActiveChain().Tip()};
    if (!best_known || !active_tip) return result;
    result.found = true;
    result.best_known_height = best_known->nHeight;

    if (best_known->nChainWork < active_tip->nChainWork || best_known->nChainWork < MinimumChainWork()) {
        return result;
    }
    result.interesting = true;

    const CBlockIndex* fork_point{LastCommonAncestor(best_known, active_tip)};
    const CBlockIndex* last_common{last_common_hash ? block_index.LookupBlockIndex(*last_common_hash) : nullptr};
    if (last_common == nullptr ||
        fork_point->nChainWork > last_common->nChainWork ||
        best_known->GetAncestor(last_common->nHeight) != last_common) {
        last_common = fork_point;
    }
    result.last_common = MakeChainWorkBlockSnapshot(last_common);
    if (last_common == best_known) return result;

    result.window_end = last_common->nHeight + download_window;
    const int max_height{std::min<int>(best_known->nHeight, result.window_end + 1)};
    result.candidates.reserve(max_height - last_common->nHeight);
    for (int height{last_common->nHeight + 1}; height <= max_height; ++height) {
        const CBlockIndex* block{best_known->GetAncestor(height)};
        if (!block) break;
        result.candidates.push_back({
            .block = *MakeChainWorkBlockSnapshot(block),
            .valid_tree = block->IsValid(BLOCK_VALID_TREE),
            .segwit_active = DeploymentActiveAt(*block, *this, Consensus::DEPLOYMENT_SEGWIT),
            .has_data = (block->nStatus & BLOCK_HAVE_DATA) != 0,
            .in_active_chain = ActiveChain().Contains(*block),
            .have_num_chain_txs = block->HaveNumChainTxs(),
        });
    }
    return result;
}

HeadersDirectFetchPlan ChainstateManager::FindHeadersDirectFetchBlocksLocked(const HeadersDirectFetchRequest& request) const
{
    AssertLockHeld(::cs_main);
    HeadersDirectFetchPlan result;
    if (request.max_blocks <= 0) return result;

    CoreBlockIndexView block_index{*this};
    const CBlockIndex* last_header{block_index.LookupBlockIndex(request.last_header_hash)};
    const CBlockIndex* active_tip{ActiveChain().Tip()};
    if (!last_header || !active_tip) return result;

    result.found = true;
    result.last_header = *MakeChainWorkBlockSnapshot(last_header);
    result.last_header_parent_valid_chain = last_header->pprev && last_header->pprev->IsValid(BLOCK_VALID_CHAIN);
    if (!last_header->IsValid(BLOCK_VALID_TREE) || active_tip->nChainWork > last_header->nChainWork) {
        return result;
    }
    result.requestable = true;

    const CChain& active_chain{ActiveChain()};
    const CBlockIndex* block{last_header};
    while (block != nullptr &&
           !active_chain.Contains(*block) &&
           result.blocks.size() < static_cast<size_t>(request.max_blocks)) {
        const uint256 block_hash{block->GetBlockHash()};
        if (!(block->nStatus & BLOCK_HAVE_DATA) &&
            !ContainsHash(request.blocks_in_flight, block_hash) &&
            (!DeploymentActiveAt(*block, *this, Consensus::DEPLOYMENT_SEGWIT) || request.can_serve_witnesses)) {
            result.blocks.push_back(*MakeChainWorkBlockSnapshot(block));
        }
        block = block->pprev;
    }

    if (block == nullptr || !active_chain.Contains(*block)) {
        result.large_reorg = true;
        result.blocks.clear();
        return result;
    }

    std::reverse(result.blocks.begin(), result.blocks.end());
    return result;
}

std::optional<CompactBlockDownloadFacts> ChainstateManager::FindCompactBlockDownloadFactsLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;

    const CBlockIndex* active_tip{ActiveChain().Tip()};
    return CompactBlockDownloadFacts{
        .block = *MakeChainWorkBlockSnapshot(block),
        .active_tip_available = active_tip != nullptr,
        .more_work_than_active_tip = active_tip != nullptr && block->nChainWork > active_tip->nChainWork,
        .near_active_tip = active_tip != nullptr && block->nHeight <= active_tip->nHeight + 2,
        .has_block_data = (block->nStatus & BLOCK_HAVE_DATA) != 0,
        .has_block_transactions = block->nTx != 0,
    };
}

bool ChainstateManager::BlockValidTransactionsLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    return block != nullptr && block->IsValid(BLOCK_VALID_TRANSACTIONS);
}

std::optional<PoWValidBlockAnnouncementFacts> ChainstateManager::FindPoWValidBlockAnnouncementFactsLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;
    return PoWValidBlockAnnouncementFacts{
        .block = *MakeChainWorkBlockSnapshot(block),
        .previous_hash = block->pprev ? std::optional<uint256>{block->pprev->GetBlockHash()} : std::nullopt,
        .segwit_active = DeploymentActiveAt(*block, *this, Consensus::DEPLOYMENT_SEGWIT),
    };
}

std::optional<ChainBlockRelayFacts> ChainstateManager::FindBlockRelayFacts(const uint256& block_hash, int64_t stale_relay_age_seconds) const
{
    LOCK(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    if (!block) return std::nullopt;

    const CBlockIndex* best_header{m_best_header};
    const CBlockIndex* active_tip{ActiveChain().Tip()};
    const bool in_active_chain{ActiveChain().Contains(*block)};
    const bool request_allowed{
        in_active_chain ||
        (block->IsValid(BLOCK_VALID_SCRIPTS) && best_header != nullptr &&
         best_header->GetBlockTime() - block->GetBlockTime() < stale_relay_age_seconds &&
         GetBlockProofEquivalentTime(*best_header, *block, *best_header, GetConsensus()) < stale_relay_age_seconds)};

    return ChainBlockRelayFacts{
        .height = block->nHeight,
        .block_time = block->GetBlockTime(),
        .best_header_time = best_header ? std::optional<int64_t>{best_header->GetBlockTime()} : std::nullopt,
        .active_tip_height = active_tip ? std::optional<int>{active_tip->nHeight} : std::nullopt,
        .active_tip_hash = active_tip ? std::optional<uint256>{active_tip->GetBlockHash()} : std::nullopt,
        .block_pos = block->GetBlockPos(),
        .request_allowed = request_allowed,
        .needs_active_chain = block->HaveNumChainTxs() &&
            !block->IsValid(BLOCK_VALID_SCRIPTS) &&
            block->IsValid(BLOCK_VALID_TREE),
        .has_data = (block->nStatus & BLOCK_HAVE_DATA) != 0,
    };
}

ChainstateManager::RawBlockDataReadResult ChainstateManager::ReadRawBlockData(const FlatFilePos& pos)
{
    return m_blockman.ReadRawBlock(pos);
}

bool ChainstateManager::ReadStoredBlock(CBlock& block, const FlatFilePos& pos, const uint256& expected_hash)
{
    CoreBlockDataStore block_store{m_blockman};
    return block_store.ReadBlockFromPosition(block, pos, expected_hash);
}

bool ChainstateManager::HasBlockIndex(const uint256& block_hash) const
{
    LOCK(::cs_main);
    return HasBlockIndexLocked(block_hash);
}

bool ChainstateManager::HasBlockIndexLocked(const uint256& block_hash) const
{
    AssertLockHeld(::cs_main);
    CoreBlockIndexView block_index{*this};
    return block_index.LookupBlockIndex(block_hash) != nullptr;
}

bool ChainstateManager::IsBlockPruned(const uint256& block_hash) const
{
    LOCK(::cs_main);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block{block_index.LookupBlockIndex(block_hash)};
    return block && m_blockman.IsBlockPruned(*block);
}

std::optional<arith_uint256> ChainstateManager::ActiveTipWorkWithBlockProofBuffer(unsigned int block_proof_count) const
{
    const auto snapshot{ActiveTipSnapshot()};
    if (!snapshot) return std::nullopt;
    return snapshot->chain_work - std::min<arith_uint256>(block_proof_count * snapshot->block_proof, snapshot->chain_work);
}

std::optional<arith_uint256> ChainstateManager::ActiveTipWorkWithBlockProofBufferLocked(unsigned int block_proof_count) const
{
    AssertLockHeld(::cs_main);
    const CBlockIndex* tip{ActiveChain().Tip()};
    if (!tip) return std::nullopt;
    return tip->nChainWork - std::min<arith_uint256>(block_proof_count * GetBlockProof(*tip), tip->nChainWork);
}

bool ChainstateManager::HaveActiveChainBlockData(int height) const
{
    LOCK(::cs_main);
    const CBlockIndex* block{ActiveChain()[height]};
    return block && ((block->nStatus & BLOCK_HAVE_DATA) != 0) && block->nTx > 0;
}

bool ChainstateManager::HaveBlocksOnDisk(const uint256& block_hash, int min_height, std::optional<int> max_height) const
{
    LOCK(::cs_main);
    CoreBlockIndexView block_index{*this};
    if (const CBlockIndex* block = block_index.LookupBlockIndex(block_hash)) {
        if (max_height && block->nHeight >= *max_height) block = block->GetAncestor(*max_height);
        for (; block->nStatus & BLOCK_HAVE_DATA; block = block->pprev) {
            if (block->nHeight <= min_height || !block->pprev) return true;
        }
    }
    return false;
}

bool ChainstateManager::HavePruned() const
{
    LOCK(::cs_main);
    return m_blockman.m_have_pruned;
}

std::optional<int> ChainstateManager::PruneHeight() const
{
    LOCK(::cs_main);
    const CChain& chain{ActiveChain()};
    const CBlockIndex* first_block{chain[1]};
    const CBlockIndex* chain_tip{chain.Tip()};
    if (!first_block || !chain_tip) return std::nullopt;
    if ((chain_tip->nStatus & BLOCK_HAVE_MASK) != BLOCK_HAVE_MASK) return chain_tip->nHeight;
    const auto& first_unpruned{m_blockman.GetFirstBlock(*chain_tip, BLOCK_HAVE_MASK, first_block)};
    if (&first_unpruned == first_block) return std::nullopt;
    return CHECK_NONFATAL(first_unpruned.pprev)->nHeight;
}

std::optional<Coin> ChainstateManager::GetUnspentOutput(const COutPoint& output)
{
    LOCK(::cs_main);
    return ActiveChainstate().CoinsTip().GetCoin(output);
}

std::optional<int> ChainstateManager::FindLocatorForkHeight(const CBlockLocator& locator) const
{
    LOCK(::cs_main);
    if (const CBlockIndex* fork = ActiveChainstate().FindForkInGlobalIndex(locator)) {
        return fork->nHeight;
    }
    return std::nullopt;
}

ChainBlockQueryResult ChainstateManager::FindBlock(const uint256& block_hash, const ChainBlockQuery& query) const
{
    WAIT_LOCK(::cs_main, lock);
    CoreBlockIndexView block_index{*this};
    return QueryChainBlock(block_index.LookupBlockIndex(block_hash), query, lock, ActiveChain(), m_blockman);
}

ChainBlockQueryResult ChainstateManager::FindFirstBlockWithTimeAndHeight(int64_t min_time, int min_height, const ChainBlockQuery& query) const
{
    WAIT_LOCK(::cs_main, lock);
    const CChain& active_chain{ActiveChain()};
    return QueryChainBlock(active_chain.FindEarliestAtLeast(min_time, min_height), query, lock, active_chain, m_blockman);
}

ChainBlockQueryResult ChainstateManager::FindAncestorByHeight(const uint256& block_hash, int ancestor_height, const ChainBlockQuery& query) const
{
    WAIT_LOCK(::cs_main, lock);
    const CChain& active_chain{ActiveChain()};
    CoreBlockIndexView block_index{*this};
    if (const CBlockIndex* block = block_index.LookupBlockIndex(block_hash)) {
        if (const CBlockIndex* ancestor = block->GetAncestor(ancestor_height)) {
            return QueryChainBlock(ancestor, query, lock, active_chain, m_blockman);
        }
    }
    return {};
}

ChainBlockQueryResult ChainstateManager::FindAncestorByHash(const uint256& block_hash, const uint256& ancestor_hash, const ChainBlockQuery& query) const
{
    WAIT_LOCK(::cs_main, lock);
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block = block_index.LookupBlockIndex(block_hash);
    const CBlockIndex* ancestor = block_index.LookupBlockIndex(ancestor_hash);
    if (block && ancestor && block->GetAncestor(ancestor->nHeight) != ancestor) ancestor = nullptr;
    return QueryChainBlock(ancestor, query, lock, ActiveChain(), m_blockman);
}

ChainCommonAncestorQueryResult ChainstateManager::FindCommonAncestor(
    const uint256& block_hash1,
    const uint256& block_hash2,
    const ChainBlockQuery& ancestor_query,
    const ChainBlockQuery& block1_query,
    const ChainBlockQuery& block2_query) const
{
    WAIT_LOCK(::cs_main, lock);
    const CChain& active_chain{ActiveChain()};
    CoreBlockIndexView block_index{*this};
    const CBlockIndex* block1 = block_index.LookupBlockIndex(block_hash1);
    const CBlockIndex* block2 = block_index.LookupBlockIndex(block_hash2);
    const CBlockIndex* ancestor = block1 && block2 ? LastCommonAncestor(block1, block2) : nullptr;
    return {
        .ancestor = QueryChainBlock(ancestor, ancestor_query, lock, active_chain, m_blockman),
        .block1 = QueryChainBlock(block1, block1_query, lock, active_chain, m_blockman),
        .block2 = QueryChainBlock(block2, block2_query, lock, active_chain, m_blockman),
    };
}

void ChainstateManager::UpdateBestHeaderSnapshot(const CBlockIndex* header)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    AssertLockHeld(::cs_main);
    const auto snapshot{MakeChainBlockSnapshot(header)};
    LOCK(m_best_header_snapshot_mutex);
    m_best_header_snapshot = snapshot;
}

void ChainstateManager::SetBestHeader(CBlockIndex* header)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    AssertLockHeld(::cs_main);
    m_best_header = header;
    UpdateBestHeaderSnapshot(header);
}

void ChainstateManager::MaybeRebalanceCaches()
{
    AssertLockHeld(::cs_main);
    // With a single chainstate, allocate everything to it.
    ActiveChainstate().ResizeCoinsCaches(m_total_coinstip_cache, m_total_coinsdb_cache);
}

void ChainstateManager::ResetChainstates()
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_active_chain_snapshot_mutex)
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    AssertLockHeld(::cs_main);
    m_chainstate.reset();
    UpdateActiveTipSnapshot(nullptr);
    SetBestHeader(nullptr);
}

/**
 * Apply default chain params to nullopt members.
 * This helps to avoid coding errors around the accidental use of the compare
 * operators that accept nullopt, thus ignoring the intended default value.
 */
static ChainstateManager::Options&& Flatten(ChainstateManager::Options&& opts)
{
    if (!opts.check_block_index.has_value()) opts.check_block_index = opts.chainparams.DefaultConsistencyChecks();
    if (!opts.minimum_chain_work.has_value()) opts.minimum_chain_work = UintToArith256(opts.chainparams.GetConsensus().nMinimumChainWork);
    if (!opts.assumed_valid_block.has_value()) opts.assumed_valid_block = opts.chainparams.DefaultAssumeValid();
    return std::move(opts);
}

ChainstateManager::ChainstateManager(const util::SignalInterrupt& interrupt, Options options, kernel::BlockManager::Options blockman_options)
    : m_script_check_queue{/*batch_size=*/128, std::clamp(options.worker_threads_num, 0, MAX_SCRIPTCHECK_THREADS)},
      m_interrupt{interrupt},
      m_options{Flatten(std::move(options))},
      m_blockman{interrupt, std::move(blockman_options)},
      m_validation_cache{m_options.script_execution_cache_bytes, m_options.signature_cache_bytes}
{
}

ChainstateManager::~ChainstateManager()
{
    LOCK(::cs_main);

    m_versionbitscache.Clear();
}


void ChainstateManager::ReportHeadersPresync(int64_t height, int64_t timestamp)
{
    AssertLockNotHeld(GetMutex());
    {
        LOCK(GetMutex());
        // Don't report headers presync progress if we already have a post-minchainwork header chain.
        // This means we lose reporting for potentially legitimate, but unlikely, deep reorgs, but
        // prevent attackers that spam low-work headers from filling our logs.
        if (m_best_header->nChainWork >= UintToArith256(GetConsensus().nMinimumChainWork)) return;
        // Rate limit headers presync updates to 4 per second, as these are not subject to DoS
        // protection.
        auto now = MockableSteadyClock::now();
        if (now < m_last_presync_update + std::chrono::milliseconds{250}) return;
        m_last_presync_update = now;
    }
    bool initial_download = IsInitialBlockDownload();
    GetNotifications().headerTip(GetSynchronizationState(initial_download, m_blockman.m_blockfiles_indexed), height, timestamp, /*presync=*/true);
    if (initial_download) {
        int64_t blocks_left{(NodeClock::now() - NodeSeconds{std::chrono::seconds{timestamp}}) / GetConsensus().PowTargetSpacing()};
        blocks_left = std::max<int64_t>(0, blocks_left);
        const double progress{100.0 * height / (height + blocks_left)};
        LogInfo("Pre-synchronizing blockheaders, height: %d (~%.2f%%)\n", height, progress);
    }
}

void ChainstateManager::RecalculateBestHeader()
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    EXCLUSIVE_LOCKS_REQUIRED(!m_best_header_snapshot_mutex)
{
    AssertLockHeld(cs_main);
    SetBestHeader(ActiveChain().Tip());
    CoreBlockIndexStore block_index{*this};
    for (CBlockIndex* entry : block_index.SnapshotBlockIndices()) {
        if (!(entry->nStatus & BLOCK_FAILED_VALID) && m_best_header->nChainWork < entry->nChainWork) {
            SetBestHeader(entry);
        }
    }
}

double ChainstateManager::GuessVerificationProgressForActiveTip() const
{
    LOCK(GetMutex());
    return GuessVerificationProgress(ActiveTip());
}

double ChainstateManager::GuessVerificationProgress(const uint256& block_hash) const
{
    LOCK(GetMutex());
    CoreBlockIndexView block_index{*this};
    return GuessVerificationProgress(block_index.LookupBlockIndex(block_hash));
}

std::optional<int> ChainstateManager::BlocksAheadOfTip() const
{
    LOCK(::cs_main);
    const CBlockIndex* best_header{m_best_header};
    const CBlockIndex* tip{ActiveChain().Tip()};
    // Only consider headers that extend the active tip; ignore competing branches.
    if (best_header && tip && best_header->nChainWork > tip->nChainWork &&
        best_header->GetAncestor(tip->nHeight) == tip) {
        return best_header->nHeight - tip->nHeight;
    }
    return std::nullopt;
}

std::pair<int, int> Chainstate::GetPruneRange(int last_height_can_prune) const
{
    if (m_chain.Height() <= 0) {
        return {0, 0};
    }
    int max_prune = std::max<int>(
        0, m_chain.Height() - static_cast<int>(MIN_BLOCKS_TO_KEEP));

    // last block to prune is the lesser of (caller-specified height, MIN_BLOCKS_TO_KEEP from the tip)
    int prune_end = std::min(last_height_can_prune, max_prune);

    return {0, prune_end};
}

util::Result<void> ChainstateManager::ActivateBestChains(ChainstateEventSink* chain_events)
{
    AssertLockNotHeld(cs_main);
    Chainstate* chainstate;
    {
        LOCK(GetMutex());
        if (!m_chainstate) return {};
        chainstate = m_chainstate.get();
    }
    BlockValidationState state;
    if (!chainstate->ActivateBestChain(state, nullptr, chain_events)) {
        LOCK(GetMutex());
        return util::Error{Untranslated(strprintf("%s Failed to connect best block (%s)", chainstate->ToString(), state.ToString()))};
    }
    return {};
}
