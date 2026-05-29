# Consensus Design

This document describes the consensus layout in this branch.

It is a current-state guide for developers. It explains the terminology used in
this branch, where the code lives, what the consensus API looks like, and which
files show the intended design.

For rationale and examples, see
[consensus-refactor-rationale.md](consensus-refactor-rationale.md).

## Summary

The consensus path is split into explicit stages:

```text
structural -> contextual -> spend -> commit
```

The design rule is simple: consensus code should receive explicit inputs and
return explicit results. It should not read global node state, mutate chainstate
directly, log, use clocks, write files, or update Core validation-state
objects.

Production validation still enters through Core validation code. The
consensus-facing rule code and interfaces live in `src/consensus`.
Core-specific adapters stay outside that directory.

## Terms

### Consensus

Consensus code decides whether protocol data satisfies Bitcoin consensus rules.

Consensus code may use protocol data types such as `CBlock`, `CTransaction`,
and `CTxOut`. It should not depend on Core node implementation types such as
`Chainstate`, `ChainstateManager`, `CBlockIndex`, `CCoinsViewCache`,
`BlockValidationState`, `TxValidationState`, `cs_main`, logging, networking, or
storage.

New consensus-facing helpers should live in the `Consensus` namespace. Some
older global helpers remain as Core compatibility wrappers.

### Validation

Validation is Core's chain-acceptance integration layer around consensus.

Validation owns chainstate ordering, indexes, block storage adapters, undo
writes, locks, script caches, logging, and error mapping. It calls consensus
code and maps consensus results back into existing Core behavior.

Mempool admission is separate. It uses consensus transaction helpers and local
policy rules, but it owns relay policy, package policy, fee policy, resource
limits, and mempool-specific caching.

Core's default validation runtime may use LevelDB coins, block files, and the
current block index. Those are adapter implementations, not consensus
requirements.

### Chain Validation Service

`ChainValidationService` is the current service boundary for block and header
admission. Node, kernel, mining, and tests should call this service instead of
calling legacy block-validation entry points directly.

The service still wraps Core's existing `ChainstateManager`. It is a boundary
around current validation behavior, not a standalone consensus API.

### Node

Node code owns P2P, relay policy, mempool policy, RPC-facing behavior, process
lifecycle, and orchestration.

Mempool state is node state. Chain validation may request mempool side effects
through `ChainstateMempoolSync`, but the concrete implementation lives in
`node::MempoolChainSync`.

### Kernel

Kernel is Core's embeddable runtime API. It wires chain validation to Core's
default runtime services and storage adapters.

The public kernel API should not expose node-only concepts such as mempool
relay, P2P peers, or RPC behavior. Core's implementation may still use LevelDB,
block files, and the current block index internally.

### Storage Adapters

Storage adapters are validation-layer capabilities for reading and writing
Core's current block storage and block index.

`BlockDataStore`, `BlockIndexView`, and `BlockIndexStore` are not consensus
interfaces. They make Core storage effects explicit while preserving current
behavior.

### Spend Backend

A spend backend provides the UTXO-dependent state needed by the spend stage.

The production backend adapts Core's `CCoinsViewCache`. The snapshot backend is
a map-backed implementation used by tests and conformance fixtures.

### Script Checker

A script checker receives script-check plans from consensus spend validation
and decides how to execute them.

The production checker uses Core's script-check queue and validation cache.
Tests can use a direct checker.

### Commit

Commit is the boundary where validated effects become mutation.

Consensus produces effects. Commit code applies those effects through explicit
interfaces.

## Directory Map

Consensus rule code:

```text
src/consensus/amount.{h,cpp}
src/consensus/block_check.{h,cpp}
src/consensus/block_spend.{h,cpp}
src/consensus/block_commit.{h,cpp}
src/consensus/block_consensus_pipeline.{h,cpp}
src/consensus/block_facts.{h,cpp}
src/consensus/coin_effects.{h,cpp}
src/consensus/locktime.{h,cpp}
src/consensus/pow.{h,cpp}
src/consensus/predicates.{h,cpp}
src/consensus/script_checker.{h,cpp}
src/consensus/sequence_locks.{h,cpp}
src/consensus/sigops.{h,cpp}
src/consensus/snapshot_spend_state.{h,cpp}
src/consensus/spend_state.h
src/consensus/tx_check.{h,cpp}
```

Core adapters:

```text
src/block_script_check_adapters.{h,cpp}
src/block_data_adapters.{h,cpp}
src/block_data_admission.{h,cpp}
src/block_index_adapters.{h,cpp}
src/block_validation_adapters.h
src/block_validation_policy.{h,cpp}
src/block_coin_effects.{h,cpp}
src/chain_validation.{h,cpp}
src/chainstate_mempool_sync.h
src/coins_view_spend_state.{h,cpp}
src/core_block_commit_adapters.{h,cpp}
src/core_block_connection_attempt.{h,cpp}
src/script_validation.h
src/sequence_locks_adapters.{h,cpp}    legacy tx verification adapters
src/tx_check_adapters.{h,cpp}
src/node/mempool_chain_sync.{h,cpp}
```

Main tests:

```text
src/test/consensus_block_check_tests.cpp
src/test/consensus_amount_tests.cpp
src/test/consensus_block_spend_tests.cpp
src/test/consensus_block_commit_tests.cpp
src/test/consensus_block_consensus_pipeline_tests.cpp
src/test/consensus_api_consumer_tests.cpp
src/test/consensus_conformance_tests.cpp
src/test/consensus_library_boundary.cpp
src/test/consensus_snapshot_spend_state_tests.cpp
src/test/util/consensus_fixture.{h,cpp}
```

`src/test/consensus_library_boundary.cpp` is the boundary test. It catches
accidental dependencies from the consensus target back into Core node code.
`src/consensus/api.h` is the extraction-facing entry point for new library
callers. It includes the staged validation API, spend-state interfaces, script
checking, effects, commit interfaces, and fixture backends.
`consensus_source_boundary` also checks public consensus headers. Public headers
may include consensus headers, standard library headers, and the small set of
protocol value headers currently accepted as part of the internal consensus API.
The public protocol header set is:

```text
primitives/block.h
primitives/transaction.h
script/verify_flags.h
uint256.h
```

Support-only consensus headers may use additional internal protocol helpers,
such as `arith_uint256.h`, but public consensus API headers may not expose
those helpers.
It classifies every `src/consensus/*.h` file as public API or support-only,
requires `consensus/api.h` to match the public API allowlist, and prevents
public API headers from exposing support-only headers.
`src/test/consensus_api_consumer_tests.cpp` is the external-consumer smoke
test. It may include only `consensus/api.h` from the consensus directory and
implements an independent spend backend, script checker, and commit adapter.
It builds as `test_consensus_api_consumer`, which directly links only
`bitcoin_consensus` and Boost headers. The `bitcoin_consensus` target publishes
its build include directories and C++20 requirement. `core_interface` remains a
private build-policy dependency for compiling the in-tree library.
`consensus_header_self_containment` verifies that each public consensus header
can be included first in a translation unit.
Consensus implementation files are also checked against direct Core utility
includes such as `util/*` and `tinyformat.h`. They must use direct includes
from the consensus directory, standard library, or the small set of protocol and
script headers allowed at the internal consensus boundary.
The source boundary test also checks the `bitcoin_consensus` CMake target. It
fails if the target pulls in Core/node sources or links non-consensus
dependencies. The header contract is listed in
`cmake/BitcoinConsensusApi.cmake`; the boundary test checks the source tree
against that manifest.
The same manifest defines `BITCOIN_CONSENSUS_INSTALL_HEADERS`, the future
install/export header set. It is the complete local header closure of
`consensus/api.h`: public consensus API headers, public protocol value headers,
and the value-header dependencies they include. It intentionally excludes
support-only consensus headers and internal protocol helpers.

The `bitcoin_chain_validation` target owns Core's current chain-validation
sources at this boundary. `bitcoin_node` and
`bitcoinkernel` link that target instead of compiling those sources directly.
`bitcoin_chain_validation` links `bitcoin_consensus`, so production validation
uses the same internal consensus target checked by the boundary tests.

Conformance fixtures model spend state with `Consensus::CoinSnapshot`. The Core
fixture adapter converts that portable shape to `Coin` only when loading a Core
UTXO cache.

## Design Shape

The important production flow is:

```text
node or kernel caller
  -> ChainValidationService
  -> Core validation setup
  -> consensus stages
  -> Core adapters
  -> Core storage and chainstate mutation
```

The important consensus flow is:

```text
structural checks
  -> contextual checks
  -> spend validation
  -> script check completion
  -> explicit effects
  -> commit interfaces
```

The important dependency rule is:

```text
node -> chain validation -> consensus
kernel -> chain validation -> consensus
Core storage adapters -> Core storage
consensus -/-> node, kernel, chainstate, mempool, storage, logging, clocks
```

The important side-effect rule is:

```text
consensus returns results and effects
validation maps those results to Core state
adapters perform storage, cache, mempool, and block-index effects
```

## Important Examples

Use these files as examples of the design.

`src/block_validation.cpp`

- Shows production validation entering the staged consensus path.
- Builds Core-derived context and options.
- Maps consensus errors back to `BlockValidationState`.
- Keeps existing Core validation behavior at the adapter boundary.

`src/core_block_connection_attempt.{h,cpp}`

- Shows the explicit spend path:
  `ValidateAndStageSpend()`, `CompleteSpendStage()`,
  `WriteUndoAndCommitSpendState()`, and `CommitBlockIndex()`.

`src/core_block_commit_adapters.{h,cpp}`

- Turns consensus spend effects into Core undo data, coins-cache mutation, and
  block-index metadata updates.

`src/block_data_admission.{h,cpp}`

- Shows block-data admission as a value-based policy function with named
  outcomes.

`src/block_data_adapters.{h,cpp}` and `src/block_index_adapters.{h,cpp}`

- Show Core block storage and block-index access behind narrow validation
  capabilities.

`src/chain_validation.{h,cpp}`

- Shows the service boundary used by node, kernel, mining, and tests.

`src/chainstate_mempool_sync.h` and `src/node/mempool_chain_sync.{h,cpp}`

- Show node-owned mempool side effects behind an explicit chainstate sync
  capability.

`src/consensus/snapshot_spend_state.{h,cpp}`

- Shows an alternate spend-state backend that does not depend on Core's UTXO
  cache implementation.

`src/test/consensus_api_consumer_tests.cpp`

- Shows how an external-style consumer can include `consensus/api.h`, provide
  its own spend backend, script checker, and commit adapters, and run the
  consensus API without Core validation headers.

## Stage API

Stages are represented by `BlockConsensusStage`:

```cpp
enum class BlockConsensusStage {
    Structural,
    Contextual,
    Spend,
    Commit,
};
```

Stage functions return `Consensus::Expected<T, E>`:

```cpp
template <typename T>
using BlockConsensusStageResult =
    Consensus::Expected<T, BlockConsensusStageError>;
```

Errors are values:

```cpp
struct BlockConsensusStageError {
    BlockConsensusStage stage;
    std::optional<BlockConsensusIssue> issue;
    std::string reject_reason;
    std::string debug_message;
};
```

Example caller shape:

```cpp
auto effects = Consensus::ValidateBlockPrecommitStages(...);
if (!effects) {
    const auto& error = effects.error();
    // Core maps this to BlockValidationState outside src/consensus.
    return false;
}
```

The stages can also be read as dependency ordering:

```text
unordered         structural
partially ordered contextual
fully ordered     spend and commit
```

This is only a model for reasoning about dependencies. The code keeps Bitcoin
Core's existing stage names.

## Structural Stage

Structural checks need only the block and structural options. They do not need
chain context or UTXO state.

Examples:

- header proof of work
- Merkle root
- block body shape
- transaction structure

API:

```cpp
Consensus::BlockStructuralConsensusOptions options{
    .check_merkle_root = true,
};

Consensus::BlockStructuralValidationView view{
    .header = header,
    .transactions = transactions,
    .facts = structural_facts,
};

auto result = Consensus::ValidateBlockStructuralStage(view, options);
```

Lower-level helpers:

```cpp
auto facts = Consensus::ComputeBlockStructuralFacts(
    transactions,
    stripped_block_size);
auto merkle = Consensus::CheckBlockMerkleRoot(header, facts);
auto body = Consensus::CheckBlockTransactions(transactions, facts);
```

Production validation still uses the `CheckBlock()` wrapper in
`src/block_validation.cpp`. That wrapper keeps Core-specific behavior such as
`CBlock::fChecked`, signet solution checks, and `BlockValidationState` mapping.
`CBlock` overloads remain as compatibility wrappers; view overloads are the
library boundary.

## Contextual Stage

Contextual checks need chain context, but not UTXO state.

Examples:

- previous median time past
- previous block time
- future block time limit
- buried deployment state
- coinbase height
- final transactions
- witness commitment expectations

API:

```cpp
Consensus::BlockContextualConsensusOptions options{
    .header = Consensus::BlockContextualHeaderOptions{
        .block_height = height,
        .difficulty_adjustment_interval = difficulty_adjustment_interval,
        .previous_median_time_past = previous_mtp,
        .previous_block_time = previous_time,
        .max_block_time = max_block_time,
        .height_in_coinbase_active = height_in_coinbase_active,
        .der_signature_active = der_signature_active,
        .cltv_active = cltv_active,
    },
    .body = Consensus::BlockContextualBodyOptions{
        .transactions = Consensus::BlockContextualTransactionOptions{
            .block_height = height,
            .locktime_cutoff = locktime_cutoff,
            .enforce_coinbase_height = height_in_coinbase_active,
        },
        .expect_witness_commitment = segwit_active,
    },
};

auto result = Consensus::ValidateBlockContextualStage(block, options);
```

Core builds these options from a header-context snapshot:

```cpp
auto headers = BuildCoreBlockHeaderContext(chainman, pindex_prev);

auto header_options =
    Consensus::BuildBlockContextualHeaderOptions(
        headers,
        chainman.GetConsensus(),
        max_future_block_time);

auto body_options =
    Consensus::BuildBlockContextualBodyOptions(block, headers);
```

`BuildCoreBlockHeaderContext()` lives outside `src/consensus` because it reads
`ChainstateManager` and `CBlockIndex`. Consensus code receives only the copied
values in `Consensus::BlockHeaderContext`; rule code reads those values through
accessors instead of mutating a public context bag.

## Spend Stage

Spend checks are UTXO-dependent. They validate inputs, calculate fees, calculate
sigop cost, build script-check plans, and stage coin effects for later
transactions in the same block.

The main interfaces are:

```cpp
class SpendStateView {
public:
    virtual bool HaveCoin(const COutPoint& outpoint) const = 0;
    virtual std::optional<CoinSnapshot> GetCoin(
        const COutPoint& outpoint) const = 0;
};

class BlockSpendWorkspace {
public:
    virtual const SpendStateView& StagedSpendView() const = 0;
    virtual const SequenceLockTimeView& SequenceLockTimes() const = 0;
    virtual BlockSpendResult<void> StageTransactionEffectsForIntraBlockView(
        const TransactionCoinEffects& effects,
        unsigned int transaction_index) = 0;
};

class BlockScriptChecker {
public:
    virtual bool WantsChecks() const { return true; }

    virtual BlockSpendResult<void> Check(
        const TransactionScriptCheckPlan& check) = 0;
    virtual BlockSpendResult<void> Complete() = 0;
};
```

`CoinSnapshot` includes only the coin output, height, and coinbase flag. BIP68
time-based sequence-lock data comes through `SequenceLockTimeView`, so coin data
and chain time lookup remain separate at the consensus boundary.

The main result is `BlockSpendEffects`:

```cpp
struct BlockSpendEffects {
    std::vector<TransactionCoinEffects> transaction_effects;
    CAmount fees;
    int inputs;
    int64_t sigop_cost;
};
```

Example:

```cpp
Consensus::BlockConsensusContext context{
    .spend = Consensus::BlockSpendContext{
        .block_height = height,
        .previous_median_time_past = previous_mtp,
    },
    .commit = Consensus::BlockCommitContext{.new_best_block = block_hash},
    .block_subsidy = block_subsidy,
};

Consensus::BlockConsensusPipeline pipeline{block, context};

auto effects = pipeline.ValidateAndCompleteSpendStage(
    workspace,
    script_checker,
    spend_options);
```

`ValidateAndCompleteSpendStage()` validates and stages transaction effects,
checks the coinbase reward, and completes queued script checks.

`ValidateAndStageSpend()` exists for Core's `ConnectBlock()` timing split. Core
can measure spend staging before waiting for queued script checks.

## Commit Stage

The commit stage applies validated effects through explicit interfaces:

```cpp
class BlockSpendStateCommitter {
public:
    virtual BlockCommitResult<void> CommitSpendState(
        const BlockCommitContext& context,
        const BlockSpendEffects& effects) = 0;
};

class BlockRevertDataWriter {
public:
    virtual BlockCommitResult<void> WriteBlockRevertData(
        const BlockCommitContext& context,
        const BlockSpendEffects& effects) = 0;
};

class BlockMetadataCommitter {
public:
    virtual BlockCommitResult<void> CommitBlockMetadata(
        const BlockCommitContext& context,
        const BlockSpendEffects& effects) = 0;
};
```

API:

```cpp
auto commit = Consensus::CommitBlockStageEffects(
    commit_context,
    *effects,
    revert_data_writer,
    spend_state_committer,
    metadata_committer);
```

Core uses:

```cpp
CoreBlockConnectionAttempt connection_attempt{
    block,
    *pindex,
    view,
    block_store,
    block_index_store,
    spend_workspace,
    spend_state_committer,
    consensus_context,
    spend_options,
};
```

The important point: consensus produces `BlockSpendEffects`; Core chooses how
to persist them.

## Full Staged API

Tests and conformance fixtures can run the full staged API:

```cpp
Consensus::BlockPrecommitValidationView view{
    .header = header,
    .transactions = transactions,
    .facts = facts,
};

auto effects = Consensus::ValidateAndCommitBlockStages(
    view,
    structural_options,
    contextual_options,
    consensus_context,
    workspace,
    script_checker,
    spend_options,
    revert_data_writer,
    spend_state_committer,
    metadata_committer);
```

Use this API when a test should cover the complete staged flow. Use lower-level
helpers when testing one rule.

## Production ConnectBlock Flow

`ConnectBlock()` currently uses the spend pipeline inside Core's existing
validation flow.

Current shape:

```text
Core validation setup
  -> compute script flags, locktime flags, and historical rule options
  -> create Core script checker over CCheckQueue and validation cache
  -> build consensus context from copied Core header values
  -> create CoreBlockConnectionAttempt
  -> CoreBlockConnectionAttempt::ValidateAndStageSpend()
  -> CoreBlockConnectionAttempt::CompleteSpendStage()
  -> CoreBlockConnectionAttempt::WriteUndoAndCommitSpendState()
  -> CoreBlockConnectionAttempt::CommitBlockIndex()
```

Structural validation uses `ValidateBlockStructuralStage()` through Core's
`CheckBlock()` wrapper. Contextual body validation uses
`ValidateBlockContextualBodyStage()` through Core's `ContextualCheckBlock()`
wrapper.

Header admission adapts Core's block-index view into
`DifficultyAdjustmentContext`, computes expected difficulty through
`Consensus::GetNextWorkRequired()`, then uses
`CheckBlockHeaderAdmissionRules()`. The full
`ValidateBlockContextualStage()` helper is available for tests and callers that
have both header and body context in one place. The Core wrappers keep existing
behavior around cache flags, signet checks, locks, logging, and validation-state
mapping.

Assumevalid is Core validation policy, not consensus policy. Core evaluates it
when constructing the production `BlockScriptChecker`; consensus always
uses the checker capability to decide whether non-coinbase script work should
be planned.
Core also builds the rest of `BlockSpendConsensusOptions`, including historical
no-overwrite applicability and deployment-derived locktime and script flags.
Core constructs `BlockConsensusContext` from copied header context and block
subsidy before entering the spend pipeline. Subsidy calculation itself is a
consensus helper (`Consensus::CalculateBlockSubsidy`); the older global
`GetBlockSubsidy` wrapper remains for Core callers.

## Spend Workspaces

The spend stage uses a per-attempt workspace.

The parent spend state is not mutated while validation is running. Each block
attempt gets a workspace:

```cpp
auto workspace = spend_backend.BeginBlockSpend(
    spend_context);
```

Transactions read from the workspace's staged view:

```cpp
const Consensus::SpendStateView& view = workspace->StagedSpendView();
```

After each transaction, consensus stages that transaction's coin effects into
the workspace:

```cpp
workspace->StageTransactionEffectsForIntraBlockView(
    coin_effects,
    transaction_index);
```

This makes intra-block spends visible to later transactions without committing
anything to the parent chainstate.

Current backends:

- `Consensus::CoinsViewBlockSpendBackend`: Core adapter over `CCoinsViewCache`
- `Consensus::SnapshotSpendState`: map-backed backend for tests and fixtures

The backend interface is the main extension point for alternate state
accumulation models.

## Script Checking

Spend validation builds script-check plans:

```cpp
struct TransactionScriptCheckPlan {
    CTransactionRef tx;
    script_verify_flags flags;
    std::vector<CTxOut> spent_outputs;
};
```

The checker decides how to execute them:

```cpp
class BlockScriptChecker {
public:
    virtual BlockSpendResult<void> Check(
        const TransactionScriptCheckPlan& check) = 0;

    virtual BlockSpendResult<void> Complete() = 0;
};
```

Current checkers:

- `Consensus::DirectBlockScriptChecker`: runs checks directly in tests
- `CoreBlockScriptChecker`: uses Core's script check queue

This keeps script execution policy outside spend accounting. It also allows
future scheduling experiments without changing spend-rule code.

## Effects

Consensus returns effects instead of mutating global state directly.

Transaction effects:

```cpp
struct TransactionCoinEffects {
    std::vector<SpentCoinEffect> spends;
    std::vector<CreatedCoinEffect> creates;
};
```

Block effects:

```cpp
struct BlockSpendEffects {
    std::vector<TransactionCoinEffects> transaction_effects;
    CAmount fees;
    int inputs;
    int64_t sigop_cost;
};
```

Core turns those effects into undo data and cache mutation in
`src/block_coin_effects.cpp` and `src/core_block_commit_adapters.cpp`.

## Why This Design

### Local Reasoning

Rule dependencies are visible in function signatures.

```cpp
CheckBlockFutureTime(block, max_block_time);
```

This is easier to reason about than a function that reads time from a global
clock or reaches through `ChainstateManager`.

At the Core adapter boundary, `BlockValidationTime` carries current time in
seconds and the derived future block time limit into header admission, block
admission, block processing, and test validation.

Core callers enter chain validation through `ChainValidationService`. This keeps
the validation capability explicit at call sites while the underlying
implementation still uses `block_validation.cpp`.

### Testability

Rules can be tested with small inputs:

```cpp
Consensus::SnapshotSpendState spend_state;
spend_state.AddCoin(prevout, coin);

auto workspace = spend_state.BeginBlockSpend(context);
```

No block database, chainstate, or node process is required.

### Backend Flexibility

Consensus depends on `SpendStateView`, `BlockSpendWorkspace`, and
`BlockSpendStateCommitter`, not directly on `CCoinsViewCache`.

This is the path for experimenting with different state accumulation models.

### Scheduler Flexibility

Consensus builds script-check plans and calls a `BlockScriptChecker`
interface. The checker can run checks directly, use Core's current queue, or
use a different scheduler.

### Behavior Preservation

Core-specific adapters preserve existing behavior and failure reasons while the
internal code moves toward explicit stages.

Examples:

- consensus errors are mapped to `BlockValidationState` in Core code
- script cache behavior stays in Core's script checker adapter
- undo and block-index writes stay in Core commit adapters

## Important Files

Start here:

- `src/consensus/block_consensus_pipeline.h`
- `src/consensus/block_spend.h`
- `src/consensus/block_commit.h`
- `src/consensus/spend_state.h`
- `src/block_validation.cpp`

Structural and contextual rules:

- `src/consensus/block_check.h`
- `src/consensus/block_facts.h`
- `src/consensus/locktime.h`
- `src/consensus/pow.h`
- `src/consensus/sigops.h`
- `src/consensus/tx_check.h`

Spend and effects:

- `src/consensus/block_spend.h`
- `src/consensus/coin_effects.h`
- `src/consensus/sequence_locks.h`
- `src/block_coin_effects.h`
- `src/coins_view_spend_state.h`

Adapters:

- `src/block_validation_adapters.h`
- `src/coins_view_spend_state.h`
- `src/sequence_locks_adapters.h`
- `src/tx_check_adapters.h`

Tests:

- `src/test/consensus_block_consensus_pipeline_tests.cpp`
- `src/test/consensus_block_spend_tests.cpp`
- `src/test/consensus_conformance_tests.cpp`
- `src/test/consensus_library_boundary.cpp`
- `src/test/util/consensus_fixture.{h,cpp}`

## Adding New Code

### Add a Structural Rule

Put pure block/header checks in `src/consensus/block_check.{h,cpp}`.

Prefer explicit inputs:

```cpp
BlockCheckResult<void> CheckNewRule(
    std::span<const CTransactionRef> transactions,
    int explicit_rule_input);
```

Avoid Core context in consensus code:

```cpp
bool CheckNewRule(const CBlock& block, const ChainstateManager& chainman);
```

Add focused tests in `src/test/consensus_block_check_tests.cpp`.

### Add a Contextual Rule

Add the needed input to `BlockContextualHeaderOptions` or
`BlockContextualTransactionOptions`.

Adapt Core state outside `src/consensus`:

```cpp
Consensus::BlockContextualHeaderOptions options{
    .block_height = header_context.block_height,
    .previous_median_time_past = header_context.previous_median_time_past,
};
```

### Add a Spend Rule

Put UTXO-dependent checks in `src/consensus/block_spend.{h,cpp}`.

Read coins through `SpendStateView`:

```cpp
auto coin = spend_state.GetCoin(txin.prevout);
if (!coin) {
    return Consensus::Unexpected<BlockSpendError>{...};
}
```

Represent state changes as effects. Do not mutate `CCoinsViewCache` directly
from `src/consensus`.

### Add a Spend Backend

Implement `Consensus::BlockSpendBackend` and return a
`Consensus::BlockSpendWorkspace`:

```cpp
class MySpendBackend final : public Consensus::BlockSpendBackend {
public:
    Consensus::BlockSpendResult<std::unique_ptr<Consensus::BlockSpendWorkspace>>
    BeginBlockSpend(
        const Consensus::BlockSpendContext& context) override;
};
```

The workspace should provide a staged view. It should not mutate parent state
until the commit boundary.

### Add a Script Execution Model

Implement `Consensus::BlockScriptChecker`:

```cpp
class MyScriptChecker final : public Consensus::BlockScriptChecker {
public:
    Consensus::BlockSpendResult<void> Check(
        const Consensus::TransactionScriptCheckPlan& check) override;

    Consensus::BlockSpendResult<void> Complete() override;
};
```

`Check()` may execute immediately or enqueue work. `Complete()` must return the
final script result before the spend stage is complete.

## Current Limitations

This branch is not yet a standalone external consensus library.

Current limitations:

- consensus APIs still use Core protocol types such as `CBlock` and
  `CTransaction`
- production adapts header history from Core's block-index view
- Core still owns validation orchestration, locks, block storage, undo writes,
  and chainstate persistence
- mempool admission has its own in-tree target and result vocabulary
  (`MempoolValidationState`)
- LevelDB coins and block-file storage are not yet behind stable kernel storage
  interfaces
- alternate spend-state backends are enabled by interfaces, but only the Core
  and snapshot backends exist today
- script scheduling is abstracted, but Core's production path still uses the
  existing script-check queue

These are intentional migration points. The current design makes the boundaries
explicit without requiring a full rewrite.

## Review Checklist

When reviewing consensus changes on this branch, check:

- boundary guardrails still cover dependency direction
- `src/consensus` avoids Core node types and side effects
- rule inputs are explicit
- failures return `Consensus::Expected` errors instead of mutating validation
  state
- spend reads go through `SpendStateView`
- coin mutations are represented as effects before commit
- script checks are planned separately from spend accounting
- Core-specific details stay in adapters
- focused unit tests cover new helpers
- `consensus_library_boundary` and `consensus_source_boundary` still pass

For performance-sensitive changes, run `bench_bitcoin` on the relevant
benchmarks, especially `ConnectBlock*`.
