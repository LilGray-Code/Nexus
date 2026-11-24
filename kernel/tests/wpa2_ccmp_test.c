#include "wpa2_ccmp.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void expect_bytes(const uint8_t *actual, const uint8_t *expected,
                         size_t length) {
    for (size_t i = 0U; i < length; ++i) assert(actual[i] == expected[i]);
}

static void fill_header(uint8_t header[26]) {
    for (size_t i = 0U; i < 26U; ++i) header[i] = 0U;
    header[0] = 0x88U; /* QoS Data, protocol version 0. */
    header[1] = 0x01U; /* To DS. */
    for (size_t i = 0U; i < 18U; ++i)
        header[4U + i] = (uint8_t)(0x10U + i);
    header[22] = 0xA5U; /* Sequence number plus fragment 5. */
    header[23] = 0xBCU;
    header[24] = 0x15U; /* TID 5 plus mutable QoS bits. */
    header[25] = 0xA0U;
}

static void test_ccmp_header_round_trip_and_invalid_fields(void) {
    uint8_t header[NEXUS_WPA2_CCMP_HEADER_BYTES];
    uint8_t key_id = 0U;
    uint64_t packet_number = 0U;
    const uint64_t expected_pn = UINT64_C(0x010203040506);

    assert(nexus_wpa2_ccmp_header_encode(expected_pn, 2U, header));
    static const uint8_t expected[8] = {
        0x06U,0x05U,0x00U,0xA0U,0x04U,0x03U,0x02U,0x01U
    };
    expect_bytes(header, expected, sizeof(expected));
    assert(nexus_wpa2_ccmp_header_decode(header, &key_id, &packet_number));
    assert(key_id == 2U && packet_number == expected_pn);

    header[2] = 1U;
    assert(!nexus_wpa2_ccmp_header_decode(header, &key_id, &packet_number));
    header[2] = 0U;
    header[3] = 0x80U; /* Missing ExtIV. */
    assert(!nexus_wpa2_ccmp_header_decode(header, &key_id, &packet_number));
    header[3] = 0x21U; /* Reserved low bit. */
    assert(!nexus_wpa2_ccmp_header_decode(header, &key_id, &packet_number));
    assert(!nexus_wpa2_ccmp_header_encode(0U, 0U, header));
    assert(!nexus_wpa2_ccmp_header_encode(1U, 4U, header));
    assert(!nexus_wpa2_ccmp_header_encode(
        NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER + 1U, 0U, header));
}

static void test_aad_masks_mutable_fields_and_builds_nonce(void) {
    uint8_t header[26];
    uint8_t changed[26];
    uint8_t nonce[13], nonce_changed[13];
    uint8_t aad[32], aad_changed[32];
    size_t aad_length = 0U, changed_length = 0U;
    uint8_t tid = 0U, changed_tid = 0U;
    const uint64_t pn = UINT64_C(0x010203040506);
    fill_header(header);

    assert(nexus_wpa2_ccmp_build_aad_nonce(
        header, sizeof(header), pn, nonce, aad, &aad_length, &tid));
    assert(aad_length == 24U && tid == 5U);
    assert(nonce[0] == 5U);
    expect_bytes(nonce + 1U, header + 10U, 6U);
    static const uint8_t pn_bytes[6] = {
        0x01U,0x02U,0x03U,0x04U,0x05U,0x06U
    };
    expect_bytes(nonce + 7U, pn_bytes, sizeof(pn_bytes));
    assert(aad[0] == 0x88U && aad[1] == 0x01U);
    assert(aad[20] == 5U && aad[21] == 0U);
    assert(aad[22] == 5U && aad[23] == 0U);

    for (size_t i = 0U; i < sizeof(header); ++i) changed[i] = header[i];
    changed[1] ^= 0x38U; /* Retry, power-management, and more-data. */
    changed[22] ^= 0xF0U; /* Sequence number bits, not fragment number. */
    changed[23] ^= 0xFFU;
    changed[25] ^= 0xFFU; /* QoS control bits other than TID. */
    assert(nexus_wpa2_ccmp_build_aad_nonce(
        changed, sizeof(changed), pn, nonce_changed,
        aad_changed, &changed_length, &changed_tid));
    assert(changed_length == aad_length && changed_tid == tid);
    expect_bytes(aad_changed, aad, aad_length);
    expect_bytes(nonce_changed, nonce, sizeof(nonce));

    changed[24] = 6U; /* Different TID changes AAD and nonce priority. */
    assert(nexus_wpa2_ccmp_build_aad_nonce(
        changed, sizeof(changed), pn, nonce_changed,
        aad_changed, &changed_length, &changed_tid));
    assert(changed_tid == 6U && nonce_changed[0] == 6U);
    assert(aad_changed[22] == 6U);
}

static void test_wds_and_non_qos_headers(void) {
    uint8_t header[32] = {0U};
    uint8_t nonce[13], aad[32];
    size_t aad_length = 0U;
    uint8_t tid = 0U;
    header[0] = 0x88U;
    header[1] = 0x03U; /* To DS + From DS: Addr4 is present. */
    for (size_t i = 0U; i < 24U; ++i) header[4U + i] = (uint8_t)i;
    header[30] = 0x0BU;
    assert(nexus_wpa2_ccmp_build_aad_nonce(
        header, sizeof(header), 7U, nonce, aad, &aad_length, &tid));
    assert(aad_length == 30U && tid == 11U);
    expect_bytes(aad + 22U, header + 24U, 6U);
    assert(aad[28] == 11U && aad[29] == 0U);

    header[0] = 0x08U; /* Non-QoS Data. */
    header[1] = 0U;
    assert(nexus_wpa2_ccmp_build_aad_nonce(
        header, 24U, 8U, nonce, aad, &aad_length, &tid));
    assert(aad_length == 22U && tid == NEXUS_WPA2_CCMP_NON_QOS_TID);
    assert(nonce[0] == 0U);

    header[1] = 0x83U; /* HT-Control on QoS data is not implemented. */
    header[0] = 0x88U;
    assert(!nexus_wpa2_ccmp_build_aad_nonce(
        header, sizeof(header), 8U, nonce, aad, &aad_length, &tid));
    assert(aad_length == 0U);
}

static void test_replay_window_and_tid_isolation(void) {
    struct nexus_wpa2_ccmp_replay_state state;
    nexus_wpa2_ccmp_replay_reset(&state);
    assert(nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 100U));
    assert(nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 102U));
    assert(nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 101U));
    assert(!nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 101U));
    assert(nexus_wpa2_ccmp_replay_accept_authenticated(&state, 4U, 101U));
    assert(nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 39U));
    assert(!nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 38U));
    assert(!nexus_wpa2_ccmp_replay_accept_authenticated(&state, 3U, 0U));
    assert(!nexus_wpa2_ccmp_replay_accept_authenticated(
        &state, 17U, 1U));
    assert(!nexus_wpa2_ccmp_replay_accept_authenticated(
        &state, 3U, NEXUS_WPA2_CCMP_MAX_PACKET_NUMBER + 1U));
    nexus_wpa2_ccmp_replay_reset(&state);
    assert(!state.initialized[3U]);
    assert(state.received_bitmap[3U] == 0U);
}

static void test_protect_unprotect_and_authentication_gating(void) {
    uint8_t key[16];
    uint8_t header[26];
    uint8_t plaintext[37];
    uint8_t protected_frame[128];
    uint8_t decrypted[128];
    size_t protected_length = 0U, decrypted_length = 0U;
    struct nexus_wpa2_ccmp_replay_state replay;
    const uint64_t pn = UINT64_C(0x010203040506);

    for (size_t i = 0U; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(plaintext); ++i)
        plaintext[i] = (uint8_t)(0x80U + i);
    fill_header(header);

    assert(nexus_wpa2_ccmp_protect_frame(
        key, 2U, pn, header, sizeof(header), plaintext, sizeof(plaintext),
        protected_frame, sizeof(protected_frame), &protected_length));
    assert(protected_length == sizeof(header) + 8U + sizeof(plaintext) + 8U);
    assert((protected_frame[1] & 0x40U) != 0U);
    uint8_t decoded_key = 0U;
    uint64_t decoded_pn = 0U;
    assert(nexus_wpa2_ccmp_header_decode(
        protected_frame + sizeof(header), &decoded_key, &decoded_pn));
    assert(decoded_key == 2U && decoded_pn == pn);

    nexus_wpa2_ccmp_replay_reset(&replay);
    assert(nexus_wpa2_ccmp_unprotect_frame(
        key, 2U, &replay, protected_frame, protected_length, sizeof(header),
        decrypted, sizeof(decrypted), &decrypted_length));
    assert(decrypted_length == sizeof(header) + sizeof(plaintext));
    expect_bytes(decrypted, protected_frame, sizeof(header));
    expect_bytes(decrypted + sizeof(header), plaintext, sizeof(plaintext));
    assert(replay.initialized[5U]);

    /* A valid duplicate is rejected and its plaintext is not released. */
    for (size_t i = 0U; i < sizeof(decrypted); ++i) decrypted[i] = 0xA5U;
    decrypted_length = 123U;
    assert(!nexus_wpa2_ccmp_unprotect_frame(
        key, 2U, &replay, protected_frame, protected_length, sizeof(header),
        decrypted, sizeof(decrypted), &decrypted_length));
    assert(decrypted_length == 0U);
    for (size_t i = 0U; i < sizeof(plaintext); ++i)
        assert(decrypted[sizeof(header) + i] == 0U);
}

static void test_bad_mic_does_not_advance_replay_state(void) {
    uint8_t key[16] = {0U};
    uint8_t header[24] = {0U};
    uint8_t plaintext[8] = {1U,2U,3U,4U,5U,6U,7U,8U};
    uint8_t protected_frame[64];
    uint8_t decrypted[64];
    size_t protected_length = 0U, decrypted_length = 0U;
    struct nexus_wpa2_ccmp_replay_state replay;

    header[0] = 0x08U;
    assert(nexus_wpa2_ccmp_protect_frame(
        key, 0U, 55U, header, sizeof(header), plaintext, sizeof(plaintext),
        protected_frame, sizeof(protected_frame), &protected_length));
    protected_frame[sizeof(header) + 8U + 2U] ^= 0x80U;
    nexus_wpa2_ccmp_replay_reset(&replay);
    assert(!nexus_wpa2_ccmp_unprotect_frame(
        key, 0U, &replay, protected_frame, protected_length, sizeof(header),
        decrypted, sizeof(decrypted), &decrypted_length));
    assert(decrypted_length == 0U);
    assert(!replay.initialized[NEXUS_WPA2_CCMP_NON_QOS_TID]);

    /* The same packet number remains usable once an authentic copy arrives. */
    assert(nexus_wpa2_ccmp_protect_frame(
        key, 0U, 55U, header, sizeof(header), plaintext, sizeof(plaintext),
        protected_frame, sizeof(protected_frame), &protected_length));
    assert(nexus_wpa2_ccmp_unprotect_frame(
        key, 0U, &replay, protected_frame, protected_length, sizeof(header),
        decrypted, sizeof(decrypted), &decrypted_length));
    expect_bytes(decrypted + sizeof(header), plaintext, sizeof(plaintext));
}

static void test_rejects_wrong_keyid_and_short_buffers(void) {
    uint8_t key[16] = {0U};
    uint8_t header[24] = {0U};
    uint8_t payload[4] = {1U,2U,3U,4U};
    uint8_t frame[64];
    uint8_t output[64];
    size_t length = 99U, output_length = 99U;
    struct nexus_wpa2_ccmp_replay_state replay;
    header[0] = 0x08U;
    assert(nexus_wpa2_ccmp_protect_frame(
        key, 1U, 1U, header, sizeof(header), payload, sizeof(payload),
        frame, sizeof(frame), &length));
    nexus_wpa2_ccmp_replay_reset(&replay);
    assert(!nexus_wpa2_ccmp_unprotect_frame(
        key, 0U, &replay, frame, length, sizeof(header),
        output, sizeof(output), &output_length));
    assert(output_length == 0U);
    assert(!nexus_wpa2_ccmp_unprotect_frame(
        key, 1U, &replay, frame, length, sizeof(header),
        output, sizeof(header), &output_length));
    assert(output_length == 0U);
    assert(!nexus_wpa2_ccmp_protect_frame(
        key, 1U, 0U, header, sizeof(header), payload, sizeof(payload),
        frame, sizeof(frame), &length));
    assert(length == 0U);
}

int main(void) {
    test_ccmp_header_round_trip_and_invalid_fields();
    test_aad_masks_mutable_fields_and_builds_nonce();
    test_wds_and_non_qos_headers();
    test_replay_window_and_tid_isolation();
    test_protect_unprotect_and_authentication_gating();
    test_bad_mic_does_not_advance_replay_state();
    test_rejects_wrong_keyid_and_short_buffers();
    return 0;
}
