// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_EVENT_QUEUE_H
#define BITCOIN_VALIDATION_EVENT_QUEUE_H

#include <arith_uint256.h>
#include <primitives/block.h>
#include <uint256.h>
#include <validation_state.h>

#include <cstdint>
#include <memory>
#include <optional>

class BlockValidationState;
class CBlock;
class ValidationSignals;

namespace validation {

struct ValidationBlockInfo {
    uint256 hash{};
    std::optional<uint256> previous_hash{};
    int height{-1};
    CBlockHeader header{};
    arith_uint256 chain_work{};
    int64_t chain_time_max{0};
    int file_number{-1};
    unsigned int data_pos{0};
};

struct BlockCheckedEvent {
    std::shared_ptr<const CBlock> block;
    BlockValidationState state;
};

struct PoWValidBlockEvent {
    std::shared_ptr<const CBlock> block;
    ValidationBlockInfo block_info;
};

struct BlockConnectedEvent {
    std::shared_ptr<const CBlock> block;
    ValidationBlockInfo block_info;
};

struct BlockDisconnectedEvent {
    std::shared_ptr<const CBlock> block;
    ValidationBlockInfo block_info;
};

struct TipUpdatedEvent {
    ValidationBlockInfo new_tip;
    std::optional<ValidationBlockInfo> fork;
    bool initial_download{false};
};

struct ActiveTipChangedEvent {
    ValidationBlockInfo new_tip;
    bool initial_download{false};
};

struct ChainStateFlushedEvent {
    CBlockLocator locator;
};

class ValidationEventQueue
{
public:
    virtual ~ValidationEventQueue() = default;

    virtual void BlockChecked(BlockCheckedEvent event) = 0;
    virtual void NewPoWValidBlock(PoWValidBlockEvent event) = 0;
    virtual void BlockConnected(BlockConnectedEvent event) = 0;
    virtual void BlockDisconnected(BlockDisconnectedEvent event) = 0;
    virtual void ChainStateFlushed(ChainStateFlushedEvent event) = 0;
    virtual void UpdatedBlockTip(TipUpdatedEvent event) = 0;
    virtual void ActiveTipChange(ActiveTipChangedEvent event) = 0;
};

class CoreValidationEventQueue final : public ValidationEventQueue
{
public:
    explicit CoreValidationEventQueue(ValidationSignals* signals) : m_signals{signals} {}

    void BlockChecked(BlockCheckedEvent event) override;
    void NewPoWValidBlock(PoWValidBlockEvent event) override;
    void BlockConnected(BlockConnectedEvent event) override;
    void BlockDisconnected(BlockDisconnectedEvent event) override;
    void ChainStateFlushed(ChainStateFlushedEvent event) override;
    void UpdatedBlockTip(TipUpdatedEvent event) override;
    void ActiveTipChange(ActiveTipChangedEvent event) override;

private:
    ValidationSignals* m_signals;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_EVENT_QUEUE_H
