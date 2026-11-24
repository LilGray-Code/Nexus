#include "wpa2_handshake.h"

#include "aes_ccm.h"
#include "wpa2_eapol.h"
#include "random.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct test_radio {
    uint8_t sent[2][NEXUS_WPA2_HANDSHAKE_EAPOL_RESPONSE_MAX];
    size_t sent_length[2];
    size_t send_count;
    size_t install_count;
    size_t clear_count;
    uint8_t installed_ptk[NEXUS_WPA2_TK_CCMP_BYTES];
    uint8_t installed_gtk[NEXUS_WPA2_GTK_CCMP_BYTES];
    uint8_t installed_gtk_info;
    uint8_t installed_rsc[8];
    bool fail_send;
    bool fail_install;
};

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool send_eapol(void *context, const uint8_t *frame, size_t length) {
    struct test_radio *radio = (struct test_radio *)context;
    if (radio == NULL || frame == NULL ||
        radio->send_count >= 2U || length > sizeof(radio->sent[0]) ||
        radio->fail_send)
        return false;
    copy_bytes(radio->sent[radio->send_count], frame, length);
    radio->sent_length[radio->send_count] = length;
    ++radio->send_count;
    return true;
}

static bool install_keys(void *context,
                         const uint8_t pairwise_tk[16],
                         const uint8_t group_tk[16],
                         uint8_t gtk_info, const uint8_t rsc[8]) {
    struct test_radio *radio = (struct test_radio *)context;
    if (radio == NULL || pairwise_tk == NULL || group_tk == NULL ||
        rsc == NULL || radio->fail_install)
        return false;
    copy_bytes(radio->installed_ptk, pairwise_tk, 16U);
    copy_bytes(radio->installed_gtk, group_tk, 16U);
    radio->installed_gtk_info = gtk_info;
    copy_bytes(radio->installed_rsc, rsc, 8U);
    ++radio->install_count;
    return true;
}

static void clear_keys(void *context) {
    struct test_radio *radio = (struct test_radio *)context;
    if (radio == NULL) return;
    ++radio->clear_count;
    for (size_t i = 0U; i < sizeof(radio->installed_ptk); ++i)
        radio->installed_ptk[i] = 0U;
    for (size_t i = 0U; i < sizeof(radio->installed_gtk); ++i)
        radio->installed_gtk[i] = 0U;
}

static void write_be16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void write_be64(uint8_t *out, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i)
        out[i] = (uint8_t)(value >> (56U - 8U * i));
}

static size_t make_message_one(uint8_t frame[99], uint8_t version,
                               uint64_t replay, const uint8_t anonce[32]) {
    for (size_t i = 0U; i < 99U; ++i) frame[i] = 0U;
    frame[0] = version;
    frame[1] = 3U;
    write_be16(frame + 2U, 95U);
    frame[4] = 2U;
    write_be16(frame + 5U, 0x008AU); /* Descriptor v2 | Pairwise | ACK. */
    write_be16(frame + 7U, 16U);
    write_be64(frame + 9U, replay);
    copy_bytes(frame + 17U, anonce, 32U);
    write_be16(frame + 97U, 0U);
    return 99U;
}

static void expect_bytes(const uint8_t *actual, const uint8_t *expected,
                         size_t length) {
    for (size_t i = 0U; i < length; ++i) assert(actual[i] == expected[i]);
}

static void set_test_constants(uint8_t pmk[32], uint8_t ap[6],
                               uint8_t sta[6], uint8_t anonce[32],
                               uint8_t snonce[32]) {
    for (size_t i = 0U; i < 32U; ++i) {
        pmk[i] = (uint8_t)i;
        anonce[i] = (uint8_t)(0x20U + i);
        snonce[i] = (uint8_t)(0x40U + i);
    }
    const uint8_t ap_value[6] = {0x00U,0x11U,0x22U,0x33U,0x44U,0x55U};
    const uint8_t sta_value[6] = {0x02U,0xAAU,0xBBU,0xCCU,0xDDU,0xEEU};
    copy_bytes(ap, ap_value, 6U);
    copy_bytes(sta, sta_value, 6U);
}

static void test_four_way_handshake_m1_to_m4(void) {
    static const uint8_t message_three[131] = {
        0x02U,0x03U,0x00U,0x7FU,0x02U,0x13U,0xCAU,0x00U,0x10U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x06U,0x20U,0x21U,0x22U,0x23U,0x24U,
        0x25U,0x26U,0x27U,0x28U,0x29U,0x2AU,0x2BU,0x2CU,0x2DU,0x2EU,0x2FU,
        0x30U,0x31U,0x32U,0x33U,0x34U,0x35U,0x36U,0x37U,0x38U,0x39U,0x3AU,
        0x3BU,0x3CU,0x3DU,0x3EU,0x3FU,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x62U,0x34U,0xA6U,0xD6U,0xCCU,0x16U,0x7DU,
        0x37U,0x7DU,0xD9U,0xA3U,0x42U,0x2AU,0x92U,0x9CU,0x82U,0x00U,0x20U,
        0xBAU,0x5FU,0x41U,0x7BU,0xC0U,0xD6U,0x10U,0x49U,0x80U,0xA6U,0x0BU,
        0xEDU,0xDFU,0xC5U,0xD2U,0xC3U,0xFAU,0xBAU,0x84U,0x10U,0xE2U,0x10U,
        0x8BU,0x8DU,0x12U,0x03U,0x0DU,0x15U,0x47U,0xFFU,0x21U,0xADU
    };
    static const uint8_t expected_ptk[64] = {
        0x4DU,0x52U,0xE6U,0xA1U,0x2FU,0x68U,0xAFU,0xFAU,
        0xADU,0x9FU,0x62U,0xBCU,0x24U,0x90U,0x1DU,0x29U,
        0xCEU,0xD2U,0x55U,0x18U,0xE3U,0x19U,0xE5U,0x46U,
        0x39U,0x02U,0x57U,0x1AU,0x40U,0xBBU,0x95U,0xB3U,
        0x2CU,0xCEU,0xBEU,0xB2U,0xBEU,0x33U,0xD6U,0x4AU,
        0x1CU,0x60U,0x21U,0x3CU,0x21U,0x62U,0x8EU,0x11U,
        0x48U,0x8AU,0x7EU,0x5CU,0x54U,0x0EU,0x35U,0xC9U,
        0x11U,0x90U,0x2EU,0x2AU,0xFEU,0xF0U,0xF0U,0xB2U
    };
    static const uint8_t expected_gtk[16] = {
        0xA0U,0xA1U,0xA2U,0xA3U,0xA4U,0xA5U,0xA6U,0xA7U,
        0xA8U,0xA9U,0xAAU,0xABU,0xACU,0xADU,0xAEU,0xAFU
    };

    uint8_t pmk[32], ap[6], sta[6], anonce[32], snonce[32];
    set_test_constants(pmk, ap, sta, anonce, snonce);

    struct test_radio radio = {0};
    const struct nexus_wpa2_handshake_ops ops = {
        .send_eapol = send_eapol,
        .install_ccmp128_keys = install_keys,
        .clear_ccmp128_keys = clear_keys
    };
    struct nexus_wpa2_supplicant supplicant;
    assert(nexus_wpa2_supplicant_init(
        &supplicant, pmk, sta, ap, snonce, &ops, &radio));
    assert(supplicant.state == NEXUS_WPA2_SUPPLICANT_WAIT_M1);

    uint8_t message_one[99];
    const size_t m1_length = make_message_one(message_one, 2U, 5U, anonce);
    assert(nexus_wpa2_supplicant_handle_eapol(
        &supplicant, message_one, m1_length));
    assert(supplicant.state == NEXUS_WPA2_SUPPLICANT_WAIT_M3);
    assert(radio.send_count == 1U);
    expect_bytes(supplicant.ptk, expected_ptk, sizeof(expected_ptk));

    struct nexus_wpa2_eapol_key m2;
    assert(nexus_wpa2_eapol_parse_key(
        radio.sent[0], radio.sent_length[0], &m2));
    assert(m2.key_info == 0x010AU);
    assert(m2.replay_counter == 5U);
    expect_bytes(m2.nonce, snonce, sizeof(snonce));
    assert(nexus_wpa2_eapol_mic_verify(
        expected_ptk, radio.sent[0], radio.sent_length[0], m2.mic_offset));

    assert(nexus_wpa2_supplicant_handle_eapol(
        &supplicant, message_three, sizeof(message_three)));
    assert(supplicant.state == NEXUS_WPA2_SUPPLICANT_COMPLETE);
    assert(radio.install_count == 1U);
    assert(radio.send_count == 2U);
    expect_bytes(radio.installed_ptk, expected_ptk + 32U, 16U);
    expect_bytes(radio.installed_gtk, expected_gtk, 16U);
    assert(radio.installed_gtk_info == 0x02U);
    const uint8_t expected_rsc[8] = {0U,0U,0U,0U,0U,0U,0U,0U};
    expect_bytes(radio.installed_rsc, expected_rsc, sizeof(expected_rsc));

    struct nexus_wpa2_eapol_key m4;
    assert(nexus_wpa2_eapol_parse_key(
        radio.sent[1], radio.sent_length[1], &m4));
    assert(m4.key_info == 0x030AU);
    assert(m4.replay_counter == 6U);
    assert(nexus_wpa2_eapol_mic_verify(
        expected_ptk, radio.sent[1], radio.sent_length[1], m4.mic_offset));
    assert(!nexus_wpa2_supplicant_handle_eapol(
        &supplicant, message_three, sizeof(message_three)));

    nexus_wpa2_supplicant_deinit(&supplicant);
    assert(radio.clear_count == 1U);
}

static void test_handshake_rejects_bad_mic_and_replay(void) {
    static const uint8_t message_three[131] = {
        0x02U,0x03U,0x00U,0x7FU,0x02U,0x13U,0xCAU,0x00U,0x10U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x06U,0x20U,0x21U,0x22U,0x23U,0x24U,
        0x25U,0x26U,0x27U,0x28U,0x29U,0x2AU,0x2BU,0x2CU,0x2DU,0x2EU,0x2FU,
        0x30U,0x31U,0x32U,0x33U,0x34U,0x35U,0x36U,0x37U,0x38U,0x39U,0x3AU,
        0x3BU,0x3CU,0x3DU,0x3EU,0x3FU,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x00U,0x00U,0x00U,0x00U,0x62U,0x34U,0xA6U,0xD6U,0xCCU,0x16U,0x7DU,
        0x37U,0x7DU,0xD9U,0xA3U,0x42U,0x2AU,0x92U,0x9CU,0x82U,0x00U,0x20U,
        0xBAU,0x5FU,0x41U,0x7BU,0xC0U,0xD6U,0x10U,0x49U,0x80U,0xA6U,0x0BU,
        0xEDU,0xDFU,0xC5U,0xD2U,0xC3U,0xFAU,0xBAU,0x84U,0x10U,0xE2U,0x10U,
        0x8BU,0x8DU,0x12U,0x03U,0x0DU,0x15U,0x47U,0xFFU,0x21U,0xADU
    };
    uint8_t pmk[32], ap[6], sta[6], anonce[32], snonce[32];
    set_test_constants(pmk, ap, sta, anonce, snonce);
    struct test_radio radio = {0};
    const struct nexus_wpa2_handshake_ops ops = {
        .send_eapol = send_eapol,
        .install_ccmp128_keys = install_keys,
        .clear_ccmp128_keys = clear_keys
    };
    struct nexus_wpa2_supplicant supplicant;
    uint8_t m1[99];
    assert(nexus_wpa2_supplicant_init(&supplicant, pmk, sta, ap,
                                       snonce, &ops, &radio));
    const size_t m1_length = make_message_one(m1, 2U, 5U, anonce);
    assert(nexus_wpa2_supplicant_handle_eapol(&supplicant, m1, m1_length));

    uint8_t corrupt[sizeof(message_three)];
    copy_bytes(corrupt, message_three, sizeof(corrupt));
    corrupt[81U] ^= 0x01U;
    assert(!nexus_wpa2_supplicant_handle_eapol(
        &supplicant, corrupt, sizeof(corrupt)));
    assert(radio.install_count == 0U);
    assert(supplicant.state == NEXUS_WPA2_SUPPLICANT_WAIT_M3);

    copy_bytes(corrupt, message_three, sizeof(corrupt));
    write_be64(corrupt + 9U, 5U); /* Replay M3 with M1 counter. */
    assert(!nexus_wpa2_supplicant_handle_eapol(
        &supplicant, corrupt, sizeof(corrupt)));
    assert(radio.install_count == 0U);
    nexus_wpa2_supplicant_deinit(&supplicant);
}

static void test_init_requires_valid_identity_and_nonce(void) {
    uint8_t pmk[32] = {1U};
    uint8_t sta[6] = {0x02U,0U,0U,0U,0U,1U};
    uint8_t ap[6] = {0x02U,0U,0U,0U,0U,2U};
    uint8_t nonce[32] = {1U};
    struct test_radio radio = {0};
    const struct nexus_wpa2_handshake_ops ops = {
        .send_eapol = send_eapol,
        .install_ccmp128_keys = install_keys,
        .clear_ccmp128_keys = clear_keys
    };
    struct nexus_wpa2_supplicant supplicant;
    assert(!nexus_wpa2_supplicant_init(
        &supplicant, pmk, sta, ap, NULL, &ops, &radio));
    uint8_t multicast[6] = {0x01U,0U,0U,0U,0U,1U};
    assert(!nexus_wpa2_supplicant_init(
        &supplicant, pmk, multicast, ap, nonce, &ops, &radio));
    assert(!nexus_wpa2_supplicant_init(
        &supplicant, pmk, sta, sta, nonce, &ops, &radio));
    uint8_t zero_nonce[32] = {0U};
    assert(!nexus_wpa2_supplicant_init(
        &supplicant, pmk, sta, ap, zero_nonce, &ops, &radio));
    assert(nexus_wpa2_supplicant_init(
        &supplicant, pmk, sta, ap, nonce, &ops, &radio));
    nexus_wpa2_supplicant_deinit(&supplicant);
}

static void test_random_nonce_initializer_fails_closed(void) {
    uint8_t pmk[32] = {1U};
    uint8_t sta[6] = {0x02U,0U,0U,0U,0U,1U};
    uint8_t ap[6] = {0x02U,0U,0U,0U,0U,2U};
    struct test_radio radio = {0};
    const struct nexus_wpa2_handshake_ops ops = {
        .send_eapol = send_eapol,
        .install_ccmp128_keys = install_keys,
        .clear_ccmp128_keys = clear_keys
    };
    struct nexus_wpa2_supplicant supplicant;
    const bool initialized = nexus_wpa2_supplicant_init_random(
        &supplicant, pmk, sta, ap, &ops, &radio);
    if (initialized) {
        assert(supplicant.state == NEXUS_WPA2_SUPPLICANT_WAIT_M1);
        assert(!bytes_are_zero(supplicant.supplicant_nonce,
                               sizeof(supplicant.supplicant_nonce)));
        nexus_wpa2_supplicant_deinit(&supplicant);
    } else {
        assert(!nexus_rng_available() || supplicant.state == 0);
        for (size_t i = 0U; i < sizeof(supplicant.pmk); ++i)
            assert(supplicant.pmk[i] == 0U);
    }
}

int main(void) {
    test_four_way_handshake_m1_to_m4();
    test_handshake_rejects_bad_mic_and_replay();
    test_init_requires_valid_identity_and_nonce();
    test_random_nonce_initializer_fails_closed();
    return 0;
}
