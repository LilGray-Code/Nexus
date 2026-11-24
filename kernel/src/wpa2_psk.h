#ifndef NEXUS_WPA2_PSK_H
#define NEXUS_WPA2_PSK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WPA2_PMK_BYTES 32U
#define NEXUS_WPA2_MIN_PASSPHRASE_BYTES 8U
#define NEXUS_WPA2_MAX_PASSPHRASE_BYTES 63U
#define NEXUS_WPA2_MAX_SSID_BYTES 32U

/*
 * Derive the 256-bit Pairwise Master Key from an ASCII WPA2-Personal
 * passphrase and the network SSID using PBKDF2-HMAC-SHA1 with 4096 rounds.
 *
 * This is only the passphrase-to-PMK step. It does not implement the EAPOL
 * four-way handshake, MIC verification, PTK/GTK derivation, CCMP, scanning,
 * association, or a hardware WLAN driver.
 */
bool nexus_wpa2_psk_derive_pmk(const uint8_t *passphrase,
                               size_t passphrase_length,
                               const uint8_t *ssid, size_t ssid_length,
                               uint8_t pmk[NEXUS_WPA2_PMK_BYTES]);

#endif
