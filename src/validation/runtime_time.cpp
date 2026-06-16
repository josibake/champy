// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/runtime_time.h>

NodeSeconds CurrentNodeTime()
{
    return Now<NodeSeconds>();
}

BlockValidationTime CurrentBlockValidationTime()
{
    return BlockValidationTime::FromCurrentTime(CurrentNodeTime());
}
