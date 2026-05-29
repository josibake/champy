# Consensus Refactor Rationale

This document explains why the consensus and validation refactor is useful.
It is intended as PR review context. For the current code layout, see
[consensus-design.md](consensus-design.md).

The main change is not a new consensus rule. The main change is that consensus,
validation, storage, and node policy communicate through explicit inputs,
results, and side-effect boundaries.

That makes the code easier to review because a caller can see:

- what operation is being requested
- which state may be read or mutated
- what result was produced
- which layer owns the side effect

## Example: Stored Block Data

Previous shape:

```text
ProcessNewBlock(..., bool* new_block)
  -> AcceptBlock(..., bool* fNewBlock)
```

`AcceptBlock()` returned a single `bool` and separately wrote `fNewBlock`.
That output value was used by peer processing to decide whether the block had
been stored and whether peer bookkeeping should be updated.

This made the state transition harder to review because several outcomes shared
the same result channel:

- duplicate block data
- unrequested block data not stored
- validation failure
- storage failure
- newly stored block data

The new shape uses named results:

```text
BlockAcceptanceStatus::BlockDataStored
BlockAcceptanceStatus::StorageFailed
BlockAcceptanceStatus::BlockDataAlreadyKnown
BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip
...
```

`NewBlockProcessingResult::new_block()` is true only when the block data was
stored.

Why this is better:

- peer bookkeeping can depend on `BlockDataStored`, not on an out-parameter
- storage failure and duplicate data are distinct
- tests can assert the exact acceptance status
- future call sites do not need to know `AcceptBlock()` internals

## Example: Mempool Side Effects

Previous shape:

```text
Chainstate
  owns CTxMemPool*
  updates mempool during connect, disconnect, and reorg
```

That mixed chainstate transition code with node mempool policy. A reader had to
know that chain activation also updates package policy state, cached lockpoints,
reorg resurrection, size limiting, and mempool consistency checks.

The new shape:

```text
Chainstate -> ChainstateMempoolSync
node::MempoolChainSync -> CTxMemPool
```

`ChainstateMempoolSync` is an optional side-effect boundary. Node code provides
the mempool implementation.

Why this is better:

- chain validation can run without a mempool
- mempool policy remains in the node layer
- lock requirements are stated at the boundary
- callers pass the side-effect capability explicitly
- future validation code cannot update mempool state accidentally

## Example: Block Data Admission

Previous shape:

Block data admission was embedded inside `AcceptBlock()` as a sequence of
conditionals over `fRequested`, chainwork, block height, and prior block data.
The function returned `true` for several "accepted but not stored" cases.

The new shape:

```text
BlockDataAdmissionContext -> GetBlockDataAdmissionResult()
```

The possible outcomes are named:

```text
STORE_BLOCK_DATA
ALREADY_HAVE_DATA
UNREQUESTED_PREVIOUSLY_PROCESSED
UNREQUESTED_LESS_WORK_THAN_TIP
UNREQUESTED_TOO_FAR_AHEAD
UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK
```

Why this is better:

- admission policy is a small function with value inputs
- each skipped-storage case has a name
- block download policy is visible at the validation boundary
- tests can cover each branch without setting up full chainstate

## Example: Spend Validation and Commit

Previous shape:

`ConnectBlock()` validated spends, scheduled script checks, calculated fees,
built undo data, updated coins, marked the block index dirty, and set the best
block.

The new shape splits that path:

```text
ValidateAndStageSpend()
CompleteSpendStage()
WriteUndoAndCommitSpendState()
CommitBlockIndex()
```

Why this is better:

- UTXO-dependent checks happen before commit
- script check completion is a named boundary
- spend effects are explicit data
- coin mutation happens through a commit interface
- undo and block-index updates are explicit commit effects

This follows a simple transaction pattern:

```text
gather inputs -> validate -> build effects -> commit -> notify
```

## Example: Storage Access

Previous shape:

Validation code reached directly into Core storage objects such as
`BlockManager`, flatfile block storage, and block-index mutation.

The new shape uses capabilities:

```text
BlockDataStore
BlockIndexView
BlockIndexStore
CoreBlockDataStore
CoreBlockIndexStore
```

Why this is better:

- validation code asks for the storage operation it needs
- Core's LevelDB and block files remain default implementations
- alternate storage can provide the same capability
- dirty block-index updates have a named path
- tests can provide small fake stores

## Example: Consensus Boundary

Previous shape:

Because this is a monorepo, consensus code could accidentally grow dependencies
on validation, chainstate, storage, mempool, logging, or node types and still
compile.

The new shape has boundary checks for the consensus library and public kernel
API. The consensus boundary rejects Core implementation types such as:

```text
Chainstate
ChainstateManager
CBlockIndex
CCoinsViewCache
CTxMemPool
BlockDataStore
BlockIndexStore
LevelDB
cs_main
```

Why this is better:

- extraction does not rely on reviewer memory
- public headers stay self-contained
- dependency drift fails a test
- external-consumer tests prove the intended API can be used directly

## Example: Alternate Spend State

Previous shape:

Consensus-facing spend validation was tied closely to Core's UTXO cache model.
An alternate model would need to imitate `CCoinsViewCache` behavior.

The new shape introduces portable spend-state concepts:

```text
CoinSnapshot
SpendStateView
SequenceLockTimeView
SnapshotSpendState
SnapshotSpendWorkspace
```

Why this is better:

- consensus spend checks depend on spend facts, not Core cache internals
- snapshot-backed tests can exercise the same spend path
- alternate models can provide their own state accumulation
- SwiftSync, Utreexo, or other experiments do not need to use Core's database
  layout to participate in consensus validation

## Review Standard

This branch should be reviewed for behavior preservation and boundary quality.

Good signs:

- a function receives values or a narrow capability instead of a manager object
- a status enum names the outcome instead of relying on a boolean
- validation and commit are separate
- storage writes are behind explicit adapters
- node policy remains outside consensus and chain validation
- boundary tests protect the dependency direction

The goal is not to make every function pure. The goal is to make the impure
parts explicit, narrow, and testable.
