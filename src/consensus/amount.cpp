// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>

#include <consensus/params.h>

namespace Consensus {

CAmount CalculateBlockSubsidy(int height, const Params& params)
{
    const int halvings{height / params.nSubsidyHalvingInterval};
    if (halvings >= 64) return 0;

    CAmount subsidy{50 * COIN};
    subsidy >>= halvings;
    return subsidy;
}

} // namespace Consensus
