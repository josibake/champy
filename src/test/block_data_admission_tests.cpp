// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <block_data_admission.h>

#include <arith_uint256.h>

#include <boost/test/unit_test.hpp>

#include <optional>

BOOST_AUTO_TEST_SUITE(block_data_admission_tests)

BOOST_AUTO_TEST_CASE(block_data_admission_result)
{
    constexpr int active_height{100};
    constexpr int max_unrequested_height{active_height + 288};
    const arith_uint256 minimum_chain_work{100};
    const BlockDataAdmissionContext baseline{
        .block_height = active_height + 1,
        .max_unrequested_height = max_unrequested_height,
        .block_chain_work = arith_uint256{201},
        .active_tip_chain_work = arith_uint256{200},
        .minimum_chain_work = minimum_chain_work,
    };
    auto context{baseline};

    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::STORE_BLOCK_DATA);
    BOOST_CHECK(ShouldStoreBlockData(BlockDataAdmissionResult::STORE_BLOCK_DATA));

    context = baseline;
    context.already_have_data = true;
    context.block_data_requested = true;
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::ALREADY_HAVE_DATA);
    BOOST_CHECK(!ShouldStoreBlockData(BlockDataAdmissionResult::ALREADY_HAVE_DATA));

    context = baseline;
    context.block_chain_work = arith_uint256{1};
    context.block_data_requested = true;
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::STORE_BLOCK_DATA);

    context = baseline;
    context.block_data_previously_processed = true;
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::UNREQUESTED_PREVIOUSLY_PROCESSED);

    context = baseline;
    context.block_chain_work = arith_uint256{199};
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::UNREQUESTED_LESS_WORK_THAN_TIP);

    context = baseline;
    context.block_height = max_unrequested_height + 1;
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::UNREQUESTED_TOO_FAR_AHEAD);

    context = baseline;
    context.block_chain_work = arith_uint256{99};
    context.active_tip_chain_work = std::nullopt;
    BOOST_CHECK(GetBlockDataAdmissionResult(context) == BlockDataAdmissionResult::UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK);
}

BOOST_AUTO_TEST_SUITE_END()
