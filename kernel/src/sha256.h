#ifndef NEXUS_SHA256_H
#define NEXUS_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_SHA256_SIZE 32U

struct nexus_sha256_context {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_used;
};

void nexus_sha256_init(struct nexus_sha256_context *context);
void nexus_sha256_update(struct nexus_sha256_context *context,
                         const void *data, size_t length);
void nexus_sha256_final(struct nexus_sha256_context *context,
                        uint8_t digest[NEXUS_SHA256_SIZE]);
void nexus_sha256(const void *data, size_t length,
                  uint8_t digest[NEXUS_SHA256_SIZE]);
void nexus_hmac_sha256(const void *key, size_t key_length,
                       const void *data, size_t data_length,
                       uint8_t digest[NEXUS_SHA256_SIZE]);
bool nexus_hkdf_sha256(const void *salt, size_t salt_length,
                       const void *ikm, size_t ikm_length,
                       const void *info, size_t info_length,
                       void *output, size_t output_length);

#endif
