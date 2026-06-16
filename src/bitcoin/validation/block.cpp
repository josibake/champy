// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin/validation/block.h>

#include <bitcoin/protocol/codec.h>
#include <bitcoin/protocol/detail/hash_writer.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace bitcoin {
namespace {

struct merkle_result {
    hash256 root;
    bool mutated{false};
};

struct decoded_opcode {
    unsigned char opcode{0};
    std::span<const std::byte> data;
};

struct pushed_script {
    bool push_only{true};
    std::span<const std::byte> last_push;
};

struct witness_program {
    int version{0};
    std::span<const std::byte> program;
    bool present{false};
};

inline constexpr unsigned char op_0{0x00};
inline constexpr unsigned char op_pushdata1{0x4c};
inline constexpr unsigned char op_pushdata2{0x4d};
inline constexpr unsigned char op_pushdata4{0x4e};
inline constexpr unsigned char op_1{0x51};
inline constexpr unsigned char op_16{0x60};
inline constexpr unsigned char op_hash160{0xa9};
inline constexpr unsigned char op_equal{0x87};
inline constexpr unsigned char op_checksig{0xac};
inline constexpr unsigned char op_checksigverify{0xad};
inline constexpr unsigned char op_checkmultisig{0xae};
inline constexpr unsigned char op_checkmultisigverify{0xaf};
inline constexpr std::size_t witness_v0_keyhash_size{20};
inline constexpr std::size_t witness_v0_scripthash_size{32};

[[nodiscard]] validation_rejection reject(validation_rule_id id, static_text reason) noexcept
{
    return validation_rejection::rule(validation_rejection_code::rule_violation, id, reason);
}

[[nodiscard]] verify_result<block_facts> invalid(validation_rule_id id, static_text reason)
{
    return verify_result<block_facts>::ok(validation_decision<block_facts>::invalid(reject(id, reason)));
}

[[nodiscard]] verify_result<block_facts> invalid(validation_rejection rejection)
{
    return verify_result<block_facts>::ok(validation_decision<block_facts>::invalid(rejection));
}

[[nodiscard]] bool exceeds_weight_units(std::size_t size, std::size_t max_weight) noexcept
{
    return size > max_weight / witness_scale_factor;
}

[[nodiscard]] hash256 hash_pair(hash256 left, hash256 right)
{
    std::array<std::byte, 64> bytes{};
    std::ranges::copy(as_bytes(left), bytes.begin());
    std::ranges::copy(as_bytes(right), bytes.begin() + 32);
    return protocol_detail::double_sha256(bytes);
}

[[nodiscard]] merkle_result merkle_root(std::span<const transaction> transactions)
{
    std::vector<hash256> hashes;
    hashes.reserve(transactions.size());
    for (const auto& tx : transactions) {
        hashes.push_back(hash256{as_bytes(tx.id())});
    }

    bool mutated{false};
    while (hashes.size() > 1) {
        for (std::size_t position{0}; position + 1 < hashes.size(); position += 2) {
            if (hashes[position] == hashes[position + 1]) {
                mutated = true;
            }
        }
        if ((hashes.size() & 1U) != 0) {
            hashes.push_back(hashes.back());
        }
        for (std::size_t position{0}; position < hashes.size(); position += 2) {
            hashes[position / 2] = hash_pair(hashes[position], hashes[position + 1]);
        }
        hashes.resize(hashes.size() / 2);
    }

    return merkle_result{hashes.empty() ? hash256{} : hashes.front(), mutated};
}

[[nodiscard]] hash256 witness_merkle_root(std::span<const transaction> transactions)
{
    std::vector<hash256> hashes;
    hashes.reserve(transactions.size());
    if (!transactions.empty()) {
        hashes.push_back(hash256{});
        for (std::size_t index{1}; index < transactions.size(); ++index) {
            hashes.push_back(hash256{as_bytes(transactions[index].witness_id())});
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

    return hashes.empty() ? hash256{} : hashes.front();
}

[[nodiscard]] bool is_coinbase(const transaction& tx) noexcept
{
    return tx.inputs().size() == 1 && tx.inputs().front().previous_output().is_null();
}

[[nodiscard]] bool transaction_has_witness(const transaction& tx) noexcept
{
    return std::ranges::any_of(tx.inputs(), [](const tx_input& input) {
        return !input.witness().empty();
    });
}

[[nodiscard]] bool has_witness(std::span<const transaction> transactions) noexcept
{
    return std::ranges::any_of(transactions, [](const transaction& tx) {
        return transaction_has_witness(tx);
    });
}

[[nodiscard]] bool has_witness_commitment_prefix(script_ref script) noexcept
{
    const auto bytes{as_bytes(script)};
    return bytes.size() >= 38 &&
           bytes[0] == std::byte{0x6a} &&
           bytes[1] == std::byte{0x24} &&
           bytes[2] == std::byte{0xaa} &&
           bytes[3] == std::byte{0x21} &&
           bytes[4] == std::byte{0xa9} &&
           bytes[5] == std::byte{0xed};
}

[[nodiscard]] witness_commitment find_witness_commitment(const transaction& coinbase) noexcept
{
    witness_commitment result{witness_commitment::none()};
    for (std::size_t output_index{0}; output_index < coinbase.outputs().size(); ++output_index) {
        const auto script{coinbase.outputs()[output_index].script()};
        if (!has_witness_commitment_prefix(script)) {
            continue;
        }
        std::array<std::byte, 32> commitment{};
        const auto bytes{as_bytes(script)};
        std::ranges::copy(bytes.subspan(6, 32), commitment.begin());
        result = witness_commitment::found(tx_output_index{static_cast<std::uint32_t>(output_index)}, hash256{commitment});
    }
    return result;
}

[[nodiscard]] std::size_t read_little_push_size(
    std::span<const std::byte> bytes,
    std::size_t offset,
    std::size_t byte_count) noexcept
{
    std::size_t value{0};
    for (std::size_t index{0}; index < byte_count && offset + index < bytes.size(); ++index) {
        value |= static_cast<std::size_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (8U * index);
    }
    return value;
}

[[nodiscard]] bool read_opcode(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    decoded_opcode& decoded) noexcept
{
    if (offset >= bytes.size()) {
        return false;
    }

    const auto opcode{std::to_integer<unsigned char>(bytes[offset])};
    ++offset;
    decoded = decoded_opcode{opcode, {}};

    std::size_t push_size{0};
    std::size_t size_bytes{0};
    if (opcode >= 1U && opcode <= 75U) {
        push_size = opcode;
    } else if (opcode == op_pushdata1) {
        size_bytes = 1;
    } else if (opcode == op_pushdata2) {
        size_bytes = 2;
    } else if (opcode == op_pushdata4) {
        size_bytes = 4;
    } else {
        return true;
    }

    if (size_bytes != 0) {
        if (bytes.size() - offset < size_bytes) {
            return false;
        }
        push_size = read_little_push_size(bytes, offset, size_bytes);
        offset += size_bytes;
    }

    if (bytes.size() - offset < push_size) {
        return false;
    }

    decoded.data = bytes.subspan(offset, push_size);
    offset += push_size;
    return true;
}

[[nodiscard]] bool is_small_integer_opcode(unsigned char opcode) noexcept
{
    return opcode >= op_1 && opcode <= op_16;
}

[[nodiscard]] std::size_t small_integer_value(unsigned char opcode) noexcept
{
    return static_cast<std::size_t>(opcode - 0x50U);
}

[[nodiscard]] std::size_t sigop_count(std::span<const std::byte> bytes, bool accurate) noexcept
{
    std::size_t count{0};
    unsigned char last_opcode{0xff};
    for (std::size_t offset{0}; offset < bytes.size();) {
        decoded_opcode decoded;
        if (!read_opcode(bytes, offset, decoded)) {
            break;
        }
        if (decoded.opcode == op_checksig || decoded.opcode == op_checksigverify) {
            ++count;
        } else if (decoded.opcode == op_checkmultisig || decoded.opcode == op_checkmultisigverify) {
            count += accurate && is_small_integer_opcode(last_opcode) ? small_integer_value(last_opcode) : 20U;
        }
        last_opcode = decoded.opcode;
    }
    return count;
}

[[nodiscard]] std::size_t legacy_sigop_count(script_ref script) noexcept
{
    return sigop_count(as_bytes(script), false);
}

[[nodiscard]] std::size_t legacy_sigop_count(const transaction& tx) noexcept
{
    std::size_t count{0};
    for (const auto& input : tx.inputs()) {
        count += legacy_sigop_count(input.script());
    }
    for (const auto& output : tx.outputs()) {
        count += legacy_sigop_count(output.script());
    }
    return count;
}

[[nodiscard]] bool is_pay_to_script_hash(script_ref script) noexcept
{
    const auto bytes{as_bytes(script)};
    return bytes.size() == 23 &&
           bytes[0] == static_cast<std::byte>(op_hash160) &&
           bytes[1] == std::byte{20} &&
           bytes[22] == static_cast<std::byte>(op_equal);
}

[[nodiscard]] pushed_script last_pushed_script(script_ref script_sig) noexcept
{
    pushed_script result;
    const auto bytes{as_bytes(script_sig)};
    for (std::size_t offset{0}; offset < bytes.size();) {
        decoded_opcode decoded;
        if (!read_opcode(bytes, offset, decoded)) {
            return pushed_script{false, {}};
        }
        if (decoded.opcode > op_16) {
            return pushed_script{false, {}};
        }
        result.last_push = decoded.data;
    }
    return result;
}

[[nodiscard]] std::size_t p2sh_sigop_count(const tx_input& input, const coin& previous_coin) noexcept
{
    if (!is_pay_to_script_hash(previous_coin.output().script())) {
        return 0;
    }

    const auto pushed{last_pushed_script(input.script())};
    if (!pushed.push_only) {
        return 0;
    }
    return sigop_count(pushed.last_push, true);
}

[[nodiscard]] int witness_version(unsigned char opcode) noexcept
{
    if (opcode == op_0) {
        return 0;
    }
    return static_cast<int>(small_integer_value(opcode));
}

[[nodiscard]] witness_program parse_witness_program(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() < 4 || bytes.size() > 42) {
        return {};
    }

    const auto version_opcode{std::to_integer<unsigned char>(bytes[0])};
    if (version_opcode != op_0 && !is_small_integer_opcode(version_opcode)) {
        return {};
    }

    const auto program_size{static_cast<std::size_t>(std::to_integer<unsigned char>(bytes[1]))};
    if (program_size + 2 != bytes.size()) {
        return {};
    }

    return witness_program{
        witness_version(version_opcode),
        bytes.subspan(2, program_size),
        true};
}

[[nodiscard]] witness_program parse_witness_program(script_ref script) noexcept
{
    return parse_witness_program(as_bytes(script));
}

[[nodiscard]] std::size_t witness_sigop_count(
    witness_program program,
    std::span<const witness_item> witness) noexcept
{
    if (!program.present || program.version != 0) {
        return 0;
    }
    if (program.program.size() == witness_v0_keyhash_size) {
        return 1;
    }
    if (program.program.size() == witness_v0_scripthash_size && !witness.empty()) {
        return sigop_count(as_bytes(witness.back()), true);
    }
    return 0;
}

[[nodiscard]] std::size_t witness_sigop_count(
    const tx_input& input,
    script_ref previous_script,
    verification_flags flags) noexcept
{
    if (!flags.contains(verification_flags::witness())) {
        return 0;
    }

    const auto native_program{parse_witness_program(previous_script)};
    if (native_program.present) {
        return witness_sigop_count(native_program, input.witness());
    }

    if (!flags.contains(verification_flags::p2sh()) || !is_pay_to_script_hash(previous_script)) {
        return 0;
    }

    const auto pushed{last_pushed_script(input.script())};
    if (!pushed.push_only) {
        return 0;
    }
    return witness_sigop_count(parse_witness_program(pushed.last_push), input.witness());
}

[[nodiscard]] std::vector<std::byte> script_number(std::int32_t value)
{
    std::vector<std::byte> result;
    if (value == 0) {
        return result;
    }

    std::uint32_t remaining{static_cast<std::uint32_t>(value)};
    while (remaining > 0) {
        result.push_back(static_cast<std::byte>(remaining & 0xffU));
        remaining >>= 8U;
    }
    if ((std::to_integer<unsigned char>(result.back()) & 0x80U) != 0) {
        result.push_back(std::byte{0});
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> coinbase_height_prefix(block_height height)
{
    const auto value{height.value()};
    if (value == 0) {
        return {std::byte{0}};
    }
    if (value >= 1 && value <= 16) {
        return {static_cast<std::byte>(0x50 + value)};
    }

    const auto number{script_number(value)};
    std::vector<std::byte> prefix;
    prefix.reserve(number.size() + 1);
    prefix.push_back(static_cast<std::byte>(number.size()));
    prefix.insert(prefix.end(), number.begin(), number.end());
    return prefix;
}

[[nodiscard]] bool starts_with(std::span<const std::byte> bytes, std::span<const std::byte> prefix) noexcept
{
    return bytes.size() >= prefix.size() &&
           std::ranges::equal(prefix, bytes.first(prefix.size()));
}

[[nodiscard]] hash256 witness_commitment_hash(hash256 witness_root, const witness_item& nonce)
{
    std::array<std::byte, 64> bytes{};
    std::ranges::copy(as_bytes(witness_root), bytes.begin());
    std::ranges::copy(as_bytes(nonce), bytes.begin() + 32);
    return protocol_detail::double_sha256(bytes);
}

} // namespace

verify_result<block_facts> assess_block_intrinsic(
    const block& candidate,
    const block_local_context& context)
{
    return verify_exception_boundary<block_facts>([&]() -> verify_result<block_facts> {
        const auto transactions{candidate.transactions()};
        if (transactions.empty()) {
            return invalid(validation_rule_id::l01_block_non_empty, "block has no transactions");
        }

        const auto serialized{serialized_size(candidate)};
        const auto stripped_size{stripped_serialized_size(candidate)};
        const auto block_weight{weight(candidate)};
        const auto witness_root{witness_merkle_root(transactions)};
        const auto witness_present{has_witness(transactions)};
        const auto commitment{find_witness_commitment(transactions.front())};
        if (exceeds_weight_units(transactions.size(), context.limits.max_block_weight) ||
            exceeds_weight_units(stripped_size, context.limits.max_block_weight)) {
            return invalid(validation_rule_id::l04_original_block_size, "block stripped size exceeds maximum weight");
        }

        const auto merkle{merkle_root(transactions)};
        if (context.check_merkle_root && candidate.header().merkle_root() != merkle.root) {
            return invalid(validation_rule_id::l02_merkle_root, "block merkle root does not match transactions");
        }
        if (context.check_merkle_root && merkle.mutated) {
            return invalid(validation_rule_id::l03_merkle_mutation, "block merkle tree is mutated by duplicate hashes");
        }

        if (!is_coinbase(transactions.front())) {
            return invalid(validation_rule_id::l05_coinbase_position, "first transaction is not coinbase");
        }
        for (std::size_t index{1}; index < transactions.size(); ++index) {
            if (is_coinbase(transactions[index])) {
                return invalid(validation_rule_id::l05_coinbase_position, "block contains more than one coinbase");
            }
        }

        std::size_t sigop_count{0};
        std::vector<transaction_facts> transaction_facts;
        transaction_facts.reserve(transactions.size());
        for (const auto& tx : transactions) {
            const auto tx_result{assess_transaction_intrinsic(tx, context.limits.transactions)};
            if (tx_result.has_error()) {
                return verify_result<block_facts>::failed(tx_result.assume_error());
            }
            if (!tx_result.assume_value().accepted()) {
                return invalid(tx_result.assume_value().assume_rejection());
            }
            transaction_facts.push_back(tx_result.assume_value().assume_facts());
            sigop_count += legacy_sigop_count(tx);
        }
        if (sigop_count > context.limits.max_legacy_sigop_cost / witness_scale_factor) {
            return invalid(validation_rule_id::l06_legacy_sigops, "block legacy sigop count exceeds limit");
        }
        const auto sigop_cost{sigop_count * witness_scale_factor};

        return verify_result<block_facts>::ok(validation_decision<block_facts>::valid(
            block_facts{
                candidate.hash(),
                merkle.root,
                transactions.size(),
                serialized,
                stripped_size,
                block_weight,
                sigop_cost,
                sigop_cost,
                witness_root,
                witness_present,
                commitment,
                std::move(transaction_facts)}));
    },
                                                  "block local validation exhausted resources", operation_error_code::internal_bug, "block local validation threw an exception");
}

verify_result<block_facts> assess_block_contextual(
    const block& candidate,
    const block_contextual_context& context)
{
    return verify_exception_boundary<block_facts>([&]() -> verify_result<block_facts> {
        const auto local{assess_block_intrinsic(candidate, context.local)};
        if (local.has_error() || !local.assume_value().accepted()) {
            return local;
        }

        const auto& facts{local.assume_value().assume_facts()};
        const auto transactions{candidate.transactions()};

        if (!context.deployments.segwit_active && facts.has_witness()) {
            return invalid(validation_rule_id::c02_pre_segwit_no_witness, "pre-segwit block contains witness data");
        }

        if (facts.weight() > context.local.limits.max_block_weight) {
            return invalid(validation_rule_id::c03_block_weight, "block weight exceeds maximum weight");
        }

        transaction_finality_context finality;
        finality.limits = context.local.limits.transactions.limits;
        finality.height = context.height;
        finality.timestamp = context.locktime_cutoff;
        for (const auto& tx : transactions) {
            const auto tx_result{assess_transaction_finality(tx, finality)};
            if (tx_result.has_error()) {
                return verify_result<block_facts>::failed(tx_result.assume_error());
            }
            if (!tx_result.assume_value().accepted()) {
                return invalid(tx_result.assume_value().assume_rejection());
            }
        }

        if (context.deployments.height_in_coinbase_active) {
            const auto prefix{coinbase_height_prefix(context.height)};
            const auto coinbase_script{as_bytes(transactions.front().inputs().front().script())};
            if (!starts_with(coinbase_script, std::span<const std::byte>{prefix.data(), prefix.size()})) {
                return invalid(validation_rule_id::c04_coinbase_height, "coinbase height commitment is invalid");
            }
        }

        if (context.deployments.segwit_active && facts.has_witness() && !facts.commitment().present()) {
            return invalid(validation_rule_id::c05_witness_commitment_presence, "segwit block with witness data has no witness commitment");
        }

        if (context.deployments.segwit_active && facts.commitment().present()) {
            const auto coinbase_witness{transactions.front().inputs().front().witness()};
            if (coinbase_witness.size() != 1 || as_bytes(coinbase_witness.front()).size() != 32) {
                return invalid(validation_rule_id::c06_witness_nonce_presence, "coinbase witness nonce is not exactly one 32-byte item");
            }
            if (facts.commitment().hash() != witness_commitment_hash(facts.witness_merkle_root(), coinbase_witness.front())) {
                return invalid(validation_rule_id::c07_witness_merkle_commitment, "witness commitment hash does not match witness merkle root");
            }
        }

        return local;
    },
                                                  "block contextual validation exhausted resources", operation_error_code::internal_bug, "block contextual validation threw an exception");
}

namespace validation_support {
namespace {

class block_spend_tracker
{
public:
    struct created_lookup {
        std::size_t index{0};
        coin value;
    };

    class created_lookup_result
    {
    public:
        [[nodiscard]] static created_lookup_result missing() noexcept
        {
            return created_lookup_result{};
        }

        [[nodiscard]] static created_lookup_result found(created_lookup value)
        {
            created_lookup_result result;
            result.m_found = true;
            result.m_value = std::move(value);
            return result;
        }

        [[nodiscard]] bool found() const noexcept { return m_found; }
        [[nodiscard]] const created_lookup& assume_value() const&
        {
            assert(m_found);
            return m_value;
        }

    private:
        bool m_found{false};
        created_lookup m_value;
    };

    class spend_source
    {
    public:
        [[nodiscard]] static constexpr spend_source coin_index() noexcept
        {
            return spend_source{};
        }

        [[nodiscard]] static constexpr spend_source created_output(std::size_t index) noexcept
        {
            return spend_source{true, index};
        }

        [[nodiscard]] constexpr bool from_created_output() const noexcept { return m_from_created_output; }
        [[nodiscard]] constexpr std::size_t created_index() const noexcept
        {
            assert(m_from_created_output);
            return m_created_index;
        }

    private:
        constexpr spend_source() noexcept = default;
        constexpr spend_source(bool from_created_output, std::size_t created_index) noexcept : m_from_created_output{from_created_output},
                                                                                               m_created_index{created_index}
        {
        }

        bool m_from_created_output{false};
        std::size_t m_created_index{0};
    };

    [[nodiscard]] bool spent(const outpoint& point) const
    {
        return m_spent_points.contains(point);
    }

    [[nodiscard]] bool contains_created(const outpoint& point) const
    {
        return m_created_by_point.contains(point);
    }

    [[nodiscard]] created_lookup_result find_unspent_created(const outpoint& point) const
    {
        const auto created{m_created_by_point.find(point)};
        if (created == m_created_by_point.end()) return created_lookup_result::missing();

        const auto& entry{m_created[created->second]};
        if (entry.spent) return created_lookup_result::missing();

        return created_lookup_result::found(created_lookup{
            created->second,
            coin{
                entry.output.output,
                entry.output.height,
                entry.output.coinbase,
                entry.output.previous_median_time_past}});
    }

    void mark_spent(const outpoint& point, spend_source source)
    {
        const auto inserted{m_spent_points.insert(point)};
        assert(inserted.second);
        if (source.from_created_output()) {
            assert(source.created_index() < m_created.size());
            m_created[source.created_index()].spent = true;
        }
    }

    [[nodiscard]] bool add_created(created_output output)
    {
        if (m_created_by_point.contains(output.point)) return false;

        const auto point{output.point};
        const auto index{m_created.size()};
        m_created.push_back(created_entry{std::move(output)});
        try {
            m_created_by_point.emplace(point, index);
        } catch (...) {
            m_created.pop_back();
            throw;
        }
        return true;
    }

private:
    struct created_entry {
        created_output output;
        bool spent{false};
    };

    std::vector<created_entry> m_created;
    std::map<outpoint, std::size_t> m_created_by_point;
    std::set<outpoint> m_spent_points;
};

} // namespace

verify_result<block_facts> invalid_block(validation_rejection rejection)
{
    return verify_result<block_facts>::ok(validation_decision<block_facts>::invalid(rejection));
}

verify_result<block_facts> failed_block(operation_error error)
{
    return verify_result<block_facts>::failed(error);
}

std::size_t transaction_sigop_cost(
    const transaction& tx,
    std::span<const coin> input_coins,
    verification_flags flags)
{
    std::size_t sigop_cost{legacy_sigop_count(tx) * witness_scale_factor};
    if (is_coinbase(tx)) {
        return sigop_cost;
    }

    const auto inputs{tx.inputs()};
    const auto input_count{std::min(inputs.size(), input_coins.size())};
    for (std::size_t input_index{0}; input_index < input_count; ++input_index) {
        const auto& input{inputs[input_index]};
        const auto& previous_coin{input_coins[input_index]};
        if (flags.contains(verification_flags::p2sh())) {
            sigop_cost += p2sh_sigop_count(input, previous_coin) * witness_scale_factor;
        }
        sigop_cost += witness_sigop_count(input, previous_coin.output().script(), flags);
    }

    return sigop_cost;
}

verify_result<block_facts> assess_block_spends_with_callbacks(
    const block& candidate,
    const block_spend_context& context,
    const void* coins,
    coin_lookup_callback lookup_coin)
{
    assert(coins);
    assert(lookup_coin);
    return verify_exception_boundary<block_facts>([&]() -> verify_result<block_facts> {
        const auto local{assess_block_intrinsic(candidate, context.local)};
        if (local.has_error() || !local.assume_value().accepted()) {
            return local;
        }

        const auto& local_facts{local.assume_value().assume_facts()};
        const auto transactions{candidate.transactions()};
        block_spend_tracker overlay;
        std::vector<created_output> created_outputs;
        std::vector<spent_output> spent_outputs;
        std::vector<spend_facts> spends;
        amount total_fees{0};
        std::size_t sigop_cost{0};

        const auto reject_rule{[](validation_rule_id id, static_text reason) {
            return invalid_block(validation_rejection::rule(
                validation_rejection_code::rule_violation,
                id,
                reason));
        }};

        const auto output_count{[](const transaction& tx) {
            return tx.outputs().size();
        }};

        const auto add_sigop_cost{[&](std::size_t transaction_sigop_cost) {
            if (transaction_sigop_cost > context.max_sigop_cost ||
                sigop_cost > context.max_sigop_cost - transaction_sigop_cost) {
                return false;
            }
            sigop_cost += transaction_sigop_cost;
            return true;
        }};

        for (std::size_t tx_index{0}; tx_index < transactions.size(); ++tx_index) {
            const auto& tx{transactions[tx_index]};
            const auto& tx_facts{local_facts.transactions()[tx_index]};

            if (tx_facts.coinbase()) {
                if (!add_sigop_cost(transaction_sigop_cost(tx, {}, context.scripts.flags))) {
                    return reject_rule(validation_rule_id::s03_sigop_cost, "block sigop cost exceeds limit");
                }
            } else {
                std::vector<coin> input_coins;
                input_coins.reserve(tx.inputs().size());
                std::vector<block_spend_tracker::spend_source> overlay_inputs;

                for (const auto& input : tx.inputs()) {
                    const auto point{input.previous_output()};
                    if (overlay.spent(point)) {
                        return reject_rule(validation_rule_id::s02_prevouts_unspent, "transaction double-spends a prevout in this block");
                    }

                    const auto overlay_match{overlay.find_unspent_created(point)};
                    if (overlay_match.found()) {
                        const auto& created{overlay_match.assume_value()};
                        overlay_inputs.push_back(block_spend_tracker::spend_source::created_output(created.index));
                        input_coins.push_back(created.value);
                        continue;
                    }

                    auto lookup_result{lookup_coin(coins, point)};
                    if (lookup_result.has_error()) {
                        return failed_block(lookup_result.assume_error());
                    }

                    auto lookup{std::move(lookup_result).assume_value()};
                    if (!lookup) {
                        return reject_rule(validation_rule_id::s02_prevouts_unspent, "transaction input prevout is not present");
                    }
                    input_coins.push_back(std::move(*lookup));
                    overlay_inputs.push_back(block_spend_tracker::spend_source::coin_index());
                }

                const std::span<const coin> input_coin_view{input_coins.data(), input_coins.size()};
                const auto spend_result{assess_transaction_spends_with_coins(
                    tx,
                    tx_facts,
                    context.spend,
                    input_coin_view)};
                if (spend_result.has_error()) {
                    return failed_block(spend_result.assume_error());
                }
                if (!spend_result.assume_value().accepted()) {
                    return invalid_block(spend_result.assume_value().assume_rejection());
                }

                const auto& spend{spend_result.assume_value().assume_facts()};
                if (total_fees.satoshis() > context.spend.limits.max_money.satoshis() - spend.fee().satoshis()) {
                    return reject_rule(validation_rule_id::s06_input_value_and_fee_range, "block fee total exceeds maximum money");
                }
                total_fees = amount{total_fees.satoshis() + spend.fee().satoshis()};
                spends.push_back(spend);

                if (!add_sigop_cost(transaction_sigop_cost(tx, input_coin_view, context.scripts.flags))) {
                    return reject_rule(validation_rule_id::s03_sigop_cost, "block sigop cost exceeds limit");
                }

                const auto script_result{verify_transaction_scripts(
                    tx,
                    context.spend,
                    context.scripts,
                    input_coin_view)};
                if (script_result.has_error()) {
                    return failed_block(script_result.assume_error());
                }
                if (!script_result.assume_value().accepted()) {
                    return invalid_block(script_result.assume_value().assume_rejection());
                }

                for (std::size_t input_index{0}; input_index < tx.inputs().size(); ++input_index) {
                    const auto point{tx.inputs()[input_index].previous_output()};
                    spent_outputs.push_back(spent_output{point, input_coins[input_index]});
                    overlay.mark_spent(point, overlay_inputs[input_index]);
                }
            }

            for (std::size_t output_index{0}; output_index < output_count(tx); ++output_index) {
                const auto point{outpoint{tx_facts.id(), tx_output_index{static_cast<std::uint32_t>(output_index)}}};
                if (context.deployments.enforce_bip30) {
                    if (overlay.contains_created(point)) {
                        return reject_rule(validation_rule_id::s01_bip30_duplicate_unspent, "block creates a duplicate unspent output");
                    }

                    auto lookup_result{lookup_coin(coins, point)};
                    if (lookup_result.has_error()) {
                        return failed_block(lookup_result.assume_error());
                    }

                    auto lookup{std::move(lookup_result).assume_value()};
                    if (lookup) {
                        return reject_rule(validation_rule_id::s01_bip30_duplicate_unspent, "block overwrites an unspent output");
                    }
                }

                created_output created{
                    point,
                    tx.outputs()[output_index],
                    context.spend.height,
                    tx_facts.coinbase(),
                    context.spend.previous_median_time_past};
                if (!overlay.add_created(created)) {
                    return reject_rule(validation_rule_id::s01_bip30_duplicate_unspent, "block creates a duplicate output");
                }
                created_outputs.push_back(std::move(created));
            }
        }

        const auto coinbase_value{local_facts.transactions().front().output_value()};
        if (context.subsidy.satoshis() > context.spend.limits.max_money.satoshis() - total_fees.satoshis()) {
            return reject_rule(validation_rule_id::s04_coinbase_subsidy, "block subsidy plus fees exceeds maximum money");
        }
        const auto allowed_reward{context.subsidy.satoshis() + total_fees.satoshis()};
        if (coinbase_value.satoshis() > allowed_reward) {
            return reject_rule(validation_rule_id::s04_coinbase_subsidy, "coinbase pays more than subsidy plus fees");
        }

        return verify_result<block_facts>::ok(validation_decision<block_facts>::valid(
            block_facts{
                local_facts.hash(),
                local_facts.merkle_root(),
                local_facts.transaction_count(),
                local_facts.serialized_size(),
                local_facts.stripped_size(),
                local_facts.weight(),
                local_facts.legacy_sigop_cost(),
                sigop_cost,
                local_facts.witness_merkle_root(),
                local_facts.has_witness(),
                local_facts.commitment(),
                std::vector<transaction_facts>{local_facts.transactions().begin(), local_facts.transactions().end()},
                std::move(spends),
                std::move(spent_outputs),
                std::move(created_outputs),
                total_fees,
                coinbase_value}));
    },
                                                  "block spend validation exhausted resources", operation_error_code::internal_bug, "block spend validation threw an exception");
}

} // namespace validation_support

} // namespace bitcoin
