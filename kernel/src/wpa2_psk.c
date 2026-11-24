#include "wpa2_psk.h"

#include <stdint.h>

#define SHA1_BLOCK_BYTES 64U
#define SHA1_DIGEST_BYTES 20U
#define PBKDF2_ROUNDS 4096U

struct sha1_context {
    uint32_t state[5];
    uint64_t total_bytes;
    uint8_t block[SHA1_BLOCK_BYTES];
    size_t block_length;
};

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static uint32_t rotate_left(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha1_transform(struct sha1_context *context,
                           const uint8_t block[SHA1_BLOCK_BYTES]) {
    uint32_t words[80];
    for (size_t i = 0U; i < 16U; ++i)
        words[i] = read_be32(block + i * 4U);
    for (size_t i = 16U; i < 80U; ++i) {
        words[i] = rotate_left(words[i - 3U] ^ words[i - 8U] ^
                               words[i - 14U] ^ words[i - 16U], 1U);
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];

    for (size_t i = 0U; i < 80U; ++i) {
        uint32_t function;
        uint32_t constant;
        if (i < 20U) {
            function = (b & c) | ((~b) & d);
            constant = 0x5A827999U;
        } else if (i < 40U) {
            function = b ^ c ^ d;
            constant = 0x6ED9EBA1U;
        } else if (i < 60U) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDCU;
        } else {
            function = b ^ c ^ d;
            constant = 0xCA62C1D6U;
        }
        const uint32_t temporary = rotate_left(a, 5U) + function + e +
                                   constant + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30U);
        b = a;
        a = temporary;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    secure_zero(words, sizeof(words));
}

static void sha1_init(struct sha1_context *context) {
    context->state[0] = 0x67452301U;
    context->state[1] = 0xEFCDAB89U;
    context->state[2] = 0x98BADCFEU;
    context->state[3] = 0x10325476U;
    context->state[4] = 0xC3D2E1F0U;
    context->total_bytes = 0U;
    context->block_length = 0U;
    for (size_t i = 0U; i < sizeof(context->block); ++i)
        context->block[i] = 0U;
}

static void sha1_update(struct sha1_context *context,
                        const uint8_t *data, size_t length) {
    if (length == 0U) return;
    context->total_bytes += (uint64_t)length;
    while (length != 0U) {
        size_t amount = SHA1_BLOCK_BYTES - context->block_length;
        if (amount > length) amount = length;
        for (size_t i = 0U; i < amount; ++i)
            context->block[context->block_length + i] = data[i];
        context->block_length += amount;
        data += amount;
        length -= amount;
        if (context->block_length == SHA1_BLOCK_BYTES) {
            sha1_transform(context, context->block);
            context->block_length = 0U;
        }
    }
}

static void sha1_final(struct sha1_context *context,
                       uint8_t digest[SHA1_DIGEST_BYTES]) {
    const uint64_t bit_length = context->total_bytes * 8U;
    uint8_t padding[SHA1_BLOCK_BYTES] = {0x80U};
    const size_t padding_length = context->block_length < 56U
        ? 56U - context->block_length
        : 120U - context->block_length;
    sha1_update(context, padding, padding_length);

    uint8_t encoded_length[8];
    for (size_t i = 0U; i < 8U; ++i)
        encoded_length[i] = (uint8_t)(bit_length >> (56U - 8U * i));
    sha1_update(context, encoded_length, sizeof(encoded_length));

    for (size_t i = 0U; i < 5U; ++i)
        write_be32(digest + i * 4U, context->state[i]);
    secure_zero(encoded_length, sizeof(encoded_length));
    secure_zero(padding, sizeof(padding));
    secure_zero(context, sizeof(*context));
}

static void sha1_digest(const uint8_t *data, size_t length,
                        uint8_t digest[SHA1_DIGEST_BYTES]) {
    struct sha1_context context;
    sha1_init(&context);
    sha1_update(&context, data, length);
    sha1_final(&context, digest);
}

static void hmac_sha1_parts(
    const uint8_t *key, size_t key_length,
    const uint8_t *part1, size_t part1_length,
    const uint8_t *part2, size_t part2_length,
    const uint8_t *part3, size_t part3_length,
    uint8_t digest[SHA1_DIGEST_BYTES]) {
    uint8_t key_block[SHA1_BLOCK_BYTES] = {0U};
    uint8_t inner_pad[SHA1_BLOCK_BYTES];
    uint8_t outer_pad[SHA1_BLOCK_BYTES];
    uint8_t inner_digest[SHA1_DIGEST_BYTES];

    if (key_length > SHA1_BLOCK_BYTES) {
        sha1_digest(key, key_length, inner_digest);
        for (size_t i = 0U; i < SHA1_DIGEST_BYTES; ++i)
            key_block[i] = inner_digest[i];
    } else {
        for (size_t i = 0U; i < key_length; ++i)
            key_block[i] = key[i];
    }

    for (size_t i = 0U; i < SHA1_BLOCK_BYTES; ++i) {
        inner_pad[i] = (uint8_t)(key_block[i] ^ 0x36U);
        outer_pad[i] = (uint8_t)(key_block[i] ^ 0x5CU);
    }

    struct sha1_context context;
    sha1_init(&context);
    sha1_update(&context, inner_pad, sizeof(inner_pad));
    sha1_update(&context, part1, part1_length);
    sha1_update(&context, part2, part2_length);
    sha1_update(&context, part3, part3_length);
    sha1_final(&context, inner_digest);

    sha1_init(&context);
    sha1_update(&context, outer_pad, sizeof(outer_pad));
    sha1_update(&context, inner_digest, sizeof(inner_digest));
    sha1_final(&context, digest);

    secure_zero(key_block, sizeof(key_block));
    secure_zero(inner_pad, sizeof(inner_pad));
    secure_zero(outer_pad, sizeof(outer_pad));
    secure_zero(inner_digest, sizeof(inner_digest));
}

static void hmac_sha1(const uint8_t *key, size_t key_length,
                      const uint8_t *data, size_t data_length,
                      uint8_t digest[SHA1_DIGEST_BYTES]) {
    hmac_sha1_parts(key, key_length, data, data_length,
                    NULL, 0U, NULL, 0U, digest);
}

static int compare_bytes(const uint8_t *a, const uint8_t *b,
                         size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i)
        destination[i] = source[i];
}

bool nexus_wpa2_derive_ptk(
    const uint8_t pmk[NEXUS_WPA2_PMK_BYTES],
    const uint8_t authenticator_mac[6],
    const uint8_t supplicant_mac[6],
    const uint8_t authenticator_nonce[32],
    const uint8_t supplicant_nonce[32],
    uint8_t ptk[NEXUS_WPA2_PTK_BYTES]) {
    static const uint8_t label[] = "Pairwise key expansion";
    if (ptk == NULL) return false;
    secure_zero(ptk, NEXUS_WPA2_PTK_BYTES);
    if (pmk == NULL || authenticator_mac == NULL ||
        supplicant_mac == NULL || authenticator_nonce == NULL ||
        supplicant_nonce == NULL) return false;

    uint8_t context[12U + 64U];
    uint8_t input[sizeof(label) + sizeof(context) + 1U];
    uint8_t digest[SHA1_DIGEST_BYTES];
    size_t offset = 0U;

    const uint8_t *mac_first = authenticator_mac;
    const uint8_t *mac_second = supplicant_mac;
    if (compare_bytes(mac_first, mac_second, 6U) > 0) {
        mac_first = supplicant_mac;
        mac_second = authenticator_mac;
    }
    copy_bytes(context, mac_first, 6U);
    copy_bytes(context + 6U, mac_second, 6U);

    const uint8_t *nonce_first = authenticator_nonce;
    const uint8_t *nonce_second = supplicant_nonce;
    if (compare_bytes(nonce_first, nonce_second, 32U) > 0) {
        nonce_first = supplicant_nonce;
        nonce_second = authenticator_nonce;
    }
    copy_bytes(context + 12U, nonce_first, 32U);
    copy_bytes(context + 44U, nonce_second, 32U);

    /* PRF input: label || NUL || context || one-byte block counter. */
    for (size_t i = 0U; i < sizeof(label); ++i)
        input[offset++] = label[i];
    copy_bytes(input + offset, context, sizeof(context));
    offset += sizeof(context);

    size_t output_offset = 0U;
    for (uint8_t counter = 0U;
         output_offset < NEXUS_WPA2_PTK_BYTES; ++counter) {
        input[offset] = counter;
        hmac_sha1(pmk, NEXUS_WPA2_PMK_BYTES, input, offset + 1U, digest);
        size_t amount = NEXUS_WPA2_PTK_BYTES - output_offset;
        if (amount > SHA1_DIGEST_BYTES) amount = SHA1_DIGEST_BYTES;
        copy_bytes(ptk + output_offset, digest, amount);
        output_offset += amount;
    }

    secure_zero(context, sizeof(context));
    secure_zero(input, sizeof(input));
    secure_zero(digest, sizeof(digest));
    return true;
}

bool nexus_wpa2_eapol_mic(
    const uint8_t kck[NEXUS_WPA2_KCK_BYTES],
    const uint8_t *eapol_frame, size_t frame_length, size_t mic_offset,
    uint8_t mic[NEXUS_WPA2_EAPOL_MIC_BYTES]) {
    if (mic == NULL) return false;
    secure_zero(mic, NEXUS_WPA2_EAPOL_MIC_BYTES);
    if (kck == NULL || eapol_frame == NULL ||
        frame_length == 0U || frame_length > NEXUS_WPA2_MAX_EAPOL_BYTES ||
        mic_offset > frame_length ||
        frame_length - mic_offset < NEXUS_WPA2_EAPOL_MIC_BYTES)
        return false;

    static const uint8_t zero_mic[NEXUS_WPA2_EAPOL_MIC_BYTES] = {0U};
    uint8_t digest[SHA1_DIGEST_BYTES];
    const size_t suffix_offset = mic_offset + NEXUS_WPA2_EAPOL_MIC_BYTES;
    hmac_sha1_parts(kck, NEXUS_WPA2_KCK_BYTES,
                    eapol_frame, mic_offset,
                    zero_mic, sizeof(zero_mic),
                    eapol_frame + suffix_offset,
                    frame_length - suffix_offset, digest);
    copy_bytes(mic, digest, NEXUS_WPA2_EAPOL_MIC_BYTES);
    secure_zero(digest, sizeof(digest));
    return true;
}

bool nexus_wpa2_eapol_mic_verify(
    const uint8_t kck[NEXUS_WPA2_KCK_BYTES],
    const uint8_t *eapol_frame, size_t frame_length, size_t mic_offset) {
    if (kck == NULL || eapol_frame == NULL ||
        frame_length == 0U || frame_length > NEXUS_WPA2_MAX_EAPOL_BYTES ||
        mic_offset > frame_length ||
        frame_length - mic_offset < NEXUS_WPA2_EAPOL_MIC_BYTES)
        return false;

    uint8_t expected[NEXUS_WPA2_EAPOL_MIC_BYTES];
    if (!nexus_wpa2_eapol_mic(kck, eapol_frame, frame_length,
                               mic_offset, expected)) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < NEXUS_WPA2_EAPOL_MIC_BYTES; ++i)
        difference |= (uint8_t)(expected[i] ^ eapol_frame[mic_offset + i]);
    secure_zero(expected, sizeof(expected));
    return difference == 0U;
}

bool nexus_wpa2_psk_derive_pmk(const uint8_t *passphrase,
                               size_t passphrase_length,
                               const uint8_t *ssid, size_t ssid_length,
                               uint8_t pmk[NEXUS_WPA2_PMK_BYTES]) {
    if (pmk == NULL) return false;
    secure_zero(pmk, NEXUS_WPA2_PMK_BYTES);
    if (passphrase == NULL || ssid == NULL ||
        passphrase_length < NEXUS_WPA2_MIN_PASSPHRASE_BYTES ||
        passphrase_length > NEXUS_WPA2_MAX_PASSPHRASE_BYTES ||
        ssid_length == 0U || ssid_length > NEXUS_WPA2_MAX_SSID_BYTES)
        return false;

    uint8_t salt_block[NEXUS_WPA2_MAX_SSID_BYTES + 4U];
    uint8_t current[SHA1_DIGEST_BYTES];
    uint8_t accumulator[SHA1_DIGEST_BYTES];
    size_t output_offset = 0U;

    for (uint32_t block_index = 1U; block_index <= 2U; ++block_index) {
        for (size_t i = 0U; i < ssid_length; ++i)
            salt_block[i] = ssid[i];
        salt_block[ssid_length] = (uint8_t)(block_index >> 24);
        salt_block[ssid_length + 1U] = (uint8_t)(block_index >> 16);
        salt_block[ssid_length + 2U] = (uint8_t)(block_index >> 8);
        salt_block[ssid_length + 3U] = (uint8_t)block_index;

        hmac_sha1(passphrase, passphrase_length, salt_block,
                  ssid_length + 4U, current);
        for (size_t i = 0U; i < SHA1_DIGEST_BYTES; ++i)
            accumulator[i] = current[i];

        for (uint32_t iteration = 1U; iteration < PBKDF2_ROUNDS;
             ++iteration) {
            hmac_sha1(passphrase, passphrase_length, current,
                      sizeof(current), current);
            for (size_t i = 0U; i < SHA1_DIGEST_BYTES; ++i)
                accumulator[i] ^= current[i];
        }

        size_t amount = NEXUS_WPA2_PMK_BYTES - output_offset;
        if (amount > SHA1_DIGEST_BYTES) amount = SHA1_DIGEST_BYTES;
        for (size_t i = 0U; i < amount; ++i)
            pmk[output_offset + i] = accumulator[i];
        output_offset += amount;
    }

    secure_zero(salt_block, sizeof(salt_block));
    secure_zero(current, sizeof(current));
    secure_zero(accumulator, sizeof(accumulator));
    return true;
}
