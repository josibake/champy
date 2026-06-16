// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/validation_event_queue.h>

#include <validationinterface.h>

#include <utility>

namespace validation {

void CoreValidationEventQueue::BlockChecked(BlockCheckedEvent event)
{
    if (m_signals) m_signals->BlockChecked(std::move(event));
}

void CoreValidationEventQueue::NewPoWValidBlock(PoWValidBlockEvent event)
{
    if (m_signals) m_signals->NewPoWValidBlock(std::move(event));
}

void CoreValidationEventQueue::BlockConnected(BlockConnectedEvent event)
{
    if (m_signals) m_signals->BlockConnected(std::move(event));
}

void CoreValidationEventQueue::BlockDisconnected(BlockDisconnectedEvent event)
{
    if (m_signals) m_signals->BlockDisconnected(std::move(event));
}

void CoreValidationEventQueue::ChainStateFlushed(ChainStateFlushedEvent event)
{
    if (m_signals) m_signals->ChainStateFlushed(std::move(event));
}

void CoreValidationEventQueue::UpdatedBlockTip(TipUpdatedEvent event)
{
    if (m_signals) m_signals->UpdatedBlockTip(std::move(event));
}

void CoreValidationEventQueue::ActiveTipChange(ActiveTipChangedEvent event)
{
    if (m_signals) m_signals->ActiveTipChange(std::move(event));
}

} // namespace validation
