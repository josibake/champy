// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation/validation_result.h>

#include <consensus/diagnostics.h>

#include <optional>
#include <string>
#include <utility>

namespace validation {
namespace {

[[nodiscard]] bool IsConsensusIssue(Consensus::BlockConsensusIssue issue) noexcept
{
    switch (issue) {
    case Consensus::BlockConsensusIssue::Consensus:
    case Consensus::BlockConsensusIssue::InvalidHeader:
    case Consensus::BlockConsensusIssue::Mutated:
    case Consensus::BlockConsensusIssue::TimeFuture:
        return true;
    case Consensus::BlockConsensusIssue::ValidationRuntime:
        return false;
    }
    return false;
}

[[nodiscard]] ValidationExecutionErrorKind RuntimeErrorKind(Consensus::ValidationRuntimeIssue issue) noexcept
{
    switch (issue) {
    case Consensus::ValidationRuntimeIssue::StaleWork:
        return ValidationExecutionErrorKind::StaleWork;
    case Consensus::ValidationRuntimeIssue::ResourceLimit:
        return ValidationExecutionErrorKind::ResourceLimit;
    case Consensus::ValidationRuntimeIssue::BackendUnavailable:
        return ValidationExecutionErrorKind::BackendUnavailable;
    case Consensus::ValidationRuntimeIssue::Cancelled:
        return ValidationExecutionErrorKind::Cancelled;
    case Consensus::ValidationRuntimeIssue::CommitConflict:
        return ValidationExecutionErrorKind::CommitConflict;
    case Consensus::ValidationRuntimeIssue::SystemError:
        return ValidationExecutionErrorKind::SystemError;
    }
    return ValidationExecutionErrorKind::SystemError;
}

} // namespace

Consensus::ValidationRuntimeIssue ToValidationRuntimeIssue(
    ValidationExecutionErrorKind kind) noexcept
{
    switch (kind) {
    case ValidationExecutionErrorKind::StaleWork:
        return Consensus::ValidationRuntimeIssue::StaleWork;
    case ValidationExecutionErrorKind::ResourceLimit:
        return Consensus::ValidationRuntimeIssue::ResourceLimit;
    case ValidationExecutionErrorKind::BackendUnavailable:
        return Consensus::ValidationRuntimeIssue::BackendUnavailable;
    case ValidationExecutionErrorKind::Cancelled:
        return Consensus::ValidationRuntimeIssue::Cancelled;
    case ValidationExecutionErrorKind::CommitConflict:
        return Consensus::ValidationRuntimeIssue::CommitConflict;
    case ValidationExecutionErrorKind::ConsensusInvalid:
    case ValidationExecutionErrorKind::SystemError:
        return Consensus::ValidationRuntimeIssue::SystemError;
    }
    return Consensus::ValidationRuntimeIssue::SystemError;
}

ValidationExecutionError ClassifyValidationExecutionError(
    Consensus::BlockSpendError error)
{
    const bool consensus_issue{IsConsensusIssue(error.issue)};
    const Consensus::ValidationRuntimeIssue runtime_issue{
        error.runtime_issue.value_or(Consensus::ValidationRuntimeIssue::SystemError)};
    ValidationExecutionError result{
        .kind = consensus_issue ? ValidationExecutionErrorKind::ConsensusInvalid : RuntimeErrorKind(runtime_issue),
        .runtime_issue = consensus_issue ? std::optional<Consensus::ValidationRuntimeIssue>{} :
                                           std::optional{runtime_issue},
        .reject_reason = error.reject_reason,
        .debug_message = error.debug_message,
        .consensus_error = error,
    };
    return result;
}

Consensus::BlockSpendError ToBlockSpendError(const ValidationExecutionError& error)
{
    if (error.consensus_error) {
        return *error.consensus_error;
    }
    return {
        .issue = error.kind == ValidationExecutionErrorKind::ConsensusInvalid ?
                     Consensus::BlockConsensusIssue::Consensus :
                     Consensus::BlockConsensusIssue::ValidationRuntime,
        .runtime_issue = error.kind == ValidationExecutionErrorKind::ConsensusInvalid ?
                             std::optional<Consensus::ValidationRuntimeIssue>{} :
                             std::optional{error.runtime_issue.value_or(ToValidationRuntimeIssue(error.kind))},
        .reject_reason = error.reject_reason,
        .debug_message = error.debug_message,
    };
}

} // namespace validation
