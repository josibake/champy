# Legacy Compatibility Ledger

This file tracks compatibility surfaces that remain only because the refactor is
staged.

Do not build new architecture around these shapes unless the compatibility
reason still applies.

## Core Runtime Capabilities

Some validation requests still carry broad Core objects or runtime capabilities
such as `CBlockIndex`. `ChainValidationService` still wraps
`ChainstateManager`, but internal block/header admission now receives a
`CoreChainValidationContext`.
Header and block admission results return copied snapshots, not live
`CBlockIndex` pointers.

Current role:

- preserve Core behavior while moving orchestration behind explicit requests
- keep storage, coins, and block-index mutation in existing implementations

Target:

- replace broad objects with narrower capabilities where practical
- pass copied facts for read-only context
- keep live mutable Core objects only at commit boundaries

The block connection engine no longer receives broad storage/index stores. It
receives `BlockUndoWriter` and `BlockIndexValidityCommitter` for commit effects.
It also receives block-local spend state through `BlockConnectionState` instead
of a raw `CCoinsViewCache`.
Other validation paths still use broader adapters while admission, replay, and
verification are being kept behavior-compatible.
`VerifyDBRequest` lives in `validation/verify_db.h` because database
verification still carries Core coins views for replay checks; do not treat
that request as a general chainstate API.

Validation-interface notifications are emitted through `ValidationEventQueue`.
The remaining direct `ValidationSignals` use in chainstate is queue backpressure
compatibility (`LimitValidationInterfaceQueue`), not event publication.

`CoreValidationCommitExecutor` currently wraps the existing chainstate mutex and
`cs_main` locking model. It is an explicit compatibility boundary, not a new
lock design.

## `cs_main`

`cs_main` still protects broad parts of chain validation.

Current role:

- preserve Core's active-chain and block-index synchronization model
- keep behavior stable while validation is being reorganized

Target:

- gather chain facts under narrow locked sections
- run consensus-facing validation without broad locks where possible
- serialize commit explicitly
- publish node events after commit and outside broad locks where possible

`ChainstateManager::ActiveChain()` still exposes the mutable `CChain` for
legacy callers. `ChainstateManager::m_best_header` is also still exposed as a
mutable block-index pointer. New read-only callers should prefer copied
active-tip, best-header, and block-index query snapshots. New active-tip or
best-header mutations should go through the ChainstateManager helpers so
snapshots stay synchronized with the committed Core state.

## `ChainstateEventSink`

`ChainstateEventSink` is a generic validation-to-node event boundary, but its
current event set still reflects Core's mempool repair path. The interface no
longer passes `Chainstate`, `CCoinsViewCache`, or a node-owned mutex;
node-owned implementations bind any Core state they need at construction.

Current role:

- keep mempool repair behavior stable during chain activation and invalidation
- buffer activation reorg events under `cs_main`, then apply them under the
  node event sink before `cs_main` is released
- buffer invalidation repair events and let node apply each repair batch
- let validation report chain events without owning mempool policy
- let the node event sink lock `CTxMemPool::cs` internally

Target:

- replace mempool-specific event values with explicit execution/commit contracts
- publish node events from a narrower post-commit boundary
- keep mempool-specific replay and repair entirely in node orchestration

Straight connects no longer hold the mempool lock across block connection.
Activation reorgs use an in-memory event batch instead of holding the mempool
lock across script work. Invalidation now batches each disconnect-and-repair
step, but it still runs synchronously under `cs_main` until node owns an
explicit reorg repair executor or epoch.

## Legacy Script Reject Reasons

`block_script_check_adapters.cpp` still preserves Core's existing
`mempool-script-verify-flag-failed` reject reason when standard script flags
fail. The name is a relay artifact, but changing it would alter observable
validation strings. Remove it only with an explicit compatibility decision at
the adapter boundary.

## Block Data Admission

`AcceptBlock` still accepts forced block-data storage through
`BlockAcceptanceOptions::block_data_storage`.

Current role:

- preserve block download behavior and `getblockfrompeer` compatibility
- decide whether block bytes should be stored before chain activation

Target:

- replace `BlockDataStorageMode::ForceStore` with a validation-facing
  chain-candidate query where possible
- keep download/orphan/peer policy in node orchestration

## Mixed Storage Flush

`FlushStateToDisk()` still flushes block storage and chainstate storage through
one Core runtime operation. Validation reaches it through
`ChainstateManager::FlushActiveChainstateToDisk()`.

Current role:

- preserve pruning and cache-flush behavior while block storage remains Core's
  default implementation

Target:

- let alternate storage implementations make equivalent flush decisions
  without changing consensus code

## Script Cache Locking

Script-cache lookup no longer requires `cs_main`. `ValidationCache` owns a
dedicated script-execution-cache mutex, and `CoreScriptValidationCache` exposes
the narrow lookup/store capability used by script checking.

Current role:

- preserve script-cache behavior and validation-cache sharing
- keep cache lookup/store operations behind `CoreScriptValidationCache`
- keep Core's current `CCheckQueue` behind `ScriptCheckScheduler`
- keep legacy `CheckInputScripts` compatible with callers that read Core's live
  coins view under `cs_main`

Target:

- finish separating block connection so queued script completion can run outside
  broad chain locks
- keep script scheduling behind an explicit runtime capability

Queued script completion may temporarily release `cs_main` through
`CoreChainLock`. The helper uses `NO_THREAD_SAFETY_ANALYSIS` because Clang
cannot model the indirect `UniqueLock` handoff, but it still uses
`REVERSE_LOCK` so debug lock-order checks remain active.

## Roll-Forward Replay

Roll-forward replay still applies block effects without full block connection
validation so interrupted database flushes can be repaired idempotently.

Current role:

- preserve existing chainstate recovery and verification behavior
- keep the idempotent replay mutation isolated in the Core coins adapter

Target:

- keep direct UTXO mutation isolated to commit/recovery code

## Kernel Runtime

Kernel still contains Core's default block storage, chainstate loading, and
block-index-backed implementation.

Current role:

- provide an embeddable Core runtime
- preserve existing storage behavior

Target:

- keep storage out of consensus
- expose storage through narrow runtime capabilities
- allow alternate storage/state implementations without changing consensus code
