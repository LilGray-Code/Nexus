#ifndef NEXUS_WPA2_EAPOL_H
#define NEXUS_WPA2_EAPOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WPA2_EAPOL_HEADER_BYTES 4U
#define NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES 95U
#define NEXUS_WPA2_EAPOL_MAX_FRAME_BYTES 4096U
#define NEXUS_WPA2_EAPOL_NONCE_BYTES 32U
#define NEXUS_WPA2_EAPOL_IV_BYTES 16U
#define NEXUS_WPA2_EAPOL_RSC_BYTES 8U
#define NEXUS_WPA2_EAPOL_MIC_BYTES 16U
#define NEXUS_WPA2_GTK_CCMP_BYTES 16U

#define NEXUS_WPA2_KEY_INFO_DESCRIPTOR_VERSION_MASK 0x0007U
#define NEXUS_WPA2_KEY_INFO_PAIRWISE 0x0008U
#define NEXUS_WPA2_KEY_INFO_INSTALL 0x0040U
#define NEXUS_WPA2_KEY_INFO_ACK 0x0080U
#define NEXUS_WPA2_KEY_INFO_MIC 0x0100U
#define NEXUS_WPA2_KEY_INFO_SECURE 0x0200U
#define NEXUS_WPA2_KEY_INFO_ERROR 0x0400U
#define NEXUS_WPA2_KEY_INFO_REQUEST 0x0800U
#define NEXUS_WPA2_KEY_INFO_ENCRYPTED_DATA 0x1000U

struct nexus_wpa2_eapol_key {
    uint8_t eapol_version;
    uint8_t descriptor_type;
    uint16_t key_info;
    uint16_t key_length;
    uint64_t replay_counter;
    uint8_t nonce[NEXUS_WPA2_EAPOL_NONCE_BYTES];
    uint8_t key_iv[NEXUS_WPA2_EAPOL_IV_BYTES];
    uint8_t key_rsc[NEXUS_WPA2_EAPOL_RSC_BYTES];
    uint8_t key_id[8];
    uint8_t mic[NEXUS_WPA2_EAPOL_MIC_BYTES];
    size_t mic_offset;
    const uint8_t *key_data;
    size_t key_data_length;
    bool pairwise;
    bool install;
    bool acknowledgement;
    bool mic_present;
    bool secure;
    bool error;
    bool request;
    bool encrypted_key_data;
};

/*
 * Parse a complete EAPOL-Key packet (packet type 3, RSN descriptor type 2).
 * The frame must be a single complete packet no larger than 4096 bytes. The
 * returned key_data pointer refers into the caller's original frame.
 *
 * This routine validates structure only; it does not authenticate the MIC,
 * enforce replay counters, or advance a four-way-handshake state machine.
 */
bool nexus_wpa2_eapol_parse_key(
    const uint8_t *frame, size_t frame_length,
    struct nexus_wpa2_eapol_key *parsed);

/*
 * Locate one RSN GTK KDE in already-decrypted EAPOL key data. The bounded
 * CCMP-128 profile accepts a single 16-byte GTK KDE. Duplicate GTK KDEs,
 * malformed information fields, and malformed/truncated IE streams fail.
 */
bool nexus_wpa2_eapol_extract_gtk_kde(
    const uint8_t *key_data, size_t key_data_length,
    uint8_t gtk[NEXUS_WPA2_GTK_CCMP_BYTES],
    uint8_t *gtk_info, size_t *gtk_length);

#endif
