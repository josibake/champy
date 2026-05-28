// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tx_check_adapters.h>

#include <consensus/tx_check.h>
#include <validation_state.h>

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    const auto check{Consensus::CheckTransaction(tx)};
    if (!check) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, check.error().reject_reason, check.error().debug_message);
    }
    return true;
}
