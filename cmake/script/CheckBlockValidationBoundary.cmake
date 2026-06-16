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

function(forbid_file relative_path)
  if(EXISTS "${SOURCE_DIR}/${relative_path}")
    message(FATAL_ERROR "Forbidden legacy block-validation file still exists: ${relative_path}")
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

function(forbid_target_source target source)
  file(READ "${SOURCE_DIR}/src/CMakeLists.txt" contents)
  string(REGEX MATCH "add_library\\(${target}[ \t\r\n][^\\)]*${source}" match_text "${contents}")
  if(match_text)
    message(FATAL_ERROR "${target} contains forbidden source: ${source}")
  endif()
endfunction()

foreach(relative_path IN ITEMS
    src/kernel/blk_file_scanner.cpp
    src/kernel/blk_file_scanner.h
    src/kernel/block_import_pipeline.cpp
    src/kernel/block_import_pipeline.h
    src/primitives/block.h
    src/validation/active_chain.h
    src/validation/block_data_admission.cpp
    src/validation/block_data_admission.h
    src/validation/block_data_adapters.cpp
    src/validation/block_data_adapters.h
    src/validation/block_storage.h
    src/validation/block_index_adapters.cpp
    src/validation/block_index_adapters.h
    src/validation/block_index.h
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
    src/validation/candidate_context.h
    src/validation/core_block_index_invariants.cpp
    src/validation/core_block_index_invariants.h
    src/validation/core_chain_activation.cpp
    src/validation/core_chain_activation.h
    src/validation/validation_commit_executor.h
    src/validation/core_chain_validation_runtimes.cpp
    src/validation/core_chain_validation_runtimes.h
    src/validation/core_block_connection_context.cpp
    src/validation/core_block_connection_context.h
    src/validation/core_block_connection_setup.cpp
    src/validation/core_block_connection_setup.h
    src/validation/verify_db.h
    src/validation/block_validation_internal.h
    src/validation/test_block_validity.h
    src/validation/chain_validation.cpp
    src/validation/chain_validation.h)
  require_file("${relative_path}")
endforeach()

forbid_file("doc")

foreach(relative_path IN ITEMS
    src/validation/block_candidate_admission.h
    src/validation/core_block_candidate_admission.cpp
    src/validation/core_block_candidate_admission.h
    src/validation/core_validation_committer.cpp
    src/validation/core_validation_committer.h
    src/validation/ibd_segment_adapters.cpp
    src/validation/ibd_segment_adapters.h
    src/validation/in_memory_segment_runtime.cpp
    src/validation/in_memory_segment_runtime.h
    src/validation/script_plan_executor.cpp
    src/validation/script_plan_executor.h
    src/validation/segment_validation_executor.cpp
    src/validation/segment_validation_executor.h
    src/validation/validation_committer.h
    src/validation/validation_facade.cpp
    src/validation/validation_facade.h
    src/validation/validation_pipeline.cpp
    src/validation/validation_pipeline.h
    src/validation/validation_segment_types.h
    src/node/ibd_block_candidate.h
    src/node/ibd_candidate_chain_overlay.cpp
    src/node/ibd_candidate_chain_overlay.h
    src/node/ibd_core_backend.cpp
    src/node/ibd_core_backend.h
    src/node/ibd_pipeline.h
    src/node/ibd_pipeline_controller.cpp
    src/node/ibd_pipeline_controller.h
    src/node/ibd_segment_executor.h
    src/node/ibd_segment_job.h
    src/node/ibd_segment_scheduler.cpp
    src/node/ibd_segment_scheduler.h
    src/node/ibd_segment_worker_executor.cpp
    src/node/ibd_segment_worker_executor.h
    src/node/ibd_validated_block.h
    src/test/ibd_pipeline_tests.cpp
    src/bench/ibd_pipeline.cpp)
  forbid_file("${relative_path}")
endforeach()

require_text("src/CMakeLists.txt" "block_data_adapters.cpp")
require_text("src/CMakeLists.txt" "kernel/block_import_pipeline.cpp")
require_text("src/CMakeLists.txt" "kernel/blk_file_scanner.cpp")
require_text("src/kernel/CMakeLists.txt" "block_import_pipeline.cpp")
require_text("src/kernel/CMakeLists.txt" "blk_file_scanner.cpp")
require_text("src/CMakeLists.txt" "core_coins_block_connection_state.cpp")
require_text("src/test/CMakeLists.txt" "core_block_policy_tests.cpp")
require_text("src/CMakeLists.txt" "block_data_admission.cpp")
require_text("src/CMakeLists.txt" "block_connection_trace.cpp")
require_text("src/CMakeLists.txt" "block_replay.cpp")
require_text("src/CMakeLists.txt" "core_block_index_invariants.cpp")
require_text("src/CMakeLists.txt" "core_chain_activation.cpp")
require_text("src/CMakeLists.txt" "core_chain_validation_runtimes.cpp")
forbid_text("src/CMakeLists.txt" "core_chain_validation_context.cpp")
require_text("src/CMakeLists.txt" "core_check_queue_script_task_executor.cpp")
forbid_text("src/CMakeLists.txt" "legacy_script_check_queue_executor.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_context.cpp")
require_text("src/CMakeLists.txt" "core_block_connection_setup.cpp")
forbid_text("src/CMakeLists.txt" "add_library(bitcoin_node_ibd_validation")
forbid_text("src/CMakeLists.txt" "add_library(bitcoin_node_ibd_validation_core_adapters")
forbid_text("src/CMakeLists.txt" "bitcoin_node_ibd_validation")
forbid_text("src/CMakeLists.txt" "validation/core_validation_committer.cpp")
forbid_text("src/CMakeLists.txt" "validation/block_candidate_admission.h")
forbid_text("src/CMakeLists.txt" "validation/core_block_candidate_admission.cpp")
forbid_text("src/CMakeLists.txt" "validation/ibd_segment_adapters.cpp")
forbid_text("src/CMakeLists.txt" "validation/segment_validation_executor.cpp")
forbid_text("src/CMakeLists.txt" "validation/validation_facade.cpp")
forbid_text("src/CMakeLists.txt" "validation/validation_pipeline.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_candidate_chain_overlay.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_core_backend.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_pipeline_controller.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_segment_scheduler.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_segment_worker_executor.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_segment_job.h")
forbid_text("src/test/CMakeLists.txt" "ibd_pipeline_tests.cpp")
forbid_text("src/bench/CMakeLists.txt" "ibd_pipeline.cpp")
require_text("src/node/ibd_block_download.h" "IbdBlockDownloadWindow")
require_text("src/node/ibd_block_download.h" "AdmitIbdBlockDownloadCandidate")
require_text("src/node/block_download_planner.h" "std::optional<IbdBlockDownloadWindow> ibd_window")
forbid_text("src/node/block_download_planner.h" "IbdPipelineAdmissionWindow")
forbid_text("src/node/block_download_planner.cpp" "IbdPipeline")
forbid_text("src/node/ibd_block_processor.h" "ExecuteAndCommitAcceptedCandidateSegment")
forbid_text("src/node/ibd_block_processor.cpp" "CoreBlockCandidateAdmission")
forbid_text("src/node/ibd_block_processor.cpp" "CoreValidationCommitter")
forbid_text("src/node/ibd_block_processor.cpp" "AcceptBlockCandidate")
require_text("src/CMakeLists.txt" "add_library(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "add_library(bitcoin_core_storage_adapters")
require_text("src/CMakeLists.txt" "BITCOIN_CORE_STORAGE_ADAPTER_SOURCES")
require_text("src/CMakeLists.txt" "BITCOIN_CHAIN_VALIDATION_SOURCES")
require_text("src/CMakeLists.txt" "target_link_libraries(bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/CMakeLists.txt" "bitcoin_core_storage_adapters")
require_text("src/kernel/CMakeLists.txt" "bitcoin_chain_validation")
require_text("src/test/CMakeLists.txt" "test_bitcoin_validation_core_adapter_consumer")
require_text("src/test/CMakeLists.txt" "block_index_invariant_tests.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_pipeline.cpp")
forbid_text("src/CMakeLists.txt" "node/ibd_segment_executor.cpp")
forbid_text("src/CMakeLists.txt" "target_include_directories(bitcoin_chain_validation")
require_text("src/validation/core_chain_validation_runtimes.cpp" "executor.emplace(chainman.GetCheckQueue())")
forbid_text("src/node/ibd_block_processor.h" "ProcessDownloadedBlockWithLegacyActivationFallback")
forbid_text("src/node/ibd_block_processor.h" "LegacyProcessDownloadedBlockWithActivationFallback")
forbid_text("src/node/ibd_block_processor.h" "RunLegacyActivationFallbackForDownloadedBlock")
forbid_text("src/node/ibd_block_processor.cpp" "RunStructuralValidationStage")
forbid_text("src/node/ibd_block_processor.cpp" "RunBlockAdmissionStage")
forbid_text("src/node/ibd_block_processor.cpp" "RunContextSnapshotStage")
forbid_text("src/node/ibd_block_processor.cpp" "CheckNewBlockStructural")
forbid_text("src/node/ibd_block_processor.cpp" "AcceptNewBlockData")
forbid_text("src/node/ibd_block_processor.cpp" "SnapshotAcceptedBlockContext")
forbid_text("src/node/ibd_block_processor.cpp" "ExecuteCoreIbdCandidateSegment")
forbid_text("src/node/ibd_block_processor.cpp" "LegacyProcessDownloadedBlockWithActivationFallback")
forbid_text("src/node/ibd_block_processor.cpp" "RunLegacyActivationFallbackForDownloadedBlock")
forbid_text("src/node/ibd_block_processor.cpp" "RunLegacyActivationFallbackStage")
forbid_text("src/node/ibd_block_processor.cpp" "RunLegacyCompatibleActivationStage")
forbid_text("src/node/ibd_block_processor.cpp" "FallbackActivation")
forbid_text("src/net_processing.cpp" "LegacyProcessDownloadedBlockWithActivationFallback")
forbid_text("src/net_processing.cpp" "IbdPipelineLimits")
forbid_text("src/net_processing.cpp" "BuildIbdPipelineAdmissionWindow")
require_text("src/validation/block_validation.cpp" "#include <validation/block_validation_internal.h>")
require_text("src/chainstate.cpp" "#include <validation/block_replay.h>")
require_text("src/chainstate.cpp" "#include <validation/core_block_index_invariants.h>")
require_text("src/chainstate.cpp" "validation::AssertCoreBlockIndexInvariants(invariant_view)")
forbid_text("src/chainstate.cpp" "std::multimap<const CBlockIndex*, const CBlockIndex*> forward")
require_text("src/validation/core_block_index_invariants.h" "CoreBlockIndexInvariantReport")
require_text("src/validation/core_block_index_invariants.h" "CheckCoreBlockIndexInvariants")
require_text("src/validation/core_block_index_invariants.h" "DirtyIndex")
require_text("src/validation/core_block_index_invariants.h" "dirty_block_indices")
require_text("src/validation/core_block_index_invariants.cpp" "AssertCoreBlockIndexInvariants")
forbid_text("src/validation/core_block_index_invariants.cpp" "Begin: actual consistency checks.")
require_text("src/test/block_index_invariant_tests.cpp" "failed_best_header_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "parent_child_graph_rejects_null_snapshot_entry")
require_text("src/test/block_index_invariant_tests.cpp" "unlinked_sequence_id_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "bad_chain_transaction_count_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "missing_skip_pointer_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "non_tree_valid_ancestor_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "dirty_index_outside_snapshot_is_reported")
require_text("src/test/block_index_invariant_tests.cpp" "snapshot_missing_best_header_ancestor_is_reported")
require_text("src/kernel/block_import_pipeline.h" "using ExternalBlockFileImportMode = std::variant")
require_text("src/kernel/block_import_pipeline.h" "struct ExternalBlockFileLoadBlock")
require_text("src/kernel/block_import_pipeline.h" "struct ExternalBlockFileReindex")
require_text("src/kernel/block_import_pipeline.h" "int file_number")
require_text("src/kernel/block_import_pipeline.cpp" "std::get_if<ExternalBlockFileReindex>")
require_text("src/kernel/block_import_pipeline.cpp" "class BlockRecordDecoder")
require_text("src/kernel/block_import_pipeline.cpp" "class ImportAdmission")
require_text("src/kernel/block_import_pipeline.cpp" "class ImportActivation")
require_text("src/kernel/block_import_pipeline.cpp" "class ImportReporter")
require_text("src/kernel/block_import_pipeline.cpp" "BlockImportStatus::ResourceLimit")
require_text("src/kernel/block_import_pipeline.cpp" "bool should_process{false}")
require_text("src/kernel/blockimport.h" "struct BlockImportCounters")
require_text("src/kernel/blockimport.h" "BlockImportCounters counters")
require_text("src/kernel/blk_file_scanner.h" "Interrupted")
require_text("src/kernel/blk_file_scanner.h" "RecordPosition")
require_text("src/test/blk_file_scanner_tests.cpp" "scanner_reports_interruption_as_scan_status")
require_text("src/test/blk_file_scanner_tests.cpp" "scanner_record_position_includes_reindex_file_number")
forbid_text("src/kernel/block_import_pipeline.h" "FlatFilePos* current_file_pos")
forbid_text("src/kernel/block_import_pipeline.h" "UnknownParentIndex* unknown_parent_index")
forbid_text("src/kernel/block_import_pipeline.h" "current_file_pos")
forbid_text("src/kernel/block_import_pipeline.cpp" ".current_file_pos = nullptr")
forbid_text("src/kernel/block_import_pipeline.cpp" ".unknown_parent_index = nullptr")
forbid_text("src/chainstate.cpp" "LoadExternalBlockFile")
forbid_text("src/chainstate.h" "LoadExternalBlockFile")
forbid_text("src/kernel/blockimport.cpp" "LoadExternalBlockFile")
forbid_text("src/kernel/blockimport.h" "LoadExternalBlockFile")
foreach(needle IN ITEMS
    "MarkBlockIndexDirty"
    "ReceivedBlockTransactions"
    "setBlockIndexCandidates.insert"
    "m_blocks_unlinked.insert"
    "nStatus ="
    "nStatus |=")
  forbid_text("src/validation/core_block_index_invariants.cpp" "${needle}")
endforeach()
forbid_text("src/kernel/chainstate_load.cpp" "#include <validation/block_replay.h>")
require_text("src/chainstate.cpp" "BlockReplayRequest request")
require_text("src/validation/block_header_context_adapters.h" "class BlockHeaderContextProvider")
require_text("src/validation/block_header_context_adapters.h" "class CoreBlockHeaderContextProvider")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreHeaderAdmissionRuntime")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreBlockDataAdmissionRuntime")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreAcceptedContextReader")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreActivationRuntime")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreReplayRuntime")
require_text("src/validation/core_chain_validation_runtimes.h" "class CoreTestBlockValidityRuntime")
forbid_text("src/validation/core_chain_validation_runtimes.h" "CoreChainValidationContext")
forbid_text("src/validation/core_chain_validation_runtimes.h" "CoreChainValidationRuntime")
require_text("src/validation/core_chain_activation.h" "class CoreChainActivationState")
require_text("src/validation/core_chain_activation.h" "struct CoreChainActivationResources")
require_text("src/validation/core_chain_activation.h" "struct CoreConnectTipRequest")
require_text("src/validation/core_chain_activation.h" "enum class CoreConnectTipStatus")
require_text("src/validation/core_chain_activation.h" "PrepareCoreConnectTip")
require_text("src/validation/core_chain_activation.h" "ExecutePreparedCoreConnectTip")
require_text("src/validation/core_chain_activation.h" "ReportCoreConnectTipExecution")
require_text("src/validation/core_chain_activation.h" "CommitCoreConnectTip")
require_text("src/validation/core_chain_activation.h" "struct CoreActivateBestChainStepRequest")
require_text("src/validation/core_chain_activation.h" "enum class CoreActivateBestChainStepStatus")
require_text("src/validation/core_chain_activation.h" "ActivateCoreBestChainStep")
require_text("src/validation/validation_commit_executor.h" "class CoreValidationCommitExecutor")
require_text("src/validation/validation_commit_executor.h" "RunSerialized")
require_text("src/validation/validation_commit_executor.h" "RunBlockIndexLocked")
require_text("src/validation/validation_commit_executor.h" "RunChainstateCommitLocked")
require_text("src/validation/validation_commit_executor.h" "RunStorageCoordinationLocked")
require_text("src/validation/block_validation.cpp" "const BlockHeaderContextProvider& header_context_provider")
require_text("src/validation/block_validation.cpp" "validation::BlockConnectionEngine")
require_text("src/validation/block_connection.h" "struct BlockConnectionContext")
require_text("src/validation/block_connection.h" "Consensus::BlockConsensusContext consensus_context")
require_text("src/validation/block_connection.h" "Consensus::BlockSpendConsensusOptions spend_options")
require_text("src/validation/block_connection.h" "struct BlockConnectionRuntime")
require_text("src/validation/block_connection.h" "kernel::Notifications& notifications")
require_text("src/validation/block_connection.h" "Consensus::BlockRevertDataWriter& revert_data_writer")
require_text("src/validation/block_connection.h" "Consensus::BlockMetadataCommitter& metadata_committer")
require_text("src/validation/block_connection.h" "Consensus::BlockScriptChecker& script_checker")
require_text("src/validation/block_connection.h" "BlockConnectionTrace& trace")
require_text("src/validation/block_connection_trace.h" "struct BlockConnectionTraceCounters")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCountersFor")
require_text("src/validation/block_connection_trace.h" "BlockConnectionTraceCounters m_counters")
require_text("src/validation/validation_event_queue.h" "struct BlockCheckedEvent")
require_text("src/validation/validation_event_queue.h" "struct PoWValidBlockEvent")
require_text("src/validation/validation_event_queue.h" "struct BlockConnectedEvent")
require_text("src/validation/validation_event_queue.h" "struct BlockDisconnectedEvent")
require_text("src/validation/validation_event_queue.h" "struct TipUpdatedEvent")
require_text("src/validation/validation_event_queue.h" "struct ActiveTipChangedEvent")
require_text("src/validation/validation_event_queue.h" "struct ChainStateFlushedEvent")
require_text("src/validation/validation_event_queue.h" "virtual void BlockChecked(BlockCheckedEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void NewPoWValidBlock(PoWValidBlockEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void BlockConnected(BlockConnectedEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void BlockDisconnected(BlockDisconnectedEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void ChainStateFlushed(ChainStateFlushedEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void UpdatedBlockTip(TipUpdatedEvent event)")
require_text("src/validation/validation_event_queue.h" "virtual void ActiveTipChange(ActiveTipChangedEvent event)")
require_text("src/validation/validation_event_queue.h" "class CoreValidationEventQueue final")
require_text("src/test/validationinterface_tests.cpp" "validation_callbacks_preserve_per_subscriber_order_and_copied_event_values")
forbid_text("src/validation/validation_event_queue.h" "CBlockIndex")
forbid_text("src/validation/validation_event_queue.h" "const CBlockIndex")
forbid_text("src/validation/validation_event_queue.h" "CBlockIndex*")
forbid_text("src/validation/validation_event_queue.h" "CBlockIndex&")
forbid_text("src/validationinterface.h" "CBlockIndex")
forbid_text("src/validation/core_chain_validation_runtimes.h" "CoreValidationEventQueue& ValidationEvents")
require_text("src/validation/core_chain_validation_runtimes.h" "validation::ValidationEventQueue& ValidationEvents()")
require_text("src/validation/core_validation_event_snapshot.h" "SnapshotCoreValidationBlockInfo(const CBlockIndex& index)")
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
forbid_text("src/validation/chain_validation.h" "class ChainValidationService")
forbid_text("src/validation/chain_validation.cpp" "ChainValidationService::")
forbid_text("src/chainstate.cpp" "ChainValidationService")
forbid_text("src/kernel/bitcoinkernel.cpp" "ChainValidationService")
forbid_text("src/net_processing.cpp" "ChainValidationService")
forbid_text("src/node/ibd_block_processor.cpp" "ChainValidationService")
forbid_text("src/node/interfaces.cpp" "ChainValidationService")
forbid_text("src/node/miner.cpp" "ChainValidationService")
require_text("src/validation/chain_validation.h" "TestActiveBlockValidity(")
require_text("src/validation/chain_validation.h" "TestActiveBlockValidityLocked(")
forbid_text("src/validation/chain_validation.h" "Chainstate&")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlockHeaders(")
require_text("src/validation/block_validation_internal.h" "AcceptBlock(")
require_text("src/validation/block_validation_internal.h" "ProcessNewBlock(")
require_text("src/validation/test_block_validity.h" "struct TestBlockValidityRequest")
require_text("src/validation/test_block_validity.h" "TestBlockValidity(")
require_text("src/validation/block_validation_internal.h" "CoreHeaderAdmissionRuntime& runtime")
require_text("src/validation/block_validation_internal.h" "CoreBlockDataAdmissionRuntime& runtime")
require_text("src/validation/block_validation_internal.h" "CoreAcceptedContextReader& reader")
require_text("src/validation/block_validation_internal.h" "CoreActivationRuntime& runtime")
forbid_text("src/validation/block_validation_internal.h" "CoreChainValidationContext")
require_text("src/validation/active_chain.h" "class ActiveChainView")
forbid_text("src/validation/block_validation_internal.h" "ChainstateManager&")
require_text("src/validation/block_data_admission.h" "struct BlockDataAdmissionContext")
require_text("src/CMakeLists.txt" "block_index_adapters.cpp")
require_text("src/validation/block_storage.h" "class BlockDataReader")
require_text("src/validation/block_storage.h" "class BlockUndoReader")
require_text("src/validation/block_storage.h" "class BlockUndoWriter")
require_text("src/validation/block_storage.h" "class BlockDataWriter")
require_text("src/validation/block_storage.h" "class BlockDataAvailability")
require_text("src/validation/block_data_adapters.h" "class CoreBlockDataStore")
require_text("src/validation/block_index.h" "class BlockIndexView")
require_text("src/validation/block_index_adapters.h" "class CoreBlockIndexView")
require_text("src/validation/block_index.h" "class BlockIndexLookup")
require_text("src/validation/block_index.h" "class BlockIndexHeaderStore")
require_text("src/validation/block_index.h" "class BlockIndexDataReceiver")
require_text("src/validation/block_index.h" "class BlockIndexValidityCommitter")
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
forbid_text("src/validation/core_block_connection_context.h" "CoreChainValidationContext")
require_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection")
require_text("src/validation/core_block_connection_context.h" "const CoreBlockConnectionPolicySnapshot& policy")
forbid_text("src/validation/core_block_connection_context.h" "SnapshotCoreBlockConnectionPolicy(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.cpp" "SnapshotCoreBlockConnectionPolicy(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.h" "PlanCoreBlockConnection(ChainstateManager&")
forbid_text("src/validation/core_block_connection_context.cpp" "PlanCoreBlockConnection(ChainstateManager&")
require_text("src/validation/core_block_connection_context.h" "MaybeLogCoreBlockConnectionScriptPolicy")
require_text("src/validation/core_block_connection_setup.h" "class CoreBlockConnectionSetup")
require_text("src/validation/core_block_connection_setup.h" "struct CoreBlockConnectionRuntimeInputs")
require_text("src/validation/core_block_connection_setup.h" "kernel::Notifications& m_notifications")
require_text("src/validation/core_block_connection_setup.h" "CoreBlockConnectionPlan m_connection_plan")
require_text("src/validation/core_block_connection_setup.h" "validation::BlockConnectionState& connection_state")
require_text("src/validation/core_block_connection_setup.h" "CoreBlockScriptChecks m_script_checks")
require_text("src/validation/core_block_connection_setup.h" "BlockConnectionTrace& m_trace")
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
require_text("src/validation/verify_db.h" "struct VerifyDBRequest")
require_text("src/validation/verify_db.h" "class CVerifyDB")
forbid_text("src/chainstate.h" "struct VerifyDBRequest")
require_text("src/chainstate.h" "enum class RawBlockDataReadError")
forbid_text("src/chainstate.h" "kernel::ReadRawError")
require_text("src/validation/block_validation.cpp" "CVerifyDB::VerifyDB(\n    VerifyDBRequest request")
require_text("src/node/miner.h" "GenerateCoinbaseCommitment")
forbid_text("src/validation/block_data_adapters.h" "class BlockDataStore")
forbid_text("src/validation/block_index_adapters.h" "class BlockIndexStore")
forbid_text("src/validation/block_storage.h" "CoreBlockDataStore")
forbid_text("src/validation/block_index.h" "CoreBlockIndexStore")
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
require_text("src/chainstate.cpp" "class ChainstateActivationOrchestrator")
require_text("src/chainstate.cpp" "ChainstateActivationOrchestrator{*this")
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
forbid_text("src/chainstate.h" "m_blockman.m_block_index")
forbid_text("src/chainstate.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/chainstate.cpp" "m_dirty_blockindex")
forbid_text("src/validation/core_block_policy.cpp" "m_blockman.m_block_index")
forbid_text("src/kernel/bitcoinkernel.cpp" "m_blockman.LookupBlockIndex(")
forbid_text("src/node/miner.cpp" "m_blockman.LookupBlockIndex(")
