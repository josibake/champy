# Consensus Refactor Rationale

This document explains why the branch is structured this way.

The short version: consensus-critical code should be small, explicit, and
testable without a running node.

## Problem

Bitcoin Core's historical validation path mixes several concerns:

- consensus rule checks
- chainstate mutation
- block storage
- mempool repair
- script scheduling
- validation-state mapping
- logging and process-level behavior

That makes local reasoning hard. A function can appear to validate a block while
also reading global state, mutating indexes, scheduling scripts, updating
caches, or repairing the mempool.

The refactor separates those jobs without changing consensus behavior.

## Design Choice

Consensus helpers receive explicit inputs and return explicit results.

Validation adapts Core's existing state into those inputs, calls consensus, and
then commits the result.

Node-owned behavior stays in node. Kernel-owned behavior is limited to Core's
embeddable runtime. Consensus does not depend on either.

## Example: Spend Then Commit

The clearest example is block spend validation.

Spend validation now reads UTXOs through `SpendState`, requests script work
through `BlockScriptChecker`, and records spend/create operations as
`CoinEffects`.

Only after validation succeeds does commit code apply those effects.

This gives reviewers a useful split:

- spend validation decides whether the block is valid
- commit code mutates Core state

Tests can exercise spend validation with an in-memory spend backend instead of
building a full node.

## Example: Mempool Outside Kernel

Mempool state is not consensus and is not kernel runtime state.

Validation reports chain events through `ChainstateMempoolSync`. The concrete
implementation, `node::MempoolChainSync`, owns mempool repair and
disconnected-transaction staging.

This keeps kernel usable without node and prevents mempool policy from becoming
part of the embeddable runtime API.

## Review Standard

When reviewing new code in this area, ask:

- Is this a protocol rule, Core integration, kernel runtime behavior, or node
  behavior?
- Are all inputs explicit?
- Is mutation delayed until the commit step?
- Is script execution separate from spend accounting?
- Is storage access behind a validation or kernel capability?
- Does consensus avoid Core runtime types?
- Does kernel avoid node dependencies?

If the answer is unclear, the code probably belongs behind a smaller interface
or in a different layer.
