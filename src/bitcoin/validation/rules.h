// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BITCOIN_VALIDATION_RULES_H
#define BITCOIN_BITCOIN_VALIDATION_RULES_H

#include <array>
#include <cstddef>
#include <string_view>

namespace bitcoin {

enum class validation_rule_id {
    h01_previous_hash_parent,
    h02_proof_of_work,
    h03_difficulty_transition,
    h04_median_time_past,
    h05_future_time,
    h06_retired_version,
    h07_timewarp,
    l01_block_non_empty,
    l02_merkle_root,
    l03_merkle_mutation,
    l04_original_block_size,
    l05_coinbase_position,
    l06_legacy_sigops,
    l07_transaction_inputs_non_empty,
    l08_transaction_outputs_non_empty,
    l09_transaction_size,
    l10_output_value_non_negative,
    l11_output_value_range,
    l12_unique_inputs,
    l13_coinbase_script_size,
    l14_non_coinbase_prevout,
    c01_transaction_finality,
    c02_pre_segwit_no_witness,
    c03_block_weight,
    c04_coinbase_height,
    c05_witness_commitment_presence,
    c06_witness_nonce_presence,
    c07_witness_merkle_commitment,
    s01_bip30_duplicate_unspent,
    s02_prevouts_unspent,
    s03_sigop_cost,
    s04_coinbase_subsidy,
    s05_outputs_do_not_exceed_inputs,
    s06_input_value_and_fee_range,
    s07_scripts_validate,
    s08_sequence_locks,
    s09_coinbase_maturity,
};

class validation_rule_descriptor
{
public:
    constexpr validation_rule_descriptor(
        validation_rule_id id,
        std::string_view code,
        std::string_view statement) noexcept :
        m_id{id}, m_code{code}, m_statement{statement}
    {
    }

    [[nodiscard]] constexpr validation_rule_id id() const noexcept { return m_id; }
    [[nodiscard]] constexpr std::string_view code() const noexcept { return m_code; }
    [[nodiscard]] constexpr std::string_view statement() const noexcept { return m_statement; }

private:
    validation_rule_id m_id;
    std::string_view m_code;
    std::string_view m_statement;
};

inline constexpr std::array validation_rule_inventory{
    validation_rule_descriptor{validation_rule_id::h01_previous_hash_parent, "H01", "previous hash references supplied parent or null genesis parent"},
    validation_rule_descriptor{validation_rule_id::h02_proof_of_work, "H02", "proof of work satisfies target"},
    validation_rule_descriptor{validation_rule_id::h03_difficulty_transition, "H03", "target satisfies difficulty transition"},
    validation_rule_descriptor{validation_rule_id::h04_median_time_past, "H04", "timestamp is greater than median time past"},
    validation_rule_descriptor{validation_rule_id::h05_future_time, "H05", "timestamp is not too far after validation time"},
    validation_rule_descriptor{validation_rule_id::h06_retired_version, "H06", "version is not below active minimum version"},
    validation_rule_descriptor{validation_rule_id::h07_timewarp, "H07", "timewarp constraints hold at enforced adjustment boundaries"},
    validation_rule_descriptor{validation_rule_id::l01_block_non_empty, "L01", "block contains at least one transaction"},
    validation_rule_descriptor{validation_rule_id::l02_merkle_root, "L02", "merkle root matches transactions"},
    validation_rule_descriptor{validation_rule_id::l03_merkle_mutation, "L03", "merkle mutation is rejected where consensus requires it"},
    validation_rule_descriptor{validation_rule_id::l04_original_block_size, "L04", "original block size is within limit"},
    validation_rule_descriptor{validation_rule_id::l05_coinbase_position, "L05", "first transaction is the only coinbase"},
    validation_rule_descriptor{validation_rule_id::l06_legacy_sigops, "L06", "legacy sigops are within limit"},
    validation_rule_descriptor{validation_rule_id::l07_transaction_inputs_non_empty, "L07", "transaction has inputs"},
    validation_rule_descriptor{validation_rule_id::l08_transaction_outputs_non_empty, "L08", "transaction has outputs"},
    validation_rule_descriptor{validation_rule_id::l09_transaction_size, "L09", "transaction stripped and full sizes are within limits"},
    validation_rule_descriptor{validation_rule_id::l10_output_value_non_negative, "L10", "outputs are non-negative"},
    validation_rule_descriptor{validation_rule_id::l11_output_value_range, "L11", "outputs sum within money range"},
    validation_rule_descriptor{validation_rule_id::l12_unique_inputs, "L12", "transaction inputs are unique"},
    validation_rule_descriptor{validation_rule_id::l13_coinbase_script_size, "L13", "coinbase script size is valid"},
    validation_rule_descriptor{validation_rule_id::l14_non_coinbase_prevout, "L14", "non-coinbase prevouts are non-null"},
    validation_rule_descriptor{validation_rule_id::c01_transaction_finality, "C01", "transactions are final"},
    validation_rule_descriptor{validation_rule_id::c02_pre_segwit_no_witness, "C02", "pre-SegWit block has no witness data"},
    validation_rule_descriptor{validation_rule_id::c03_block_weight, "C03", "block weight is within limit"},
    validation_rule_descriptor{validation_rule_id::c04_coinbase_height, "C04", "coinbase height commitment is valid"},
    validation_rule_descriptor{validation_rule_id::c05_witness_commitment_presence, "C05", "witness commitment presence is valid"},
    validation_rule_descriptor{validation_rule_id::c06_witness_nonce_presence, "C06", "witness nonce presence is valid"},
    validation_rule_descriptor{validation_rule_id::c07_witness_merkle_commitment, "C07", "witness merkle commitment is valid"},
    validation_rule_descriptor{validation_rule_id::s01_bip30_duplicate_unspent, "S01", "BIP30 duplicate unspent outpoints are rejected"},
    validation_rule_descriptor{validation_rule_id::s02_prevouts_unspent, "S02", "prevouts exist and are unspent"},
    validation_rule_descriptor{validation_rule_id::s03_sigop_cost, "S03", "total sigop cost is within limit"},
    validation_rule_descriptor{validation_rule_id::s04_coinbase_subsidy, "S04", "coinbase subsidy is not excessive"},
    validation_rule_descriptor{validation_rule_id::s05_outputs_do_not_exceed_inputs, "S05", "outputs do not exceed inputs"},
    validation_rule_descriptor{validation_rule_id::s06_input_value_and_fee_range, "S06", "input values and fees remain within money range"},
    validation_rule_descriptor{validation_rule_id::s07_scripts_validate, "S07", "scripts validate"},
    validation_rule_descriptor{validation_rule_id::s08_sequence_locks, "S08", "sequence locks are satisfied"},
    validation_rule_descriptor{validation_rule_id::s09_coinbase_maturity, "S09", "coinbase maturity is satisfied"},
};

inline constexpr std::size_t validation_rule_count{validation_rule_inventory.size()};

[[nodiscard]] constexpr const validation_rule_descriptor* find_validation_rule(validation_rule_id id) noexcept
{
    for (const auto& rule : validation_rule_inventory) {
        if (rule.id() == id) {
            return &rule;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr std::string_view validation_rule_code(validation_rule_id id) noexcept
{
    const auto* rule{find_validation_rule(id)};
    return rule != nullptr ? rule->code() : std::string_view{};
}

} // namespace bitcoin

#endif // BITCOIN_BITCOIN_VALIDATION_RULES_H
