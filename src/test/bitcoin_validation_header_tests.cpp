// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/protocol/api.h>
#include <bitcoin/validation/api.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int check(bool condition, int line) noexcept
{
    return condition ? 0 : line;
}

std::byte byte(unsigned char value) noexcept
{
    return static_cast<std::byte>(value);
}

std::vector<std::byte> bytes(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (auto value : values) {
        result.push_back(byte(value));
    }
    return result;
}

std::vector<std::byte> genesis_header_bytes()
{
    return bytes({
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
        0x29, 0xab, 0x5f, 0x49,
        0xff, 0xff, 0x00, 0x1d,
        0x1d, 0xac, 0x2b, 0x7c,
    });
}

bitcoin::proof_of_work_limit mainnet_pow_limit() noexcept
{
    std::array<std::byte, 32> limit{};
    limit[26] = byte(0xff);
    limit[27] = byte(0xff);
    return bitcoin::proof_of_work_limit{limit};
}

bitcoin::consensus_params params() noexcept
{
    bitcoin::consensus_params result;
    result.pow_limit = mainnet_pow_limit();
    return result;
}

bitcoin::validation_time operation_time(std::int64_t seconds) noexcept
{
    return bitcoin::validation_time{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
}

const bitcoin::validation_rejection& rejection(const bitcoin::verify_result<bitcoin::header_facts>& result)
{
    return result.assume_value().assume_rejection();
}

bitcoin::block_header with_time(bitcoin::block_header header, bitcoin::block_time time) noexcept
{
    return bitcoin::block_header{
        header.version(),
        header.previous_block_hash(),
        header.merkle_root(),
        time,
        header.bits(),
        header.nonce()};
}

bitcoin::block_header with_previous_hash(bitcoin::block_header header, bitcoin::block_hash previous_hash) noexcept
{
    return bitcoin::block_header{
        header.version(),
        previous_hash,
        header.merkle_root(),
        header.time(),
        header.bits(),
        header.nonce()};
}

bitcoin::block_header with_version(bitcoin::block_header header, std::int32_t version) noexcept
{
    return bitcoin::block_header{
        version,
        header.previous_block_hash(),
        header.merkle_root(),
        header.time(),
        header.bits(),
        header.nonce()};
}

bitcoin::block_header with_bits(bitcoin::block_header header, std::uint32_t bits) noexcept
{
    return bitcoin::block_header{
        header.version(),
        header.previous_block_hash(),
        header.merkle_root(),
        header.time(),
        bits,
        header.nonce()};
}

bitcoin::block_header with_nonce(bitcoin::block_header header, std::uint32_t nonce) noexcept
{
    return bitcoin::block_header{
        header.version(),
        header.previous_block_hash(),
        header.merkle_root(),
        header.time(),
        header.bits(),
        nonce};
}

bitcoin::block_header child_of(
    const bitcoin::block_header& parent,
    std::uint32_t time,
    std::uint32_t bits)
{
    return bitcoin::block_header{
        parent.version(),
        parent.hash(),
        parent.merkle_root(),
        bitcoin::block_time{time},
        bits,
        parent.nonce()};
}

std::vector<bitcoin::block_header> chain_from(
    bitcoin::block_header first,
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> times_and_bits)
{
    std::vector<bitcoin::block_header> result;
    result.reserve(times_and_bits.size() + 1);
    result.push_back(first);
    for (const auto [time, bits] : times_and_bits) {
        result.push_back(child_of(result.back(), time, bits));
    }
    return result;
}

template <typename Chain>
bitcoin::verify_result<bitcoin::header_facts> verify_header_candidate(
    const bitcoin::block_header& header,
    Chain&& ancestors,
    bitcoin::validation_time time,
    const bitcoin::consensus_params& consensus)
{
    auto checked{bitcoin::verify(header, std::forward<Chain>(ancestors), time, consensus)};
    if (!checked.has_value()) {
        if (checked.has_invalid_evidence()) {
            return bitcoin::verify_result<bitcoin::header_facts>::failed(bitcoin::operation_error::make(
                bitcoin::operation_error_code::internal_bug,
                "test helper received invalid ancestry evidence"));
        }
        return bitcoin::verify_result<bitcoin::header_facts>::failed(checked.assume_error());
    }
    return bitcoin::verify_result<bitcoin::header_facts>::ok(std::move(checked).assume_value());
}

} // namespace

int main()
{
    const auto parsed{bitcoin::parse_block_header(genesis_header_bytes())};
    if (auto failure{check(parsed.has_value(), __LINE__)}) return failure;
    const auto genesis{parsed.assume_value()};
    const auto consensus{params()};

    const std::array<bitcoin::block_header, 0> no_parent{};
    const auto valid{verify_header_candidate(genesis, std::span<const bitcoin::block_header>{no_parent}, operation_time(1231006505), consensus)};
    if (auto failure{check(valid.has_value() && valid.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().hash == genesis.hash(), __LINE__)}) return failure;

    const auto genesis_with_parent{verify_header_candidate(
        with_previous_hash(genesis, genesis.hash()),
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(genesis_with_parent.has_value() && !genesis_with_parent.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(genesis_with_parent).rule_id() == bitcoin::validation_rule_id::h01_previous_hash_parent, __LINE__)}) return failure;

    const auto bad_nonce{verify_header_candidate(
        with_nonce(genesis, 0),
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(bad_nonce.has_value() && !bad_nonce.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(bad_nonce).rule_id() == bitcoin::validation_rule_id::h02_proof_of_work, __LINE__)}) return failure;

    for (auto bits : {0x1d80ffffU, 0x01000000U, ~0x00800000U}) {
        const auto result{verify_header_candidate(
            with_bits(genesis, bits),
            std::span<const bitcoin::block_header>{no_parent},
            operation_time(1231006505),
            consensus)};
        if (auto failure{check(result.has_value() && !result.assume_value().accepted(), __LINE__)}) return failure;
        if (auto failure{check(rejection(result).rule_id() == bitcoin::validation_rule_id::h02_proof_of_work, __LINE__)}) return failure;
    }

    const auto wrong_genesis_difficulty{verify_header_candidate(
        with_bits(genesis, 0x1c0ffff0U),
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(wrong_genesis_difficulty.has_value() && !wrong_genesis_difficulty.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(wrong_genesis_difficulty).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    auto minimum_version_params{consensus};
    minimum_version_params.minimum_block_version = 2;
    const auto retired_version{verify_header_candidate(
        genesis,
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006505),
        minimum_version_params)};
    if (auto failure{check(retired_version.has_value() && !retired_version.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(retired_version).rule_id() == bitcoin::validation_rule_id::h06_retired_version, __LINE__)}) return failure;

    const auto parent{with_time(genesis, bitcoin::block_time{1231006505})};
    const std::array parents{parent};
    const auto bad_parent_link{verify_header_candidate(
        with_time(genesis, bitcoin::block_time{1231006506}),
        std::span<const bitcoin::block_header>{parents},
        operation_time(1231006506),
        consensus)};
    if (auto failure{check(bad_parent_link.has_value() && !bad_parent_link.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(bad_parent_link).rule_id() == bitcoin::validation_rule_id::h01_previous_hash_parent, __LINE__)}) return failure;

    const std::array broken_chain{parent, parent};
    const auto broken_witness{bitcoin::validate_header_context(std::span<const bitcoin::block_header>{broken_chain}, consensus)};
    if (auto failure{check(!broken_witness.has_value(), __LINE__)}) return failure;
    if (auto failure{check(broken_witness.has_invalid_evidence(), __LINE__)}) return failure;
    if (auto failure{check(!broken_witness.has_error(), __LINE__)}) return failure;
    if (auto failure{check(broken_witness.assume_invalid_evidence().code() == bitcoin::header_context_evidence_code::non_contiguous_ancestry, __LINE__)}) return failure;

    const auto broken_verify{bitcoin::verify(
        genesis,
        std::span<const bitcoin::block_header>{broken_chain},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(!broken_verify.has_value(), __LINE__)}) return failure;
    if (auto failure{check(broken_verify.has_invalid_evidence(), __LINE__)}) return failure;
    if (auto failure{check(!broken_verify.has_error(), __LINE__)}) return failure;
    if (auto failure{check(broken_verify.assume_invalid_evidence().code() == bitcoin::header_context_evidence_code::non_contiguous_ancestry, __LINE__)}) return failure;

    const std::array bad_genesis_chain{with_previous_hash(parent, parent.hash())};
    const auto bad_genesis_witness{bitcoin::validate_header_context(std::span<const bitcoin::block_header>{bad_genesis_chain}, consensus)};
    if (auto failure{check(!bad_genesis_witness.has_value(), __LINE__)}) return failure;
    if (auto failure{check(bad_genesis_witness.has_invalid_evidence(), __LINE__)}) return failure;
    if (auto failure{check(!bad_genesis_witness.has_error(), __LINE__)}) return failure;
    if (auto failure{check(bad_genesis_witness.assume_invalid_evidence().code() == bitcoin::header_context_evidence_code::genesis_parent_not_null, __LINE__)}) return failure;

    const auto throwing_ancestors{
        std::span<const bitcoin::block_header>{parents} |
        std::views::transform([](const bitcoin::block_header&) -> bitcoin::block_header {
            throw std::runtime_error{"view adapter failure"};
        })};
    static_assert(bitcoin::chain_view<decltype(throwing_ancestors)>);
    const auto throwing_context{bitcoin::validate_header_context(throwing_ancestors, consensus)};
    if (auto failure{check(!throwing_context.has_value(), __LINE__)}) return failure;
    if (auto failure{check(!throwing_context.has_invalid_evidence(), __LINE__)}) return failure;
    if (auto failure{check(throwing_context.has_error(), __LINE__)}) return failure;
    if (auto failure{check(throwing_context.assume_error().code() == bitcoin::operation_error_code::callback_failure, __LINE__)}) return failure;

    const auto equal_mtp{verify_header_candidate(
        with_previous_hash(with_time(genesis, bitcoin::block_time{1231006505}), parent.hash()),
        std::span<const bitcoin::block_header>{parents},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(equal_mtp.has_value() && !equal_mtp.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(equal_mtp).rule_id() == bitcoin::validation_rule_id::h04_median_time_past, __LINE__)}) return failure;

    const auto before_mtp{verify_header_candidate(
        with_previous_hash(with_time(genesis, bitcoin::block_time{1231006504}), parent.hash()),
        std::span<const bitcoin::block_header>{parents},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(before_mtp.has_value() && !before_mtp.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(before_mtp).rule_id() == bitcoin::validation_rule_id::h04_median_time_past, __LINE__)}) return failure;

    const auto just_after_mtp{verify_header_candidate(
        with_previous_hash(with_time(genesis, bitcoin::block_time{1231006506}), parent.hash()),
        std::span<const bitcoin::block_header>{parents},
        operation_time(1231006506),
        consensus)};
    if (auto failure{check(just_after_mtp.has_value(), __LINE__)}) return failure;
    if (auto failure{check(!just_after_mtp.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(just_after_mtp).rule_id() == bitcoin::validation_rule_id::h02_proof_of_work, __LINE__)}) return failure;

    std::array<bitcoin::block_header, 11> mtp_window{};
    for (std::uint32_t i{0}; i < mtp_window.size(); ++i) {
        mtp_window[i] = i == 0 ?
            with_time(genesis, bitcoin::block_time{1231006500}) :
            child_of(mtp_window[i - 1], 1231006500 + i, genesis.bits());
    }
    const auto after_median_before_parent{verify_header_candidate(
        with_previous_hash(with_time(genesis, bitcoin::block_time{1231006506}), mtp_window.back().hash()),
        std::span<const bitcoin::block_header>{mtp_window},
        operation_time(1231006506),
        consensus)};
    if (auto failure{check(after_median_before_parent.has_value(), __LINE__)}) return failure;
    if (auto failure{check(!after_median_before_parent.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(after_median_before_parent).rule_id() == bitcoin::validation_rule_id::h02_proof_of_work, __LINE__)}) return failure;

    const auto future_header{with_time(genesis, bitcoin::block_time{1231013706})};
    const auto future_rejected{verify_header_candidate(
        future_header,
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006505),
        consensus)};
    if (auto failure{check(future_rejected.has_value() && !future_rejected.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(future_rejected).rule_id() == bitcoin::validation_rule_id::h05_future_time, __LINE__)}) return failure;

    const auto future_later{verify_header_candidate(
        future_header,
        std::span<const bitcoin::block_header>{no_parent},
        operation_time(1231006506),
        consensus)};
    if (auto failure{check(future_later.has_value(), __LINE__)}) return failure;
    if (auto failure{check(!future_later.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(future_later).rule_id() == bitcoin::validation_rule_id::h02_proof_of_work, __LINE__)}) return failure;

    const auto parent_bits{0x1c0ffff0U};
    const auto parent_with_bits{with_bits(with_time(genesis, bitcoin::block_time{1231006505}), parent_bits)};
    const std::array difficulty_parent{parent_with_bits};
    const auto non_adjustment_bad_bits{verify_header_candidate(
        child_of(parent_with_bits, 1231006506, 0x1d00ffffU),
        std::span<const bitcoin::block_header>{difficulty_parent},
        operation_time(1231006506),
        consensus)};
    if (auto failure{check(non_adjustment_bad_bits.has_value() && !non_adjustment_bad_bits.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(non_adjustment_bad_bits).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    auto min_difficulty_params{consensus};
    min_difficulty_params.allow_min_difficulty_blocks = true;
    const auto min_difficulty_required{verify_header_candidate(
        child_of(parent_with_bits, 1231006505 + 1201, parent_bits),
        std::span<const bitcoin::block_header>{difficulty_parent},
        operation_time(1231006505 + 1201),
        min_difficulty_params)};
    if (auto failure{check(min_difficulty_required.has_value() && !min_difficulty_required.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(min_difficulty_required).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    const auto min_parent{child_of(parent_with_bits, 1231006506, 0x1d00ffffU)};
    const std::array min_ancestors{parent_with_bits, min_parent};
    const auto last_non_min_required{verify_header_candidate(
        child_of(min_parent, 1231006506, 0x1d00ffffU),
        std::span<const bitcoin::block_header>{min_ancestors},
        operation_time(1231006506),
        min_difficulty_params)};
    if (auto failure{check(last_non_min_required.has_value() && !last_non_min_required.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(last_non_min_required).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    auto retarget_params{consensus};
    retarget_params.target_spacing = std::chrono::seconds{10};
    retarget_params.target_timespan = std::chrono::seconds{40};
    const auto retarget_chain{chain_from(
        with_bits(with_time(genesis, bitcoin::block_time{1000}), parent_bits),
        {{1010, parent_bits}, {1020, parent_bits}, {1080, parent_bits}})};
    const auto retarget_bad_bits{verify_header_candidate(
        child_of(retarget_chain.back(), 1081, parent_bits),
        std::span<const bitcoin::block_header>{retarget_chain},
        operation_time(1081),
        retarget_params)};
    if (auto failure{check(retarget_bad_bits.has_value() && !retarget_bad_bits.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(retarget_bad_bits).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    auto no_retarget_params{retarget_params};
    no_retarget_params.no_retargeting = true;
    const auto no_retarget_bad_bits{verify_header_candidate(
        child_of(retarget_chain.back(), 1081, 0x1d00ffffU),
        std::span<const bitcoin::block_header>{retarget_chain},
        operation_time(1081),
        no_retarget_params)};
    if (auto failure{check(no_retarget_bad_bits.has_value() && !no_retarget_bad_bits.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(no_retarget_bad_bits).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    auto bip94_params{retarget_params};
    bip94_params.enforce_bip94 = true;
    const auto bip94_chain{chain_from(
        with_bits(with_time(genesis, bitcoin::block_time{1000}), parent_bits),
        {{1010, 0x1d00ffffU}, {1020, 0x1d00ffffU}, {1040, 0x1d00ffffU}})};
    const auto bip94_uses_first_period_bits{verify_header_candidate(
        child_of(bip94_chain.back(), 1041, 0x1d00ffffU),
        std::span<const bitcoin::block_header>{bip94_chain},
        operation_time(1041),
        bip94_params)};
    if (auto failure{check(bip94_uses_first_period_bits.has_value() && !bip94_uses_first_period_bits.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(bip94_uses_first_period_bits).rule_id() == bitcoin::validation_rule_id::h03_difficulty_transition, __LINE__)}) return failure;

    const auto timewarp_chain{chain_from(
        with_time(genesis, bitcoin::block_time{1960}),
        {{1970, 0x1d00ffffU}, {1980, 0x1d00ffffU}, {2000, 0x1d00ffffU}})};
    const auto timewarp{verify_header_candidate(
        child_of(timewarp_chain.back(), 1399, 0x1d00ffffU),
        std::span<const bitcoin::block_header>{timewarp_chain},
        operation_time(2000),
        bip94_params)};
    if (auto failure{check(timewarp.has_value() && !timewarp.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(rejection(timewarp).rule_id() == bitcoin::validation_rule_id::h07_timewarp, __LINE__)}) return failure;

    return 0;
}
