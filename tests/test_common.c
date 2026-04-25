/*
 * tests/test_common.c — rcunit tests for common.c utilities
 *
 * Covers three modules:
 *   hex    — bytes_to_hex / hex_to_bytes
 *   replay — check_replay sliding-window logic
 *   crypto — encrypt_packet / decrypt_packet round-trips
 *
 * Build & run (Linux / inside Docker):
 *   make test
 */
#include "rcunit.h"
#include "common.h"

/* ------------------------------------------------------------------ */
/* hex utilities                                                        */
/* ------------------------------------------------------------------ */

RCU_TEST(test_hex_roundtrip) {
    const unsigned char in[4] = {0xde, 0xad, 0xbe, 0xef};
    char hex[9];
    unsigned char out[4];

    bytes_to_hex(in, 4, hex);
    RCU_ASSERT_EQUAL_STRING("deadbeef", hex);

    RCU_ASSERT_EQUAL(RCU_E_OK, hex_to_bytes(hex, out, 4));
    RCU_ASSERT_SAME_BYTE_ARRAY(in, out, 4);
}

RCU_TEST(test_hex_all_zeros) {
    const unsigned char in[4] = {0};
    char hex[9];
    unsigned char out[4];

    bytes_to_hex(in, 4, hex);
    RCU_ASSERT_EQUAL_STRING("00000000", hex);
    RCU_ASSERT_EQUAL(RCU_E_OK, hex_to_bytes(hex, out, 4));
    RCU_ASSERT_SAME_BYTE_ARRAY(in, out, 4);
}

RCU_TEST(test_hex_all_ff) {
    const unsigned char in[4] = {0xff, 0xff, 0xff, 0xff};
    char hex[9];
    unsigned char out[4];

    bytes_to_hex(in, 4, hex);
    RCU_ASSERT_EQUAL_STRING("ffffffff", hex);
    RCU_ASSERT_EQUAL(RCU_E_OK, hex_to_bytes(hex, out, 4));
    RCU_ASSERT_SAME_BYTE_ARRAY(in, out, 4);
}

RCU_TEST(test_hex_wrong_length_short) {
    unsigned char out[4];
    RCU_ASSERT_EQUAL(-1, hex_to_bytes("dead", out, 4)); /* 4 chars, need 8 */
}

RCU_TEST(test_hex_wrong_length_long) {
    unsigned char out[4];
    RCU_ASSERT_EQUAL(-1, hex_to_bytes("deadbeefff", out, 4)); /* 10 chars, need 8 */
}

RCU_TEST(test_hex_invalid_chars) {
    unsigned char out[4];
    RCU_ASSERT_EQUAL(-1, hex_to_bytes("deadbXef", out, 4));
}

RCU_DEF_FUNC_TBL(hex_tests)
    RCU_INC_TEST(test_hex_roundtrip)
    RCU_INC_TEST(test_hex_all_zeros)
    RCU_INC_TEST(test_hex_all_ff)
    RCU_INC_TEST(test_hex_wrong_length_short)
    RCU_INC_TEST(test_hex_wrong_length_long)
    RCU_INC_TEST(test_hex_invalid_chars)
RCU_DEF_FUNC_TBL_END

/* ------------------------------------------------------------------ */
/* replay window                                                        */
/* ------------------------------------------------------------------ */

static uint64_t  rp_highest;
static uint64_t  rp_window[REPLAY_WINDOW_WORDS];

RCU_SETUP(replay_setup) {
    rp_highest = 0;
    memset(rp_window, 0, sizeof(rp_window));
}

RCU_TEST(test_replay_first_packet) {
    RCU_ASSERT_EQUAL(0, check_replay(1, &rp_highest, rp_window));
    RCU_ASSERT_EQUAL(1, (long)rp_highest);
}

RCU_TEST(test_replay_duplicate_rejected) {
    RCU_ASSERT_EQUAL(0,  check_replay(5, &rp_highest, rp_window));
    RCU_ASSERT_EQUAL(-1, check_replay(5, &rp_highest, rp_window));
}

RCU_TEST(test_replay_out_of_order_accepted) {
    RCU_ASSERT_EQUAL(0,  check_replay(10, &rp_highest, rp_window));
    RCU_ASSERT_EQUAL(0,  check_replay(8,  &rp_highest, rp_window)); /* late but in window */
    RCU_ASSERT_EQUAL(-1, check_replay(8,  &rp_highest, rp_window)); /* duplicate */
}

RCU_TEST(test_replay_too_old_rejected) {
    RCU_ASSERT_EQUAL(0,  check_replay(3000, &rp_highest, rp_window));
    RCU_ASSERT_EQUAL(-1, check_replay(1,    &rp_highest, rp_window)); /* behind 2048-bit window */
}

RCU_TEST(test_replay_advancing_window) {
    /* Fill a range, then advance so earlier seqs fall outside */
    for (uint64_t i = 1; i <= 10; i++)
        RCU_ASSERT_EQUAL(0, check_replay(i, &rp_highest, rp_window));
    /* All of 1-10 should be duplicates */
    for (uint64_t i = 1; i <= 10; i++)
        RCU_ASSERT_EQUAL(-1, check_replay(i, &rp_highest, rp_window));
}

RCU_TEST(test_replay_window_boundary) {
    /* seq at exactly the window edge should still be accepted */
    RCU_ASSERT_EQUAL(0, check_replay(REPLAY_WINDOW_WORDS * 64, &rp_highest, rp_window));
    RCU_ASSERT_EQUAL(0, check_replay(1, &rp_highest, rp_window)); /* just inside */
    RCU_ASSERT_EQUAL(-1, check_replay(0, &rp_highest, rp_window)); /* one step outside */
}

RCU_DEF_FUNC_TBL(replay_tests)
    RCU_INC_TEST_FXT(test_replay_first_packet,      replay_setup, NULL)
    RCU_INC_TEST_FXT(test_replay_duplicate_rejected, replay_setup, NULL)
    RCU_INC_TEST_FXT(test_replay_out_of_order_accepted, replay_setup, NULL)
    RCU_INC_TEST_FXT(test_replay_too_old_rejected,   replay_setup, NULL)
    RCU_INC_TEST_FXT(test_replay_advancing_window,   replay_setup, NULL)
    RCU_INC_TEST_FXT(test_replay_window_boundary,    replay_setup, NULL)
RCU_DEF_FUNC_TBL_END

/* ------------------------------------------------------------------ */
/* encrypt / decrypt                                                    */
/* ------------------------------------------------------------------ */

static EVP_CIPHER_CTX *enc_ctx;
static EVP_CIPHER_CTX *dec_ctx;

static const unsigned char TEST_KEY[CRYPTO_KEY_LEN] = {
    0x01,0x02,0x03,0x04, 0x05,0x06,0x07,0x08,
    0x09,0x0a,0x0b,0x0c, 0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14, 0x15,0x16,0x17,0x18,
    0x19,0x1a,0x1b,0x1c, 0x1d,0x1e,0x1f,0x20,
};

RCU_SETUP(crypto_setup) {
    enc_ctx = EVP_CIPHER_CTX_new();
    dec_ctx = EVP_CIPHER_CTX_new();
    RCU_ASSERT_NOT_NULL(enc_ctx);
    RCU_ASSERT_NOT_NULL(dec_ctx);
}

RCU_TEARDOWN(crypto_teardown) {
    EVP_CIPHER_CTX_free(enc_ctx);
    EVP_CIPHER_CTX_free(dec_ctx);
    enc_ctx = dec_ctx = NULL;
}

RCU_TEST(test_crypto_roundtrip) {
    const unsigned char plain[] = "hello lane-cove-tunnel";
    int plain_len = sizeof(plain) - 1;
    unsigned char wire[BUFFER_SIZE + WIRE_OVERHEAD];
    unsigned char recovered[BUFFER_SIZE];
    int wire_len = 0, recovered_len = 0;

    RCU_ASSERT_EQUAL(0, encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire, &wire_len));
    RCU_ASSERT(wire_len > plain_len); /* ciphertext + IV + tag */

    RCU_ASSERT_EQUAL(0, decrypt_packet(dec_ctx, TEST_KEY, wire, wire_len, recovered, &recovered_len));
    RCU_ASSERT_EQUAL(plain_len, recovered_len);
    RCU_ASSERT_SAME_BYTE_ARRAY(plain, recovered, plain_len);
}

RCU_TEST(test_crypto_empty_payload) {
    unsigned char wire[WIRE_OVERHEAD];
    unsigned char recovered[BUFFER_SIZE];
    int wire_len = 0, recovered_len = 0;

    RCU_ASSERT_EQUAL(0, encrypt_packet(enc_ctx, TEST_KEY, NULL, 0, wire, &wire_len));
    RCU_ASSERT_EQUAL(CRYPTO_OVERHEAD, wire_len); /* IV + tag only */
    RCU_ASSERT_EQUAL(0, decrypt_packet(dec_ctx, TEST_KEY, wire, wire_len, recovered, &recovered_len));
    RCU_ASSERT_EQUAL(0, recovered_len);
}

RCU_TEST(test_crypto_wrong_key_rejected) {
    const unsigned char bad_key[CRYPTO_KEY_LEN] = {0xff};
    const unsigned char plain[] = "secret";
    int plain_len = sizeof(plain) - 1;
    unsigned char wire[BUFFER_SIZE + WIRE_OVERHEAD];
    unsigned char recovered[BUFFER_SIZE];
    int wire_len = 0, recovered_len = 0;

    RCU_ASSERT_EQUAL(0,  encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire, &wire_len));
    RCU_ASSERT_EQUAL(-1, decrypt_packet(dec_ctx, bad_key,  wire, wire_len, recovered, &recovered_len));
}

RCU_TEST(test_crypto_truncated_tag_rejected) {
    const unsigned char plain[] = "secret";
    int plain_len = sizeof(plain) - 1;
    unsigned char wire[BUFFER_SIZE + WIRE_OVERHEAD];
    unsigned char recovered[BUFFER_SIZE];
    int wire_len = 0, recovered_len = 0;

    RCU_ASSERT_EQUAL(0,  encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire, &wire_len));
    RCU_ASSERT_EQUAL(-1, decrypt_packet(dec_ctx, TEST_KEY, wire, wire_len - 4, recovered, &recovered_len));
}

RCU_TEST(test_crypto_tampered_ciphertext_rejected) {
    const unsigned char plain[] = "secret";
    int plain_len = sizeof(plain) - 1;
    unsigned char wire[BUFFER_SIZE + WIRE_OVERHEAD];
    unsigned char recovered[BUFFER_SIZE];
    int wire_len = 0, recovered_len = 0;

    RCU_ASSERT_EQUAL(0, encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire, &wire_len));
    wire[CRYPTO_IV_LEN] ^= 0xff; /* flip a byte in the ciphertext */
    RCU_ASSERT_EQUAL(-1, decrypt_packet(dec_ctx, TEST_KEY, wire, wire_len, recovered, &recovered_len));
}

RCU_TEST(test_crypto_ciphertexts_differ_same_plain) {
    const unsigned char plain[] = "hello";
    int plain_len = sizeof(plain) - 1;
    unsigned char wire1[BUFFER_SIZE + WIRE_OVERHEAD];
    unsigned char wire2[BUFFER_SIZE + WIRE_OVERHEAD];
    int len1 = 0, len2 = 0;

    RCU_ASSERT_EQUAL(0, encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire1, &len1));
    RCU_ASSERT_EQUAL(0, encrypt_packet(enc_ctx, TEST_KEY, plain, plain_len, wire2, &len2));
    RCU_ASSERT_EQUAL(len1, len2);
    /* IVs are random so wire packets must differ */
    RCU_ASSERT_NOT_SAME_BYTE_ARRAY(wire1, wire2, len1);
}

RCU_DEF_FUNC_TBL(crypto_tests)
    RCU_INC_TEST_FXT(test_crypto_roundtrip,                   crypto_setup, crypto_teardown)
    RCU_INC_TEST_FXT(test_crypto_empty_payload,               crypto_setup, crypto_teardown)
    RCU_INC_TEST_FXT(test_crypto_wrong_key_rejected,          crypto_setup, crypto_teardown)
    RCU_INC_TEST_FXT(test_crypto_truncated_tag_rejected,      crypto_setup, crypto_teardown)
    RCU_INC_TEST_FXT(test_crypto_tampered_ciphertext_rejected,crypto_setup, crypto_teardown)
    RCU_INC_TEST_FXT(test_crypto_ciphertexts_differ_same_plain,crypto_setup, crypto_teardown)
RCU_DEF_FUNC_TBL_END

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

RCU_DEF_MOD_TBL(all_modules)
    RCU_INC_MOD("hex",    hex_tests)
    RCU_INC_MOD("replay", replay_tests)
    RCU_INC_MOD("crypto", crypto_tests)
RCU_DEF_MOD_TBL_END

int main(void) {
    rcu_add_test_mod_tbl(rcu_get_default_reg(), all_modules);
    return rcu_run_tests();
}
