// Copyright (c) 2025-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/system.h>
#include <interfaces/mining.h>
#include <node/miner.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <test/util/time.h>
#include <util/time.h>
#include <chainstate.h>

#include <boost/test/unit_test.hpp>

#include <future>
#include <stdexcept>

using interfaces::BlockTemplate;
using interfaces::Mining;
using node::BlockAssembler;
using node::BlockWaitOptions;

namespace testnet4_miner_tests {
template <typename T, typename Start>
T WaitForAsync(Start&& start)
{
    std::promise<T> promise;
    auto future{promise.get_future()};
    auto handler{start([&promise](T result) mutable {
        promise.set_value(std::move(result));
    })};
    (void)handler;
    return future.get();
}

std::unique_ptr<BlockTemplate> CreateNewBlock(Mining& mining, const node::BlockCreateOptions& options = {}, bool cooldown = true)
{
    auto result{WaitForAsync<Mining::CreateBlockResult>([&](Mining::CreateBlockFn done) {
        return mining.createNewBlockAsync(options, cooldown, std::move(done));
    })};
    if (!result) throw std::runtime_error(util::ErrorString(result).original);
    return std::move(*result);
}

std::unique_ptr<BlockTemplate> WaitNext(BlockTemplate& block_template, node::BlockWaitOptions options = {})
{
    return WaitForAsync<std::unique_ptr<BlockTemplate>>([&](BlockTemplate::NextTemplateFn done) {
        return block_template.watchNext(options, std::move(done));
    });
}

struct Testnet4MinerTestingSetup : public Testnet4Setup {
    std::unique_ptr<Mining> MakeMining()
    {
        return interfaces::MakeMining(m_node, /*wait_loaded=*/false);
    }
};
} // namespace testnet4_miner_tests

BOOST_FIXTURE_TEST_SUITE(testnet4_miner_tests, Testnet4MinerTestingSetup)

BOOST_AUTO_TEST_CASE(MiningInterface)
{
    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    BlockAssembler::Options options;
    std::unique_ptr<BlockTemplate> block_template;

    // Set node time a few minutes past the testnet4 genesis block
    const auto template_time{3min + WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->Time())};
    NodeClockContext clock_ctx{template_time};

    block_template = CreateNewBlock(*mining, options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);

    // The template should use the mocked system time
    BOOST_REQUIRE_EQUAL(block_template->getBlockHeader().Time(), template_time);

    const BlockWaitOptions wait_options{.timeout = MillisecondsDouble{0}, .fee_threshold = 1};

    // waitNext() should return nullptr because there is no better template
    auto should_be_nullptr = WaitNext(*block_template, wait_options);
    BOOST_REQUIRE(should_be_nullptr == nullptr);

    // This remains the case when exactly 20 minutes have gone by
    clock_ctx += 17min;
    should_be_nullptr = WaitNext(*block_template, wait_options);
    BOOST_REQUIRE(should_be_nullptr == nullptr);

    // One second later the difficulty drops and it returns a new template
    // Note that we can't test the actual difficulty change, because the
    // difficulty is already at 1.
    clock_ctx += 1s;
    block_template = WaitNext(*block_template, wait_options);
    BOOST_REQUIRE(block_template);
}

BOOST_AUTO_TEST_SUITE_END()
