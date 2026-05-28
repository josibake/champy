// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/locktime.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

namespace {

CTransaction TxWithLocktime(uint32_t locktime, uint32_t sequence)
{
    CMutableTransaction tx;
    tx.nLockTime = locktime;
    tx.vin.resize(1);
    tx.vin[0].nSequence = sequence;
    tx.vout.resize(1);
    return CTransaction{tx};
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_locktime_tests)

BOOST_AUTO_TEST_CASE(zero_locktime_is_final)
{
    BOOST_CHECK(IsFinalTx(TxWithLocktime(/*locktime=*/0, /*sequence=*/0), /*nBlockHeight=*/0, /*nBlockTime=*/0));
}

BOOST_AUTO_TEST_CASE(height_locktime_uses_block_height_cutoff)
{
    const CTransaction tx{TxWithLocktime(100, /*sequence=*/0)};

    BOOST_CHECK(!IsFinalTx(tx, /*nBlockHeight=*/100, /*nBlockTime=*/LOCKTIME_THRESHOLD + 1000));
    BOOST_CHECK(IsFinalTx(tx, /*nBlockHeight=*/101, /*nBlockTime=*/0));
}

BOOST_AUTO_TEST_CASE(time_locktime_uses_block_time_cutoff)
{
    const CTransaction tx{TxWithLocktime(LOCKTIME_THRESHOLD + 100, /*sequence=*/0)};

    BOOST_CHECK(!IsFinalTx(tx, /*nBlockHeight=*/LOCKTIME_THRESHOLD + 1000, /*nBlockTime=*/LOCKTIME_THRESHOLD + 100));
    BOOST_CHECK(IsFinalTx(tx, /*nBlockHeight=*/0, /*nBlockTime=*/LOCKTIME_THRESHOLD + 101));
}

BOOST_AUTO_TEST_CASE(final_sequence_overrides_unsatisfied_locktime)
{
    const CTransaction tx{TxWithLocktime(100, CTxIn::SEQUENCE_FINAL)};

    BOOST_CHECK(IsFinalTx(tx, /*nBlockHeight=*/0, /*nBlockTime=*/0));
}

BOOST_AUTO_TEST_SUITE_END()
