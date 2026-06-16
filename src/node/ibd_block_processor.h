// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H
#define BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H

#include <kernel/cs_main.h>
#include <node/ibd_block_download.h>
#include <validation/active_chain.h>
#include <validation/block_data_admission.h>
#include <validation/block_validation.h>

#include <memory>
#include <optional>

class ChainstateEventSink;
class ChainstateManager;
class CBlock;

namespace node {

struct IbdBlockProcessRequest {
    ChainstateEventSink* chain_events{nullptr};
    std::shared_ptr<const CBlock> block;
    BlockDataStorageMode block_data_storage{BlockDataStorageMode::ApplyAdmissionChecks};
    bool min_pow_checked{false};
    BlockValidationTime time;
};

[[nodiscard]] IbdBlockDownloadWindow BuildIbdBlockDownloadWindow(
    std::optional<validation::ActiveChainTipSnapshot> active_tip,
    IbdBlockDownloadLimits limits);

/**
 * Node-owned IBD execution shell.
 *
 * This processor now delegates block admission and activation to the explicit
 * chain-validation entry point. The old segmented validation facade/pipeline is
 * intentionally not part of the node build.
 */
class IbdBlockProcessor
{
public:
    explicit IbdBlockProcessor(ChainstateManager& chainman);

    [[nodiscard]] IbdBlockDownloadWindow AdmissionWindow(IbdBlockDownloadLimits limits) const LOCKS_EXCLUDED(::cs_main);
    [[nodiscard]] NewBlockProcessingResult ProcessDownloadedBlock(IbdBlockProcessRequest request) LOCKS_EXCLUDED(::cs_main);

    [[nodiscard]] const IbdBlockProcessingMetrics& Metrics() const noexcept { return m_metrics; }

private:
    ChainstateManager& m_chainman;
    IbdBlockProcessingMetrics m_metrics;
};

} // namespace node

#endif // BITCOIN_NODE_IBD_BLOCK_PROCESSOR_H
