// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BITCOINKERNEL_BUILD

#include <kernel/bitcoinkernel.h>

#include <bitcoin/core_adapter/block.h>
#include <bitcoin/core_adapter/transaction.h>
#include <bitcoin/protocol/codec.h>
#include <bitcoin/protocol/result.h>
#include <bitcoin/validation/block.h>
#include <bitcoin/validation/transaction.h>
#include <bitcoin/validation/verify.h>
#include <chain.h>
#include <chainstate.h>
#include <coins.h>
#include <consensus/block_check.h>
#include <dbwrapper.h>
#include <kernel/blockimport.h>
#include <kernel/blockstorage.h>
#include <kernel/caches.h>
#include <kernel/chainparams.h>
#include <kernel/chainstate_load.h>
#include <kernel/checks.h>
#include <kernel/context.h>
#include <kernel/notifications_interface.h>
#include <kernel/warning.h>
#include <logging.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <sync.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/task_runner.h>
#include <util/translation.h>
#include <validation/block_data_adapters.h>
#include <validation/block_index_adapters.h>
#include <validation/block_validation.h>
#include <validation/chain_validation.h>
#include <validation/runtime_time.h>
#include <validation/tx_check_adapters.h>
#include <validation_state.h>
#include <validationinterface.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace Consensus {
struct Params;
} // namespace Consensus

using util::ImmediateTaskRunner;

// Define G_TRANSLATION_FUN symbol in libbitcoinkernel library so users of the
// library aren't required to export this symbol
extern const TranslateFn G_TRANSLATION_FUN{nullptr};

static const kernel::Context btck_context_static{};

namespace {

bool is_valid_flag_combination(script_verify_flags flags)
{
    if (flags & SCRIPT_VERIFY_CLEANSTACK && ~flags & (SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS)) return false;
    if (flags & SCRIPT_VERIFY_WITNESS && ~flags & SCRIPT_VERIFY_P2SH) return false;
    return true;
}

struct CallbackByteSink {
    btck_WriteBytes writer;
    void* user_data;

    void write(std::span<const std::byte> bytes);
};

class CallbackFailure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class CallbackWriteFailure : public CallbackFailure
{
public:
    using CallbackFailure::CallbackFailure;
};

void CallbackByteSink::write(std::span<const std::byte> bytes)
{
    if (writer(bytes.data(), bytes.size(), user_data) != 0) {
        throw CallbackWriteFailure{"failed to write serialized bytes"};
    }
}

class InvalidArgumentFailure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

template <typename Hash>
Hash HashFromBytes(const unsigned char input[32]) noexcept
{
    std::array<std::byte, 32> bytes{};
    std::ranges::transform(std::span<const unsigned char>{input, bytes.size()}, bytes.begin(), [](unsigned char byte) {
        return static_cast<std::byte>(byte);
    });
    return Hash{bytes};
}

template <typename Hash>
void CopyHashBytes(Hash value, unsigned char output[32]) noexcept
{
    std::ranges::transform(as_bytes(value), output, [](std::byte byte) {
        return std::to_integer<unsigned char>(byte);
    });
}

BlockValidationResult BlockValidationResultFor(const bitcoin::validation_rejection& rejection) noexcept
{
    switch (rejection.rule_id()) {
    case bitcoin::validation_rule_id::h01_previous_hash_parent:
    case bitcoin::validation_rule_id::h02_proof_of_work:
    case bitcoin::validation_rule_id::h03_difficulty_transition:
    case bitcoin::validation_rule_id::h04_median_time_past:
    case bitcoin::validation_rule_id::h05_future_time:
    case bitcoin::validation_rule_id::h06_retired_version:
    case bitcoin::validation_rule_id::h07_timewarp:
        return BlockValidationResult::BLOCK_INVALID_HEADER;
    case bitcoin::validation_rule_id::l02_merkle_root:
    case bitcoin::validation_rule_id::l03_merkle_mutation:
        return BlockValidationResult::BLOCK_MUTATED;
    case bitcoin::validation_rule_id::l01_block_non_empty:
    case bitcoin::validation_rule_id::l04_original_block_size:
    case bitcoin::validation_rule_id::l05_coinbase_position:
    case bitcoin::validation_rule_id::l06_legacy_sigops:
    case bitcoin::validation_rule_id::l07_transaction_inputs_non_empty:
    case bitcoin::validation_rule_id::l08_transaction_outputs_non_empty:
    case bitcoin::validation_rule_id::l09_transaction_size:
    case bitcoin::validation_rule_id::l10_output_value_non_negative:
    case bitcoin::validation_rule_id::l11_output_value_range:
    case bitcoin::validation_rule_id::l12_unique_inputs:
    case bitcoin::validation_rule_id::l13_coinbase_script_size:
    case bitcoin::validation_rule_id::l14_non_coinbase_prevout:
    case bitcoin::validation_rule_id::c01_transaction_finality:
    case bitcoin::validation_rule_id::c02_pre_segwit_no_witness:
    case bitcoin::validation_rule_id::c03_block_weight:
    case bitcoin::validation_rule_id::c04_coinbase_height:
    case bitcoin::validation_rule_id::c05_witness_commitment_presence:
    case bitcoin::validation_rule_id::c06_witness_nonce_presence:
    case bitcoin::validation_rule_id::c07_witness_merkle_commitment:
    case bitcoin::validation_rule_id::s01_bip30_duplicate_unspent:
    case bitcoin::validation_rule_id::s02_prevouts_unspent:
    case bitcoin::validation_rule_id::s03_sigop_cost:
    case bitcoin::validation_rule_id::s04_coinbase_subsidy:
    case bitcoin::validation_rule_id::s05_outputs_do_not_exceed_inputs:
    case bitcoin::validation_rule_id::s06_input_value_and_fee_range:
    case bitcoin::validation_rule_id::s07_scripts_validate:
    case bitcoin::validation_rule_id::s08_sequence_locks:
    case bitcoin::validation_rule_id::s09_coinbase_maturity:
        return BlockValidationResult::BLOCK_CONSENSUS;
    }
    return BlockValidationResult::BLOCK_CONSENSUS;
}

btck_ValidationRejectionCode ValidationRejectionCodeFor(bitcoin::validation_rejection_code code) noexcept
{
    switch (code) {
    case bitcoin::validation_rejection_code::rule_violation:
        return btck_ValidationRejectionCode_RULE_VIOLATION;
    }
    return btck_ValidationRejectionCode_NONE;
}

btck_ValidationRule ValidationRuleFor(bitcoin::validation_rule_id id) noexcept
{
    static_assert(bitcoin::validation_rule_count == 37);

    switch (id) {
    case bitcoin::validation_rule_id::h01_previous_hash_parent:
        return btck_ValidationRule_H01_PREVIOUS_HASH_PARENT;
    case bitcoin::validation_rule_id::h02_proof_of_work:
        return btck_ValidationRule_H02_PROOF_OF_WORK;
    case bitcoin::validation_rule_id::h03_difficulty_transition:
        return btck_ValidationRule_H03_DIFFICULTY_TRANSITION;
    case bitcoin::validation_rule_id::h04_median_time_past:
        return btck_ValidationRule_H04_MEDIAN_TIME_PAST;
    case bitcoin::validation_rule_id::h05_future_time:
        return btck_ValidationRule_H05_FUTURE_TIME;
    case bitcoin::validation_rule_id::h06_retired_version:
        return btck_ValidationRule_H06_RETIRED_VERSION;
    case bitcoin::validation_rule_id::h07_timewarp:
        return btck_ValidationRule_H07_TIMEWARP;
    case bitcoin::validation_rule_id::l01_block_non_empty:
        return btck_ValidationRule_L01_BLOCK_NON_EMPTY;
    case bitcoin::validation_rule_id::l02_merkle_root:
        return btck_ValidationRule_L02_MERKLE_ROOT;
    case bitcoin::validation_rule_id::l03_merkle_mutation:
        return btck_ValidationRule_L03_MERKLE_MUTATION;
    case bitcoin::validation_rule_id::l04_original_block_size:
        return btck_ValidationRule_L04_ORIGINAL_BLOCK_SIZE;
    case bitcoin::validation_rule_id::l05_coinbase_position:
        return btck_ValidationRule_L05_COINBASE_POSITION;
    case bitcoin::validation_rule_id::l06_legacy_sigops:
        return btck_ValidationRule_L06_LEGACY_SIGOPS;
    case bitcoin::validation_rule_id::l07_transaction_inputs_non_empty:
        return btck_ValidationRule_L07_TRANSACTION_INPUTS_NON_EMPTY;
    case bitcoin::validation_rule_id::l08_transaction_outputs_non_empty:
        return btck_ValidationRule_L08_TRANSACTION_OUTPUTS_NON_EMPTY;
    case bitcoin::validation_rule_id::l09_transaction_size:
        return btck_ValidationRule_L09_TRANSACTION_SIZE;
    case bitcoin::validation_rule_id::l10_output_value_non_negative:
        return btck_ValidationRule_L10_OUTPUT_VALUE_NON_NEGATIVE;
    case bitcoin::validation_rule_id::l11_output_value_range:
        return btck_ValidationRule_L11_OUTPUT_VALUE_RANGE;
    case bitcoin::validation_rule_id::l12_unique_inputs:
        return btck_ValidationRule_L12_UNIQUE_INPUTS;
    case bitcoin::validation_rule_id::l13_coinbase_script_size:
        return btck_ValidationRule_L13_COINBASE_SCRIPT_SIZE;
    case bitcoin::validation_rule_id::l14_non_coinbase_prevout:
        return btck_ValidationRule_L14_NON_COINBASE_PREVOUT;
    case bitcoin::validation_rule_id::c01_transaction_finality:
        return btck_ValidationRule_C01_TRANSACTION_FINALITY;
    case bitcoin::validation_rule_id::c02_pre_segwit_no_witness:
        return btck_ValidationRule_C02_PRE_SEGWIT_NO_WITNESS;
    case bitcoin::validation_rule_id::c03_block_weight:
        return btck_ValidationRule_C03_BLOCK_WEIGHT;
    case bitcoin::validation_rule_id::c04_coinbase_height:
        return btck_ValidationRule_C04_COINBASE_HEIGHT;
    case bitcoin::validation_rule_id::c05_witness_commitment_presence:
        return btck_ValidationRule_C05_WITNESS_COMMITMENT_PRESENCE;
    case bitcoin::validation_rule_id::c06_witness_nonce_presence:
        return btck_ValidationRule_C06_WITNESS_NONCE_PRESENCE;
    case bitcoin::validation_rule_id::c07_witness_merkle_commitment:
        return btck_ValidationRule_C07_WITNESS_MERKLE_COMMITMENT;
    case bitcoin::validation_rule_id::s01_bip30_duplicate_unspent:
        return btck_ValidationRule_S01_BIP30_DUPLICATE_UNSPENT;
    case bitcoin::validation_rule_id::s02_prevouts_unspent:
        return btck_ValidationRule_S02_PREVOUTS_UNSPENT;
    case bitcoin::validation_rule_id::s03_sigop_cost:
        return btck_ValidationRule_S03_SIGOP_COST;
    case bitcoin::validation_rule_id::s04_coinbase_subsidy:
        return btck_ValidationRule_S04_COINBASE_SUBSIDY;
    case bitcoin::validation_rule_id::s05_outputs_do_not_exceed_inputs:
        return btck_ValidationRule_S05_OUTPUTS_DO_NOT_EXCEED_INPUTS;
    case bitcoin::validation_rule_id::s06_input_value_and_fee_range:
        return btck_ValidationRule_S06_INPUT_VALUE_AND_FEE_RANGE;
    case bitcoin::validation_rule_id::s07_scripts_validate:
        return btck_ValidationRule_S07_SCRIPTS_VALIDATE;
    case bitcoin::validation_rule_id::s08_sequence_locks:
        return btck_ValidationRule_S08_SEQUENCE_LOCKS;
    case bitcoin::validation_rule_id::s09_coinbase_maturity:
        return btck_ValidationRule_S09_COINBASE_MATURITY;
    }
    return btck_ValidationRule_NONE;
}

btck_HeaderContextEvidenceCode HeaderContextEvidenceCodeFor(bitcoin::header_context_evidence_code code) noexcept
{
    switch (code) {
    case bitcoin::header_context_evidence_code::genesis_parent_not_null:
        return btck_HeaderContextEvidenceCode_GENESIS_PARENT_NOT_NULL;
    case bitcoin::header_context_evidence_code::non_contiguous_ancestry:
        return btck_HeaderContextEvidenceCode_NON_CONTIGUOUS_ANCESTRY;
    }
    return btck_HeaderContextEvidenceCode_NONE;
}

void ApplyRejection(BlockValidationState& state, const bitcoin::validation_rejection& rejection)
{
    state.Invalid(BlockValidationResultFor(rejection), std::string{rejection.reason()});
}

void ApplyInvalidHeaderContextEvidence(BlockValidationState& state, const bitcoin::invalid_header_context_evidence& evidence)
{
    switch (evidence.code()) {
    case bitcoin::header_context_evidence_code::genesis_parent_not_null:
    case bitcoin::header_context_evidence_code::non_contiguous_ancestry:
        state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, std::string{evidence.reason()});
        return;
    }
    state.Invalid(BlockValidationResult::BLOCK_INVALID_PREV, "invalid header ancestry evidence");
}

void ApplyRejection(TxValidationState& state, const bitcoin::validation_rejection& rejection)
{
    state.Invalid(TxValidationResult::TX_CONSENSUS, std::string{rejection.reason()});
}

struct ErrorValue {
    btck_ErrorCode code{btck_ErrorCode_NONE};
    std::string message{};
};

template <typename C, typename CPP>
struct Handle {
    static C* ref(CPP* cpp_type)
    {
        return reinterpret_cast<C*>(cpp_type);
    }

    static const C* ref(const CPP* cpp_type)
    {
        return reinterpret_cast<const C*>(cpp_type);
    }

    template <typename... Args>
    static C* create(Args&&... args)
    {
        auto cpp_obj{std::make_unique<CPP>(std::forward<Args>(args)...)};
        return ref(cpp_obj.release());
    }

    static C* copy(const C* ptr)
    {
        auto cpp_obj{std::make_unique<CPP>(get(ptr))};
        return ref(cpp_obj.release());
    }

    static const CPP& get(const C* ptr)
    {
        return *reinterpret_cast<const CPP*>(ptr);
    }

    static CPP& get(C* ptr)
    {
        return *reinterpret_cast<CPP*>(ptr);
    }

    static void operator delete(void* ptr)
    {
        delete reinterpret_cast<CPP*>(ptr);
    }
};

template <typename F>
auto KernelTryPointer(const char* operation, F&& fn) -> decltype(std::forward<F>(fn)())
{
    try {
        return std::forward<F>(fn)();
    } catch (const CallbackFailure& e) {
        LogError("%s failed: %s", operation, e.what());
    } catch (const std::exception& e) {
        LogError("%s failed: %s", operation, e.what());
    } catch (...) {
        LogError("%s failed: unknown exception", operation);
    }
    return nullptr;
}

template <typename T, typename F>
T KernelTryValue(const char* operation, T failure, F&& fn)
{
    try {
        return std::forward<F>(fn)();
    } catch (const CallbackFailure& e) {
        LogError("%s failed: %s", operation, e.what());
    } catch (const std::exception& e) {
        LogError("%s failed: %s", operation, e.what());
    } catch (...) {
        LogError("%s failed: unknown exception", operation);
    }
    return failure;
}

template <typename F>
int KernelTryInt(const char* operation, int failure, F&& fn)
{
    return KernelTryValue(operation, failure, std::forward<F>(fn));
}

template <typename F>
void KernelTryVoid(const char* operation, F&& fn)
{
    try {
        std::forward<F>(fn)();
    } catch (const std::exception& e) {
        LogError("%s failed: %s", operation, e.what());
    } catch (...) {
        LogError("%s failed: unknown exception", operation);
    }
}

template <typename F>
void KernelInvokeCallback(const char* operation, F&& fn)
{
    try {
        if (std::forward<F>(fn)() != 0) {
            std::string message{operation};
            message += " callback returned failure";
            throw CallbackFailure{message};
        }
    } catch (const CallbackFailure&) {
        throw;
    } catch (const std::exception& e) {
        std::string message{operation};
        message += " callback threw: ";
        message += e.what();
        throw CallbackFailure{message};
    } catch (...) {
        std::string message{operation};
        message += " callback threw: unknown exception";
        throw CallbackFailure{message};
    }
}

template <typename F>
void KernelInvokeCallbackNoThrow(const char* operation, F&& fn) noexcept
{
    try {
        KernelInvokeCallback(operation, std::forward<F>(fn));
    } catch (const std::exception& e) {
        LogError("%s cleanup callback failed: %s", operation, e.what());
    } catch (...) {
        LogError("%s cleanup callback failed: unknown exception", operation);
    }
}

struct BlockInfoSnapshot {
    int32_t height{-1};
    bitcoin::block_hash hash{};
    std::optional<bitcoin::block_hash> previous_hash{};
    bitcoin::block_header header{};
};

struct ChainSnapshot {
    std::vector<BlockInfoSnapshot> blocks;
};

struct TransactionParseResultValue {
    btck_ParseStatus status{btck_ParseStatus_MALFORMED};
    btck_ParseFailureCode failure_code{btck_ParseFailureCode_NONE};
    size_t failure_offset{0};
    std::shared_ptr<const bitcoin::transaction> transaction{};
};

struct BlockParseResultValue {
    btck_ParseStatus status{btck_ParseStatus_MALFORMED};
    btck_ParseFailureCode failure_code{btck_ParseFailureCode_NONE};
    size_t failure_offset{0};
    std::shared_ptr<const bitcoin::block> block{};
};

struct BlockHeaderParseResultValue {
    btck_ParseStatus status{btck_ParseStatus_MALFORMED};
    btck_ParseFailureCode failure_code{btck_ParseFailureCode_NONE};
    size_t failure_offset{0};
    std::optional<bitcoin::block_header> header{};
};

struct TransactionCheckResultValue {
    btck_CheckStatus status{btck_CheckStatus_INVALID};
    TxValidationState validation_state{};
};

struct BlockCheckResultValue {
    btck_CheckStatus status{btck_CheckStatus_INVALID};
    BlockValidationState validation_state{};
};

struct BlockVerifyResultValue {
    btck_CheckStatus status{btck_CheckStatus_INVALID};
    BlockValidationState validation_state{};
    std::optional<bitcoin::validation_rejection> rejection{};
    std::optional<bitcoin::invalid_header_context_evidence> header_context_evidence{};
};

struct HeaderProcessResultValue {
    btck_HeaderProcessStatus status{btck_HeaderProcessStatus_REJECTED};
    BlockValidationState validation_state{};
};

struct BlockProcessResultValue {
    btck_BlockProcessStatus status{btck_BlockProcessStatus_CHECK_FAILED};
    bool has_new_block_data{false};
    BlockValidationState validation_state{};
};

struct BlockImportResultValue {
    btck_BlockImportStatus status{btck_BlockImportStatus_COMPLETED};
    int loaded_blocks{0};
    int skipped_records{0};
    int skipped_blocks{0};
    int rejected_blocks{0};
};

struct BlockReadResultValue {
    btck_BlockReadStatus status{btck_BlockReadStatus_NOT_INDEXED};
    std::shared_ptr<const bitcoin::block> block{};
};

struct CoinValue {
    bitcoin::tx_output output;
    uint32_t height{0};
    bool coinbase{false};
    bitcoin::median_time_past previous_median_time_past{};
};

struct TransactionSpentOutputsValue {
    std::vector<CoinValue> coins;
};

struct BlockSpentOutputsValue {
    std::vector<TransactionSpentOutputsValue> transactions;
};

struct BlockSpentOutputsReadResultValue {
    btck_BlockSpentOutputsReadStatus status{btck_BlockSpentOutputsReadStatus_NOT_INDEXED};
    std::shared_ptr<BlockSpentOutputsValue> spent_outputs{};
};

btck_ParseFailureCode ParseFailureCodeFor(bitcoin::parse_failure_code code) noexcept
{
    switch (code) {
    case bitcoin::parse_failure_code::truncated:
        return btck_ParseFailureCode_TRUNCATED;
    case bitcoin::parse_failure_code::trailing_data:
        return btck_ParseFailureCode_TRAILING_DATA;
    case bitcoin::parse_failure_code::non_canonical_compact_size:
        return btck_ParseFailureCode_NON_CANONICAL_COMPACT_SIZE;
    case bitcoin::parse_failure_code::compact_size_overflow:
        return btck_ParseFailureCode_COMPACT_SIZE_OVERFLOW;
    case bitcoin::parse_failure_code::invalid_witness_marker:
        return btck_ParseFailureCode_INVALID_WITNESS_MARKER;
    }
    return btck_ParseFailureCode_NONE;
}

template <typename ParseResultValue>
ParseResultValue MalformedParseResult(bitcoin::malformed_parse failure)
{
    return ParseResultValue{
        .status = btck_ParseStatus_MALFORMED,
        .failure_code = ParseFailureCodeFor(failure.code()),
        .failure_offset = failure.offset(),
    };
}

BlockInfoSnapshot SnapshotBlockInfo(const CBlockIndex& index)
{
    return BlockInfoSnapshot{
        .height = index.nHeight,
        .hash = bitcoin::core_adapter::to_block_hash(index.GetBlockHash()),
        .previous_hash = index.pprev ? std::optional<bitcoin::block_hash>{bitcoin::core_adapter::to_block_hash(index.pprev->GetBlockHash())} : std::nullopt,
        .header = bitcoin::core_adapter::to_block_header(index.GetBlockHeader()),
    };
}

BlockInfoSnapshot SnapshotBlockInfo(const validation::ValidationBlockInfo& info)
{
    return BlockInfoSnapshot{
        .height = info.height,
        .hash = bitcoin::core_adapter::to_block_hash(info.hash),
        .previous_hash = info.previous_hash ? std::optional<bitcoin::block_hash>{bitcoin::core_adapter::to_block_hash(*info.previous_hash)} : std::nullopt,
        .header = bitcoin::core_adapter::to_block_header(info.header),
    };
}

CoinValue SnapshotCoin(const Coin& coin)
{
    return CoinValue{
        .output = bitcoin::core_adapter::to_tx_output(coin.out),
        .height = static_cast<uint32_t>(coin.nHeight),
        .coinbase = coin.IsCoinBase(),
    };
}

BlockSpentOutputsValue SnapshotBlockSpentOutputs(const CBlockUndo& undo)
{
    BlockSpentOutputsValue result;
    result.transactions.reserve(undo.vtxundo.size());
    for (const auto& tx_undo : undo.vtxundo) {
        auto& tx_outputs{result.transactions.emplace_back()};
        tx_outputs.coins.reserve(tx_undo.vprevout.size());
        for (const auto& coin : tx_undo.vprevout) {
            tx_outputs.coins.push_back(SnapshotCoin(coin));
        }
    }
    return result;
}

ChainSnapshot SnapshotActiveChain(const ChainstateManager& chainman)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    const CChain& chain{chainman.ActiveChain()};
    ChainSnapshot snapshot;
    snapshot.blocks.reserve(static_cast<size_t>(chain.Height() + 1));
    for (int32_t height{0}; height <= chain.Height(); ++height) {
        snapshot.blocks.push_back(SnapshotBlockInfo(*Assert(chain[height])));
    }
    return snapshot;
}

} // namespace

struct btck_Error : Handle<btck_Error, ErrorValue> {
};
struct btck_BlockInfo : Handle<btck_BlockInfo, BlockInfoSnapshot> {
};
struct btck_ChainSnapshot : Handle<btck_ChainSnapshot, ChainSnapshot> {
};
struct btck_Block : Handle<btck_Block, bitcoin::block> {
};
struct btck_BlockValidationState : Handle<btck_BlockValidationState, BlockValidationState> {
};
struct btck_TxValidationState : Handle<btck_TxValidationState, TxValidationState> {
};
struct btck_TransactionParseResult : Handle<btck_TransactionParseResult, TransactionParseResultValue> {
};
struct btck_BlockParseResult : Handle<btck_BlockParseResult, BlockParseResultValue> {
};
struct btck_BlockHeaderParseResult : Handle<btck_BlockHeaderParseResult, BlockHeaderParseResultValue> {
};
struct btck_TransactionCheckResult : Handle<btck_TransactionCheckResult, TransactionCheckResultValue> {
};
struct btck_BlockCheckResult : Handle<btck_BlockCheckResult, BlockCheckResultValue> {
};
struct btck_BlockVerifyResult : Handle<btck_BlockVerifyResult, BlockVerifyResultValue> {
};
struct btck_HeaderProcessResult : Handle<btck_HeaderProcessResult, HeaderProcessResultValue> {
};
struct btck_BlockProcessResult : Handle<btck_BlockProcessResult, BlockProcessResultValue> {
};
struct btck_BlockImportResult : Handle<btck_BlockImportResult, BlockImportResultValue> {
};
struct btck_BlockReadResult : Handle<btck_BlockReadResult, BlockReadResultValue> {
};
struct btck_BlockSpentOutputsReadResult : Handle<btck_BlockSpentOutputsReadResult, BlockSpentOutputsReadResultValue> {
};

namespace {

BCLog::Level get_bclog_level(btck_LogLevel level)
{
    switch (level) {
    case btck_LogLevel_INFO: {
        return BCLog::Level::Info;
    }
    case btck_LogLevel_DEBUG: {
        return BCLog::Level::Debug;
    }
    case btck_LogLevel_TRACE: {
        return BCLog::Level::Trace;
    }
    }
    assert(false);
}

BCLog::LogFlags get_bclog_flag(btck_LogCategory category)
{
    switch (category) {
    case btck_LogCategory_BENCH: {
        return BCLog::LogFlags::BENCH;
    }
    case btck_LogCategory_BLOCKSTORAGE: {
        return BCLog::LogFlags::BLOCKSTORAGE;
    }
    case btck_LogCategory_COINDB: {
        return BCLog::LogFlags::COINDB;
    }
    case btck_LogCategory_LEVELDB: {
        return BCLog::LogFlags::LEVELDB;
    }
    case btck_LogCategory_MEMPOOL: {
        return BCLog::LogFlags::MEMPOOL;
    }
    case btck_LogCategory_PRUNE: {
        return BCLog::LogFlags::PRUNE;
    }
    case btck_LogCategory_RAND: {
        return BCLog::LogFlags::RAND;
    }
    case btck_LogCategory_REINDEX: {
        return BCLog::LogFlags::REINDEX;
    }
    case btck_LogCategory_VALIDATION: {
        return BCLog::LogFlags::VALIDATION;
    }
    case btck_LogCategory_KERNEL: {
        return BCLog::LogFlags::KERNEL;
    }
    case btck_LogCategory_ALL: {
        return BCLog::LogFlags::ALL;
    }
    }
    assert(false);
}

btck_SynchronizationState cast_state(SynchronizationState state)
{
    switch (state) {
    case SynchronizationState::INIT_REINDEX:
        return btck_SynchronizationState_INIT_REINDEX;
    case SynchronizationState::INIT_DOWNLOAD:
        return btck_SynchronizationState_INIT_DOWNLOAD;
    case SynchronizationState::POST_INIT:
        return btck_SynchronizationState_POST_INIT;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

btck_Warning cast_btck_warning(kernel::Warning warning)
{
    switch (warning) {
    case kernel::Warning::UNKNOWN_NEW_RULES_ACTIVATED:
        return btck_Warning_UNKNOWN_NEW_RULES_ACTIVATED;
    case kernel::Warning::LARGE_WORK_INVALID_CHAIN:
        return btck_Warning_LARGE_WORK_INVALID_CHAIN;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

struct LoggingConnection {
    std::unique_ptr<std::list<std::function<void(const std::string&)>>::iterator> m_connection;
    void* m_user_data;
    std::function<void(void* user_data)> m_deleter;

    LoggingConnection(btck_LogCallback callback, void* user_data, btck_DestroyCallback user_data_destroy_callback)
    {
        LOCK(cs_main);

        auto connection{LogInstance().PushBackCallback([callback, user_data](const std::string& str) {
            KernelInvokeCallbackNoThrow(__func__, [&] {
                callback(user_data, str.c_str(), str.length());
                return 0;
            });
        })};

        // Only start logging if we just added the connection.
        if (LogInstance().NumConnections() == 1 && !LogInstance().StartLogging()) {
            LogError("Logger start failed.");
            LogInstance().DeleteCallback(connection);
            throw std::runtime_error("Failed to start logging");
        }

        m_connection = std::make_unique<std::list<std::function<void(const std::string&)>>::iterator>(connection);
        m_user_data = user_data;
        m_deleter = user_data_destroy_callback;

        LogDebug(BCLog::KERNEL, "Logger connected.");
    }

    ~LoggingConnection()
    {
        LOCK(cs_main);
        LogDebug(BCLog::KERNEL, "Logger disconnecting.");

        // Switch back to buffering by calling DisconnectTestLogger if the
        // connection that we are about to remove is the last one.
        if (LogInstance().NumConnections() == 1) {
            LogInstance().DisconnectTestLogger();
        } else {
            LogInstance().DeleteCallback(*m_connection);
        }

        m_connection.reset();
        KernelInvokeCallbackNoThrow(__func__, [&] {
            if (m_user_data && m_deleter) {
                m_deleter(m_user_data);
            }
            return 0;
        });
    }
};

class KernelNotifications final : public kernel::Notifications
{
private:
    btck_NotificationInterfaceCallbacks m_cbs;

public:
    KernelNotifications(btck_NotificationInterfaceCallbacks cbs)
        : m_cbs{cbs}
    {
    }

    ~KernelNotifications()
    {
        KernelInvokeCallbackNoThrow(__func__, [&] {
            if (m_cbs.user_data && m_cbs.user_data_destroy) {
                m_cbs.user_data_destroy(m_cbs.user_data);
            }
            return 0;
        });
        m_cbs.user_data_destroy = nullptr;
        m_cbs.user_data = nullptr;
    }

    kernel::InterruptResult blockTip(SynchronizationState state, const CBlockIndex& index, double verification_progress) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.block_tip) {
                const BlockInfoSnapshot tip{SnapshotBlockInfo(index)};
                return m_cbs.block_tip(m_cbs.user_data, cast_state(state), btck_BlockInfo::ref(&tip), verification_progress);
            }
            return 0;
        });
        return {};
    }
    void headerTip(SynchronizationState state, int64_t height, int64_t timestamp, bool presync) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.header_tip) return m_cbs.header_tip(m_cbs.user_data, cast_state(state), height, timestamp, presync ? 1 : 0);
            return 0;
        });
    }
    void progress(const bilingual_str& title, int progress_percent, bool resume_possible) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.progress) return m_cbs.progress(m_cbs.user_data, title.original.c_str(), title.original.length(), progress_percent, resume_possible ? 1 : 0);
            return 0;
        });
    }
    void warningSet(kernel::Warning id, const bilingual_str& message) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.warning_set) return m_cbs.warning_set(m_cbs.user_data, cast_btck_warning(id), message.original.c_str(), message.original.length());
            return 0;
        });
    }
    void warningUnset(kernel::Warning id) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.warning_unset) return m_cbs.warning_unset(m_cbs.user_data, cast_btck_warning(id));
            return 0;
        });
    }
    void flushError(const bilingual_str& message) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.flush_error) return m_cbs.flush_error(m_cbs.user_data, message.original.c_str(), message.original.length());
            return 0;
        });
    }
    void fatalError(const bilingual_str& message) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.fatal_error) return m_cbs.fatal_error(m_cbs.user_data, message.original.c_str(), message.original.length());
            return 0;
        });
    }
};

class KernelValidationInterface final : public CValidationInterface
{
public:
    btck_ValidationInterfaceCallbacks m_cbs;

    explicit KernelValidationInterface(const btck_ValidationInterfaceCallbacks vi_cbs) : m_cbs{vi_cbs} {}

    ~KernelValidationInterface()
    {
        KernelInvokeCallbackNoThrow(__func__, [&] {
            if (m_cbs.user_data && m_cbs.user_data_destroy) {
                m_cbs.user_data_destroy(m_cbs.user_data);
            }
            return 0;
        });
        m_cbs.user_data = nullptr;
        m_cbs.user_data_destroy = nullptr;
    }

protected:
    void BlockChecked(const validation::BlockCheckedEvent& event) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.block_checked) {
                const bitcoin::block block{bitcoin::core_adapter::to_block(*event.block)};
                return m_cbs.block_checked(m_cbs.user_data,
                                           btck_Block::ref(&block),
                                           btck_BlockValidationState::ref(&event.state));
            }
            return 0;
        });
    }

    void NewPoWValidBlock(const validation::PoWValidBlockEvent& event) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.pow_valid_block) {
                const bitcoin::block block{bitcoin::core_adapter::to_block(*event.block)};
                const BlockInfoSnapshot info{SnapshotBlockInfo(event.block_info)};
                return m_cbs.pow_valid_block(m_cbs.user_data,
                                             btck_Block::ref(&block),
                                             btck_BlockInfo::ref(&info));
            }
            return 0;
        });
    }

    void BlockConnected(const validation::BlockConnectedEvent& event) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.block_connected) {
                const bitcoin::block block{bitcoin::core_adapter::to_block(*event.block)};
                const BlockInfoSnapshot info{SnapshotBlockInfo(event.block_info)};
                return m_cbs.block_connected(m_cbs.user_data,
                                             btck_Block::ref(&block),
                                             btck_BlockInfo::ref(&info));
            }
            return 0;
        });
    }

    void BlockDisconnected(const validation::BlockDisconnectedEvent& event) override
    {
        KernelInvokeCallback(__func__, [&] {
            if (m_cbs.block_disconnected) {
                const bitcoin::block block{bitcoin::core_adapter::to_block(*event.block)};
                const BlockInfoSnapshot info{SnapshotBlockInfo(event.block_info)};
                return m_cbs.block_disconnected(m_cbs.user_data,
                                                btck_Block::ref(&block),
                                                btck_BlockInfo::ref(&info));
            }
            return 0;
        });
    }
};

struct ContextOptions {
    mutable Mutex m_mutex;
    std::unique_ptr<const CChainParams> m_chainparams GUARDED_BY(m_mutex);
    std::shared_ptr<KernelNotifications> m_notifications GUARDED_BY(m_mutex);
    std::shared_ptr<KernelValidationInterface> m_validation_interface GUARDED_BY(m_mutex);
};

class Context
{
public:
    std::unique_ptr<kernel::Context> m_context;

    std::shared_ptr<KernelNotifications> m_notifications;

    std::unique_ptr<util::SignalInterrupt> m_interrupt;

    std::unique_ptr<ValidationSignals> m_signals;

    std::unique_ptr<const CChainParams> m_chainparams;

    std::shared_ptr<KernelValidationInterface> m_validation_interface;

    Context(const ContextOptions* options, bool& sane)
        : m_context{std::make_unique<kernel::Context>()},
          m_interrupt{std::make_unique<util::SignalInterrupt>()}
    {
        if (options) {
            LOCK(options->m_mutex);
            if (options->m_chainparams) {
                m_chainparams = std::make_unique<const CChainParams>(*options->m_chainparams);
            }
            if (options->m_notifications) {
                m_notifications = options->m_notifications;
            }
            if (options->m_validation_interface) {
                m_signals = std::make_unique<ValidationSignals>(std::make_unique<ImmediateTaskRunner>());
                m_validation_interface = options->m_validation_interface;
                m_signals->RegisterSharedValidationInterface(m_validation_interface);
            }
        }

        if (!m_chainparams) {
            m_chainparams = CChainParams::Main();
        }
        if (!m_notifications) {
            m_notifications = std::make_shared<KernelNotifications>(btck_NotificationInterfaceCallbacks{
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
        }

        if (!kernel::SanityChecks(*m_context)) {
            sane = false;
        }
    }

    ~Context()
    {
        if (m_signals) {
            m_signals->UnregisterSharedValidationInterface(m_validation_interface);
        }
    }
};

//! Helper struct to wrap public chainstate options backed by ChainstateManager internals.
struct ChainstateOptions {
    mutable Mutex m_mutex;
    ChainstateManager::Options m_chainman_options GUARDED_BY(m_mutex);
    kernel::BlockManager::Options m_blockman_options GUARDED_BY(m_mutex);
    std::shared_ptr<const Context> m_context;
    kernel::ChainstateLoadOptions m_chainstate_load_options GUARDED_BY(m_mutex);

    ChainstateOptions(const std::shared_ptr<const Context>& context, const fs::path& data_dir, const fs::path& blocks_dir)
        : m_chainman_options{ChainstateManager::Options{
              .chainparams = *context->m_chainparams,
              .datadir = data_dir,
              .notifications = *context->m_notifications,
              .signals = context->m_signals.get()}},
          m_blockman_options{kernel::BlockManager::Options{
              .chainparams = *context->m_chainparams,
              .blocks_dir = blocks_dir,
              .notifications = *context->m_notifications,
              .block_tree_db_params = DBParams{
                  .path = data_dir / "blocks" / "index",
                  .cache_bytes = kernel::CacheSizes{DEFAULT_KERNEL_CACHE}.block_tree_db,
              }}},
          m_context{context},
          m_chainstate_load_options{kernel::ChainstateLoadOptions{}}
    {
    }
};

struct BlockValidationOptions {
    std::optional<BlockValidationTime> m_time;
};

struct ChainstateRuntime {
    std::optional<NodeSeconds> m_current_time;
};

struct KernelChainstate {
    std::unique_ptr<ChainstateManager> m_chainman;
    std::shared_ptr<const Context> m_context;

    KernelChainstate(std::unique_ptr<ChainstateManager> chainman, std::shared_ptr<const Context> context)
        : m_chainman(std::move(chainman)), m_context(std::move(context)) {}
};

} // namespace

struct btck_Transaction : Handle<btck_Transaction, bitcoin::transaction> {
};
struct btck_TransactionOutput : Handle<btck_TransactionOutput, bitcoin::tx_output> {
};
struct btck_ScriptPubkey : Handle<btck_ScriptPubkey, bitcoin::script> {
};
struct btck_LoggingConnection : Handle<btck_LoggingConnection, LoggingConnection> {
};
struct btck_ContextOptions : Handle<btck_ContextOptions, ContextOptions> {
};
struct btck_Context : Handle<btck_Context, std::shared_ptr<const Context>> {
};
struct btck_ChainParameters : Handle<btck_ChainParameters, CChainParams> {
};
struct btck_ChainstateOptions : Handle<btck_ChainstateOptions, ChainstateOptions> {
};
struct btck_ChainstateRuntime : Handle<btck_ChainstateRuntime, ChainstateRuntime> {
};
struct btck_BlockValidationOptions : Handle<btck_BlockValidationOptions, BlockValidationOptions> {
};
struct btck_Chainstate : Handle<btck_Chainstate, KernelChainstate> {
};
struct btck_BlockSpentOutputs : Handle<btck_BlockSpentOutputs, std::shared_ptr<BlockSpentOutputsValue>> {
};
struct btck_TransactionSpentOutputs : Handle<btck_TransactionSpentOutputs, TransactionSpentOutputsValue> {
};
struct btck_Coin : Handle<btck_Coin, CoinValue> {
};
struct btck_BlockHash : Handle<btck_BlockHash, bitcoin::block_hash> {
};
struct btck_TransactionInput : Handle<btck_TransactionInput, bitcoin::tx_input> {
};
struct btck_TransactionOutPoint : Handle<btck_TransactionOutPoint, bitcoin::outpoint> {
};
struct btck_Txid : Handle<btck_Txid, bitcoin::txid> {
};
struct btck_PrecomputedTransactionData : Handle<btck_PrecomputedTransactionData, PrecomputedTransactionData> {
};
struct btck_BlockHeader : Handle<btck_BlockHeader, bitcoin::block_header> {
};
struct btck_ConsensusParams : Handle<btck_ConsensusParams, Consensus::Params> {
};

namespace {

void ClearError(btck_Error** error) noexcept
{
    if (error) *error = nullptr;
}

void SetError(btck_Error** error, btck_ErrorCode code, std::string_view message) noexcept
{
    if (!error) return;
    try {
        *error = btck_Error::create(ErrorValue{.code = code, .message = std::string{message}});
    } catch (const std::exception& e) {
        LogError("failed to allocate btck_Error: %s", e.what());
        *error = nullptr;
    } catch (...) {
        LogError("failed to allocate btck_Error: unknown exception");
        *error = nullptr;
    }
}

void SetOperationError(btck_Error** error, btck_ErrorCode code, const char* operation, std::string_view detail) noexcept
{
    try {
        std::string message{operation};
        message += " failed: ";
        message += detail;
        SetError(error, code, message);
    } catch (const std::exception& e) {
        LogError("failed to format btck_Error: %s", e.what());
        SetError(error, code, "operation failed");
    } catch (...) {
        LogError("failed to format btck_Error: unknown exception");
        SetError(error, code, "operation failed");
    }
}

void SetExceptionError(btck_Error** error, const char* operation, const std::exception& e) noexcept
{
    LogError("%s failed: %s", operation, e.what());
    SetOperationError(error, btck_ErrorCode_EXCEPTION, operation, e.what());
}

void SetUnknownExceptionError(btck_Error** error, const char* operation) noexcept
{
    LogError("%s failed: unknown exception", operation);
    SetOperationError(error, btck_ErrorCode_EXCEPTION, operation, "unknown exception");
}

btck_ErrorCode ChainstateLoadErrorCode(kernel::ChainstateLoadStatus status) noexcept
{
    switch (status) {
    case kernel::ChainstateLoadStatus::SUCCESS:
        return btck_ErrorCode_NONE;
    case kernel::ChainstateLoadStatus::FAILURE:
        return btck_ErrorCode_CHAINSTATE_LOAD;
    case kernel::ChainstateLoadStatus::FAILURE_FATAL:
    case kernel::ChainstateLoadStatus::FAILURE_INCOMPATIBLE_DB:
        return btck_ErrorCode_STORAGE_CORRUPTION;
    case kernel::ChainstateLoadStatus::FAILURE_INSUFFICIENT_DBCACHE:
        return btck_ErrorCode_RESOURCE_EXHAUSTION;
    case kernel::ChainstateLoadStatus::INTERRUPTED:
        return btck_ErrorCode_INTERRUPTED;
    }
    assert(false);
    return btck_ErrorCode_EXCEPTION;
}

btck_ErrorCode BlockImportErrorCode(kernel::BlockImportErrorKind kind) noexcept
{
    switch (kind) {
    case kernel::BlockImportErrorKind::IO:
    case kernel::BlockImportErrorKind::Scanner:
    case kernel::BlockImportErrorKind::Read:
        return btck_ErrorCode_IO_READ;
    case kernel::BlockImportErrorKind::Flush:
        return btck_ErrorCode_IO_WRITE;
    case kernel::BlockImportErrorKind::Admission:
    case kernel::BlockImportErrorKind::Activation:
    case kernel::BlockImportErrorKind::Chainstate:
        return btck_ErrorCode_CHAINSTATE_LOAD;
    }
    assert(false);
    return btck_ErrorCode_EXCEPTION;
}

btck_ErrorCode ValidationOperationErrorCode(bitcoin::operation_error_code code) noexcept
{
    switch (code) {
    case bitcoin::operation_error_code::resource_exhaustion:
        return btck_ErrorCode_RESOURCE_EXHAUSTION;
    case bitcoin::operation_error_code::data_unavailable:
        return btck_ErrorCode_DATA_UNAVAILABLE;
    case bitcoin::operation_error_code::io_read:
    case bitcoin::operation_error_code::seek:
        return btck_ErrorCode_IO_READ;
    case bitcoin::operation_error_code::io_write:
        return btck_ErrorCode_IO_WRITE;
    case bitcoin::operation_error_code::storage_corruption:
    case bitcoin::operation_error_code::malformed_stored_data:
        return btck_ErrorCode_STORAGE_CORRUPTION;
    case bitcoin::operation_error_code::interruption:
        return btck_ErrorCode_INTERRUPTED;
    case bitcoin::operation_error_code::callback_failure:
        return btck_ErrorCode_CALLBACK;
    case bitcoin::operation_error_code::unsupported_operation:
        return btck_ErrorCode_INVALID_ARGUMENT;
    case bitcoin::operation_error_code::invariant_violation:
    case bitcoin::operation_error_code::internal_bug:
        return btck_ErrorCode_EXCEPTION;
    }
    return btck_ErrorCode_EXCEPTION;
}

void SetValidationOperationError(btck_Error** error, const bitcoin::operation_error& operation_error) noexcept
{
    SetError(error, ValidationOperationErrorCode(operation_error.code()), operation_error.reason());
}

bitcoin::coin ToValidationCoin(const CoinValue& value)
{
    if (value.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        throw InvalidArgumentFailure{"coin height exceeds validation library range"};
    }
    return bitcoin::coin{
        value.output,
        bitcoin::block_height{static_cast<int32_t>(value.height)},
        value.coinbase,
        value.previous_median_time_past};
}

bitcoin::consensus_params ToValidationConsensusParams(const Consensus::Params& params) noexcept
{
    std::array<std::byte, 32> pow_limit{};
    std::ranges::transform(
        std::span<const unsigned char>{params.powLimit.data(), uint256::size()},
        pow_limit.begin(),
        [](unsigned char byte) { return static_cast<std::byte>(byte); });
    return bitcoin::consensus_params{
        .pow_limit = bitcoin::proof_of_work_limit{pow_limit},
        .target_spacing = std::chrono::seconds{params.nPowTargetSpacing},
        .target_timespan = std::chrono::seconds{params.nPowTargetTimespan},
        .max_timewarp = std::chrono::seconds{600},
        .allow_min_difficulty_blocks = params.fPowAllowMinDifficultyBlocks,
        .no_retargeting = params.fPowNoRetargeting,
        .enforce_bip94 = params.enforce_BIP94,
        .minimum_block_version = 0};
}

class CApiCoinIndex
{
public:
    explicit CApiCoinIndex(btck_ValidationCoinIndex callbacks) : m_callbacks{callbacks}
    {
        if (m_callbacks.lookup == nullptr) {
            throw InvalidArgumentFailure{"coin lookup callback is null"};
        }
    }

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        btck_CoinLookupResult result{
            .status = btck_CoinLookupStatus_MISSING,
            .coin = nullptr};
        const btck_TransactionOutPoint* out_point{btck_TransactionOutPoint::ref(&point)};
        if (m_callbacks.lookup(m_callbacks.user_data, out_point, &result) != 0) {
            throw CallbackFailure{"coin lookup callback returned failure"};
        }

        switch (result.status) {
        case btck_CoinLookupStatus_FOUND:
            if (result.coin == nullptr) {
                throw CallbackFailure{"coin lookup returned FOUND with null coin"};
            }
            return bitcoin::coin_lookup_result::found(ToValidationCoin(btck_Coin::get(result.coin)));
        case btck_CoinLookupStatus_MISSING:
            return bitcoin::coin_lookup_result::missing();
        case btck_CoinLookupStatus_SPENT:
            return bitcoin::coin_lookup_result::spent();
        case btck_CoinLookupStatus_UNAVAILABLE:
            return bitcoin::coin_lookup_result::unavailable("coin lookup data is unavailable");
        case btck_CoinLookupStatus_MALFORMED_STORED_DATA:
            return bitcoin::coin_lookup_result::malformed_stored_data("stored coin data is malformed");
        case btck_CoinLookupStatus_INTERRUPTED:
            return bitcoin::coin_lookup_result::interrupted("coin lookup interrupted");
        case btck_CoinLookupStatus_IO_FAILURE:
            return bitcoin::coin_lookup_result::io_failure("coin lookup read failed");
        }
        throw CallbackFailure{"coin lookup returned status outside the API domain"};
    }

private:
    btck_ValidationCoinIndex m_callbacks;
};

bitcoin::block_validation_context ToValidationBlockContext(
    const btck_BlockValidationLibraryOptions& options,
    bitcoin::consensus_params consensus)
{
    if ((options.script_flags & ~btck_ValidationScriptFlags_ALL) != 0) {
        throw InvalidArgumentFailure{"validation script flags contain unknown bits"};
    }
    if (options.subsidy < 0) {
        throw InvalidArgumentFailure{"block subsidy must be non-negative"};
    }
    if (options.coinbase_maturity < 0) {
        throw InvalidArgumentFailure{"coinbase maturity must be non-negative"};
    }
    if (options.max_sigop_cost > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw InvalidArgumentFailure{"maximum sigop cost is outside the API domain"};
    }

    const auto max_sigop_cost{
        options.max_sigop_cost == 0 ?
            std::size_t{80'000} :
            static_cast<std::size_t>(options.max_sigop_cost)};
    auto validation_script_flags{bitcoin::verification_flags::none()};
    if ((options.script_flags & btck_ValidationScriptFlags_P2SH) != 0) {
        validation_script_flags = validation_script_flags | bitcoin::verification_flags::p2sh();
    }
    if ((options.script_flags & btck_ValidationScriptFlags_WITNESS) != 0) {
        validation_script_flags = validation_script_flags | bitcoin::verification_flags::witness();
    }

    return bitcoin::block_validation_context{
        .consensus = std::move(consensus),
        .limits = {},
        .deployments = bitcoin::block_deployments{
            .segwit_active = options.segwit_active != 0,
            .height_in_coinbase_active = options.height_in_coinbase_active != 0},
        .spend_deployments = bitcoin::deployment_state{.enforce_bip30 = options.enforce_bip30 != 0},
        .locktime = bitcoin::locktime_flags::sequence(),
        .scripts = bitcoin::script_context{.flags = validation_script_flags},
        .subsidy = bitcoin::amount{options.subsidy},
        .coinbase_maturity = options.coinbase_maturity,
        .max_sigop_cost = max_sigop_cost};
}

template <typename F>
auto KernelTryPointer(const char* operation, btck_Error** error, F&& fn) -> decltype(std::forward<F>(fn)())
{
    ClearError(error);
    try {
        return std::forward<F>(fn)();
    } catch (const std::bad_alloc& e) {
        SetOperationError(error, btck_ErrorCode_RESOURCE_EXHAUSTION, operation, e.what());
    } catch (const CallbackFailure& e) {
        LogError("%s failed: %s", operation, e.what());
        SetOperationError(error, btck_ErrorCode_CALLBACK, operation, e.what());
    } catch (const InvalidArgumentFailure& e) {
        SetOperationError(error, btck_ErrorCode_INVALID_ARGUMENT, operation, e.what());
    } catch (const fs::filesystem_error& e) {
        SetOperationError(error, btck_ErrorCode_IO, operation, e.what());
    } catch (const std::exception& e) {
        SetExceptionError(error, operation, e);
    } catch (...) {
        SetUnknownExceptionError(error, operation);
    }
    return nullptr;
}

template <typename T, typename F>
T KernelTryValue(const char* operation, btck_Error** error, T failure, F&& fn)
{
    ClearError(error);
    try {
        return std::forward<F>(fn)();
    } catch (const std::bad_alloc& e) {
        SetOperationError(error, btck_ErrorCode_RESOURCE_EXHAUSTION, operation, e.what());
    } catch (const CallbackFailure& e) {
        LogError("%s failed: %s", operation, e.what());
        SetOperationError(error, btck_ErrorCode_CALLBACK, operation, e.what());
    } catch (const InvalidArgumentFailure& e) {
        SetOperationError(error, btck_ErrorCode_INVALID_ARGUMENT, operation, e.what());
    } catch (const fs::filesystem_error& e) {
        SetOperationError(error, btck_ErrorCode_IO, operation, e.what());
    } catch (const std::exception& e) {
        SetExceptionError(error, operation, e);
    } catch (...) {
        SetUnknownExceptionError(error, operation);
    }
    return failure;
}

template <typename F>
int KernelTryInt(const char* operation, btck_Error** error, int failure, F&& fn)
{
    return KernelTryValue(operation, error, failure, std::forward<F>(fn));
}

BlockValidationTime TimeFromOptions(const btck_BlockValidationOptions* options)
{
    const BlockValidationOptions& opts{btck_BlockValidationOptions::get(options)};
    if (!opts.m_time) {
        throw InvalidArgumentFailure{"block validation current time not set"};
    }
    return *opts.m_time;
}

NodeSeconds TimeFromOptions(const btck_ChainstateRuntime* options)
{
    const ChainstateRuntime& opts{btck_ChainstateRuntime::get(options)};
    if (!opts.m_current_time) {
        throw InvalidArgumentFailure{"chainstate current time not set"};
    }
    return *opts.m_current_time;
}

btck_BlockProcessStatus BlockProcessStatusFor(const NewBlockProcessingResult& result)
{
    if (result.status() == NewBlockProcessingStatus::BlockCheckFailed) {
        return btck_BlockProcessStatus_CHECK_FAILED;
    }
    switch (result.block_acceptance_status()) {
    case BlockAcceptanceStatus::HeaderRejected:
        return btck_BlockProcessStatus_HEADER_REJECTED;
    case BlockAcceptanceStatus::BlockRejected:
        return btck_BlockProcessStatus_BLOCK_REJECTED;
    case BlockAcceptanceStatus::BlockDataAlreadyKnown:
        return btck_BlockProcessStatus_ALREADY_KNOWN;
    case BlockAcceptanceStatus::BlockDataStored:
        return btck_BlockProcessStatus_STORED;
    case BlockAcceptanceStatus::BlockDataUnrequestedPreviouslyProcessed:
        return btck_BlockProcessStatus_UNREQUESTED_PREVIOUSLY_PROCESSED;
    case BlockAcceptanceStatus::BlockDataUnrequestedLessWorkThanTip:
        return btck_BlockProcessStatus_UNREQUESTED_LESS_WORK_THAN_TIP;
    case BlockAcceptanceStatus::BlockDataUnrequestedTooFarAhead:
        return btck_BlockProcessStatus_UNREQUESTED_TOO_FAR_AHEAD;
    case BlockAcceptanceStatus::BlockDataUnrequestedBelowMinimumChainWork:
        return btck_BlockProcessStatus_UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK;
    case BlockAcceptanceStatus::StorageFailed:
        break;
    }
    assert(false);
    return btck_BlockProcessStatus_BLOCK_REJECTED;
}

std::optional<BlockProcessResultValue> ProcessBlockWithTime(
    btck_Chainstate* chainman,
    const btck_Block* block,
    BlockValidationTime time,
    btck_Error** error)
{
    auto core_block{std::make_shared<const CBlock>(bitcoin::core_adapter::to_core_block(btck_Block::get(block)))};
    const NewBlockProcessingResult result{ProcessNewBlock({
        .chainman = *btck_Chainstate::get(chainman).m_chainman,
        .block = core_block,
        .options = {.block_data_storage = BlockDataStorageMode::ForceStore, .header = {.min_pow_checked = true}},
        .time = time,
    })};
    if (!result.Processed()) {
        if (result.block_acceptance_status() == BlockAcceptanceStatus::StorageFailed) {
            SetError(error, btck_ErrorCode_IO_WRITE, "failed to store block data");
            return std::nullopt;
        } else if (result.status() == NewBlockProcessingStatus::ActivationFailed) {
            SetError(error, btck_ErrorCode_CHAINSTATE_LOAD, "failed to activate chain after processing block");
            return std::nullopt;
        }
    }

    return BlockProcessResultValue{
        .status = BlockProcessStatusFor(result),
        .has_new_block_data = result.HasNewStoredBlockData(),
        .validation_state = result.validation_state(),
    };
}

HeaderProcessResultValue ProcessBlockHeaderWithTime(
    btck_Chainstate* chainstate_manager,
    const btck_BlockHeader* header,
    BlockValidationTime time)
{
    auto& chainman = btck_Chainstate::get(chainstate_manager).m_chainman;
    const CBlockHeader core_header{bitcoin::core_adapter::to_core_block_header(btck_BlockHeader::get(header))};

    BlockValidationState state;
    const NewBlockHeadersResult result{ProcessNewBlockHeaders({
        .chainman = *chainman,
        .headers = {&core_header, 1},
        .options = {.min_pow_checked = true},
        .time = time,
        .state = state,
    })};
    assert(result.accepted == state.IsValid());
    return HeaderProcessResultValue{
        .status = result.accepted ? btck_HeaderProcessStatus_ACCEPTED : btck_HeaderProcessStatus_REJECTED,
        .validation_state = std::move(state),
    };
}

} // namespace

btck_ErrorCode btck_error_get_code(const btck_Error* error)
{
    return KernelTryValue<btck_ErrorCode>(__func__, btck_ErrorCode_EXCEPTION, [&] { return btck_Error::get(error).code; });
}

const char* btck_error_get_message(const btck_Error* error, size_t* message_len)
{
    return KernelTryPointer(__func__, [&]() -> const char* {
        const std::string& message{btck_Error::get(error).message};
        if (message_len) *message_len = message.size();
        return message.c_str();
    });
}

void btck_error_destroy(btck_Error* error)
{
    delete error;
}

btck_TransactionParseResult* btck_transaction_parse_result(const void* raw_transaction, size_t raw_transaction_len, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_TransactionParseResult* {
        if (raw_transaction == nullptr && raw_transaction_len != 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "raw transaction bytes are null but length is non-zero");
            return nullptr;
        }
        const auto parsed{bitcoin::parse_transaction(std::span{reinterpret_cast<const std::byte*>(raw_transaction), raw_transaction_len})};
        if (!parsed) {
            return btck_TransactionParseResult::create(MalformedParseResult<TransactionParseResultValue>(parsed.assume_failure()));
        }
        return btck_TransactionParseResult::create(TransactionParseResultValue{
            .status = btck_ParseStatus_OK,
            .transaction = std::make_shared<const bitcoin::transaction>(parsed.assume_value()),
        });
    });
}

btck_ParseStatus btck_transaction_parse_result_get_status(const btck_TransactionParseResult* result)
{
    return KernelTryValue<btck_ParseStatus>(__func__, btck_ParseStatus_MALFORMED, [&] { return btck_TransactionParseResult::get(result).status; });
}

btck_ParseFailureCode btck_transaction_parse_result_get_failure_code(const btck_TransactionParseResult* result)
{
    return KernelTryValue<btck_ParseFailureCode>(__func__, btck_ParseFailureCode_NONE, [&] {
        return btck_TransactionParseResult::get(result).failure_code;
    });
}

size_t btck_transaction_parse_result_get_failure_offset(const btck_TransactionParseResult* result)
{
    return KernelTryValue<size_t>(__func__, 0, [&] {
        return btck_TransactionParseResult::get(result).failure_offset;
    });
}

const btck_Transaction* btck_transaction_parse_result_get_transaction(const btck_TransactionParseResult* result)
{
    return KernelTryPointer(__func__, [&]() -> const btck_Transaction* {
        const auto& value{btck_TransactionParseResult::get(result)};
        if (value.status != btck_ParseStatus_OK || !value.transaction) return nullptr;
        return btck_Transaction::ref(value.transaction.get());
    });
}

void btck_transaction_parse_result_destroy(btck_TransactionParseResult* result)
{
    delete result;
}

size_t btck_transaction_count_outputs(const btck_Transaction* transaction)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_Transaction::get(transaction).outputs().size(); });
}

const btck_TransactionOutput* btck_transaction_get_output_at(const btck_Transaction* transaction, size_t output_index)
{
    return KernelTryPointer(__func__, [&] {
        const auto outputs{btck_Transaction::get(transaction).outputs()};
        if (output_index >= outputs.size()) {
            LogError("Transaction output index is out of range.");
            return static_cast<const btck_TransactionOutput*>(nullptr);
        }
        return btck_TransactionOutput::ref(&outputs[output_index]);
    });
}

size_t btck_transaction_count_inputs(const btck_Transaction* transaction)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_Transaction::get(transaction).inputs().size(); });
}

const btck_TransactionInput* btck_transaction_get_input_at(const btck_Transaction* transaction, size_t input_index)
{
    return KernelTryPointer(__func__, [&] {
        const auto inputs{btck_Transaction::get(transaction).inputs()};
        if (input_index >= inputs.size()) {
            LogError("Transaction input index is out of range.");
            return static_cast<const btck_TransactionInput*>(nullptr);
        }
        return btck_TransactionInput::ref(&inputs[input_index]);
    });
}

uint32_t btck_transaction_get_locktime(const btck_Transaction* transaction)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_Transaction::get(transaction).locktime(); });
}

btck_Txid* btck_transaction_get_txid(const btck_Transaction* transaction, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Txid::create(btck_Transaction::get(transaction).id()); });
}

btck_Transaction* btck_transaction_copy(const btck_Transaction* transaction, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Transaction::copy(transaction); });
}

int btck_transaction_serialize(const btck_Transaction* transaction, btck_WriteBytes writer, void* user_data, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        CallbackByteSink sink{writer, user_data};
        bitcoin::serialize(btck_Transaction::get(transaction), bitcoin::byte_sink_ref{sink});
        return 0;
    });
}

void btck_transaction_destroy(btck_Transaction* transaction)
{
    delete transaction;
}

btck_ScriptPubkey* btck_script_pubkey_create(const void* script_pubkey, size_t script_pubkey_len, btck_Error** error)
{
    ClearError(error);
    if (script_pubkey == nullptr && script_pubkey_len != 0) {
        SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "script pubkey bytes are null but length is non-zero");
        return nullptr;
    }
    return KernelTryPointer(__func__, error, [&] {
        auto data = std::span{reinterpret_cast<const std::byte*>(script_pubkey), script_pubkey_len};
        return btck_ScriptPubkey::create(data);
    });
}

int btck_script_pubkey_to_bytes(const btck_ScriptPubkey* script_pubkey_, btck_WriteBytes writer, void* user_data, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        const auto& script_pubkey{btck_ScriptPubkey::get(script_pubkey_)};
        const auto bytes{as_bytes(script_pubkey)};
        if (writer(bytes.data(), bytes.size(), user_data) != 0) {
            throw CallbackWriteFailure{"failed to write script pubkey bytes"};
        }
        return 0;
    });
}

btck_ScriptPubkey* btck_script_pubkey_copy(const btck_ScriptPubkey* script_pubkey, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_ScriptPubkey::copy(script_pubkey); });
}

void btck_script_pubkey_destroy(btck_ScriptPubkey* script_pubkey)
{
    delete script_pubkey;
}

btck_TransactionOutput* btck_transaction_output_create(const btck_ScriptPubkey* script_pubkey, int64_t amount, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        return btck_TransactionOutput::create(bitcoin::amount{amount}, btck_ScriptPubkey::get(script_pubkey));
    });
}

btck_TransactionOutput* btck_transaction_output_copy(const btck_TransactionOutput* output, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TransactionOutput::copy(output); });
}

btck_ScriptPubkey* btck_transaction_output_get_script_pubkey(const btck_TransactionOutput* output, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        return btck_ScriptPubkey::create(btck_TransactionOutput::get(output).script());
    });
}

int64_t btck_transaction_output_get_amount(const btck_TransactionOutput* output)
{
    return KernelTryValue<int64_t>(__func__, 0, [&] { return btck_TransactionOutput::get(output).value().satoshis(); });
}

void btck_transaction_output_destroy(btck_TransactionOutput* output)
{
    delete output;
}

btck_PrecomputedTransactionData* btck_precomputed_transaction_data_create(
    const btck_Transaction* tx_to,
    const btck_TransactionOutput** spent_outputs_, size_t spent_outputs_len,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        const CTransaction tx{bitcoin::core_adapter::to_core_transaction(btck_Transaction::get(tx_to))};
        auto txdata{btck_PrecomputedTransactionData::create()};
        if (spent_outputs_ != nullptr && spent_outputs_len > 0) {
            if (spent_outputs_len != btck_Transaction::get(tx_to).inputs().size()) {
                SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "spent outputs length does not match transaction input count");
                return static_cast<btck_PrecomputedTransactionData*>(nullptr);
            }
            std::vector<CTxOut> spent_outputs;
            spent_outputs.reserve(spent_outputs_len);
            for (size_t i = 0; i < spent_outputs_len; i++) {
                spent_outputs.push_back(bitcoin::core_adapter::to_core_tx_output(btck_TransactionOutput::get(spent_outputs_[i])));
            }
            btck_PrecomputedTransactionData::get(txdata).Init(tx, std::move(spent_outputs));
        } else {
            btck_PrecomputedTransactionData::get(txdata).Init(tx, {});
        }

        return txdata;
    });
}

btck_PrecomputedTransactionData* btck_precomputed_transaction_data_copy(const btck_PrecomputedTransactionData* precomputed_txdata, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_PrecomputedTransactionData::copy(precomputed_txdata); });
}

void btck_precomputed_transaction_data_destroy(btck_PrecomputedTransactionData* precomputed_txdata)
{
    delete precomputed_txdata;
}

int btck_script_pubkey_verify(const btck_ScriptPubkey* script_pubkey,
                              const int64_t amount,
                              const btck_Transaction* tx_to,
                              const btck_PrecomputedTransactionData* precomputed_txdata,
                              const unsigned int input_index,
                              const btck_ScriptVerificationFlags flags,
                              btck_ScriptVerifyStatus* status,
                              btck_Error** error)
{
    if (status) *status = btck_ScriptVerifyStatus_ERROR_INTERNAL;
    return KernelTryInt(__func__, error, 0, [&] {
        if ((flags & ~btck_ScriptVerificationFlags_ALL) != 0) {
            if (status) *status = btck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION;
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "script verification flags contain unknown bits");
            return 0;
        }

        if (!is_valid_flag_combination(script_verify_flags::from_int(flags))) {
            if (status) *status = btck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION;
            return 0;
        }

        const auto& transaction{btck_Transaction::get(tx_to)};
        if (input_index >= transaction.inputs().size()) {
            LogError("Transaction input index is out of range.");
            return 0;
        }
        const CTransaction tx{bitcoin::core_adapter::to_core_transaction(transaction)};
        const CScript script_pubkey_core{bitcoin::core_adapter::to_core_script(bitcoin::script_ref{btck_ScriptPubkey::get(script_pubkey)})};

        const PrecomputedTransactionData& txdata{precomputed_txdata ? btck_PrecomputedTransactionData::get(precomputed_txdata) : PrecomputedTransactionData(tx)};

        if (flags & btck_ScriptVerificationFlags_TAPROOT && txdata.m_spent_outputs.empty()) {
            if (status) *status = btck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED;
            return 0;
        }

        bool result = VerifyScript(tx.vin[input_index].scriptSig,
                                   script_pubkey_core,
                                   &tx.vin[input_index].scriptWitness,
                                   script_verify_flags::from_int(flags),
                                   TransactionSignatureChecker(&tx, input_index, amount, txdata, MissingDataBehavior::FAIL),
                                   nullptr);
        if (status) *status = btck_ScriptVerifyStatus_OK;
        return result ? 1 : 0;
    });
}

btck_TransactionInput* btck_transaction_input_copy(const btck_TransactionInput* input, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TransactionInput::copy(input); });
}

btck_TransactionOutPoint* btck_transaction_input_get_out_point(const btck_TransactionInput* input, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TransactionOutPoint::create(btck_TransactionInput::get(input).previous_output()); });
}

uint32_t btck_transaction_input_get_sequence(const btck_TransactionInput* input)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_TransactionInput::get(input).sequence(); });
}

void btck_transaction_input_destroy(btck_TransactionInput* input)
{
    delete input;
}

btck_TransactionOutPoint* btck_transaction_out_point_copy(const btck_TransactionOutPoint* out_point, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TransactionOutPoint::copy(out_point); });
}

uint32_t btck_transaction_out_point_get_index(const btck_TransactionOutPoint* out_point)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_TransactionOutPoint::get(out_point).index().value(); });
}

btck_Txid* btck_transaction_out_point_get_txid(const btck_TransactionOutPoint* out_point, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Txid::create(btck_TransactionOutPoint::get(out_point).txid()); });
}

void btck_transaction_out_point_destroy(btck_TransactionOutPoint* out_point)
{
    delete out_point;
}

btck_Txid* btck_txid_copy(const btck_Txid* txid, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Txid::copy(txid); });
}

void btck_txid_to_bytes(const btck_Txid* txid, unsigned char output[32])
{
    KernelTryVoid(__func__, [&] { CopyHashBytes(btck_Txid::get(txid), output); });
}

int btck_txid_equals(const btck_Txid* txid1, const btck_Txid* txid2)
{
    return KernelTryInt(__func__, 0, [&] { return btck_Txid::get(txid1) == btck_Txid::get(txid2); });
}

void btck_txid_destroy(btck_Txid* txid)
{
    delete txid;
}

void btck_logging_set_options(const btck_LoggingOptions options)
{
    KernelTryVoid(__func__, [&] {
        LOCK(cs_main);
        LogInstance().m_log_timestamps = options.log_timestamps;
        LogInstance().m_log_time_micros = options.log_time_micros;
        LogInstance().m_log_threadnames = options.log_threadnames;
        LogInstance().m_log_sourcelocations = options.log_sourcelocations;
        LogInstance().m_always_print_category_level = options.always_print_category_levels;
    });
}

void btck_logging_set_level_category(btck_LogCategory category, btck_LogLevel level)
{
    KernelTryVoid(__func__, [&] {
        LOCK(cs_main);
        if (category == btck_LogCategory_ALL) {
            LogInstance().SetLogLevel(get_bclog_level(level));
        }

        LogInstance().AddCategoryLogLevel(get_bclog_flag(category), get_bclog_level(level));
    });
}

void btck_logging_enable_category(btck_LogCategory category)
{
    KernelTryVoid(__func__, [&] { LogInstance().EnableCategory(get_bclog_flag(category)); });
}

void btck_logging_disable_category(btck_LogCategory category)
{
    KernelTryVoid(__func__, [&] { LogInstance().DisableCategory(get_bclog_flag(category)); });
}

void btck_logging_disable()
{
    KernelTryVoid(__func__, [&] { LogInstance().DisableLogging(); });
}

btck_LoggingConnection* btck_logging_connection_create(btck_LogCallback callback, void* user_data, btck_DestroyCallback user_data_destroy_callback, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_LoggingConnection::create(callback, user_data, user_data_destroy_callback); });
}

void btck_logging_connection_destroy(btck_LoggingConnection* connection)
{
    delete connection;
}

btck_ChainParameters* btck_chain_parameters_create(const btck_ChainType chain_type, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        switch (chain_type) {
        case btck_ChainType_MAINNET: {
            return btck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::Main().release()));
        }
        case btck_ChainType_TESTNET: {
            return btck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet().release()));
        }
        case btck_ChainType_TESTNET_4: {
            return btck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet4().release()));
        }
        case btck_ChainType_SIGNET: {
            return btck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::SigNet({}).release()));
        }
        case btck_ChainType_REGTEST: {
            return btck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::RegTest({}).release()));
        }
        }
        SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "chain type is outside the API domain");
        return static_cast<btck_ChainParameters*>(nullptr);
    });
}

btck_ChainParameters* btck_chain_parameters_copy(const btck_ChainParameters* chain_parameters, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_ChainParameters::copy(chain_parameters); });
}

const btck_ConsensusParams* btck_chain_parameters_get_consensus_params(const btck_ChainParameters* chain_parameters)
{
    return KernelTryPointer(__func__, [&] { return btck_ConsensusParams::ref(&btck_ChainParameters::get(chain_parameters).GetConsensus()); });
}

void btck_chain_parameters_destroy(btck_ChainParameters* chain_parameters)
{
    delete chain_parameters;
}

btck_ContextOptions* btck_context_options_create(btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_ContextOptions::create(); });
}

int btck_context_options_set_chainparams(btck_ContextOptions* options, const btck_ChainParameters* chain_parameters, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        // Copy the chainparams, so the caller can free it again
        auto chainparams{std::make_unique<const CChainParams>(btck_ChainParameters::get(chain_parameters))};
        LOCK(btck_ContextOptions::get(options).m_mutex);
        btck_ContextOptions::get(options).m_chainparams = std::move(chainparams);
        return 0;
    });
}

int btck_context_options_set_notifications(btck_ContextOptions* options, btck_NotificationInterfaceCallbacks notifications, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        // The KernelNotifications are copy-initialized, so the caller can free them again.
        auto notifications_interface{std::make_shared<KernelNotifications>(notifications)};
        LOCK(btck_ContextOptions::get(options).m_mutex);
        btck_ContextOptions::get(options).m_notifications = std::move(notifications_interface);
        return 0;
    });
}

int btck_context_options_set_validation_interface(btck_ContextOptions* options, btck_ValidationInterfaceCallbacks vi_cbs, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        auto validation_interface{std::make_shared<KernelValidationInterface>(vi_cbs)};
        LOCK(btck_ContextOptions::get(options).m_mutex);
        btck_ContextOptions::get(options).m_validation_interface = std::move(validation_interface);
        return 0;
    });
}

void btck_context_options_destroy(btck_ContextOptions* options)
{
    delete options;
}

btck_Context* btck_context_create(const btck_ContextOptions* options, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        bool sane{true};
        const ContextOptions* opts = options ? &btck_ContextOptions::get(options) : nullptr;
        auto context{std::make_shared<const Context>(opts, sane)};
        if (!sane) {
            LogError("Kernel context sanity check failed.");
            SetError(error, btck_ErrorCode_EXCEPTION, "kernel context sanity check failed");
            return static_cast<btck_Context*>(nullptr);
        }
        return btck_Context::create(context);
    });
}

btck_Context* btck_context_copy(const btck_Context* context, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Context::copy(context); });
}

int btck_context_interrupt(btck_Context* context, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        if ((*btck_Context::get(context)->m_interrupt)()) {
            return 0;
        }
        SetError(error, btck_ErrorCode_IO_WRITE, "failed to signal context interrupt");
        return -1;
    });
}

void btck_context_destroy(btck_Context* context)
{
    delete context;
}

btck_BlockInfo* btck_block_info_copy(const btck_BlockInfo* block_info, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockInfo::copy(block_info); });
}

int32_t btck_block_info_get_height(const btck_BlockInfo* block_info)
{
    return KernelTryValue<int32_t>(__func__, -1, [&] { return btck_BlockInfo::get(block_info).height; });
}

const btck_BlockHash* btck_block_info_get_block_hash(const btck_BlockInfo* block_info)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockHash::ref(&btck_BlockInfo::get(block_info).hash); });
}

const btck_BlockHash* btck_block_info_get_previous_block_hash(const btck_BlockInfo* block_info)
{
    return KernelTryPointer(__func__, [&]() -> const btck_BlockHash* {
        const auto& previous_hash{btck_BlockInfo::get(block_info).previous_hash};
        if (!previous_hash) {
            return nullptr;
        }
        return btck_BlockHash::ref(&*previous_hash);
    });
}

const btck_BlockHeader* btck_block_info_get_header(const btck_BlockInfo* block_info)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockHeader::ref(&btck_BlockInfo::get(block_info).header); });
}

int btck_block_info_equals(const btck_BlockInfo* info1, const btck_BlockInfo* info2)
{
    return KernelTryInt(__func__, 0, [&] { return btck_BlockInfo::get(info1).hash == btck_BlockInfo::get(info2).hash ? 1 : 0; });
}

void btck_block_info_destroy(btck_BlockInfo* block_info)
{
    delete block_info;
}

btck_BlockValidationState* btck_block_validation_state_create(btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockValidationState::create(); });
}

btck_BlockValidationState* btck_block_validation_state_copy(const btck_BlockValidationState* state, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockValidationState::copy(state); });
}

void btck_block_validation_state_destroy(btck_BlockValidationState* state)
{
    delete state;
}

btck_ValidationMode btck_block_validation_state_get_validation_mode(const btck_BlockValidationState* block_validation_state_)
{
    return KernelTryValue<btck_ValidationMode>(__func__, btck_ValidationMode_INTERNAL_ERROR, [&] {
        auto& block_validation_state = btck_BlockValidationState::get(block_validation_state_);
        if (block_validation_state.IsValid()) return btck_ValidationMode_VALID;
        if (block_validation_state.IsInvalid()) return btck_ValidationMode_INVALID;
        return btck_ValidationMode_INTERNAL_ERROR;
    });
}

btck_BlockValidationResult btck_block_validation_state_get_block_validation_result(const btck_BlockValidationState* block_validation_state_)
{
    return KernelTryValue<btck_BlockValidationResult>(__func__, btck_BlockValidationResult_UNSET, [&] {
        auto& block_validation_state = btck_BlockValidationState::get(block_validation_state_);
        switch (block_validation_state.GetResult()) {
        case BlockValidationResult::BLOCK_RESULT_UNSET:
            return btck_BlockValidationResult_UNSET;
        case BlockValidationResult::BLOCK_CONSENSUS:
            return btck_BlockValidationResult_CONSENSUS;
        case BlockValidationResult::BLOCK_CACHED_INVALID:
            return btck_BlockValidationResult_CACHED_INVALID;
        case BlockValidationResult::BLOCK_INVALID_HEADER:
            return btck_BlockValidationResult_INVALID_HEADER;
        case BlockValidationResult::BLOCK_MUTATED:
            return btck_BlockValidationResult_MUTATED;
        case BlockValidationResult::BLOCK_MISSING_PREV:
            return btck_BlockValidationResult_MISSING_PREV;
        case BlockValidationResult::BLOCK_INVALID_PREV:
            return btck_BlockValidationResult_INVALID_PREV;
        case BlockValidationResult::BLOCK_TIME_FUTURE:
            return btck_BlockValidationResult_TIME_FUTURE;
        case BlockValidationResult::BLOCK_HEADER_LOW_WORK:
            return btck_BlockValidationResult_HEADER_LOW_WORK;
        } // no default case, so the compiler can warn about missing cases
        assert(false);
        return btck_BlockValidationResult_UNSET;
    });
}

btck_ChainstateOptions* btck_chainstate_options_create(const btck_Context* context, const char* data_dir, size_t data_dir_len, const char* blocks_dir, size_t blocks_dir_len, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        if (data_dir == nullptr || data_dir_len == 0 || blocks_dir == nullptr || blocks_dir_len == 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "chainstate data and blocks directories must be non-null and non-empty");
            return static_cast<btck_ChainstateOptions*>(nullptr);
        }
        fs::path abs_data_dir{fs::absolute(fs::PathFromString({data_dir, data_dir_len}))};
        fs::create_directories(abs_data_dir);
        fs::path abs_blocks_dir{fs::absolute(fs::PathFromString({blocks_dir, blocks_dir_len}))};
        fs::create_directories(abs_blocks_dir);
        return btck_ChainstateOptions::create(btck_Context::get(context), abs_data_dir, abs_blocks_dir);
    });
}

int btck_chainstate_options_set_worker_threads_num(btck_ChainstateOptions* opts, int worker_threads, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        LOCK(btck_ChainstateOptions::get(opts).m_mutex);
        btck_ChainstateOptions::get(opts).m_chainman_options.worker_threads_num = worker_threads;
        return 0;
    });
}

void btck_chainstate_options_destroy(btck_ChainstateOptions* options)
{
    delete options;
}

btck_ChainstateRuntime* btck_chainstate_runtime_create(btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_ChainstateRuntime::create(); });
}

int btck_chainstate_runtime_set_current_time(btck_ChainstateRuntime* options, int64_t timestamp, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        const auto time{BlockValidationTime::FromUnixSeconds(timestamp)};
        if (!time) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "chainstate current time is outside the supported timestamp range");
            return -1;
        }
        btck_ChainstateRuntime::get(options).m_current_time = time->CurrentTime();
        return 0;
    });
}

void btck_chainstate_runtime_destroy(btck_ChainstateRuntime* options)
{
    delete options;
}

int btck_chainstate_options_set_wipe_state(btck_ChainstateOptions* chainman_opts, int reindex_block_files, int wipe_chainstate, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        if (reindex_block_files == 1 && wipe_chainstate != 1) {
            LogError("Reindexing block files without also wiping the chainstate is currently unsupported.");
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "reindexing block files requires wiping chainstate");
            return -1;
        }
        auto& opts{btck_ChainstateOptions::get(chainman_opts)};
        LOCK(opts.m_mutex);
        opts.m_blockman_options.block_tree_db_params.wipe_data = reindex_block_files == 1;
        opts.m_chainstate_load_options.wipe_chainstate_db = wipe_chainstate == 1;
        return 0;
    });
}

int btck_chainstate_options_set_in_memory(
    btck_ChainstateOptions* chainman_opts,
    int in_memory,
    btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        auto& opts{btck_ChainstateOptions::get(chainman_opts)};
        LOCK(opts.m_mutex);
        opts.m_blockman_options.block_tree_db_params.memory_only = in_memory == 1;
        opts.m_chainstate_load_options.coins_db_in_memory = in_memory == 1;
        return 0;
    });
}

btck_BlockValidationOptions* btck_block_validation_options_create(btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockValidationOptions::create(); });
}

int btck_block_validation_options_set_current_time(btck_BlockValidationOptions* options, int64_t timestamp, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        auto time{BlockValidationTime::FromUnixSeconds(timestamp)};
        if (!time) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "block validation current time is outside the supported timestamp range");
            return -1;
        }
        btck_BlockValidationOptions::get(options).m_time = *time;
        return 0;
    });
}

void btck_block_validation_options_destroy(btck_BlockValidationOptions* options)
{
    delete options;
}

btck_Chainstate* btck_chainstate_open(
    const btck_ChainstateOptions* chainman_opts,
    const btck_ChainstateRuntime* runtime_options,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        const NodeSeconds current_time{TimeFromOptions(runtime_options)};
        auto& opts{btck_ChainstateOptions::get(chainman_opts)};
        std::unique_ptr<ChainstateManager> chainman;

        {
            LOCK(opts.m_mutex);
            chainman = std::make_unique<ChainstateManager>(*opts.m_context->m_interrupt, opts.m_chainman_options, opts.m_blockman_options);
        }

        auto chainstate_load_opts{WITH_LOCK(opts.m_mutex, return opts.m_chainstate_load_options)};
        chainstate_load_opts.current_time = current_time;

        kernel::CacheSizes cache_sizes{DEFAULT_KERNEL_CACHE};
        auto [status, chainstate_err]{kernel::LoadChainstate(*chainman, cache_sizes, chainstate_load_opts)};
        if (status != kernel::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to load chain state from your data directory: %s", chainstate_err.original);
            SetError(error, ChainstateLoadErrorCode(status), strprintf("failed to load chainstate: %s", chainstate_err.original));
            return static_cast<btck_Chainstate*>(nullptr);
        }
        std::tie(status, chainstate_err) = kernel::VerifyLoadedChainstate(*chainman, chainstate_load_opts);
        if (status != kernel::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to verify loaded chain state from your datadir: %s", chainstate_err.original);
            SetError(error, ChainstateLoadErrorCode(status), strprintf("failed to verify loaded chainstate: %s", chainstate_err.original));
            return static_cast<btck_Chainstate*>(nullptr);
        }
        if (auto result = chainman->ActivateBestChains(current_time); !result) {
            LogError("%s", util::ErrorString(result).original);
            SetError(error, btck_ErrorCode_CHAINSTATE_LOAD, util::ErrorString(result).original);
            return static_cast<btck_Chainstate*>(nullptr);
        }

        return btck_Chainstate::create(std::move(chainman), opts.m_context);
    });
}

btck_BlockInfo* btck_chainstate_get_block_info(const btck_Chainstate* chainman, const btck_BlockHash* block_hash, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockInfo* {
        auto& chainman_ref{*btck_Chainstate::get(chainman).m_chainman};
        const auto info { WITH_LOCK(chainman_ref.GetMutex(),
                                    CoreBlockIndexStore block_index_store{chainman_ref};
                                    const CBlockIndex* block_index{block_index_store.LookupBlockIndex(bitcoin::core_adapter::to_uint256(btck_BlockHash::get(block_hash)))};
                                    if (!block_index) return std::optional<BlockInfoSnapshot>{};
                                    return std::optional<BlockInfoSnapshot>{SnapshotBlockInfo(*block_index)};) };
        if (!info) {
            LogDebug(BCLog::KERNEL, "A block with the given hash is not indexed.");
            return nullptr;
        }
        return btck_BlockInfo::create(*info);
    });
}

btck_BlockInfo* btck_chainstate_get_best_header_info(const btck_Chainstate* chainstate_manager, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockInfo* {
        auto& chainman = *btck_Chainstate::get(chainstate_manager).m_chainman;
        const auto info { WITH_LOCK(chainman.GetMutex(),
                                    const CBlockIndex* best_header{chainman.m_best_header};
                                    if (!best_header) return std::optional<BlockInfoSnapshot>{};
                                    return std::optional<BlockInfoSnapshot>{SnapshotBlockInfo(*best_header)};) };
        if (!info) return nullptr;
        return btck_BlockInfo::create(*info);
    });
}

void btck_chainstate_destroy(btck_Chainstate* chainman)
{
    KernelTryVoid(__func__, [&] {
        auto& cm{*btck_Chainstate::get(chainman).m_chainman};
        LOCK(cm.GetMutex());
        if (cm.m_chainstate && cm.m_chainstate->CanFlushToDisk()) {
            cm.m_chainstate->ForceFlushStateToDisk();
            cm.m_chainstate->ResetCoinsViews();
        }
    });

    delete chainman;
}

btck_BlockImportResult* btck_chainstate_import_blocks_result(
    btck_Chainstate* chainman,
    const char** block_file_paths_data,
    const size_t* block_file_paths_lens,
    size_t block_file_paths_data_len,
    const btck_ChainstateRuntime* runtime_options,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockImportResult* {
        if (block_file_paths_data_len != 0 && (!block_file_paths_data || !block_file_paths_lens)) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "block import path arrays must be non-null when path count is non-zero");
            return nullptr;
        }
        const NodeSeconds current_time{TimeFromOptions(runtime_options)};
        std::vector<fs::path> import_files;
        import_files.reserve(block_file_paths_data_len);
        for (size_t i{0}; i < block_file_paths_data_len; ++i) {
            if (block_file_paths_data[i] == nullptr || block_file_paths_lens[i] == 0) {
                SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "block import path entries must be non-null and non-empty");
                return nullptr;
            }
            import_files.emplace_back(fs::PathFromString({block_file_paths_data[i], block_file_paths_lens[i]}));
        }
        auto& chainman_ref{*btck_Chainstate::get(chainman).m_chainman};
        auto result{kernel::ImportBlocks(chainman_ref, import_files, current_time)};
        if (!result) {
            const kernel::BlockImportError& import_error{result.error()};
            SetError(error, BlockImportErrorCode(import_error.kind), import_error.message.original);
            return nullptr;
        }
        if (result->status != kernel::BlockImportStatus::Completed) {
            btck_BlockImportStatus status{btck_BlockImportStatus_RESOURCE_LIMIT};
            if (result->status == kernel::BlockImportStatus::Interrupted) status = btck_BlockImportStatus_INTERRUPTED;
            if (result->status == kernel::BlockImportStatus::AlreadyImporting) status = btck_BlockImportStatus_ALREADY_IMPORTING;
            return btck_BlockImportResult::create(BlockImportResultValue{
                .status = status,
                .loaded_blocks = result->counters.loaded_blocks,
                .skipped_records = result->counters.skipped_records,
                .skipped_blocks = result->counters.skipped_blocks,
                .rejected_blocks = result->counters.rejected_blocks,
            });
        }
        WITH_LOCK(::cs_main, chainman_ref.UpdateIBDStatus(current_time));
        return btck_BlockImportResult::create(BlockImportResultValue{
            .status = btck_BlockImportStatus_COMPLETED,
            .loaded_blocks = result->counters.loaded_blocks,
            .skipped_records = result->counters.skipped_records,
            .skipped_blocks = result->counters.skipped_blocks,
            .rejected_blocks = result->counters.rejected_blocks,
        });
    });
}

btck_BlockImportStatus btck_block_import_result_get_status(const btck_BlockImportResult* result)
{
    return KernelTryValue<btck_BlockImportStatus>(__func__, btck_BlockImportStatus_COMPLETED, [&] { return btck_BlockImportResult::get(result).status; });
}

int btck_block_import_result_get_loaded_block_count(const btck_BlockImportResult* result)
{
    return KernelTryValue<int>(__func__, 0, [&] { return btck_BlockImportResult::get(result).loaded_blocks; });
}

int btck_block_import_result_get_skipped_record_count(const btck_BlockImportResult* result)
{
    return KernelTryValue<int>(__func__, 0, [&] { return btck_BlockImportResult::get(result).skipped_records; });
}

int btck_block_import_result_get_skipped_block_count(const btck_BlockImportResult* result)
{
    return KernelTryValue<int>(__func__, 0, [&] { return btck_BlockImportResult::get(result).skipped_blocks; });
}

int btck_block_import_result_get_rejected_block_count(const btck_BlockImportResult* result)
{
    return KernelTryValue<int>(__func__, 0, [&] { return btck_BlockImportResult::get(result).rejected_blocks; });
}

void btck_block_import_result_destroy(btck_BlockImportResult* result)
{
    delete result;
}

btck_BlockParseResult* btck_block_parse_result(const void* raw_block, size_t raw_block_length, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockParseResult* {
        if (raw_block == nullptr && raw_block_length != 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "raw block bytes are null but length is non-zero");
            return nullptr;
        }
        const auto parsed{bitcoin::parse_block(std::span{reinterpret_cast<const std::byte*>(raw_block), raw_block_length})};
        if (!parsed) {
            return btck_BlockParseResult::create(MalformedParseResult<BlockParseResultValue>(parsed.assume_failure()));
        }

        return btck_BlockParseResult::create(BlockParseResultValue{
            .status = btck_ParseStatus_OK,
            .block = std::make_shared<const bitcoin::block>(parsed.assume_value()),
        });
    });
}

btck_ParseStatus btck_block_parse_result_get_status(const btck_BlockParseResult* result)
{
    return KernelTryValue<btck_ParseStatus>(__func__, btck_ParseStatus_MALFORMED, [&] { return btck_BlockParseResult::get(result).status; });
}

btck_ParseFailureCode btck_block_parse_result_get_failure_code(const btck_BlockParseResult* result)
{
    return KernelTryValue<btck_ParseFailureCode>(__func__, btck_ParseFailureCode_NONE, [&] {
        return btck_BlockParseResult::get(result).failure_code;
    });
}

size_t btck_block_parse_result_get_failure_offset(const btck_BlockParseResult* result)
{
    return KernelTryValue<size_t>(__func__, 0, [&] {
        return btck_BlockParseResult::get(result).failure_offset;
    });
}

const btck_Block* btck_block_parse_result_get_block(const btck_BlockParseResult* result)
{
    return KernelTryPointer(__func__, [&]() -> const btck_Block* {
        const auto& value{btck_BlockParseResult::get(result)};
        if (value.status != btck_ParseStatus_OK || !value.block) return nullptr;
        return btck_Block::ref(value.block.get());
    });
}

void btck_block_parse_result_destroy(btck_BlockParseResult* result)
{
    delete result;
}

btck_Block* btck_block_copy(const btck_Block* block, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Block::copy(block); });
}

btck_BlockCheckResult* btck_block_check_result(const btck_Block* block, const btck_ConsensusParams* consensus_params, btck_BlockCheckFlags flags, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockCheckResult* {
        if ((flags & ~btck_BlockCheckFlags_ALL) != 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "block check flags contain unknown bits");
            return nullptr;
        }

        BlockValidationState state;
        if ((flags & btck_BlockCheckFlags_POW) != 0) {
            const CBlockHeader header{bitcoin::core_adapter::to_core_block_header(btck_Block::get(block).header())};
            if (!CheckProofOfWork(header.GetHash(), header.nBits, btck_ConsensusParams::get(consensus_params))) {
                state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "high-hash");
                return btck_BlockCheckResult::create(BlockCheckResultValue{
                    .status = btck_CheckStatus_INVALID,
                    .validation_state = std::move(state),
                });
            }
        }
        const auto result{bitcoin::assess_block_intrinsic(
            btck_Block::get(block),
            bitcoin::block_local_context{
                .limits = {},
                .check_merkle_root = (flags & btck_BlockCheckFlags_MERKLE) != 0})};
        if (result.has_error()) {
            SetValidationOperationError(error, result.assume_error());
            return nullptr;
        } else if (!result.assume_value().accepted()) {
            ApplyRejection(state, result.assume_value().assume_rejection());
        }

        return btck_BlockCheckResult::create(BlockCheckResultValue{
            .status = state.IsValid() ? btck_CheckStatus_VALID : btck_CheckStatus_INVALID,
            .validation_state = std::move(state),
        });
    });
}

btck_CheckStatus btck_block_check_result_get_status(const btck_BlockCheckResult* result)
{
    return KernelTryValue<btck_CheckStatus>(__func__, btck_CheckStatus_INVALID, [&] { return btck_BlockCheckResult::get(result).status; });
}

const btck_BlockValidationState* btck_block_check_result_get_validation_state(const btck_BlockCheckResult* result)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockValidationState::ref(&btck_BlockCheckResult::get(result).validation_state); });
}

void btck_block_check_result_destroy(btck_BlockCheckResult* result)
{
    delete result;
}

btck_BlockVerifyResult* btck_block_verify_result(
    const btck_Block* block,
    const btck_BlockHeader* const* ancestors,
    size_t ancestor_count,
    const btck_ConsensusParams* consensus_params,
    btck_BlockValidationLibraryOptions options,
    btck_ValidationCoinIndex coins,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockVerifyResult* {
        if (ancestors == nullptr && ancestor_count != 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "ancestor header array is null but count is non-zero");
            return nullptr;
        }

        std::vector<bitcoin::block_header> ancestor_headers;
        ancestor_headers.reserve(ancestor_count);
        for (size_t index{0}; index < ancestor_count; ++index) {
            if (ancestors[index] == nullptr) {
                SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "ancestor header array contains a null entry");
                return nullptr;
            }
            ancestor_headers.push_back(btck_BlockHeader::get(ancestors[index]));
        }

        const bitcoin::consensus_params consensus{ToValidationConsensusParams(btck_ConsensusParams::get(consensus_params))};
        const auto ancestor_context{bitcoin::validate_header_context(
            std::span<const bitcoin::block_header>{ancestor_headers.data(), ancestor_headers.size()},
            consensus)};
        if (ancestor_context.has_error()) {
            SetValidationOperationError(error, ancestor_context.assume_error());
            return nullptr;
        }
        if (ancestor_context.has_invalid_evidence()) {
            BlockValidationState state;
            ApplyInvalidHeaderContextEvidence(state, ancestor_context.assume_invalid_evidence());
            return btck_BlockVerifyResult::create(BlockVerifyResultValue{
                .status = btck_CheckStatus_INVALID,
                .validation_state = std::move(state),
                .header_context_evidence = ancestor_context.assume_invalid_evidence(),
            });
        }

        const bitcoin::block_validation_context validation_context{
            ToValidationBlockContext(options, consensus)};
        const bitcoin::validation_time operation_time{
            std::chrono::sys_seconds{std::chrono::seconds{options.operation_time}}};
        const auto result{bitcoin::verify(
            btck_Block::get(block),
            ancestor_context.assume_value(),
            operation_time,
            validation_context,
            CApiCoinIndex{coins})};
        if (result.has_error()) {
            SetValidationOperationError(error, result.assume_error());
            return nullptr;
        }

        BlockValidationState state;
        std::optional<bitcoin::validation_rejection> rejection{};
        if (!result.assume_value().accepted()) {
            rejection = result.assume_value().assume_rejection();
            ApplyRejection(state, *rejection);
        }

        return btck_BlockVerifyResult::create(BlockVerifyResultValue{
            .status = state.IsValid() ? btck_CheckStatus_VALID : btck_CheckStatus_INVALID,
            .validation_state = std::move(state),
            .rejection = rejection});
    });
}

btck_CheckStatus btck_block_verify_result_get_status(const btck_BlockVerifyResult* result)
{
    return KernelTryValue<btck_CheckStatus>(__func__, btck_CheckStatus_INVALID, [&] { return btck_BlockVerifyResult::get(result).status; });
}

const btck_BlockValidationState* btck_block_verify_result_get_validation_state(const btck_BlockVerifyResult* result)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockValidationState::ref(&btck_BlockVerifyResult::get(result).validation_state); });
}

btck_ValidationRejectionCode btck_block_verify_result_get_rejection_code(const btck_BlockVerifyResult* result)
{
    return KernelTryValue<btck_ValidationRejectionCode>(__func__, btck_ValidationRejectionCode_NONE, [&] {
        const auto& rejection{btck_BlockVerifyResult::get(result).rejection};
        return rejection ? ValidationRejectionCodeFor(rejection->code()) : btck_ValidationRejectionCode_NONE;
    });
}

btck_ValidationRule btck_block_verify_result_get_rejection_rule(const btck_BlockVerifyResult* result)
{
    return KernelTryValue<btck_ValidationRule>(__func__, btck_ValidationRule_NONE, [&] {
        const auto& rejection{btck_BlockVerifyResult::get(result).rejection};
        return rejection ? ValidationRuleFor(rejection->rule_id()) : btck_ValidationRule_NONE;
    });
}

const char* btck_block_verify_result_get_rejection_rule_code(const btck_BlockVerifyResult* result, size_t* rule_code_len)
{
    return KernelTryPointer(__func__, [&]() -> const char* {
        if (rule_code_len != nullptr) *rule_code_len = 0;
        const auto& rejection{btck_BlockVerifyResult::get(result).rejection};
        if (!rejection) return nullptr;

        const std::string_view rule_code{rejection->rule_code()};
        if (rule_code_len != nullptr) *rule_code_len = rule_code.size();
        return rule_code.data();
    });
}

const char* btck_block_verify_result_get_rejection_reason(const btck_BlockVerifyResult* result, size_t* reason_len)
{
    return KernelTryPointer(__func__, [&]() -> const char* {
        if (reason_len != nullptr) *reason_len = 0;
        const auto& rejection{btck_BlockVerifyResult::get(result).rejection};
        if (!rejection) return nullptr;

        const std::string_view reason{rejection->reason()};
        if (reason_len != nullptr) *reason_len = reason.size();
        return reason.data();
    });
}

btck_HeaderContextEvidenceCode btck_block_verify_result_get_header_context_evidence_code(const btck_BlockVerifyResult* result)
{
    return KernelTryValue<btck_HeaderContextEvidenceCode>(__func__, btck_HeaderContextEvidenceCode_NONE, [&] {
        const auto& evidence{btck_BlockVerifyResult::get(result).header_context_evidence};
        return evidence ? HeaderContextEvidenceCodeFor(evidence->code()) : btck_HeaderContextEvidenceCode_NONE;
    });
}

const char* btck_block_verify_result_get_header_context_evidence_reason(const btck_BlockVerifyResult* result, size_t* reason_len)
{
    return KernelTryPointer(__func__, [&]() -> const char* {
        if (reason_len != nullptr) *reason_len = 0;
        const auto& evidence{btck_BlockVerifyResult::get(result).header_context_evidence};
        if (!evidence) return nullptr;

        const std::string_view reason{evidence->reason()};
        if (reason_len != nullptr) *reason_len = reason.size();
        return reason.data();
    });
}

void btck_block_verify_result_destroy(btck_BlockVerifyResult* result)
{
    delete result;
}

size_t btck_block_count_transactions(const btck_Block* block)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_Block::get(block).transactions().size(); });
}

const btck_Transaction* btck_block_get_transaction_at(const btck_Block* block, size_t index)
{
    return KernelTryPointer(__func__, [&] {
        const auto transactions{btck_Block::get(block).transactions()};
        if (index >= transactions.size()) {
            LogError("Block transaction index is out of range.");
            return static_cast<const btck_Transaction*>(nullptr);
        }
        return btck_Transaction::ref(&transactions[index]);
    });
}

btck_BlockHeader* btck_block_get_header(const btck_Block* block, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        return btck_BlockHeader::create(btck_Block::get(block).header());
    });
}

int btck_block_serialize(const btck_Block* block, btck_WriteBytes writer, void* user_data, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        CallbackByteSink sink{writer, user_data};
        bitcoin::serialize(btck_Block::get(block), bitcoin::byte_sink_ref{sink});
        return 0;
    });
}

btck_BlockHash* btck_block_get_hash(const btck_Block* block, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHash::create(btck_Block::get(block).hash()); });
}

void btck_block_destroy(btck_Block* block)
{
    delete block;
}

btck_BlockReadResult* btck_chainstate_read_block_result(const btck_Chainstate* chainman, const btck_BlockHash* block_hash, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockReadResult* {
        auto& chainman_ref{*btck_Chainstate::get(chainman).m_chainman};
        CoreBlockDataStore block_store{chainman_ref.m_blockman};
        using BlockReadRequestResult = std::variant<btck_BlockReadStatus, BlockDataReadRequest>;
        const auto read_request { WITH_LOCK(chainman_ref.GetMutex(),
                                            CoreBlockIndexStore block_index_store{chainman_ref};
                                            const CBlockIndex* block_index{block_index_store.LookupBlockIndex(bitcoin::core_adapter::to_uint256(btck_BlockHash::get(block_hash)))};
                                            if (!block_index) return BlockReadRequestResult{btck_BlockReadStatus_NOT_INDEXED};
                                            if (!(block_index->nStatus & BLOCK_HAVE_DATA)) return BlockReadRequestResult{btck_BlockReadStatus_DATA_UNAVAILABLE};
                                            return BlockReadRequestResult{SnapshotBlockDataReadRequest(*block_index)};) };
        if (const auto* status{std::get_if<btck_BlockReadStatus>(&read_request)}) {
            LogDebug(BCLog::KERNEL, "%s", *status == btck_BlockReadStatus_NOT_INDEXED ? "A block with the given hash is not indexed." : "A block with the given hash has no available block data.");
            return btck_BlockReadResult::create(BlockReadResultValue{.status = *status});
        }

        auto block_result{block_store.ReadBlock(std::get<BlockDataReadRequest>(read_request))};
        if (!block_result) {
            LogError("Failed to read block.");
            SetError(error, btck_ErrorCode_IO_READ, "failed to read block data");
            return nullptr;
        }
        auto block{std::make_shared<const bitcoin::block>(bitcoin::core_adapter::to_block(*block_result))};
        return btck_BlockReadResult::create(BlockReadResultValue{
            .status = btck_BlockReadStatus_FOUND,
            .block = std::move(block),
        });
    });
}

btck_BlockReadStatus btck_block_read_result_get_status(const btck_BlockReadResult* result)
{
    return KernelTryValue<btck_BlockReadStatus>(__func__, btck_BlockReadStatus_NOT_INDEXED, [&] { return btck_BlockReadResult::get(result).status; });
}

const btck_Block* btck_block_read_result_get_block(const btck_BlockReadResult* result)
{
    return KernelTryPointer(__func__, [&]() -> const btck_Block* {
        const auto& value{btck_BlockReadResult::get(result)};
        if (value.status != btck_BlockReadStatus_FOUND || !value.block) return nullptr;
        return btck_Block::ref(value.block.get());
    });
}

void btck_block_read_result_destroy(btck_BlockReadResult* result)
{
    delete result;
}

btck_BlockHash* btck_block_hash_create(const unsigned char block_hash[32], btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHash::create(HashFromBytes<bitcoin::block_hash>(block_hash)); });
}

btck_BlockHash* btck_block_hash_copy(const btck_BlockHash* block_hash, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHash::copy(block_hash); });
}

void btck_block_hash_to_bytes(const btck_BlockHash* block_hash, unsigned char output[32])
{
    KernelTryVoid(__func__, [&] { CopyHashBytes(btck_BlockHash::get(block_hash), output); });
}

int btck_block_hash_equals(const btck_BlockHash* hash1, const btck_BlockHash* hash2)
{
    return KernelTryInt(__func__, 0, [&] { return btck_BlockHash::get(hash1) == btck_BlockHash::get(hash2); });
}

void btck_block_hash_destroy(btck_BlockHash* hash)
{
    delete hash;
}

btck_BlockSpentOutputsReadResult* btck_chainstate_read_block_spent_outputs_result(const btck_Chainstate* chainman, const btck_BlockHash* block_hash, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockSpentOutputsReadResult* {
        auto& chainman_ref{*btck_Chainstate::get(chainman).m_chainman};
        CoreBlockDataStore block_store{chainman_ref.m_blockman};
        using SpentOutputsReadRequestResult = std::variant<btck_BlockSpentOutputsReadStatus, BlockUndoReadRequest>;
        const auto read_request { WITH_LOCK(chainman_ref.GetMutex(),
                                            CoreBlockIndexStore block_index_store{chainman_ref};
                                            const CBlockIndex* block_index{block_index_store.LookupBlockIndex(bitcoin::core_adapter::to_uint256(btck_BlockHash::get(block_hash)))};
                                            if (!block_index) return SpentOutputsReadRequestResult{btck_BlockSpentOutputsReadStatus_NOT_INDEXED};
                                            if (block_index->nHeight < 1) return SpentOutputsReadRequestResult{BlockUndoReadRequest{}};
                                            if (!(block_index->nStatus & BLOCK_HAVE_UNDO)) return SpentOutputsReadRequestResult{btck_BlockSpentOutputsReadStatus_DATA_UNAVAILABLE};
                                            return SpentOutputsReadRequestResult{SnapshotBlockUndoReadRequest(*block_index)};) };
        if (const auto* status{std::get_if<btck_BlockSpentOutputsReadStatus>(&read_request)}) {
            LogDebug(BCLog::KERNEL, "%s", *status == btck_BlockSpentOutputsReadStatus_NOT_INDEXED ? "A block with the given hash is not indexed." : "A block with the given hash has no available undo data.");
            return btck_BlockSpentOutputsReadResult::create(BlockSpentOutputsReadResultValue{.status = *status});
        }
        const BlockUndoReadRequest& undo_request{std::get<BlockUndoReadRequest>(read_request)};
        if (undo_request.height < 1) {
            LogDebug(BCLog::KERNEL, "The genesis block does not have any spent outputs.");
            auto block_undo{std::make_shared<BlockSpentOutputsValue>()};
            return btck_BlockSpentOutputsReadResult::create(BlockSpentOutputsReadResultValue{
                .status = btck_BlockSpentOutputsReadStatus_FOUND,
                .spent_outputs = std::move(block_undo),
            });
        }
        auto block_undo_result{block_store.ReadBlockUndo(undo_request)};
        if (!block_undo_result) {
            LogError("Failed to read block spent outputs data.");
            SetError(error, btck_ErrorCode_IO_READ, "failed to read block spent outputs data");
            return nullptr;
        }
        auto block_undo{std::make_shared<BlockSpentOutputsValue>(SnapshotBlockSpentOutputs(*block_undo_result))};
        return btck_BlockSpentOutputsReadResult::create(BlockSpentOutputsReadResultValue{
            .status = btck_BlockSpentOutputsReadStatus_FOUND,
            .spent_outputs = std::move(block_undo),
        });
    });
}

btck_BlockSpentOutputsReadStatus btck_block_spent_outputs_read_result_get_status(const btck_BlockSpentOutputsReadResult* result)
{
    return KernelTryValue<btck_BlockSpentOutputsReadStatus>(__func__, btck_BlockSpentOutputsReadStatus_NOT_INDEXED, [&] {
        return btck_BlockSpentOutputsReadResult::get(result).status;
    });
}

const btck_BlockSpentOutputs* btck_block_spent_outputs_read_result_get_spent_outputs(const btck_BlockSpentOutputsReadResult* result)
{
    return KernelTryPointer(__func__, [&]() -> const btck_BlockSpentOutputs* {
        const auto& value{btck_BlockSpentOutputsReadResult::get(result)};
        if (value.status != btck_BlockSpentOutputsReadStatus_FOUND || !value.spent_outputs) return nullptr;
        return btck_BlockSpentOutputs::ref(&value.spent_outputs);
    });
}

void btck_block_spent_outputs_read_result_destroy(btck_BlockSpentOutputsReadResult* result)
{
    delete result;
}

btck_BlockSpentOutputs* btck_block_spent_outputs_copy(const btck_BlockSpentOutputs* block_spent_outputs, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockSpentOutputs::copy(block_spent_outputs); });
}

size_t btck_block_spent_outputs_count(const btck_BlockSpentOutputs* block_spent_outputs)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_BlockSpentOutputs::get(block_spent_outputs)->transactions.size(); });
}

const btck_TransactionSpentOutputs* btck_block_spent_outputs_get_transaction_spent_outputs_at(const btck_BlockSpentOutputs* block_spent_outputs, size_t transaction_index)
{
    return KernelTryPointer(__func__, [&] {
        const auto& tx_undos{btck_BlockSpentOutputs::get(block_spent_outputs)->transactions};
        if (transaction_index >= tx_undos.size()) {
            LogError("Block spent outputs transaction index is out of range.");
            return static_cast<const btck_TransactionSpentOutputs*>(nullptr);
        }
        return btck_TransactionSpentOutputs::ref(&tx_undos[transaction_index]);
    });
}

void btck_block_spent_outputs_destroy(btck_BlockSpentOutputs* block_spent_outputs)
{
    delete block_spent_outputs;
}

btck_TransactionSpentOutputs* btck_transaction_spent_outputs_copy(const btck_TransactionSpentOutputs* transaction_spent_outputs, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TransactionSpentOutputs::copy(transaction_spent_outputs); });
}

size_t btck_transaction_spent_outputs_count(const btck_TransactionSpentOutputs* transaction_spent_outputs)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_TransactionSpentOutputs::get(transaction_spent_outputs).coins.size(); });
}

void btck_transaction_spent_outputs_destroy(btck_TransactionSpentOutputs* transaction_spent_outputs)
{
    delete transaction_spent_outputs;
}

const btck_Coin* btck_transaction_spent_outputs_get_coin_at(const btck_TransactionSpentOutputs* transaction_spent_outputs, size_t coin_index)
{
    return KernelTryPointer(__func__, [&] {
        const auto& coins{btck_TransactionSpentOutputs::get(transaction_spent_outputs).coins};
        if (coin_index >= coins.size()) {
            LogError("Transaction spent outputs coin index is out of range.");
            return static_cast<const btck_Coin*>(nullptr);
        }
        return btck_Coin::ref(&coins[coin_index]);
    });
}

btck_Coin* btck_coin_create(
    const btck_TransactionOutput* output,
    uint32_t confirmation_height,
    int coinbase,
    int64_t previous_median_time_past,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_Coin* {
        if (confirmation_height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "coin height exceeds validation library range");
            return nullptr;
        }
        return btck_Coin::create(CoinValue{
            .output = btck_TransactionOutput::get(output),
            .height = confirmation_height,
            .coinbase = coinbase != 0,
            .previous_median_time_past = bitcoin::median_time_past{
                std::chrono::sys_seconds{std::chrono::seconds{previous_median_time_past}}}});
    });
}

btck_Coin* btck_coin_copy(const btck_Coin* coin, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_Coin::copy(coin); });
}

uint32_t btck_coin_confirmation_height(const btck_Coin* coin)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_Coin::get(coin).height; });
}

int btck_coin_is_coinbase(const btck_Coin* coin)
{
    return KernelTryInt(__func__, 0, [&] { return btck_Coin::get(coin).coinbase ? 1 : 0; });
}

int64_t btck_coin_previous_median_time_past(const btck_Coin* coin)
{
    return KernelTryValue<int64_t>(__func__, 0, [&] {
        return btck_Coin::get(coin).previous_median_time_past.value().time_since_epoch().count();
    });
}

const btck_TransactionOutput* btck_coin_get_output(const btck_Coin* coin)
{
    return KernelTryPointer(__func__, [&] { return btck_TransactionOutput::ref(&btck_Coin::get(coin).output); });
}

void btck_coin_destroy(btck_Coin* coin)
{
    delete coin;
}

btck_BlockProcessResult* btck_chainstate_process_block_result(
    btck_Chainstate* chainman,
    const btck_Block* block,
    const btck_BlockValidationOptions* options,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockProcessResult* {
        const auto result{ProcessBlockWithTime(chainman, block, TimeFromOptions(options), error)};
        if (!result) return nullptr;
        return btck_BlockProcessResult::create(*result);
    });
}

btck_BlockProcessStatus btck_block_process_result_get_status(const btck_BlockProcessResult* result)
{
    return KernelTryValue<btck_BlockProcessStatus>(__func__, btck_BlockProcessStatus_CHECK_FAILED, [&] {
        return btck_BlockProcessResult::get(result).status;
    });
}

int btck_block_process_result_has_new_block_data(const btck_BlockProcessResult* result)
{
    return KernelTryInt(__func__, 0, [&] { return btck_BlockProcessResult::get(result).has_new_block_data ? 1 : 0; });
}

const btck_BlockValidationState* btck_block_process_result_get_validation_state(const btck_BlockProcessResult* result)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockValidationState::ref(&btck_BlockProcessResult::get(result).validation_state); });
}

void btck_block_process_result_destroy(btck_BlockProcessResult* result)
{
    delete result;
}

btck_HeaderProcessResult* btck_chainstate_process_header_result(
    btck_Chainstate* chainstate_manager,
    const btck_BlockHeader* header,
    const btck_BlockValidationOptions* options,
    btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        return btck_HeaderProcessResult::create(ProcessBlockHeaderWithTime(chainstate_manager, header, TimeFromOptions(options)));
    });
}

btck_HeaderProcessStatus btck_header_process_result_get_status(const btck_HeaderProcessResult* result)
{
    return KernelTryValue<btck_HeaderProcessStatus>(__func__, btck_HeaderProcessStatus_REJECTED, [&] {
        return btck_HeaderProcessResult::get(result).status;
    });
}

const btck_BlockValidationState* btck_header_process_result_get_validation_state(const btck_HeaderProcessResult* result)
{
    return KernelTryPointer(__func__, [&] { return btck_BlockValidationState::ref(&btck_HeaderProcessResult::get(result).validation_state); });
}

void btck_header_process_result_destroy(btck_HeaderProcessResult* result)
{
    delete result;
}

btck_ChainSnapshot* btck_chainstate_snapshot_active_chain(const btck_Chainstate* chainman, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] {
        const auto& chainman_ref{*btck_Chainstate::get(chainman).m_chainman};
        return btck_ChainSnapshot::create(WITH_LOCK(chainman_ref.GetMutex(), return SnapshotActiveChain(chainman_ref)));
    });
}

btck_ChainSnapshot* btck_chain_snapshot_copy(const btck_ChainSnapshot* chain_snapshot, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_ChainSnapshot::copy(chain_snapshot); });
}

int32_t btck_chain_snapshot_get_height(const btck_ChainSnapshot* chain_snapshot)
{
    return KernelTryValue<int32_t>(__func__, -1, [&] {
        const auto& blocks{btck_ChainSnapshot::get(chain_snapshot).blocks};
        return blocks.empty() ? -1 : blocks.back().height;
    });
}

size_t btck_chain_snapshot_count(const btck_ChainSnapshot* chain_snapshot)
{
    return KernelTryValue<size_t>(__func__, 0, [&] { return btck_ChainSnapshot::get(chain_snapshot).blocks.size(); });
}

const btck_BlockInfo* btck_chain_snapshot_get_block_info_by_height(const btck_ChainSnapshot* chain_snapshot, int32_t height)
{
    return KernelTryPointer(__func__, [&]() -> const btck_BlockInfo* {
        const auto& blocks{btck_ChainSnapshot::get(chain_snapshot).blocks};
        if (height < 0 || static_cast<size_t>(height) >= blocks.size()) {
            LogError("Chain snapshot height is out of range.");
            return nullptr;
        }
        return btck_BlockInfo::ref(&blocks[static_cast<size_t>(height)]);
    });
}

int btck_chain_snapshot_contains_block_hash(const btck_ChainSnapshot* chain_snapshot, const btck_BlockHash* block_hash)
{
    return KernelTryInt(__func__, 0, [&] {
        const auto& blocks{btck_ChainSnapshot::get(chain_snapshot).blocks};
        const bitcoin::block_hash hash{btck_BlockHash::get(block_hash)};
        return std::ranges::any_of(blocks, [&](const BlockInfoSnapshot& info) { return info.hash == hash; }) ? 1 : 0;
    });
}

void btck_chain_snapshot_destroy(btck_ChainSnapshot* chain_snapshot)
{
    delete chain_snapshot;
}

btck_BlockHeaderParseResult* btck_block_header_parse_result(const void* raw_block_header, size_t raw_block_header_len, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_BlockHeaderParseResult* {
        if (raw_block_header == nullptr && raw_block_header_len != 0) {
            SetError(error, btck_ErrorCode_INVALID_ARGUMENT, "raw block header bytes are null but length is non-zero");
            return nullptr;
        }
        const auto parsed{bitcoin::parse_block_header(std::span{reinterpret_cast<const std::byte*>(raw_block_header), raw_block_header_len})};
        if (!parsed) {
            return btck_BlockHeaderParseResult::create(MalformedParseResult<BlockHeaderParseResultValue>(parsed.assume_failure()));
        }
        return btck_BlockHeaderParseResult::create(BlockHeaderParseResultValue{
            .status = btck_ParseStatus_OK,
            .header = parsed.assume_value(),
        });
    });
}

btck_ParseStatus btck_block_header_parse_result_get_status(const btck_BlockHeaderParseResult* result)
{
    return KernelTryValue<btck_ParseStatus>(__func__, btck_ParseStatus_MALFORMED, [&] { return btck_BlockHeaderParseResult::get(result).status; });
}

btck_ParseFailureCode btck_block_header_parse_result_get_failure_code(const btck_BlockHeaderParseResult* result)
{
    return KernelTryValue<btck_ParseFailureCode>(__func__, btck_ParseFailureCode_NONE, [&] {
        return btck_BlockHeaderParseResult::get(result).failure_code;
    });
}

size_t btck_block_header_parse_result_get_failure_offset(const btck_BlockHeaderParseResult* result)
{
    return KernelTryValue<size_t>(__func__, 0, [&] {
        return btck_BlockHeaderParseResult::get(result).failure_offset;
    });
}

const btck_BlockHeader* btck_block_header_parse_result_get_header(const btck_BlockHeaderParseResult* result)
{
    return KernelTryPointer(__func__, [&]() -> const btck_BlockHeader* {
        const auto& value{btck_BlockHeaderParseResult::get(result)};
        if (value.status != btck_ParseStatus_OK || !value.header) return nullptr;
        return btck_BlockHeader::ref(&*value.header);
    });
}

void btck_block_header_parse_result_destroy(btck_BlockHeaderParseResult* result)
{
    delete result;
}

btck_BlockHeader* btck_block_header_copy(const btck_BlockHeader* header, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHeader::copy(header); });
}

btck_BlockHash* btck_block_header_get_hash(const btck_BlockHeader* header, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHash::create(btck_BlockHeader::get(header).hash()); });
}

btck_BlockHash* btck_block_header_get_prev_hash(const btck_BlockHeader* header, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_BlockHash::create(btck_BlockHeader::get(header).previous_block_hash()); });
}

uint32_t btck_block_header_get_timestamp(const btck_BlockHeader* header)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_BlockHeader::get(header).time().seconds_since_epoch(); });
}

uint32_t btck_block_header_get_bits(const btck_BlockHeader* header)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_BlockHeader::get(header).bits(); });
}

int32_t btck_block_header_get_version(const btck_BlockHeader* header)
{
    return KernelTryValue<int32_t>(__func__, 0, [&] { return btck_BlockHeader::get(header).version(); });
}

uint32_t btck_block_header_get_nonce(const btck_BlockHeader* header)
{
    return KernelTryValue<uint32_t>(__func__, 0, [&] { return btck_BlockHeader::get(header).nonce(); });
}

int btck_block_header_serialize(const btck_BlockHeader* header, btck_WriteBytes writer, void* user_data, btck_Error** error)
{
    return KernelTryInt(__func__, error, -1, [&] {
        CallbackByteSink sink{writer, user_data};
        bitcoin::serialize(btck_BlockHeader::get(header), bitcoin::byte_sink_ref{sink});
        return 0;
    });
}

void btck_block_header_destroy(btck_BlockHeader* header)
{
    delete header;
}

btck_ValidationMode btck_tx_validation_state_get_validation_mode(const btck_TxValidationState* state_)
{
    return KernelTryValue<btck_ValidationMode>(__func__, btck_ValidationMode_INTERNAL_ERROR, [&] {
        const auto& state = btck_TxValidationState::get(state_);
        if (state.IsValid()) return btck_ValidationMode_VALID;
        if (state.IsInvalid()) return btck_ValidationMode_INVALID;
        return btck_ValidationMode_INTERNAL_ERROR;
    });
}

btck_TxValidationState* btck_tx_validation_state_create(btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TxValidationState::create(); });
}

btck_TxValidationState* btck_tx_validation_state_copy(const btck_TxValidationState* state, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&] { return btck_TxValidationState::copy(state); });
}

btck_TxValidationResult btck_tx_validation_state_get_tx_validation_result(const btck_TxValidationState* state_)
{
    return KernelTryValue<btck_TxValidationResult>(__func__, btck_TxValidationResult_UNKNOWN, [&] {
        switch (btck_TxValidationState::get(state_).GetResult()) {
        case TxValidationResult::TX_RESULT_UNSET:
            return btck_TxValidationResult_UNSET;
        case TxValidationResult::TX_CONSENSUS:
            return btck_TxValidationResult_CONSENSUS;
        default:
            return btck_TxValidationResult_UNKNOWN;
        }
    });
}

void btck_tx_validation_state_destroy(btck_TxValidationState* state)
{
    delete state;
}

btck_TransactionCheckResult* btck_transaction_check_result(const btck_Transaction* tx, btck_Error** error)
{
    return KernelTryPointer(__func__, error, [&]() -> btck_TransactionCheckResult* {
        TxValidationState state;
        const auto result{bitcoin::assess_transaction_intrinsic(btck_Transaction::get(tx), bitcoin::transaction_context{})};
        if (result.has_error()) {
            SetValidationOperationError(error, result.assume_error());
            return nullptr;
        } else if (!result.assume_value().accepted()) {
            ApplyRejection(state, result.assume_value().assume_rejection());
        }
        return btck_TransactionCheckResult::create(TransactionCheckResultValue{
            .status = state.IsValid() ? btck_CheckStatus_VALID : btck_CheckStatus_INVALID,
            .validation_state = std::move(state),
        });
    });
}

btck_CheckStatus btck_transaction_check_result_get_status(const btck_TransactionCheckResult* result)
{
    return KernelTryValue<btck_CheckStatus>(__func__, btck_CheckStatus_INVALID, [&] { return btck_TransactionCheckResult::get(result).status; });
}

const btck_TxValidationState* btck_transaction_check_result_get_validation_state(const btck_TransactionCheckResult* result)
{
    return KernelTryPointer(__func__, [&] { return btck_TxValidationState::ref(&btck_TransactionCheckResult::get(result).validation_state); });
}

void btck_transaction_check_result_destroy(btck_TransactionCheckResult* result)
{
    delete result;
}
