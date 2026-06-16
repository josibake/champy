// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/api.h>

#include <bitcoin/protocol/codec.h>
#include <bitcoin/protocol/detail/hash_writer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
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

bitcoin::script script(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (auto value : values) {
        bytes.push_back(byte(value));
    }
    return bitcoin::script{bytes};
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

bitcoin::script push_script(std::span<const std::byte> payload)
{
    std::vector<std::byte> result;
    result.reserve(payload.size() + 1);
    result.push_back(static_cast<std::byte>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return bitcoin::script{result};
}

bitcoin::script p2sh_script(std::byte value)
{
    std::vector<std::byte> result{byte(0xa9), byte(0x14)};
    result.insert(result.end(), 20, value);
    result.push_back(byte(0x87));
    return bitcoin::script{result};
}

bitcoin::script witness_v0_keyhash_script(std::byte value)
{
    std::vector<std::byte> result{byte(0x00), byte(0x14)};
    result.insert(result.end(), 20, value);
    return bitcoin::script{result};
}

bitcoin::script witness_v0_scripthash_script(std::byte value)
{
    std::vector<std::byte> result{byte(0x00), byte(0x20)};
    result.insert(result.end(), 32, value);
    return bitcoin::script{result};
}

bitcoin::txid txid_with(std::byte value) noexcept
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(value);
    return bitcoin::txid{bytes};
}

bitcoin::outpoint point(std::byte tx_byte, std::uint32_t index) noexcept
{
    return bitcoin::outpoint{txid_with(tx_byte), bitcoin::tx_output_index{index}};
}

bitcoin::tx_input input(bitcoin::outpoint previous, std::uint32_t sequence = 0xffffffffU)
{
    return bitcoin::tx_input{previous, script({0x51}), sequence};
}

bitcoin::tx_input input_with(
    bitcoin::outpoint previous,
    bitcoin::script script_sig,
    std::vector<bitcoin::witness_item> witness = {},
    std::uint32_t sequence = 0xffffffffU)
{
    return bitcoin::tx_input{previous, std::move(script_sig), sequence, std::move(witness)};
}

bitcoin::tx_output output(std::int64_t value = 50, bitcoin::script locking_script = script({0x51}))
{
    return bitcoin::tx_output{bitcoin::amount{value}, std::move(locking_script)};
}

bitcoin::transaction coinbase(bitcoin::script locking_script = script({0x51}))
{
    return bitcoin::transaction{
        1,
        {bitcoin::tx_input{bitcoin::outpoint::null(), script({0x51, 0x51}), 0xffffffffU}},
        {output(50, std::move(locking_script))},
        0};
}

bitcoin::witness_item witness_item(std::byte value)
{
    std::array<std::byte, 32> bytes{};
    bytes.fill(value);
    return bitcoin::witness_item{std::span<const std::byte>{bytes}};
}

bitcoin::witness_item witness_item(std::initializer_list<unsigned char> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (auto value : values) {
        bytes.push_back(byte(value));
    }
    return bitcoin::witness_item{std::span<const std::byte>{bytes}};
}

bitcoin::transaction coinbase_with(
    bitcoin::script script_sig,
    std::vector<bitcoin::tx_output> outputs,
    std::vector<bitcoin::witness_item> witness = {})
{
    return bitcoin::transaction{
        1,
        {bitcoin::tx_input{bitcoin::outpoint::null(), std::move(script_sig), 0xffffffffU, std::move(witness)}},
        std::move(outputs),
        0};
}

bitcoin::transaction ordinary(std::byte tx_byte, std::uint32_t output_index = 0, std::int64_t value = 1)
{
    return bitcoin::transaction{
        1,
        {input(point(tx_byte, output_index))},
        {output(value)},
        0};
}

bitcoin::transaction ordinary_with_witness(std::byte tx_byte)
{
    return bitcoin::transaction{
        1,
        {bitcoin::tx_input{point(tx_byte, 0), script({0x51}), 0xffffffffU, {witness_item(byte(0x42))}}},
        {output(1)},
        0};
}

bitcoin::transaction spends(bitcoin::outpoint previous, std::int64_t value = 50)
{
    return bitcoin::transaction{
        1,
        {input(previous)},
        {output(value)},
        0};
}

bitcoin::transaction spends_with(
    bitcoin::outpoint previous,
    bitcoin::script script_sig,
    std::vector<bitcoin::witness_item> witness,
    std::int64_t value = 50)
{
    return bitcoin::transaction{
        1,
        {input_with(previous, std::move(script_sig), std::move(witness))},
        {output(value)},
        0};
}

bitcoin::hash256 hash_pair(bitcoin::hash256 left, bitcoin::hash256 right)
{
    std::array<std::byte, 64> bytes{};
    std::ranges::copy(as_bytes(left), bytes.begin());
    std::ranges::copy(as_bytes(right), bytes.begin() + 32);
    return bitcoin::protocol_detail::double_sha256(bytes);
}

bitcoin::hash256 merkle_root(std::span<const bitcoin::transaction> transactions)
{
    std::vector<bitcoin::hash256> hashes;
    hashes.reserve(transactions.size());
    for (const auto& tx : transactions) {
        hashes.push_back(bitcoin::hash256{as_bytes(tx.id())});
    }
    while (hashes.size() > 1) {
        if ((hashes.size() & 1U) != 0) {
            hashes.push_back(hashes.back());
        }
        for (std::size_t position{0}; position < hashes.size(); position += 2) {
            hashes[position / 2] = hash_pair(hashes[position], hashes[position + 1]);
        }
        hashes.resize(hashes.size() / 2);
    }
    return hashes.empty() ? bitcoin::hash256{} : hashes.front();
}

bitcoin::hash256 witness_merkle_root(std::span<const bitcoin::transaction> transactions)
{
    std::vector<bitcoin::hash256> hashes;
    hashes.reserve(transactions.size());
    if (!transactions.empty()) {
        hashes.push_back(bitcoin::hash256{});
        for (std::size_t index{1}; index < transactions.size(); ++index) {
            hashes.push_back(bitcoin::hash256{as_bytes(transactions[index].witness_id())});
        }
    }
    while (hashes.size() > 1) {
        if ((hashes.size() & 1U) != 0) {
            hashes.push_back(hashes.back());
        }
        for (std::size_t position{0}; position < hashes.size(); position += 2) {
            hashes[position / 2] = hash_pair(hashes[position], hashes[position + 1]);
        }
        hashes.resize(hashes.size() / 2);
    }
    return hashes.empty() ? bitcoin::hash256{} : hashes.front();
}

bitcoin::hash256 commitment_hash(bitcoin::hash256 witness_root, const bitcoin::witness_item& nonce)
{
    std::array<std::byte, 64> bytes{};
    std::ranges::copy(as_bytes(witness_root), bytes.begin());
    std::ranges::copy(as_bytes(nonce), bytes.begin() + 32);
    return bitcoin::protocol_detail::double_sha256(bytes);
}

bitcoin::script commitment_script(bitcoin::hash256 commitment)
{
    std::vector<std::byte> bytes{
        byte(0x6a), byte(0x24), byte(0xaa), byte(0x21), byte(0xa9), byte(0xed)};
    bytes.insert(bytes.end(), as_bytes(commitment).begin(), as_bytes(commitment).end());
    return bitcoin::script{std::span<const std::byte>{bytes}};
}

bitcoin::block block_with(std::vector<bitcoin::transaction> transactions, std::uint32_t bits = 0x1d00ffff)
{
    const auto root{merkle_root(transactions)};
    return bitcoin::block{
        bitcoin::block_header{
            1,
            bitcoin::block_hash{},
            root,
            bitcoin::block_time{1231006505},
            bits,
            1},
        std::move(transactions)};
}

bitcoin::block block_with_bad_merkle(std::vector<bitcoin::transaction> transactions)
{
    return bitcoin::block{
        bitcoin::block_header{
            1,
            bitcoin::block_hash{},
            bitcoin::hash256{},
            bitcoin::block_time{1231006505},
            0x1d00ffff,
            1},
        std::move(transactions)};
}

bitcoin::block_local_context context() noexcept
{
    bitcoin::block_local_context result;
    result.limits.transactions.limits.max_money = bitcoin::amount{100};
    return result;
}

bitcoin::median_time_past mtp(std::int64_t seconds) noexcept
{
    return bitcoin::median_time_past{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
}

bitcoin::coin coin(std::int64_t value, std::int32_t height = 100, bool coinbase = false)
{
    return bitcoin::coin{
        output(value),
        bitcoin::block_height{height},
        coinbase,
        mtp(1231006505)};
}

bitcoin::coin coin_with_script(
    std::int64_t value,
    bitcoin::script locking_script,
    std::int32_t height = 100,
    bool coinbase = false)
{
    return bitcoin::coin{
        output(value, std::move(locking_script)),
        bitcoin::block_height{height},
        coinbase,
        mtp(1231006505)};
}

bitcoin::block_spend_context spend_context() noexcept
{
    bitcoin::block_spend_context result;
    result.local = context();
    result.spend.height = bitcoin::block_height{200};
    result.spend.previous_median_time_past = mtp(1231007000);
    result.spend.limits.max_money = bitcoin::amount{100};
    result.spend.coinbase_maturity = 100;
    result.deployments.enforce_bip30 = true;
    result.subsidy = bitcoin::amount{50};
    return result;
}

bitcoin::block_contextual_context contextual_context() noexcept
{
    bitcoin::block_contextual_context result;
    result.local = context();
    result.height = bitcoin::block_height{17};
    result.locktime_cutoff = bitcoin::block_time{1231006505};
    return result;
}

bitcoin::proof_of_work_limit maximum_pow_limit() noexcept
{
    std::array<std::byte, 32> limit{};
    limit.fill(byte(0xff));
    return bitcoin::proof_of_work_limit{limit};
}

bitcoin::validation_time validation_time(std::int64_t seconds) noexcept
{
    return bitcoin::validation_time{std::chrono::sys_seconds{std::chrono::seconds{seconds}}};
}

bitcoin::block_validation_context complete_block_context() noexcept
{
    bitcoin::block_validation_context result;
    result.consensus.pow_limit = maximum_pow_limit();
    result.limits = context().limits;
    result.spend_deployments.enforce_bip30 = true;
    result.subsidy = bitcoin::amount{40};
    return result;
}

class in_memory_coin_index
{
public:
    void set(bitcoin::outpoint point, bitcoin::coin value)
    {
        m_entries.push_back(entry{point, std::move(value)});
    }

    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint& point) const
    {
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == point;
        })};
        if (match == m_entries.end()) {
            return std::nullopt;
        }
        return match->value;
    }

private:
    struct entry {
        bitcoin::outpoint point;
        bitcoin::coin value;
    };

    std::vector<entry> m_entries;
};

class fallible_coin_source
{
public:
    void set(bitcoin::outpoint point, bitcoin::coin_lookup_result result)
    {
        m_entries.push_back(entry{point, std::move(result)});
    }

    [[nodiscard]] bitcoin::coin_lookup_result lookup(const bitcoin::outpoint& point) const
    {
        const auto match{std::ranges::find_if(m_entries, [&](const entry& candidate) {
            return candidate.point == point;
        })};
        if (match == m_entries.end()) {
            return bitcoin::coin_lookup_result::missing();
        }
        return match->result;
    }

private:
    struct entry {
        bitcoin::outpoint point;
        bitcoin::coin_lookup_result result;
    };

    std::vector<entry> m_entries;
};

class throwing_coin_index
{
public:
    [[nodiscard]] std::optional<bitcoin::coin> operator()(const bitcoin::outpoint&) const
    {
        throw std::runtime_error{"coin lookup failed"};
    }
};

static_assert(bitcoin::coin_index<in_memory_coin_index>);
static_assert(!bitcoin::coin_index<fallible_coin_source>);
static_assert(bitcoin::fallible_coin_source<fallible_coin_source>);
static_assert(bitcoin::coin_index<throwing_coin_index>);

const bitcoin::validation_rejection& rejection(const bitcoin::verify_result<bitcoin::block_facts>& result)
{
    return result.assume_value().assume_rejection();
}

bitcoin::validation_rule_id rejected_by(const bitcoin::block& candidate, bitcoin::block_local_context candidate_context = context())
{
    return rejection(bitcoin::assess_block_intrinsic(candidate, candidate_context)).rule_id();
}

static_assert(!std::default_initializable<bitcoin::block_facts>);
static_assert(!std::is_aggregate_v<bitcoin::block_facts>);

} // namespace

int main()
{
    const auto valid_block{block_with({coinbase()})};
    const auto primary_valid{bitcoin::verify(valid_block)};
    if (auto failure{check(primary_valid.has_value() && primary_valid.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(primary_valid.assume_value().assume_facts().hash() == valid_block.hash(), __LINE__)}) return failure;

    const auto valid{bitcoin::assess_block_intrinsic(valid_block, context())};
    if (auto failure{check(valid.has_value() && valid.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().hash() == valid_block.hash(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().transaction_count() == 1, __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().transactions().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().transactions().front().coinbase(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().merkle_root() == valid_block.header().merkle_root(), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().serialized_size() == bitcoin::serialized_size(valid_block), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().stripped_size() == bitcoin::stripped_serialized_size(valid_block), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().weight() == bitcoin::weight(valid_block), __LINE__)}) return failure;
    if (auto failure{check(valid.assume_value().assume_facts().legacy_sigop_cost() == 0, __LINE__)}) return failure;

    if (auto failure{check(rejected_by(block_with({})) == bitcoin::validation_rule_id::l01_block_non_empty, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(block_with_bad_merkle({coinbase()})) == bitcoin::validation_rule_id::l02_merkle_root, __LINE__)}) return failure;

    const auto duplicate{ordinary(byte(3), 0)};
    if (auto failure{check(
            rejected_by(block_with({coinbase(), ordinary(byte(2), 0), duplicate, duplicate})) ==
                bitcoin::validation_rule_id::l03_merkle_mutation,
            __LINE__)}) return failure;

    auto tiny_context{context()};
    tiny_context.limits.max_block_weight = 1;
    if (auto failure{check(rejected_by(valid_block, tiny_context) == bitcoin::validation_rule_id::l04_original_block_size, __LINE__)}) return failure;

    if (auto failure{check(rejected_by(block_with({ordinary(byte(4), 0)})) == bitcoin::validation_rule_id::l05_coinbase_position, __LINE__)}) return failure;
    if (auto failure{check(rejected_by(block_with({coinbase(), coinbase(script({0x52}))})) == bitcoin::validation_rule_id::l05_coinbase_position, __LINE__)}) return failure;

    auto sigop_context{context()};
    sigop_context.limits.max_legacy_sigop_cost = 0;
    if (auto failure{check(
            rejected_by(block_with({coinbase(script({0xac}))}), sigop_context) ==
                bitcoin::validation_rule_id::l06_legacy_sigops,
            __LINE__)}) return failure;

    if (auto failure{check(
            rejected_by(block_with({coinbase(), ordinary(byte(5), 0, 101)})) ==
                bitcoin::validation_rule_id::l11_output_value_range,
            __LINE__)}) return failure;

    auto pre_segwit{contextual_context()};
    const auto witness_tx{ordinary_with_witness(byte(6))};
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(block_with({coinbase(), witness_tx}), pre_segwit)).rule_id() ==
                bitcoin::validation_rule_id::c02_pre_segwit_no_witness,
            __LINE__)}) return failure;

    const auto commitment_only{block_with({coinbase(commitment_script(bitcoin::hash256{}))})};
    if (auto failure{check(bitcoin::assess_block_contextual(commitment_only, pre_segwit).assume_value().accepted(), __LINE__)}) return failure;

    auto active_height{contextual_context()};
    active_height.deployments.height_in_coinbase_active = true;
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(block_with({coinbase()}), active_height)).rule_id() ==
                bitcoin::validation_rule_id::c04_coinbase_height,
            __LINE__)}) return failure;
    if (auto failure{check(
            bitcoin::assess_block_contextual(
                block_with({coinbase_with(script({0x01, 0x11, 0x51}), {output()})}),
                active_height)
                .assume_value()
                .accepted(),
            __LINE__)}) return failure;

    active_height.height = bitcoin::block_height{128};
    if (auto failure{check(
            bitcoin::assess_block_contextual(
                block_with({coinbase_with(script({0x02, 0x80, 0x00, 0x51}), {output()})}),
                active_height)
                .assume_value()
                .accepted(),
            __LINE__)}) return failure;

    auto segwit{contextual_context()};
    segwit.deployments.segwit_active = true;
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(block_with({coinbase(), witness_tx}), segwit)).rule_id() ==
                bitcoin::validation_rule_id::c05_witness_commitment_presence,
            __LINE__)}) return failure;

    const auto nonce{witness_item(byte(0x11))};
    const auto committed_noncoinbase{ordinary_with_witness(byte(7))};
    const std::vector committed_transactions{
        coinbase_with(script({0x51, 0x51}), {output()}, {nonce}),
        committed_noncoinbase};
    const auto commitment{commitment_hash(witness_merkle_root(committed_transactions), nonce)};
    const auto committed_coinbase{coinbase_with(
        script({0x51, 0x51}),
        {output(1, commitment_script(bitcoin::hash256{})), output(50, commitment_script(commitment))},
        {nonce})};
    const auto committed_block{block_with({committed_coinbase, committed_noncoinbase})};
    const auto committed_result{bitcoin::assess_block_contextual(committed_block, segwit)};
    if (auto failure{check(committed_result.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(committed_result.assume_value().assume_facts().commitment().output_index() == bitcoin::tx_output_index{1}, __LINE__)}) return failure;

    const auto bad_nonce{block_with({
        coinbase_with(script({0x51, 0x51}), {output(50, commitment_script(commitment))}, {witness_item({0x01})}),
        committed_noncoinbase})};
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(bad_nonce, segwit)).rule_id() ==
                bitcoin::validation_rule_id::c06_witness_nonce_presence,
            __LINE__)}) return failure;

    const auto bad_commitment{block_with({
        coinbase_with(script({0x51, 0x51}), {output(50, commitment_script(bitcoin::hash256{}))}, {nonce}),
        committed_noncoinbase})};
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(bad_commitment, segwit)).rule_id() ==
                bitcoin::validation_rule_id::c07_witness_merkle_commitment,
            __LINE__)}) return failure;

    auto weight_context{contextual_context()};
    weight_context.deployments.segwit_active = true;
    weight_context.local.limits.max_block_weight = bitcoin::stripped_serialized_size(committed_block) * bitcoin::witness_scale_factor;
    if (auto failure{check(
            rejection(bitcoin::assess_block_contextual(committed_block, weight_context)).rule_id() ==
                bitcoin::validation_rule_id::c03_block_weight,
            __LINE__)}) return failure;

    in_memory_coin_index index;
    index.set(point(byte(9), 0), coin(60));
    auto spend_check{spend_context()};
    spend_check.subsidy = bitcoin::amount{40};
    const auto spend_block{block_with({coinbase(), ordinary(byte(9), 0, 50)})};
    const auto spend_result{bitcoin::assess_block_spends(spend_block, spend_check, index)};
    if (auto failure{check(spend_result.has_value() && spend_result.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(spend_result.assume_value().assume_facts().total_fees() == bitcoin::amount{10}, __LINE__)}) return failure;
    if (auto failure{check(spend_result.assume_value().assume_facts().spends().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(spend_result.assume_value().assume_facts().spent_outputs().size() == 1, __LINE__)}) return failure;
    if (auto failure{check(spend_result.assume_value().assume_facts().created_outputs().size() == 2, __LINE__)}) return failure;
    if (auto failure{check(spend_result.assume_value().assume_facts().coinbase_output_value() == bitcoin::amount{50}, __LINE__)}) return failure;

    const auto first_spend{ordinary(byte(10), 0, 25)};
    const auto second_spend{spends(bitcoin::outpoint{first_spend.id(), bitcoin::tx_output_index{0}}, 20)};
    in_memory_coin_index overlay_index;
    overlay_index.set(point(byte(10), 0), coin(60));
    auto overlay_context{spend_context()};
    overlay_context.subsidy = bitcoin::amount{10};
    const auto overlay_result{bitcoin::assess_block_spends(
        block_with({coinbase(), first_spend, second_spend}),
        overlay_context,
        overlay_index)};
    if (auto failure{check(overlay_result.has_value() && overlay_result.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(overlay_result.assume_value().assume_facts().total_fees() == bitcoin::amount{40}, __LINE__)}) return failure;

    const auto spends_coinbase{spends(bitcoin::outpoint{block_with({coinbase()}).transactions().front().id(), bitcoin::tx_output_index{0}}, 40)};
    const auto same_block_coinbase_spend{bitcoin::assess_block_spends(
        block_with({coinbase(), spends_coinbase}),
        overlay_context,
        overlay_index)};
    if (auto failure{check(rejection(same_block_coinbase_spend).rule_id() == bitcoin::validation_rule_id::s09_coinbase_maturity, __LINE__)}) return failure;

    const auto out_of_order{bitcoin::assess_block_spends(
        block_with({coinbase(), second_spend, first_spend}),
        overlay_context,
        overlay_index)};
    if (auto failure{check(rejection(out_of_order).rule_id() == bitcoin::validation_rule_id::s02_prevouts_unspent, __LINE__)}) return failure;

    const auto double_spend{bitcoin::assess_block_spends(
        block_with({coinbase(), ordinary(byte(11), 0, 25), ordinary(byte(11), 0, 20)}),
        overlay_context,
        [&] {
            in_memory_coin_index double_index;
            double_index.set(point(byte(11), 0), coin(60));
            return double_index;
        }())};
    if (auto failure{check(rejection(double_spend).rule_id() == bitcoin::validation_rule_id::s02_prevouts_unspent, __LINE__)}) return failure;

    in_memory_coin_index duplicate_index;
    duplicate_index.set(
        bitcoin::outpoint{valid_block.transactions().front().id(), bitcoin::tx_output_index{0}},
        coin(50));
    const auto duplicate_output{bitcoin::assess_block_spends(valid_block, spend_context(), duplicate_index)};
    if (auto failure{check(rejection(duplicate_output).rule_id() == bitcoin::validation_rule_id::s01_bip30_duplicate_unspent, __LINE__)}) return failure;

    fallible_coin_source bip30_failure_index;
    bip30_failure_index.set(
        bitcoin::outpoint{valid_block.transactions().front().id(), bitcoin::tx_output_index{0}},
        bitcoin::coin_lookup_result::unavailable("snapshot not ready"));
    const auto bip30_unavailable{bitcoin::assess_block_spends(valid_block, spend_context(), bip30_failure_index)};
    if (auto failure{check(bip30_unavailable.has_error() && bip30_unavailable.assume_error().code() == bitcoin::operation_error_code::data_unavailable, __LINE__)}) return failure;

    auto low_reward_context{spend_context()};
    low_reward_context.subsidy = bitcoin::amount{0};
    const auto excessive_reward{bitcoin::assess_block_spends(valid_block, low_reward_context, in_memory_coin_index{})};
    if (auto failure{check(rejection(excessive_reward).rule_id() == bitcoin::validation_rule_id::s04_coinbase_subsidy, __LINE__)}) return failure;

    in_memory_coin_index false_script_index;
    false_script_index.set(point(byte(9), 0), coin_with_script(60, script({0x00})));
    const auto script_rejected{bitcoin::assess_block_spends(spend_block, spend_check, false_script_index)};
    if (auto failure{check(rejection(script_rejected).rule_id() == bitcoin::validation_rule_id::s07_scripts_validate, __LINE__)}) return failure;

    fallible_coin_source unavailable_index;
    unavailable_index.set(point(byte(9), 0), bitcoin::coin_lookup_result::unavailable("snapshot not ready"));
    const auto unavailable{bitcoin::assess_block_spends(spend_block, spend_check, unavailable_index)};
    if (auto failure{check(unavailable.has_error() && unavailable.assume_error().code() == bitcoin::operation_error_code::data_unavailable, __LINE__)}) return failure;

    const auto thrown_spend_lookup{bitcoin::assess_block_spends(spend_block, spend_check, throwing_coin_index{})};
    if (auto failure{check(thrown_spend_lookup.has_error() && thrown_spend_lookup.assume_error().code() == bitcoin::operation_error_code::callback_failure, __LINE__)}) return failure;

    const auto thrown_bip30_lookup{bitcoin::assess_block_spends(valid_block, spend_context(), throwing_coin_index{})};
    if (auto failure{check(thrown_bip30_lookup.has_error() && thrown_bip30_lookup.assume_error().code() == bitcoin::operation_error_code::callback_failure, __LINE__)}) return failure;

    const auto redeem_checksig{bytes({0xac})};
    const auto p2sh_prevout{point(byte(12), 0)};
    const auto p2sh_tx{spends_with(p2sh_prevout, push_script(redeem_checksig), {}, 50)};
    in_memory_coin_index p2sh_index;
    p2sh_index.set(p2sh_prevout, coin_with_script(60, p2sh_script(byte(0x12))));
    auto p2sh_context{spend_context()};
    p2sh_context.subsidy = bitcoin::amount{40};
    p2sh_context.scripts.flags = bitcoin::verification_flags::p2sh();
    p2sh_context.max_sigop_cost = 3;
    const auto p2sh_too_many_sigops{bitcoin::assess_block_spends(
        block_with({coinbase(), p2sh_tx}),
        p2sh_context,
        p2sh_index)};
    if (auto failure{check(rejection(p2sh_too_many_sigops).rule_id() == bitcoin::validation_rule_id::s03_sigop_cost, __LINE__)}) return failure;

    const auto redeem_multisig{bytes({0x52, 0xae})};
    const auto p2sh_multisig_tx{spends_with(p2sh_prevout, push_script(redeem_multisig), {}, 50)};
    p2sh_context.max_sigop_cost = 7;
    const auto p2sh_accurate_multisig{bitcoin::assess_block_spends(
        block_with({coinbase(), p2sh_multisig_tx}),
        p2sh_context,
        p2sh_index)};
    if (auto failure{check(rejection(p2sh_accurate_multisig).rule_id() == bitcoin::validation_rule_id::s03_sigop_cost, __LINE__)}) return failure;

    const auto p2wpkh_prevout{point(byte(13), 0)};
    const auto p2wpkh_tx{spends_with(p2wpkh_prevout, script({}), {witness_item({0x01})}, 50)};
    in_memory_coin_index p2wpkh_index;
    p2wpkh_index.set(p2wpkh_prevout, coin_with_script(60, witness_v0_keyhash_script(byte(0x13))));
    auto witness_context{spend_context()};
    witness_context.subsidy = bitcoin::amount{40};
    witness_context.scripts.flags = bitcoin::verification_flags::p2sh() | bitcoin::verification_flags::witness();
    witness_context.max_sigop_cost = 0;
    const auto p2wpkh_too_many_sigops{bitcoin::assess_block_spends(
        block_with({coinbase(), p2wpkh_tx}),
        witness_context,
        p2wpkh_index)};
    if (auto failure{check(rejection(p2wpkh_too_many_sigops).rule_id() == bitcoin::validation_rule_id::s03_sigop_cost, __LINE__)}) return failure;

    const auto p2wsh_prevout{point(byte(14), 0)};
    const auto p2wsh_tx{spends_with(p2wsh_prevout, script({}), {witness_item({0x52, 0xae})}, 50)};
    in_memory_coin_index p2wsh_index;
    p2wsh_index.set(p2wsh_prevout, coin_with_script(60, witness_v0_scripthash_script(byte(0x14))));
    witness_context.max_sigop_cost = 1;
    const auto p2wsh_too_many_sigops{bitcoin::assess_block_spends(
        block_with({coinbase(), p2wsh_tx}),
        witness_context,
        p2wsh_index)};
    if (auto failure{check(rejection(p2wsh_too_many_sigops).rule_id() == bitcoin::validation_rule_id::s03_sigop_cost, __LINE__)}) return failure;

    const auto wrapped_program{witness_v0_keyhash_script(byte(0x15))};
    const auto wrapped_prevout{point(byte(15), 0)};
    const auto wrapped_witness_tx{spends_with(
        wrapped_prevout,
        push_script(as_bytes(wrapped_program)),
        {witness_item({0x01})},
        50)};
    in_memory_coin_index wrapped_index;
    wrapped_index.set(wrapped_prevout, coin_with_script(60, p2sh_script(byte(0x15))));
    witness_context.max_sigop_cost = 0;
    const auto wrapped_too_many_sigops{bitcoin::assess_block_spends(
        block_with({coinbase(), wrapped_witness_tx}),
        witness_context,
        wrapped_index)};
    if (auto failure{check(rejection(wrapped_too_many_sigops).rule_id() == bitcoin::validation_rule_id::s03_sigop_cost, __LINE__)}) return failure;

    const auto future_prevout{point(byte(16), 0)};
    const auto future_witness_tx{spends_with(future_prevout, script({}), {witness_item({0xac})}, 50)};
    in_memory_coin_index future_index;
    future_index.set(future_prevout, coin_with_script(60, script({0x51, 0x02, 0xaa, 0xbb})));
    witness_context.max_sigop_cost = 0;
    const auto future_witness_sigops{bitcoin::assess_block_spends(
        block_with({coinbase(), future_witness_tx}),
        witness_context,
        future_index)};
    if (auto failure{check(future_witness_sigops.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(future_witness_sigops.assume_value().assume_facts().sigop_cost() == 0, __LINE__)}) return failure;

    const auto complete_prevout{point(byte(17), 0)};
    const auto complete_candidate{block_with({coinbase(), spends(complete_prevout, 50)}, 0x2100ffff)};
    in_memory_coin_index complete_index;
    complete_index.set(complete_prevout, coin(60));
    const std::array<bitcoin::block_header, 0> no_ancestors{};
    const auto complete_context{complete_block_context()};
    const auto complete_time{validation_time(1231006505)};
    const auto deferred_result{bitcoin::verify(
        complete_candidate,
        std::span<const bitcoin::block_header>{no_ancestors},
        complete_time,
        complete_context)};
    if (auto failure{check(deferred_result.has_value() && deferred_result.assume_value().accepted(), __LINE__)}) return failure;

    const std::array broken_ancestors{complete_candidate.header(), complete_candidate.header()};
    const auto broken_deferred{bitcoin::verify(
        complete_candidate,
        std::span<const bitcoin::block_header>{broken_ancestors},
        complete_time,
        complete_context)};
    if (auto failure{check(!broken_deferred.has_value(), __LINE__)}) return failure;
    if (auto failure{check(broken_deferred.has_invalid_evidence(), __LINE__)}) return failure;
    if (auto failure{check(!broken_deferred.has_error(), __LINE__)}) return failure;
    if (auto failure{check(broken_deferred.assume_invalid_evidence().code() == bitcoin::header_context_evidence_code::non_contiguous_ancestry, __LINE__)}) return failure;

    const auto complete_result{bitcoin::verify(
        complete_candidate,
        std::span<const bitcoin::block_header>{no_ancestors},
        complete_time,
        complete_context,
        complete_index)};
    if (auto failure{check(complete_result.has_value() && complete_result.assume_value().accepted(), __LINE__)}) return failure;
    if (auto failure{check(complete_result.assume_value().assume_facts().total_fees() == bitcoin::amount{10}, __LINE__)}) return failure;

    const auto too_early{bitcoin::verify(
        complete_candidate,
        std::span<const bitcoin::block_header>{no_ancestors},
        validation_time(1230999304),
        complete_context,
        complete_index)};
    if (auto failure{check(too_early.has_value(), __LINE__)}) return failure;
    if (auto failure{check(too_early.assume_value().rejected(), __LINE__)}) return failure;
    if (auto failure{check(too_early.assume_value().assume_rejection().rule_id() == bitcoin::validation_rule_id::h05_future_time, __LINE__)}) return failure;

    return 0;
}
