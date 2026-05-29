# src/node/

The [`src/node/`](./) directory contains code that needs to access node state
(state in `CChain`, `CBlockIndex`, `CCoinsView`, `CTxMemPool`, and similar
classes).

This directory contains node-owned state orchestration such as mempool handling.
Kernel and consensus code should not depend on relay policy or mempool
bookkeeping here.
