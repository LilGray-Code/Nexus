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

static void hmac_sha1(const uint8_t *key, size_t key_length,
                      const uint8_t *data, size_t data_length,
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
    sha1_update(&context, data, data_length);
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
