// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_VALIDATION_H
#define BITCOIN_CHAIN_VALIDATION_H

#include <block_validation.h>

#include <memory>
#include <span>

class ChainValidationService
{
public:
    explicit ChainValidationService(ChainstateManager& chainman) : m_chainman{chainman} {}

    [[nodiscard]] NewBlockHeadersResult ProcessNewBlockHeaders(
        std::span<const CBlockHeader> headers,
        BlockHeaderAcceptanceOptions options,
        BlockValidationTime time,
        BlockValidationState& state) LOCKS_EXCLUDED(cs_main);

    [[nodiscard]] BlockAcceptanceResult AcceptBlock(
        const std::shared_ptr<const CBlock>& block,
        BlockValidationState& state,
        BlockAcceptanceOptions options,
        BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    [[nodiscard]] NewBlockProcessingResult ProcessNewBlock(
        ChainstateMempoolSync* mempool_sync,
        const std::shared_ptr<const CBlock>& block,
        NewBlockProcessingOptions options,
        BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

    [[nodiscard]] NewBlockProcessingResult ProcessNewBlock(
        const std::shared_ptr<const CBlock>& block,
        NewBlockProcessingOptions options,
        BlockValidationTime time) LOCKS_EXCLUDED(cs_main);

    [[nodiscard]] BlockValidationState TestBlockValidity(
        Chainstate& chainstate,
        const CBlock& block,
        const Consensus::BlockCheckOptions& options,
        BlockValidationTime time) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    ChainstateManager& m_chainman;
};

#endif // BITCOIN_CHAIN_VALIDATION_H
