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
- use the script-check scheduler capability supplied by the runtime

`ChainValidationService` is the Core-facing adapter. Internal block/header
admission receives `CoreChainValidationContext`, which names the Core runtime
operations validation currently needs instead of passing `ChainstateManager`
through the validation path.

Script and spend policy receive the block's contextual deployment state as an
input. They should not query deployment state through `ChainstateManager`.

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

Current `cs_main` users must fit one of these roles:

- active-chain orchestration: `ActivateBestChain`, `InvalidateBlock`, and
  `CoreChainActivationState`
- block-index mutation: `CoreBlockIndexStore` dirty marking, validity updates,
  and candidate maintenance
- chainstate commit: `BlockConnectionState`, undo writes, coin commits, and
  best-block updates
- storage coordination: legacy mixed block/coins flush decisions

`cs_main` must not be used as a general runtime mutex. New code should name the
role it needs and prefer a copied value or narrow capability over a broad
locked object.

### Lock Role Map

`cs_main` currently combines several roles that should be split in stages:

- active-chain view: current tip, height, ancestry, and reorg path selection
- block-index metadata: validity flags, sequence ids, dirty marking, and best
  header selection
- chainstate commit: best block, UTXO commit, undo commit, active-tip update,
  and failure-state updates
- storage coordination: block files, undo files, pruning, coins flush, and
  reindex/replay repair
- validation event ordering: preserving Core's current callback order while
  node-owned event sinks are moved out of broad locks

The first physical split is active-chain snapshots. A snapshot copies stable
facts such as height and block hash. It must not expose mutable `CBlockIndex`
state unless the caller also holds the lock that protects that state.

Later splits should follow this order:

- separate copied active-chain reads from active-chain mutation
- separate block-index read facts from block-index mutation
- keep chainstate mutation behind commit capabilities
- move storage flush and pruning coordination behind runtime capabilities
- move validation event publication to an executor-owned boundary

When more than one lock is needed, acquire locks in this order:

```text
cs_main -> active-chain snapshot lock -> node event sink lock -> mempool lock
```

Snapshot-only readers may take the active-chain snapshot lock without
`cs_main`. They must return copied values only.

Do not use `cs_main` for script-cache lookup, queued script execution, mempool
locking, or event-sink synchronization.

`ValidationCache` protects script-execution-cache entries with its own mutex.
Signature-cache entries are protected by `SignatureCache` internally. Script
cache lookup/store must go through `CoreScriptValidationCache`; callers should
not use `cs_main` only to access the cache.

`CoreChainLock` is the only validation capability that may temporarily release
`cs_main`. It must only be used when `cs_main` is the most recently acquired
lock. The current use is queued script completion during straight block
connection; activation reorg repair disables this release and buffers node
events until the chain transition reaches a stable point.

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

Connecting a block to the active chain tip is represented by
`CoreConnectTipRequest` plus shared `CoreConnectTipResources`. These name block
loading, block index lookup, undo writing, connection view, script-policy
logging state, event sink, signal sink, and timing counters explicitly.
The implementation is split into prepare, execute, and commit helpers: prepare
loads the block and snapshots the request, execute runs block connection, and
commit flushes the connection attempt, persists if needed, advances the tip, and
publishes the connected-block event.

One bounded active-chain activation step is represented by
`CoreActivateBestChainStepRequest`. The request carries a
`CoreChainActivationState` capability for active-chain mutation. `Chainstate`
owns the concrete state; validation owns the disconnect/connect/prune sequence
for the step.

`Chainstate::ActivateBestChain()` delegates its serialized loop to
`ChainstateActivationOrchestrator`. The orchestrator owns the current Core
activation loop shape: drain validation callbacks, enter a chain-locked
activation cycle, flush periodically, and stop on interrupt or completion.

Current block connection capabilities are intentionally narrow:

- `BlockConnectionState` owns block-local spend state and best-block mutation.
- `BlockUndoWriter` writes undo data during commit.
- `BlockIndexValidityCommitter` records validated block metadata.
- `ScriptCheckScheduler` owns the runtime queue for script work.
- `BlockScriptChecker` owns script execution, completion, and caching.
- `SpendState` owns UTXO reads for spend validation.

Do not pass `Chainstate`, `ChainstateManager`, `CoreBlockDataStore`,
`CoreBlockIndexStore`, `CCoinsViewCache`, or another broad store into the block
connection engine.

Storage and index interfaces live in `block_storage.h` and `block_index.h`.
Core implementations live in the `*_adapters.h` headers. Validation code that
only needs an interface should include the interface header, not a Core adapter.

Interrupted flush replay uses `BlockReplayRequest`: coins DB, block reader,
undo reader, block-index lookup, and notifications. Core builds that request in
`Chainstate::ReplayBlocks()`. The replay algorithm should not reach through
`Chainstate` directly.

Database verification uses `VerifyDBRequest`: `ActiveChainView`, coins views,
block storage, block index, script scheduler, validation context, and interrupt
source. The `Chainstate` overload is the Core adapter that builds this request.

Test-only block validation uses `TestBlockValidityRequest` for the same reason:
the check path names the active-chain view, header context, block-local
connection state, script checker, undo writer, and metadata committer it uses.

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

Current runtime capabilities:

- `ScriptCheckScheduler` starts a per-block script-check batch.
- `CoreChainValidationRuntime` adapts Core's current script scheduler and
  validation-event queue into explicit validation capabilities.
- `CoreValidationCommitExecutor` serializes Core chainstate activation and
  invalidation. Its current implementation is still `cs_main`-backed, but its
  entry points name the role being protected: active-chain reads, block-index
  mutation, chainstate commit, and storage coordination.
- `CoreChainLock` may release `cs_main` while waiting for queued script work.
- `ValidationEventQueue` adapts Core's validation-interface queue.

## Event Contract

Validation may publish chain events such as:

- block connected
- block disconnected
- reorg completed

Those events are not consensus effects. They are runtime notifications used by
node features such as mempool repair.

Event sinks should be called after the corresponding chainstate mutation is
committed. Validation passes value batches to the event sink. Node-owned event
sinks own their own synchronization; validation must not acquire node locks.
Activation reorgs buffer event values and flush them before `cs_main` is
released. This is tracked in `legacy-compatibility.md`.
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
