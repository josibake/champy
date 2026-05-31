// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINSTATE_EVENT_SINK_H
#define BITCOIN_CHAINSTATE_EVENT_SINK_H

#include <chainstate_cache.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

enum class ChainstateEventType {
    TransactionsUpdated,
    BlockDisconnected,
    BlockConnected,
    ReorgCompleted,
    CheckPostReorgState,
};

struct ChainstateEvent {
    ChainstateEventType type{ChainstateEventType::TransactionsUpdated};
    const CBlock* block{nullptr};
    std::shared_ptr<const CBlock> owned_block;
    unsigned int block_height{0};
    bool restore_disconnected_transactions{false};
    int64_t spend_height{0};

    [[nodiscard]] const CBlock& Block() const
    {
        assert(block);
        return *block;
    }
};

class ChainstateEventBatch
{
public:
    ChainstateEventBatch() = default;
    ChainstateEventBatch(const ChainstateEventBatch& other) { Append(other); }
    ChainstateEventBatch& operator=(const ChainstateEventBatch& other)
    {
        if (this == &other) return *this;
        Clear();
        Append(other);
        return *this;
    }
    ChainstateEventBatch(ChainstateEventBatch&&) noexcept = default;
    ChainstateEventBatch& operator=(ChainstateEventBatch&&) noexcept = default;

    [[nodiscard]] bool Empty() const noexcept { return m_events.empty(); }
    [[nodiscard]] std::span<const ChainstateEvent> Events() const noexcept { return m_events; }

    void Append(const ChainstateEventBatch& batch)
    {
        for (const ChainstateEvent& event : batch.m_events) {
            ChainstateEvent copy{event};
            if (copy.block && !copy.owned_block) {
                copy.owned_block = std::make_shared<CBlock>(*copy.block);
                copy.block = copy.owned_block.get();
            }
            m_events.push_back(std::move(copy));
        }
    }

    void TransactionsUpdated()
    {
        ChainstateEvent event;
        event.type = ChainstateEventType::TransactionsUpdated;
        m_events.push_back(std::move(event));
    }

    void CheckPostReorgState(int64_t spend_height)
    {
        ChainstateEvent event;
        event.type = ChainstateEventType::CheckPostReorgState;
        event.spend_height = spend_height;
        m_events.push_back(std::move(event));
    }

    void BlockDisconnected(const CBlock& block)
    {
        ChainstateEvent event;
        event.type = ChainstateEventType::BlockDisconnected;
        event.block = &block;
        m_events.push_back(std::move(event));
    }

    void BlockConnected(const CBlock& block, unsigned int block_height)
    {
        ChainstateEvent event;
        event.type = ChainstateEventType::BlockConnected;
        event.block = &block;
        event.block_height = block_height;
        m_events.push_back(std::move(event));
    }

    void ReorgCompleted(bool restore_disconnected_transactions)
    {
        ChainstateEvent event;
        event.type = ChainstateEventType::ReorgCompleted;
        event.restore_disconnected_transactions = restore_disconnected_transactions;
        m_events.push_back(std::move(event));
    }

    void Clear() noexcept { m_events.clear(); }

private:
    std::vector<ChainstateEvent> m_events;
};

/**
 * Optional side-effect boundary for node-owned state that tracks chainstate
 * transitions.
 *
 * Validation records value events and publishes them after the corresponding
 * chainstate mutation. Event sinks own their own synchronization; validation
 * must not acquire node-owned locks such as `CTxMemPool::cs`.
 */
class ChainstateEventSink
{
public:
    virtual ~ChainstateEventSink() = default;

    virtual ExternalCacheUsage CacheUsage() const = 0;
    virtual void ProcessEvents(const ChainstateEventBatch& events) EXCLUSIVE_LOCKS_REQUIRED(cs_main) = 0;

    void TransactionsUpdated() EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        ChainstateEventBatch batch;
        batch.TransactionsUpdated();
        ProcessEvents(batch);
    }

    void CheckPostReorgState(int64_t spend_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        ChainstateEventBatch batch;
        batch.CheckPostReorgState(spend_height);
        ProcessEvents(batch);
    }

    void BlockDisconnected(const CBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        ChainstateEventBatch batch;
        batch.BlockDisconnected(block);
        ProcessEvents(batch);
    }

    void BlockConnected(const CBlock& block, unsigned int block_height) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        ChainstateEventBatch batch;
        batch.BlockConnected(block, block_height);
        ProcessEvents(batch);
    }

    void ReorgCompleted(bool restore_disconnected_transactions) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        ChainstateEventBatch batch;
        batch.ReorgCompleted(restore_disconnected_transactions);
        ProcessEvents(batch);
    }
};

class ChainstateEventRecorder final : public ChainstateEventSink
{
public:
    ChainstateEventRecorder() = default;
    explicit ChainstateEventRecorder(ExternalCacheUsage cache_usage) : m_cache_usage{cache_usage} {}

    ExternalCacheUsage CacheUsage() const override { return m_cache_usage; }

    void ProcessEvents(const ChainstateEventBatch& events) override EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        m_events.Append(events);
    }

    [[nodiscard]] const ChainstateEventBatch& Events() const noexcept { return m_events; }
    void Clear() noexcept { m_events.Clear(); }

private:
    ExternalCacheUsage m_cache_usage;
    ChainstateEventBatch m_events;
};

#endif // BITCOIN_CHAINSTATE_EVENT_SINK_H
