// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCK_VALIDATION_ERROR_H
#define BITCOIN_BLOCK_VALIDATION_ERROR_H

#include <validation/block_validation_result.h>

class BlockValidationState;
struct bilingual_str;

namespace kernel {
class Notifications;
} // namespace kernel

namespace Consensus {
enum class BlockConsensusIssue;
struct BlockCheckError;
struct BlockCommitError;
struct BlockConsensusStageError;
struct BlockSpendError;
} // namespace Consensus

[[nodiscard]] BlockValidationResult ToBlockValidationResult(Consensus::BlockConsensusIssue issue);
bool ApplyBlockCheckError(BlockValidationState& state, const Consensus::BlockCheckError& error);
bool ApplyBlockSpendError(BlockValidationState& state, const Consensus::BlockSpendError& error);
bool ApplyBlockCommitError(BlockValidationState& state, const Consensus::BlockCommitError& error);
bool ApplyBlockConsensusStageError(BlockValidationState& state, const Consensus::BlockConsensusStageError& error);
bool FatalError(kernel::Notifications& notifications, BlockValidationState& state, const bilingual_str& message);

#endif // BITCOIN_BLOCK_VALIDATION_ERROR_H
