// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <consensus/block_commit.h>
#include <consensus/block_check.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_facts.h>
#include <consensus/block_spend.h>
#include <consensus/coin_effects.h>
#include <consensus/consensus.h>
#include <consensus/diagnostics.h>
#include <consensus/expected.h>
#include <consensus/locktime.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/pow.h>
#include <consensus/predicates.h>
#include <consensus/script_checker.h>
#include <consensus/sequence_locks.h>
#include <consensus/sigops.h>
#include <consensus/snapshot_spend_state.h>
#include <consensus/spend_state.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>

#include <cassert>

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

class NoopScriptChecker final : public Consensus::BlockScriptChecker {
public:
    Consensus::BlockSpendResult<void> Check(const Consensus::TransactionScriptCheckPlan&) override
    {
        return {};
    }

    Consensus::BlockSpendResult<void> Complete() override { return {}; }
};

class NoopBlockCommitSideEffects final : public Consensus::BlockRevertDataWriter, public Consensus::BlockMetadataCommitter {
public:
    Consensus::BlockCommitResult<void> WriteBlockRevertData(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override { return {}; }
    Consensus::BlockCommitResult<void> CommitBlockMetadata(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override { return {}; }
};

class NoopBlockSpendStateCommitter final : public Consensus::BlockSpendStateCommitter {
public:
    Consensus::BlockCommitResult<void> CommitSpendState(const Consensus::BlockCommitContext&, const Consensus::BlockSpendEffects&) override { return {}; }
};

} // namespace

int main()
{
    const COutPoint prevout{Txid::FromUint256(uint256::ONE), 0};

    CBlock block;
    block.vtx.push_back(MakeCoinbase(60));
    block.vtx.push_back(MakeSpend(prevout, 40));
    block.nVersion = 4;
    block.nTime = 2;
    block.hashMerkleRoot = BlockMerkleRoot(block);

    Consensus::SnapshotSpendState spend_state;
    spend_state.AddCoin(prevout, Consensus::CoinSnapshot{
                                     .output = CTxOut{50, CScript{} << OP_TRUE},
                                     .height = 1,
                                     .is_coinbase = false,
                                 });

    const Consensus::BlockConsensusContext context{
        .spend = Consensus::BlockSpendContext{
            .block_height = 200,
            .previous_median_time_past = 0,
        },
        .commit = Consensus::BlockCommitContext{.new_best_block = uint256::ONE},
        .block_subsidy = 50,
    };
    const Consensus::BlockStructuralConsensusOptions structural_options{
        .check_merkle_root = true,
    };
    const Consensus::BlockContextualConsensusOptions contextual_options{
        .header = Consensus::BlockContextualHeaderOptions{
            .block_height = 200,
            .difficulty_adjustment_interval = 2016,
            .previous_median_time_past = 1,
            .previous_block_time = 1,
            .max_block_time = 3,
        },
        .body = Consensus::BlockContextualBodyOptions{
            .transactions = Consensus::BlockContextualTransactionOptions{
                .block_height = 200,
                .locktime_cutoff = 1,
            },
        },
    };
    const Consensus::BlockSpendConsensusOptions options;
    NoopScriptChecker scripts;
    auto workspace{spend_state.BeginBlockSpend(context.spend)};
    assert(workspace);

    const auto validation_input{Consensus::BuildBlockPrecommitValidationView(block)};
    const auto effects{Consensus::ValidateBlockPrecommit(
        validation_input,
        structural_options,
        contextual_options,
        context,
        **workspace,
        scripts,
        options)};

    assert(effects);
    assert(effects->fees == 10);
    assert(effects->transaction_effects.size() == 2);
    assert(spend_state.HaveCoin(prevout));
    assert(!(*workspace)->StagedSpendView().HaveCoin(prevout));
    assert((*workspace)->StagedSpendView().HaveCoin(COutPoint{block.vtx[0]->GetHash(), 0}));
    assert((*workspace)->StagedSpendView().HaveCoin(COutPoint{block.vtx[1]->GetHash(), 0}));

    NoopBlockSpendStateCommitter spend_committer;
    NoopBlockCommitSideEffects side_effects;
    const auto commit{Consensus::CommitBlockStageEffects(context.commit, *effects, side_effects, spend_committer, side_effects)};
    assert(commit);

    return 0;
}
