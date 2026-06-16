# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Public API manifest for the Champy boundary work.
#
# Header paths are relative to src/. Target status is intentionally explicit so
# boundary checks can distinguish shipped APIs from planned release APIs without
# accepting compatibility aliases.

set(BITCOIN_API_SHIPPED_REQUIRED_TARGETS
  bitcoin_protocol
  bitcoin_validation
)

set(BITCOIN_API_SHIPPED_OPTIONAL_TARGETS
  bitcoin_chain_graph
)

# Pending targets do not exist yet. Experimental targets may exist in the build
# tree, but they must not be installed or exported as release APIs.
set(BITCOIN_API_PENDING_REQUIRED_TARGETS)

set(BITCOIN_API_EXPERIMENTAL_TARGETS
  bitcoin_kernel
)

set(BITCOIN_API_NOT_SHIPPING_OPTIONAL_TARGETS
  bitcoin_validation_c
)

# C ABI release targets are tracked separately from the C++ API targets. The
# current bitcoinkernel ABI is useful as a build-tree and experimental Core
# projection, but it is not a Champy release ABI until the versioning,
# size/version-safe struct, symbol allowlist, and kernel/validation split gates
# are satisfied.
set(BITCOIN_C_ABI_SHIPPED_TARGETS)

set(BITCOIN_C_ABI_EXPERIMENTAL_TARGETS
  bitcoinkernel
)

set(BITCOIN_C_ABI_NOT_SHIPPING_OPTIONAL_TARGETS
  bitcoin_validation_c
)

set(BITCOIN_C_ABI_RELEASE_BLOCKING_TEXT
  "btck_ValidationInterfaceCallbacks;"
  "btck_NotificationInterfaceCallbacks;"
  "btck_LoggingOptions;"
  "btck_BlockValidationLibraryOptions"
  "btck_ValidationCoinIndex"
  "btck_block_verify_result("
)

set(BITCOIN_PROTOCOL_PUBLIC_HEADERS
  "bitcoin/protocol/amount.h"
  "bitcoin/protocol/api.h"
  "bitcoin/protocol/block.h"
  "bitcoin/protocol/block_header.h"
  "bitcoin/protocol/chain_view.h"
  "bitcoin/protocol/codec.h"
  "bitcoin/protocol/coin_index.h"
  "bitcoin/protocol/hash.h"
  "bitcoin/protocol/result.h"
  "bitcoin/protocol/script.h"
  "bitcoin/protocol/transaction.h"
)

set(BITCOIN_VALIDATION_PUBLIC_HEADERS
  "bitcoin/validation/api.h"
  "bitcoin/validation/block.h"
  "bitcoin/validation/context.h"
  "bitcoin/validation/header.h"
  "bitcoin/validation/result.h"
  "bitcoin/validation/rules.h"
  "bitcoin/validation/script.h"
  "bitcoin/validation/spend.h"
  "bitcoin/validation/transaction.h"
  "bitcoin/validation/verify.h"
)

set(BITCOIN_CHAIN_GRAPH_PUBLIC_HEADERS
  "bitcoin/chain_graph/chain_graph.h"
)

set(BITCOIN_BITCOINKERNEL_PUBLIC_HEADERS
  "kernel/bitcoinkernel.h"
)

set(BITCOIN_KERNEL_PUBLIC_HEADERS
  "bitcoin/kernel/api.h"
  "bitcoin/kernel/chainstate.h"
  "bitcoin/kernel/context.h"
  "bitcoin/kernel/events.h"
  "bitcoin/kernel/result.h"
)
set(BITCOIN_VALIDATION_C_PUBLIC_HEADERS)

set(BITCOIN_AGGREGATE_PUBLIC_HEADERS
  "bitcoin/api.h"
)

set(BITCOIN_VALIDATION_INSTALL_SHAPE_HEADERS
  ${BITCOIN_PROTOCOL_PUBLIC_HEADERS}
  ${BITCOIN_VALIDATION_PUBLIC_HEADERS}
)

set(BITCOIN_CHAIN_GRAPH_INSTALL_SHAPE_HEADERS
  ${BITCOIN_PROTOCOL_PUBLIC_HEADERS}
  ${BITCOIN_CHAIN_GRAPH_PUBLIC_HEADERS}
)

# Public C ABI concepts in bitcoinkernel that must be traceable to a C++ source
# concept. Active rules require a source header and identifying text on either a
# shipped target or an explicitly experimental build-tree target. Experimental
# C++ targets are not install/export promises; they keep the C ABI buildable
# without pretending the modern C++ API is release-ready.
set(BITCOIN_C_ABI_ACTIVE_CPP_CONCEPT_RULES
  "btck_Transaction|bitcoin_protocol|bitcoin/protocol/transaction.h|class transaction"
  "btck_TransactionInput|bitcoin_protocol|bitcoin/protocol/transaction.h|class tx_input"
  "btck_TransactionOutput|bitcoin_protocol|bitcoin/protocol/transaction.h|class tx_output"
  "btck_TransactionOutPoint|bitcoin_protocol|bitcoin/protocol/transaction.h|class outpoint"
  "btck_Txid|bitcoin_protocol|bitcoin/protocol/hash.h|using txid"
  "btck_Block|bitcoin_protocol|bitcoin/protocol/block.h|class block"
  "btck_BlockHeader|bitcoin_protocol|bitcoin/protocol/block_header.h|class block_header"
  "btck_BlockHash|bitcoin_protocol|bitcoin/protocol/hash.h|using block_hash"
  "btck_Coin|bitcoin_protocol|bitcoin/protocol/coin_index.h|class coin"
  "btck_CoinLookupStatus|bitcoin_protocol|bitcoin/protocol/coin_index.h|enum class coin_lookup_state"
  "btck_TransactionParseResult|bitcoin_protocol|bitcoin/protocol/codec.h|parse_result<transaction>"
  "btck_BlockParseResult|bitcoin_protocol|bitcoin/protocol/codec.h|parse_result<block>"
  "btck_BlockHeaderParseResult|bitcoin_protocol|bitcoin/protocol/codec.h|parse_result<block_header>"
  "btck_ParseStatus|bitcoin_protocol|bitcoin/protocol/result.h|class parse_result"
  "btck_ParseFailureCode|bitcoin_protocol|bitcoin/protocol/result.h|enum class parse_failure_code"
  "btck_ConsensusParams|bitcoin_validation|bitcoin/validation/context.h|struct consensus_params"
  "btck_BlockValidationOptions|bitcoin_validation|bitcoin/validation/context.h|validation_time"
  "btck_BlockValidationState|bitcoin_validation|bitcoin/validation/result.h|class validation_rejection"
  "btck_TxValidationState|bitcoin_validation|bitcoin/validation/result.h|class validation_rejection"
  "btck_TransactionCheckResult|bitcoin_validation|bitcoin/validation/transaction.h|verify_result<transaction_facts>"
  "btck_BlockCheckResult|bitcoin_validation|bitcoin/validation/block.h|verify_result<block_facts>"
  "btck_BlockVerifyResult|bitcoin_validation|bitcoin/validation/verify.h|verify_result<block_facts>"
  "btck_CheckStatus|bitcoin_validation|bitcoin/validation/result.h|template <typename T>"
  "btck_ValidationMode|bitcoin_validation|bitcoin/validation/result.h|class validation_rejection"
  "btck_ValidationRejectionCode|bitcoin_validation|bitcoin/validation/result.h|enum class validation_rejection_code"
  "btck_ValidationRule|bitcoin_validation|bitcoin/validation/rules.h|enum class validation_rule_id"
  "btck_ScriptPubkey|bitcoin_protocol|bitcoin/protocol/script.h|class script"
  "btck_ScriptVerifyStatus|bitcoin_validation|bitcoin/validation/script.h|class script_execution_result"
  "btck_ValidationScriptFlags|bitcoin_validation|bitcoin/validation/context.h|verification_flags"
  "btck_ValidationCoinLookup|bitcoin_protocol|bitcoin/protocol/coin_index.h|concept coin_index"
  "btck_BlockCheckFlags|bitcoin_validation|bitcoin/validation/block.h|block_local_context"
  "btck_Context|bitcoin_kernel|bitcoin/kernel/context.h|class context"
  "btck_ContextOptions|bitcoin_kernel|bitcoin/kernel/context.h|class context_options"
  "btck_LoggingConnection|bitcoin_kernel|bitcoin/kernel/events.h|class diagnostic_connection"
  "btck_ChainParameters|bitcoin_kernel|bitcoin/kernel/context.h|class chain_parameters"
  "btck_Error|bitcoin_kernel|bitcoin/kernel/result.h|class operation_error"
  "btck_BlockInfo|bitcoin_kernel|bitcoin/kernel/chainstate.h|struct block_info"
  "btck_Chainstate|bitcoin_kernel|bitcoin/kernel/chainstate.h|class chainstate"
  "btck_ChainstateOptions|bitcoin_kernel|bitcoin/kernel/chainstate.h|class chainstate_options"
  "btck_ChainstateRuntime|bitcoin_kernel|bitcoin/kernel/chainstate.h|class chainstate_runtime"
  "btck_ChainSnapshot|bitcoin_kernel|bitcoin/kernel/chainstate.h|class chain_snapshot"
  "btck_HeaderProcessResult|bitcoin_kernel|bitcoin/kernel/chainstate.h|struct header_process_result"
  "btck_BlockProcessResult|bitcoin_kernel|bitcoin/kernel/chainstate.h|struct block_process_result"
  "btck_BlockImportResult|bitcoin_kernel|bitcoin/kernel/chainstate.h|struct block_import_result"
  "btck_BlockReadResult|bitcoin_kernel|bitcoin/kernel/chainstate.h|class block_read_result"
  "btck_BlockSpentOutputs|bitcoin_kernel|bitcoin/kernel/chainstate.h|class block_spent_outputs"
  "btck_BlockSpentOutputsReadResult|bitcoin_kernel|bitcoin/kernel/chainstate.h|class block_spent_outputs_read_result"
  "btck_TransactionSpentOutputs|bitcoin_kernel|bitcoin/kernel/chainstate.h|struct transaction_spent_outputs"
  "btck_PrecomputedTransactionData|bitcoin_kernel|bitcoin/kernel/chainstate.h|class precomputed_transaction_data"
  "btck_SynchronizationState|bitcoin_kernel|bitcoin/kernel/events.h|enum class synchronization_state"
  "btck_Warning|bitcoin_kernel|bitcoin/kernel/events.h|enum class warning"
  "btck_HeaderProcessStatus|bitcoin_kernel|bitcoin/kernel/chainstate.h|enum class header_process_status"
  "btck_BlockProcessStatus|bitcoin_kernel|bitcoin/kernel/chainstate.h|enum class block_process_status"
  "btck_BlockImportStatus|bitcoin_kernel|bitcoin/kernel/chainstate.h|enum class block_import_status"
  "btck_BlockReadStatus|bitcoin_kernel|bitcoin/kernel/chainstate.h|enum class block_read_status"
  "btck_BlockSpentOutputsReadStatus|bitcoin_kernel|bitcoin/kernel/chainstate.h|enum class block_spent_outputs_read_status"
  "btck_BlockValidationResult|bitcoin_kernel|bitcoin/kernel/result.h|enum class block_validation_result"
  "btck_TxValidationResult|bitcoin_kernel|bitcoin/kernel/result.h|enum class tx_validation_result"
  "btck_LogCategory|bitcoin_kernel|bitcoin/kernel/events.h|enum class diagnostic_category"
  "btck_LogLevel|bitcoin_kernel|bitcoin/kernel/events.h|enum class diagnostic_level"
  "btck_ChainType|bitcoin_kernel|bitcoin/kernel/context.h|enum class chain_type"
)

set(BITCOIN_C_ABI_PENDING_CPP_CONCEPT_RULES
)

set(BITCOIN_C_ABI_ALLOWED_FUNCTION_PREFIXES
  btck_block_
  btck_block_check_result_
  btck_block_hash_
  btck_block_header_
  btck_block_header_parse_result_
  btck_block_import_result_
  btck_block_info_
  btck_block_parse_result_
  btck_block_process_result_
  btck_block_read_result_
  btck_block_spent_outputs_
  btck_block_spent_outputs_read_result_
  btck_block_validation_options_
  btck_block_validation_state_
  btck_block_verify_result_
  btck_chain_parameters_
  btck_chain_snapshot_
  btck_chainstate_
  btck_chainstate_options_
  btck_chainstate_runtime_
  btck_coin_
  btck_context_
  btck_context_options_
  btck_error_
  btck_header_process_result_
  btck_logging_
  btck_logging_connection_
  btck_precomputed_transaction_data_
  btck_script_pubkey_
  btck_transaction_
  btck_transaction_check_result_
  btck_transaction_input_
  btck_transaction_out_point_
  btck_transaction_output_
  btck_transaction_parse_result_
  btck_transaction_spent_outputs_
  btck_tx_validation_state_
  btck_txid_
)

set(BITCOIN_VALIDATION_PUBLIC_HEADER_FORBIDDEN_TEXT
  "#include <chainstate.h>"
  "#include <chain.h>"
  "#include <coins.h>"
  "#include <consensus/"
  "#include <flatfile.h>"
  "#include <interfaces/"
  "#include <kernel/"
  "#include <logging.h>"
  "#include <node/"
  "#include <policy/"
  "#include <primitives/"
  "#include <serialize.h>"
  "#include <streams.h>"
  "#include <sync.h>"
  "#include <util/"
  "#include <validation.h>"
  "#include <validation/"
  "ArgsManager"
  "BlockManager"
  "BlockValidationState"
  "CBlock"
  "CBlockHeader"
  "CBlockIndex"
  "CBlockUndo"
  "CChain"
  "CCoinsView"
  "CCoinsViewCache"
  "CMutableTransaction"
  "COutPoint"
  "CScript"
  "CTransaction"
  "CValidationState"
  "Chainstate"
  "ChainstateManager"
  "DataStream"
  "FlatFilePos"
  "LOCK("
  "Mutex"
  "NodeClock"
  "READWRITE"
  "SERIALIZE_METHODS"
  "btck_"
  "BITCOINKERNEL_"
  "extern \"C\""
  "cs_main"
  "gArgs"
)

set(BITCOIN_VALIDATION_RELEASE_PENDING_FORBIDDEN_TEXT
  "detail_"
  "block_spend_overlay"
  "struct validation_context"
)
