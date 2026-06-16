// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KERNEL_BITCOINKERNEL_WRAPPER_H
#define BITCOIN_KERNEL_BITCOINKERNEL_WRAPPER_H

#include <kernel/bitcoinkernel.h>

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace btck {

enum class LogCategory : btck_LogCategory {
    ALL = btck_LogCategory_ALL,
    BENCH = btck_LogCategory_BENCH,
    BLOCKSTORAGE = btck_LogCategory_BLOCKSTORAGE,
    COINDB = btck_LogCategory_COINDB,
    LEVELDB = btck_LogCategory_LEVELDB,
    MEMPOOL = btck_LogCategory_MEMPOOL,
    PRUNE = btck_LogCategory_PRUNE,
    RAND = btck_LogCategory_RAND,
    REINDEX = btck_LogCategory_REINDEX,
    VALIDATION = btck_LogCategory_VALIDATION,
    KERNEL = btck_LogCategory_KERNEL
};

enum class LogLevel : btck_LogLevel {
    TRACE_LEVEL = btck_LogLevel_TRACE,
    DEBUG_LEVEL = btck_LogLevel_DEBUG,
    INFO_LEVEL = btck_LogLevel_INFO
};

enum class ChainType : btck_ChainType {
    MAINNET = btck_ChainType_MAINNET,
    TESTNET = btck_ChainType_TESTNET,
    TESTNET_4 = btck_ChainType_TESTNET_4,
    SIGNET = btck_ChainType_SIGNET,
    REGTEST = btck_ChainType_REGTEST
};

enum class SynchronizationState : btck_SynchronizationState {
    INIT_REINDEX = btck_SynchronizationState_INIT_REINDEX,
    INIT_DOWNLOAD = btck_SynchronizationState_INIT_DOWNLOAD,
    POST_INIT = btck_SynchronizationState_POST_INIT
};

enum class Warning : btck_Warning {
    UNKNOWN_NEW_RULES_ACTIVATED = btck_Warning_UNKNOWN_NEW_RULES_ACTIVATED,
    LARGE_WORK_INVALID_CHAIN = btck_Warning_LARGE_WORK_INVALID_CHAIN
};

enum class ValidationMode : btck_ValidationMode {
    VALID = btck_ValidationMode_VALID,
    INVALID = btck_ValidationMode_INVALID,
    INTERNAL_ERROR = btck_ValidationMode_INTERNAL_ERROR
};

enum class BlockValidationResult : btck_BlockValidationResult {
    UNSET = btck_BlockValidationResult_UNSET,
    CONSENSUS = btck_BlockValidationResult_CONSENSUS,
    CACHED_INVALID = btck_BlockValidationResult_CACHED_INVALID,
    INVALID_HEADER = btck_BlockValidationResult_INVALID_HEADER,
    MUTATED = btck_BlockValidationResult_MUTATED,
    MISSING_PREV = btck_BlockValidationResult_MISSING_PREV,
    INVALID_PREV = btck_BlockValidationResult_INVALID_PREV,
    TIME_FUTURE = btck_BlockValidationResult_TIME_FUTURE,
    HEADER_LOW_WORK = btck_BlockValidationResult_HEADER_LOW_WORK
};

enum class TxValidationResult : btck_TxValidationResult {
    UNSET = btck_TxValidationResult_UNSET,
    CONSENSUS = btck_TxValidationResult_CONSENSUS,
    UNKNOWN = btck_TxValidationResult_UNKNOWN
};

enum class ScriptVerifyStatus : btck_ScriptVerifyStatus {
    OK = btck_ScriptVerifyStatus_OK,
    ERROR_INVALID_FLAGS_COMBINATION = btck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION,
    ERROR_SPENT_OUTPUTS_REQUIRED = btck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED,
    ERROR_INTERNAL = btck_ScriptVerifyStatus_ERROR_INTERNAL,
};

enum class ScriptVerificationFlags : btck_ScriptVerificationFlags {
    NONE = btck_ScriptVerificationFlags_NONE,
    P2SH = btck_ScriptVerificationFlags_P2SH,
    DERSIG = btck_ScriptVerificationFlags_DERSIG,
    NULLDUMMY = btck_ScriptVerificationFlags_NULLDUMMY,
    CHECKLOCKTIMEVERIFY = btck_ScriptVerificationFlags_CHECKLOCKTIMEVERIFY,
    CHECKSEQUENCEVERIFY = btck_ScriptVerificationFlags_CHECKSEQUENCEVERIFY,
    WITNESS = btck_ScriptVerificationFlags_WITNESS,
    TAPROOT = btck_ScriptVerificationFlags_TAPROOT,
    ALL = btck_ScriptVerificationFlags_ALL
};

enum class BlockCheckFlags : btck_BlockCheckFlags {
    BASE = btck_BlockCheckFlags_BASE,
    POW = btck_BlockCheckFlags_POW,
    MERKLE = btck_BlockCheckFlags_MERKLE,
    ALL = btck_BlockCheckFlags_ALL
};

enum class ErrorCode : btck_ErrorCode {
    NONE = btck_ErrorCode_NONE,
    EXCEPTION = btck_ErrorCode_EXCEPTION,
    RESOURCE_EXHAUSTION = btck_ErrorCode_RESOURCE_EXHAUSTION,
    INVALID_ARGUMENT = btck_ErrorCode_INVALID_ARGUMENT,
    IO = btck_ErrorCode_IO,
    CALLBACK = btck_ErrorCode_CALLBACK,
    CHAINSTATE_LOAD = btck_ErrorCode_CHAINSTATE_LOAD,
    IO_READ = btck_ErrorCode_IO_READ,
    IO_WRITE = btck_ErrorCode_IO_WRITE,
    DATA_UNAVAILABLE = btck_ErrorCode_DATA_UNAVAILABLE,
    STORAGE_CORRUPTION = btck_ErrorCode_STORAGE_CORRUPTION,
    INTERRUPTED = btck_ErrorCode_INTERRUPTED
};

enum class ParseStatus : btck_ParseStatus {
    OK = btck_ParseStatus_OK,
    MALFORMED = btck_ParseStatus_MALFORMED,
};

enum class CheckStatus : btck_CheckStatus {
    VALID = btck_CheckStatus_VALID,
    INVALID = btck_CheckStatus_INVALID,
};

enum class HeaderProcessStatus : btck_HeaderProcessStatus {
    ACCEPTED = btck_HeaderProcessStatus_ACCEPTED,
    REJECTED = btck_HeaderProcessStatus_REJECTED,
};

enum class BlockProcessStatus : btck_BlockProcessStatus {
    CHECK_FAILED = btck_BlockProcessStatus_CHECK_FAILED,
    HEADER_REJECTED = btck_BlockProcessStatus_HEADER_REJECTED,
    BLOCK_REJECTED = btck_BlockProcessStatus_BLOCK_REJECTED,
    ALREADY_KNOWN = btck_BlockProcessStatus_ALREADY_KNOWN,
    STORED = btck_BlockProcessStatus_STORED,
    UNREQUESTED_PREVIOUSLY_PROCESSED = btck_BlockProcessStatus_UNREQUESTED_PREVIOUSLY_PROCESSED,
    UNREQUESTED_LESS_WORK_THAN_TIP = btck_BlockProcessStatus_UNREQUESTED_LESS_WORK_THAN_TIP,
    UNREQUESTED_TOO_FAR_AHEAD = btck_BlockProcessStatus_UNREQUESTED_TOO_FAR_AHEAD,
    UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK = btck_BlockProcessStatus_UNREQUESTED_BELOW_MINIMUM_CHAIN_WORK,
};

enum class BlockImportStatus : btck_BlockImportStatus {
    COMPLETED = btck_BlockImportStatus_COMPLETED,
    INTERRUPTED = btck_BlockImportStatus_INTERRUPTED,
    ALREADY_IMPORTING = btck_BlockImportStatus_ALREADY_IMPORTING,
    RESOURCE_LIMIT = btck_BlockImportStatus_RESOURCE_LIMIT,
};

enum class BlockReadStatus : btck_BlockReadStatus {
    FOUND = btck_BlockReadStatus_FOUND,
    NOT_INDEXED = btck_BlockReadStatus_NOT_INDEXED,
    DATA_UNAVAILABLE = btck_BlockReadStatus_DATA_UNAVAILABLE,
};

enum class BlockSpentOutputsReadStatus : btck_BlockSpentOutputsReadStatus {
    FOUND = btck_BlockSpentOutputsReadStatus_FOUND,
    NOT_INDEXED = btck_BlockSpentOutputsReadStatus_NOT_INDEXED,
    DATA_UNAVAILABLE = btck_BlockSpentOutputsReadStatus_DATA_UNAVAILABLE,
};

template <typename T>
struct is_bitmask_enum : std::false_type {
};

template <>
struct is_bitmask_enum<ScriptVerificationFlags> : std::true_type {
};

template <>
struct is_bitmask_enum<BlockCheckFlags> : std::true_type {
};

template <typename T>
concept BitmaskEnum = is_bitmask_enum<T>::value;

template <BitmaskEnum T>
constexpr T operator|(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) | static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator&(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) & static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator^(T lhs, T rhs)
{
    return static_cast<T>(
        static_cast<std::underlying_type_t<T>>(lhs) ^ static_cast<std::underlying_type_t<T>>(rhs));
}

template <BitmaskEnum T>
constexpr T operator~(T value)
{
    return static_cast<T>(~static_cast<std::underlying_type_t<T>>(value));
}

template <BitmaskEnum T>
constexpr T& operator|=(T& lhs, T rhs)
{
    return lhs = lhs | rhs;
}

template <BitmaskEnum T>
constexpr T& operator&=(T& lhs, T rhs)
{
    return lhs = lhs & rhs;
}

template <BitmaskEnum T>
constexpr T& operator^=(T& lhs, T rhs)
{
    return lhs = lhs ^ rhs;
}

template <typename T>
T check(T ptr)
{
    if (ptr == nullptr) {
        throw std::runtime_error("failed to instantiate btck object");
    }
    return ptr;
}

inline void check_status(int status, const char* operation)
{
    if (status != 0) {
        throw std::runtime_error(operation);
    }
}

class ApiError : public std::runtime_error
{
    ErrorCode m_code;

public:
    ApiError(ErrorCode code, const std::string& message)
        : std::runtime_error{message}, m_code{code}
    {
    }

    ErrorCode code() const noexcept { return m_code; }
};

class ErrorOut
{
    btck_Error* m_error{nullptr};

public:
    ErrorOut() = default;
    ErrorOut(const ErrorOut&) = delete;
    ErrorOut& operator=(const ErrorOut&) = delete;
    ~ErrorOut() { btck_error_destroy(m_error); }

    btck_Error** out() { return &m_error; }
    bool has_error() const { return m_error != nullptr; }

    ErrorCode code() const
    {
        return m_error ? static_cast<ErrorCode>(btck_error_get_code(m_error)) : ErrorCode::NONE;
    }

    std::string message(std::string_view fallback) const
    {
        if (!m_error) return std::string{fallback};
        size_t message_len{0};
        const char* message{btck_error_get_message(m_error, &message_len)};
        if (!message) return std::string{fallback};
        return std::string{message, message_len};
    }
};

[[noreturn]] inline void throw_api_error(const ErrorOut& error, std::string_view fallback)
{
    throw ApiError{error.code(), error.message(fallback)};
}

inline void check_status(int status, ErrorOut& error, const char* operation)
{
    if (status != 0) {
        throw_api_error(error, operation);
    }
}

template <typename T>
T check(T ptr, ErrorOut& error, const char* operation)
{
    if (ptr == nullptr) {
        throw_api_error(error, operation);
    }
    return ptr;
}

template <typename Collection, typename ValueType>
class Iterator
{
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = ValueType;

private:
    const Collection* m_collection;
    size_t m_idx;

public:
    Iterator() = default;
    Iterator(const Collection* ptr) : m_collection{ptr}, m_idx{0} {}
    Iterator(const Collection* ptr, size_t idx) : m_collection{ptr}, m_idx{idx} {}

    // This is just a view, so return a copy.
    auto operator*() const { return (*m_collection)[m_idx]; }
    auto operator->() const { return (*m_collection)[m_idx]; }

    auto& operator++()
    {
        m_idx++;
        return *this;
    }
    auto operator++(int)
    {
        Iterator tmp = *this;
        ++(*this);
        return tmp;
    }

    auto& operator--()
    {
        m_idx--;
        return *this;
    }
    auto operator--(int)
    {
        auto temp = *this;
        --m_idx;
        return temp;
    }

    auto& operator+=(difference_type n)
    {
        m_idx += n;
        return *this;
    }
    auto& operator-=(difference_type n)
    {
        m_idx -= n;
        return *this;
    }

    auto operator+(difference_type n) const { return Iterator(m_collection, m_idx + n); }
    auto operator-(difference_type n) const { return Iterator(m_collection, m_idx - n); }

    auto operator-(const Iterator& other) const { return static_cast<difference_type>(m_idx) - static_cast<difference_type>(other.m_idx); }

    ValueType operator[](difference_type n) const { return (*m_collection)[m_idx + n]; }

    auto operator<=>(const Iterator& other) const { return m_idx <=> other.m_idx; }

    bool operator==(const Iterator& other) const { return m_collection == other.m_collection && m_idx == other.m_idx; }

private:
    friend Iterator operator+(difference_type n, const Iterator& it) { return it + n; }
};

template <typename Container, typename SizeFunc, typename GetFunc>
concept IndexedContainer = requires(const Container& c, SizeFunc size_func, GetFunc get_func, std::size_t i) {
    { std::invoke(size_func, c) } -> std::convertible_to<std::size_t>;
    { std::invoke(get_func, c, i) }; // Return type is deduced
};

template <typename Container, auto SizeFunc, auto GetFunc>
    requires IndexedContainer<Container, decltype(SizeFunc), decltype(GetFunc)>
class Range
{
public:
    using value_type = std::invoke_result_t<decltype(GetFunc), const Container&, size_t>;
    using difference_type = std::ptrdiff_t;
    using iterator = Iterator<Range, value_type>;
    using const_iterator = iterator;

private:
    const Container* m_container;

public:
    explicit Range(const Container& container) : m_container(&container)
    {
        static_assert(std::ranges::random_access_range<Range>);
    }

    iterator begin() const { return iterator(this, 0); }
    iterator end() const { return iterator(this, size()); }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    size_t size() const { return std::invoke(SizeFunc, *m_container); }

    bool empty() const { return size() == 0; }

    value_type operator[](size_t index) const { return std::invoke(GetFunc, *m_container, index); }

    value_type at(size_t index) const
    {
        if (index >= size()) {
            throw std::out_of_range("Index out of range");
        }
        return (*this)[index];
    }

    value_type front() const { return (*this)[0]; }
    value_type back() const { return (*this)[size() - 1]; }
};

#define MAKE_RANGE_METHOD(method_name, ContainerType, SizeFunc, GetFunc, container_expr) \
    auto method_name() const&                                                            \
    {                                                                                    \
        return Range<ContainerType, SizeFunc, GetFunc>{container_expr};                  \
    }                                                                                    \
    auto method_name() const&& = delete;

template <typename T>
std::vector<std::byte> write_bytes(const T* object, int (*to_bytes)(const T*, btck_WriteBytes, void*, btck_Error**))
{
    std::vector<std::byte> bytes;
    struct UserData {
        std::vector<std::byte>* bytes;
        std::exception_ptr exception;
    };
    UserData user_data = UserData{.bytes = &bytes, .exception = nullptr};

    constexpr auto const write = +[](const void* buffer, size_t len, void* user_data) -> int {
        auto& data = *reinterpret_cast<UserData*>(user_data);
        auto& bytes = *data.bytes;
        try {
            auto const* first = static_cast<const std::byte*>(buffer);
            auto const* last = first + len;
            bytes.insert(bytes.end(), first, last);
            return 0;
        } catch (...) {
            data.exception = std::current_exception();
            return -1;
        }
    };

    ErrorOut error;
    if (to_bytes(object, write, &user_data, error.out()) != 0) {
        if (user_data.exception) std::rethrow_exception(user_data.exception);
        throw_api_error(error, "failed to serialize bytes");
    }
    return bytes;
}

template <typename CType>
class View
{
protected:
    const CType* m_ptr;

public:
    explicit View(const CType* ptr) : m_ptr{check(ptr)} {}

    const CType* get() const { return m_ptr; }
};

template <typename CType, auto CopyFunc, void (*DestroyFunc)(CType*)>
class Handle
{
protected:
    CType* m_ptr;

    static CType* CopyChecked(const CType* ptr)
    {
        if constexpr (std::is_invocable_r_v<CType*, decltype(CopyFunc), const CType*, btck_Error**>) {
            ErrorOut error;
            return check(CopyFunc(ptr, error.out()), error, "failed to copy btck object");
        } else {
            return check(CopyFunc(ptr));
        }
    }

public:
    explicit Handle(CType* ptr) : m_ptr{check(ptr)} {}

    // Copy constructors
    Handle(const Handle& other)
        : m_ptr{CopyChecked(other.m_ptr)} {}
    Handle& operator=(const Handle& other)
    {
        if (this != &other) {
            Handle temp(other);
            std::swap(m_ptr, temp.m_ptr);
        }
        return *this;
    }

    // Move constructors
    Handle(Handle&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            DestroyFunc(m_ptr);
            m_ptr = std::exchange(other.m_ptr, nullptr);
        }
        return *this;
    }

    template <typename ViewType>
        requires std::derived_from<ViewType, View<CType>>
    Handle(const ViewType& view)
        : Handle{CopyChecked(view.get())}
    {
    }

    ~Handle() { DestroyFunc(m_ptr); }

    CType* get() { return m_ptr; }
    const CType* get() const { return m_ptr; }
};

template <typename CType, void (*DestroyFunc)(CType*)>
class UniqueHandle
{
protected:
    struct Deleter {
        void operator()(CType* ptr) const noexcept
        {
            if (ptr) DestroyFunc(ptr);
        }
    };
    std::unique_ptr<CType, Deleter> m_ptr;

public:
    explicit UniqueHandle(CType* ptr) : m_ptr{check(ptr)} {}

    CType* get() { return m_ptr.get(); }
    const CType* get() const { return m_ptr.get(); }
};

class PrecomputedTransactionData;
class Transaction;
class TransactionOutput;
class BlockValidationState;

template <typename Derived>
class ScriptPubkeyApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    ScriptPubkeyApi() = default;

public:
    bool Verify(int64_t amount,
                const Transaction& tx_to,
                const PrecomputedTransactionData* precomputed_txdata,
                unsigned int input_index,
                ScriptVerificationFlags flags,
                ScriptVerifyStatus& status) const;

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(impl(), btck_script_pubkey_to_bytes);
    }
};

class ScriptPubkeyView : public View<btck_ScriptPubkey>, public ScriptPubkeyApi<ScriptPubkeyView>
{
public:
    explicit ScriptPubkeyView(const btck_ScriptPubkey* ptr) : View{ptr} {}
};

class ScriptPubkey : public Handle<btck_ScriptPubkey, btck_script_pubkey_copy, btck_script_pubkey_destroy>, public ScriptPubkeyApi<ScriptPubkey>
{
public:
    explicit ScriptPubkey(btck_ScriptPubkey* script_pubkey)
        : Handle{script_pubkey} {}

    explicit ScriptPubkey(std::span<const std::byte> raw)
        : Handle{[&] {
              ErrorOut error;
              return check(btck_script_pubkey_create(raw.data(), raw.size(), error.out()), error, "failed to create script pubkey");
          }()} {}

    ScriptPubkey(const ScriptPubkeyView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionOutputApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionOutputApi() = default;

public:
    int64_t Amount() const
    {
        return btck_transaction_output_get_amount(impl());
    }

    ScriptPubkey GetScriptPubkey() const
    {
        ErrorOut error;
        return ScriptPubkey{check(btck_transaction_output_get_script_pubkey(impl(), error.out()), error, "failed to copy transaction output script pubkey")};
    }
};

class TransactionOutputView : public View<btck_TransactionOutput>, public TransactionOutputApi<TransactionOutputView>
{
public:
    explicit TransactionOutputView(const btck_TransactionOutput* ptr) : View{ptr} {}
};

class TransactionOutput : public Handle<btck_TransactionOutput, btck_transaction_output_copy, btck_transaction_output_destroy>, public TransactionOutputApi<TransactionOutput>
{
public:
    explicit TransactionOutput(const ScriptPubkey& script_pubkey, int64_t amount)
        : Handle{[&] {
              ErrorOut error;
              return check(btck_transaction_output_create(script_pubkey.get(), amount, error.out()), error, "failed to create transaction output");
          }()} {}

    TransactionOutput(const TransactionOutputView& view)
        : Handle(view) {}
};

template <typename Derived>
class TxidApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TxidApi() = default;

public:
    template <typename Other>
    bool operator==(const TxidApi<Other>& other) const
    {
        return btck_txid_equals(impl(), static_cast<const Other&>(other).get()) != 0;
    }

    template <typename Other>
    bool operator!=(const TxidApi<Other>& other) const
    {
        return btck_txid_equals(impl(), static_cast<const Other&>(other).get()) == 0;
    }

    std::array<std::byte, 32> ToBytes() const
    {
        std::array<std::byte, 32> hash;
        btck_txid_to_bytes(impl(), reinterpret_cast<unsigned char*>(hash.data()));
        return hash;
    }
};

class TxidView : public View<btck_Txid>, public TxidApi<TxidView>
{
public:
    explicit TxidView(const btck_Txid* ptr) : View{ptr} {}
};

class Txid : public Handle<btck_Txid, btck_txid_copy, btck_txid_destroy>, public TxidApi<Txid>
{
public:
    explicit Txid(btck_Txid* txid)
        : Handle{txid} {}

    Txid(const TxidView& view)
        : Handle(view) {}
};

template <typename Derived>
class OutPointApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    OutPointApi() = default;

public:
    uint32_t index() const
    {
        return btck_transaction_out_point_get_index(impl());
    }

    Txid Txid() const
    {
        ErrorOut error;
        return btck::Txid{check(btck_transaction_out_point_get_txid(impl(), error.out()), error, "failed to copy outpoint txid")};
    }
};

class OutPointView : public View<btck_TransactionOutPoint>, public OutPointApi<OutPointView>
{
public:
    explicit OutPointView(const btck_TransactionOutPoint* ptr) : View{ptr} {}
};

class OutPoint : public Handle<btck_TransactionOutPoint, btck_transaction_out_point_copy, btck_transaction_out_point_destroy>, public OutPointApi<OutPoint>
{
public:
    explicit OutPoint(btck_TransactionOutPoint* outpoint)
        : Handle{outpoint} {}

    OutPoint(const OutPointView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionInputApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionInputApi() = default;

public:
    OutPoint OutPoint() const
    {
        ErrorOut error;
        return btck::OutPoint{check(btck_transaction_input_get_out_point(impl(), error.out()), error, "failed to copy transaction input outpoint")};
    }

    uint32_t GetSequence() const
    {
        return btck_transaction_input_get_sequence(impl());
    }
};

class TransactionInputView : public View<btck_TransactionInput>, public TransactionInputApi<TransactionInputView>
{
public:
    explicit TransactionInputView(const btck_TransactionInput* ptr) : View{ptr} {}
};

class TransactionInput : public Handle<btck_TransactionInput, btck_transaction_input_copy, btck_transaction_input_destroy>, public TransactionInputApi<TransactionInput>
{
public:
    TransactionInput(const TransactionInputView& view)
        : Handle(view) {}
};

template <typename Derived>
class TransactionApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    size_t CountOutputs() const
    {
        return btck_transaction_count_outputs(impl());
    }

    size_t CountInputs() const
    {
        return btck_transaction_count_inputs(impl());
    }

    TransactionOutputView GetOutput(size_t index) const
    {
        return TransactionOutputView{btck_transaction_get_output_at(impl(), index)};
    }

    TransactionInputView GetInput(size_t index) const
    {
        return TransactionInputView{btck_transaction_get_input_at(impl(), index)};
    }

    uint32_t GetLocktime() const
    {
        return btck_transaction_get_locktime(impl());
    }

    Txid Txid() const
    {
        ErrorOut error;
        return btck::Txid{check(btck_transaction_get_txid(impl(), error.out()), error, "failed to compute transaction txid")};
    }

    MAKE_RANGE_METHOD(Outputs, Derived, &TransactionApi<Derived>::CountOutputs, &TransactionApi<Derived>::GetOutput, *static_cast<const Derived*>(this))

    MAKE_RANGE_METHOD(Inputs, Derived, &TransactionApi<Derived>::CountInputs, &TransactionApi<Derived>::GetInput, *static_cast<const Derived*>(this))

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(impl(), btck_transaction_serialize);
    }
};

class TransactionView : public View<btck_Transaction>, public TransactionApi<TransactionView>
{
public:
    explicit TransactionView(const btck_Transaction* ptr) : View{ptr} {}
};

class Transaction : public Handle<btck_Transaction, btck_transaction_copy, btck_transaction_destroy>, public TransactionApi<Transaction>
{
    static btck_Transaction* ParseC(std::span<const std::byte> raw_transaction, ErrorOut& error)
    {
        UniqueHandle<btck_TransactionParseResult, btck_transaction_parse_result_destroy> result{
            check(btck_transaction_parse_result(raw_transaction.data(), raw_transaction.size(), error.out()), error, "failed to parse transaction")};
        if (btck_transaction_parse_result_get_status(result.get()) == btck_ParseStatus_MALFORMED) {
            return nullptr;
        }
        return check(btck_transaction_copy(
                         check(btck_transaction_parse_result_get_transaction(result.get())),
                         error.out()),
                     error, "failed to copy parsed transaction");
    }

public:
    static std::optional<Transaction> Parse(std::span<const std::byte> raw_transaction)
    {
        ErrorOut error;
        auto* parsed{ParseC(raw_transaction, error)};
        if (!parsed) {
            if (error.has_error()) {
                throw_api_error(error, "failed to parse transaction");
            }
            return std::nullopt;
        }
        return Transaction{parsed};
    }

    explicit Transaction(btck_Transaction* transaction)
        : Handle{transaction} {}

    Transaction(const TransactionView& view)
        : Handle{view} {}
};

class PrecomputedTransactionData : public Handle<btck_PrecomputedTransactionData, btck_precomputed_transaction_data_copy, btck_precomputed_transaction_data_destroy>
{
public:
    explicit PrecomputedTransactionData(const Transaction& tx_to, std::span<const TransactionOutput> spent_outputs)
        : Handle{[&] {
              ErrorOut error;
              return check(
                  btck_precomputed_transaction_data_create(
                      tx_to.get(),
                      reinterpret_cast<const btck_TransactionOutput**>(
                          const_cast<TransactionOutput*>(spent_outputs.data())),
                      spent_outputs.size(),
                      error.out()),
                  error,
                  "failed to create precomputed transaction data");
          }()} {}
};

template <typename Derived>
bool ScriptPubkeyApi<Derived>::Verify(int64_t amount,
                                      const Transaction& tx_to,
                                      const PrecomputedTransactionData* precomputed_txdata,
                                      unsigned int input_index,
                                      ScriptVerificationFlags flags,
                                      ScriptVerifyStatus& status) const
{
    ErrorOut error;
    auto result = btck_script_pubkey_verify(
        impl(),
        amount,
        tx_to.get(),
        precomputed_txdata ? precomputed_txdata->get() : nullptr,
        input_index,
        static_cast<btck_ScriptVerificationFlags>(flags),
        reinterpret_cast<btck_ScriptVerifyStatus*>(&status),
        error.out());
    if (error.has_error()) {
        throw_api_error(error, "failed to verify script pubkey");
    }
    return result == 1;
}

template <typename Derived>
class BlockHashApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    bool operator==(const Derived& other) const
    {
        return btck_block_hash_equals(impl(), other.get()) != 0;
    }

    bool operator!=(const Derived& other) const
    {
        return btck_block_hash_equals(impl(), other.get()) == 0;
    }

    std::array<std::byte, 32> ToBytes() const
    {
        std::array<std::byte, 32> hash;
        btck_block_hash_to_bytes(impl(), reinterpret_cast<unsigned char*>(hash.data()));
        return hash;
    }
};

class BlockHashView : public View<btck_BlockHash>, public BlockHashApi<BlockHashView>
{
public:
    explicit BlockHashView(const btck_BlockHash* ptr) : View{ptr} {}
};

class BlockHash : public Handle<btck_BlockHash, btck_block_hash_copy, btck_block_hash_destroy>, public BlockHashApi<BlockHash>
{
public:
    explicit BlockHash(const std::array<std::byte, 32>& hash)
        : Handle{[&] {
              ErrorOut error;
              return check(btck_block_hash_create(reinterpret_cast<const unsigned char*>(hash.data()), error.out()), error, "failed to create block hash");
          }()} {}

    explicit BlockHash(btck_BlockHash* hash)
        : Handle{hash} {}

    BlockHash(const BlockHashView& view)
        : Handle{view} {}
};

template <typename Derived>
class BlockHeaderApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    BlockHeaderApi() = default;

public:
    BlockHash Hash() const
    {
        ErrorOut error;
        return BlockHash{check(btck_block_header_get_hash(impl(), error.out()), error, "failed to hash block header")};
    }

    BlockHash PrevHash() const
    {
        ErrorOut error;
        return BlockHash{check(btck_block_header_get_prev_hash(impl(), error.out()), error, "failed to copy previous block hash")};
    }

    uint32_t Timestamp() const
    {
        return btck_block_header_get_timestamp(impl());
    }

    uint32_t Bits() const
    {
        return btck_block_header_get_bits(impl());
    }

    int32_t Version() const
    {
        return btck_block_header_get_version(impl());
    }

    uint32_t Nonce() const
    {
        return btck_block_header_get_nonce(impl());
    }

    std::array<std::byte, 80> ToBytes() const
    {
        std::array<std::byte, 80> header;
        const auto bytes{write_bytes(impl(), btck_block_header_serialize)};
        if (bytes.size() != header.size()) {
            throw std::runtime_error("Failed to serialize block header");
        }
        std::ranges::copy(bytes, header.begin());
        return header;
    }
};

class BlockHeaderView : public View<btck_BlockHeader>, public BlockHeaderApi<BlockHeaderView>
{
public:
    explicit BlockHeaderView(const btck_BlockHeader* ptr) : View{ptr} {}
};

class BlockHeader : public Handle<btck_BlockHeader, btck_block_header_copy, btck_block_header_destroy>, public BlockHeaderApi<BlockHeader>
{
    static btck_BlockHeader* ParseC(std::span<const std::byte> raw_header, ErrorOut& error)
    {
        UniqueHandle<btck_BlockHeaderParseResult, btck_block_header_parse_result_destroy> result{
            check(btck_block_header_parse_result(raw_header.data(), raw_header.size(), error.out()), error, "failed to parse block header")};
        if (btck_block_header_parse_result_get_status(result.get()) == btck_ParseStatus_MALFORMED) {
            return nullptr;
        }
        return check(btck_block_header_copy(
                         check(btck_block_header_parse_result_get_header(result.get())),
                         error.out()),
                     error, "failed to copy parsed block header");
    }

public:
    static std::optional<BlockHeader> Parse(std::span<const std::byte> raw_header)
    {
        ErrorOut error;
        auto* parsed{ParseC(raw_header, error)};
        if (!parsed) {
            if (error.has_error()) {
                throw_api_error(error, "failed to parse block header");
            }
            return std::nullopt;
        }
        return BlockHeader{parsed};
    }

    BlockHeader(const BlockHeaderView& view)
        : Handle{view} {}

    explicit BlockHeader(btck_BlockHeader* header)
        : Handle{header} {}
};

class ConsensusParamsView : public View<btck_ConsensusParams>
{
public:
    explicit ConsensusParamsView(const btck_ConsensusParams* ptr) : View{ptr} {}
};

template <typename Derived>
class BlockApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    size_t CountTransactions() const
    {
        return btck_block_count_transactions(impl());
    }

    TransactionView GetTransaction(size_t index) const
    {
        return TransactionView{btck_block_get_transaction_at(impl(), index)};
    }

    MAKE_RANGE_METHOD(Transactions, Derived, &Derived::CountTransactions, &Derived::GetTransaction, *static_cast<const Derived*>(this))

    BlockHash GetHash() const
    {
        ErrorOut error;
        return BlockHash{check(btck_block_get_hash(impl(), error.out()), error, "failed to hash block")};
    }

    BlockHeader GetHeader() const
    {
        ErrorOut error;
        return BlockHeader{check(btck_block_get_header(impl(), error.out()), error, "failed to copy block header")};
    }

    std::vector<std::byte> ToBytes() const
    {
        return write_bytes(impl(), btck_block_serialize);
    }
};

class BlockView : public View<btck_Block>, public BlockApi<BlockView>
{
public:
    explicit BlockView(const btck_Block* block) : View{block} {}
};

class Block : public Handle<btck_Block, btck_block_copy, btck_block_destroy>, public BlockApi<Block>
{
    static btck_Block* ParseC(std::span<const std::byte> raw_block, ErrorOut& error)
    {
        UniqueHandle<btck_BlockParseResult, btck_block_parse_result_destroy> result{
            check(btck_block_parse_result(raw_block.data(), raw_block.size(), error.out()), error, "failed to parse block")};
        if (btck_block_parse_result_get_status(result.get()) == btck_ParseStatus_MALFORMED) {
            return nullptr;
        }
        return check(btck_block_copy(
                         check(btck_block_parse_result_get_block(result.get())),
                         error.out()),
                     error, "failed to copy parsed block");
    }

public:
    static std::optional<Block> Parse(std::span<const std::byte> raw_block)
    {
        ErrorOut error;
        auto* parsed{ParseC(raw_block, error)};
        if (!parsed) {
            if (error.has_error()) {
                throw_api_error(error, "failed to parse block");
            }
            return std::nullopt;
        }
        return Block{parsed};
    }

    explicit Block(btck_Block* block) : Handle{block} {}
    Block(const BlockView& block) : Handle{block} {}

    bool Check(const ConsensusParamsView& consensus_params,
               BlockCheckFlags flags,
               BlockValidationState& state) const;
};

inline void logging_disable()
{
    btck_logging_disable();
}

inline void logging_set_options(const btck_LoggingOptions& logging_options)
{
    btck_logging_set_options(logging_options);
}

inline void logging_set_level_category(LogCategory category, LogLevel level)
{
    btck_logging_set_level_category(static_cast<btck_LogCategory>(category), static_cast<btck_LogLevel>(level));
}

inline void logging_enable_category(LogCategory category)
{
    btck_logging_enable_category(static_cast<btck_LogCategory>(category));
}

inline void logging_disable_category(LogCategory category)
{
    btck_logging_disable_category(static_cast<btck_LogCategory>(category));
}

template <typename T>
concept Log = requires(T a, std::string_view message) {
    { a.LogMessage(message) } -> std::same_as<void>;
};

template <Log T>
class Logger : UniqueHandle<btck_LoggingConnection, btck_logging_connection_destroy>
{
    static btck_LoggingConnection* Create(std::unique_ptr<T> log)
    {
        ErrorOut error;
        T* user_data{log.get()};
        auto connection{btck_logging_connection_create(
            +[](void* user_data, const char* message, size_t message_len) { static_cast<T*>(user_data)->LogMessage({message, message_len}); },
            user_data,
            +[](void* user_data) { delete static_cast<T*>(user_data); },
            error.out())};
        if (!connection) {
            throw_api_error(error, "failed to create logging connection");
        }
        log.release();
        return connection;
    }

public:
    Logger(std::unique_ptr<T> log)
        : UniqueHandle{Create(std::move(log))}
    {
    }
};

template <typename Derived>
class BlockInfoApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

public:
    bool operator==(const Derived& other) const
    {
        return btck_block_info_equals(impl(), other.get()) != 0;
    }

    bool operator!=(const Derived& other) const
    {
        return !(*this == other);
    }

    int32_t GetHeight() const
    {
        return btck_block_info_get_height(impl());
    }

    BlockHashView GetHash() const
    {
        return BlockHashView{btck_block_info_get_block_hash(impl())};
    }

    std::optional<BlockHashView> PreviousHash() const
    {
        const auto* hash{btck_block_info_get_previous_block_hash(impl())};
        if (!hash) return std::nullopt;
        return BlockHashView{hash};
    }

    BlockHeader GetHeader() const
    {
        return BlockHeader{BlockHeaderView{btck_block_info_get_header(impl())}};
    }
};

class BlockInfoView : public View<btck_BlockInfo>, public BlockInfoApi<BlockInfoView>
{
public:
    explicit BlockInfoView(const btck_BlockInfo* info) : View{info} {}
};

class BlockInfo : public Handle<btck_BlockInfo, btck_block_info_copy, btck_block_info_destroy>, public BlockInfoApi<BlockInfo>
{
public:
    explicit BlockInfo(btck_BlockInfo* info) : Handle{info} {}
    explicit BlockInfo(const BlockInfoView& info) : Handle{info} {}
};

struct KernelNotifications {
    std::function<void(SynchronizationState state, BlockInfoView tip, double verification_progress)> block_tip;
    std::function<void(SynchronizationState state, int64_t height, int64_t timestamp, bool presync)> header_tip;
    std::function<void(std::string_view title, int progress_percent, bool resume_possible)> progress;
    std::function<void(Warning warning, std::string_view message)> warning_set;
    std::function<void(Warning warning)> warning_unset;
    std::function<void(std::string_view error)> flush_error;
    std::function<void(std::string_view error)> fatal_error;
};

template <typename F>
int callback_status(F&& fn) noexcept
{
    try {
        std::forward<F>(fn)();
        return 0;
    } catch (...) {
        return -1;
    }
}

template <typename Derived>
class BlockValidationStateApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    BlockValidationStateApi() = default;

public:
    ValidationMode GetValidationMode() const
    {
        return static_cast<ValidationMode>(btck_block_validation_state_get_validation_mode(impl()));
    }

    BlockValidationResult GetBlockValidationResult() const
    {
        return static_cast<BlockValidationResult>(btck_block_validation_state_get_block_validation_result(impl()));
    }
};

class BlockValidationStateView : public View<btck_BlockValidationState>, public BlockValidationStateApi<BlockValidationStateView>
{
public:
    explicit BlockValidationStateView(const btck_BlockValidationState* ptr) : View{ptr} {}
};

class BlockValidationState : public Handle<btck_BlockValidationState, btck_block_validation_state_copy, btck_block_validation_state_destroy>, public BlockValidationStateApi<BlockValidationState>
{
public:
    explicit BlockValidationState()
        : Handle{[] {
              ErrorOut error;
              return check(btck_block_validation_state_create(error.out()), error, "failed to create block validation state");
          }()} {}

    explicit BlockValidationState(const BlockValidationStateView& view) : Handle{view} {}

    explicit BlockValidationState(btck_BlockValidationState* state) : Handle{state} {}
};

struct BlockProcessResult {
    BlockProcessStatus status{BlockProcessStatus::CHECK_FAILED};
    bool processed{false};
    bool new_block{false};
    BlockValidationState validation_state{};
};

struct HeaderProcessResult {
    HeaderProcessStatus status{HeaderProcessStatus::REJECTED};
    BlockValidationState validation_state{};
};

struct BlockImportResult {
    BlockImportStatus status{BlockImportStatus::COMPLETED};
    int loaded_blocks{0};
    int skipped_records{0};
    int skipped_blocks{0};
    int rejected_blocks{0};
};

inline bool IsProcessedBlockStatus(BlockProcessStatus status)
{
    return status == BlockProcessStatus::STORED ||
           status == BlockProcessStatus::ALREADY_KNOWN;
}

inline bool Block::Check(const ConsensusParamsView& consensus_params,
                         BlockCheckFlags flags,
                         BlockValidationState& state) const
{
    ErrorOut error;
    UniqueHandle<btck_BlockCheckResult, btck_block_check_result_destroy> result{
        check(btck_block_check_result(get(), consensus_params.get(), static_cast<btck_BlockCheckFlags>(flags), error.out()), error, "failed to check block")};
    state = BlockValidationState{BlockValidationStateView{btck_block_check_result_get_validation_state(result.get())}};
    return btck_block_check_result_get_status(result.get()) == btck_CheckStatus_VALID;
}

class TxValidationState : public Handle<btck_TxValidationState, btck_tx_validation_state_copy, btck_tx_validation_state_destroy>
{
public:
    explicit TxValidationState()
        : Handle{[] {
              ErrorOut error;
              return check(btck_tx_validation_state_create(error.out()), error, "failed to create transaction validation state");
          }()} {}

    explicit TxValidationState(const btck_TxValidationState* state)
        : Handle{[&] {
              ErrorOut error;
              return check(btck_tx_validation_state_copy(state, error.out()), error, "failed to copy transaction validation state");
          }()} {}

    explicit TxValidationState(btck_TxValidationState* state) : Handle{state} {}

    ValidationMode GetValidationMode() const
    {
        return static_cast<ValidationMode>(btck_tx_validation_state_get_validation_mode(get()));
    }

    TxValidationResult GetTxValidationResult() const
    {
        return static_cast<TxValidationResult>(btck_tx_validation_state_get_tx_validation_result(get()));
    }
};

inline bool CheckTransaction(const Transaction& tx, TxValidationState& state)
{
    ErrorOut error;
    UniqueHandle<btck_TransactionCheckResult, btck_transaction_check_result_destroy> result{
        check(btck_transaction_check_result(tx.get(), error.out()), error, "failed to check transaction")};
    state = TxValidationState{btck_transaction_check_result_get_validation_state(result.get())};
    return btck_transaction_check_result_get_status(result.get()) == btck_CheckStatus_VALID;
}

struct ValidationInterface {
    std::function<void(BlockView block, BlockValidationStateView state)> block_checked;
    std::function<void(BlockView block, BlockInfoView info)> pow_valid_block;
    std::function<void(BlockView block, BlockInfoView info)> block_connected;
    std::function<void(BlockView block, BlockInfoView info)> block_disconnected;
};

class ChainParams : public Handle<btck_ChainParameters, btck_chain_parameters_copy, btck_chain_parameters_destroy>
{
public:
    ChainParams(ChainType chain_type)
        : Handle{[&] {
              ErrorOut error;
              return check(btck_chain_parameters_create(static_cast<btck_ChainType>(chain_type), error.out()), error, "failed to create chain parameters");
          }()} {}

    ConsensusParamsView GetConsensusParams() const
    {
        return ConsensusParamsView{btck_chain_parameters_get_consensus_params(get())};
    }
};

class ContextOptions : public UniqueHandle<btck_ContextOptions, btck_context_options_destroy>
{
public:
    ContextOptions()
        : UniqueHandle{[] {
              ErrorOut error;
              return check(btck_context_options_create(error.out()), error, "failed to create context options");
          }()} {}

    void SetChainParams(ChainParams& chain_params)
    {
        ErrorOut error;
        check_status(btck_context_options_set_chainparams(get(), chain_params.get(), error.out()), error, "failed to set chain parameters");
    }

    void SetNotifications(KernelNotifications notifications)
    {
        auto heap_notifications = std::make_unique<KernelNotifications>(std::move(notifications));
        using user_type = KernelNotifications*;
        btck_NotificationInterfaceCallbacks callbacks{
            .user_data = heap_notifications.get(),
            .user_data_destroy = +[](void* user_data) { delete static_cast<user_type>(user_data); },
            .block_tip = +[](void* user_data, btck_SynchronizationState state, const btck_BlockInfo* tip, double verification_progress) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.block_tip) callbacks.block_tip(static_cast<SynchronizationState>(state), BlockInfoView{tip}, verification_progress);
                });
            },
            .header_tip = +[](void* user_data, btck_SynchronizationState state, int64_t height, int64_t timestamp, int presync) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.header_tip) callbacks.header_tip(static_cast<SynchronizationState>(state), height, timestamp, presync == 1);
                });
            },
            .progress = +[](void* user_data, const char* title, size_t title_len, int progress_percent, int resume_possible) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.progress) callbacks.progress({title, title_len}, progress_percent, resume_possible == 1);
                });
            },
            .warning_set = +[](void* user_data, btck_Warning warning, const char* message, size_t message_len) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.warning_set) callbacks.warning_set(static_cast<Warning>(warning), {message, message_len});
                });
            },
            .warning_unset = +[](void* user_data, btck_Warning warning) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.warning_unset) callbacks.warning_unset(static_cast<Warning>(warning));
                });
            },
            .flush_error = +[](void* user_data, const char* error, size_t error_len) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.flush_error) callbacks.flush_error({error, error_len});
                });
            },
            .fatal_error = +[](void* user_data, const char* error, size_t error_len) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.fatal_error) callbacks.fatal_error({error, error_len});
                });
            },
        };
        ErrorOut error;
        check_status(btck_context_options_set_notifications(get(), callbacks, error.out()), error, "failed to set kernel notifications");
        heap_notifications.release();
    }

    void SetValidationInterface(ValidationInterface validation_interface)
    {
        auto heap_vi = std::make_unique<ValidationInterface>(std::move(validation_interface));
        using user_type = ValidationInterface*;
        btck_ValidationInterfaceCallbacks callbacks{
            .user_data = heap_vi.get(),
            .user_data_destroy = +[](void* user_data) { delete static_cast<user_type>(user_data); },
            .block_checked = +[](void* user_data, const btck_Block* block, const btck_BlockValidationState* state) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.block_checked) callbacks.block_checked(BlockView{block}, BlockValidationStateView{state});
                });
            },
            .pow_valid_block = +[](void* user_data, const btck_Block* block, const btck_BlockInfo* info) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.pow_valid_block) callbacks.pow_valid_block(BlockView{block}, BlockInfoView{info});
                });
            },
            .block_connected = +[](void* user_data, const btck_Block* block, const btck_BlockInfo* info) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.block_connected) callbacks.block_connected(BlockView{block}, BlockInfoView{info});
                });
            },
            .block_disconnected = +[](void* user_data, const btck_Block* block, const btck_BlockInfo* info) -> int {
                return callback_status([&] {
                    auto& callbacks{*static_cast<user_type>(user_data)};
                    if (callbacks.block_disconnected) callbacks.block_disconnected(BlockView{block}, BlockInfoView{info});
                });
            },
        };
        ErrorOut error;
        check_status(btck_context_options_set_validation_interface(get(), callbacks, error.out()), error, "failed to set validation interface");
        heap_vi.release();
    }
};

class Context : public Handle<btck_Context, btck_context_copy, btck_context_destroy>
{
    static btck_Context* Create(const btck_ContextOptions* options)
    {
        ErrorOut error;
        return check(btck_context_create(options, error.out()), error, "failed to create context");
    }

public:
    Context(ContextOptions& opts)
        : Handle{Create(opts.get())} {}

    Context()
        : Handle{Create(ContextOptions{}.get())} {}

    bool interrupt()
    {
        ErrorOut error;
        check_status(btck_context_interrupt(get(), error.out()), error, "failed to interrupt context");
        return true;
    }
};

class ChainstateOptions : public UniqueHandle<btck_ChainstateOptions, btck_chainstate_options_destroy>
{
    static btck_ChainstateOptions* Create(const Context& context, std::string_view data_dir, std::string_view blocks_dir)
    {
        ErrorOut error;
        return check(
            btck_chainstate_options_create(
                context.get(), data_dir.data(), data_dir.length(), blocks_dir.data(), blocks_dir.length(), error.out()),
            error,
            "failed to create chainstate options");
    }

public:
    ChainstateOptions(const Context& context, std::string_view data_dir, std::string_view blocks_dir)
        : UniqueHandle{Create(context, data_dir, blocks_dir)}
    {
    }

    void SetWorkerThreads(int worker_threads)
    {
        ErrorOut error;
        check_status(btck_chainstate_options_set_worker_threads_num(get(), worker_threads, error.out()), error, "failed to set worker thread count");
    }

    bool SetWipeState(bool reindex_block_files, bool wipe_chainstate)
    {
        ErrorOut error;
        if (btck_chainstate_options_set_wipe_state(get(), reindex_block_files, wipe_chainstate, error.out()) == 0) {
            return true;
        }
        if (error.code() == ErrorCode::INVALID_ARGUMENT) {
            return false;
        }
        throw_api_error(error, "failed to set chainstate wipe options");
    }

    void SetInMemory(bool in_memory)
    {
        ErrorOut error;
        check_status(btck_chainstate_options_set_in_memory(get(), in_memory, error.out()), error, "failed to set chainstate memory option");
    }
};

class ChainstateRuntime : public UniqueHandle<btck_ChainstateRuntime, btck_chainstate_runtime_destroy>
{
public:
    ChainstateRuntime()
        : UniqueHandle{[] {
              ErrorOut error;
              return check(btck_chainstate_runtime_create(error.out()), error, "failed to create chainstate runtime inputs");
          }()}
    {
    }

    void SetCurrentTime(int64_t timestamp)
    {
        ErrorOut error;
        check_status(btck_chainstate_runtime_set_current_time(get(), timestamp, error.out()), error, "failed to set chainstate current time");
    }
};

class BlockValidationOptions : public UniqueHandle<btck_BlockValidationOptions, btck_block_validation_options_destroy>
{
public:
    BlockValidationOptions()
        : UniqueHandle{[] {
              ErrorOut error;
              return check(btck_block_validation_options_create(error.out()), error, "failed to create block validation options");
          }()}
    {
    }

    void SetCurrentTime(int64_t timestamp)
    {
        ErrorOut error;
        check_status(btck_block_validation_options_set_current_time(get(), timestamp, error.out()), error, "failed to set block validation current time");
    }
};

class ChainSnapshot : public Handle<btck_ChainSnapshot, btck_chain_snapshot_copy, btck_chain_snapshot_destroy>
{
public:
    explicit ChainSnapshot(btck_ChainSnapshot* ptr) : Handle{ptr} {}

    int32_t Height() const
    {
        return btck_chain_snapshot_get_height(get());
    }

    int32_t CountEntries() const
    {
        return static_cast<int32_t>(btck_chain_snapshot_count(get()));
    }

    BlockInfoView GetByHeight(int32_t height) const
    {
        auto info{btck_chain_snapshot_get_block_info_by_height(get(), height)};
        if (!info) throw std::runtime_error("No block info in the chain snapshot at the provided height");
        return BlockInfoView{info};
    }

    bool ContainsHash(const BlockHashView& hash) const
    {
        return btck_chain_snapshot_contains_block_hash(get(), hash.get()) != 0;
    }

    bool Contains(const BlockInfoView& info) const
    {
        return ContainsHash(info.GetHash());
    }

    bool Contains(const BlockInfo& info) const
    {
        return Contains(BlockInfoView{info.get()});
    }

    MAKE_RANGE_METHOD(Entries, ChainSnapshot, &ChainSnapshot::CountEntries, &ChainSnapshot::GetByHeight, *this)
};

template <typename Derived>
class CoinApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    CoinApi() = default;

public:
    uint32_t GetConfirmationHeight() const { return btck_coin_confirmation_height(impl()); }

    bool IsCoinbase() const { return btck_coin_is_coinbase(impl()) == 1; }

    TransactionOutputView GetOutput() const
    {
        return TransactionOutputView{btck_coin_get_output(impl())};
    }
};

class CoinView : public View<btck_Coin>, public CoinApi<CoinView>
{
public:
    explicit CoinView(const btck_Coin* ptr) : View{ptr} {}
};

class Coin : public Handle<btck_Coin, btck_coin_copy, btck_coin_destroy>, public CoinApi<Coin>
{
public:
    Coin(btck_Coin* coin) : Handle{coin} {}

    Coin(const CoinView& view) : Handle{view} {}
};

template <typename Derived>
class TransactionSpentOutputsApi
{
private:
    auto impl() const
    {
        return static_cast<const Derived*>(this)->get();
    }

    friend Derived;
    TransactionSpentOutputsApi() = default;

public:
    size_t Count() const
    {
        return btck_transaction_spent_outputs_count(impl());
    }

    CoinView GetCoin(size_t index) const
    {
        return CoinView{btck_transaction_spent_outputs_get_coin_at(impl(), index)};
    }

    MAKE_RANGE_METHOD(Coins, Derived, &TransactionSpentOutputsApi<Derived>::Count, &TransactionSpentOutputsApi<Derived>::GetCoin, *static_cast<const Derived*>(this))
};

class TransactionSpentOutputsView : public View<btck_TransactionSpentOutputs>, public TransactionSpentOutputsApi<TransactionSpentOutputsView>
{
public:
    explicit TransactionSpentOutputsView(const btck_TransactionSpentOutputs* ptr) : View{ptr} {}
};

class TransactionSpentOutputs : public Handle<btck_TransactionSpentOutputs, btck_transaction_spent_outputs_copy, btck_transaction_spent_outputs_destroy>,
                                public TransactionSpentOutputsApi<TransactionSpentOutputs>
{
public:
    TransactionSpentOutputs(btck_TransactionSpentOutputs* transaction_spent_outputs) : Handle{transaction_spent_outputs} {}

    TransactionSpentOutputs(const TransactionSpentOutputsView& view) : Handle{view} {}
};

class BlockSpentOutputs : public Handle<btck_BlockSpentOutputs, btck_block_spent_outputs_copy, btck_block_spent_outputs_destroy>
{
public:
    BlockSpentOutputs(btck_BlockSpentOutputs* block_spent_outputs)
        : Handle{block_spent_outputs}
    {
    }

    size_t Count() const
    {
        return btck_block_spent_outputs_count(get());
    }

    TransactionSpentOutputsView GetTxSpentOutputs(size_t tx_undo_index) const
    {
        return TransactionSpentOutputsView{btck_block_spent_outputs_get_transaction_spent_outputs_at(get(), tx_undo_index)};
    }

    MAKE_RANGE_METHOD(TxsSpentOutputs, BlockSpentOutputs, &BlockSpentOutputs::Count, &BlockSpentOutputs::GetTxSpentOutputs, *this)
};

class Chainstate : UniqueHandle<btck_Chainstate, btck_chainstate_destroy>
{
    static btck_Chainstate* Create(const ChainstateOptions& chainman_opts, const ChainstateRuntime& runtime_options)
    {
        ErrorOut error;
        return check(
            btck_chainstate_open(chainman_opts.get(), runtime_options.get(), error.out()),
            error,
            "failed to open chainstate");
    }

public:
    using UniqueHandle::get;

    Chainstate(const ChainstateOptions& chainman_opts, const ChainstateRuntime& runtime_options)
        : UniqueHandle{Create(chainman_opts, runtime_options)}
    {
    }

    BlockImportResult ImportBlocks(const std::span<const std::string> paths, const ChainstateRuntime& runtime_options)
    {
        std::vector<const char*> c_paths;
        std::vector<size_t> c_paths_lens;
        c_paths.reserve(paths.size());
        c_paths_lens.reserve(paths.size());
        for (const auto& path : paths) {
            c_paths.push_back(path.c_str());
            c_paths_lens.push_back(path.length());
        }

        ErrorOut error;
        UniqueHandle<btck_BlockImportResult, btck_block_import_result_destroy> result{
            check(btck_chainstate_import_blocks_result(get(), c_paths.data(), c_paths_lens.data(), c_paths.size(), runtime_options.get(), error.out()),
                  error,
                  "failed to import blocks")};
        return {
            .status = static_cast<BlockImportStatus>(btck_block_import_result_get_status(result.get())),
            .loaded_blocks = btck_block_import_result_get_loaded_block_count(result.get()),
            .skipped_records = btck_block_import_result_get_skipped_record_count(result.get()),
            .skipped_blocks = btck_block_import_result_get_skipped_block_count(result.get()),
            .rejected_blocks = btck_block_import_result_get_rejected_block_count(result.get()),
        };
    }

    BlockProcessResult ProcessBlock(const Block& block, const BlockValidationOptions& options)
    {
        ErrorOut error;
        UniqueHandle<btck_BlockProcessResult, btck_block_process_result_destroy> result{
            check(btck_chainstate_process_block_result(get(), block.get(), options.get(), error.out()), error, "failed to process block")};
        const auto status{static_cast<BlockProcessStatus>(btck_block_process_result_get_status(result.get()))};
        return {
            .status = status,
            .processed = IsProcessedBlockStatus(status),
            .new_block = btck_block_process_result_has_new_block_data(result.get()) == 1,
            .validation_state = BlockValidationState{BlockValidationStateView{btck_block_process_result_get_validation_state(result.get())}},
        };
    }

    HeaderProcessResult ProcessBlockHeader(const BlockHeader& header, const BlockValidationOptions& options)
    {
        ErrorOut error;
        UniqueHandle<btck_HeaderProcessResult, btck_header_process_result_destroy> result{
            check(btck_chainstate_process_header_result(get(), header.get(), options.get(), error.out()), error, "failed to process block header")};
        return {
            .status = static_cast<HeaderProcessStatus>(btck_header_process_result_get_status(result.get())),
            .validation_state = BlockValidationState{BlockValidationStateView{btck_header_process_result_get_validation_state(result.get())}},
        };
    }

    ChainSnapshot SnapshotActiveChain() const
    {
        ErrorOut error;
        return ChainSnapshot{check(btck_chainstate_snapshot_active_chain(get(), error.out()), error, "failed to snapshot active chain")};
    }

    std::optional<BlockInfo> GetBlockInfo(const BlockHashView& block_hash) const
    {
        ErrorOut error;
        auto info{btck_chainstate_get_block_info(get(), block_hash.get(), error.out())};
        if (error.has_error()) {
            throw_api_error(error, "failed to get block info");
        }
        if (!info) return std::nullopt;
        return BlockInfo{info};
    }

    std::optional<BlockInfo> GetBlockInfo(const BlockHash& block_hash) const
    {
        return GetBlockInfo(BlockHashView{block_hash.get()});
    }

    std::optional<BlockInfo> GetBestHeaderInfo() const
    {
        ErrorOut error;
        auto info{btck_chainstate_get_best_header_info(get(), error.out())};
        if (error.has_error()) {
            throw_api_error(error, "failed to get best header info");
        }
        if (!info) return std::nullopt;
        return BlockInfo{info};
    }

    std::optional<Block> ReadBlockByHash(const BlockHashView& block_hash) const
    {
        ErrorOut error;
        UniqueHandle<btck_BlockReadResult, btck_block_read_result_destroy> result{
            check(btck_chainstate_read_block_result(get(), block_hash.get(), error.out()), error, "failed to read block")};
        if (btck_block_read_result_get_status(result.get()) != btck_BlockReadStatus_FOUND) {
            return std::nullopt;
        }
        return Block{check(btck_block_copy(
                               check(btck_block_read_result_get_block(result.get())),
                               error.out()),
                           error, "failed to copy read block")};
    }

    std::optional<Block> ReadBlock(const BlockInfoView& info) const
    {
        return ReadBlockByHash(info.GetHash());
    }

    std::optional<Block> ReadBlock(const BlockInfo& info) const
    {
        return ReadBlock(BlockInfoView{info.get()});
    }

    std::optional<BlockSpentOutputs> ReadBlockSpentOutputsByHash(const BlockHashView& block_hash) const
    {
        ErrorOut error;
        UniqueHandle<btck_BlockSpentOutputsReadResult, btck_block_spent_outputs_read_result_destroy> result{
            check(btck_chainstate_read_block_spent_outputs_result(get(), block_hash.get(), error.out()), error, "failed to read block spent outputs")};
        if (btck_block_spent_outputs_read_result_get_status(result.get()) != btck_BlockSpentOutputsReadStatus_FOUND) {
            return std::nullopt;
        }
        return BlockSpentOutputs{check(btck_block_spent_outputs_copy(
                                           check(btck_block_spent_outputs_read_result_get_spent_outputs(result.get())),
                                           error.out()),
                                       error, "failed to copy block spent outputs")};
    }

    std::optional<BlockSpentOutputs> ReadBlockSpentOutputs(const BlockInfoView& info) const
    {
        return ReadBlockSpentOutputsByHash(info.GetHash());
    }

    std::optional<BlockSpentOutputs> ReadBlockSpentOutputs(const BlockInfo& info) const
    {
        return ReadBlockSpentOutputs(BlockInfoView{info.get()});
    }
};

} // namespace btck

#endif // BITCOIN_KERNEL_BITCOINKERNEL_WRAPPER_H
