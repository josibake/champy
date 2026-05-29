# Validation Execution Contracts

This document describes the execution and locking rules for validation code in
this branch.

The goal is local reasoning. A caller should know what state a validation API
may read, mutate, lock, schedule, or notify from its declaration and nearby
documentation.

## Layer Rules

### Consensus

Consensus code:

- takes no Core locks
- starts no threads
- performs no disk, network, IPC, or logging side effects
- does not call node, kernel, or validation callbacks
- reads state only through values or narrow consensus-facing interfaces
- returns explicit results and effects

Consensus code may describe work that another layer executes, such as script
checks, but it does not own the execution model.

### Validation

Validation code adapts Core runtime state to consensus inputs and commits
validated effects.

Validation may:

- read active-chain and block-index state under `cs_main`
- read and update coins state through explicit spend/commit capabilities
- write undo data and block metadata during commit
- map consensus results into Core validation-state objects
- use Core's script-check execution capability

Validation should not:

- own mempool policy or mempool state
- add new node event handlers that require broad `cs_main` scopes
- hide storage access behind consensus helpers
- schedule background work from consensus code

### Kernel

Kernel owns Core's embeddable runtime implementation.

Kernel may use Core's default block storage, chainstate loading, LevelDB-backed
coins state, and block index. Kernel must not depend on node code.

### Node

Node owns process orchestration, networking, mempool, relay policy, RPC, and
runtime scheduling choices.

Node may implement validation event sinks and execution adapters. Those
adapters must not leak node policy back into consensus or kernel APIs.

## Lock Contracts

`cs_main` protects:

- active-chain selection
- block-index consistency
- chainstate activation and invalidation
- serialized commit of chainstate-visible block effects

The mempool lock protects:

- mempool entries
- mempool ancestor/descendant indexes
- mempool policy caches
- disconnected-transaction staging owned by node

Validation APIs should make lock requirements explicit with thread-safety
annotations or assertions. If a function requires `cs_main`, that requirement
belongs at the declaration. If a function must not call out while holding
`cs_main`, assert or structure the code so that the callback boundary is
visibly outside the locked scope.

## Block Connection Contract

Block connection should follow this shape:

```text
gather Core state under required locks
  -> build explicit request values and capabilities
  -> run consensus-facing validation stages
  -> execute or wait for script checks through the script checker capability
  -> commit validated effects in a serialized commit section
  -> publish node/kernel events after commit
```

Only the commit section should mutate active chainstate. Consensus-facing
validation stages should not rely on hidden global state.

Current block connection capabilities are intentionally narrow:

- `BlockUndoWriter` writes undo data during commit.
- `BlockIndexValidityCommitter` records validated block metadata.
- `BlockScriptChecker` owns script execution and caching.
- `SpendState` owns UTXO reads for spend validation.

Do not pass `Chainstate`, `ChainstateManager`, `CoreBlockDataStore`,
`CoreBlockIndexStore`, or another broad store into the block connection engine.

## Execution Contract

The validation API should not commit to one runtime model.

The same validation code should support:

- current Core synchronous/blocking execution
- parallel script execution through existing script-check queues
- later pipelined IBD orchestration
- later async or process-boundary adapters

Execution choices belong at node or kernel adapter boundaries. Consensus and
validation should expose work and effects clearly enough that those adapters can
choose how to run them.

## Event Contract

Validation may publish chain events such as:

- block connected
- block disconnected
- reorg completed

Those events are not consensus effects. They are runtime notifications used by
node features such as mempool repair.

Event sinks should be called after the corresponding chainstate mutation is
committed. The current mempool event sink still uses Core's legacy `cs_main` and
mempool lock contract; this is tracked in `legacy-compatibility.md`.
Validation should not name or own mempool behavior directly.

## Review Checklist

For validation changes, check:

- Are consensus inputs explicit?
- Is mutation delayed until commit?
- Are lock requirements visible at API boundaries?
- Are callbacks and event sinks outside broad lock scopes where possible?
- Does the callee receive the smallest value or capability it needs?
- Does kernel remain independent of node?
- Does consensus remain independent of validation, kernel, and node?
- Can script execution be replaced without changing spend accounting?
- Can storage behavior be replaced without changing consensus rules?
- Does a runtime request expose only the capabilities used by the callee?
