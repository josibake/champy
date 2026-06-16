// Copyright (c) 2020-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <validation_state.h>
#include <primitives/block.h>
#include <scheduler.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validationinterface.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(validationinterface_tests, ChainTestingSetup)

struct TestSubscriberNoop final : public CValidationInterface {
    void BlockChecked(const validation::BlockCheckedEvent&) override {}
};

namespace {

CBlock MakeSignalBlock(uint32_t nonce)
{
    CBlock block;
    block.nVersion = 1;
    block.nTime = nonce;
    block.nBits = 0x207fffff;
    block.nNonce = nonce;
    return block;
}

validation::ValidationBlockInfo MakeSignalBlockInfo(const CBlock& block, int height)
{
    return {
        .hash = block.GetHash(),
        .height = height,
        .header = static_cast<const CBlockHeader&>(block),
        .chain_work = arith_uint256{static_cast<uint64_t>(height + 1)},
        .chain_time_max = block.nTime,
        .file_number = height,
        .data_pos = static_cast<unsigned int>(height),
    };
}

class RecordingSubscriber final : public CValidationInterface
{
public:
    std::vector<std::string> calls;
    std::vector<std::shared_ptr<const CBlock>> retained_blocks;
    std::vector<validation::ValidationBlockInfo> retained_infos;

    void BlockChecked(const validation::BlockCheckedEvent& event) override
    {
        AssertLockNotHeld(cs_main);
        calls.push_back("checked");
        retained_blocks.push_back(event.block);
    }

    void NewPoWValidBlock(const validation::PoWValidBlockEvent& event) override
    {
        AssertLockNotHeld(cs_main);
        calls.push_back("pow");
        retained_blocks.push_back(event.block);
        retained_infos.push_back(event.block_info);
    }

    void BlockConnected(const validation::BlockConnectedEvent& event) override
    {
        AssertLockNotHeld(cs_main);
        calls.push_back("connected");
        retained_blocks.push_back(event.block);
        retained_infos.push_back(event.block_info);
    }

    void BlockDisconnected(const validation::BlockDisconnectedEvent& event) override
    {
        AssertLockNotHeld(cs_main);
        calls.push_back("disconnected");
        retained_blocks.push_back(event.block);
        retained_infos.push_back(event.block_info);
    }
};

} // namespace

BOOST_AUTO_TEST_CASE(unregister_validation_interface_race)
{
    std::atomic<bool> generate{true};

    // Start thread to generate notifications
    std::thread gen{[&] {
        BlockValidationState state_dummy;
        while (generate) {
            m_node.validation_signals->BlockChecked({.block = std::make_shared<const CBlock>(), .state = state_dummy});
        }
    }};

    // Start thread to consume notifications
    std::thread sub{[&] {
        // keep going for about 1 sec, which is 250k iterations
        for (int i = 0; i < 250000; i++) {
            auto sub = std::make_shared<TestSubscriberNoop>();
            m_node.validation_signals->RegisterSharedValidationInterface(sub);
            m_node.validation_signals->UnregisterSharedValidationInterface(sub);
        }
        // tell the other thread we are done
        generate = false;
    }};

    gen.join();
    sub.join();
    BOOST_CHECK(!generate);
}

class TestInterface : public CValidationInterface
{
public:
    TestInterface(ValidationSignals& signals, std::function<void()> on_call = nullptr, std::function<void()> on_destroy = nullptr)
        : m_on_call(std::move(on_call)), m_on_destroy(std::move(on_destroy)), m_signals{signals}
    {
    }
    virtual ~TestInterface()
    {
        if (m_on_destroy) m_on_destroy();
    }
    void BlockChecked(const validation::BlockCheckedEvent& event) override
    {
        if (m_on_call) m_on_call();
    }
    void Call()
    {
        BlockValidationState state;
        m_signals.BlockChecked({.block = std::make_shared<const CBlock>(), .state = state});
    }
    std::function<void()> m_on_call;
    std::function<void()> m_on_destroy;
    ValidationSignals& m_signals;
};

// Regression test to ensure UnregisterAllValidationInterfaces calls don't
// destroy a validation interface while it is being called. Bug:
// https://github.com/bitcoin/bitcoin/pull/18551
BOOST_AUTO_TEST_CASE(unregister_all_during_call)
{
    bool destroyed = false;
    auto shared{std::make_shared<TestInterface>(
        *m_node.validation_signals,
        [&] {
            // First call should decrements reference count 2 -> 1
            m_node.validation_signals->UnregisterAllValidationInterfaces();
            BOOST_CHECK(!destroyed);
            // Second call should not decrement reference count 1 -> 0
            m_node.validation_signals->UnregisterAllValidationInterfaces();
            BOOST_CHECK(!destroyed);
        },
        [&] { destroyed = true; })};
    m_node.validation_signals->RegisterSharedValidationInterface(shared);
    BOOST_CHECK(shared.use_count() == 2);
    shared->Call();
    BOOST_CHECK(shared.use_count() == 1);
    BOOST_CHECK(!destroyed);
    shared.reset();
    BOOST_CHECK(destroyed);
}

BOOST_AUTO_TEST_CASE(validation_callbacks_preserve_per_subscriber_order_and_copied_event_values)
{
    auto subscriber_a{std::make_shared<RecordingSubscriber>()};
    auto subscriber_b{std::make_shared<RecordingSubscriber>()};
    m_node.validation_signals->RegisterSharedValidationInterface(subscriber_a);
    m_node.validation_signals->RegisterSharedValidationInterface(subscriber_b);

    BlockValidationState state;
    auto checked_block{std::make_shared<const CBlock>(MakeSignalBlock(1))};
    auto pow_block{std::make_shared<const CBlock>(MakeSignalBlock(2))};
    auto connected_block{std::make_shared<const CBlock>(MakeSignalBlock(3))};
    auto disconnected_block{std::make_shared<const CBlock>(MakeSignalBlock(4))};
    const auto pow_info{MakeSignalBlockInfo(*pow_block, 2)};
    const auto connected_info{MakeSignalBlockInfo(*connected_block, 3)};
    const auto disconnected_info{MakeSignalBlockInfo(*disconnected_block, 4)};

    m_node.validation_signals->BlockChecked({.block = checked_block, .state = state});
    m_node.validation_signals->NewPoWValidBlock({.block = pow_block, .block_info = pow_info});
    WITH_LOCK(cs_main, m_node.validation_signals->BlockConnected({.block = connected_block, .block_info = connected_info}));
    WITH_LOCK(cs_main, m_node.validation_signals->BlockDisconnected({.block = disconnected_block, .block_info = disconnected_info}));
    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    const std::vector<std::string> expected_calls{"checked", "pow", "connected", "disconnected"};
    BOOST_CHECK_EQUAL_COLLECTIONS(subscriber_a->calls.begin(), subscriber_a->calls.end(), expected_calls.begin(), expected_calls.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(subscriber_b->calls.begin(), subscriber_b->calls.end(), expected_calls.begin(), expected_calls.end());

    for (const auto& subscriber : {subscriber_a, subscriber_b}) {
        BOOST_REQUIRE_EQUAL(subscriber->retained_blocks.size(), 4U);
        BOOST_REQUIRE_EQUAL(subscriber->retained_infos.size(), 3U);
        BOOST_CHECK_EQUAL(subscriber->retained_blocks[0]->GetHash().ToString(), checked_block->GetHash().ToString());
        BOOST_CHECK_EQUAL(subscriber->retained_infos[0].hash.ToString(), pow_block->GetHash().ToString());
        BOOST_CHECK_EQUAL(subscriber->retained_infos[1].hash.ToString(), connected_block->GetHash().ToString());
        BOOST_CHECK_EQUAL(subscriber->retained_infos[2].hash.ToString(), disconnected_block->GetHash().ToString());
    }
}

BOOST_AUTO_TEST_SUITE_END()
