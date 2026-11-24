#ifndef NEXUS_WPA2_PSK_H
#define NEXUS_WPA2_PSK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WPA2_PMK_BYTES 32U
#define NEXUS_WPA2_PTK_BYTES 64U
#define NEXUS_WPA2_KCK_BYTES 16U
#define NEXUS_WPA2_EAPOL_MIC_BYTES 16U
#define NEXUS_WPA2_MAX_EAPOL_BYTES 4096U
#define NEXUS_WPA2_MIN_PASSPHRASE_BYTES 8U
#define NEXUS_WPA2_MAX_PASSPHRASE_BYTES 63U
#define NEXUS_WPA2_MAX_SSID_BYTES 32U

/*
 * Derive the 256-bit Pairwise Master Key from an ASCII WPA2-Personal
 * passphrase and the network SSID using PBKDF2-HMAC-SHA1 with 4096 rounds.
 */
bool nexus_wpa2_psk_derive_pmk(const uint8_t *passphrase,
                               size_t passphrase_length,
                               const uint8_t *ssid, size_t ssid_length,
                               uint8_t pmk[NEXUS_WPA2_PMK_BYTES]);

/*
 * Derive the 64-byte WPA2 Pairwise Transient Key using the IEEE 802.11
 * Pairwise key expansion PRF. MAC addresses and nonces are sorted
 * lexicographically as required by the protocol.
 */
bool nexus_wpa2_derive_ptk(
    const uint8_t pmk[NEXUS_WPA2_PMK_BYTES],
    const uint8_t authenticator_mac[6],
    const uint8_t supplicant_mac[6],
    const uint8_t authenticator_nonce[32],
    const uint8_t supplicant_nonce[32],
    uint8_t ptk[NEXUS_WPA2_PTK_BYTES]);

/*
 * Compute a WPA2 EAPOL-Key MIC using the first 16 bytes of the PTK (KCK).
 * The caller identifies the 16-byte MIC field in the EAPOL frame; the
 * routine logically replaces that field with zero bytes during HMAC-SHA1
 * and returns the first 16 digest bytes.
 */
bool nexus_wpa2_eapol_mic(
    const uint8_t kck[NEXUS_WPA2_KCK_BYTES],
    const uint8_t *eapol_frame, size_t frame_length, size_t mic_offset,
    uint8_t mic[NEXUS_WPA2_EAPOL_MIC_BYTES]);

/* Verify the MIC stored in the frame using a constant-time comparison. */
bool nexus_wpa2_eapol_mic_verify(
    const uint8_t kck[NEXUS_WPA2_KCK_BYTES],
    const uint8_t *eapol_frame, size_t frame_length, size_t mic_offset);

#endif
