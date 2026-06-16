// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The bitcoin-chainstate executable serves to surface the dependencies required
// by a program wishing to use Bitcoin Core's consensus engine as it is right
// now.
//
// DEVELOPER NOTE: Since this is a "demo-only", experimental, etc. executable,
//                 it may diverge from Bitcoin Core's coding style.
//
// It is part of the libbitcoinkernel project.

#include <kernel/bitcoinkernel_wrapper.h>

#include <cassert>
#include <charconv>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace btck;

std::vector<std::byte> hex_string_to_byte_vec(std::string_view hex)
{
    std::vector<std::byte> bytes;
    bytes.reserve(hex.length() / 2);

    for (size_t i{0}; i < hex.length(); i += 2) {
        uint8_t byte_value;
        auto [ptr, ec] = std::from_chars(hex.data() + i, hex.data() + i + 2, byte_value, 16);

        if (ec != std::errc{} || ptr != hex.data() + i + 2) {
            throw std::invalid_argument("Invalid hex character");
        }
        bytes.push_back(static_cast<std::byte>(byte_value));
    }
    return bytes;
}

class KernelLog
{
public:
    void LogMessage(std::string_view message)
    {
        std::cout << "kernel: " << message;
    }
};

ValidationInterface MakeValidationInterface()
{
    ValidationInterface callbacks;
    callbacks.block_checked = [](BlockView block, BlockValidationStateView state) {
        auto mode{state.GetValidationMode()};
        switch (mode) {
        case ValidationMode::VALID: {
            std::cout << "Valid block" << std::endl;
            return;
        }
        case ValidationMode::INVALID: {
            std::cout << "Invalid block: ";
            auto result{state.GetBlockValidationResult()};
            switch (result) {
            case BlockValidationResult::UNSET:
                std::cout << "initial value. Block has not yet been rejected" << std::endl;
                break;
            case BlockValidationResult::HEADER_LOW_WORK:
                std::cout << "the block header may be on a too-little-work chain" << std::endl;
                break;
            case BlockValidationResult::CONSENSUS:
                std::cout << "invalid by consensus rules" << std::endl;
                break;
            case BlockValidationResult::CACHED_INVALID:
                std::cout << "this block was cached as being invalid and we didn't store the reason why" << std::endl;
                break;
            case BlockValidationResult::INVALID_HEADER:
                std::cout << "invalid proof of work or time too old" << std::endl;
                break;
            case BlockValidationResult::MUTATED:
                std::cout << "the block's data didn't match the data committed to by the PoW" << std::endl;
                break;
            case BlockValidationResult::MISSING_PREV:
                std::cout << "We don't have the previous block the checked one is built on" << std::endl;
                break;
            case BlockValidationResult::INVALID_PREV:
                std::cout << "A block this one builds on is invalid" << std::endl;
                break;
            case BlockValidationResult::TIME_FUTURE:
                std::cout << "block timestamp was > 2 hours in the future (or our clock is bad)" << std::endl;
                break;
            }
            return;
        }
        case ValidationMode::INTERNAL_ERROR: {
            std::cout << "Internal error" << std::endl;
            return;
        }
        }
    };
    return callbacks;
}

KernelNotifications MakeNotifications()
{
    KernelNotifications notifications;
    notifications.block_tip = [](SynchronizationState, BlockInfoView, double) {
        std::cout << "Block tip changed" << std::endl;
    };
    notifications.progress = [](std::string_view title, int progress_percent, bool resume_possible) {
        std::cout << "Made progress: " << title << " " << progress_percent << "%" << std::endl;
    };
    notifications.warning_set = [](Warning warning, std::string_view message) {
        std::cout << message << std::endl;
    };
    notifications.warning_unset = [](Warning warning) {
        std::cout << "Warning unset: " << static_cast<std::underlying_type_t<Warning>>(warning) << std::endl;
    };
    notifications.flush_error = [](std::string_view error) {
        std::cout << error << std::endl;
    };
    notifications.fatal_error = [](std::string_view error) {
        std::cout << error << std::endl;
    };
    return notifications;
}

int main(int argc, char* argv[])
{
    // SETUP: Argument parsing and handling
    const bool has_regtest_flag{argc == 3 && std::string(argv[1]) == "-regtest"};
    if (argc < 2 || argc > 3 || (argc == 3 && !has_regtest_flag)) {
        std::cerr
            << "Usage: " << argv[0] << " [-regtest] DATADIR" << std::endl
            << "Display DATADIR information, and process hex-encoded blocks on standard input." << std::endl
            << "Uses mainnet parameters by default, regtest with -regtest flag" << std::endl
            << std::endl
            << "IMPORTANT: THIS EXECUTABLE IS EXPERIMENTAL, FOR TESTING ONLY, AND EXPECTED TO" << std::endl
            << "           BREAK IN FUTURE VERSIONS. DO NOT USE ON YOUR ACTUAL DATADIR." << std::endl;
        return 1;
    }
    std::filesystem::path abs_datadir{std::filesystem::absolute(argv[argc-1])};
    std::filesystem::create_directories(abs_datadir);

    btck_LoggingOptions logging_options = {
        .log_timestamps = true,
        .log_time_micros = false,
        .log_threadnames = false,
        .log_sourcelocations = false,
        .always_print_category_levels = true,
    };

    logging_set_options(logging_options);

    Logger logger{std::make_unique<KernelLog>()};

    ContextOptions options{};
    ChainParams params{has_regtest_flag ? ChainType::REGTEST : ChainType::MAINNET};
    options.SetChainParams(params);

    options.SetNotifications(MakeNotifications());
    options.SetValidationInterface(MakeValidationInterface());

    Context context{options};

    ChainstateOptions chainman_opts{context, abs_datadir.string(), (abs_datadir / "blocks").string()};
    chainman_opts.SetWorkerThreads(4);
    ChainstateRuntime runtime_options;
    runtime_options.SetCurrentTime(std::time(nullptr));

    std::unique_ptr<Chainstate> chainman;
    try {
        chainman = std::make_unique<Chainstate>(chainman_opts, runtime_options);
    } catch (std::exception&) {
        std::cerr << "Failed to instantiate Chainstate, exiting" << std::endl;
        return 1;
    }

    std::cout << "Enter the block you want to validate on the next line:" << std::endl;

    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty()) {
            std::cerr << "Empty line found, try again:" << std::endl;
            continue;
        }

        auto raw_block{hex_string_to_byte_vec(line)};
        auto block{Block::Parse(raw_block)};
        if (!block) {
            std::cerr << "Block decode failed, try again:" << std::endl;
            continue;
        }

        BlockValidationOptions validation_options;
        validation_options.SetCurrentTime(std::time(nullptr));
        const BlockProcessResult process_result{chainman->ProcessBlock(*block, validation_options)};
        if (process_result.processed) {
            std::cerr << "Block has not yet been rejected" << std::endl;
        } else {
            std::cerr << "Block was not accepted" << std::endl;
        }
        if (!process_result.new_block) {
            std::cerr << "Block is a duplicate" << std::endl;
        }
    }
}
