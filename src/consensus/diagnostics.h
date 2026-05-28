// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_DIAGNOSTICS_H
#define BITCOIN_CONSENSUS_DIAGNOSTICS_H

namespace Consensus {

enum class BlockConsensusIssue {
    Consensus,
    InvalidHeader,
    Mutated,
    TimeFuture,
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_DIAGNOSTICS_H
