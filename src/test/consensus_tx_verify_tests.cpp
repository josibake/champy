// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/sequence_locks.h>
#include <validation/sequence_locks_adapters.h>

#include <chain.h>
#include <consensus/consensus.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

#include <vector>

BOOST_AUTO_TEST_SUITE(consensus_tx_verify_tests)

BOOST_AUTO_TEST_CASE(sequence_locks_evaluate_against_explicit_block_context)
{
    BOOST_CHECK(Consensus::EvaluateSequenceLocksAtBlock(/*block_height=*/100, /*previous_median_time_past=*/1000, /*lock_pair=*/{-1, -1}));
    BOOST_CHECK(Consensus::EvaluateSequenceLocksAtBlock(/*block_height=*/100, /*previous_median_time_past=*/1000, /*lock_pair=*/{99, 999}));

    BOOST_CHECK(!Consensus::EvaluateSequenceLocksAtBlock(/*block_height=*/100, /*previous_median_time_past=*/1000, /*lock_pair=*/{100, 999}));
    BOOST_CHECK(!Consensus::EvaluateSequenceLocksAtBlock(/*block_height=*/100, /*previous_median_time_past=*/1000, /*lock_pair=*/{99, 1000}));
}

BOOST_AUTO_TEST_CASE(sequence_lock_calculation_accepts_const_previous_heights)
{
    CMutableTransaction mutable_tx;
    mutable_tx.version = 2;
    mutable_tx.vin.resize(1);
    mutable_tx.vin[0].nSequence = 2;
    const CTransaction tx{mutable_tx};

    CBlockIndex block;
    block.nHeight = 10;

    const std::vector<Consensus::SequenceLockInputContext> inputs{{
        .height = 5,
        .previous_median_time_past = 0,
    }};
    const Consensus::SequenceLockContext context{
        .block_height = 10,
        .previous_median_time_past = 0,
        .inputs = inputs,
    };
    const auto lock_pair{Consensus::CalculateSequenceLocks(tx, LOCKTIME_VERIFY_SEQUENCE, context)};
    BOOST_CHECK_EQUAL(lock_pair.first, 6);
    BOOST_CHECK_EQUAL(lock_pair.second, -1);
}

BOOST_AUTO_TEST_CASE(sequence_lock_context_carries_input_median_time)
{
    CMutableTransaction mutable_tx;
    mutable_tx.version = 2;
    mutable_tx.vin.resize(1);
    mutable_tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    const CTransaction tx{mutable_tx};

    const std::vector<Consensus::SequenceLockInputContext> inputs{{
        .height = 5,
        .previous_median_time_past = 1000,
    }};
    const Consensus::SequenceLockContext context{
        .block_height = 10,
        .previous_median_time_past = 1512,
        .inputs = inputs,
    };

    const auto lock_pair{Consensus::CalculateSequenceLocks(tx, LOCKTIME_VERIFY_SEQUENCE, context)};
    BOOST_CHECK_EQUAL(lock_pair.first, -1);
    BOOST_CHECK_EQUAL(lock_pair.second, 1511);
    BOOST_CHECK(Consensus::EvaluateSequenceLocks(context, lock_pair));
    BOOST_CHECK(!Consensus::EvaluateSequenceLocksAtBlock(/*block_height=*/10, /*previous_median_time_past=*/1511, lock_pair));
}

BOOST_AUTO_TEST_CASE(sequence_lock_adapter_does_not_mutate_prev_heights)
{
    CMutableTransaction mutable_tx;
    mutable_tx.version = 2;
    mutable_tx.vin.resize(1);
    mutable_tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    const CTransaction tx{mutable_tx};

    CBlockIndex block;
    block.nHeight = 10;

    std::vector<int> previous_heights{5};
    const auto lock_pair{Consensus::CalculateSequenceLocks(tx, LOCKTIME_VERIFY_SEQUENCE, previous_heights, block)};
    BOOST_CHECK_EQUAL(lock_pair.first, -1);
    BOOST_CHECK_EQUAL(lock_pair.second, -1);
    BOOST_CHECK_EQUAL(previous_heights[0], 5);
}

BOOST_AUTO_TEST_SUITE_END()
