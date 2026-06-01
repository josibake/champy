// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <node/ibd_pipeline.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <vector>

namespace {

node::PeerBlockRef Block(int height)
{
    const arith_uint256 value{static_cast<uint64_t>(height)};
    return {
        .hash = ArithToUint256(value),
        .height = height,
        .chain_work = value,
    };
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
    BOOST_CHECK_EQUAL(ready[0].height, 101);
    BOOST_CHECK_EQUAL(ready[1].height, 102);
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
    BOOST_CHECK_EQUAL(retired[0].height, 101);
    BOOST_CHECK_EQUAL(retired[1].height, 102);
    BOOST_CHECK_EQUAL(retired[2].height, 103);
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

BOOST_AUTO_TEST_CASE(pipeline_admission_bounds_parallel_work_ahead_of_commit)
{
    node::IbdPipeline pipeline{/*next_commit_height=*/101, node::IbdPipelineLimits{.max_blocks_ahead = 3}};

    BOOST_CHECK(pipeline.Admit(Block(100)).status == node::IbdAdmissionStatus::StaleHeight);
    BOOST_CHECK(pipeline.Admit(Block(101)).status == node::IbdAdmissionStatus::Accepted);
    BOOST_CHECK(pipeline.Admit(Block(103)).status == node::IbdAdmissionStatus::Accepted);
    BOOST_CHECK(pipeline.Admit(Block(104)).status == node::IbdAdmissionStatus::TooFarAhead);
    BOOST_CHECK(pipeline.Admit(Block(-1)).status == node::IbdAdmissionStatus::InvalidHeight);
}

BOOST_AUTO_TEST_CASE(pipeline_commits_only_contiguous_validated_blocks)
{
    node::IbdPipeline pipeline{/*next_commit_height=*/101};

    BOOST_CHECK(pipeline.MarkValidated(Block(102)).status == node::IbdRetireStatus::Queued);
    BOOST_CHECK(pipeline.PopReadyToCommit().empty());

    BOOST_CHECK(pipeline.MarkValidated(Block(101)).status == node::IbdRetireStatus::Ready);
    const std::vector ready{pipeline.PopReadyToCommit()};
    BOOST_REQUIRE_EQUAL(ready.size(), 2U);
    BOOST_CHECK_EQUAL(ready[0].height, 101);
    BOOST_CHECK_EQUAL(ready[1].height, 102);
    BOOST_CHECK_EQUAL(pipeline.NextCommitHeight(), 103);
}

BOOST_AUTO_TEST_CASE(stage_metrics_are_recorded_by_explicit_stage)
{
    node::IbdPipelineMetrics metrics;

    metrics.Record(node::IbdPipelineStage::Download, std::chrono::milliseconds{3}, /*bytes_processed=*/1000);
    metrics.Record(node::IbdPipelineStage::Download, std::chrono::milliseconds{2}, /*bytes_processed=*/2000);
    metrics.Record(node::IbdPipelineStage::BlockAdmission, std::chrono::milliseconds{4});
    metrics.Record(node::IbdPipelineStage::ScriptValidation, std::chrono::milliseconds{7});

    const node::IbdStageMetrics& download{metrics.Stage(node::IbdPipelineStage::Download)};
    BOOST_CHECK_EQUAL(download.blocks, 2U);
    BOOST_CHECK_EQUAL(download.bytes, 3000U);
    BOOST_CHECK(download.elapsed == std::chrono::milliseconds{5});

    const node::IbdStageMetrics& admission{metrics.Stage(node::IbdPipelineStage::BlockAdmission)};
    BOOST_CHECK_EQUAL(admission.blocks, 1U);
    BOOST_CHECK_EQUAL(admission.bytes, 0U);
    BOOST_CHECK(admission.elapsed == std::chrono::milliseconds{4});

    const node::IbdStageMetrics& script{metrics.Stage(node::IbdPipelineStage::ScriptValidation)};
    BOOST_CHECK_EQUAL(script.blocks, 1U);
    BOOST_CHECK_EQUAL(script.bytes, 0U);
    BOOST_CHECK(script.elapsed == std::chrono::milliseconds{7});
}

BOOST_AUTO_TEST_SUITE_END()
