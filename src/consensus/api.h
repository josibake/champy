// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_API_H
#define BITCOIN_CONSENSUS_API_H

// Extraction-facing consensus API.
//
// This header is the intended public surface for the internal consensus
// library. It exposes block validation stages, spend-state interfaces, script
// checking, staged effects, commit interfaces, and fixture backends. Lower-level
// rule helpers may stay available inside the tree without becoming part of the
// extraction contract.

#include <consensus/amount.h>
#include <consensus/block_check.h>
#include <consensus/block_commit.h>
#include <consensus/block_consensus_pipeline.h>
#include <consensus/block_facts.h>
#include <consensus/block_spend.h>
#include <consensus/coin_effects.h>
#include <consensus/consensus.h>
#include <consensus/diagnostics.h>
#include <consensus/expected.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/predicates.h>
#include <consensus/script_checker.h>
#include <consensus/script_view.h>
#include <consensus/snapshot_spend_state.h>
#include <consensus/spend_state.h>
#include <consensus/tx_check.h>

#endif // BITCOIN_CONSENSUS_API_H
