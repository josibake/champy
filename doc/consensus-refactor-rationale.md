# Consensus Refactor Rationale

The refactor separates protocol rules from Core runtime behavior.

The intended result is code that is easier to audit, easier to test, and easier
to optimize without changing consensus semantics.

## Problem

The historical validation path mixed:

- consensus checks
- UTXO reads and writes
- block and undo storage
- block-index mutation
- script scheduling
- mempool repair
- logging and validation-state mapping

That makes review non-local. A caller may look like it is only checking a
block, while the implementation also reads global state, mutates caches,
schedules work, writes files, or repairs the mempool.

## Why This Is Safer

Consensus functions receive explicit inputs and return explicit results.

Validation converts Core state into value contexts and narrow capabilities.
Mutation is delayed to commit interfaces. Side effects are named at the call
site.

This reduces hidden dependencies:

- no consensus dependency on `Chainstate`, `CBlockIndex`, mempool, or storage
- no hidden clock, logging, database, or lock access in consensus
- script execution is requested through `BlockScriptChecker`
- UTXO access is behind spend-state interfaces
- final mutation happens through commit interfaces

The boundary scripts and API-consumer tests make these rules executable.

## Why This Is Cleaner

The layers now have distinct jobs:

- consensus decides protocol validity
- validation adapts Core state and commits accepted effects
- kernel exposes Core's embeddable runtime
- node owns P2P, RPC, mempool, relay, and process behavior

The code follows a small set of repeated patterns:

- value contexts for rule inputs
- stage functions for validation order
- workspace interfaces for spend state
- script-check plans for execution
- effects objects for validated mutations
- commit interfaces for writes

These patterns make it easier to know where new code belongs.

## Why This Enables Better Performance

The refactor does not depend on fine-grained virtual calls in the hot path.
Abstractions sit at block, attempt, script-check, or commit boundaries.

That keeps the current path measurable while opening better implementation
options:

- alternate spend-state layouts such as SwiftSync or Utreexo
- alternate script scheduling models
- pipelined block validation stages
- isolated benchmarks for block connection and spend backends

Performance work can now change orchestration or storage layout without
changing consensus rule code.

## Two Concrete Examples

### Spend Validation

Spend validation reads from `SpendStateView`, plans script checks, and records
`BlockSpendEffects`.

Commit code writes undo data, applies coin changes, and updates block metadata.

Reviewers can inspect validity decisions separately from mutation.

### Mempool Reorg Handling

Validation emits chain events through `ChainstateEventSink`.

`node::MempoolChainSync` owns disconnected-transaction staging and mempool
repair. Kernel remains usable without node, and mempool policy does not become
part of consensus or the kernel API.

## Review Rule

When adding code, classify it first:

- protocol rule
- Core validation integration
- Core runtime/storage adapter
- node behavior
- compatibility shim

If the classification is unclear, the interface is probably too broad.
