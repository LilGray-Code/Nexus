#ifndef NEXUS_WPA2_CCMP_H
#define NEXUS_WPA2_CCMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WPA2_CCMP_HEADER_BYTES 8U
#define NEXUS_WPA2_CCMP_TID_COUNT 17U
#define NEXUS_WPA2_CCMP_NON_QOS_TID 16U
#define NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER UINT64_C(0xFFFFFFFFFFFF)
#define NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES 32U
#define NEXUS_WPA2_CCMP_REPLAY_WINDOW_BITS 64U

/*
 * Per-peer, per-key CCMP replay state. Keep a separate instance for each
 * transmitter/key; serialize access from the WLAN driver's receive path.
 */
struct nexus_wpa2_ccmp_replay_state {
    uint64_t highest_packet_number[NEXUS_WPA2_CCMP_TID_COUNT];
    uint64_t received_bitmap[NEXUS_WPA2_CCMP_TID_COUNT];
    bool initialized[NEXUS_WPA2_CCMP_TID_COUNT];
};

/*
 * Construct the CCMP AAD and 13-byte nonce from an 802.11 data MAC header.
 * Supported headers are 24/26 bytes, with optional Addr4 (30/32 bytes).
 * QoS TID is returned as 0..15; non-QoS data uses replay slot 16.
 * HT-Control-bearing QoS headers are rejected until explicitly supported.
 */
bool nexus_wpa2_ccmp_build_aad_nonce(
    const uint8_t *mac_header, size_t mac_header_length,
    uint64_t packet_number,
    uint8_t nonce[13], uint8_t aad[NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES],
    size_t *aad_length, uint8_t *tid);

/* Encode/decode the 8-byte CCMP ExtIV and 48-bit packet number. */
bool nexus_wpa2_ccmp_header_encode(uint64_t packet_number, uint8_t key_id,
                                   uint8_t header[NEXUS_WPA2_CCMP_HEADER_BYTES]);
bool nexus_wpa2_ccmp_header_decode(
    const uint8_t header[NEXUS_WPA2_CCMP_HEADER_BYTES],
    uint8_t *key_id, uint64_t *packet_number);

void nexus_wpa2_ccmp_replay_reset(
    struct nexus_wpa2_ccmp_replay_state *state);

/*
 * Record a packet number only after its CCMP authentication tag has passed.
 * A 64-packet sliding replay window per TID permits normal frame reordering.
 */
bool nexus_wpa2_ccmp_replay_accept_authenticated(
    struct nexus_wpa2_ccmp_replay_state *state,
    uint8_t tid, uint64_t packet_number);

/*
 * Build/protect an 802.11 data MPDU:
 *   MAC header || CCMP header || encrypted payload || 8-byte MIC
 * The Protected bit is set in the output MAC header. The output buffer must
 * not overlap the MAC-header or plaintext input buffers. FCS is not added.
 */
bool nexus_wpa2_ccmp_protect_frame(
    const uint8_t key[16], uint8_t key_id, uint64_t packet_number,
    const uint8_t *mac_header, size_t mac_header_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *output, size_t output_capacity, size_t *output_length);

/*
 * Authenticate/decrypt one protected MPDU. Input and output buffers must be
 * disjoint; output is MAC header || plaintext payload (CCMP header/MIC removed).
 * The caller supplies the replay state for the transmitting peer and key.
 * Replay state changes only after successful MIC verification.
 */
bool nexus_wpa2_ccmp_unprotect_frame(
    const uint8_t key[16], uint8_t expected_key_id,
    struct nexus_wpa2_ccmp_replay_state *replay_state,
    const uint8_t *protected_frame, size_t protected_frame_length,
    size_t mac_header_length,
    uint8_t *output, size_t output_capacity, size_t *output_length);

#endif
