#include "aes_ccm.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f')
        return (uint8_t)(value - 'a' + 10);
    assert(0);
    return 0U;
}

static void expect_hex(const uint8_t *actual, size_t length,
                       const char *expected) {
    for (size_t i = 0U; i < length; ++i) {
        const uint8_t wanted =
            (uint8_t)((hex_nibble(expected[i * 2U]) << 4) |
                      hex_nibble(expected[i * 2U + 1U]));
        assert(actual[i] == wanted);
    }
}

static void test_ccm_independent_vector_and_round_trip(void) {
    uint8_t key[NEXUS_AES128_KEY_BYTES];
    uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES];
    uint8_t plaintext[49];
    uint8_t ciphertext[49];
    uint8_t recovered[49];
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES];
    uint8_t aad[40];
    static const char expected_ciphertext[] =
        "59ac42d773ab1ae74c9c9f73b8b41ce3249b9ec79ffbcb850d90b54f938d95c2"
        "ebb5f91c7289fd6353ca4c75ca21b5be20";
    static const char expected_tag[] = "f067a2f6f812d18d";
    static const char aad_text[] = "Nexus CCMP additional authenticated data";

    for (size_t i = 0U; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(nonce); ++i)
        nonce[i] = (uint8_t)(0xA0U + i);
    for (size_t i = 0U; i < sizeof(plaintext); ++i)
        plaintext[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(aad); ++i)
        aad[i] = (uint8_t)aad_text[i];

    assert(nexus_aes_ccm_encrypt_128(
        key, nonce, aad, sizeof(aad), plaintext, sizeof(plaintext),
        ciphertext, tag));
    expect_hex(ciphertext, sizeof(ciphertext), expected_ciphertext);
    expect_hex(tag, sizeof(tag), expected_tag);
    assert(nexus_aes_ccm_decrypt_128(
        key, nonce, aad, sizeof(aad), ciphertext, sizeof(ciphertext),
        tag, recovered));
    for (size_t i = 0U; i < sizeof(plaintext); ++i)
        assert(recovered[i] == plaintext[i]);
}

static void test_empty_payload_vector(void) {
    uint8_t key[NEXUS_AES128_KEY_BYTES];
    uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES];
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES];
    static const char expected_tag[] = "1de0b930515748d5";
    for (size_t i = 0U; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(nonce); ++i)
        nonce[i] = (uint8_t)(0xA0U + i);
    assert(nexus_aes_ccm_encrypt_128(
        key, nonce, NULL, 0U, NULL, 0U, NULL, tag));
    expect_hex(tag, sizeof(tag), expected_tag);
    assert(nexus_aes_ccm_decrypt_128(
        key, nonce, NULL, 0U, NULL, 0U, tag, NULL));
}

static void test_in_place_and_authentication_failure(void) {
    uint8_t key[NEXUS_AES128_KEY_BYTES];
    uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES];
    uint8_t aad[7] = {1U,2U,3U,4U,5U,6U,7U};
    uint8_t data[32];
    uint8_t original[32];
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES];
    for (size_t i = 0U; i < sizeof(key); ++i) key[i] = (uint8_t)(0xF0U - i);
    for (size_t i = 0U; i < sizeof(nonce); ++i) nonce[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 3U);
    for (size_t i = 0U; i < sizeof(data); ++i) original[i] = data[i];

    assert(nexus_aes_ccm_encrypt_128(
        key, nonce, aad, sizeof(aad), data, sizeof(data), data, tag));
    assert(nexus_aes_ccm_decrypt_128(
        key, nonce, aad, sizeof(aad), data, sizeof(data), tag, data));
    for (size_t i = 0U; i < sizeof(data); ++i)
        assert(data[i] == original[i]);

    assert(nexus_aes_ccm_encrypt_128(
        key, nonce, aad, sizeof(aad), original, sizeof(original), data, tag));
    tag[3] ^= 0x80U;
    assert(!nexus_aes_ccm_decrypt_128(
        key, nonce, aad, sizeof(aad), data, sizeof(data), tag, original));
    for (size_t i = 0U; i < sizeof(original); ++i) assert(original[i] == 0U);
}

static void test_reject_limits_nulls_and_partial_overlap(void) {
    uint8_t key[NEXUS_AES128_KEY_BYTES] = {0U};
    uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES] = {0U};
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES];
    uint8_t buffer[64] = {0U};
    uint8_t too_large_aad[NEXUS_AES_CCM_MAX_AAD_BYTES + 1U] = {0U};
    uint8_t too_large_payload[NEXUS_AES_CCM_MAX_PAYLOAD_BYTES + 1U] = {0U};

    assert(!nexus_aes_ccm_encrypt_128(
        key, nonce, too_large_aad, sizeof(too_large_aad),
        buffer, 1U, buffer, tag));
    assert(!nexus_aes_ccm_encrypt_128(
        key, nonce, NULL, 0U, too_large_payload, sizeof(too_large_payload),
        too_large_payload, tag));
    assert(!nexus_aes_ccm_encrypt_128(
        key, nonce, NULL, 1U, buffer, 1U, buffer, tag));
    assert(!nexus_aes_ccm_encrypt_128(
        key, nonce, NULL, 0U, NULL, 1U, buffer, tag));
    assert(!nexus_aes_ccm_encrypt_128(
        key, nonce, NULL, 0U, buffer, 16U, buffer + 1U, tag));
    assert(!nexus_aes_ccm_decrypt_128(
        key, nonce, NULL, 0U, buffer, 1U, tag, NULL));
}

int main(void) {
    test_ccm_independent_vector_and_round_trip();
    test_empty_payload_vector();
    test_in_place_and_authentication_failure();
    test_reject_limits_nulls_and_partial_overlap();
    return 0;
}
