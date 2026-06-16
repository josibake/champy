// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_KERNEL_CHAINSTATE_H
#define BITCOIN_BITCOIN_KERNEL_CHAINSTATE_H

#include <bitcoin/kernel/context.h>
#include <bitcoin/kernel/result.h>
#include <bitcoin/protocol/block.h>
#include <bitcoin/protocol/block_header.h>
#include <bitcoin/protocol/coin_index.h>
#include <bitcoin/protocol/hash.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bitcoin::kernel {

class chainstate_options
{
public:
    [[nodiscard]] static operation_result<chainstate_options> from_directories(
        std::filesystem::path data_directory,
        std::filesystem::path blocks_directory);

    [[nodiscard]] const std::filesystem::path& data_directory() const noexcept { return m_data_directory; }
    [[nodiscard]] const std::filesystem::path& blocks_directory() const noexcept { return m_blocks_directory; }

    [[nodiscard]] bool in_memory() const noexcept { return m_in_memory; }
    void set_in_memory(bool value) noexcept { m_in_memory = value; }

    [[nodiscard]] bool wipe_chainstate() const noexcept { return m_wipe_chainstate; }
    void set_wipe_chainstate(bool value) noexcept { m_wipe_chainstate = value; }

    [[nodiscard]] bool reindex_blocks() const noexcept { return m_reindex_blocks; }
    void set_reindex_blocks(bool value) noexcept { m_reindex_blocks = value; }

private:
    chainstate_options(std::filesystem::path data_directory, std::filesystem::path blocks_directory);

    std::filesystem::path m_data_directory;
    std::filesystem::path m_blocks_directory;
    bool m_in_memory{false};
    bool m_wipe_chainstate{false};
    bool m_reindex_blocks{false};
};

class chainstate_runtime
{
public:
    constexpr explicit chainstate_runtime(operation_time current_time) noexcept : m_current_time{current_time} {}

    [[nodiscard]] constexpr operation_time current_time() const noexcept { return m_current_time; }

private:
    operation_time m_current_time;
};

struct block_info {
    bitcoin::block_hash hash;
    std::int32_t height{0};
    bitcoin::block_hash previous_hash;
    bool active{false};

    friend bool operator==(const block_info&, const block_info&) noexcept = default;
};

class chain_snapshot
{
public:
    chain_snapshot() = default;
    explicit chain_snapshot(std::vector<block_info> blocks) : m_blocks{std::move(blocks)} {}

    [[nodiscard]] const std::vector<block_info>& blocks() const noexcept { return m_blocks; }
    [[nodiscard]] bool empty() const noexcept { return m_blocks.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_blocks.size(); }

private:
    std::vector<block_info> m_blocks;
};

enum class header_process_status {
    accepted,
    rejected,
};

enum class block_process_status {
    check_failed,
    header_rejected,
    block_rejected,
    already_known,
    stored,
    unrequested_previously_processed,
    unrequested_less_work_than_tip,
    unrequested_too_far_ahead,
    unrequested_below_minimum_chain_work,
};

enum class block_import_status {
    completed,
    interrupted,
    already_importing,
    resource_limit,
};

enum class block_read_status {
    found,
    not_indexed,
    data_unavailable,
};

enum class block_spent_outputs_read_status {
    found,
    not_indexed,
    data_unavailable,
};

struct validation_state_summary {
    block_validation_result block_result{block_validation_result::unset};
    tx_validation_result transaction_result{tx_validation_result::unset};
    std::string reason;

    friend bool operator==(const validation_state_summary&, const validation_state_summary&) noexcept = default;
};

struct header_process_result {
    header_process_status status{header_process_status::rejected};
    validation_state_summary validation;

    [[nodiscard]] bool accepted() const noexcept { return status == header_process_status::accepted; }
};

struct block_process_result {
    block_process_status status{block_process_status::check_failed};
    bool has_new_block_data{false};
    validation_state_summary validation;

    [[nodiscard]] bool stored() const noexcept { return status == block_process_status::stored; }
};

struct block_import_result {
    block_import_status status{block_import_status::completed};
    std::size_t skipped_records{0};
    std::size_t skipped_blocks{0};
    std::size_t rejected_blocks{0};
};

class block_read_result
{
public:
    [[nodiscard]] static block_read_result found(bitcoin::block value);
    [[nodiscard]] static block_read_result not_indexed();
    [[nodiscard]] static block_read_result data_unavailable();

    [[nodiscard]] block_read_status status() const noexcept { return m_status; }
    [[nodiscard]] const std::optional<bitcoin::block>& block() const noexcept { return m_block; }

private:
    block_read_result(block_read_status status, std::optional<bitcoin::block> value);

    block_read_status m_status;
    std::optional<bitcoin::block> m_block;
};

struct transaction_spent_outputs {
    std::vector<bitcoin::coin> outputs;
};

class block_spent_outputs
{
public:
    block_spent_outputs() = default;
    explicit block_spent_outputs(std::vector<transaction_spent_outputs> transactions) :
        m_transactions{std::move(transactions)}
    {
    }

    [[nodiscard]] const std::vector<transaction_spent_outputs>& transactions() const noexcept { return m_transactions; }

private:
    std::vector<transaction_spent_outputs> m_transactions;
};

class block_spent_outputs_read_result
{
public:
    [[nodiscard]] static block_spent_outputs_read_result found(block_spent_outputs value);
    [[nodiscard]] static block_spent_outputs_read_result not_indexed();
    [[nodiscard]] static block_spent_outputs_read_result data_unavailable();

    [[nodiscard]] block_spent_outputs_read_status status() const noexcept { return m_status; }
    [[nodiscard]] const std::optional<block_spent_outputs>& spent_outputs() const noexcept { return m_spent_outputs; }

private:
    block_spent_outputs_read_result(block_spent_outputs_read_status status, std::optional<block_spent_outputs> value);

    block_spent_outputs_read_status m_status;
    std::optional<block_spent_outputs> m_spent_outputs;
};

class precomputed_transaction_data
{
public:
    precomputed_transaction_data() = default;
};

class chainstate
{
public:
    // Move-only chainstate owner. Mutating operations require exclusive access.
    // A moved-from chainstate is valid only for destruction or assignment.
    chainstate(chainstate&&) noexcept;
    chainstate& operator=(chainstate&&) noexcept;
    chainstate(const chainstate&) = delete;
    chainstate& operator=(const chainstate&) = delete;
    ~chainstate();

private:
    struct state;

    explicit chainstate(std::unique_ptr<state> value) noexcept;

    std::unique_ptr<state> m_state;
};

[[nodiscard]] operation_result<chainstate> open_chainstate(
    const context& ctx,
    const chainstate_options& options,
    chainstate_runtime runtime);

[[nodiscard]] operation_result<header_process_result> process_header(
    chainstate& state,
    const bitcoin::block_header& header,
    chainstate_runtime runtime);

[[nodiscard]] operation_result<block_process_result> process_block(
    chainstate& state,
    const bitcoin::block& block,
    chainstate_runtime runtime);

[[nodiscard]] operation_result<block_import_result> import_blocks(
    chainstate& state,
    chainstate_runtime runtime);

[[nodiscard]] operation_result<chain_snapshot> snapshot_active_chain(const chainstate& state);

[[nodiscard]] operation_result<block_read_result> read_block(const chainstate& state, bitcoin::block_hash hash);

[[nodiscard]] operation_result<block_spent_outputs_read_result> read_block_spent_outputs(
    const chainstate& state,
    bitcoin::block_hash hash);

} // namespace bitcoin::kernel

#endif // BITCOIN_BITCOIN_KERNEL_CHAINSTATE_H
