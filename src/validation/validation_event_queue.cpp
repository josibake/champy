// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/validation_event_queue.h>

#include <validationinterface.h>

#include <utility>

namespace validation {

void CoreValidationEventQueue::BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state)
{
    if (m_signals) m_signals->BlockChecked(block, state);
}

void CoreValidationEventQueue::NewPoWValidBlock(const CBlockIndex* index, const std::shared_ptr<const CBlock>& block)
{
    if (m_signals) m_signals->NewPoWValidBlock(index, block);
}

void CoreValidationEventQueue::BlockConnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index)
{
    if (m_signals) m_signals->BlockConnected(std::move(block), index);
}

void CoreValidationEventQueue::BlockDisconnected(std::shared_ptr<const CBlock> block, const CBlockIndex* index)
{
    if (m_signals) m_signals->BlockDisconnected(std::move(block), index);
}

void CoreValidationEventQueue::ChainStateFlushed(const CBlockLocator& locator)
{
    if (m_signals) m_signals->ChainStateFlushed(locator);
}

void CoreValidationEventQueue::UpdatedBlockTip(const CBlockIndex* new_tip, const CBlockIndex* fork, bool initial_download)
{
    if (m_signals) m_signals->UpdatedBlockTip(new_tip, fork, initial_download);
}

void CoreValidationEventQueue::ActiveTipChange(const CBlockIndex& new_tip, bool initial_download)
{
    if (m_signals) m_signals->ActiveTipChange(new_tip, initial_download);
}

} // namespace validation
