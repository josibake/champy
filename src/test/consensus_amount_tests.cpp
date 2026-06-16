// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <consensus/params.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(consensus_amount_tests)

BOOST_AUTO_TEST_CASE(block_subsidy_halvings)
{
    Consensus::Params params{};
    params.nSubsidyHalvingInterval = 210000;

    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(0, params), 50 * COIN);
    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(209999, params), 50 * COIN);
    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(210000, params), 25 * COIN);
    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(420000, params), 1250000000);
    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(63 * params.nSubsidyHalvingInterval, params), 0);
    BOOST_CHECK_EQUAL(Consensus::CalculateBlockSubsidy(64 * params.nSubsidyHalvingInterval, params), 0);
}

BOOST_AUTO_TEST_SUITE_END()
