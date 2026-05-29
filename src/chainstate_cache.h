// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINSTATE_CACHE_H
#define BITCOIN_CHAINSTATE_CACHE_H

#include <cstdint>
#include <cstddef>

struct ExternalCacheUsage
{
    int64_t max_size_bytes{0};
    size_t usage_bytes{0};
};

#endif // BITCOIN_CHAINSTATE_CACHE_H
