# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Header sets that define the current in-tree consensus library boundary.
# Paths are relative to src/.

set(BITCOIN_CONSENSUS_PUBLIC_API_HEADERS
  "consensus/api.h"
  "consensus/amount.h"
  "consensus/block_check.h"
  "consensus/block_commit.h"
  "consensus/block_consensus_pipeline.h"
  "consensus/block_facts.h"
  "consensus/block_spend.h"
  "consensus/coin_effects.h"
  "consensus/consensus.h"
  "consensus/diagnostics.h"
  "consensus/expected.h"
  "consensus/merkle.h"
  "consensus/params.h"
  "consensus/predicates.h"
  "consensus/script_checker.h"
  "consensus/script_view.h"
  "consensus/snapshot_spend_state.h"
  "consensus/spend_state.h"
  "consensus/tx_check.h"
)

set(BITCOIN_CONSENSUS_SUPPORT_HEADERS
  "consensus/locktime.h"
  "consensus/pow.h"
  "consensus/sequence_locks.h"
  "consensus/sigops.h"
  "consensus/validation.h"
)

set(BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS
  "primitives/block.h"
  "primitives/transaction.h"
  "script/verify_flags.h"
  "uint256.h"
)

set(BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS
  "attributes.h"
  "compat/assumptions.h"
  "compat/byteswap.h"
  "compat/endian.h"
  "crypto/common.h"
  "crypto/hex_base.h"
  "prevector.h"
  "primitives/transaction_identifier.h"
  "script/script.h"
  "serialize.h"
  "span.h"
  "util/check.h"
  "util/hash_type.h"
  "util/overflow.h"
  "util/strencodings.h"
  "util/string.h"
  "util/time.h"
  "util/types.h"
)

set(BITCOIN_CONSENSUS_INTERNAL_PROTOCOL_HEADERS
  "arith_uint256.h"
  "hash.h"
  "script/interpreter.h"
  "script/script_error.h"
)

set(BITCOIN_CONSENSUS_INSTALL_HEADERS
  ${BITCOIN_CONSENSUS_PUBLIC_API_HEADERS}
  ${BITCOIN_CONSENSUS_PUBLIC_PROTOCOL_HEADERS}
  ${BITCOIN_CONSENSUS_PUBLIC_TRANSITIVE_HEADERS}
)
