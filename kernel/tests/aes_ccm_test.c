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



static void test_rfc3394_aes128_key_unwrap_vector(void) {
    static const uint8_t kek[NEXUS_AES128_KEY_BYTES] = {
        0x00U,0x01U,0x02U,0x03U,0x04U,0x05U,0x06U,0x07U,
        0x08U,0x09U,0x0AU,0x0BU,0x0CU,0x0DU,0x0EU,0x0FU
    };
    static const uint8_t wrapped[24] = {
        0x1FU,0xA6U,0x8BU,0x0AU,0x81U,0x12U,0xB4U,0x47U,
        0xAEU,0xF3U,0x4BU,0xD8U,0xFBU,0x5AU,0x7BU,0x82U,
        0x9DU,0x3EU,0x86U,0x23U,0x71U,0xD2U,0xCFU,0xE5U
    };
    static const char expected[] = "00112233445566778899aabbccddeeff";
    uint8_t plaintext[16];
    size_t plaintext_length = 99U;
    assert(nexus_aes_key_unwrap_128(kek, wrapped, sizeof(wrapped),
                                    plaintext, sizeof(plaintext),
                                    &plaintext_length));
    assert(plaintext_length == sizeof(plaintext));
    expect_hex(plaintext, sizeof(plaintext), expected);
}

static void test_rfc3394_aes128_key_unwrap_longer_vector(void) {
    static const uint8_t kek[NEXUS_AES128_KEY_BYTES] = {
        0x00U,0x01U,0x02U,0x03U,0x04U,0x05U,0x06U,0x07U,
        0x08U,0x09U,0x0AU,0x0BU,0x0CU,0x0DU,0x0EU,0x0FU
    };
    static const uint8_t wrapped[32] = {
        0x88U,0x96U,0x71U,0x10U,0x65U,0x35U,0xA9U,0xF8U,
        0x6DU,0x9FU,0x9AU,0x26U,0x2FU,0x67U,0x45U,0x69U,
        0xEFU,0xA3U,0x8DU,0x75U,0x35U,0xAAU,0xC7U,0x75U,
        0x27U,0xCAU,0xB9U,0x28U,0x55U,0xBDU,0xDDU,0x6EU
    };
    static const char expected[] =
        "00112233445566778899aabbccddeeff0001020304050607";
    uint8_t plaintext[24];
    size_t plaintext_length = 0U;
    assert(nexus_aes_key_unwrap_128(kek, wrapped, sizeof(wrapped),
                                    plaintext, sizeof(plaintext),
                                    &plaintext_length));
    assert(plaintext_length == sizeof(plaintext));
    expect_hex(plaintext, sizeof(plaintext), expected);
}

static void test_rfc3394_key_unwrap_rejects_bad_iv_and_lengths(void) {
    static const uint8_t kek[NEXUS_AES128_KEY_BYTES] = {
        0x00U,0x01U,0x02U,0x03U,0x04U,0x05U,0x06U,0x07U,
        0x08U,0x09U,0x0AU,0x0BU,0x0CU,0x0DU,0x0EU,0x0FU
    };
    static const uint8_t valid_wrapped[24] = {
        0x1FU,0xA6U,0x8BU,0x0AU,0x81U,0x12U,0xB4U,0x47U,
        0xAEU,0xF3U,0x4BU,0xD8U,0xFBU,0x5AU,0x7BU,0x82U,
        0x9DU,0x3EU,0x86U,0x23U,0x71U,0xD2U,0xCFU,0xE5U
    };
    uint8_t modified[24];
    uint8_t plaintext[16];
    size_t plaintext_length = 123U;

    for (size_t i = 0U; i < sizeof(modified); ++i)
        modified[i] = valid_wrapped[i];
    modified[0] ^= 0x01U;
    for (size_t i = 0U; i < sizeof(plaintext); ++i) plaintext[i] = 0xA5U;
    assert(!nexus_aes_key_unwrap_128(
        kek, modified, sizeof(modified), plaintext, sizeof(plaintext),
        &plaintext_length));
    assert(plaintext_length == 0U);
    for (size_t i = 0U; i < sizeof(plaintext); ++i) assert(plaintext[i] == 0U);

    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, 23U, plaintext, sizeof(plaintext),
        &plaintext_length));
    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, 25U, plaintext, sizeof(plaintext),
        &plaintext_length));
    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, sizeof(valid_wrapped), plaintext, 15U,
        &plaintext_length));
    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, sizeof(valid_wrapped), plaintext,
        sizeof(plaintext), NULL));
    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, sizeof(valid_wrapped), NULL, 0U,
        &plaintext_length));
    assert(!nexus_aes_key_unwrap_128(
        kek, valid_wrapped, sizeof(valid_wrapped), (uint8_t *)valid_wrapped,
        sizeof(valid_wrapped), &plaintext_length));
}

int main(void) {
    test_ccm_independent_vector_and_round_trip();
    test_empty_payload_vector();
    test_in_place_and_authentication_failure();
    test_reject_limits_nulls_and_partial_overlap();
    test_rfc3394_aes128_key_unwrap_vector();
    test_rfc3394_aes128_key_unwrap_longer_vector();
    test_rfc3394_key_unwrap_rejects_bad_iv_and_lengths();
    return 0;
}
