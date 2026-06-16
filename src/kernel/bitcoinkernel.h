// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_BITCOINKERNEL_H
#define BITCOIN_KERNEL_BITCOINKERNEL_H

#ifndef __cplusplus
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif // __cplusplus

#ifndef BITCOINKERNEL_API
#ifdef BITCOINKERNEL_BUILD
#if defined(_WIN32)
#define BITCOINKERNEL_API __declspec(dllexport)
#else
#define BITCOINKERNEL_API __attribute__((visibility("default")))
#endif
#else
#if defined(_WIN32) && !defined(BITCOINKERNEL_STATIC)
#define BITCOINKERNEL_API __declspec(dllimport)
#else
#define BITCOINKERNEL_API
#endif
#endif
#endif

/**
 * BITCOINKERNEL_WARN_UNUSED_RESULT is a compiler attribute used to indicate
 * that ignoring a function's return value is almost certainly a bug.
 *
 * It is used in cases such as a resource leak (e.g. an owning handle returned
 * by a *_create or *_copy function), or when the returned value is itself an
 * error/status code. It is not used merely because discarding the result is
 * wasteful, e.g. on getters or predicates.
 */
#if defined(__GNUC__)
#define BITCOINKERNEL_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define BITCOINKERNEL_WARN_UNUSED_RESULT
#endif

/**
 * BITCOINKERNEL_ARG_NONNULL is a compiler attribute used to indicate that
 * certain pointer arguments to a function are not expected to be null.
 *
 * Callers must not pass a null pointer for arguments marked with this attribute,
 * as doing so may result in undefined behavior. This attribute should only be
 * used for arguments where a null pointer is unambiguously a programmer error,
 * such as for opaque handles, and not for pointers to raw input data that might
 * validly be null (e.g., from an empty std::span or std::string).
 */
#if !defined(BITCOINKERNEL_BUILD) && defined(__GNUC__)
#define BITCOINKERNEL_ARG_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define BITCOINKERNEL_ARG_NONNULL(...)
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @page remarks Remarks
 *
 * @section purpose Purpose
 *
 * This header currently exposes an API for interacting with parts of Bitcoin
 * Core's consensus code. Users can validate blocks, inspect chain snapshots,
 * read block and undo data from disk, and validate scripts. The header is
 * unversioned and not stable yet. Users should expect breaking changes. It is
 * also not yet included in releases of Bitcoin Core.
 *
 * @section context Context
 *
 * The library provides a built-in static constant kernel context. This static
 * context offers only limited functionality. It detects and self-checks the
 * correct sha256 implementation, initializes the random number generator and
 * self-checks the secp256k1 static context. It is used internally for
 * otherwise "context-free" operations. This means that the user is not
 * required to initialize their own context before using the library.
 *
 * The user should create their own context for passing it to state-rich validation
 * functions and holding callbacks for kernel events.
 *
 * @section error Error handling
 *
 * Functions that can fail operationally take a nullable btck_Error** as their
 * final parameter. If provided, the out parameter is set to NULL on success or
 * expected negative results, and to a newly allocated btck_Error on operational
 * failure. The caller owns that error and must destroy it with
 * btck_error_destroy.
 *
 * Validation-invalid results are not operational failures. They are
 * communicated through validation states or domain status codes, e.g. script
 * verification status values.
 *
 * Fine-grained validation information is communicated through the validation
 * interface.
 *
 * The kernel notifications issue callbacks for errors. These are usually
 * indicative of a system error. If such an error is issued, it is recommended
 * to halt and tear down the existing kernel objects. Remediating the error may
 * require system intervention by the user.
 *
 * @section pointer Pointer and argument conventions
 *
 * The user is responsible for de-allocating the memory owned by pointers
 * returned by functions. Typically pointers returned by *_create(...) functions
 * can be de-allocated by corresponding *_destroy(...) functions.
 *
 * A function that takes pointer arguments makes no assumptions on their
 * lifetime. Once the function returns the user can safely de-allocate the
 * passed in arguments.
 *
 * Const pointers represent views, and do not transfer ownership. Lifetime
 * guarantees of these objects are described in the respective documentation.
 * Ownership of these resources may be taken by copying. They are typically
 * used for iteration with minimal overhead and require some care by the
 * programmer that their lifetime is not extended beyond that of the original
 * object.
 *
 * Array lengths follow the pointer argument they describe.
 *
 * @section types Type conventions
 *
 * Fixed-width integer types (e.g. int32_t, uint32_t) are used for data values
 * such as heights. Plain int and unsigned int are used for boolean-like values
 * and flags.
 */

/**
 * Opaque data structure for holding a transaction.
 */
typedef struct btck_Transaction btck_Transaction;

/**
 * Opaque data structure for holding a script pubkey.
 */
typedef struct btck_ScriptPubkey btck_ScriptPubkey;

/**
 * Opaque data structure for holding a transaction output.
 */
typedef struct btck_TransactionOutput btck_TransactionOutput;

/**
 * Opaque data structure for holding a logging connection.
 *
 * The logging connection can be used to manually stop logging.
 *
 * Messages that were logged before a connection is created are buffered in a
 * 1MB buffer. Logging can alternatively be permanently disabled by calling
 * @ref btck_logging_disable. Functions changing the logging settings are
 * global and change the settings for all existing btck_LoggingConnection
 * instances.
 */
typedef struct btck_LoggingConnection btck_LoggingConnection;

/**
 * Opaque data structure for holding the chain parameters.
 *
 * These are eventually placed into a kernel context through the kernel context
 * options. The parameters describe the properties of a chain, and may be
 * instantiated for either mainnet, testnet, signet, or regtest.
 */
typedef struct btck_ChainParameters btck_ChainParameters;

/**
 * Opaque data structure for holding options for creating a new kernel context.
 *
 * Once a kernel context has been created from these options, they may be
 * destroyed. The options hold the notification and validation interface
 * callbacks as well as the selected chain type until they are passed to the
 * context. If no options are configured, the context will be instantiated with
 * no callbacks and for mainnet. Their content and scope can be expanded over
 * time.
 */
typedef struct btck_ContextOptions btck_ContextOptions;

/**
 * Opaque data structure for holding a kernel context.
 *
 * The kernel context is used to initialize internal state and hold the chain
 * parameters and callbacks for handling error and validation events. Once
 * other validation objects are instantiated from it, the context is kept in
 * memory for the duration of their lifetimes.
 *
 * The processing of validation events is done through an internal task runner
 * owned by the context. It passes events through the registered validation
 * interface callbacks.
 *
 * A constructed context can be safely used from multiple threads.
 */
typedef struct btck_Context btck_Context;

/**
 * Opaque data structure for holding an operational API error.
 *
 * btck_Error reports operational failures such as invalid runtime options, disk
 * I/O failure, callback writer failure, and exceptions caught at the C ABI
 * boundary. It is not used for ordinary validation invalidity or malformed
 * external bytes.
 */
typedef struct btck_Error btck_Error;

/**
 * Opaque value containing immutable facts about a block index entry.
 *
 * A btck_BlockInfo owns a snapshot of the block hash, previous block hash,
 * height, and header at the time it was created. It does not point into the
 * chainstate's internal block index. Callback-provided btck_BlockInfo pointers
 * are views valid only for the duration of the callback; copy them if they need
 * to be retained.
 */
typedef struct btck_BlockInfo btck_BlockInfo;

/**
 * Opaque data structure for holding chainstate options.
 *
 * Chainstate options configure storage paths and validation runtime resources
 * before a chainstate is opened.
 */
typedef struct btck_ChainstateOptions btck_ChainstateOptions;

/**
 * Opaque data structure for holding chainstate runtime inputs.
 *
 * These options let callers provide operation-specific inputs such as current
 * time for chain activation and import operations without mutating
 * process-global kernel state.
 */
typedef struct btck_ChainstateRuntime btck_ChainstateRuntime;

/**
 * Opaque data structure for holding block validation runtime inputs.
 *
 * These options let callers provide operation-specific inputs such as current
 * time without mutating process-global kernel state.
 */
typedef struct btck_BlockValidationOptions btck_BlockValidationOptions;

/**
 * Opaque data structure for holding a chainstate.
 *
 * A chainstate is an owned, mutable validation state handle. Operations on it
 * are explicit state transitions using caller-provided runtime inputs.
 */
typedef struct btck_Chainstate btck_Chainstate;

/**
 * Opaque data structure for holding a block.
 */
typedef struct btck_Block btck_Block;

/**
 * Opaque data structure for holding the state of a block during validation.
 *
 * Contains information indicating whether validation was successful, and if not
 * which step during block validation failed.
 */
typedef struct btck_BlockValidationState btck_BlockValidationState;

/**
 * Opaque data structure for holding the Consensus Params.
 */
typedef struct btck_ConsensusParams btck_ConsensusParams;

/**
 * Opaque value containing an immutable snapshot of the active chain.
 *
 * The snapshot owns a sequence of btck_BlockInfo values ordered from genesis
 * to tip. It is unaffected by later chainstate mutations.
 */
typedef struct btck_ChainSnapshot btck_ChainSnapshot;

/**
 * Opaque data structure for holding the state of a transaction during validation.
 *
 * Contains information indicating whether validation was successful, and if not
 * which step during transaction validation failed.
 */
typedef struct btck_TxValidationState btck_TxValidationState;

/**
 * Opaque data structure for holding a block's spent outputs.
 *
 * Contains all the previous outputs consumed by all transactions in a specific
 * block. Internally it holds a nested vector. The top level vector has an
 * entry for each transaction in a block (in order of the actual transactions
 * of the block and without the coinbase transaction). This is exposed through
 * @ref btck_TransactionSpentOutputs. Each btck_TransactionSpentOutputs is in
 * turn a vector of all the previous outputs of a transaction (in order of
 * their corresponding inputs).
 */
typedef struct btck_BlockSpentOutputs btck_BlockSpentOutputs;

/**
 * Opaque data structure for holding a transaction's spent outputs.
 *
 * Holds the coins consumed by a certain transaction. Retrieved through the
 * @ref btck_BlockSpentOutputs. The coins are in the same order as the
 * transaction's inputs consuming them.
 */
typedef struct btck_TransactionSpentOutputs btck_TransactionSpentOutputs;

/**
 * Opaque data structure for holding a coin.
 *
 * Holds information on the @ref btck_TransactionOutput held within,
 * including the height it was spent at and whether it is a coinbase output.
 */
typedef struct btck_Coin btck_Coin;

/**
 * Opaque data structure for holding a block hash.
 *
 * This is a type-safe identifier for a block.
 */
typedef struct btck_BlockHash btck_BlockHash;

/**
 * Opaque data structure for holding a transaction input.
 *
 * Holds information on the @ref btck_TransactionOutPoint held within.
 */
typedef struct btck_TransactionInput btck_TransactionInput;

/**
 * Opaque data structure for holding a transaction out point.
 *
 * Holds the txid and output index it is pointing to.
 */
typedef struct btck_TransactionOutPoint btck_TransactionOutPoint;

/**
 * Opaque data structure for holding precomputed transaction data.
 *
 * Reusable when verifying multiple inputs of the same transaction.
 * This avoids recomputing transaction hashes for each input.
 *
 * Required when verifying a taproot input.
 */
typedef struct btck_PrecomputedTransactionData btck_PrecomputedTransactionData;

/**
 * Opaque data structure for holding a btck_Txid.
 *
 * This is a type-safe identifier for a transaction.
 */
typedef struct btck_Txid btck_Txid;

/**
 * Opaque data structure for holding a btck_BlockHeader.
 */
typedef struct btck_BlockHeader btck_BlockHeader;

/**
 * Opaque parse result handles.
 *
 * Successful parse results own the parsed object. Accessors return borrowed
 * views valid for the lifetime of the result; copy the object before retaining
 * it after destroying the result.
 */
typedef struct btck_TransactionParseResult btck_TransactionParseResult;
typedef struct btck_BlockParseResult btck_BlockParseResult;
typedef struct btck_BlockHeaderParseResult btck_BlockHeaderParseResult;

/**
 * Opaque context-free validation result handles.
 */
typedef struct btck_TransactionCheckResult btck_TransactionCheckResult;
typedef struct btck_BlockCheckResult btck_BlockCheckResult;
typedef struct btck_BlockVerifyResult btck_BlockVerifyResult;

/**
 * Opaque chainstate operation result handles.
 */
typedef struct btck_HeaderProcessResult btck_HeaderProcessResult;
typedef struct btck_BlockProcessResult btck_BlockProcessResult;
typedef struct btck_BlockImportResult btck_BlockImportResult;
typedef struct btck_BlockReadResult btck_BlockReadResult;
typedef struct btck_BlockSpentOutputsReadResult btck_BlockSpentOutputsReadResult;

/** Current sync state passed to tip changed callbacks. */
typedef uint8_t btck_SynchronizationState;
#define btck_SynchronizationState_INIT_REINDEX ((btck_SynchronizationState)(0))
#define btck_SynchronizationState_INIT_DOWNLOAD ((btck_SynchronizationState)(1))
#define btck_SynchronizationState_POST_INIT ((btck_SynchronizationState)(2))

/** Possible warning types issued by validation. */
typedef uint8_t btck_Warning;
#define btck_Warning_UNKNOWN_NEW_RULES_ACTIVATED ((btck_Warning)(0))
#define btck_Warning_LARGE_WORK_INVALID_CHAIN ((btck_Warning)(1))

/** Callback function types */

/**
 * Callback contract.
 *
 * The callback table is copied when it is set on context options. If
 * user_data_destroy is provided, ownership of user_data transfers only when the
 * setter returns success; otherwise the caller must keep user_data alive for as
 * long as callbacks can be invoked.
 *
 * Callback-provided btck_* pointers, string pointers, and byte views are
 * borrowed views valid only for the duration of the callback. Copy the object
 * with its copy function before retaining it beyond the callback.
 *
 * Validation interface callbacks preserve generated event order for each subscriber,
 * but not across subscribers. Kernel notification callbacks are
 * emitted from the operation that reports them and are not globally serialized
 * by the validation interface queue.
 *
 * Event callbacks must return 0 on success and non-zero on failure. A callback
 * failure is an operational notification failure, not a validation result and
 * not a rollback signal. State transitions completed before callback delivery
 * remain committed; the failure is translated to btck_ErrorCode_CALLBACK at the
 * recoverable operation boundary. Callbacks must return promptly, must
 * synchronize their own user_data if it is shared, and must not call back into
 * mutating kernel APIs unless that API explicitly documents reentrant use. No
 * exception may cross a C callback boundary.
 */

/**
 * Function signature for the global logging callback. All bitcoin kernel
 * internal logs will pass through this callback.
 */
typedef void (*btck_LogCallback)(void* user_data, const char* message, size_t message_len);

/**
 * Function signature for freeing user data.
 */
typedef void (*btck_DestroyCallback)(void* user_data);

/**
 * Function signatures for the kernel notifications.
 */
typedef int (*btck_NotifyBlockTip)(void* user_data, btck_SynchronizationState state, const btck_BlockInfo* tip, double verification_progress);
typedef int (*btck_NotifyHeaderTip)(void* user_data, btck_SynchronizationState state, int64_t height, int64_t timestamp, int presync);
typedef int (*btck_NotifyProgress)(void* user_data, const char* title, size_t title_len, int progress_percent, int resume_possible);
typedef int (*btck_NotifyWarningSet)(void* user_data, btck_Warning warning, const char* message, size_t message_len);
typedef int (*btck_NotifyWarningUnset)(void* user_data, btck_Warning warning);
typedef int (*btck_NotifyFlushError)(void* user_data, const char* message, size_t message_len);
typedef int (*btck_NotifyFatalError)(void* user_data, const char* message, size_t message_len);

/**
 * Function signatures for the validation interface.
 */
typedef int (*btck_ValidationInterfaceBlockChecked)(void* user_data, const btck_Block* block, const btck_BlockValidationState* state);
typedef int (*btck_ValidationInterfacePoWValidBlock)(void* user_data, const btck_Block* block, const btck_BlockInfo* info);
typedef int (*btck_ValidationInterfaceBlockConnected)(void* user_data, const btck_Block* block, const btck_BlockInfo* info);
typedef int (*btck_ValidationInterfaceBlockDisconnected)(void* user_data, const btck_Block* block, const btck_BlockInfo* info);

/**
 * Function signature for serializing data.
 *
 * Returns 0 to indicate success.
 */
typedef int (*btck_WriteBytes)(const void* bytes, size_t size, void* userdata);

/**
 * Whether a validated data structure is valid, invalid, or an error was
 * encountered during processing.
 */
typedef uint8_t btck_ValidationMode;
#define btck_ValidationMode_VALID ((btck_ValidationMode)(0))
#define btck_ValidationMode_INVALID ((btck_ValidationMode)(1))
#define btck_ValidationMode_INTERNAL_ERROR ((btck_ValidationMode)(2))

/**
 * Operational API error code.
 */
typedef uint32_t btck_ErrorCode;
#define btck_ErrorCode_NONE ((btck_ErrorCode)(0))
#define btck_ErrorCode_EXCEPTION ((btck_ErrorCode)(1))           //!< A C++ exception was caught at the C ABI boundary.
#define btck_ErrorCode_RESOURCE_EXHAUSTION ((btck_ErrorCode)(2)) //!< The operation could not allocate or reserve required resources.
#define btck_ErrorCode_INVALID_ARGUMENT ((btck_ErrorCode)(3))    //!< A runtime option or flag was outside the API's domain.
#define btck_ErrorCode_IO ((btck_ErrorCode)(4))                  //!< Disk-backed data could not be read or written; use narrower IO codes when returned.
#define btck_ErrorCode_CALLBACK ((btck_ErrorCode)(5))            //!< A caller-provided callback reported failure.
#define btck_ErrorCode_CHAINSTATE_LOAD ((btck_ErrorCode)(6))     //!< Chainstate loading or verification failed; use narrower storage codes when returned.
#define btck_ErrorCode_IO_READ ((btck_ErrorCode)(7))             //!< Disk-backed data could not be read.
#define btck_ErrorCode_IO_WRITE ((btck_ErrorCode)(8))            //!< Disk-backed data could not be written.
#define btck_ErrorCode_DATA_UNAVAILABLE ((btck_ErrorCode)(9))    //!< Requested chain data is indexed but unavailable or pruned.
#define btck_ErrorCode_STORAGE_CORRUPTION ((btck_ErrorCode)(10)) //!< Stored chain data failed verification or was malformed.
#define btck_ErrorCode_INTERRUPTED ((btck_ErrorCode)(11))        //!< The operation was interrupted before completion.

/** Parse result status for serialized transaction, block, and block-header bytes. */
typedef uint8_t btck_ParseStatus;
#define btck_ParseStatus_OK ((btck_ParseStatus)(0))        //!< Bytes parsed into the requested object.
#define btck_ParseStatus_MALFORMED ((btck_ParseStatus)(1)) //!< Bytes are not the exact canonical object encoding.

/** Malformed parse reason for serialized transaction, block, and block-header bytes. */
typedef uint8_t btck_ParseFailureCode;
#define btck_ParseFailureCode_NONE ((btck_ParseFailureCode)(0))                       //!< No parse failure; status is OK.
#define btck_ParseFailureCode_TRUNCATED ((btck_ParseFailureCode)(1))                  //!< The byte stream ended before the object encoding was complete.
#define btck_ParseFailureCode_TRAILING_DATA ((btck_ParseFailureCode)(2))              //!< Extra bytes remained after a complete object encoding.
#define btck_ParseFailureCode_NON_CANONICAL_COMPACT_SIZE ((btck_ParseFailureCode)(3)) //!< CompactSize used a non-canonical encoding.
#define btck_ParseFailureCode_COMPACT_SIZE_OVERFLOW ((btck_ParseFailureCode)(4))      //!< CompactSize exceeded this API's supported size domain.
#define btck_ParseFailureCode_INVALID_WITNESS_MARKER ((btck_ParseFailureCode)(5))     //!< Transaction witness marker/flag encoding was invalid.

/** Context-free validation status for block and transaction check results. */
typedef uint8_t btck_CheckStatus;
#define btck_CheckStatus_VALID ((btck_CheckStatus)(0))   //!< The object passed the requested validation checks.
#define btck_CheckStatus_INVALID ((btck_CheckStatus)(1)) //!< The object failed validation; inspect the validation state.

/** Validation-library rejection class. */
typedef uint8_t btck_ValidationRejectionCode;
#define btck_ValidationRejectionCode_NONE ((btck_ValidationRejectionCode)(0))           //!< No validation rejection.
#define btck_ValidationRejectionCode_RULE_VIOLATION ((btck_ValidationRejectionCode)(1)) //!< A consensus rule was violated.

/** Stable validation-library consensus rule identifiers. */
typedef uint8_t btck_ValidationRule;
#define btck_ValidationRule_NONE ((btck_ValidationRule)(0))
#define btck_ValidationRule_H01_PREVIOUS_HASH_PARENT ((btck_ValidationRule)(1))
#define btck_ValidationRule_H02_PROOF_OF_WORK ((btck_ValidationRule)(2))
#define btck_ValidationRule_H03_DIFFICULTY_TRANSITION ((btck_ValidationRule)(3))
#define btck_ValidationRule_H04_MEDIAN_TIME_PAST ((btck_ValidationRule)(4))
#define btck_ValidationRule_H05_FUTURE_TIME ((btck_ValidationRule)(5))
#define btck_ValidationRule_H06_RETIRED_VERSION ((btck_ValidationRule)(6))
#define btck_ValidationRule_H07_TIMEWARP ((btck_ValidationRule)(7))
#define btck_ValidationRule_L01_BLOCK_NON_EMPTY ((btck_ValidationRule)(8))
#define btck_ValidationRule_L02_MERKLE_ROOT ((btck_ValidationRule)(9))
#define btck_ValidationRule_L03_MERKLE_MUTATION ((btck_ValidationRule)(10))
#define btck_ValidationRule_L04_ORIGINAL_BLOCK_SIZE ((btck_ValidationRule)(11))
#define btck_ValidationRule_L05_COINBASE_POSITION ((btck_ValidationRule)(12))
#define btck_ValidationRule_L06_LEGACY_SIGOPS ((btck_ValidationRule)(13))
#define btck_ValidationRule_L07_TRANSACTION_INPUTS_NON_EMPTY ((btck_ValidationRule)(14))
#define btck_ValidationRule_L08_TRANSACTION_OUTPUTS_NON_EMPTY ((btck_ValidationRule)(15))
#define btck_ValidationRule_L09_TRANSACTION_SIZE ((btck_ValidationRule)(16))
#define btck_ValidationRule_L10_OUTPUT_VALUE_NON_NEGATIVE ((btck_ValidationRule)(17))
#define btck_ValidationRule_L11_OUTPUT_VALUE_RANGE ((btck_ValidationRule)(18))
#define btck_ValidationRule_L12_UNIQUE_INPUTS ((btck_ValidationRule)(19))
#define btck_ValidationRule_L13_COINBASE_SCRIPT_SIZE ((btck_ValidationRule)(20))
#define btck_ValidationRule_L14_NON_COINBASE_PREVOUT ((btck_ValidationRule)(21))
#define btck_ValidationRule_C01_TRANSACTION_FINALITY ((btck_ValidationRule)(22))
#define btck_ValidationRule_C02_PRE_SEGWIT_NO_WITNESS ((btck_ValidationRule)(23))
#define btck_ValidationRule_C03_BLOCK_WEIGHT ((btck_ValidationRule)(24))
#define btck_ValidationRule_C04_COINBASE_HEIGHT ((btck_ValidationRule)(25))
#define btck_ValidationRule_C05_WITNESS_COMMITMENT_PRESENCE ((btck_ValidationRule)(26))
#define btck_ValidationRule_C06_WITNESS_NONCE_PRESENCE ((btck_ValidationRule)(27))
#define btck_ValidationRule_C07_WITNESS_MERKLE_COMMITMENT ((btck_ValidationRule)(28))
#define btck_ValidationRule_S01_BIP30_DUPLICATE_UNSPENT ((btck_ValidationRule)(29))
#define btck_ValidationRule_S02_PREVOUTS_UNSPENT ((btck_ValidationRule)(30))
#define btck_ValidationRule_S03_SIGOP_COST ((btck_ValidationRule)(31))
#define btck_ValidationRule_S04_COINBASE_SUBSIDY ((btck_ValidationRule)(32))
#define btck_ValidationRule_S05_OUTPUTS_DO_NOT_EXCEED_INPUTS ((btck_ValidationRule)(33))
#define btck_ValidationRule_S06_INPUT_VALUE_AND_FEE_RANGE ((btck_ValidationRule)(34))
#define btck_ValidationRule_S07_SCRIPTS_VALIDATE ((btck_ValidationRule)(35))
#define btck_ValidationRule_S08_SEQUENCE_LOCKS ((btck_ValidationRule)(36))
#define btck_ValidationRule_S09_COINBASE_MATURITY ((btck_ValidationRule)(37))

/** Invalid caller-supplied header ancestry evidence. */
typedef uint8_t btck_HeaderContextEvidenceCode;
#define btck_HeaderContextEvidenceCode_NONE ((btck_HeaderContextEvidenceCode)(0))                      //!< No invalid header-context evidence.
#define btck_HeaderContextEvidenceCode_GENESIS_PARENT_NOT_NULL ((btck_HeaderContextEvidenceCode)(1))   //!< The supplied genesis ancestor has a non-null parent.
#define btck_HeaderContextEvidenceCode_NON_CONTIGUOUS_ANCESTRY ((btck_HeaderContextEvidenceCode)(2))   //!< The supplied ancestry does not form a contiguous chain.

/** Header processing status. */
typedef uint8_t btck_HeaderProcessStatus;
#define btck_HeaderProcessStatus_ACCEPTED ((btck_HeaderProcessStatus)(0)) //!< Header was accepted into the chainstate.
#define btck_HeaderProcessStatus_REJECTED ((btck_HeaderProcessStatus)(1)) //!< Header was rejected; inspect the validation state.

/** Block processing status. */
typedef uint8_t btck_BlockProcessStatus;
#define btck_BlockProcessStatus_CHECK_FAILED ((btck_BlockProcessStatus)(0))                         //!< Context-free block checks failed.
#define btck_BlockProcessStatus_HEADER_REJECTED ((btck_BlockProcessStatus)(1))                      //!< Header admission rejected the block.
#define btck_BlockProcessStatus_BLOCK_REJECTED ((btck_BlockProcessStatus)(2))                       //!< Block data admission rejected the block.
#define btck_BlockProcessStatus_ALREADY_KNOWN ((btck_BlockProcessStatus)(3))                        //!< Full block data was already known.
#define btck_BlockProcessStatus_STORED ((btck_BlockProcessStatus)(4))                               //!< New block data was stored and processing completed.
#define btck_BlockProcessStatus_UNREQUESTED_PREVIOUSLY_PROCESSED ((btck_BlockProcessStatus)(5))     //!< Unrequested block was already processed.
#define btck_BlockProcessStatus_UNREQUESTED_LESS_WORK_THAN_TIP ((btck_BlockProcessStatus)(6))       //!< Unrequested block has less work than the active tip.
#define btck_BlockProcessStatus_UNREQUESTED_TOO_FAR_AHEAD ((btck_BlockProcessStatus)(7))            //!< Unrequested block is too far ahead.
#define btck_BlockProcessStatus_UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK ((btck_BlockProcessStatus)(8)) //!< Unrequested block is below minimum chain work.

/** Block import status. */
typedef uint8_t btck_BlockImportStatus;
#define btck_BlockImportStatus_COMPLETED ((btck_BlockImportStatus)(0))         //!< Import completed normally.
#define btck_BlockImportStatus_INTERRUPTED ((btck_BlockImportStatus)(1))       //!< Import stopped because the context was interrupted.
#define btck_BlockImportStatus_ALREADY_IMPORTING ((btck_BlockImportStatus)(2)) //!< Another import is already running on this chainstate.
#define btck_BlockImportStatus_RESOURCE_LIMIT ((btck_BlockImportStatus)(3))    //!< Import stopped because a bounded import resource was exhausted.

/** Disk-backed block read status. */
typedef uint8_t btck_BlockReadStatus;
#define btck_BlockReadStatus_FOUND ((btck_BlockReadStatus)(0))            //!< Block bytes were read and are available from the result.
#define btck_BlockReadStatus_NOT_INDEXED ((btck_BlockReadStatus)(1))      //!< Block hash is not indexed by this chainstate.
#define btck_BlockReadStatus_DATA_UNAVAILABLE ((btck_BlockReadStatus)(2)) //!< Block data is indexed but unavailable or pruned.

/** Disk-backed block spent-output read status. */
typedef uint8_t btck_BlockSpentOutputsReadStatus;
#define btck_BlockSpentOutputsReadStatus_FOUND ((btck_BlockSpentOutputsReadStatus)(0))            //!< Spent outputs were read and are available from the result.
#define btck_BlockSpentOutputsReadStatus_NOT_INDEXED ((btck_BlockSpentOutputsReadStatus)(1))      //!< Block hash is not indexed by this chainstate.
#define btck_BlockSpentOutputsReadStatus_DATA_UNAVAILABLE ((btck_BlockSpentOutputsReadStatus)(2)) //!< Undo data is indexed but unavailable or pruned.

/**
 * A granular "reason" why a block was invalid.
 */
typedef uint32_t btck_BlockValidationResult;
#define btck_BlockValidationResult_UNSET ((btck_BlockValidationResult)(0))           //!< initial value. Block has not yet been rejected
#define btck_BlockValidationResult_CONSENSUS ((btck_BlockValidationResult)(1))       //!< invalid by consensus rules (excluding any below reasons)
#define btck_BlockValidationResult_CACHED_INVALID ((btck_BlockValidationResult)(2))  //!< this block was cached as being invalid and we didn't store the reason why
#define btck_BlockValidationResult_INVALID_HEADER ((btck_BlockValidationResult)(3))  //!< invalid proof of work or time too old
#define btck_BlockValidationResult_MUTATED ((btck_BlockValidationResult)(4))         //!< the block's data didn't match the data committed to by the PoW
#define btck_BlockValidationResult_MISSING_PREV ((btck_BlockValidationResult)(5))    //!< We don't have the previous block the checked one is built on
#define btck_BlockValidationResult_INVALID_PREV ((btck_BlockValidationResult)(6))    //!< A block this one builds on is invalid
#define btck_BlockValidationResult_TIME_FUTURE ((btck_BlockValidationResult)(7))     //!< block timestamp was > 2 hours in the future (or our clock is bad)
#define btck_BlockValidationResult_HEADER_LOW_WORK ((btck_BlockValidationResult)(8)) //!< the block header may be on a too-little-work chain

/** Indicates the reason why a context-free transaction check failed. */
typedef uint32_t btck_TxValidationResult;
#define btck_TxValidationResult_UNSET ((btck_TxValidationResult)(0))     //!< initial value. Tx has not yet been rejected
#define btck_TxValidationResult_CONSENSUS ((btck_TxValidationResult)(1)) //!< invalid by consensus rules
#define btck_TxValidationResult_UNKNOWN ((btck_TxValidationResult)(2))   //!< transaction validation result is not exposed by this API

/**
 * Holds the validation interface callbacks. The user data pointer may be used
 * to point to user-defined structures to make processing the validation
 * callbacks easier. These callbacks block the validation signal delivery that
 * invokes them.
 */
typedef struct {
    void* user_data;                                              //!< Holds a user-defined opaque structure that is passed to the validation
                                                                  //!< interface callbacks. If user_data_destroy is also defined ownership of the
                                                                  //!< user_data is passed to the created context options and subsequently context.
    btck_DestroyCallback user_data_destroy;                       //!< Frees the provided user data structure.
    btck_ValidationInterfaceBlockChecked block_checked;           //!< Called when a new block has been fully validated. Contains the
                                                                  //!< result of its validation.
    btck_ValidationInterfacePoWValidBlock pow_valid_block;        //!< Called when a new block extends the header chain and has a valid transaction
                                                                  //!< and segwit merkle root.
    btck_ValidationInterfaceBlockConnected block_connected;       //!< Called when a block is valid and has now been connected to the best chain.
    btck_ValidationInterfaceBlockDisconnected block_disconnected; //!< Called during a re-org when a block has been removed from the best chain.
} btck_ValidationInterfaceCallbacks;

/**
 * A struct for holding the kernel notification callbacks. The user data
 * pointer may be used to point to user-defined structures to make processing
 * the notifications easier.
 *
 * If user_data_destroy is provided, the kernel will automatically call this
 * callback to clean up user_data when the notification interface object is destroyed.
 * If user_data_destroy is NULL, it is the user's responsibility to ensure that
 * the user_data outlives the kernel objects. Notifications can
 * occur even as kernel objects are deleted, so care has to be taken to ensure
 * safe unwinding.
 */
typedef struct {
    void* user_data;                        //!< Holds a user-defined opaque structure that is passed to the notification callbacks.
                                            //!< If user_data_destroy is also defined ownership of the user_data is passed to the
                                            //!< created context options and subsequently context.
    btck_DestroyCallback user_data_destroy; //!< Frees the provided user data structure.
    btck_NotifyBlockTip block_tip;          //!< The chain's tip was updated to the provided block entry.
    btck_NotifyHeaderTip header_tip;        //!< A new best block header was added.
    btck_NotifyProgress progress;           //!< Reports on current block synchronization progress.
    btck_NotifyWarningSet warning_set;      //!< A warning issued by the kernel library during validation.
    btck_NotifyWarningUnset warning_unset;  //!< A previous condition leading to the issuance of a warning is no longer given.
    btck_NotifyFlushError flush_error;      //!< An error encountered when flushing data to disk.
    btck_NotifyFatalError fatal_error;      //!< An unrecoverable system error encountered by the library.
} btck_NotificationInterfaceCallbacks;

/**
 * A collection of logging categories that may be encountered by kernel code.
 */
typedef uint8_t btck_LogCategory;
#define btck_LogCategory_ALL ((btck_LogCategory)(0))
#define btck_LogCategory_BENCH ((btck_LogCategory)(1))
#define btck_LogCategory_BLOCKSTORAGE ((btck_LogCategory)(2))
#define btck_LogCategory_COINDB ((btck_LogCategory)(3))
#define btck_LogCategory_LEVELDB ((btck_LogCategory)(4))
#define btck_LogCategory_MEMPOOL ((btck_LogCategory)(5))
#define btck_LogCategory_PRUNE ((btck_LogCategory)(6))
#define btck_LogCategory_RAND ((btck_LogCategory)(7))
#define btck_LogCategory_REINDEX ((btck_LogCategory)(8))
#define btck_LogCategory_VALIDATION ((btck_LogCategory)(9))
#define btck_LogCategory_KERNEL ((btck_LogCategory)(10))

/**
 * The level at which logs should be produced.
 */
typedef uint8_t btck_LogLevel;
#define btck_LogLevel_TRACE ((btck_LogLevel)(0))
#define btck_LogLevel_DEBUG ((btck_LogLevel)(1))
#define btck_LogLevel_INFO ((btck_LogLevel)(2))

/**
 * Options controlling the format of log messages.
 *
 * Set fields as non-zero to indicate true.
 */
typedef struct {
    int log_timestamps;               //!< Prepend a timestamp to log messages.
    int log_time_micros;              //!< Log timestamps in microsecond precision.
    int log_threadnames;              //!< Prepend the name of the thread to log messages.
    int log_sourcelocations;          //!< Prepend the source location to log messages.
    int always_print_category_levels; //!< Prepend the log category and level to log messages.
} btck_LoggingOptions;

/**
 * A collection of status codes that may be issued by the script verify function.
 */
typedef uint8_t btck_ScriptVerifyStatus;
#define btck_ScriptVerifyStatus_OK ((btck_ScriptVerifyStatus)(0))
#define btck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION ((btck_ScriptVerifyStatus)(1)) //!< The flags were combined in an invalid way.
#define btck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED ((btck_ScriptVerifyStatus)(2))    //!< The taproot flag was set, so valid spent_outputs have to be provided.
#define btck_ScriptVerifyStatus_ERROR_INTERNAL ((btck_ScriptVerifyStatus)(3))                  //!< An internal runtime error prevented script verification.

/**
 * Script verification flags that may be composed with each other.
 */
typedef uint32_t btck_ScriptVerificationFlags;
#define btck_ScriptVerificationFlags_NONE ((btck_ScriptVerificationFlags)(0))
#define btck_ScriptVerificationFlags_P2SH ((btck_ScriptVerificationFlags)(1U << 0))                 //!< evaluate P2SH (BIP16) subscripts
#define btck_ScriptVerificationFlags_DERSIG ((btck_ScriptVerificationFlags)(1U << 2))               //!< enforce strict DER (BIP66) compliance
#define btck_ScriptVerificationFlags_NULLDUMMY ((btck_ScriptVerificationFlags)(1U << 4))            //!< enforce NULLDUMMY (BIP147)
#define btck_ScriptVerificationFlags_CHECKLOCKTIMEVERIFY ((btck_ScriptVerificationFlags)(1U << 9))  //!< enable CHECKLOCKTIMEVERIFY (BIP65)
#define btck_ScriptVerificationFlags_CHECKSEQUENCEVERIFY ((btck_ScriptVerificationFlags)(1U << 10)) //!< enable CHECKSEQUENCEVERIFY (BIP112)
#define btck_ScriptVerificationFlags_WITNESS ((btck_ScriptVerificationFlags)(1U << 11))             //!< enable WITNESS (BIP141)
#define btck_ScriptVerificationFlags_TAPROOT ((btck_ScriptVerificationFlags)(1U << 17))             //!< enable TAPROOT (BIPs 341 & 342)
#define btck_ScriptVerificationFlags_ALL ((btck_ScriptVerificationFlags)(btck_ScriptVerificationFlags_P2SH |                \
                                                                         btck_ScriptVerificationFlags_DERSIG |              \
                                                                         btck_ScriptVerificationFlags_NULLDUMMY |           \
                                                                         btck_ScriptVerificationFlags_CHECKLOCKTIMEVERIFY | \
                                                                         btck_ScriptVerificationFlags_CHECKSEQUENCEVERIFY | \
                                                                         btck_ScriptVerificationFlags_WITNESS |             \
                                                                         btck_ScriptVerificationFlags_TAPROOT))

/**
 * Validation-library C ABI declarations are build-tree experimental. They are
 * not a release ABI while they remain inside bitcoinkernel and while the public
 * option/callback structs lack size, version, and reserved fields. Release
 * promotion requires moving these declarations to bitcoin_validation_c or
 * deleting them from the C ABI surface, then adding ABI version and symbol
 * allowlist gates.
 */

/**
 * Coin lookup result status for side-effect-free validation-library block
 * verification. Missing and spent coins are consensus-invalid inputs; the
 * unavailable, malformed, interrupted, and I/O states are operational failures.
 */
typedef uint8_t btck_CoinLookupStatus;
#define btck_CoinLookupStatus_FOUND ((btck_CoinLookupStatus)(0))
#define btck_CoinLookupStatus_MISSING ((btck_CoinLookupStatus)(1))
#define btck_CoinLookupStatus_SPENT ((btck_CoinLookupStatus)(2))
#define btck_CoinLookupStatus_UNAVAILABLE ((btck_CoinLookupStatus)(3))
#define btck_CoinLookupStatus_MALFORMED_STORED_DATA ((btck_CoinLookupStatus)(4))
#define btck_CoinLookupStatus_INTERRUPTED ((btck_CoinLookupStatus)(5))
#define btck_CoinLookupStatus_IO_FAILURE ((btck_CoinLookupStatus)(6))

typedef struct {
    btck_CoinLookupStatus status; //!< Lookup outcome.
    const btck_Coin* coin;        //!< Borrowed coin view; required only when status is FOUND.
} btck_CoinLookupResult;

/**
 * Lookup one coin by outpoint. Return 0 after filling @p result, non-zero for
 * callback failure. The outpoint and result pointers are borrowed for the
 * duration of the call.
 */
typedef int (*btck_ValidationCoinLookup)(
    void* user_data,
    const btck_TransactionOutPoint* out_point,
    btck_CoinLookupResult* result);

/**
 * Borrowed coin-index callback table used for one synchronous block validation
 * operation. The caller retains ownership of user_data.
 */
typedef struct {
    void* user_data;
    btck_ValidationCoinLookup lookup;
} btck_ValidationCoinIndex;

/** Script verification flags for side-effect-free validation-library calls. */
typedef uint64_t btck_ValidationScriptFlags;
#define btck_ValidationScriptFlags_NONE ((btck_ValidationScriptFlags)(0))
#define btck_ValidationScriptFlags_P2SH ((btck_ValidationScriptFlags)(1ULL << 0))
#define btck_ValidationScriptFlags_WITNESS ((btck_ValidationScriptFlags)(1ULL << 1))
#define btck_ValidationScriptFlags_ALL ((btck_ValidationScriptFlags)(btck_ValidationScriptFlags_P2SH | btck_ValidationScriptFlags_WITNESS))

/**
 * Explicit runtime and deployment inputs for validation-library block
 * verification. This API intentionally does not read chainstate or node policy.
 */
typedef struct {
    int64_t operation_time;                  //!< Unix timestamp used for future-time header checks.
    int segwit_active;                       //!< Non-zero when segwit contextual checks are active.
    int height_in_coinbase_active;           //!< Non-zero when BIP34 coinbase height checks are active.
    int enforce_bip30;                       //!< Non-zero to reject duplicate unspent outputs.
    int64_t subsidy;                         //!< Expected block subsidy in satoshis, excluding fees.
    int32_t coinbase_maturity;               //!< Required coinbase spend depth.
    btck_ValidationScriptFlags script_flags; //!< Script flags used by the built-in validation script engine.
    uint64_t max_sigop_cost;                 //!< Maximum total block sigop cost; 0 selects the consensus default.
} btck_BlockValidationLibraryOptions;

typedef uint8_t btck_ChainType;
#define btck_ChainType_MAINNET ((btck_ChainType)(0))
#define btck_ChainType_TESTNET ((btck_ChainType)(1))
#define btck_ChainType_TESTNET_4 ((btck_ChainType)(2))
#define btck_ChainType_SIGNET ((btck_ChainType)(3))
#define btck_ChainType_REGTEST ((btck_ChainType)(4))

/** @name Error
 *  Introspection for operational API errors.
 */
///@{

/**
 * Returns the operational error code.
 *
 * @param[in] error Non-null.
 * @return          The error code.
 */
BITCOINKERNEL_API btck_ErrorCode btck_error_get_code(
    const btck_Error* error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Returns the operational error message.
 *
 * @param[in]  error       Non-null.
 * @param[out] message_len Nullable, set to the message byte length when provided.
 * @return                 A pointer valid for the lifetime of error.
 */
BITCOINKERNEL_API const char* btck_error_get_message(
    const btck_Error* error,
    size_t* message_len) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy an operational API error.
 */
BITCOINKERNEL_API void btck_error_destroy(btck_Error* error);

///@}

/** @name TxValidationState
 *  Introspection for transaction validation state.
 */
///@{

/**
 * Create a new btck_TxValidationState.
 *
 * @param[out] error Nullable, set on operational failure.
 * @return           The validation state, or null on error.
 */
BITCOINKERNEL_API btck_TxValidationState* BITCOINKERNEL_WARN_UNUSED_RESULT btck_tx_validation_state_create(
    btck_Error** error);

/**
 * Copy a btck_TxValidationState.
 *
 * @param[in]  state Non-null.
 * @param[out] error Nullable, set on operational failure.
 * @return           The copied validation state, or null on error.
 */
BITCOINKERNEL_API btck_TxValidationState* BITCOINKERNEL_WARN_UNUSED_RESULT btck_tx_validation_state_copy(
    const btck_TxValidationState* state,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Returns the validation mode from an opaque btck_TxValidationState pointer.
 */
BITCOINKERNEL_API btck_ValidationMode btck_tx_validation_state_get_validation_mode(
    const btck_TxValidationState* state) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Returns the validation result from an opaque btck_TxValidationState pointer.
 *
 * btck_transaction_check_result currently produces only
 * btck_TxValidationResult_UNSET for valid transactions and
 * btck_TxValidationResult_CONSENSUS for invalid ones. Other values remain
 * exposed for forward compatibility with higher-level validation entry points.
 */
BITCOINKERNEL_API btck_TxValidationResult btck_tx_validation_state_get_tx_validation_result(
    const btck_TxValidationState* state) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the btck_TxValidationState.
 */
BITCOINKERNEL_API void btck_tx_validation_state_destroy(btck_TxValidationState* state);

///@}

/** @name Transaction
 * Functions for working with transactions.
 */
///@{

/**
 * @brief Parse a new transaction from serialized data.
 *
 * @param[in] raw_transaction     Serialized transaction.
 * @param[in] raw_transaction_len Length of the serialized transaction.
 * @param[out] error              Nullable, set only on operational failure.
 * @return                        Parse result handle, or null on operational failure.
 */
BITCOINKERNEL_API btck_TransactionParseResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_parse_result(
    const void* raw_transaction, size_t raw_transaction_len,
    btck_Error** error);

/**
 * Return the parse status.
 */
BITCOINKERNEL_API btck_ParseStatus btck_transaction_parse_result_get_status(
    const btck_TransactionParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the malformed parse reason, or btck_ParseFailureCode_NONE when status
 * is btck_ParseStatus_OK.
 */
BITCOINKERNEL_API btck_ParseFailureCode btck_transaction_parse_result_get_failure_code(
    const btck_TransactionParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the byte offset at which malformed parsing failed, or 0 when status is
 * btck_ParseStatus_OK.
 */
BITCOINKERNEL_API size_t btck_transaction_parse_result_get_failure_offset(
    const btck_TransactionParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the parsed transaction, or null unless status is btck_ParseStatus_OK.
 *
 * The returned pointer is borrowed and valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_Transaction* btck_transaction_parse_result_get_transaction(
    const btck_TransactionParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a transaction parse result.
 */
BITCOINKERNEL_API void btck_transaction_parse_result_destroy(
    btck_TransactionParseResult* result);

/**
 * @brief Copy a transaction. Transactions are reference counted, so this just
 * increments the reference count.
 *
 * @param[in]  transaction Non-null.
 * @param[out] error       Nullable, set on operational failure.
 * @return                 The copied transaction.
 */
BITCOINKERNEL_API btck_Transaction* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_copy(
    const btck_Transaction* transaction,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Serializes the transaction through the passed in callback to bytes.
 * This is consensus serialization that is also used for the P2P network.
 *
 * @param[in] transaction Non-null.
 * @param[in] writer      Non-null, callback to a write bytes function.
 * @param[in] user_data   Holds a user-defined opaque structure that will be
 *                        passed back through the writer callback.
 * @param[out] error      Nullable, set on writer callback or operational failure.
 * @return                0 on success.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_serialize(
    const btck_Transaction* transaction,
    btck_WriteBytes writer,
    void* user_data,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Get the number of outputs of a transaction.
 *
 * @param[in] transaction Non-null.
 * @return                The number of outputs.
 */
BITCOINKERNEL_API size_t btck_transaction_count_outputs(
    const btck_Transaction* transaction) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the transaction outputs at the provided index. The returned
 * transaction output is not owned and depends on the lifetime of the
 * transaction.
 *
 * @param[in] transaction  Non-null.
 * @param[in] output_index The index of the transaction output to be retrieved.
 * @return                 The transaction output
 */
BITCOINKERNEL_API const btck_TransactionOutput* btck_transaction_get_output_at(
    const btck_Transaction* transaction, size_t output_index) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the transaction input at the provided index. The returned
 * transaction input is not owned and depends on the lifetime of the
 * transaction.
 *
 * @param[in] transaction Non-null.
 * @param[in] input_index The index of the transaction input to be retrieved.
 * @return                 The transaction input
 */
BITCOINKERNEL_API const btck_TransactionInput* btck_transaction_get_input_at(
    const btck_Transaction* transaction, size_t input_index) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the number of inputs of a transaction.
 *
 * @param[in] transaction Non-null.
 * @return                The number of inputs.
 */
BITCOINKERNEL_API size_t btck_transaction_count_inputs(
    const btck_Transaction* transaction) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get a transaction's nLockTime value.
 *
 * @param[in] transaction Non-null.
 * @return                The nLockTime value.
 */
BITCOINKERNEL_API uint32_t btck_transaction_get_locktime(
    const btck_Transaction* transaction) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the txid of a transaction.
 *
 * @param[in] transaction Non-null.
 * @param[out] error      Nullable, set on operational failure.
 * @return                A newly allocated txid handle, or null on failure.
 */
BITCOINKERNEL_API btck_Txid* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_get_txid(
    const btck_Transaction* transaction,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Run context-free consensus validation on a btck_Transaction.
 *
 * Performs basic structural consensus checks (consensus/tx_check::CheckTransaction)
 * without requiring blockchain state.
 *
 * @param[in]  tx               Non-null, the transaction to validate.
 * @param[out] error            Nullable, set on operational failure.
 * @return                      Check result handle, or null on operational failure.
 * @note                        Only btck_TxValidationResult_UNSET and
 *                              btck_TxValidationResult_CONSENSUS are reachable.
 */
BITCOINKERNEL_API btck_TransactionCheckResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_check_result(
    const btck_Transaction* tx,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the context-free transaction check status.
 */
BITCOINKERNEL_API btck_CheckStatus btck_transaction_check_result_get_status(
    const btck_TransactionCheckResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the validation state. The returned pointer is borrowed and valid for
 * the lifetime of result.
 */
BITCOINKERNEL_API const btck_TxValidationState* btck_transaction_check_result_get_validation_state(
    const btck_TransactionCheckResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a transaction check result.
 */
BITCOINKERNEL_API void btck_transaction_check_result_destroy(
    btck_TransactionCheckResult* result);

/**
 * Destroy the transaction.
 */
BITCOINKERNEL_API void btck_transaction_destroy(btck_Transaction* transaction);

///@}

/** @name PrecomputedTransactionData
 * Functions for working with precomputed transaction data.
 */
///@{

/**
 * @brief Create precomputed transaction data for script verification.
 *
 * @param[in] tx_to             Non-null.
 * @param[in] spent_outputs     Nullable for non-taproot verification. Points to an array of
 *                              outputs spent by the transaction.
 * @param[in] spent_outputs_len Length of the spent_outputs array.
 * @param[out] error            Nullable, set on operational failure.
 * @return                      The precomputed data, or null on error.
 */
BITCOINKERNEL_API btck_PrecomputedTransactionData* BITCOINKERNEL_WARN_UNUSED_RESULT btck_precomputed_transaction_data_create(
    const btck_Transaction* tx_to,
    const btck_TransactionOutput** spent_outputs, size_t spent_outputs_len,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy precomputed transaction data.
 *
 * @param[in] precomputed_txdata Non-null.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       The copied precomputed transaction data.
 */
BITCOINKERNEL_API btck_PrecomputedTransactionData* BITCOINKERNEL_WARN_UNUSED_RESULT btck_precomputed_transaction_data_copy(
    const btck_PrecomputedTransactionData* precomputed_txdata,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the precomputed transaction data.
 */
BITCOINKERNEL_API void btck_precomputed_transaction_data_destroy(btck_PrecomputedTransactionData* precomputed_txdata);

///@}

/** @name ScriptPubkey
 * Functions for working with script pubkeys.
 */
///@{

/**
 * @brief Create a script pubkey from serialized data.
 * @param[in]  script_pubkey     Serialized script pubkey.
 * @param[in]  script_pubkey_len Length of the script pubkey data.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       The script pubkey.
 */
BITCOINKERNEL_API btck_ScriptPubkey* BITCOINKERNEL_WARN_UNUSED_RESULT btck_script_pubkey_create(
    const void* script_pubkey, size_t script_pubkey_len,
    btck_Error** error);

/**
 * @brief Copy a script pubkey.
 *
 * @param[in]  script_pubkey Non-null.
 * @param[out] error         Nullable, set on operational failure.
 * @return                   The copied script pubkey.
 */
BITCOINKERNEL_API btck_ScriptPubkey* BITCOINKERNEL_WARN_UNUSED_RESULT btck_script_pubkey_copy(
    const btck_ScriptPubkey* script_pubkey,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Verify if the input at input_index of tx_to spends the script pubkey
 * under the constraints specified by flags. If the
 * `btck_ScriptVerificationFlags_WITNESS` flag is set in the flags bitfield, the
 * amount parameter is used. If the taproot flag is set, the precomputed data
 * must contain the spent outputs.
 *
 * @param[in] script_pubkey      Non-null, script pubkey to be spent.
 * @param[in] amount             Amount of the script pubkey's associated output. May be zero if
 *                               the witness flag is not set.
 * @param[in] tx_to              Non-null, transaction spending the script_pubkey.
 * @param[in] precomputed_txdata Nullable if the taproot flag is not set. Otherwise, precomputed data
 *                               for tx_to with the spent outputs must be provided.
 * @param[in] input_index        Index of the input in tx_to spending the script_pubkey.
 * @param[in] flags              Bitfield of btck_ScriptVerificationFlags controlling validation constraints.
 * @param[out] status            Nullable, will be set to an error code if the operation fails, or OK otherwise.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       1 if the script is valid, 0 otherwise.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_script_pubkey_verify(
    const btck_ScriptPubkey* script_pubkey,
    int64_t amount,
    const btck_Transaction* tx_to,
    const btck_PrecomputedTransactionData* precomputed_txdata,
    unsigned int input_index,
    btck_ScriptVerificationFlags flags,
    btck_ScriptVerifyStatus* status,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 3);

/**
 * @brief Serializes the script pubkey through the passed in callback to bytes.
 *
 * @param[in] script_pubkey Non-null.
 * @param[in] writer        Non-null, callback to a write bytes function.
 * @param[in] user_data     Holds a user-defined opaque structure that will be
 *                          passed back through the writer callback.
 * @param[out] error        Nullable, set on writer callback or operational failure.
 * @return                  0 on success.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_script_pubkey_to_bytes(
    const btck_ScriptPubkey* script_pubkey,
    btck_WriteBytes writer,
    void* user_data,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the script pubkey.
 */
BITCOINKERNEL_API void btck_script_pubkey_destroy(btck_ScriptPubkey* script_pubkey);

///@}

/** @name TransactionOutput
 * Functions for working with transaction outputs.
 */
///@{

/**
 * @brief Create a transaction output from a script pubkey and an amount.
 *
 * @param[in] script_pubkey Non-null.
 * @param[in] amount        The amount associated with the script pubkey for this output.
 * @param[out] error        Nullable, set on operational failure.
 * @return                  The transaction output.
 */
BITCOINKERNEL_API btck_TransactionOutput* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_output_create(
    const btck_ScriptPubkey* script_pubkey,
    int64_t amount,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy the script pubkey of the output.
 *
 * @param[in] transaction_output Non-null.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       A newly allocated script pubkey, or null on failure.
 */
BITCOINKERNEL_API btck_ScriptPubkey* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_output_get_script_pubkey(
    const btck_TransactionOutput* transaction_output,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the amount in the output.
 *
 * @param[in] transaction_output Non-null.
 * @return                       The amount.
 */
BITCOINKERNEL_API int64_t btck_transaction_output_get_amount(
    const btck_TransactionOutput* transaction_output) BITCOINKERNEL_ARG_NONNULL(1);

/**
 *  @brief Copy a transaction output.
 *
 *  @param[in] transaction_output Non-null.
 *  @param[out] error             Nullable, set on operational failure.
 *  @return                       The copied transaction output.
 */
BITCOINKERNEL_API btck_TransactionOutput* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_output_copy(
    const btck_TransactionOutput* transaction_output,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the transaction output.
 */
BITCOINKERNEL_API void btck_transaction_output_destroy(btck_TransactionOutput* transaction_output);

///@}

/** @name Logging
 * Logging-related functions.
 */
///@{

/**
 * @brief This disables the global internal logger. No log messages will be
 * buffered internally anymore once this is called and the buffer is cleared.
 * This function should only be called once and is not thread or re-entry safe.
 * Log messages will be buffered until this function is called, or a logging
 * connection is created. This must not be called while a logging connection
 * already exists.
 */
BITCOINKERNEL_API void btck_logging_disable();

/**
 * @brief Set some options for the global internal logger. This changes global
 * settings and will override settings for all existing @ref
 * btck_LoggingConnection instances.
 *
 * @param[in] options Sets formatting options of the log messages.
 */
BITCOINKERNEL_API void btck_logging_set_options(btck_LoggingOptions options);

/**
 * @brief Set the log level of the global internal logger. This does not
 * enable the selected categories. Use @ref btck_logging_enable_category to
 * start logging from a specific, or all categories. This changes a global
 * setting and will override settings for all existing
 * @ref btck_LoggingConnection instances.
 *
 * @param[in] category If btck_LogCategory_ALL is chosen, sets both the global fallback log level
 *                     used by all categories that don't have a specific level set, and also
 *                     sets the log level for messages logged with the btck_LogCategory_ALL category itself.
 *                     For any other category, sets a category-specific log level that overrides
 *                     the global fallback for that category only.

 * @param[in] level    Log level at which the log category is set.
 */
BITCOINKERNEL_API void btck_logging_set_level_category(btck_LogCategory category, btck_LogLevel level);

/**
 * @brief Enable a specific log category for the global internal logger. This
 * changes a global setting and will override settings for all existing @ref
 * btck_LoggingConnection instances.
 *
 * @param[in] category If btck_LogCategory_ALL is chosen, all categories will be enabled.
 */
BITCOINKERNEL_API void btck_logging_enable_category(btck_LogCategory category);

/**
 * @brief Disable a specific log category for the global internal logger. This
 * changes a global setting and will override settings for all existing @ref
 * btck_LoggingConnection instances.
 *
 * @param[in] category If btck_LogCategory_ALL is chosen, all categories will be disabled.
 */
BITCOINKERNEL_API void btck_logging_disable_category(btck_LogCategory category);

/**
 * @brief Start logging messages through the provided callback. Log messages
 * produced before this function is first called are buffered and on calling this
 * function are logged immediately.
 *
 * @param[in] log_callback               Non-null, function through which messages will be logged.
 * @param[in] user_data                  Nullable, holds a user-defined opaque structure. Is passed back
 *                                       to the user through the callback. If the user_data_destroy_callback
 *                                       is also defined, ownership of user_data is passed to the created
 *                                       logging connection on success. On failure, ownership remains with
 *                                       the caller.
 * @param[in] user_data_destroy_callback Nullable, function for freeing the user data.
 * @param[out] error                     Nullable, set on operational failure.
 * @return                               A new kernel logging connection, or null on error.
 */
BITCOINKERNEL_API btck_LoggingConnection* BITCOINKERNEL_WARN_UNUSED_RESULT btck_logging_connection_create(
    btck_LogCallback log_callback,
    void* user_data,
    btck_DestroyCallback user_data_destroy_callback,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Stop logging and destroy the logging connection.
 */
BITCOINKERNEL_API void btck_logging_connection_destroy(btck_LoggingConnection* logging_connection);

///@}

/** @name ChainParameters
 * Functions for working with chain parameters.
 */
///@{

/**
 * @brief Creates a chain parameters struct with default parameters based on the
 * passed in chain type.
 *
 * @param[in]  chain_type Controls the chain parameters type created.
 * @param[out] error      Nullable, set on operational failure.
 * @return                An allocated chain parameters opaque struct.
 */
BITCOINKERNEL_API btck_ChainParameters* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chain_parameters_create(
    btck_ChainType chain_type,
    btck_Error** error);

/**
 * Copy the chain parameters.
 */
BITCOINKERNEL_API btck_ChainParameters* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chain_parameters_copy(
    const btck_ChainParameters* chain_parameters,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get btck_ConsensusParams from btck_ChainParameters. The returned
 * btck_ConsensusParams pointer is valid only for the lifetime of the
 * btck_ChainParameters object and must not be destroyed by the caller.
 *
 * @param[in] chain_parameters  Non-null.
 * @return                      The btck_ConsensusParams.
 */
BITCOINKERNEL_API const btck_ConsensusParams* btck_chain_parameters_get_consensus_params(
    const btck_ChainParameters* chain_parameters) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the chain parameters.
 */
BITCOINKERNEL_API void btck_chain_parameters_destroy(btck_ChainParameters* chain_parameters);

///@}

/** @name ContextOptions
 * Functions for working with context options.
 */
///@{

/**
 * Creates an empty context options.
 *
 * @param[out] error Nullable, set on operational failure.
 * @return           The context options, or null on error.
 */
BITCOINKERNEL_API btck_ContextOptions* BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_options_create(
    btck_Error** error);

/**
 * @brief Sets the chain params for the context options. The context created
 * with the options will be configured for these chain parameters.
 *
 * @param[in] context_options  Non-null, previously created by @ref btck_context_options_create.
 * @param[in] chain_parameters Is set to the context options.
 * @param[out] error           Nullable, set on operational failure.
 * @return                     0 if the chain parameters were set successfully, non-zero otherwise.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_options_set_chainparams(
    btck_ContextOptions* context_options,
    const btck_ChainParameters* chain_parameters,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Set the kernel notifications for the context options. The context
 * created with the options will be configured with these notifications.
 *
 * @param[in] context_options Non-null, previously created by @ref btck_context_options_create.
 * @param[in] notifications   Is set to the context options.
 * @param[out] error          Nullable, set on operational failure.
 * @return                    0 if the notifications were set successfully, non-zero otherwise. If non-zero is
 *                            returned, ownership of notifications.user_data remains with the caller.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_options_set_notifications(
    btck_ContextOptions* context_options,
    btck_NotificationInterfaceCallbacks notifications,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Set the validation interface callbacks for the context options. The
 * context created with the options will be configured for these validation
 * interface callbacks. The callbacks will then be triggered from validation
 * events issued by the chainstate created from the same context.
 *
 * @param[in] context_options                Non-null, previously created with btck_context_options_create.
 * @param[in] validation_interface_callbacks The callbacks used for passing validation information to the
 *                                           user.
 * @param[out] error                         Nullable, set on operational failure.
 * @return                                   0 if the validation interface was set successfully, non-zero otherwise. If
 *                                           non-zero is returned, ownership of validation_interface_callbacks.user_data
 *                                           remains with the caller.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_options_set_validation_interface(
    btck_ContextOptions* context_options,
    btck_ValidationInterfaceCallbacks validation_interface_callbacks,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the context options.
 */
BITCOINKERNEL_API void btck_context_options_destroy(btck_ContextOptions* context_options);

///@}

/** @name Context
 * Functions for working with contexts.
 */
///@{

/**
 * @brief Create a new kernel context. If the options have not been previously
 * set, their corresponding fields will be initialized to default values; the
 * context will assume mainnet chain parameters and won't attempt to call the
 * kernel notification callbacks.
 *
 * @param[in] context_options Nullable, created by @ref btck_context_options_create.
 * @param[out] error          Nullable, set on operational failure.
 * @return                    The allocated context, or null on error.
 */
BITCOINKERNEL_API btck_Context* BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_create(
    const btck_ContextOptions* context_options,
    btck_Error** error);

/**
 * Copy the context.
 *
 * @param[in]  context Non-null.
 * @param[out] error   Nullable, set on operational failure.
 * @return             The copied context, or null on error.
 */
BITCOINKERNEL_API btck_Context* BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_copy(
    const btck_Context* context,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Interrupt can be used to halt long-running validation functions like
 * when reindexing, importing or processing blocks.
 *
 * @param[in] context Non-null.
 * @param[out] error  Nullable, set on operational failure.
 * @return            0 if the interrupt was successful, non-zero otherwise.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_context_interrupt(
    btck_Context* context,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the context.
 */
BITCOINKERNEL_API void btck_context_destroy(btck_Context* context);

///@}

/** @name BlockInfo
 * Functions for working with immutable block information snapshots.
 */
///@{

/**
 * @brief Copy a block information snapshot.
 *
 * @param[in] block_info Non-null.
 * @param[out] error     Nullable, set on operational failure.
 * @return               The copied block information snapshot.
 */
BITCOINKERNEL_API btck_BlockInfo* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_info_copy(
    const btck_BlockInfo* block_info,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the height of the block.
 *
 * @param[in] block_info Non-null.
 * @return               The block height.
 */
BITCOINKERNEL_API int32_t btck_block_info_get_height(
    const btck_BlockInfo* block_info) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the block hash.
 *
 * @param[in] block_info Non-null.
 * @return               The block hash. The returned pointer is valid for the lifetime of block_info.
 */
BITCOINKERNEL_API const btck_BlockHash* btck_block_info_get_block_hash(
    const btck_BlockInfo* block_info) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the previous block hash.
 *
 * @param[in] block_info Non-null.
 * @return               The previous block hash, or null for the genesis block.
 *                       The returned pointer is valid for the lifetime of block_info.
 */
BITCOINKERNEL_API const btck_BlockHash* btck_block_info_get_previous_block_hash(
    const btck_BlockInfo* block_info) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the block header.
 *
 * @param[in] block_info Non-null.
 * @return               The block header. The returned pointer is valid for the lifetime of block_info.
 */
BITCOINKERNEL_API const btck_BlockHeader* btck_block_info_get_header(
    const btck_BlockInfo* block_info) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Check if two block information snapshots identify the same block.
 *
 * @param[in] info1 Non-null.
 * @param[in] info2 Non-null.
 * @return          1 if the snapshots have the same block hash, 0 otherwise.
 */
BITCOINKERNEL_API int btck_block_info_equals(
    const btck_BlockInfo* info1,
    const btck_BlockInfo* info2) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the block information snapshot.
 */
BITCOINKERNEL_API void btck_block_info_destroy(btck_BlockInfo* block_info);

///@}

/** @name ChainstateOptions
 * Functions for working with chainstate options.
 */
///@{

/**
 * @brief Create options for opening a chainstate.
 *
 * @param[in] context          Non-null, the created options and the opened chainstate will be
 *                             associated with this kernel context for the duration of their lifetimes.
 * @param[in] data_directory   Non-null, non-empty path string of the directory containing the
 *                             chainstate data. If the directory does not exist yet, it will be
 *                             created.
 * @param[in] blocks_directory Non-null, non-empty path string of the directory containing the block
 *                             data. If the directory does not exist yet, it will be created.
 * @param[out] error           Nullable, set on filesystem or operational failure.
 * @return                     The allocated chainstate options, or null on error.
 */
BITCOINKERNEL_API btck_ChainstateOptions* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_options_create(
    const btck_Context* context,
    const char* data_directory,
    size_t data_directory_len,
    const char* blocks_directory,
    size_t blocks_directory_len,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Set the number of available worker threads used during validation.
 *
 * @param[in] options        Non-null, options to be set.
 * @param[in] worker_threads             The number of worker threads that should be spawned in the thread pool
 *                                       used for validation. When set to 0 no parallel verification is done.
 *                                       The value range is clamped internally between 0 and 15.
 * @param[out] error                     Nullable, set on operational failure.
 * @return                               0 if the worker thread count was set successfully, non-zero otherwise.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_options_set_worker_threads_num(
    btck_ChainstateOptions* options,
    int worker_threads,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Sets whether opening the chainstate should wipe existing state.
 *
 * In combination with calling @ref btck_chainstate_import_blocks_result this
 * triggers either a full reindex, or a rebuild of the validation state from
 * existing block files.
 *
 * @param[in] options               Non-null, created by @ref btck_chainstate_options_create.
 * @param[in] reindex_block_files   Reindex existing block files. Should only be 1 if wipe_chainstate is 1 too.
 * @param[in] wipe_chainstate       Wipe the validation state database.
 * @param[out] error                Nullable, set on operational failure or unsupported option combination.
 * @return                          0 if the set was successful, non-zero if the set failed.
 * @note                            When a wipe is set, the caller must invoke @ref btck_chainstate_import_blocks_result
 *                                  on the resulting chainstate before using it for anything else.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_options_set_wipe_state(
    btck_ChainstateOptions* options,
    int reindex_block_files,
    int wipe_chainstate,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Sets whether chainstate storage should be memory-backed.
 *
 * @param[in] options   Non-null, created by @ref btck_chainstate_options_create.
 * @param[in] in_memory Use memory-backed storage when set to 1.
 * @param[out] error    Nullable, set on operational failure.
 * @return              0 if the option was set successfully, non-zero otherwise.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_options_set_in_memory(
    btck_ChainstateOptions* options,
    int in_memory,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the chainstate options.
 */
BITCOINKERNEL_API void btck_chainstate_options_destroy(btck_ChainstateOptions* options);

///@}

/** @name ChainstateRuntime
 * Runtime inputs for chainstate operations.
 */
///@{

/**
 * Create empty chainstate runtime inputs.
 *
 * @param[out] error Nullable, set on operational failure.
 * @return           The runtime inputs, or null on error.
 */
BITCOINKERNEL_API btck_ChainstateRuntime* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_runtime_create(
    btck_Error** error);

/**
 * @brief Set the current time used by chainstate operations.
 *
 * @param[in] options   Non-null options.
 * @param[in] timestamp Unix epoch seconds.
 * @param[out] error    Nullable, set when timestamp is outside the API domain.
 * @return              0 on success, non-zero if timestamp is invalid.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_runtime_set_current_time(
    btck_ChainstateRuntime* options,
    int64_t timestamp,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy chainstate runtime inputs.
 */
BITCOINKERNEL_API void btck_chainstate_runtime_destroy(btck_ChainstateRuntime* options);

///@}

/** @name BlockValidationOptions
 * Runtime inputs for block and block-header validation operations.
 */
///@{

/**
 * @brief Create block validation options.
 *
 * If no current time is set, APIs requiring these options will fail instead of
 * reading the system clock through this object.
 *
 * @param[out] error Nullable, set on operational failure.
 * @return           The allocated options, or null on error.
 */
BITCOINKERNEL_API btck_BlockValidationOptions* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_validation_options_create(
    btck_Error** error);

/**
 * @brief Set the current time used by block validation.
 *
 * @param[in] options      Non-null options.
 * @param[in] timestamp    Unix epoch seconds. Must be representable with the
 *                         validation future-time window.
 * @param[out] error       Nullable, set when timestamp is outside the API domain.
 * @return                 0 on success, non-zero if timestamp is invalid.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_validation_options_set_current_time(
    btck_BlockValidationOptions* options,
    int64_t timestamp,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy block validation options.
 */
BITCOINKERNEL_API void btck_block_validation_options_destroy(btck_BlockValidationOptions* options);

///@}

/** @name Chainstate
 * Functions for opening and mutating chainstate.
 */
///@{

/**
 * @brief Open an owned chainstate handle.
 *
 * @param[in] options         Non-null, created by @ref btck_chainstate_options_create.
 * @param[in] runtime_options Non-null runtime inputs with current time set.
 * @param[out] error          Nullable, set on chainstate load or operational failure.
 * @return                    The opened chainstate, or null on error.
 */
BITCOINKERNEL_API btck_Chainstate* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_open(
    const btck_ChainstateOptions* options,
    const btck_ChainstateRuntime* runtime_options,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Get information about the header with the most known cumulative proof of work.
 *
 * This header is not necessarily the active chain tip and may not have full
 * block data available on disk.
 *
 * @param[in] chainstate Non-null.
 * @param[out] error     Nullable, set on operational failure. A null return
 *                       with no error means no block headers have been loaded.
 * @return               The block information snapshot, or null if no block headers have been loaded.
 */
BITCOINKERNEL_API btck_BlockInfo* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_get_best_header_info(
    const btck_Chainstate* chainstate,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Processes and validates the provided btck_BlockHeader.
 *
 * @param[in] chainstate                Non-null.
 * @param[in] header                    Non-null btck_BlockHeader to be validated.
 * @param[in] options                   Non-null block validation options with current time set.
 * @param[out] error                    Nullable, set on operational failure.
 * @return                              Header process result, or null on operational failure.
 */
BITCOINKERNEL_API btck_HeaderProcessResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_process_header_result(
    btck_Chainstate* chainstate,
    const btck_BlockHeader* header,
    const btck_BlockValidationOptions* options,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2, 3);

/**
 * Return header processing status.
 */
BITCOINKERNEL_API btck_HeaderProcessStatus btck_header_process_result_get_status(
    const btck_HeaderProcessResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the header validation state. The returned pointer is borrowed and
 * valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockValidationState* btck_header_process_result_get_validation_state(
    const btck_HeaderProcessResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a header process result.
 */
BITCOINKERNEL_API void btck_header_process_result_destroy(
    btck_HeaderProcessResult* result);

/**
 * @brief Triggers the start of a reindex if wipe options were previously set.
 * Can also import an array of existing block files selected by the user.
 *
 * @param[in] chainstate                Non-null.
 * @param[in] block_file_paths_data     Nullable only when block_file_paths_data_len is 0,
 *                                      array of block files described by their full filesystem paths.
 *                                      Each path entry must be non-null and non-empty.
 * @param[in] block_file_paths_lens     Nullable only when block_file_paths_data_len is 0,
 *                                      array containing the lengths of each of the paths.
 * @param[in] block_file_paths_data_len Length of the block_file_paths_data and block_file_paths_len arrays.
 * @param[in] runtime_options           Non-null runtime options with current time set.
 * @param[out] error                    Nullable, set on operational failure.
 * @return                              Import result handle, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockImportResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_import_blocks_result(
    btck_Chainstate* chainstate,
    const char** block_file_paths_data, const size_t* block_file_paths_lens,
    size_t block_file_paths_data_len,
    const btck_ChainstateRuntime* runtime_options,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 5);

/**
 * Return import status.
 */
BITCOINKERNEL_API btck_BlockImportStatus btck_block_import_result_get_status(
    const btck_BlockImportResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the number of newly loaded blocks reported by the import pipeline.
 */
BITCOINKERNEL_API int btck_block_import_result_get_loaded_block_count(
    const btck_BlockImportResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the number of recoverable file records skipped by the import scanner.
 */
BITCOINKERNEL_API int btck_block_import_result_get_skipped_record_count(
    const btck_BlockImportResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the number of indexed or policy-skipped blocks observed by import.
 */
BITCOINKERNEL_API int btck_block_import_result_get_skipped_block_count(
    const btck_BlockImportResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the number of rejected blocks observed by import.
 */
BITCOINKERNEL_API int btck_block_import_result_get_rejected_block_count(
    const btck_BlockImportResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block import result.
 */
BITCOINKERNEL_API void btck_block_import_result_destroy(
    btck_BlockImportResult* result);

/**
 * @brief Process and validate the passed in block with the chainstate.
 * Processing first does checks on the block, and if these passed,
 * saves it to disk. It then validates the block against the utxo set. If it is
 * valid, the chain is extended with it.
 *
 * @param[in] chainstate         Non-null.
 * @param[in] block              Non-null, block to be validated.
 * @param[in] options            Non-null block validation options with current time set.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       Block process result, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockProcessResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_process_block_result(
    btck_Chainstate* chainstate,
    const btck_Block* block,
    const btck_BlockValidationOptions* options,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2, 3);

/**
 * Return block processing status.
 */
BITCOINKERNEL_API btck_BlockProcessStatus btck_block_process_result_get_status(
    const btck_BlockProcessResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return 1 when new block data was stored by this operation, otherwise 0.
 */
BITCOINKERNEL_API int btck_block_process_result_has_new_block_data(
    const btck_BlockProcessResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the block validation state. The returned pointer is borrowed and
 * valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockValidationState* btck_block_process_result_get_validation_state(
    const btck_BlockProcessResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block process result.
 */
BITCOINKERNEL_API void btck_block_process_result_destroy(
    btck_BlockProcessResult* result);

/**
 * @brief Return an immutable snapshot of the active chain.
 *
 * @param[in] chainstate         Non-null.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       The active chain snapshot, or null on error.
 */
BITCOINKERNEL_API btck_ChainSnapshot* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_snapshot_active_chain(
    const btck_Chainstate* chainstate,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Retrieve immutable block information by block hash.
 *
 * @param[in]  chainstate        Non-null.
 * @param[in]  block_hash        Non-null.
 * @param[out] error             Nullable, set on operational failure. Not set
 *                               when the block hash is not indexed.
 * @return                       The block information snapshot for the passed
 *                               hash, or null if the block hash is not indexed.
 */
BITCOINKERNEL_API btck_BlockInfo* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_get_block_info(
    const btck_Chainstate* chainstate,
    const btck_BlockHash* block_hash,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Read the block identified by a block hash from disk.
 *
 * @param[in] chainstate         Non-null.
 * @param[in] block_hash         Non-null.
 * @param[out] error             Nullable, set on disk I/O or operational failure.
 * @return                       Block read result, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockReadResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_read_block_result(
    const btck_Chainstate* chainstate,
    const btck_BlockHash* block_hash,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Return block read status.
 */
BITCOINKERNEL_API btck_BlockReadStatus btck_block_read_result_get_status(
    const btck_BlockReadResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the read block, or null unless status is btck_BlockReadStatus_FOUND.
 *
 * The returned pointer is borrowed and valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_Block* btck_block_read_result_get_block(
    const btck_BlockReadResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block read result.
 */
BITCOINKERNEL_API void btck_block_read_result_destroy(
    btck_BlockReadResult* result);

/**
 * @brief Read the block spent outputs identified by a block hash from disk.
 *
 * @param[in] chainstate         Non-null.
 * @param[in] block_hash         Non-null.
 * @param[out] error             Nullable, set on disk I/O or operational failure.
 * @return                       Spent-output read result, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockSpentOutputsReadResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chainstate_read_block_spent_outputs_result(
    const btck_Chainstate* chainstate,
    const btck_BlockHash* block_hash,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Return block spent-output read status.
 */
BITCOINKERNEL_API btck_BlockSpentOutputsReadStatus btck_block_spent_outputs_read_result_get_status(
    const btck_BlockSpentOutputsReadResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the read spent outputs, or null unless status is
 * btck_BlockSpentOutputsReadStatus_FOUND.
 *
 * The returned pointer is borrowed and valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockSpentOutputs* btck_block_spent_outputs_read_result_get_spent_outputs(
    const btck_BlockSpentOutputsReadResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block spent-output read result.
 */
BITCOINKERNEL_API void btck_block_spent_outputs_read_result_destroy(
    btck_BlockSpentOutputsReadResult* result);

/**
 * Destroy the chainstate.
 */
BITCOINKERNEL_API void btck_chainstate_destroy(btck_Chainstate* chainstate);

///@}

/** @name Block
 * Functions for working with blocks.
 */
///@{

/**
 * @brief Parse a serialized raw block into a new block object.
 *
 * @param[in] raw_block     Serialized block.
 * @param[in] raw_block_len Length of the serialized block.
 * @param[out] error        Nullable, set only on operational failure.
 * @return                  Parse result handle, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockParseResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_parse_result(
    const void* raw_block, size_t raw_block_len,
    btck_Error** error);

/**
 * Return the parse status.
 */
BITCOINKERNEL_API btck_ParseStatus btck_block_parse_result_get_status(
    const btck_BlockParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the malformed parse reason, or btck_ParseFailureCode_NONE when status
 * is btck_ParseStatus_OK.
 */
BITCOINKERNEL_API btck_ParseFailureCode btck_block_parse_result_get_failure_code(
    const btck_BlockParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the byte offset at which malformed parsing failed, or 0 when status is
 * btck_ParseStatus_OK.
 */
BITCOINKERNEL_API size_t btck_block_parse_result_get_failure_offset(
    const btck_BlockParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the parsed block, or null unless status is btck_ParseStatus_OK.
 *
 * The returned pointer is borrowed and valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_Block* btck_block_parse_result_get_block(
    const btck_BlockParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block parse result.
 */
BITCOINKERNEL_API void btck_block_parse_result_destroy(
    btck_BlockParseResult* result);

/**
 * @brief Copy a block. Blocks are reference counted, so this just increments
 * the reference count.
 *
 * @param[in]  block Non-null.
 * @param[out] error Nullable, set on operational failure.
 * @return           The copied block.
 */
BITCOINKERNEL_API btck_Block* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_copy(
    const btck_Block* block,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/** Bitflags to control context-free block checks (optional). */
typedef uint32_t btck_BlockCheckFlags;
#define btck_BlockCheckFlags_BASE ((btck_BlockCheckFlags)0)                                                       //!< run the base context-free block checks only
#define btck_BlockCheckFlags_POW ((btck_BlockCheckFlags)(1U << 0))                                                //!< run CheckProofOfWork via CheckBlockHeader
#define btck_BlockCheckFlags_MERKLE ((btck_BlockCheckFlags)(1U << 1))                                             //!< verify merkle root (and mutation detection)
#define btck_BlockCheckFlags_ALL ((btck_BlockCheckFlags)(btck_BlockCheckFlags_POW | btck_BlockCheckFlags_MERKLE)) //!< enable all optional context-free block checks

/**
 * @brief Perform context-free validation checks on a btck_Block.
 *
 * Runs the base context-free block checks (size limits, coinbase structure,
 * transaction checks, and sigop limits) using the supplied
 * btck_ConsensusParams. The proof-of-work and merkle-root checks are optional
 * and can be toggled via @p flags. Note that this does not include any
 * transaction script, timestamps, order, or other checks that may require more
 * context.
 *
 * @param[in]     block             Non-null, btck_Block to validate.
 * @param[in]     consensus_params  Non-null, btck_ConsensusParams for validation.
 * @param[in]     flags             Bitmask of btck_BlockCheckFlags controlling the
 *                                  optional POW and merkle-root checks. Use
 *                                  btck_BlockCheckFlags_BASE to run only the base
 *                                  checks.
 * @param[out]    error             Nullable, set on operational failure, including
 *                                  unknown flag bits.
 * @return                          Check result handle, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockCheckResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_check_result(
    const btck_Block* block,
    const btck_ConsensusParams* consensus_params,
    btck_BlockCheckFlags flags,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Return the context-free block check status.
 */
BITCOINKERNEL_API btck_CheckStatus btck_block_check_result_get_status(
    const btck_BlockCheckResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the block validation state. The returned pointer is borrowed and
 * valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockValidationState* btck_block_check_result_get_validation_state(
    const btck_BlockCheckResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block check result.
 */
BITCOINKERNEL_API void btck_block_check_result_destroy(
    btck_BlockCheckResult* result);

/**
 * @brief Verify a block with the side-effect-free validation library.
 *
 * The ancestry array is a genesis-to-parent chain-view snapshot. It may be
 * null only when ancestor_count is zero. Consensus parameters and all runtime
 * deployment choices are explicit inputs. Coin lookup and script verification
 * are borrowed synchronous callbacks; the caller retains ownership of callback
 * user_data and any coin views returned by lookup.
 *
 * Invalid blocks are returned as a btck_BlockVerifyResult with invalid
 * validation state and exact validation-library rejection identity. Invalid
 * caller-supplied ancestry evidence is also returned as an invalid
 * btck_BlockVerifyResult, with invalid validation state and no rejection
 * identity. Operational failures return null and set @p error.
 *
 * @param[in]  block             Non-null candidate block.
 * @param[in]  ancestors         Nullable only when ancestor_count is zero.
 * @param[in]  ancestor_count    Number of headers in genesis-to-parent order.
 * @param[in]  consensus_params  Non-null consensus parameters.
 * @param[in]  options           Explicit validation runtime options.
 * @param[in]  coins             Borrowed coin-index callback table.
 * @param[out] error             Nullable, set on operational failure.
 * @return                       Verification result, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockVerifyResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_verify_result(
    const btck_Block* block,
    const btck_BlockHeader* const* ancestors,
    size_t ancestor_count,
    const btck_ConsensusParams* consensus_params,
    btck_BlockValidationLibraryOptions options,
    btck_ValidationCoinIndex coins,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 4);

/**
 * Return the validation-library block verification status.
 */
BITCOINKERNEL_API btck_CheckStatus btck_block_verify_result_get_status(
    const btck_BlockVerifyResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the block validation state. The returned pointer is borrowed and
 * valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockValidationState* btck_block_verify_result_get_validation_state(
    const btck_BlockVerifyResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the validation-library rejection code, or
 * btck_ValidationRejectionCode_NONE when the result is valid.
 */
BITCOINKERNEL_API btck_ValidationRejectionCode btck_block_verify_result_get_rejection_code(
    const btck_BlockVerifyResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the validation-library rule that rejected the block, or
 * btck_ValidationRule_NONE when the result is valid.
 */
BITCOINKERNEL_API btck_ValidationRule btck_block_verify_result_get_rejection_rule(
    const btck_BlockVerifyResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the stable short rule code, such as "H01" or "S04". The returned
 * pointer is borrowed and valid for the lifetime of the process. Null is
 * returned when the result is valid.
 */
BITCOINKERNEL_API const char* btck_block_verify_result_get_rejection_rule_code(
    const btck_BlockVerifyResult* result,
    size_t* rule_code_len) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the validation-library rejection reason. The returned pointer is
 * borrowed and valid for the lifetime of the process. Null is returned when
 * the result is valid.
 */
BITCOINKERNEL_API const char* btck_block_verify_result_get_rejection_reason(
    const btck_BlockVerifyResult* result,
    size_t* reason_len) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return invalid caller-supplied header ancestry evidence, or
 * btck_HeaderContextEvidenceCode_NONE when the supplied ancestry evidence was
 * valid.
 */
BITCOINKERNEL_API btck_HeaderContextEvidenceCode btck_block_verify_result_get_header_context_evidence_code(
    const btck_BlockVerifyResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the invalid header ancestry evidence reason. The returned pointer is
 * borrowed and valid for the lifetime of the process. Null is returned when
 * the supplied ancestry evidence was valid.
 */
BITCOINKERNEL_API const char* btck_block_verify_result_get_header_context_evidence_reason(
    const btck_BlockVerifyResult* result,
    size_t* reason_len) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block verify result.
 */
BITCOINKERNEL_API void btck_block_verify_result_destroy(
    btck_BlockVerifyResult* result);

/**
 * @brief Count the number of transactions contained in a block.
 *
 * @param[in] block Non-null.
 * @return          The number of transactions in the block.
 */
BITCOINKERNEL_API size_t btck_block_count_transactions(
    const btck_Block* block) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the transaction at the provided index. The returned transaction
 * is not owned and depends on the lifetime of the block.
 *
 * @param[in] block             Non-null.
 * @param[in] transaction_index The index of the transaction to be retrieved.
 * @return                      The transaction.
 */
BITCOINKERNEL_API const btck_Transaction* btck_block_get_transaction_at(
    const btck_Block* block, size_t transaction_index) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the btck_BlockHeader from the block.
 *
 * Creates a new btck_BlockHeader object from the block's header data.
 *
 * @param[in]  block Non-null btck_Block.
 * @param[out] error Nullable, set on operational failure.
 * @return           btck_BlockHeader.
 */
BITCOINKERNEL_API btck_BlockHeader* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_get_header(
    const btck_Block* block,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Calculate and return the hash of a block.
 *
 * @param[in]  block Non-null.
 * @param[out] error Nullable, set on operational failure.
 * @return           The block hash.
 */
BITCOINKERNEL_API btck_BlockHash* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_get_hash(
    const btck_Block* block,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Serializes the block through the passed in callback to bytes.
 * This is consensus serialization that is also used for the P2P network.
 *
 * @param[in] block     Non-null.
 * @param[in] writer    Non-null, callback to a write bytes function.
 * @param[in] user_data Holds a user-defined opaque structure that will be
 *                      passed back through the writer callback.
 * @param[out] error    Nullable, set on writer callback or operational failure.
 * @return              0 on success.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_serialize(
    const btck_Block* block,
    btck_WriteBytes writer,
    void* user_data,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the block.
 */
BITCOINKERNEL_API void btck_block_destroy(btck_Block* block);

///@}

/** @name BlockValidationState
 * Functions for working with block validation states.
 */
///@{

/**
 * Create a new btck_BlockValidationState.
 *
 * @param[out] error Nullable, set on operational failure.
 * @return           The validation state, or null on error.
 */
BITCOINKERNEL_API btck_BlockValidationState* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_validation_state_create(
    btck_Error** error);

/**
 * Returns the validation mode from an opaque btck_BlockValidationState pointer.
 */
BITCOINKERNEL_API btck_ValidationMode btck_block_validation_state_get_validation_mode(
    const btck_BlockValidationState* block_validation_state) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Returns the validation result from an opaque btck_BlockValidationState pointer.
 */
BITCOINKERNEL_API btck_BlockValidationResult btck_block_validation_state_get_block_validation_result(
    const btck_BlockValidationState* block_validation_state) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copies the btck_BlockValidationState.
 *
 * @param[in] block_validation_state Non-null.
 * @param[out] error                  Nullable, set on operational failure.
 * @return                           The copied btck_BlockValidationState.
 */
BITCOINKERNEL_API btck_BlockValidationState* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_validation_state_copy(
    const btck_BlockValidationState* block_validation_state,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the btck_BlockValidationState.
 */
BITCOINKERNEL_API void btck_block_validation_state_destroy(
    btck_BlockValidationState* block_validation_state);

///@}

/** @name ChainSnapshot
 * Functions for working with immutable active-chain snapshots.
 */
///@{

/**
 * @brief Copy a chain snapshot.
 *
 * @param[in] chain_snapshot Non-null.
 * @param[out] error         Nullable, set on operational failure.
 * @return                    The copied chain snapshot.
 */
BITCOINKERNEL_API btck_ChainSnapshot* BITCOINKERNEL_WARN_UNUSED_RESULT btck_chain_snapshot_copy(
    const btck_ChainSnapshot* chain_snapshot,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the height of the snapshot tip.
 *
 * @param[in] chain_snapshot Non-null.
 * @return                    The tip height, or -1 if the snapshot is empty.
 */
BITCOINKERNEL_API int32_t btck_chain_snapshot_get_height(
    const btck_ChainSnapshot* chain_snapshot) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the number of blocks in the snapshot.
 *
 * @param[in] chain_snapshot Non-null.
 * @return                    The number of block information entries.
 */
BITCOINKERNEL_API size_t btck_chain_snapshot_count(
    const btck_ChainSnapshot* chain_snapshot) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Retrieve block information by height in this snapshot.
 *
 * @param[in] chain_snapshot Non-null.
 * @param[in] block_height   Height in the snapshot.
 * @return                   The block information at the given height, or null if the height is out of bounds.
 *                           The returned pointer is valid for the lifetime of chain_snapshot.
 */
BITCOINKERNEL_API const btck_BlockInfo* btck_chain_snapshot_get_block_info_by_height(
    const btck_ChainSnapshot* chain_snapshot,
    int32_t block_height) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return true if the snapshot contains the block hash.
 *
 * @param[in] chain_snapshot Non-null.
 * @param[in] block_hash     Non-null.
 * @return                   1 if block_hash is in the snapshot, 0 otherwise.
 */
BITCOINKERNEL_API int btck_chain_snapshot_contains_block_hash(
    const btck_ChainSnapshot* chain_snapshot,
    const btck_BlockHash* block_hash) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the chain snapshot.
 */
BITCOINKERNEL_API void btck_chain_snapshot_destroy(btck_ChainSnapshot* chain_snapshot);

///@}

/** @name BlockSpentOutputs
 * Functions for working with block spent outputs.
 */
///@{

/**
 * @brief Copy a block's spent outputs.
 *
 * @param[in] block_spent_outputs Non-null.
 * @param[out] error              Nullable, set on operational failure.
 * @return                        The copied block spent outputs.
 */
BITCOINKERNEL_API btck_BlockSpentOutputs* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_spent_outputs_copy(
    const btck_BlockSpentOutputs* block_spent_outputs,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns the number of transaction spent outputs whose data is contained in
 * block spent outputs.
 *
 * @param[in] block_spent_outputs Non-null.
 * @return                        The number of transaction spent outputs data in the block spent outputs.
 */
BITCOINKERNEL_API size_t btck_block_spent_outputs_count(
    const btck_BlockSpentOutputs* block_spent_outputs) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns a transaction spent outputs contained in the block spent
 * outputs at a certain index. The returned pointer is unowned and only valid
 * for the lifetime of block_spent_outputs.
 *
 * @param[in] block_spent_outputs             Non-null.
 * @param[in] transaction_spent_outputs_index The index of the transaction spent outputs within the block spent outputs.
 * @return                                    A transaction spent outputs pointer.
 */
BITCOINKERNEL_API const btck_TransactionSpentOutputs* btck_block_spent_outputs_get_transaction_spent_outputs_at(
    const btck_BlockSpentOutputs* block_spent_outputs,
    size_t transaction_spent_outputs_index) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the block spent outputs.
 */
BITCOINKERNEL_API void btck_block_spent_outputs_destroy(btck_BlockSpentOutputs* block_spent_outputs);

///@}

/** @name TransactionSpentOutputs
 * Functions for working with the spent coins of a transaction
 */
///@{

/**
 * @brief Copy a transaction's spent outputs.
 *
 * @param[in] transaction_spent_outputs Non-null.
 * @param[out] error                    Nullable, set on operational failure.
 * @return                              The copied transaction spent outputs.
 */
BITCOINKERNEL_API btck_TransactionSpentOutputs* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_spent_outputs_copy(
    const btck_TransactionSpentOutputs* transaction_spent_outputs,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns the number of previous transaction outputs contained in the
 * transaction spent outputs data.
 *
 * @param[in] transaction_spent_outputs Non-null
 * @return                              The number of spent transaction outputs for the transaction.
 */
BITCOINKERNEL_API size_t btck_transaction_spent_outputs_count(
    const btck_TransactionSpentOutputs* transaction_spent_outputs) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns a coin contained in the transaction spent outputs at a
 * certain index. The returned pointer is unowned and only valid for the
 * lifetime of transaction_spent_outputs.
 *
 * @param[in] transaction_spent_outputs Non-null.
 * @param[in] coin_index                The index of the to be retrieved coin within the
 *                                      transaction spent outputs.
 * @return                              A coin pointer.
 */
BITCOINKERNEL_API const btck_Coin* btck_transaction_spent_outputs_get_coin_at(
    const btck_TransactionSpentOutputs* transaction_spent_outputs,
    size_t coin_index) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the transaction spent outputs.
 */
BITCOINKERNEL_API void btck_transaction_spent_outputs_destroy(btck_TransactionSpentOutputs* transaction_spent_outputs);

///@}

/** @name Transaction Input
 * Functions for working with transaction inputs.
 */
///@{

/**
 * @brief Copy a transaction input.
 *
 * @param[in] transaction_input Non-null.
 * @param[out] error            Nullable, set on operational failure.
 * @return                      The copied transaction input.
 */
BITCOINKERNEL_API btck_TransactionInput* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_input_copy(
    const btck_TransactionInput* transaction_input,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy the transaction out point.
 *
 * @param[in] transaction_input Non-null.
 * @param[out] error            Nullable, set on operational failure.
 * @return                      A newly allocated transaction out point, or null on failure.
 */
BITCOINKERNEL_API btck_TransactionOutPoint* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_input_get_out_point(
    const btck_TransactionInput* transaction_input,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get a transaction input's nSequence value.
 *
 * @param[in] transaction_input Non-null.
 * @return                      The nSequence value.
 */
BITCOINKERNEL_API uint32_t btck_transaction_input_get_sequence(
    const btck_TransactionInput* transaction_input) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the transaction input.
 */
BITCOINKERNEL_API void btck_transaction_input_destroy(btck_TransactionInput* transaction_input);

///@}

/** @name Transaction Out Point
 * Functions for working with transaction out points.
 */
///@{

/**
 * @brief Copy a transaction out point.
 *
 * @param[in] transaction_out_point Non-null.
 * @param[out] error                Nullable, set on operational failure.
 * @return                          The copied transaction out point.
 */
BITCOINKERNEL_API btck_TransactionOutPoint* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_out_point_copy(
    const btck_TransactionOutPoint* transaction_out_point,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the output position from the transaction out point.
 *
 * @param[in] transaction_out_point Non-null.
 * @return                          The output index.
 */
BITCOINKERNEL_API uint32_t btck_transaction_out_point_get_index(
    const btck_TransactionOutPoint* transaction_out_point) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy the txid from the transaction out point.
 *
 * @param[in] transaction_out_point Non-null.
 * @param[out] error                Nullable, set on operational failure.
 * @return                          A newly allocated txid, or null on failure.
 */
BITCOINKERNEL_API btck_Txid* BITCOINKERNEL_WARN_UNUSED_RESULT btck_transaction_out_point_get_txid(
    const btck_TransactionOutPoint* transaction_out_point,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the transaction out point.
 */
BITCOINKERNEL_API void btck_transaction_out_point_destroy(btck_TransactionOutPoint* transaction_out_point);

///@}

/** @name Txid
 * Functions for working with txids.
 */
///@{

/**
 * @brief Copy a txid.
 *
 * @param[in] txid Non-null.
 * @param[out] error Nullable, set on operational failure.
 * @return         The copied txid.
 */
BITCOINKERNEL_API btck_Txid* BITCOINKERNEL_WARN_UNUSED_RESULT btck_txid_copy(
    const btck_Txid* txid,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Check if two txids are equal.
 *
 * @param[in] txid1 Non-null.
 * @param[in] txid2 Non-null.
 * @return          0 if the txid is not equal.
 */
BITCOINKERNEL_API int btck_txid_equals(
    const btck_Txid* txid1, const btck_Txid* txid2) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Serializes the txid to bytes.
 *
 * @param[in] txid    Non-null.
 * @param[out] output The serialized txid.
 */
BITCOINKERNEL_API void btck_txid_to_bytes(
    const btck_Txid* txid, unsigned char output[32]) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the txid.
 */
BITCOINKERNEL_API void btck_txid_destroy(btck_Txid* txid);

///@}

/** @name Coin
 * Functions for working with coins.
 */
///@{

/**
 * @brief Create a validation-library coin value.
 *
 * The previous median time past is the median-time-past of the block that
 * precedes the block creating this coin. It is required for BIP68 time-based
 * sequence-lock validation when the coin is later spent.
 *
 * @param[in]  output                    Non-null transaction output.
 * @param[in]  confirmation_height       Height of the block that created the coin.
 * @param[in]  coinbase                  Non-zero if the creating transaction was coinbase.
 * @param[in]  previous_median_time_past Unix timestamp of the creator block's previous MTP.
 * @param[out] error                     Nullable, set on operational failure.
 * @return                               The created coin, or null on failure.
 */
BITCOINKERNEL_API btck_Coin* BITCOINKERNEL_WARN_UNUSED_RESULT btck_coin_create(
    const btck_TransactionOutput* output,
    uint32_t confirmation_height,
    int coinbase,
    int64_t previous_median_time_past,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy a coin.
 *
 * @param[in] coin Non-null.
 * @param[out] error Nullable, set on operational failure.
 * @return         The copied coin.
 */
BITCOINKERNEL_API btck_Coin* BITCOINKERNEL_WARN_UNUSED_RESULT btck_coin_copy(
    const btck_Coin* coin,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns the block height where the transaction that
 * created this coin was included in.
 *
 * @param[in] coin Non-null.
 * @return         The block height of the coin.
 */
BITCOINKERNEL_API uint32_t btck_coin_confirmation_height(
    const btck_Coin* coin) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns whether the containing transaction was a coinbase.
 *
 * @param[in] coin Non-null.
 * @return         1 if the coin is a coinbase coin, 0 otherwise.
 */
BITCOINKERNEL_API int btck_coin_is_coinbase(
    const btck_Coin* coin) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Returns the previous median-time-past timestamp carried by the coin.
 *
 * @param[in] coin Non-null.
 * @return         Unix timestamp in seconds.
 */
BITCOINKERNEL_API int64_t btck_coin_previous_median_time_past(
    const btck_Coin* coin) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Return the transaction output of a coin. The returned pointer is
 * unowned and only valid for the lifetime of the coin.
 *
 * @param[in] coin Non-null.
 * @return         A transaction output pointer.
 */
BITCOINKERNEL_API const btck_TransactionOutput* btck_coin_get_output(
    const btck_Coin* coin) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy the coin.
 */
BITCOINKERNEL_API void btck_coin_destroy(btck_Coin* coin);

///@}

/** @name BlockHash
 * Functions for working with block hashes.
 */
///@{

/**
 * @brief Create a block hash from its raw data.
 *
 * @param[in] block_hash Non-null.
 * @param[out] error     Nullable, set on operational failure.
 * @return               The block hash, or null on failure.
 */
BITCOINKERNEL_API btck_BlockHash* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_hash_create(
    const unsigned char block_hash[32],
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Check if two block hashes are equal.
 *
 * @param[in] hash1 Non-null.
 * @param[in] hash2 Non-null.
 * @return          0 if the block hashes are not equal.
 */
BITCOINKERNEL_API int btck_block_hash_equals(
    const btck_BlockHash* hash1, const btck_BlockHash* hash2) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * @brief Copy a block hash.
 *
 * @param[in] block_hash Non-null.
 * @param[out] error     Nullable, set on operational failure.
 * @return               The copied block hash.
 */
BITCOINKERNEL_API btck_BlockHash* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_hash_copy(
    const btck_BlockHash* block_hash,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Serializes the block hash to bytes.
 *
 * @param[in] block_hash     Non-null.
 * @param[in] output         The serialized block hash.
 */
BITCOINKERNEL_API void btck_block_hash_to_bytes(
    const btck_BlockHash* block_hash, unsigned char output[32]) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the block hash.
 */
BITCOINKERNEL_API void btck_block_hash_destroy(btck_BlockHash* block_hash);

///@}

/**
 * @name Block Header
 * Functions for working with block headers.
 */
///@{

/**
 * @brief Parse a btck_BlockHeader from serialized data.
 *
 * @param[in] raw_block_header      Serialized header data (80 bytes).
 * @param[in] raw_block_header_len  Length of serialized header (must be 80)
 * @param[out] error                Nullable, set only on operational failure.
 * @return                          Parse result handle, or null on operational failure.
 */
BITCOINKERNEL_API btck_BlockHeaderParseResult* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_header_parse_result(
    const void* raw_block_header, size_t raw_block_header_len,
    btck_Error** error);

/**
 * Return the parse status.
 */
BITCOINKERNEL_API btck_ParseStatus btck_block_header_parse_result_get_status(
    const btck_BlockHeaderParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the malformed parse reason, or btck_ParseFailureCode_NONE when status
 * is btck_ParseStatus_OK.
 */
BITCOINKERNEL_API btck_ParseFailureCode btck_block_header_parse_result_get_failure_code(
    const btck_BlockHeaderParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the byte offset at which malformed parsing failed, or 0 when status is
 * btck_ParseStatus_OK.
 */
BITCOINKERNEL_API size_t btck_block_header_parse_result_get_failure_offset(
    const btck_BlockHeaderParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Return the parsed header, or null unless status is btck_ParseStatus_OK.
 *
 * The returned pointer is borrowed and valid for the lifetime of result.
 */
BITCOINKERNEL_API const btck_BlockHeader* btck_block_header_parse_result_get_header(
    const btck_BlockHeaderParseResult* result) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * Destroy a block-header parse result.
 */
BITCOINKERNEL_API void btck_block_header_parse_result_destroy(
    btck_BlockHeaderParseResult* result);

/**
 * @brief Copy a btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader.
 * @param[out] error    Nullable, set on operational failure.
 * @return              Copied btck_BlockHeader.
 */
BITCOINKERNEL_API btck_BlockHeader* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_header_copy(
    const btck_BlockHeader* header,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the btck_BlockHash.
 *
 * @param[in] header    Non-null header
 * @param[out] error    Nullable, set on operational failure.
 * @return              btck_BlockHash.
 */
BITCOINKERNEL_API btck_BlockHash* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_header_get_hash(
    const btck_BlockHeader* header,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Copy the previous btck_BlockHash from btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader
 * @param[out] error    Nullable, set on operational failure.
 * @return              Previous btck_BlockHash
 */
BITCOINKERNEL_API btck_BlockHash* BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_header_get_prev_hash(
    const btck_BlockHeader* header,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the timestamp from btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader
 * @return              Block timestamp (Unix epoch seconds)
 */
BITCOINKERNEL_API uint32_t btck_block_header_get_timestamp(
    const btck_BlockHeader* header) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the nBits difficulty target from btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader
 * @return              Difficulty target (compact format)
 */
BITCOINKERNEL_API uint32_t btck_block_header_get_bits(
    const btck_BlockHeader* header) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the version from btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader
 * @return              Block version
 */
BITCOINKERNEL_API int32_t btck_block_header_get_version(
    const btck_BlockHeader* header) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Get the nonce from btck_BlockHeader.
 *
 * @param[in] header    Non-null btck_BlockHeader
 * @return              Nonce
 */
BITCOINKERNEL_API uint32_t btck_block_header_get_nonce(
    const btck_BlockHeader* header) BITCOINKERNEL_ARG_NONNULL(1);

/**
 * @brief Serializes the btck_BlockHeader through the passed in callback to bytes.
 * This is consensus serialization that is also used for the P2P network.
 *
 * @param[in] header    Non-null.
 * @param[in] writer    Non-null, callback to a write bytes function.
 * @param[in] user_data Holds a user-defined opaque structure that will be
 *                      passed back through the writer callback.
 * @param[out] error    Nullable, set on writer callback or operational failure.
 * @return              0 on success.
 */
BITCOINKERNEL_API int BITCOINKERNEL_WARN_UNUSED_RESULT btck_block_header_serialize(
    const btck_BlockHeader* header,
    btck_WriteBytes writer,
    void* user_data,
    btck_Error** error) BITCOINKERNEL_ARG_NONNULL(1, 2);

/**
 * Destroy the btck_BlockHeader.
 */
BITCOINKERNEL_API void btck_block_header_destroy(btck_BlockHeader* header);

///@}

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // BITCOIN_KERNEL_BITCOINKERNEL_H
