// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/kernel/chainstate.h>

#include <memory>
#include <utility>

namespace bitcoin::kernel {

namespace {
operation_error unsupported(std::string operation)
{
    return operation_error{
        operation_error_code::unsupported_operation,
        std::move(operation) + " is not implemented by the first bitcoin_kernel tracer-bullet target"};
}
} // namespace

chainstate_options::chainstate_options(std::filesystem::path data_directory, std::filesystem::path blocks_directory) :
    m_data_directory{std::move(data_directory)},
    m_blocks_directory{std::move(blocks_directory)}
{
}

operation_result<chainstate_options> chainstate_options::from_directories(
    std::filesystem::path data_directory,
    std::filesystem::path blocks_directory)
{
    if (data_directory.empty()) {
        return operation_result<chainstate_options>::failure(operation_error{
            operation_error_code::invalid_argument,
            "chainstate data directory is empty"});
    }
    if (blocks_directory.empty()) {
        return operation_result<chainstate_options>::failure(operation_error{
            operation_error_code::invalid_argument,
            "chainstate blocks directory is empty"});
    }
    return operation_result<chainstate_options>::success(
        chainstate_options{std::move(data_directory), std::move(blocks_directory)});
}

block_read_result::block_read_result(block_read_status status, std::optional<bitcoin::block> value) :
    m_status{status},
    m_block{std::move(value)}
{
}

block_read_result block_read_result::found(bitcoin::block value)
{
    return block_read_result{block_read_status::found, std::move(value)};
}

block_read_result block_read_result::not_indexed()
{
    return block_read_result{block_read_status::not_indexed, std::nullopt};
}

block_read_result block_read_result::data_unavailable()
{
    return block_read_result{block_read_status::data_unavailable, std::nullopt};
}

block_spent_outputs_read_result::block_spent_outputs_read_result(
    block_spent_outputs_read_status status,
    std::optional<block_spent_outputs> value) :
    m_status{status},
    m_spent_outputs{std::move(value)}
{
}

block_spent_outputs_read_result block_spent_outputs_read_result::found(block_spent_outputs value)
{
    return block_spent_outputs_read_result{block_spent_outputs_read_status::found, std::move(value)};
}

block_spent_outputs_read_result block_spent_outputs_read_result::not_indexed()
{
    return block_spent_outputs_read_result{block_spent_outputs_read_status::not_indexed, std::nullopt};
}

block_spent_outputs_read_result block_spent_outputs_read_result::data_unavailable()
{
    return block_spent_outputs_read_result{block_spent_outputs_read_status::data_unavailable, std::nullopt};
}

struct chainstate::state {
};

chainstate::chainstate(std::unique_ptr<state> value) noexcept : m_state{std::move(value)} {}
chainstate::chainstate(chainstate&&) noexcept = default;
chainstate& chainstate::operator=(chainstate&&) noexcept = default;
chainstate::~chainstate() = default;

operation_result<chainstate> open_chainstate(
    const context& ctx,
    const chainstate_options&,
    chainstate_runtime)
{
    if (ctx.interrupt_requested()) {
        return operation_result<chainstate>::failure(operation_error{
            operation_error_code::interrupted,
            "chainstate open was interrupted before it started"});
    }
    return operation_result<chainstate>::failure(unsupported("open_chainstate"));
}

operation_result<header_process_result> process_header(
    chainstate&,
    const bitcoin::block_header&,
    chainstate_runtime)
{
    return operation_result<header_process_result>::failure(unsupported("process_header"));
}

operation_result<block_process_result> process_block(
    chainstate&,
    const bitcoin::block&,
    chainstate_runtime)
{
    return operation_result<block_process_result>::failure(unsupported("process_block"));
}

operation_result<block_import_result> import_blocks(
    chainstate&,
    chainstate_runtime)
{
    return operation_result<block_import_result>::failure(unsupported("import_blocks"));
}

operation_result<chain_snapshot> snapshot_active_chain(const chainstate&)
{
    return operation_result<chain_snapshot>::failure(unsupported("snapshot_active_chain"));
}

operation_result<block_read_result> read_block(const chainstate&, bitcoin::block_hash)
{
    return operation_result<block_read_result>::failure(unsupported("read_block"));
}

operation_result<block_spent_outputs_read_result> read_block_spent_outputs(
    const chainstate&,
    bitcoin::block_hash)
{
    return operation_result<block_spent_outputs_read_result>::failure(unsupported("read_block_spent_outputs"));
}

} // namespace bitcoin::kernel
