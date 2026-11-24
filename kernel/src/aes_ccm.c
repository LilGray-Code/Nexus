#include "aes_ccm.h"

#include <stdint.h>

#define AES_BLOCK_BYTES 16U
#define AES_ROUND_KEY_BYTES 176U

static const uint8_t aes_sbox[256] = {
    0x63U,0x7CU,0x77U,0x7BU,0xF2U,0x6BU,0x6FU,0xC5U,0x30U,0x01U,0x67U,0x2BU,0xFEU,0xD7U,0xABU,0x76U,
    0xCAU,0x82U,0xC9U,0x7DU,0xFAU,0x59U,0x47U,0xF0U,0xADU,0xD4U,0xA2U,0xAFU,0x9CU,0xA4U,0x72U,0xC0U,
    0xB7U,0xFDU,0x93U,0x26U,0x36U,0x3FU,0xF7U,0xCCU,0x34U,0xA5U,0xE5U,0xF1U,0x71U,0xD8U,0x31U,0x15U,
    0x04U,0xC7U,0x23U,0xC3U,0x18U,0x96U,0x05U,0x9AU,0x07U,0x12U,0x80U,0xE2U,0xEBU,0x27U,0xB2U,0x75U,
    0x09U,0x83U,0x2CU,0x1AU,0x1BU,0x6EU,0x5AU,0xA0U,0x52U,0x3BU,0xD6U,0xB3U,0x29U,0xE3U,0x2FU,0x84U,
    0x53U,0xD1U,0x00U,0xEDU,0x20U,0xFCU,0xB1U,0x5BU,0x6AU,0xCBU,0xBEU,0x39U,0x4AU,0x4CU,0x58U,0xCFU,
    0xD0U,0xEFU,0xAAU,0xFBU,0x43U,0x4DU,0x33U,0x85U,0x45U,0xF9U,0x02U,0x7FU,0x50U,0x3CU,0x9FU,0xA8U,
    0x51U,0xA3U,0x40U,0x8FU,0x92U,0x9DU,0x38U,0xF5U,0xBCU,0xB6U,0xDAU,0x21U,0x10U,0xFFU,0xF3U,0xD2U,
    0xCDU,0x0CU,0x13U,0xECU,0x5FU,0x97U,0x44U,0x17U,0xC4U,0xA7U,0x7EU,0x3DU,0x64U,0x5DU,0x19U,0x73U,
    0x60U,0x81U,0x4FU,0xDCU,0x22U,0x2AU,0x90U,0x88U,0x46U,0xEEU,0xB8U,0x14U,0xDEU,0x5EU,0x0BU,0xDBU,
    0xE0U,0x32U,0x3AU,0x0AU,0x49U,0x06U,0x24U,0x5CU,0xC2U,0xD3U,0xACU,0x62U,0x91U,0x95U,0xE4U,0x79U,
    0xE7U,0xC8U,0x37U,0x6DU,0x8DU,0xD5U,0x4EU,0xA9U,0x6CU,0x56U,0xF4U,0xEAU,0x65U,0x7AU,0xAEU,0x08U,
    0xBAU,0x78U,0x25U,0x2EU,0x1CU,0xA6U,0xB4U,0xC6U,0xE8U,0xDDU,0x74U,0x1FU,0x4BU,0xBDU,0x8BU,0x8AU,
    0x70U,0x3EU,0xB5U,0x66U,0x48U,0x03U,0xF6U,0x0EU,0x61U,0x35U,0x57U,0xB9U,0x86U,0xC1U,0x1DU,0x9EU,
    0xE1U,0xF8U,0x98U,0x11U,0x69U,0xD9U,0x8EU,0x94U,0x9BU,0x1EU,0x87U,0xE9U,0xCEU,0x55U,0x28U,0xDFU,
    0x8CU,0xA1U,0x89U,0x0DU,0xBFU,0xE6U,0x42U,0x68U,0x41U,0x99U,0x2DU,0x0FU,0xB0U,0x54U,0xBBU,0x16U
};

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i)
        destination[i] = source[i];
}

static uint8_t aes_xtime(uint8_t value) {
    return (uint8_t)((value << 1) ^ ((value & 0x80U) != 0U ? 0x1BU : 0U));
}

static void aes_expand_key(const uint8_t key[AES_BLOCK_BYTES],
                           uint8_t round_keys[AES_ROUND_KEY_BYTES]) {
    static const uint8_t round_constant[10] = {
        0x01U,0x02U,0x04U,0x08U,0x10U,0x20U,0x40U,0x80U,0x1BU,0x36U
    };
    copy_bytes(round_keys, key, AES_BLOCK_BYTES);
    size_t generated = AES_BLOCK_BYTES;
    size_t round = 0U;
    uint8_t temporary[4];
    while (generated < AES_ROUND_KEY_BYTES) {
        copy_bytes(temporary, round_keys + generated - 4U, sizeof(temporary));
        if ((generated % AES_BLOCK_BYTES) == 0U) {
            const uint8_t first = temporary[0];
            temporary[0] = aes_sbox[temporary[1]];
            temporary[1] = aes_sbox[temporary[2]];
            temporary[2] = aes_sbox[temporary[3]];
            temporary[3] = aes_sbox[first];
            temporary[0] ^= round_constant[round++];
        }
        for (size_t i = 0U; i < 4U; ++i) {
            round_keys[generated] =
                (uint8_t)(round_keys[generated - AES_BLOCK_BYTES] ^
                          temporary[i]);
            ++generated;
        }
    }
    secure_zero(temporary, sizeof(temporary));
}

static void aes_add_round_key(uint8_t state[AES_BLOCK_BYTES],
                              const uint8_t *round_key) {
    for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
        state[i] ^= round_key[i];
}

static void aes_sub_bytes(uint8_t state[AES_BLOCK_BYTES]) {
    for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
        state[i] = aes_sbox[state[i]];
}

static void aes_shift_rows(uint8_t state[AES_BLOCK_BYTES]) {
    uint8_t temporary;
    temporary = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[3];
    state[3] = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = temporary;
}

static void aes_mix_columns(uint8_t state[AES_BLOCK_BYTES]) {
    for (size_t column = 0U; column < 4U; ++column) {
        const size_t base = column * 4U;
        const uint8_t a = state[base];
        const uint8_t b = state[base + 1U];
        const uint8_t c = state[base + 2U];
        const uint8_t d = state[base + 3U];
        const uint8_t total = (uint8_t)(a ^ b ^ c ^ d);
        state[base] ^= (uint8_t)(total ^ aes_xtime((uint8_t)(a ^ b)));
        state[base + 1U] ^= (uint8_t)(total ^ aes_xtime((uint8_t)(b ^ c)));
        state[base + 2U] ^= (uint8_t)(total ^ aes_xtime((uint8_t)(c ^ d)));
        state[base + 3U] ^= (uint8_t)(total ^ aes_xtime((uint8_t)(d ^ a)));
    }
}

static void aes_encrypt_block(const uint8_t round_keys[AES_ROUND_KEY_BYTES],
                              const uint8_t input[AES_BLOCK_BYTES],
                              uint8_t output[AES_BLOCK_BYTES]) {
    uint8_t state[AES_BLOCK_BYTES];
    copy_bytes(state, input, sizeof(state));
    aes_add_round_key(state, round_keys);
    for (size_t round = 1U; round < 10U; ++round) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, round_keys + round * AES_BLOCK_BYTES);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, round_keys + 10U * AES_BLOCK_BYTES);
    copy_bytes(output, state, sizeof(state));
    secure_zero(state, sizeof(state));
}

static void cbc_mac_block(const uint8_t round_keys[AES_ROUND_KEY_BYTES],
                          uint8_t mac[AES_BLOCK_BYTES],
                          const uint8_t block[AES_BLOCK_BYTES]) {
    for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
        mac[i] ^= block[i];
    uint8_t encrypted[AES_BLOCK_BYTES];
    aes_encrypt_block(round_keys, mac, encrypted);
    copy_bytes(mac, encrypted, sizeof(encrypted));
    secure_zero(encrypted, sizeof(encrypted));
}

static bool ranges_partially_overlap(const void *first, size_t first_length,
                                     const void *second, size_t second_length) {
    if (first_length == 0U || second_length == 0U || first == second)
        return false;
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    if (a > UINTPTR_MAX - first_length ||
        b > UINTPTR_MAX - second_length) return true;
    return a < b + second_length && b < a + first_length;
}

static bool ccm_arguments_valid(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *input, size_t input_length,
    uint8_t *output) {
    if (key == NULL || nonce == NULL ||
        (aad == NULL && aad_length != 0U) ||
        (input == NULL && input_length != 0U) ||
        (output == NULL && input_length != 0U) ||
        aad_length > NEXUS_AES_CCM_MAX_AAD_BYTES ||
        input_length > NEXUS_AES_CCM_MAX_PAYLOAD_BYTES)
        return false;
    if (ranges_partially_overlap(input, input_length, output, input_length))
        return false;
    return true;
}

static void ccm_compute_mac(
    const uint8_t round_keys[AES_ROUND_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t mac[AES_BLOCK_BYTES]) {
    uint8_t block[AES_BLOCK_BYTES] = {0U};
    const uint8_t flags = (uint8_t)(
        (aad_length != 0U ? 0x40U : 0U) |
        (((NEXUS_AES_CCM_TAG_BYTES - 2U) / 2U) << 3) |
        (2U - 1U));
    block[0] = flags;
    copy_bytes(block + 1U, nonce, NEXUS_AES_CCM_NONCE_BYTES);
    block[14] = (uint8_t)(plaintext_length >> 8);
    block[15] = (uint8_t)plaintext_length;
    for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i) mac[i] = 0U;
    cbc_mac_block(round_keys, mac, block);

    uint8_t aad_encoded[2U + NEXUS_AES_CCM_MAX_AAD_BYTES];
    if (aad_length != 0U) {
        aad_encoded[0] = (uint8_t)(aad_length >> 8);
        aad_encoded[1] = (uint8_t)aad_length;
        copy_bytes(aad_encoded + 2U, aad, aad_length);
        const size_t encoded_length = aad_length + 2U;
        for (size_t offset = 0U; offset < encoded_length;
             offset += AES_BLOCK_BYTES) {
            for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i) {
                const size_t index = offset + i;
                block[i] = index < encoded_length ? aad_encoded[index] : 0U;
            }
            cbc_mac_block(round_keys, mac, block);
        }
        secure_zero(aad_encoded, sizeof(aad_encoded));
    }

    for (size_t offset = 0U; offset < plaintext_length;
         offset += AES_BLOCK_BYTES) {
        const size_t amount = plaintext_length - offset < AES_BLOCK_BYTES
            ? plaintext_length - offset : AES_BLOCK_BYTES;
        for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
            block[i] = i < amount ? plaintext[offset + i] : 0U;
        cbc_mac_block(round_keys, mac, block);
    }
    secure_zero(block, sizeof(block));
}

static void ccm_counter_block(
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    uint16_t counter, uint8_t block[AES_BLOCK_BYTES]) {
    block[0] = 1U; /* L - 1, where L = 2 bytes. */
    copy_bytes(block + 1U, nonce, NEXUS_AES_CCM_NONCE_BYTES);
    block[14] = (uint8_t)(counter >> 8);
    block[15] = (uint8_t)counter;
}

static void ccm_tag_from_mac(
    const uint8_t round_keys[AES_ROUND_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t mac[AES_BLOCK_BYTES],
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES]) {
    uint8_t counter[AES_BLOCK_BYTES];
    uint8_t stream[AES_BLOCK_BYTES];
    ccm_counter_block(nonce, 0U, counter);
    aes_encrypt_block(round_keys, counter, stream);
    for (size_t i = 0U; i < NEXUS_AES_CCM_TAG_BYTES; ++i)
        tag[i] = (uint8_t)(mac[i] ^ stream[i]);
    secure_zero(counter, sizeof(counter));
    secure_zero(stream, sizeof(stream));
}

static void ccm_crypt_payload(
    const uint8_t round_keys[AES_ROUND_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *input, size_t length, uint8_t *output) {
    uint8_t counter[AES_BLOCK_BYTES];
    uint8_t stream[AES_BLOCK_BYTES];
    uint8_t input_block[AES_BLOCK_BYTES];
    uint16_t block_counter = 1U;
    for (size_t offset = 0U; offset < length; offset += AES_BLOCK_BYTES) {
        const size_t amount = length - offset < AES_BLOCK_BYTES
            ? length - offset : AES_BLOCK_BYTES;
        for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
            input_block[i] = i < amount ? input[offset + i] : 0U;
        ccm_counter_block(nonce, block_counter++, counter);
        aes_encrypt_block(round_keys, counter, stream);
        for (size_t i = 0U; i < amount; ++i)
            output[offset + i] = (uint8_t)(input_block[i] ^ stream[i]);
    }
    secure_zero(counter, sizeof(counter));
    secure_zero(stream, sizeof(stream));
    secure_zero(input_block, sizeof(input_block));
}

bool nexus_aes_ccm_encrypt_128(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext,
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES]) {
    if (tag == NULL) return false;
    secure_zero(tag, NEXUS_AES_CCM_TAG_BYTES);
    if (!ccm_arguments_valid(key, nonce, aad, aad_length,
                             plaintext, plaintext_length, ciphertext))
        return false;

    uint8_t round_keys[AES_ROUND_KEY_BYTES];
    uint8_t mac[AES_BLOCK_BYTES];
    aes_expand_key(key, round_keys);
    ccm_compute_mac(round_keys, nonce, aad, aad_length,
                    plaintext, plaintext_length, mac);
    ccm_tag_from_mac(round_keys, nonce, mac, tag);
    if (plaintext_length != 0U)
        ccm_crypt_payload(round_keys, nonce, plaintext,
                          plaintext_length, ciphertext);
    secure_zero(round_keys, sizeof(round_keys));
    secure_zero(mac, sizeof(mac));
    return true;
}

bool nexus_aes_ccm_decrypt_128(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    const uint8_t tag[NEXUS_AES_CCM_TAG_BYTES],
    uint8_t *plaintext) {
    if (!ccm_arguments_valid(key, nonce, aad, aad_length,
                             ciphertext, ciphertext_length, plaintext) ||
        tag == NULL) return false;

    uint8_t round_keys[AES_ROUND_KEY_BYTES];
    uint8_t mac[AES_BLOCK_BYTES];
    uint8_t expected[NEXUS_AES_CCM_TAG_BYTES];
    aes_expand_key(key, round_keys);
    if (ciphertext_length != 0U)
        ccm_crypt_payload(round_keys, nonce, ciphertext,
                          ciphertext_length, plaintext);
    ccm_compute_mac(round_keys, nonce, aad, aad_length,
                    plaintext, ciphertext_length, mac);
    ccm_tag_from_mac(round_keys, nonce, mac, expected);

    uint8_t difference = 0U;
    for (size_t i = 0U; i < NEXUS_AES_CCM_TAG_BYTES; ++i)
        difference |= (uint8_t)(expected[i] ^ tag[i]);
    if (difference != 0U && ciphertext_length != 0U)
        secure_zero(plaintext, ciphertext_length);

    secure_zero(round_keys, sizeof(round_keys));
    secure_zero(mac, sizeof(mac));
    secure_zero(expected, sizeof(expected));
    return difference == 0U;
}
