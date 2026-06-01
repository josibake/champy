// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <node/ibd_pipeline.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <utility>
#include <vector>

namespace {

node::PeerBlockRef Block(int height)
{
    const arith_uint256 value{static_cast<uint64_t>(height)};
    const arith_uint256 parent_value{height > 0 ? static_cast<uint64_t>(height - 1) : 0};
    return {
        .hash = ArithToUint256(value),
        .parent_hash = ArithToUint256(parent_value),
        .height = height,
        .chain_work = value,
    };
}

node::IbdValidatedBlockPackage ReadyPackage(int height)
{
    return node::CommitReadyPackage(Block(height));
}

} // namespace

BOOST_AUTO_TEST_SUITE(ibd_pipeline_tests)

BOOST_AUTO_TEST_CASE(retire_queue_retires_in_order_blocks_immediately)
{
    node::IbdOrderedRetireQueue queue{/*next_height=*/101};

    const auto first{queue.Add(Block(101))};
    BOOST_CHECK(first.status == node::IbdRetireStatus::Ready);
    BOOST_CHECK_EQUAL(first.ready_count, 1U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 102);

    const auto second{queue.Add(Block(102))};
    BOOST_CHECK(second.status == node::IbdRetireStatus::Ready);
    BOOST_CHECK_EQUAL(second.ready_count, 1U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 103);

    const std::vector ready{queue.PopReady()};
    BOOST_REQUIRE_EQUAL(ready.size(), 2U);
    BOOST_CHECK_EQUAL(ready[0].block.height, 101);
    BOOST_CHECK_EQUAL(ready[1].block.height, 102);
    BOOST_CHECK(queue.Empty());
}

BOOST_AUTO_TEST_CASE(retire_queue_holds_future_blocks_until_gap_closes)
{
    node::IbdOrderedRetireQueue queue{/*next_height=*/101};

    const auto future{queue.Add(Block(103))};
    BOOST_CHECK(future.status == node::IbdRetireStatus::Queued);
    BOOST_CHECK_EQUAL(future.ready_count, 0U);
    BOOST_CHECK_EQUAL(queue.PendingCount(), 1U);
    BOOST_CHECK_EQUAL(queue.ReadyCount(), 0U);

    const auto ready{queue.Add(Block(101))};
    BOOST_CHECK(ready.status == node::IbdRetireStatus::Ready);
    BOOST_CHECK_EQUAL(ready.ready_count, 1U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 102);

    const auto closes_gap{queue.Add(Block(102))};
    BOOST_CHECK(closes_gap.status == node::IbdRetireStatus::Ready);
    BOOST_CHECK_EQUAL(closes_gap.ready_count, 2U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 104);

    const std::vector retired{queue.PopReady()};
    BOOST_REQUIRE_EQUAL(retired.size(), 3U);
    BOOST_CHECK_EQUAL(retired[0].block.height, 101);
    BOOST_CHECK_EQUAL(retired[1].block.height, 102);
    BOOST_CHECK_EQUAL(retired[2].block.height, 103);
}

BOOST_AUTO_TEST_CASE(retire_queue_rejects_duplicate_stale_and_invalid_heights)
{
    node::IbdOrderedRetireQueue queue{/*next_height=*/101};

    BOOST_CHECK(queue.Add(Block(102)).status == node::IbdRetireStatus::Queued);
    BOOST_CHECK(queue.Add(Block(102)).status == node::IbdRetireStatus::DuplicateHeight);
    BOOST_CHECK(queue.Add(Block(100)).status == node::IbdRetireStatus::StaleHeight);
    BOOST_CHECK(queue.Add(Block(-1)).status == node::IbdRetireStatus::InvalidHeight);
    BOOST_CHECK_EQUAL(queue.PendingCount(), 1U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 101);
}

BOOST_AUTO_TEST_CASE(retire_queue_requires_commit_ready_packages)
{
    node::IbdOrderedRetireQueue queue{/*next_height=*/101};

    node::IbdValidatedBlockPackage not_ready{
        .block = Block(101),
        .parent_hash = {},
        .block_data = nullptr,
        .spend_effects_ready = false,
        .script_status = node::IbdScriptValidationStatus::NotSubmitted,
    };
    BOOST_CHECK(queue.Add(std::move(not_ready)).status == node::IbdRetireStatus::NotReady);
    BOOST_CHECK_EQUAL(queue.PendingCount(), 0U);
    BOOST_CHECK_EQUAL(queue.ReadyCount(), 0U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 101);

    node::IbdValidatedBlockPackage ready{ReadyPackage(101)};
    ready.parent_hash = uint256::ONE;
    BOOST_CHECK(queue.Add(std::move(ready)).status == node::IbdRetireStatus::Ready);

    const std::vector retired{queue.PopReady()};
    BOOST_REQUIRE_EQUAL(retired.size(), 1U);
    BOOST_CHECK_EQUAL(retired[0].block.height, 101);
    BOOST_CHECK(retired[0].parent_hash == uint256::ONE);
    BOOST_CHECK(retired[0].ReadyForSerializedCommit());
}

BOOST_AUTO_TEST_CASE(parent_bound_retire_queue_retires_only_matching_parent_chain)
{
    node::IbdOrderedRetireQueue queue{node::IbdRetireChainPosition{
        .next_height = 101,
        .expected_parent_hash = Block(100).hash,
    }};

    node::IbdValidatedBlockPackage future{ReadyPackage(102)};
    future.parent_hash = Block(101).hash;
    BOOST_CHECK(queue.Add(std::move(future)).status == node::IbdRetireStatus::Queued);

    node::IbdValidatedBlockPackage wrong_parent{ReadyPackage(101)};
    wrong_parent.parent_hash = Block(99).hash;
    BOOST_CHECK(queue.Add(std::move(wrong_parent)).status == node::IbdRetireStatus::ParentMismatch);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 101);
    BOOST_CHECK_EQUAL(queue.PendingCount(), 1U);

    node::IbdValidatedBlockPackage next{ReadyPackage(101)};
    next.parent_hash = Block(100).hash;
    const auto retired_result{queue.Add(std::move(next))};
    BOOST_CHECK(retired_result.status == node::IbdRetireStatus::Ready);
    BOOST_CHECK_EQUAL(retired_result.ready_count, 2U);
    BOOST_CHECK_EQUAL(queue.NextHeight(), 103);

    const std::vector ready{queue.PopReady()};
    BOOST_REQUIRE_EQUAL(ready.size(), 2U);
    BOOST_CHECK_EQUAL(ready[0].block.height, 101);
    BOOST_CHECK(ready[0].parent_hash == Block(100).hash);
    BOOST_CHECK_EQUAL(ready[1].block.height, 102);
    BOOST_CHECK(ready[1].parent_hash == ready[0].block.hash);
}

BOOST_AUTO_TEST_CASE(parent_bound_retire_queue_rejects_inconsistent_future_parent)
{
    node::IbdOrderedRetireQueue queue{node::IbdRetireChainPosition{
        .next_height = 101,
        .expected_parent_hash = Block(100).hash,
    }};

    node::IbdValidatedBlockPackage future{ReadyPackage(102)};
    future.parent_hash = Block(99).hash;
    BOOST_CHECK(queue.Add(std::move(future)).status == node::IbdRetireStatus::Queued);

    node::IbdValidatedBlockPackage next{ReadyPackage(101)};
    next.parent_hash = Block(100).hash;
    BOOST_CHECK(queue.Add(std::move(next)).status == node::IbdRetireStatus::ParentMismatch);
    BOOST_CHECK(queue.PopReady().empty());
    BOOST_CHECK_EQUAL(queue.NextHeight(), 101);
}

BOOST_AUTO_TEST_CASE(pipeline_admission_bounds_parallel_work_ahead_of_commit)
{
    node::IbdPipeline pipeline{/*next_commit_height=*/101, node::IbdPipelineLimits{.max_blocks_ahead = 3}};

    BOOST_CHECK(pipeline.Admit(Block(100)).status == node::IbdAdmissionStatus::StaleHeight);
    BOOST_CHECK(pipeline.Admit(Block(101)).status == node::IbdAdmissionStatus::Accepted);
    BOOST_CHECK(pipeline.Admit(Block(103)).status == node::IbdAdmissionStatus::Accepted);
    BOOST_CHECK(pipeline.Admit(Block(104)).status == node::IbdAdmissionStatus::TooFarAhead);
    BOOST_CHECK(pipeline.Admit(Block(-1)).status == node::IbdAdmissionStatus::InvalidHeight);
}

BOOST_AUTO_TEST_CASE(parent_bound_pipeline_admission_rejects_wrong_next_parent)
{
    node::IbdPipeline pipeline{
        node::IbdRetireChainPosition{
            .next_height = 101,
            .expected_parent_hash = Block(100).hash,
        }};

    node::PeerBlockRef wrong_parent{Block(101)};
    wrong_parent.parent_hash = Block(99).hash;
    BOOST_CHECK(pipeline.Admit(wrong_parent).status == node::IbdAdmissionStatus::ParentMismatch);
    BOOST_CHECK(pipeline.Admit(Block(101)).status == node::IbdAdmissionStatus::Accepted);
    BOOST_CHECK(pipeline.Admit(Block(102)).status == node::IbdAdmissionStatus::Accepted);
}

BOOST_AUTO_TEST_CASE(pipeline_commits_only_contiguous_validated_blocks)
{
    node::IbdPipeline pipeline{/*next_commit_height=*/101};

    BOOST_CHECK(pipeline.MarkValidated(Block(102)).status == node::IbdRetireStatus::Queued);
    BOOST_CHECK(pipeline.PopReadyToCommit().empty());

    BOOST_CHECK(pipeline.MarkValidated(Block(101)).status == node::IbdRetireStatus::Ready);
    const std::vector ready{pipeline.PopReadyToCommit()};
    BOOST_REQUIRE_EQUAL(ready.size(), 2U);
    BOOST_CHECK_EQUAL(ready[0].block.height, 101);
    BOOST_CHECK_EQUAL(ready[1].block.height, 102);
    BOOST_CHECK_EQUAL(pipeline.NextCommitHeight(), 103);
}

BOOST_AUTO_TEST_CASE(pipeline_retires_commit_ready_packages)
{
    node::IbdPipeline pipeline{/*next_commit_height=*/101};

    node::IbdValidatedBlockPackage future{ReadyPackage(102)};
    future.parent_hash = uint256::ONE;
    BOOST_CHECK(pipeline.MarkValidated(std::move(future)).status == node::IbdRetireStatus::Queued);

    node::IbdValidatedBlockPackage next{ReadyPackage(101)};
    next.parent_hash = uint256::ZERO;
    BOOST_CHECK(pipeline.MarkValidated(std::move(next)).status == node::IbdRetireStatus::Ready);

    const std::vector ready{pipeline.PopReadyToCommit()};
    BOOST_REQUIRE_EQUAL(ready.size(), 2U);
    BOOST_CHECK_EQUAL(ready[0].block.height, 101);
    BOOST_CHECK(ready[0].parent_hash == uint256::ZERO);
    BOOST_CHECK_EQUAL(ready[1].block.height, 102);
    BOOST_CHECK(ready[1].parent_hash == uint256::ONE);
}

BOOST_AUTO_TEST_CASE(stage_metrics_are_recorded_by_explicit_stage)
{
    node::IbdPipelineMetrics metrics;

    metrics.Record(node::IbdPipelineStage::Download, std::chrono::milliseconds{3}, /*bytes_processed=*/1000);
    metrics.Record(node::IbdPipelineStage::Download, std::chrono::milliseconds{2}, /*bytes_processed=*/2000);
    metrics.Record(node::IbdPipelineStage::BlockAdmission, std::chrono::milliseconds{4});
    metrics.Record(node::IbdPipelineStage::ScriptValidation, std::chrono::milliseconds{7}, /*bytes_processed=*/0, /*blocks_processed=*/3);

    const node::IbdStageMetrics& download{metrics.Stage(node::IbdPipelineStage::Download)};
    BOOST_CHECK_EQUAL(download.blocks, 2U);
    BOOST_CHECK_EQUAL(download.bytes, 3000U);
    BOOST_CHECK(download.elapsed == std::chrono::milliseconds{5});

    const node::IbdStageMetrics& admission{metrics.Stage(node::IbdPipelineStage::BlockAdmission)};
    BOOST_CHECK_EQUAL(admission.blocks, 1U);
    BOOST_CHECK_EQUAL(admission.bytes, 0U);
    BOOST_CHECK(admission.elapsed == std::chrono::milliseconds{4});

    const node::IbdStageMetrics& script{metrics.Stage(node::IbdPipelineStage::ScriptValidation)};
    BOOST_CHECK_EQUAL(script.blocks, 3U);
    BOOST_CHECK_EQUAL(script.bytes, 0U);
    BOOST_CHECK(script.elapsed == std::chrono::milliseconds{7});
}

BOOST_AUTO_TEST_SUITE_END()
