#ifndef NEXUS_AES_CCM_H
#define NEXUS_AES_CCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_AES128_KEY_BYTES 16U
#define NEXUS_AES_CCM_NONCE_BYTES 13U
#define NEXUS_AES_CCM_TAG_BYTES 8U
#define NEXUS_AES_CCM_MAX_AAD_BYTES 1024U
#define NEXUS_AES_CCM_MAX_PAYLOAD_BYTES 4096U

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

#endif
