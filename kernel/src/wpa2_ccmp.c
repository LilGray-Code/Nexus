#include "wpa2_ccmp.h"

#include "aes_ccm.h"

#include <stdint.h>

#define CCMP_NONCE_BYTES 13U
#define CCMP_TAG_BYTES 8U
#define CCMP_MAX_PAYLOAD NEXUS_AES_CCM_MAX_PAYLOAD_BYTES

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool inspect_mac_header(const uint8_t *header, size_t header_length,
                               bool *qos, bool *four_address,
                               uint8_t *tid) {
    if (header == NULL || qos == NULL || four_address == NULL || tid == NULL ||
        header_length < 24U) return false;

    const uint8_t frame_control0 = header[0];
    const uint8_t frame_control1 = header[1];
    if ((frame_control0 & 0x03U) != 0U ||
        (frame_control0 & 0x0CU) != 0x08U) return false;

    *qos = (frame_control0 & 0x80U) != 0U;
    *four_address = (frame_control1 & 0x03U) == 0x03U;
    if (*qos && (frame_control1 & 0x80U) != 0U) return false;

    const size_t expected_length =
        24U + (*four_address ? 6U : 0U) + (*qos ? 2U : 0U);
    if (header_length != expected_length) return false;

    if (*qos) {
        const size_t qos_offset = 24U + (*four_address ? 6U : 0U);
        *tid = (uint8_t)(header[qos_offset] & 0x0FU);
    } else {
        *tid = NEXUS_WPA2_CCMP_NON_QOS_TID;
    }
    return true;
}

bool nexus_wpa2_ccmp_build_aad_nonce(
    const uint8_t *mac_header, size_t mac_header_length,
    uint64_t packet_number,
    uint8_t nonce[CCMP_NONCE_BYTES],
    uint8_t aad[NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES],
    size_t *aad_length, uint8_t *tid) {
    if (aad_length != NULL) *aad_length = 0U;
    if (tid != NULL) *tid = 0U;
    if (nonce != NULL) secure_zero(nonce, CCMP_NONCE_BYTES);
    if (aad != NULL)
        secure_zero(aad, NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES);
    if (mac_header == NULL || nonce == NULL || aad == NULL ||
        aad_length == NULL || tid == NULL ||
        packet_number == 0U ||
        packet_number > NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER) return false;

    bool qos = false;
    bool four_address = false;
    uint8_t priority = 0U;
    if (!inspect_mac_header(mac_header, mac_header_length,
                            &qos, &four_address, &priority)) return false;

    /* CCMP nonce: Priority || transmitter address (A2) || PN5..PN0. */
    nonce[0] = qos ? priority : 0U;
    copy_bytes(nonce + 1U, mac_header + 10U, 6U);
    for (size_t i = 0U; i < 6U; ++i) {
        const unsigned shift = (unsigned)(40U - 8U * i);
        nonce[7U + i] = (uint8_t)(packet_number >> shift);
    }

    /* IEEE 802.11 CCMP AAD masks mutable FC and sequence-number fields. */
    size_t offset = 0U;
    aad[offset++] = (uint8_t)(mac_header[0] & 0x8FU);
    aad[offset++] = (uint8_t)(mac_header[1] & 0xC7U);
    copy_bytes(aad + offset, mac_header + 4U, 18U);
    offset += 18U;
    aad[offset++] = (uint8_t)(mac_header[22] & 0x0FU);
    aad[offset++] = 0U;

    if (four_address) {
        copy_bytes(aad + offset, mac_header + 24U, 6U);
        offset += 6U;
    }
    if (qos) {
        const size_t qos_offset = 24U + (four_address ? 6U : 0U);
        aad[offset++] = (uint8_t)(mac_header[qos_offset] & 0x0FU);
        aad[offset++] = 0U;
    }

    *aad_length = offset;
    *tid = qos ? priority : NEXUS_WPA2_CCMP_NON_QOS_TID;
    return true;
}

bool nexus_wpa2_ccmp_header_encode(uint64_t packet_number, uint8_t key_id,
                                   uint8_t header[NEXUS_WPA2_CCMP_HEADER_BYTES]) {
    if (header == NULL) return false;
    secure_zero(header, NEXUS_WPA2_CCMP_HEADER_BYTES);
    if (packet_number == 0U ||
        packet_number > NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER ||
        key_id > 3U) return false;

    header[0] = (uint8_t)packet_number;
    header[1] = (uint8_t)(packet_number >> 8);
    header[2] = 0U;
    header[3] = (uint8_t)((key_id << 6) | 0x20U);
    header[4] = (uint8_t)(packet_number >> 16);
    header[5] = (uint8_t)(packet_number >> 24);
    header[6] = (uint8_t)(packet_number >> 32);
    header[7] = (uint8_t)(packet_number >> 40);
    return true;
}

bool nexus_wpa2_ccmp_header_decode(
    const uint8_t header[NEXUS_WPA2_CCMP_HEADER_BYTES],
    uint8_t *key_id, uint64_t *packet_number) {
    if (key_id != NULL) *key_id = 0U;
    if (packet_number != NULL) *packet_number = 0U;
    if (header == NULL || key_id == NULL || packet_number == NULL ||
        header[2] != 0U || (header[3] & 0x20U) == 0U ||
        (header[3] & 0x1FU) != 0U) return false;

    const uint64_t number =
        (uint64_t)header[0] |
        ((uint64_t)header[1] << 8) |
        ((uint64_t)header[4] << 16) |
        ((uint64_t)header[5] << 24) |
        ((uint64_t)header[6] << 32) |
        ((uint64_t)header[7] << 40);
    if (number == 0U) return false;
    *key_id = (uint8_t)(header[3] >> 6);
    *packet_number = number;
    return true;
}

void nexus_wpa2_ccmp_replay_reset(
    struct nexus_wpa2_ccmp_replay_state *state) {
    if (state != NULL) secure_zero(state, sizeof(*state));
}

bool nexus_wpa2_ccmp_replay_accept_authenticated(
    struct nexus_wpa2_ccmp_replay_state *state,
    uint8_t tid, uint64_t packet_number) {
    if (state == NULL || tid >= NEXUS_WPA2_CCMP_TID_COUNT ||
        packet_number == 0U ||
        packet_number > NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER) return false;

    if (!state->initialized[tid]) {
        state->highest_packet_number[tid] = packet_number;
        state->received_bitmap[tid] = UINT64_C(1);
        state->initialized[tid] = true;
        return true;
    }

    const uint64_t highest = state->highest_packet_number[tid];
    if (packet_number > highest) {
        const uint64_t advance = packet_number - highest;
        state->received_bitmap[tid] =
            advance >= NEXUS_WPA2_CCMP_REPLAY_WINDOW_BITS
                ? UINT64_C(1)
                : (state->received_bitmap[tid] << (unsigned)advance) |
                    UINT64_C(1);
        state->highest_packet_number[tid] = packet_number;
        return true;
    }

    const uint64_t distance = highest - packet_number;
    if (distance >= NEXUS_WPA2_CCMP_REPLAY_WINDOW_BITS) return false;
    const uint64_t mask = UINT64_C(1) << (unsigned)distance;
    if ((state->received_bitmap[tid] & mask) != 0U) return false;
    state->received_bitmap[tid] |= mask;
    return true;
}

bool nexus_wpa2_ccmp_protect_frame(
    const uint8_t key[16], uint8_t key_id, uint64_t packet_number,
    const uint8_t *mac_header, size_t mac_header_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *output, size_t output_capacity, size_t *output_length) {
    if (output_length == NULL) return false;
    *output_length = 0U;
    if (key == NULL || mac_header == NULL || output == NULL ||
        (plaintext == NULL && plaintext_length != 0U) ||
        plaintext_length > CCMP_MAX_PAYLOAD ||
        mac_header_length > NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES ||
        mac_header_length < 24U) return false;

    const size_t total_length =
        mac_header_length + NEXUS_WPA2_CCMP_HEADER_BYTES +
        plaintext_length + CCMP_TAG_BYTES;
    if (output_capacity < total_length) return false;

    copy_bytes(output, mac_header, mac_header_length);
    output[1] = (uint8_t)(output[1] | 0x40U);

    uint8_t nonce[CCMP_NONCE_BYTES];
    uint8_t aad[NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES];
    size_t aad_length = 0U;
    uint8_t tid = 0U;
    if (!nexus_wpa2_ccmp_build_aad_nonce(
            output, mac_header_length, packet_number,
            nonce, aad, &aad_length, &tid) ||
        !nexus_wpa2_ccmp_header_encode(
            packet_number, key_id, output + mac_header_length)) {
        secure_zero(nonce, sizeof(nonce));
        secure_zero(aad, sizeof(aad));
        secure_zero(output, total_length);
        return false;
    }
    (void)tid;

    uint8_t *ciphertext =
        output + mac_header_length + NEXUS_WPA2_CCMP_HEADER_BYTES;
    uint8_t *tag = ciphertext + plaintext_length;
    const bool encrypted = nexus_aes_ccm_encrypt_128(
        key, nonce, aad, aad_length, plaintext, plaintext_length,
        ciphertext, tag);
    secure_zero(nonce, sizeof(nonce));
    secure_zero(aad, sizeof(aad));
    if (!encrypted) {
        secure_zero(output, total_length);
        return false;
    }

    *output_length = total_length;
    return true;
}

bool nexus_wpa2_ccmp_unprotect_frame(
    const uint8_t key[16], uint8_t expected_key_id,
    struct nexus_wpa2_ccmp_replay_state *replay_state,
    const uint8_t *protected_frame, size_t protected_frame_length,
    size_t mac_header_length,
    uint8_t *output, size_t output_capacity, size_t *output_length) {
    if (output_length == NULL) return false;
    *output_length = 0U;
    if (key == NULL || replay_state == NULL || protected_frame == NULL ||
        output == NULL || expected_key_id > 3U ||
        mac_header_length < 24U ||
        mac_header_length > NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES ||
        protected_frame_length < mac_header_length +
            NEXUS_WPA2_CCMP_HEADER_BYTES + CCMP_TAG_BYTES) return false;
    if ((protected_frame[1] & 0x40U) == 0U) return false;

    const size_t payload_length =
        protected_frame_length - mac_header_length -
        NEXUS_WPA2_CCMP_HEADER_BYTES - CCMP_TAG_BYTES;
    if (payload_length > CCMP_MAX_PAYLOAD ||
        output_capacity < mac_header_length + payload_length) return false;

    uint8_t key_id = 0U;
    uint64_t packet_number = 0U;
    if (!nexus_wpa2_ccmp_header_decode(
            protected_frame + mac_header_length, &key_id, &packet_number) ||
        key_id != expected_key_id) return false;

    uint8_t nonce[CCMP_NONCE_BYTES];
    uint8_t aad[NEXUS_WPA2_CCMP_MAX_MAC_HEADER_BYTES];
    size_t aad_length = 0U;
    uint8_t tid = 0U;
    if (!nexus_wpa2_ccmp_build_aad_nonce(
            protected_frame, mac_header_length, packet_number,
            nonce, aad, &aad_length, &tid)) {
        secure_zero(nonce, sizeof(nonce));
        secure_zero(aad, sizeof(aad));
        return false;
    }

    const uint8_t *ciphertext =
        protected_frame + mac_header_length + NEXUS_WPA2_CCMP_HEADER_BYTES;
    const uint8_t *tag = ciphertext + payload_length;
    uint8_t *plaintext = output + mac_header_length;
    const bool authenticated = nexus_aes_ccm_decrypt_128(
        key, nonce, aad, aad_length, ciphertext, payload_length,
        tag, plaintext);
    secure_zero(nonce, sizeof(nonce));
    secure_zero(aad, sizeof(aad));
    if (!authenticated) return false;

    if (!nexus_wpa2_ccmp_replay_accept_authenticated(
            replay_state, tid, packet_number)) {
        secure_zero(plaintext, payload_length);
        return false;
    }

    copy_bytes(output, protected_frame, mac_header_length);
    *output_length = mac_header_length + payload_length;
    return true;
}
