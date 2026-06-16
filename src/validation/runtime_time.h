// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_RUNTIME_TIME_H
#define BITCOIN_VALIDATION_RUNTIME_TIME_H

#include <util/time.h>
#include <validation/block_validation.h>

[[nodiscard]] NodeSeconds CurrentNodeTime();
[[nodiscard]] BlockValidationTime CurrentBlockValidationTime();

#endif // BITCOIN_VALIDATION_RUNTIME_TIME_H
