# Consensus Design

This branch separates consensus rule evaluation from Core's node runtime.

The goal is not to rewrite Bitcoin Core. The goal is to make the consensus
path easier to test, easier to reason about, and easier to extract later.

For validation locking, scheduling, and callback rules, see
[validation-execution-contracts.md](validation-execution-contracts.md).

## Layers

### Consensus

Consensus code answers protocol questions.

It may use protocol value types such as `CBlock`, `CTransaction`, `CTxOut`,
`Coin`, `uint256`, and consensus parameters.

It must not depend on node runtime state:

- no `Chainstate` or `ChainstateManager`
- no `CBlockIndex` mutation
- no mempool
- no P2P, RPC, logging, clocks, files, or databases
- no `cs_main`
- no Core validation-state objects

Consensus helpers should take explicit inputs and return explicit results.

### Validation

Validation is Core's chain-acceptance integration layer.

It owns:

- active-chain ordering
- block index integration
- undo and coins cache integration
- script cache integration
- lock requirements
- logging
- validation-state mapping

Validation calls consensus helpers and applies their results to Core's current
runtime.

Validation-layer adapters and services live under `src/validation`.

Mempool admission is separate from chain validation. It may call consensus
transaction helpers, but relay policy, package policy, fee policy, and mempool
state are node concerns.

### Kernel

Kernel is Core's embeddable runtime layer.

It wires chain validation to Core's default storage and chainstate
implementation. Kernel may use LevelDB, block files, and the current block
index internally.

Kernel must not depend on node code. In particular, it must not expose or own
mempool, P2P, RPC, or relay-policy concepts.

### Node

Node owns process-level orchestration:

- P2P
- RPC
- mempool state and policy
- relay
- indexes and optional services
- application lifecycle

Node talks to validation through service interfaces.

## Validation Flow

Block validation is split into stages:

```text
structural -> contextual -> spend -> commit
```

The stages mean:

- `structural`: checks that only need the block itself.
- `contextual`: checks that need chain context, deployment state, difficulty,
  or height.
- `spend`: checks UTXO-dependent transaction validity and plans coin effects.
- `commit`: applies validated effects to Core's mutable state.

The important rule is that validation gathers Core-specific context before
calling consensus code. Consensus code should not go looking for state.

## API Shape

The extraction-facing consensus include is `consensus/api.h`.

The important interfaces are:

- `SpendState`: reads UTXO state needed by spend validation.
- `BlockScriptChecker`: executes or schedules script-check work.
- `CoinEffects`: records validated coin spends and creates.
- `BlockCommitter`: applies validated effects.
- `ChainValidationService`: Core-facing service for block/header admission.
- `ChainstateMempoolSync`: validation-to-node hook for mempool side effects.

These names are intentional:

- Consensus decides rules.
- Validation integrates consensus with Core state.
- Kernel exposes Core's embeddable runtime.
- Node owns networking, mempool, policy, and process orchestration.

## Boundary Rules

Consensus should be written as explicit rule code:

- explicit inputs
- explicit outputs
- no hidden reads
- no hidden writes
- no logging as control flow
- no global mutable state

Kernel should be usable without node:

- no `node::*` references from `src/kernel`
- no `<node/...>` includes from `src/kernel`
- no mempool ownership in kernel

Mempool state is node state. Chain validation may notify a
`ChainstateMempoolSync` implementation that blocks connected or disconnected,
but the concrete mempool repair logic belongs to node.

Storage is not consensus. Core's block storage and chainstate loading live in
kernel as default runtime capabilities. Alternate implementations should
provide equivalent capabilities without changing consensus code.

## Example: Spend Validation

The old `ConnectBlock` path mixed several jobs:

- read UTXOs
- validate spends
- run or schedule scripts
- calculate fees and subsidy
- mutate coins
- update block index state
- map errors into Core validation state

This branch separates that path.

Validation builds a `BlockConsensusContext` and a `SpendState`. Consensus spend
code validates the block and records `CoinEffects`. Script checks are requested
through `BlockScriptChecker`, so script execution is separate from spend
accounting. Commit code then applies the validated effects through an explicit
commit interface.

The practical result is that spend rules can be tested without Core chainstate,
and commit code can be reviewed as mutation rather than validation.

## Example: Mempool Reorg Effects

Mempool reorg handling used to leak through validation and kernel-adjacent code.

Now validation only reports chain events through `ChainstateMempoolSync`:

- a block connected
- a block disconnected
- a reorg finished

`node::MempoolChainSync` owns the concrete mempool behavior, including
disconnected-transaction staging.

This keeps mempool policy out of kernel and keeps validation from owning node
state.

## Adding Code

Use this placement rule:

- Put protocol rule decisions in consensus.
- Put Core state integration in validation.
- Put embeddable Core runtime services in kernel.
- Put mempool, P2P, RPC, relay, and process orchestration in node.

When adding a consensus helper:

- pass all required context as values or narrow interfaces
- return a result type, not a mutated Core validation state
- keep script execution behind `BlockScriptChecker`
- keep UTXO access behind `SpendState`
- keep mutation behind commit/effects interfaces

When adding runtime behavior:

- do not make consensus depend on it
- do not make kernel depend on node
- prefer a small capability interface over a concrete Core type

## Current Limits

This is not yet a standalone external library release.

The consensus target is much closer to extraction, but Core still supplies many
protocol value types and default runtime adapters.

Kernel is still Core's default runtime, not pure consensus. It intentionally
contains Core's block storage, chainstate loading, and block-index-backed
implementation.

Validation is still partly organized around existing Core source layout. The
important boundary is the dependency direction: consensus does not know about
validation, kernel, or node; kernel does not know about node.
