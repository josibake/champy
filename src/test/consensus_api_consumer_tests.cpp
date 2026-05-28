// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Consensus API Consumer Test Suite

#include <consensus/api.h>

#include <boost/test/included/unit_test.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

CTransactionRef MakeCoinbase(CAmount value)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript{} << OP_0 << OP_0;
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CTransactionRef MakeSpend(const COutPoint& prevout, CAmount value)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(prevout);
    tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    return MakeTransactionRef(tx);
}

CBlock MakeBlock(std::vector<CTransactionRef> transactions)
{
    CBlock block;
    block.nVersion = 4;
    block.nTime = 2;
    block.vtx = std::move(transactions);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

Consensus::CoinSnapshot SpendableCoin(CAmount value)
{
    return Consensus::CoinSnapshot{
        .output = CTxOut{value, CScript{} << OP_TRUE},
        .height = 1,
        .is_coinbase = false,
    };
}

class ExternalSpendWorkspace final : public Consensus::BlockSpendWorkspace,
                                     public Consensus::SpendStateView,
                                     public Consensus::SequenceLockTimeView {
public:
    explicit ExternalSpendWorkspace(
        std::map<COutPoint, Consensus::CoinSnapshot> coins,
        int64_t previous_median_time_past)
        : m_coins{std::move(coins)},
          m_previous_median_time_past{previous_median_time_past}
    {
    }

    [[nodiscard]] const Consensus::SpendStateView& StagedSpendView() const override { return *this; }
    [[nodiscard]] const Consensus::SequenceLockTimeView& SequenceLockTimes() const override { return *this; }

    [[nodiscard]] bool HaveCoin(const COutPoint& outpoint) const override
    {
        return m_coins.contains(outpoint);
    }

    [[nodiscard]] std::optional<Consensus::CoinSnapshot> GetCoin(const COutPoint& outpoint) const override
    {
        const auto coin{m_coins.find(outpoint)};
        if (coin == m_coins.end()) return std::nullopt;
        return coin->second;
    }

    [[nodiscard]] int64_t PreviousMedianTimePast(const COutPoint&, int) const override
    {
        return m_previous_median_time_past;
    }

    [[nodiscard]] Consensus::BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(
        const Consensus::TransactionCoinEffects& coin_effects,
        unsigned int transaction_index) override
    {
        m_staged_indices.push_back(transaction_index);

        for (const auto& spend : coin_effects.spends) {
            if (!m_coins.erase(spend.outpoint)) {
                return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
                    .issue = Consensus::BlockConsensusIssue::Consensus,
                    .reject_reason = "external-backend-missing-spend",
                    .debug_message = "external backend could not stage spend",
                }};
            }
        }

        for (const auto& create : coin_effects.creates) {
            if (!m_coins.emplace(create.outpoint, create.coin).second) {
                return Consensus::Unexpected<Consensus::BlockSpendError>{Consensus::BlockSpendError{
                    .issue = Consensus::BlockConsensusIssue::Consensus,
                    .reject_reason = "external-backend-duplicate-create",
                    .debug_message = "external backend could not stage created coin",
                }};
            }
        }

        return {};
    }

    [[nodiscard]] const std::vector<unsigned int>& StagedIndices() const noexcept { return m_staged_indices; }

private:
    std::map<COutPoint, Consensus::CoinSnapshot> m_coins;
    int64_t m_previous_median_time_past{0};
    std::vector<unsigned int> m_staged_indices;
};

class ExternalSpendBackend final : public Consensus::BlockSpendBackend {
public:
    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin)
    {
        m_coins.emplace(outpoint, std::move(coin));
    }

    [[nodiscard]] Consensus::BlockSpendResult<std::unique_ptr<Consensus::BlockSpendWorkspace>>
    BeginBlockSpend(const Consensus::BlockSpendContext& context) override
    {
        ++m_started_workspaces;
        auto workspace{std::make_unique<ExternalSpendWorkspace>(m_coins, context.previous_median_time_past)};
        m_last_workspace = workspace.get();
        std::unique_ptr<Consensus::BlockSpendWorkspace> result{std::move(workspace)};
        return std::move(result);
    }

    [[nodiscard]] int StartedWorkspaces() const noexcept { return m_started_workspaces; }
    [[nodiscard]] ExternalSpendWorkspace* LastWorkspace() const noexcept { return m_last_workspace; }

private:
    std::map<COutPoint, Consensus::CoinSnapshot> m_coins;
    ExternalSpendWorkspace* m_last_workspace{nullptr};
    int m_started_workspaces{0};
};

class ExternalScriptChecker final : public Consensus::BlockScriptChecker {
public:
    [[nodiscard]] Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan& check) override
    {
        ++m_checks;
        m_spent_outputs += check.spent_outputs.size();
        return {};
    }

    [[nodiscard]] Consensus::BlockSpendResult<void> Complete() override
    {
        ++m_completions;
        return {};
    }

    [[nodiscard]] int Checks() const noexcept { return m_checks; }
    [[nodiscard]] int Completions() const noexcept { return m_completions; }
    [[nodiscard]] std::size_t SpentOutputs() const noexcept { return m_spent_outputs; }

private:
    int m_checks{0};
    int m_completions{0};
    std::size_t m_spent_outputs{0};
};

class ExternalCommitter final : public Consensus::BlockRevertDataWriter,
                                public Consensus::BlockSpendStateCommitter,
                                public Consensus::BlockMetadataCommitter {
public:
    void AddCoin(const COutPoint& outpoint, Consensus::CoinSnapshot coin)
    {
        m_committed_coins.emplace(outpoint, std::move(coin));
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> WriteBlockRevertData(
        const Consensus::BlockCommitContext& context,
        const Consensus::BlockSpendEffects& effects) override
    {
        m_new_best_block = context.new_best_block;
        m_revert_entries += effects.transaction_effects.size();
        return {};
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitSpendState(
        const Consensus::BlockCommitContext&,
        const Consensus::BlockSpendEffects& effects) override
    {
        ++m_spend_commits;
        for (const auto& tx_effects : effects.transaction_effects) {
            for (const auto& spend : tx_effects.spends) {
                m_committed_coins.erase(spend.outpoint);
            }
            for (const auto& create : tx_effects.creates) {
                m_committed_coins.emplace(create.outpoint, create.coin);
            }
        }
        return {};
    }

    [[nodiscard]] Consensus::BlockCommitResult<void> CommitBlockMetadata(
        const Consensus::BlockCommitContext&,
        const Consensus::BlockSpendEffects&) override
    {
        ++m_metadata_commits;
        return {};
    }

    [[nodiscard]] bool HaveCommittedCoin(const COutPoint& outpoint) const
    {
        return m_committed_coins.contains(outpoint);
    }

    [[nodiscard]] int SpendCommits() const noexcept { return m_spend_commits; }
    [[nodiscard]] int MetadataCommits() const noexcept { return m_metadata_commits; }
    [[nodiscard]] std::size_t RevertEntries() const noexcept { return m_revert_entries; }
    [[nodiscard]] const uint256& NewBestBlock() const noexcept { return m_new_best_block; }

private:
    std::map<COutPoint, Consensus::CoinSnapshot> m_committed_coins;
    uint256 m_new_best_block;
    std::size_t m_revert_entries{0};
    int m_spend_commits{0};
    int m_metadata_commits{0};
};

Consensus::BlockContextualConsensusOptions ContextualOptions(const CBlock& block)
{
    return Consensus::BlockContextualConsensusOptions{
        .header = Consensus::BlockContextualHeaderOptions{
            .block_height = 200,
            .difficulty_adjustment_interval = 2016,
            .previous_median_time_past = 0,
            .previous_block_time = 1,
            .max_block_time = 3,
        },
        .body = Consensus::BlockContextualBodyOptions{
            .transactions = Consensus::BlockContextualTransactionOptions{
                .block_height = 200,
                .locktime_cutoff = block.GetBlockTime(),
            },
        },
    };
}

Consensus::BlockConsensusContext ConsensusContext()
{
    return Consensus::BlockConsensusContext{
        .spend = Consensus::BlockSpendContext{
            .block_height = 200,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };
}

} // namespace

BOOST_AUTO_TEST_SUITE(consensus_api_consumer_tests)

BOOST_AUTO_TEST_CASE(public_api_supports_external_spend_backend)
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};
    const auto coin{SpendableCoin(50)};
    CBlock block{MakeBlock({
        MakeCoinbase(60),
        MakeSpend(prevout, 40),
    })};

    ExternalSpendBackend backend;
    backend.AddCoin(prevout, coin);
    ExternalCommitter committer;
    committer.AddCoin(prevout, coin);

    const Consensus::BlockConsensusContext context{ConsensusContext()};
    auto workspace{backend.BeginBlockSpend(context.spend)};
    BOOST_REQUIRE(workspace);
    BOOST_CHECK_EQUAL(backend.StartedWorkspaces(), 1);

    ExternalScriptChecker scripts;
    const auto effects{Consensus::ValidateAndCommitBlockStages(
        Consensus::BuildBlockPrecommitValidationView(block),
        Consensus::BlockStructuralConsensusOptions{},
        ContextualOptions(block),
        context,
        **workspace,
        scripts,
        Consensus::BlockSpendConsensusOptions{},
        committer,
        committer,
        committer)};

    BOOST_REQUIRE(effects);
    BOOST_CHECK_EQUAL(effects->fees, 10);
    BOOST_CHECK_EQUAL(effects->inputs, 2);
    BOOST_CHECK_EQUAL(effects->transaction_effects.size(), 2);

    BOOST_CHECK_EQUAL(scripts.Checks(), 1);
    BOOST_CHECK_EQUAL(scripts.Completions(), 1);
    BOOST_CHECK_EQUAL(scripts.SpentOutputs(), 1);

    auto* external_workspace{backend.LastWorkspace()};
    BOOST_REQUIRE(external_workspace);
    BOOST_CHECK_EQUAL(external_workspace->StagedIndices().size(), 2);
    BOOST_CHECK(!external_workspace->StagedSpendView().HaveCoin(prevout));
    BOOST_CHECK(external_workspace->StagedSpendView().HaveCoin(COutPoint{block.vtx[0]->GetHash(), 0}));
    BOOST_CHECK(external_workspace->StagedSpendView().HaveCoin(COutPoint{block.vtx[1]->GetHash(), 0}));

    BOOST_CHECK_EQUAL(committer.RevertEntries(), 2);
    BOOST_CHECK_EQUAL(committer.SpendCommits(), 1);
    BOOST_CHECK_EQUAL(committer.MetadataCommits(), 1);
    BOOST_CHECK(committer.NewBestBlock() == uint256::ONE);
    BOOST_CHECK(!committer.HaveCommittedCoin(prevout));
    BOOST_CHECK(committer.HaveCommittedCoin(COutPoint{block.vtx[0]->GetHash(), 0}));
    BOOST_CHECK(committer.HaveCommittedCoin(COutPoint{block.vtx[1]->GetHash(), 0}));
}

BOOST_AUTO_TEST_SUITE_END()
