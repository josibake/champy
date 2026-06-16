// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>
#include <test/util/time.h>
#include <chainstate.h>
#include <validation/runtime_time.h>
#include <validationinterface.h>

#include <boost/test/unit_test.hpp>

#include <optional>

// Taken from validation.cpp
static constexpr auto DATABASE_WRITE_INTERVAL_MIN{50min};
static constexpr auto DATABASE_WRITE_INTERVAL_MAX{70min};

BOOST_AUTO_TEST_SUITE(chainstate_write_tests)

BOOST_FIXTURE_TEST_CASE(chainstate_write_interval, TestingSetup)
{
    struct TestSubscriber final : CValidationInterface {
        bool m_did_flush{false};
        void ChainStateFlushed(const validation::ChainStateFlushedEvent&) override
        {
            m_did_flush = true;
        }
    };

    const auto sub{std::make_shared<TestSubscriber>()};
    m_node.validation_signals->RegisterSharedValidationInterface(sub);
    auto& chainstate{Assert(m_node.chainman)->ActiveChainstate()};
    BlockValidationState state_dummy{};
    NodeClockContext clock_ctx{};

    // The first periodic flush sets m_next_write and does not flush
    chainstate.FlushStateToDisk(state_dummy, FlushStateMode::PERIODIC);
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    BOOST_CHECK(!sub->m_did_flush);

    // The periodic flush interval is between 50 and 70 minutes (inclusive)
    clock_ctx += DATABASE_WRITE_INTERVAL_MIN - 1min;
    chainstate.FlushStateToDisk(state_dummy, FlushStateMode::PERIODIC);
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    BOOST_CHECK(!sub->m_did_flush);

    clock_ctx += DATABASE_WRITE_INTERVAL_MAX;
    chainstate.FlushStateToDisk(state_dummy, FlushStateMode::PERIODIC);
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    BOOST_CHECK(sub->m_did_flush);
}

// Test that we do PERIODIC flushes inside ActivateBestChain.
// This is necessary for reindex-chainstate to be able to periodically flush
// before reaching chain tip.
BOOST_FIXTURE_TEST_CASE(write_during_multiblock_activation, TestChain100Setup)
{
    struct TestSubscriber final : CValidationInterface
    {
        std::optional<uint256> m_tip;
        std::optional<uint256> m_flushed_at_block;
        void ChainStateFlushed(const validation::ChainStateFlushedEvent&) override
        {
            m_flushed_at_block = m_tip;
        }
        void UpdatedBlockTip(const validation::TipUpdatedEvent& event) override {
            m_tip = event.new_tip.hash;
        }
    };

    auto& chainstate{Assert(m_node.chainman)->ActiveChainstate()};
    BlockValidationState state_dummy{};

    // Pop two blocks from the tip
    const CBlockIndex* tip{chainstate.m_chain.Tip()};
    CBlockIndex* second_from_tip{tip->pprev};
    const uint256 second_from_tip_hash{second_from_tip->GetBlockHash()};

    {
        LOCK(m_node.chainman->GetMutex());
        chainstate.DisconnectTip(state_dummy, CurrentNodeTime(), nullptr);
        chainstate.DisconnectTip(state_dummy, CurrentNodeTime(), nullptr);
    }

    BOOST_CHECK_EQUAL(second_from_tip->pprev, chainstate.m_chain.Tip());

    // Set m_next_write to current time
    chainstate.FlushStateToDisk(state_dummy, FlushStateMode::FORCE_FLUSH);
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    // The periodic flush interval is between 50 and 70 minutes (inclusive)
    // The next call to a PERIODIC write will flush
    SetMockTime(GetMockTime() + DATABASE_WRITE_INTERVAL_MAX);

    const auto sub{std::make_shared<TestSubscriber>()};
    m_node.validation_signals->RegisterSharedValidationInterface(sub);

    // ActivateBestChain back to tip
    chainstate.ActivateBestChain(state_dummy, CurrentNodeTime(), nullptr);
    BOOST_CHECK_EQUAL(tip, chainstate.m_chain.Tip());
    // Check that we flushed inside ActivateBestChain while we were at the
    // second block from tip, since FlushStateToDisk is called with PERIODIC
    // inside the outer loop.
    m_node.validation_signals->SyncWithValidationInterfaceQueue();
    BOOST_REQUIRE(sub->m_flushed_at_block.has_value());
    BOOST_CHECK(sub->m_flushed_at_block.value() == second_from_tip_hash);
}

BOOST_AUTO_TEST_SUITE_END()
