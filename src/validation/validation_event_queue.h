// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_EVENT_QUEUE_H
#define BITCOIN_VALIDATION_EVENT_QUEUE_H

#include <kernel/cs_main.h>

#include <memory>

class BlockValidationState;
class CBlock;
class CBlockIndex;
struct CBlockLocator;
class ValidationSignals;

namespace validation {

class ValidationEventQueue
{
public:
    virtual ~ValidationEventQueue() = default;

    virtual void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state) = 0;
    virtual void NewPoWValidBlock(const CBlockIndex* index, const std::shared_ptr<const CBlock>& block) = 0;
    virtual void BlockConnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index) = 0;
    virtual void BlockDisconnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index) = 0;
    virtual void ChainStateFlushed(const CBlockLocator& locator) = 0;
    virtual void UpdatedBlockTip(const CBlockIndex* new_tip, const CBlockIndex* fork, bool initial_download) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) = 0;
    virtual void ActiveTipChange(const CBlockIndex& new_tip, bool initial_download) = 0;
};

class CoreValidationEventQueue final : public ValidationEventQueue
{
public:
    explicit CoreValidationEventQueue(ValidationSignals* signals) : m_signals{signals} {}

    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state) override;
    void NewPoWValidBlock(const CBlockIndex* index, const std::shared_ptr<const CBlock>& block) override;
    void BlockConnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index) override;
    void BlockDisconnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index) override;
    void ChainStateFlushed(const CBlockLocator& locator) override;
    void UpdatedBlockTip(const CBlockIndex* new_tip, const CBlockIndex* fork, bool initial_download) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void ActiveTipChange(const CBlockIndex& new_tip, bool initial_download) override;

private:
    ValidationSignals* m_signals;
};

} // namespace validation

#endif // BITCOIN_VALIDATION_EVENT_QUEUE_H
