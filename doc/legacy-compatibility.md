# Legacy Compatibility Ledger

This file tracks compatibility surfaces that remain only because the refactor is
staged.

Do not build new architecture around these shapes unless the compatibility
reason still applies.

## Core Runtime Capabilities

Some validation requests still carry broad Core objects or runtime capabilities
such as `ChainstateManager`, `CBlockIndex`, and `CCoinsViewCache`.

Current role:

- preserve Core behavior while moving orchestration behind explicit requests
- keep storage, coins, and block-index mutation in existing implementations

Target:

- replace broad objects with narrower capabilities where practical
- pass copied facts for read-only context
- keep live mutable Core objects only at commit boundaries

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

## `ChainstateEventSink`

`ChainstateEventSink` is a generic validation-to-node event boundary, but its
current lock shape still reflects Core's mempool repair path.

Current role:

- keep mempool repair behavior stable during chain activation and invalidation
- hold `cs_main` and `CTxMemPool::cs` across legacy reorg repair
- let validation report chain events without owning mempool policy

Target:

- replace the dynamic event mutex with explicit execution/commit contracts
- publish node events from a narrower post-commit boundary
- keep mempool-specific replay and repair entirely in node orchestration

## Block Data Admission

`AcceptBlock` still carries block-download policy through
`BlockAcceptanceOptions::block_data_requested`.

Current role:

- preserve block download behavior and `getblockfrompeer` compatibility
- decide whether block bytes should be stored before chain activation

Target:

- replace the policy flag with a validation-facing chain-candidate query
- keep download/orphan/peer policy in node orchestration

## Mixed Storage Flush

`FlushStateToDisk()` still flushes block storage and chainstate storage through
one validation call.

Current role:

- preserve pruning and cache-flush behavior while block storage remains Core's
  default implementation

Target:

- move mixed storage coordination behind a ChainstateManager/runtime boundary
- let alternate storage implementations make equivalent flush decisions
  without changing consensus code

## Script Cache Locking

Script-cache lookup still requires `cs_main` because Core's CuckooCache is
externally synchronized.

Current role:

- preserve script-cache behavior and validation-cache sharing

Target:

- give script execution an explicit cache capability with its own lock contract
- remove `cs_main` from script-cache-only paths

## Roll-Forward Replay

`RollforwardBlock()` still mutates a coins cache directly when replaying block
effects during database verification.

Current role:

- preserve existing chainstate recovery and verification behavior

Target:

- reuse block-connection effects for replay
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
