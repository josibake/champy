// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoinkernel.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define TEST_PROCESS_ID _getpid
#define TEST_REMOVE_DIR _rmdir
#else
#include <unistd.h>
#define TEST_PROCESS_ID getpid
#define TEST_REMOVE_DIR rmdir
#endif

static void clear_error(btck_Error** error)
{
    if (*error != NULL) {
        btck_error_destroy(*error);
        *error = NULL;
    }
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_hex(const char* hex, unsigned char* out, size_t out_capacity, size_t* out_len)
{
    const size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    if (hex_len / 2 > out_capacity) return -1;

    for (size_t i = 0; i < hex_len / 2; ++i) {
        const int high = hex_digit(hex[2 * i]);
        const int low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) return -1;
        out[i] = (unsigned char)((high << 4) | low);
    }
    *out_len = hex_len / 2;
    return 0;
}

static int check_block_validation_result(
    const btck_BlockValidationState* state,
    btck_ValidationMode expected_mode,
    btck_BlockValidationResult expected_result)
{
    if (state == NULL) return 1;
    if (btck_block_validation_state_get_validation_mode(state) != expected_mode) return 2;
    if (btck_block_validation_state_get_block_validation_result(state) != expected_result) return 3;
    return 0;
}

static int check_block_verify_rejection(
    const btck_BlockVerifyResult* result,
    btck_ValidationRule expected_rule,
    const char* expected_rule_code)
{
    size_t text_len = 0;
    const char* text = NULL;

    if (result == NULL) return 1;
    if (btck_block_verify_result_get_rejection_code(result) != btck_ValidationRejectionCode_RULE_VIOLATION) return 2;
    if (btck_block_verify_result_get_rejection_rule(result) != expected_rule) return 3;

    text = btck_block_verify_result_get_rejection_rule_code(result, &text_len);
    if (text == NULL) return 4;
    if (text_len != strlen(expected_rule_code)) return 5;
    if (memcmp(text, expected_rule_code, text_len) != 0) return 6;

    text = btck_block_verify_result_get_rejection_reason(result, &text_len);
    if (text == NULL) return 7;
    if (text_len == 0) return 8;

    return 0;
}

static int validation_coin_lookup(
    void* user_data,
    const btck_TransactionOutPoint* out_point,
    btck_CoinLookupResult* result)
{
    (void)user_data;
    (void)out_point;
    if (result == NULL) return 1;
    result->status = btck_CoinLookupStatus_MISSING;
    result->coin = NULL;
    return 0;
}

int main(void)
{
    btck_Error* error = NULL;
    btck_BlockValidationOptions* options = btck_block_validation_options_create(&error);
    if (options == NULL) return 1;
    if (error != NULL) return 73;

    if (btck_block_validation_options_set_current_time(options, -1, &error) == 0) return 2;
    if (error == NULL) return 3;
    if (btck_error_get_code(error) != btck_ErrorCode_INVALID_ARGUMENT) return 4;
    btck_error_destroy(error);
    error = NULL;

    if (btck_block_validation_options_set_current_time(options, 2000000000, &error) != 0) return 5;
    if (error != NULL) return 6;
    btck_block_validation_options_destroy(options);
    options = NULL;

    const unsigned char malformed_header[] = {0x01, 0x02, 0x03};
    btck_BlockHeaderParseResult* parse_result =
        btck_block_header_parse_result(malformed_header, sizeof(malformed_header), &error);
    if (parse_result == NULL) return 7;
    if (error != NULL) return 8;
    if (btck_block_header_parse_result_get_status(parse_result) != btck_ParseStatus_MALFORMED) return 9;
    if (btck_block_header_parse_result_get_header(parse_result) != NULL) return 10;
    if (btck_block_header_parse_result_get_failure_code(parse_result) != btck_ParseFailureCode_TRUNCATED) return 71;
    if (btck_block_header_parse_result_get_failure_offset(parse_result) != 0) return 72;
    btck_block_header_parse_result_destroy(parse_result);
    parse_result = NULL;

    btck_Context* context = btck_context_create(NULL, &error);
    if (context == NULL) return 11;
    if (error != NULL) return 12;

    char data_dir[160];
    char blocks_dir[192];
    snprintf(data_dir, sizeof(data_dir), "c_api_validation_consumer_data_%ld", (long)TEST_PROCESS_ID());
    snprintf(blocks_dir, sizeof(blocks_dir), "%s/blocks", data_dir);

    btck_ChainstateOptions* chainstate_options = btck_chainstate_options_create(
        context, data_dir, strlen(data_dir), blocks_dir, strlen(blocks_dir), &error);
    if (chainstate_options == NULL) return 13;
    if (error != NULL) return 14;
    if (btck_chainstate_options_set_in_memory(chainstate_options, 1, &error) != 0) return 15;
    if (error != NULL) return 74;

    btck_ChainstateRuntime* runtime = btck_chainstate_runtime_create(&error);
    if (runtime == NULL) return 16;
    if (error != NULL) return 75;
    if (btck_chainstate_runtime_set_current_time(runtime, 2000000000, &error) != 0) return 17;
    if (error != NULL) return 18;

    btck_Chainstate* chainstate = btck_chainstate_open(chainstate_options, runtime, &error);
    if (chainstate == NULL) return 19;
    if (error != NULL) return 20;

    static const char block_1_hex[] =
        "010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd"
        "1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e36299"
        "0101000000010000000000000000000000000000000000000000000000000000000000000000ffff"
        "ffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec1"
        "1600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf62"
        "1e73a82cbf2342c858eeac00000000";
    unsigned char block_1[256];
    size_t block_1_len = 0;
    if (decode_hex(block_1_hex, block_1, sizeof(block_1), &block_1_len) != 0) return 21;

    static const char genesis_header_hex[] =
        "010000000000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49ffff001d1dac2b7c";
    unsigned char genesis_header_bytes[80];
    size_t genesis_header_len = 0;
    if (decode_hex(genesis_header_hex, genesis_header_bytes, sizeof(genesis_header_bytes), &genesis_header_len) != 0) return 77;
    if (genesis_header_len != 80) return 78;

    btck_BlockHeaderParseResult* genesis_header_parse =
        btck_block_header_parse_result(genesis_header_bytes, genesis_header_len, &error);
    if (genesis_header_parse == NULL) return 79;
    if (error != NULL) return 80;
    if (btck_block_header_parse_result_get_status(genesis_header_parse) != btck_ParseStatus_OK) return 81;
    const btck_BlockHeader* genesis_header = btck_block_header_parse_result_get_header(genesis_header_parse);
    if (genesis_header == NULL) return 82;

    btck_BlockHeaderParseResult* header_parse = btck_block_header_parse_result(block_1, 80, &error);
    if (header_parse == NULL) return 22;
    if (error != NULL) return 23;
    if (btck_block_header_parse_result_get_status(header_parse) != btck_ParseStatus_OK) return 24;
    const btck_BlockHeader* header = btck_block_header_parse_result_get_header(header_parse);
    if (header == NULL) return 25;

    options = btck_block_validation_options_create(&error);
    if (options == NULL) return 26;
    if (error != NULL) return 76;
    if (btck_block_validation_options_set_current_time(options, 1231462464, &error) != 0) return 27;
    if (error != NULL) return 28;

    btck_HeaderProcessResult* header_result =
        btck_chainstate_process_header_result(chainstate, header, options, &error);
    if (header_result == NULL) return 29;
    if (error != NULL) return 30;
    if (btck_header_process_result_get_status(header_result) != btck_HeaderProcessStatus_REJECTED) return 31;
    if (check_block_validation_result(
            btck_header_process_result_get_validation_state(header_result),
            btck_ValidationMode_INVALID,
            btck_BlockValidationResult_TIME_FUTURE) != 0) return 32;
    btck_header_process_result_destroy(header_result);
    header_result = NULL;

    if (btck_block_validation_options_set_current_time(options, 2000000000, &error) != 0) return 33;
    if (error != NULL) return 34;
    header_result = btck_chainstate_process_header_result(chainstate, header, options, &error);
    if (header_result == NULL) return 35;
    if (error != NULL) return 36;
    if (btck_header_process_result_get_status(header_result) != btck_HeaderProcessStatus_ACCEPTED) return 37;
    if (check_block_validation_result(
            btck_header_process_result_get_validation_state(header_result),
            btck_ValidationMode_VALID,
            btck_BlockValidationResult_UNSET) != 0) return 38;
    btck_header_process_result_destroy(header_result);
    header_result = NULL;

    btck_BlockParseResult* block_parse = btck_block_parse_result(block_1, block_1_len, &error);
    if (block_parse == NULL) return 39;
    if (error != NULL) return 40;
    if (btck_block_parse_result_get_status(block_parse) != btck_ParseStatus_OK) return 41;
    const btck_Block* block = btck_block_parse_result_get_block(block_parse);
    if (block == NULL) return 42;

    btck_ChainParameters* mainnet_params = btck_chain_parameters_create(btck_ChainType_MAINNET, &error);
    if (mainnet_params == NULL) return 83;
    if (error != NULL) return 84;
    const btck_ConsensusParams* consensus_params = btck_chain_parameters_get_consensus_params(mainnet_params);
    if (consensus_params == NULL) return 85;

    const btck_BlockHeader* ancestors[] = {genesis_header};
    btck_BlockValidationLibraryOptions library_options;
    memset(&library_options, 0, sizeof(library_options));
    library_options.operation_time = 2000000000;
    library_options.segwit_active = 0;
    library_options.height_in_coinbase_active = 0;
    library_options.enforce_bip30 = 0;
    library_options.subsidy = 5000000000;
    library_options.coinbase_maturity = 100;
    library_options.script_flags = btck_ValidationScriptFlags_NONE;
    library_options.max_sigop_cost = 0;

    btck_ValidationCoinIndex coin_index;
    memset(&coin_index, 0, sizeof(coin_index));
    coin_index.lookup = validation_coin_lookup;

    btck_BlockVerifyResult* library_result =
        btck_block_verify_result(block, ancestors, 1, consensus_params, library_options, coin_index, &error);
    if (library_result == NULL) return 86;
    if (error != NULL) return 87;
    if (btck_block_verify_result_get_status(library_result) != btck_CheckStatus_VALID) return 88;
    if (check_block_validation_result(
            btck_block_verify_result_get_validation_state(library_result),
            btck_ValidationMode_VALID,
            btck_BlockValidationResult_UNSET) != 0) return 89;
    if (btck_block_verify_result_get_rejection_code(library_result) != btck_ValidationRejectionCode_NONE) return 94;
    if (btck_block_verify_result_get_rejection_rule(library_result) != btck_ValidationRule_NONE) return 95;
    {
        size_t text_len = 1;
        if (btck_block_verify_result_get_rejection_rule_code(library_result, &text_len) != NULL) return 96;
        if (text_len != 0) return 97;
        text_len = 1;
        if (btck_block_verify_result_get_rejection_reason(library_result, &text_len) != NULL) return 98;
        if (text_len != 0) return 99;
    }
    btck_block_verify_result_destroy(library_result);
    library_result = NULL;

    library_options.subsidy = 0;
    library_result =
        btck_block_verify_result(block, ancestors, 1, consensus_params, library_options, coin_index, &error);
    if (library_result == NULL) return 90;
    if (error != NULL) return 91;
    if (btck_block_verify_result_get_status(library_result) != btck_CheckStatus_INVALID) return 92;
    if (check_block_validation_result(
            btck_block_verify_result_get_validation_state(library_result),
            btck_ValidationMode_INVALID,
            btck_BlockValidationResult_CONSENSUS) != 0) return 93;
    {
        const int rejection_check = check_block_verify_rejection(
            library_result,
            btck_ValidationRule_S04_COINBASE_SUBSIDY,
            "S04");
        if (rejection_check != 0) return 100 + rejection_check;
    }
    btck_block_verify_result_destroy(library_result);
    library_result = NULL;

    library_options.subsidy = 5000000000;
    library_result =
        btck_block_verify_result(block, NULL, 0, consensus_params, library_options, coin_index, &error);
    if (library_result == NULL) return 110;
    if (error != NULL) return 111;
    if (btck_block_verify_result_get_status(library_result) != btck_CheckStatus_INVALID) return 112;
    if (check_block_validation_result(
            btck_block_verify_result_get_validation_state(library_result),
            btck_ValidationMode_INVALID,
            btck_BlockValidationResult_INVALID_HEADER) != 0) return 113;
    {
        const int rejection_check = check_block_verify_rejection(
            library_result,
            btck_ValidationRule_H01_PREVIOUS_HASH_PARENT,
            "H01");
        if (rejection_check != 0) return 120 + rejection_check;
    }
    btck_block_verify_result_destroy(library_result);
    library_result = NULL;

    const btck_BlockHeader* invalid_ancestors[] = {header};
    library_result =
        btck_block_verify_result(block, invalid_ancestors, 1, consensus_params, library_options, coin_index, &error);
    if (library_result == NULL) return 130;
    if (error != NULL) return 131;
    if (btck_block_verify_result_get_status(library_result) != btck_CheckStatus_INVALID) return 132;
    if (check_block_validation_result(
            btck_block_verify_result_get_validation_state(library_result),
            btck_ValidationMode_INVALID,
            btck_BlockValidationResult_INVALID_PREV) != 0) return 133;
    if (btck_block_verify_result_get_rejection_code(library_result) != btck_ValidationRejectionCode_NONE) return 134;
    if (btck_block_verify_result_get_rejection_rule(library_result) != btck_ValidationRule_NONE) return 135;
    if (btck_block_verify_result_get_header_context_evidence_code(library_result) !=
        btck_HeaderContextEvidenceCode_GENESIS_PARENT_NOT_NULL) return 136;
    {
        size_t text_len = 1;
        if (btck_block_verify_result_get_rejection_rule_code(library_result, &text_len) != NULL) return 137;
        if (text_len != 0) return 138;
        text_len = 1;
        if (btck_block_verify_result_get_rejection_reason(library_result, &text_len) != NULL) return 139;
        if (text_len != 0) return 140;
        text_len = 0;
        if (btck_block_verify_result_get_header_context_evidence_reason(library_result, &text_len) == NULL) return 141;
        if (text_len == 0) return 142;
    }
    btck_block_verify_result_destroy(library_result);
    library_result = NULL;

    btck_chain_parameters_destroy(mainnet_params);
    mainnet_params = NULL;

    btck_BlockProcessResult* block_result =
        btck_chainstate_process_block_result(chainstate, block, options, &error);
    if (block_result == NULL) return 43;
    if (error != NULL) return 44;
    if (btck_block_process_result_get_status(block_result) != btck_BlockProcessStatus_STORED) return 45;
    if (btck_block_process_result_has_new_block_data(block_result) != 1) return 46;
    if (check_block_validation_result(
            btck_block_process_result_get_validation_state(block_result),
            btck_ValidationMode_VALID,
            btck_BlockValidationResult_UNSET) != 0) return 47;
    btck_block_process_result_destroy(block_result);
    block_result = NULL;

    btck_block_parse_result_destroy(block_parse);
    btck_block_header_parse_result_destroy(header_parse);
    btck_block_header_parse_result_destroy(genesis_header_parse);
    btck_block_validation_options_destroy(options);
    btck_chainstate_destroy(chainstate);
    btck_chainstate_runtime_destroy(runtime);
    btck_chainstate_options_destroy(chainstate_options);
    btck_context_destroy(context);
    clear_error(&error);
    TEST_REMOVE_DIR(blocks_dir);
    TEST_REMOVE_DIR(data_dir);

    return 0;
}
