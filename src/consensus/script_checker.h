// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_SCRIPT_CHECKER_H
#define BITCOIN_CONSENSUS_SCRIPT_CHECKER_H

#include <consensus/block_spend.h>

namespace Consensus {

class DirectBlockScriptChecker final : public BlockScriptChecker {
public:
    [[nodiscard]] BlockSpendResult<void> Check(const TransactionScriptCheckPlan& check) override;
    [[nodiscard]] BlockSpendResult<void> Complete() override;
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_SCRIPT_CHECKER_H
