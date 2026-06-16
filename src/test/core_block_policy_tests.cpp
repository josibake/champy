// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <kernel/cs_main.h>
#include <sync.h>
#include <uint256.h>
#include <validation/block_index_adapters.h>
#include <validation/core_block_policy.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

class FakeBlockIndexLookup final : public BlockIndexLookup
{
public:
    explicit FakeBlockIndexLookup(CBlockIndex* lookup_result = nullptr)
        : result{lookup_result}
    {
    }

    CBlockIndex* LookupBlockIndex(const uint256& block_hash) override EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        called = true;
        last_lookup = block_hash;
        return result;
    }

    CBlockIndex* result{nullptr};
    bool called{false};
    uint256 last_lookup;
};

std::vector<CBlockIndex> BuildTestChain(int height)
{
    static constexpr uint32_t TEST_BITS{0x207fffff};

    std::vector<CBlockIndex> chain(height + 1);
    for (int i{0}; i <= height; ++i) {
        CBlockIndex& block{chain[i]};
        block.pprev = i > 0 ? &chain[i - 1] : nullptr;
        block.nHeight = i;
        block.nBits = TEST_BITS;
        block.nChainWork = i > 0 ? chain[i - 1].nChainWork + GetBlockProof(chain[i - 1]) : arith_uint256{};
        block.BuildSkip();
    }
    return chain;
}

Consensus::Params TestConsensusParams()
{
    Consensus::Params params{};
    params.nPowTargetSpacing = 600;
    return params;
}

} // namespace

BOOST_AUTO_TEST_SUITE(core_block_policy_tests)

BOOST_AUTO_TEST_CASE(assumevalid_disabled_requires_script_checks_without_lookup)
{
    LOCK(::cs_main);

    FakeBlockIndexLookup block_index;
    CBlockIndex block;

    const CoreBlockScriptCheckDecision decision{DetermineCoreBlockScriptChecks(
        {.assumed_valid_block = uint256{}, .minimum_chain_work = arith_uint256{}},
        block_index,
        block,
        TestConsensusParams())};

    BOOST_CHECK(decision.run_script_checks);
    BOOST_CHECK(std::string_view{decision.reason} == "assumevalid=0 (always verify)");
    BOOST_CHECK(!block_index.called);
}

BOOST_AUTO_TEST_CASE(missing_assumevalid_header_requires_script_checks)
{
    LOCK(::cs_main);

    FakeBlockIndexLookup block_index;
    CBlockIndex block;

    const CoreBlockScriptCheckDecision decision{DetermineCoreBlockScriptChecks(
        {.assumed_valid_block = uint256::ONE, .best_header = &block, .minimum_chain_work = arith_uint256{}},
        block_index,
        block,
        TestConsensusParams())};

    BOOST_CHECK(decision.run_script_checks);
    BOOST_CHECK(std::string_view{decision.reason} == "assumevalid hash not in headers");
    BOOST_CHECK(block_index.called);
    BOOST_CHECK(block_index.last_lookup == uint256::ONE);
}

BOOST_AUTO_TEST_CASE(buried_assumevalid_ancestor_may_skip_script_checks)
{
    LOCK(::cs_main);

    std::vector<CBlockIndex> chain{BuildTestChain(/*height=*/3000)};
    CBlockIndex& block{chain[1]};
    CBlockIndex& buried_tip{chain.back()};
    FakeBlockIndexLookup block_index{&buried_tip};

    const CoreBlockScriptCheckDecision decision{DetermineCoreBlockScriptChecks(
        {.assumed_valid_block = uint256::ONE, .best_header = &buried_tip, .minimum_chain_work = arith_uint256{}},
        block_index,
        block,
        TestConsensusParams())};

    BOOST_CHECK(!decision.run_script_checks);
    BOOST_CHECK(block_index.called);
    BOOST_CHECK(block_index.last_lookup == uint256::ONE);
}

BOOST_AUTO_TEST_SUITE_END()
