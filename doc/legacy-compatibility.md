# Legacy Compatibility Ledger

This file tracks compatibility surfaces that remain only because the refactor is
staged.

Do not build new architecture around these shapes unless the compatibility
reason still applies.

## `ConnectBlock`

`ConnectBlock` remains as the historical Core entry point used by `Chainstate`.

Current role:

- preserve existing callers
- preserve existing lock annotations and failure mapping
- delegate block connection to `validation::BlockConnectionEngine`

Target:

- callers use a validation service or engine request directly
- `Chainstate` owns active-chain state and commit coordination only
- block connection orchestration lives in validation, not in `Chainstate`

## Core Runtime Capabilities

Some validation requests still carry broad Core objects such as `Chainstate`,
`CBlockIndex`, and `CCoinsViewCache`.

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
