# Consensus Design

This document describes the current code shape.

For remaining compatibility surfaces, see
[legacy-compatibility.md](legacy-compatibility.md). For lock and execution
contracts, see [validation-execution-contracts.md](validation-execution-contracts.md).

## Layers

### Consensus

`src/consensus` builds as `bitcoin_consensus`.

Consensus code answers protocol-rule questions. It may use protocol value
types such as `CBlock`, `CTransaction`, `CTxOut`, `Coin`, `uint256`, and
`Consensus::Params`.

Consensus must not depend on:

- `Chainstate`, `ChainstateManager`, or `CBlockIndex`
- `CCoinsViewCache`
- mempool, P2P, RPC, wallet, or node runtime code
- logging, clocks, files, databases, or `cs_main`
- `BlockValidationState` or `TxValidationState`

Public consensus consumers include `consensus/api.h`. The installed consensus
library is experimental.

### Validation

`src/validation` builds mostly as `bitcoin_chain_validation`.

Validation integrates consensus rules with Core state. It owns:

- active-chain ordering
- block-index integration
- validation-state mapping
- lock requirements
- script-cache integration
- calls into Core storage adapters

Validation should pass explicit facts or narrow capabilities into consensus. It
should not make consensus code pull from Core runtime state.

### Core Storage Adapters

Core's default storage adapters build as `bitcoin_core_storage_adapters`.

This target adapts the current Core runtime to validation interfaces:

- `CoreBlockDataStore`: block and undo reads/writes
- `CoreBlockIndexStore`: block-index lookup, header admission, dirty marking
- `CoinsViewBlockSpendBackend`: `CCoinsViewCache` spend backend
- `CoreBlockSpendStateCommitter`: commits staged coin changes
- `block_replay`: interrupted-flush recovery and roll-forward replay

LevelDB, block files, the block index, and `CCoinsViewCache` are default Core
implementations. They are not consensus abstractions.

### Kernel

`src/kernel` builds as `bitcoinkernel`.

Kernel is Core's embeddable runtime. It may construct Core's default storage,
chainstate, params, notifications, and validation services.

Kernel must not depend on node code. Public kernel headers must not expose
mempool, P2P, RPC, relay-policy, or process-lifecycle concepts.

### Node

`src/node` and `bitcoin_node` own process-level behavior:

- P2P and IBD orchestration
- RPC-facing behavior
- mempool state and relay policy
- package policy and local transaction admission
- process args and lifecycle

Mempool code builds separately as `bitcoin_mempool_policy`. Chain validation
does not own mempool behavior.

## Validation Pattern

Block validation is organized as:

```text
structural -> contextual -> spend -> commit
```

- `structural`: checks that only need the block bytes.
- `contextual`: checks that need chain facts such as height, time, difficulty,
  deployment state, or previous headers.
- `spend`: checks UTXO-dependent validity, fees, subsidy, and scripts.
- `commit`: writes undo data, commits spend state, and updates block metadata.

The rule is: gather Core facts before calling consensus; commit Core mutations
only after validation succeeds.

## Common Code Patterns

### Value Contexts

Consensus entry points receive value-shaped context:

- `BlockConsensusContext`
- `BlockContextualConsensusOptions`
- `BlockSpendConsensusOptions`
- `BlockCommitContext`

Core-specific state is converted into these values in validation adapters.

### Spend State

Consensus reads coins through:

- `SpendStateView`
- `SequenceLockTimeView`
- `BlockSpendWorkspace`
- `BlockSpendBackend`

Production code reaches this through `BlockConnectionState`. Tests and
experiments can provide alternate backends without changing consensus rules.

### Script Checks

Spend validation creates `TransactionScriptCheckPlan` values.

Script execution is handled by `BlockScriptChecker`:

- `DirectBlockScriptChecker` is useful for tests and fixtures.
- `CoreBlockScriptChecker` adapts Core's cache and check queue.

Script execution is separate from spend accounting.

### Effects Then Commit

Spend validation records `BlockSpendEffects`. It does not directly mutate
Core's final coin state.

Commit happens through:

- `BlockRevertDataWriter`
- `BlockSpendStateCommitter`
- `BlockMetadataCommitter`

This keeps validation review separate from mutation review.

### Chain Events

Validation reports chain events through `ChainstateEventSink`.

`node::MempoolChainSync` implements mempool repair in node. Validation does not
own mempool policy or mempool state.

## Placement Rules

- Protocol rule decision: `src/consensus`
- Core chain acceptance, locks, and state mapping: `src/validation`
- Core storage implementation: `bitcoin_core_storage_adapters`
- Embeddable Core runtime: `src/kernel`
- P2P, RPC, mempool, relay, process behavior: `src/node`

If a function needs broad Core objects, first ask whether it really needs live
state or only a value snapshot.

If a function mutates state, keep that mutation at an explicit commit or
recovery boundary.

## Current Limits

`bitcoin_consensus` installs an experimental static library and public header
closure, but it is not a supported external package yet. It still needs package
export metadata, versioning, and dependency export rules.

Kernel still uses Core's default LevelDB/block-file/block-index runtime
internally. That is acceptable as a default implementation, but alternate
storage or state models should be added behind validation/kernel capabilities,
not by changing consensus rules.
