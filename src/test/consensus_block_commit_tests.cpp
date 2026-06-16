// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/block_commit.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

class FakeBlockCommitter final : public Consensus::BlockRevertDataWriter, public Consensus::BlockSpendStateCommitter, public Consensus::BlockMetadataCommitter {
public:
    std::optional<std::string> fail_step;
    std::vector<std::string> calls;

    Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("revert-data");
    }

    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("spend-state");
    }

    Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override
    {
        return Record("metadata");
    }

private:
    Consensus::BlockCommitResult<void> Record(std::string step)
    {
        calls.push_back(step);
        if (fail_step == step) {
            return Consensus::Unexpected<Consensus::BlockCommitError>{Consensus::BlockCommitError{
                .reject_reason = step + "-failed",
            }};
        }
        return {};
    }
};

void CheckCalls(const std::vector<std::string>& calls, std::initializer_list<std::string> expected)
{
    BOOST_CHECK_EQUAL_COLLECTIONS(calls.begin(), calls.end(), expected.begin(), expected.end());
}

Consensus::BlockCommitContext CommitContext()
{
    return Consensus::BlockCommitContext{.new_best_block = uint256::ONE};
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_block_commit_tests)

BOOST_AUTO_TEST_CASE(commit_block_effects_writes_revert_data_coins_and_metadata)
{
    FakeBlockCommitter committer;

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    BOOST_CHECK(result);
    CheckCalls(committer.calls, {"revert-data", "spend-state", "metadata"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_stops_after_revert_data_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "revert-data";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, "revert-data-failed");
    CheckCalls(committer.calls, {"revert-data"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_stops_after_spend_state_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "spend-state";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, "spend-state-failed");
    CheckCalls(committer.calls, {"revert-data", "spend-state"});
}

BOOST_AUTO_TEST_CASE(commit_block_effects_returns_metadata_error)
{
    FakeBlockCommitter committer;
    committer.fail_step = "metadata";

    const auto result{Consensus::CommitBlockEffects(CommitContext(), Consensus::BlockSpendEffects{}, committer, committer, committer)};

    BOOST_REQUIRE(!result);
    BOOST_CHECK_EQUAL(result.error().reject_reason, "metadata-failed");
    CheckCalls(committer.calls, {"revert-data", "spend-state", "metadata"});
}

BOOST_AUTO_TEST_SUITE_END()
