// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_VALIDATION_RESULT_H
#define BITCOIN_VALIDATION_VALIDATION_RESULT_H

#include <consensus/block_spend.h>
#include <consensus/expected.h>

#include <optional>
#include <string>
#include <utility>

namespace validation {

enum class ValidationExecutionErrorKind {
    ConsensusInvalid,
    StaleWork,
    ResourceLimit,
    BackendUnavailable,
    Cancelled,
    CommitConflict,
    SystemError,
};

struct ValidationExecutionError {
    ValidationExecutionErrorKind kind{ValidationExecutionErrorKind::SystemError};
    std::optional<Consensus::ValidationRuntimeIssue> runtime_issue{};
    std::string reject_reason;
    std::string debug_message;
    std::optional<Consensus::BlockSpendError> consensus_error{};
};

template <typename T>
using ValidationResult = Consensus::Expected<T, ValidationExecutionError>;

[[nodiscard]] ValidationExecutionError ClassifyValidationExecutionError(
    Consensus::BlockSpendError error);
[[nodiscard]] Consensus::ValidationRuntimeIssue ToValidationRuntimeIssue(
    ValidationExecutionErrorKind kind) noexcept;
[[nodiscard]] Consensus::BlockSpendError ToBlockSpendError(
    const ValidationExecutionError& error);

template <typename T>
[[nodiscard]] ValidationResult<T> ToValidationResult(
    Consensus::BlockSpendResult<T> result)
{
    if (result) {
        return std::move(result).value();
    }
    return Consensus::Unexpected<ValidationExecutionError>{
        ClassifyValidationExecutionError(std::move(result).error())};
}

} // namespace validation

#endif // BITCOIN_VALIDATION_VALIDATION_RESULT_H
