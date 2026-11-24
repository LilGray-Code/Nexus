#include "wpa2_eapol.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void write_be16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void write_be64(uint8_t *out, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i)
        out[i] = (uint8_t)(value >> (56U - i * 8U));
}

static size_t build_eapol_key(uint8_t *frame, size_t capacity,
                              uint16_t key_info,
                              const uint8_t *key_data,
                              size_t key_data_length) {
    const size_t total =
        NEXUS_WPA2_EAPOL_HEADER_BYTES +
        NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES + key_data_length;
    assert(frame != NULL && total <= capacity && total <= UINT16_MAX + 4U);
    for (size_t i = 0U; i < total; ++i) frame[i] = 0U;
    frame[0] = 2U; /* EAPOL version 2. */
    frame[1] = 3U; /* EAPOL-Key. */
    write_be16(frame + 2U, (uint16_t)(total - 4U));
    frame[4] = 2U; /* RSN key descriptor. */
    write_be16(frame + 5U, key_info);
    write_be16(frame + 7U, 16U);
    write_be64(frame + 9U, 0x0102030405060708ULL);
    for (size_t i = 0U; i < NEXUS_WPA2_EAPOL_NONCE_BYTES; ++i)
        frame[17U + i] = (uint8_t)(0x20U + i);
    write_be16(frame + 97U, (uint16_t)key_data_length);
    for (size_t i = 0U; i < key_data_length; ++i)
        frame[99U + i] = key_data[i];
    return total;
}

static void test_parse_eapol_key_frame(void) {
    uint8_t key_data[4] = {0x30U,0x02U,0x01U,0x00U};
    uint8_t frame[160];
    const uint16_t info = 0x13CAU; /* Pairwise, install, ACK, MIC, secure, encrypted. */
    const size_t length =
        build_eapol_key(frame, sizeof(frame), info, key_data, sizeof(key_data));
    struct nexus_wpa2_eapol_key parsed;
    assert(nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    assert(parsed.eapol_version == 2U);
    assert(parsed.descriptor_type == 2U);
    assert(parsed.key_info == info);
    assert(parsed.key_length == 16U);
    assert(parsed.replay_counter == 0x0102030405060708ULL);
    assert(parsed.pairwise && parsed.install && parsed.acknowledgement);
    assert(parsed.mic_present && parsed.secure && parsed.encrypted_key_data);
    assert(!parsed.error && !parsed.request);
    assert(parsed.mic_offset == 81U);
    assert(parsed.key_data_length == sizeof(key_data));
    for (size_t i = 0U; i < sizeof(key_data); ++i)
        assert(parsed.key_data[i] == key_data[i]);
}

static void test_parse_rejects_bad_headers_and_lengths(void) {
    uint8_t frame[128];
    struct nexus_wpa2_eapol_key parsed;
    const size_t length = build_eapol_key(frame, sizeof(frame), 0x0102U,
                                          NULL, 0U);
    uint8_t saved = frame[1];
    frame[1] = 1U;
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    frame[1] = saved;

    saved = frame[4];
    frame[4] = 254U;
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    frame[4] = saved;

    frame[0] = 4U;
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    frame[0] = 2U;

    frame[3] ^= 1U;
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    frame[3] ^= 1U;

    write_be16(frame + 97U, 1U);
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    write_be16(frame + 97U, 0U);

    write_be16(frame + 5U, 0U); /* Invalid descriptor version. */
    assert(!nexus_wpa2_eapol_parse_key(frame, length, &parsed));
    assert(parsed.key_data == NULL && parsed.key_data_length == 0U);

    assert(!nexus_wpa2_eapol_parse_key(frame, 98U, &parsed));
    assert(!nexus_wpa2_eapol_parse_key(frame,
         NEXUS_WPA2_EAPOL_MAX_FRAME_BYTES + 1U, &parsed));
    assert(!nexus_wpa2_eapol_parse_key(NULL, 100U, &parsed));
    assert(!nexus_wpa2_eapol_parse_key(frame, length, NULL));
}

static void test_extract_single_ccmp_gtk_kde(void) {
    uint8_t key_data[2U + 3U + 2U + NEXUS_WPA2_GTK_CCMP_BYTES];
    size_t cursor = 0U;
    key_data[cursor++] = 0xDDU;
    key_data[cursor++] = (uint8_t)(sizeof(key_data) - 2U);
    key_data[cursor++] = 0x00U;
    key_data[cursor++] = 0x0FU;
    key_data[cursor++] = 0xACU;
    key_data[cursor++] = 0x01U;
    key_data[cursor++] = 0x02U; /* GTK key ID 2, Tx bit clear. */
    key_data[cursor++] = 0x00U;
    for (size_t i = 0U; i < NEXUS_WPA2_GTK_CCMP_BYTES; ++i)
        key_data[cursor++] = (uint8_t)(0xA0U + i);

    uint8_t gtk[NEXUS_WPA2_GTK_CCMP_BYTES];
    uint8_t info = 0U;
    size_t gtk_length = 0U;
    assert(nexus_wpa2_eapol_extract_gtk_kde(
        key_data, sizeof(key_data), gtk, &info, &gtk_length));
    assert(gtk_length == NEXUS_WPA2_GTK_CCMP_BYTES);
    assert(info == 0x02U);
    for (size_t i = 0U; i < gtk_length; ++i)
        assert(gtk[i] == (uint8_t)(0xA0U + i));
}

static void test_extract_gtk_kde_rejects_duplicate_and_malformed(void) {
    uint8_t kde[2U + 22U];
    kde[0] = 0xDDU;
    kde[1] = 22U;
    kde[2] = 0x00U; kde[3] = 0x0FU; kde[4] = 0xACU; kde[5] = 0x01U;
    kde[6] = 0x01U; kde[7] = 0x00U;
    for (size_t i = 0U; i < NEXUS_WPA2_GTK_CCMP_BYTES; ++i)
        kde[8U + i] = (uint8_t)i;

    uint8_t gtk[NEXUS_WPA2_GTK_CCMP_BYTES];
    uint8_t info;
    size_t gtk_length;
    uint8_t duplicate[sizeof(kde) * 2U];
    for (size_t i = 0U; i < sizeof(kde); ++i) {
        duplicate[i] = kde[i];
        duplicate[sizeof(kde) + i] = kde[i];
    }
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        duplicate, sizeof(duplicate), gtk, &info, &gtk_length));
    assert(gtk_length == 0U);

    kde[6] = 0x81U; /* Reserved GTK-info bits set. */
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        kde, sizeof(kde), gtk, &info, &gtk_length));
    kde[6] = 0x01U;
    kde[7] = 1U; /* Reserved octet must be zero. */
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        kde, sizeof(kde), gtk, &info, &gtk_length));

    kde[7] = 0U;
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        kde, sizeof(kde) - 1U, gtk, &info, &gtk_length));
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        kde, sizeof(kde), gtk, NULL, &gtk_length));
    assert(!nexus_wpa2_eapol_extract_gtk_kde(
        kde, sizeof(kde), gtk, &info, NULL));
}

int main(void) {
    test_parse_eapol_key_frame();
    test_parse_rejects_bad_headers_and_lengths();
    test_extract_single_ccmp_gtk_kde();
    test_extract_gtk_kde_rejects_duplicate_and_malformed();
    return 0;
}
