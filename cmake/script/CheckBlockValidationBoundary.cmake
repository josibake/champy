# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

function(require_file relative_path)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Expected ${relative_path} to exist")
  endif()
endfunction()

function(require_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} is missing required block-validation-boundary text: ${needle}")
  endif()
endfunction()

function(forbid_text relative_path needle)
  file(READ "${SOURCE_DIR}/${relative_path}" contents)
  string(FIND "${contents}" "${needle}" match_index)
  if(NOT match_index EQUAL -1)
    message(FATAL_ERROR "${relative_path} contains forbidden block-validation-boundary text: ${needle}")
  endif()
endfunction()

foreach(relative_path IN ITEMS
    doc/consensus-design.md
    doc/legacy-compatibility.md
    doc/validation-execution-contracts.md
    src/primitives/block.h
    src/validation/block_data_admission.cpp
    src/validation/block_data_admission.h
    src/validation/block_data_adapters.cpp
    src/validation/block_data_adapters.h
    src/validation/block_index_adapters.cpp
    src/validation/block_index_adapters.h
    src/validation/block_connection.cpp
    src/validation/block_connection.h
    src/validation/block_connection_state.h
    src/validation/block_replay.cpp
    src/validation/block_replay.h
    src/validation/core_coins_block_connection_state.cpp
    src/validation/core_coins_block_connection_state.h
    src/validation/block_connection_trace.cpp
    src/validation/block_connection_trace.h
    src/validation/block_coin_effects.cpp
    src/validation/block_coin_effects.h
    src/validation/coins_view_spend_state.cpp
    src/validation/coins_view_spend_state.h
    src/validation/sequence_locks_adapters.cpp
    src/validation/sequence_locks_adapters.h
    src/validation/tx_verify.cpp
    src/validation/tx_verify.h
    src/validation/core_chain_activation.cpp
    src/validation/core_chain_activation.h
    src/validation/core_chain_validation_context.cpp
    src/validation/core_chain_validation_context.h
    src/validation/core_block_connection_context.cpp
    src/validation/core_block_connection_context.h
    src/validation/core_block_connection_setup.cpp
    src/validation/core_block_connection_setup.h
    src/validation/block_validation_internal.h
    src/validation/chain_validation.cpp
    src/validation/chain_validation.h)
  require_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "block_data_adapters.cpp")
require_text("src/CMakeLists.txt" "core_coins_block_connection_state.cpp")
require_text("src/test/CMakeLists.txt" "core_block_policy_tests.cpp")
require_text("doc/consensus-design.md" "BlockIndexValidityCommitter")
require_text("doc/consensus-design.md" "Protocol values should not carry hidden validation cache state.")
require_text("doc/validation-execution-contracts.md" "Do not pass `Chainstate`, `ChainstateManager`, `CoreBlockDataStore`,")
require_text("doc/legacy-compatibility.md" "The block connection engine no longer receives broad storage/index stores.")
require_text("src/CMakeLists.txt" "block_data_admission.cpp")
require_text("src/CMakeLists.txt" "block_connection_trace.cpp")
require_text("src/CMakeLists.txt" "block_replay.cpp")
require_text("src/CMakeLists.txt" "core_chain_activation.cpp")
require_text("src/CMakeLists.txt" "core_chain_validation_context.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_context.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_setup.cpp")
require_text("src/CMakeLists.txt" "add_library(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "add_library(bitcoin_core_storage_adapters")
require_text("src/CMakeLists.txt" "BITCOIN_CORE_STORAGE_ADAPTER_SOURCES")
require_text("src/CMakeLists.txt" "BITCOIN_CHAIN_VALIDATION_SOURCES")
require_text("src/CMakeLists.txt" "target_link_libraries(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_core_storage_adapters")
require_text("src/kernel/CMakeLists.txt" "bitcoin_chain_validation")
forbid_text("src/CMakeLists.txt" "target_include_directories(bitcoin_chain_validation")
require_text("src/validation/block_validation.cpp" "#include <validation/block_validation_internal.h>")
require_text("src/chainstate.cpp" "#include <validation/block_replay.h>")
require_text("src/kernel/chainstate_load.cpp" "#include <validation/block_replay.h>")
require_text("src/validation/block_header_context_adapters.h" "class BlockHeaderContextProvider")
require_text("src/validation/block_header_context_adapters.h" "class CoreBlockHeaderContextProvider")
require_text("src/validation/core_chain_validation_context.h" "class CoreChainValidationContext")
require_text("src/validation/core_chain_activation.h" "class CoreChainActivationState")
require_text("src/validation/core_chain_activation.h" "struct CoreConnectTipResources")
require_text("src/validation/core_chain_activation.h" "struct CoreConnectTipRequest")
require_text("src/validation/core_chain_activation.h" "enum class CoreConnectTipStatus")
require_text("src/validation/core_chain_activation.h" "ConnectCoreChainTip")
require_text("src/validation/core_chain_activation.h" "struct CoreActivateBestChainStepRequest")
require_text("src/validation/core_chain_activation.h" "enum class CoreActivateBestChainStepStatus")
require_text("src/validation/core_chain_activation.h" "ActivateCoreBestChainStep")
require_text("src/validation/block_validation.cpp" "const BlockHeaderContextProvider& header_context_provider")
require_text("src/validation/block_validation.cpp" "validation::BlockConnectionEngine")
require_text("src/validation/block_connection.h" "struct BlockConnectionContext")
require_text("src/validation/block_connection.h" "Consensus::BlockConsensusContext consensus_context")
require_text("src/validation/block_connection.h" "Consensus::BlockSpendConsensusOptions spend_options")
require_text("src/validation/block_connection.h" "struct BlockConnectionRuntime")
require_text("src/validation/block_connection.h" "kernel::Notifications& notifications")
require_text("src/validation/block_connection.h" "BlockUndoWriter& undo_writer")
require_text("src/validation/block_connection.h" "BlockIndexValidityCommitter& block_index_committer")
require_text("src/validation/block_connection.h" "Consensus::BlockScriptChecker& script_checker")
require_text("src/validation/block_connection.h" "BlockConnectionTrace& trace")
require_text("src/validation/block_connection_trace.h" "struct BlockConnectionTraceCounters")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCountersFor")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCounters m_counters")
require_text("src/validation/block_connection.h" "struct BlockConnectionRequest")
require_text("src/validation/block_connection.h" "BlockConnectionState& connection_state")
require_text("src/validation/block_connection.h" "enum class BlockConnectionStatus")
require_text("src/validation/block_connection.h" "struct BlockConnectionResult")
require_text("src/validation/block_connection.h" "class BlockConnectionEngine")
require_text("src/validation/block_connection.h" "BlockConnectionResult Connect")
require_text("src/validation/block_connection_state.h" "class BlockConnectionState")
require_text("src/validation/block_connection_state.h" "class BlockConnectionAttemptGuard")
require_text("src/validation/block_connection_state.h" "class BlockConnectionSpendState")
require_text("src/validation/block_connection_state.h" "BeginBlockSpend")
require_text("src/validation/block_connection_state.h" "BeginConnectionAttempt")
require_text("src/validation/core_coins_block_connection_state.h" "class CoreCoinsBlockConnectionState")
forbid_text("src/validation/block_connection_state.h" "CCoinsViewCache")
forbid_text("src/validation/block_connection_state.h" "CBlockIndex")
require_text("src/validation/block_connection.h" "sequence_lock_times")
require_text("src/validation/block_script_check_adapters.h" "class CoreScriptValidationCache")
require_text("src/validation/block_coin_effects.h" "namespace validation")
require_text("src/validation/coins_view_spend_state.h" "namespace validation")
require_text("src/validation/block_coin_effects.h" "ReplayBlockCoinsForRecovery")
require_text("src/validation/sequence_locks_adapters.h" "namespace validation")
require_text("src/validation/tx_verify.h" "namespace validation")
forbid_text("src/validation/block_coin_effects.h" "namespace Consensus")
forbid_text("src/validation/block_coin_effects.cpp" "namespace Consensus")
forbid_text("src/validation/coins_view_spend_state.h" "namespace Consensus")
forbid_text("src/validation/coins_view_spend_state.cpp" "namespace Consensus")
forbid_text("src/validation/sequence_locks_adapters.h" "namespace Consensus")
forbid_text("src/validation/sequence_locks_adapters.cpp" "namespace Consensus")
forbid_text("src/validation/tx_verify.h" "namespace Consensus")
forbid_text("src/validation/tx_verify.cpp" "namespace Consensus")
forbid_text("src/primitives/block.h" "fChecked")
forbid_text("src/primitives/block.h" "m_checked_merkle_root")
forbid_text("src/primitives/block.h" "m_checked_witness_commitment")
forbid_text("src/validation/block_connection.h" "bool Connect(const BlockConnectionRequest&")
forbid_text("src/validation/block_connection.h" "Chainstate&")
forbid_text("src/validation/block_connection.h" "ChainstateManager&")
forbid_text("src/validation/block_connection.h" "BlockManager")
forbid_text("src/validation/block_connection.h" "CCoinsViewCache")
forbid_text("src/validation/block_connection.h" "coins_view")
forbid_text("src/validation/block_connection.h" "CCheckQueue")
forbid_text("src/validation/block_connection.h" "CScriptCheck")
forbid_text("src/validation/block_connection.h" "ValidationCache")
forbid_text("src/validation/block_connection_trace.h" "ChainstateManager& m_chainman")
require_text("src/validation/chain_validation.cpp" "#include <validation/block_validation_internal.h>")
require_text("src/validation/chain_validation.h" "class ChainValidationService")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlockHeaders(")
require_text("src/validation/block_validation_internal.h" "AcceptBlock(")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlock(")
require_text("src/validation/block_validation_internal.h" "TestBlockValidity(")
require_text("src/validation/block_validation_internal.h" "CoreChainValidationContext& context")
forbid_text("src/validation/block_validation_internal.h" "ChainstateManager&")
require_text("src/validation/block_data_admission.h" "struct BlockDataAdmissionContext")
require_text("src/CMakeLists.txt" "block_index_adapters.cpp")
require_text("src/validation/block_data_adapters.h" "class BlockDataReader")
require_text("src/validation/block_data_adapters.h" "class BlockUndoReader")
require_text("src/validation/block_data_adapters.h" "class BlockUndoWriter")
require_text("src/validation/block_data_adapters.h" "class BlockDataWriter")
require_text("src/validation/block_data_adapters.h" "class CoreBlockDataStore")
require_text("src/validation/block_index_adapters.h" "class BlockIndexView")
require_text("src/validation/block_index_adapters.h" "class CoreBlockIndexView")
require_text("src/validation/block_index_adapters.h" "class BlockIndexLookup")
require_text("src/validation/block_index_adapters.h" "class BlockIndexHeaderStore")
require_text("src/validation/block_index_adapters.h" "class BlockIndexDataReceiver")
require_text("src/validation/block_index_adapters.h" "class BlockIndexValidityCommitter")
require_text("src/validation/block_index_adapters.h" "class CoreBlockIndexStore")
require_text("src/validation/block_index_adapters.h" "MarkBlockIndexDirty")
require_text("src/validation/block_validation.cpp" "CoreBlockDataStore")
require_text("src/validation/block_validation.cpp" "CoreBlockIndexStore")
require_text("src/chainstate.cpp" "CoreBlockDataStore")
require_text("src/chainstate.cpp" "CoreBlockIndexStore")
require_text("src/validation/core_block_policy.cpp" "BlockIndexLookup&")
require_text("src/validation/core_block_policy.h" "struct CoreBlockScriptCheckPolicy")
require_text("src/validation/core_block_policy.h" "const CoreBlockScriptCheckPolicy& policy")
require_text("src/validation/core_block_policy.h" "Consensus::BlockDeploymentContext deployments")
require_text("src/validation/block_validation_policy.h" "Consensus::BlockDeploymentContext deployments")
forbid_text("src/validation/block_validation_policy.h" "ChainstateManager")
forbid_text("src/validation/block_validation_policy.cpp" "ChainstateManager")
forbid_text("src/validation/core_block_policy.h" "BuildCoreBlockSpendConsensusOptions(const CBlockIndex& block_index, const ChainstateManager&")
forbid_text("src/validation/core_block_policy.cpp" "BuildCoreBlockSpendConsensusOptions(const CBlockIndex& block_index, const ChainstateManager&")
forbid_text("src/validation/core_block_policy.h" "DetermineCoreBlockScriptChecks(ChainstateManager&")
forbid_text("src/validation/core_block_policy.cpp" "DetermineCoreBlockScriptChecks(ChainstateManager&")
require_text("src/validation/core_block_connection_context.h" "struct CoreBlockConnectionPolicySnapshot")
require_text("src/validation/core_block_connection_context.h" "SnapshotCoreBlockConnectionPolicy")
require_text("src/validation/core_block_connection_context.h" "CoreChainValidationContext& context")
require_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection")
require_text("src/validation/core_block_connection_context.h" "const CoreBlockConnectionPolicySnapshot& policy")
forbid_text("src/validation/core_block_connection_context.h" "SnapshotCoreBlockConnectionPolicy(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.cpp" "SnapshotCoreBlockConnectionPolicy(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.cpp" "PlanCoreBlockConnection(ChainstateManager&")
require_text("src/validation/core_block_connection_context.h" "MaybeLogCoreBlockConnectionScriptPolicy")
require_text("src/validation/core_block_connection_setup.h" "class CoreBlockConnectionSetup")
require_text("src/validation/core_block_connection_setup.h" "struct CoreBlockConnectionRuntimeInputs")
require_text("src/validation/core_block_connection_setup.h" "BlockUndoWriter& m_undo_writer")
require_text("src/validation/core_block_connection_setup.h" "BlockIndexValidityCommitter& m_block_index_committer")
require_text("src/validation/core_block_connection_setup.h" "validation::BlockConnectionState& connection_state")
require_text("src/validation/core_block_connection_setup.h" "CoreBlockScriptChecks m_script_checks")
require_text("src/validation/core_block_connection_setup.h" "BlockConnectionTrace m_trace")
forbid_text("src/validation/core_block_connection_setup.h" "Chainstate&")
forbid_text("src/validation/core_block_connection_setup.h" "ChainstateManager&")
forbid_text("src/validation/core_block_connection_setup.h" "BlockDataStore& m_block_store")
forbid_text("src/validation/core_block_connection_setup.h" "BlockIndexStore& m_block_index_store")
forbid_text("src/validation/block_connection.h" "BlockDataStore& block_store")
forbid_text("src/validation/block_connection.h" "BlockIndexStore& block_index_store")
forbid_text("src/validation/block_validation.h" "Chainstate")
forbid_text("src/validation/block_validation.h" "ChainstateManager")
forbid_text("src/validation/block_validation.h" "CCoinsViewCache")
forbid_text("src/validation/block_validation.h" "ValidationSignals")
forbid_text("src/validation/block_validation.h" "DisconnectBlock")
forbid_text("src/validation/block_validation.h" "ReplayBlocks")
forbid_text("src/validation/block_validation.h" "GenerateCoinbaseCommitment")
require_text("src/validation/block_replay.h" "DisconnectBlock")
require_text("src/validation/block_replay.h" "ReplayBlocks")
require_text("src/node/miner.h" "GenerateCoinbaseCommitment")
forbid_text("src/validation/block_data_adapters.h" "class BlockDataStore")
forbid_text("src/validation/block_index_adapters.h" "class BlockIndexStore")
forbid_text("src/validation/block_index_adapters.h" "class BlockIndexAdmissionStore")
forbid_text("src/validation/block_data_adapters.h" "class BlockStorageInfo")
forbid_text("src/validation/block_index_adapters.h" "class BlockIndexSnapshot")
forbid_text("src/validation/core_block_connection_setup.h" "CoreBlockDataStore m_block_store")
forbid_text("src/validation/core_block_connection_setup.h" "CoreBlockIndexStore m_block_index_store")
forbid_text("src/validation/block_validation.cpp" "ContextualCheckBlockHeader(\n    const CBlockHeader& block,\n    BlockValidationState& state,\n    const ChainstateManager&")
forbid_text("src/validation/block_validation.cpp" "ContextualCheckBlock(const CBlock& block, BlockValidationState& state, const ChainstateManager&")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockDataStore")
require_text("src/kernel/bitcoinkernel.cpp" "CoreBlockIndexStore")
require_text("src/chainstate.cpp" "ActivateCoreBestChainStep")
forbid_text("src/chainstate.cpp" "ConnectCoreChainTip")
forbid_text("src/chainstate.cpp" "ActivateBestChainStep(")
forbid_text("src/chainstate.cpp" "CoreBlockConnectionSetup")

foreach(needle IN ITEMS
    "ProcessNewBlockHeaders("
    "AcceptBlock("
    "ProcessNewBlock("
    "TestBlockValidity(")
  forbid_text("src/validation/block_validation.h" "${needle}")
endforeach()

file(GLOB_RECURSE validation_boundary_sources
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/src/*.h"
)
foreach(path IN LISTS validation_boundary_sources)
  file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${path}")
  if(NOT relative_path STREQUAL "src/validation/block_validation.cpp" AND
     NOT relative_path STREQUAL "src/validation/chain_validation.cpp")
    forbid_text("${relative_path}" "#include <validation/block_validation_internal.h>")
  endif()
endforeach()

foreach(relative_path IN ITEMS
    src/validation/block_validation.cpp
    src/chainstate.cpp
    src/kernel/bitcoinkernel.cpp)
  foreach(needle IN ITEMS
    "m_blockman.ReadBlock("
    "m_blockman.ReadBlockUndo("
    "m_blockman.WriteBlock("
    "m_blockman.UpdateBlockInfo("
    "m_blockman.AddToBlockIndex(")
    forbid_text("${relative_path}" "${needle}")
  endforeach()
endforeach()

forbid_text("src/validation/block_validation.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/chainstate.cpp" "m_dirty_blockindex")
forbid_text("src/validation/core_block_policy.cpp" "m_blockman.m_block_index")
forbid_text("src/kernel/bitcoinkernel.cpp" "m_blockman.LookupBlockIndex(")
