#ifndef NEXUS_AES_CCM_H
#define NEXUS_AES_CCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_AES128_KEY_BYTES 16U

/* Raw FIPS-197 AES-128 block encryption primitive for security protocols. */
bool nexus_aes128_encrypt_block(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t plaintext[16], uint8_t ciphertext[16]);

#define NEXUS_AES_CMAC_MAX_MESSAGE_BYTES 1024U
/* RFC 4493 AES-CMAC. A NULL message is valid only when length is zero. */
bool nexus_aes128_cmac(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t *message, size_t message_length,
    uint8_t mac[16]);
#define NEXUS_AES_CCM_NONCE_BYTES 13U
#define NEXUS_AES_CCM_TAG_BYTES 8U
#define NEXUS_AES_CCM_MAX_AAD_BYTES 1024U
#define NEXUS_AES_CCM_MAX_PAYLOAD_BYTES 4096U
#define NEXUS_AES_KEY_WRAP_OVERHEAD_BYTES 8U
#define NEXUS_AES_KEY_WRAP_MIN_CIPHERTEXT_BYTES 24U
#define NEXUS_AES_KEY_WRAP_MAX_PLAINTEXT_BYTES 4096U

/*
 * Bounded AES-128-CCM profile used by WPA2-CCMP:
 *   key: 16 bytes
 *   nonce: 13 bytes (L=2)
 *   tag: 8 bytes (M=8)
 *   AAD: at most 1024 bytes
 *   payload: at most 4096 bytes
 *
 * AAD and nonce construction from 802.11 headers/packet numbers, replay
 * protection, key lifetime, and packet sequencing remain the WLAN driver's
 * responsibility. Encryption/decryption support exact in-place operation;
 * partially overlapping input/output buffers are rejected.
 */
bool nexus_aes_ccm_encrypt_128(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext,
    uint8_t tag[NEXUS_AES_CCM_TAG_BYTES]);

/*
 * Decrypt and authenticate. On an authentication failure the plaintext
 * output is zeroed before returning false.
 */
bool nexus_aes_ccm_decrypt_128(
    const uint8_t key[NEXUS_AES128_KEY_BYTES],
    const uint8_t nonce[NEXUS_AES_CCM_NONCE_BYTES],
    const uint8_t *aad, size_t aad_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    const uint8_t tag[NEXUS_AES_CCM_TAG_BYTES],
    uint8_t *plaintext);

/*
 * RFC 3394 AES-128 Key Unwrap (KW). Input is a wrapped key of at least
 * 24 bytes and a multiple of 8; plaintext capacity is bounded to 4096 bytes.
 * On integrity failure, the output buffer is zeroed and *plaintext_length=0.
 * The input and output buffers must not overlap.
 */
bool nexus_aes_key_unwrap_128(
    const uint8_t kek[NEXUS_AES128_KEY_BYTES],
    const uint8_t *wrapped, size_t wrapped_length,
    uint8_t *plaintext, size_t plaintext_capacity,
    size_t *plaintext_length);

#endif
