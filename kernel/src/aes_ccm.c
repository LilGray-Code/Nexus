#include "aes_ccm.h"

#include <stdint.h>

#define AES_BLOCK_BYTES 16U
#define AES_ROUND_KEY_BYTES 176U




/* Fixed-iteration GF(2^8) arithmetic avoids secret-indexed S-box lookups. */
static uint8_t gf256_multiply(uint8_t a, uint8_t b) {
    uint8_t result = 0U;
    for (size_t bit = 0U; bit < 8U; ++bit) {
        const uint8_t b_mask = (uint8_t)(0U - (uint8_t)(b & 1U));
        const uint8_t high_mask = (uint8_t)(0U - (uint8_t)(a >> 7));
        result ^= (uint8_t)(a & b_mask);
        a = (uint8_t)((uint8_t)(a << 1) ^
                      (uint8_t)(0x1BU & high_mask));
        b = (uint8_t)(b >> 1);
    }
    return result;
}

static uint8_t rotate_left_byte(uint8_t value, unsigned count) {
    return (uint8_t)((uint8_t)(value << count) |
                     (uint8_t)(value >> (8U - count)));
}

static uint8_t aes_sub_byte(uint8_t value) {
    const uint8_t x2 = gf256_multiply(value, value);
    const uint8_t x4 = gf256_multiply(x2, x2);
    const uint8_t x8 = gf256_multiply(x4, x4);
    const uint8_t x16 = gf256_multiply(x8, x8);
    const uint8_t x32 = gf256_multiply(x16, x16);
    const uint8_t x64 = gf256_multiply(x32, x32);
    const uint8_t x128 = gf256_multiply(x64, x64);

    uint8_t inverse = gf256_multiply(x128, x64);
    inverse = gf256_multiply(inverse, x32);
    inverse = gf256_multiply(inverse, x16);
    inverse = gf256_multiply(inverse, x8);
    inverse = gf256_multiply(inverse, x4);
    inverse = gf256_multiply(inverse, x2);
    return (uint8_t)(inverse ^ rotate_left_byte(inverse, 1U) ^
                     rotate_left_byte(inverse, 2U) ^
                     rotate_left_byte(inverse, 3U) ^
                     rotate_left_byte(inverse, 4U) ^ 0x63U);
}

static uint8_t gf256_inverse(uint8_t value) {
    const uint8_t x2 = gf256_multiply(value, value);
    const uint8_t x4 = gf256_multiply(x2, x2);
    const uint8_t x8 = gf256_multiply(x4, x4);
    const uint8_t x16 = gf256_multiply(x8, x8);
    const uint8_t x32 = gf256_multiply(x16, x16);
    const uint8_t x64 = gf256_multiply(x32, x32);
    const uint8_t x128 = gf256_multiply(x64, x64);

    uint8_t inverse = gf256_multiply(x128, x64);
    inverse = gf256_multiply(inverse, x32);
    inverse = gf256_multiply(inverse, x16);
    inverse = gf256_multiply(inverse, x8);
    inverse = gf256_multiply(inverse, x4);
    inverse = gf256_multiply(inverse, x2);
    return inverse;
}

static uint8_t aes_inv_sub_byte(uint8_t value) {
    const uint8_t affine_inverse =
        (uint8_t)(rotate_left_byte(value, 1U) ^
                  rotate_left_byte(value, 3U) ^
                  rotate_left_byte(value, 6U) ^ 0x05U);
    return gf256_inverse(affine_inverse);
}

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
    const uint8_t high_mask = (uint8_t)(0U - (uint8_t)(value >> 7));
    return (uint8_t)((uint8_t)(value << 1) ^
                     (uint8_t)(0x1BU & high_mask));
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
            temporary[0] = aes_sub_byte(temporary[1]);
            temporary[1] = aes_sub_byte(temporary[2]);
            temporary[2] = aes_sub_byte(temporary[3]);
            temporary[3] = aes_sub_byte(first);
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
        state[i] = aes_sub_byte(state[i]);
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

static void aes_inv_shift_rows(uint8_t state[AES_BLOCK_BYTES]) {
    uint8_t temporary;
    temporary = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temporary;
}

static void aes_inv_sub_bytes(uint8_t state[AES_BLOCK_BYTES]) {
    for (size_t i = 0U; i < AES_BLOCK_BYTES; ++i)
        state[i] = aes_inv_sub_byte(state[i]);
}

static void aes_inv_mix_columns(uint8_t state[AES_BLOCK_BYTES]) {
    for (size_t column = 0U; column < 4U; ++column) {
        const size_t base = column * 4U;
        const uint8_t a = state[base];
        const uint8_t b = state[base + 1U];
        const uint8_t c = state[base + 2U];
        const uint8_t d = state[base + 3U];
        state[base] = (uint8_t)(
            gf256_multiply(a, 0x0EU) ^ gf256_multiply(b, 0x0BU) ^
            gf256_multiply(c, 0x0DU) ^ gf256_multiply(d, 0x09U));
        state[base + 1U] = (uint8_t)(
            gf256_multiply(a, 0x09U) ^ gf256_multiply(b, 0x0EU) ^
            gf256_multiply(c, 0x0BU) ^ gf256_multiply(d, 0x0DU));
        state[base + 2U] = (uint8_t)(
            gf256_multiply(a, 0x0DU) ^ gf256_multiply(b, 0x09U) ^
            gf256_multiply(c, 0x0EU) ^ gf256_multiply(d, 0x0BU));
        state[base + 3U] = (uint8_t)(
            gf256_multiply(a, 0x0BU) ^ gf256_multiply(b, 0x0DU) ^
            gf256_multiply(c, 0x09U) ^ gf256_multiply(d, 0x0EU));
    }
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

static void aes_decrypt_block(const uint8_t round_keys[AES_ROUND_KEY_BYTES],
                              const uint8_t input[AES_BLOCK_BYTES],
                              uint8_t output[AES_BLOCK_BYTES]) {
    uint8_t state[AES_BLOCK_BYTES];
    copy_bytes(state, input, sizeof(state));
    aes_add_round_key(state, round_keys + 10U * AES_BLOCK_BYTES);
    for (size_t round = 9U; round > 0U; --round) {
        aes_inv_shift_rows(state);
        aes_inv_sub_bytes(state);
        aes_add_round_key(state, round_keys + round * AES_BLOCK_BYTES);
        aes_inv_mix_columns(state);
    }
    aes_inv_shift_rows(state);
    aes_inv_sub_bytes(state);
    aes_add_round_key(state, round_keys);
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

bool nexus_aes_key_unwrap_128(
    const uint8_t kek[NEXUS_AES128_KEY_BYTES],
    const uint8_t *wrapped, size_t wrapped_length,
    uint8_t *plaintext, size_t plaintext_capacity,
    size_t *plaintext_length) {
    static const uint8_t default_iv[8] = {
        0xA6U,0xA6U,0xA6U,0xA6U,0xA6U,0xA6U,0xA6U,0xA6U
    };
    if (plaintext_length == NULL) return false;
    *plaintext_length = 0U;
    if (kek == NULL || wrapped == NULL ||
        wrapped_length < NEXUS_AES_KEY_WRAP_MIN_CIPHERTEXT_BYTES ||
        (wrapped_length % 8U) != 0U ||
        wrapped_length - NEXUS_AES_KEY_WRAP_OVERHEAD_BYTES >
            NEXUS_AES_KEY_WRAP_MAX_PLAINTEXT_BYTES)
        return false;

    const size_t output_bytes =
        wrapped_length - NEXUS_AES_KEY_WRAP_OVERHEAD_BYTES;
    if (plaintext == NULL || plaintext_capacity < output_bytes ||
        wrapped == plaintext || kek == plaintext ||
        ranges_partially_overlap(wrapped, wrapped_length,
                                 plaintext, output_bytes) ||
        ranges_partially_overlap(kek, NEXUS_AES128_KEY_BYTES,
                                 plaintext, output_bytes))
        return false;

    const size_t block_count = output_bytes / 8U;
    uint8_t a[8];
    copy_bytes(a, wrapped, sizeof(a));
    copy_bytes(plaintext, wrapped + 8U, output_bytes);

    uint8_t round_keys[AES_ROUND_KEY_BYTES];
    uint8_t block[AES_BLOCK_BYTES];
    uint8_t decrypted[AES_BLOCK_BYTES];
    aes_expand_key(kek, round_keys);

    for (int j = 5; j >= 0; --j) {
        for (size_t i = block_count; i > 0U; --i) {
            const uint64_t t = (uint64_t)block_count * (uint64_t)j +
                               (uint64_t)i;
            copy_bytes(block, a, sizeof(a));
            for (size_t byte = 0U; byte < 8U; ++byte)
                block[byte] ^= (uint8_t)(t >> (56U - 8U * byte));
            copy_bytes(block + 8U, plaintext + (i - 1U) * 8U, 8U);
            aes_decrypt_block(round_keys, block, decrypted);
            copy_bytes(a, decrypted, 8U);
            copy_bytes(plaintext + (i - 1U) * 8U, decrypted + 8U, 8U);
        }
    }

    uint8_t integrity_difference = 0U;
    for (size_t i = 0U; i < sizeof(default_iv); ++i)
        integrity_difference |= (uint8_t)(a[i] ^ default_iv[i]);

    secure_zero(a, sizeof(a));
    secure_zero(round_keys, sizeof(round_keys));
    secure_zero(block, sizeof(block));
    secure_zero(decrypted, sizeof(decrypted));
    if (integrity_difference != 0U) {
        secure_zero(plaintext, output_bytes);
        return false;
    }
    *plaintext_length = output_bytes;
    return true;
}

