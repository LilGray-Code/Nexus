#include "wpa2_eapol.h"

#include <stdint.h>

static uint16_t read_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint64_t read_be64(const uint8_t *data) {
    uint64_t value = 0U;
    for (size_t i = 0U; i < 8U; ++i)
        value = (value << 8) | data[i];
    return value;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0U; i < length; ++i) out[i] = in[i];
}

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static bool memory_ranges_overlap(const void *a, size_t a_length,
                                  const void *b, size_t b_length) {
    if (a_length == 0U || b_length == 0U) return false;
    const uintptr_t start_a = (uintptr_t)a;
    const uintptr_t start_b = (uintptr_t)b;
    if (start_a > UINTPTR_MAX - a_length ||
        start_b > UINTPTR_MAX - b_length)
        return true;
    return start_a < start_b + b_length &&
           start_b < start_a + a_length;
}

bool nexus_wpa2_eapol_parse_key(
    const uint8_t *frame, size_t frame_length,
    struct nexus_wpa2_eapol_key *parsed) {
    if (parsed == NULL) return false;
    if (frame != NULL &&
        memory_ranges_overlap(frame, frame_length, parsed, sizeof(*parsed)))
        return false;
    *parsed = (struct nexus_wpa2_eapol_key){0};
    if (frame == NULL ||
        frame_length < NEXUS_WPA2_EAPOL_HEADER_BYTES +
                       NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES ||
        frame_length > NEXUS_WPA2_EAPOL_MAX_FRAME_BYTES)
        return false;

    const uint8_t version = frame[0];
    if (version < 1U || version > 3U || frame[1] != 3U)
        return false;
    const size_t body_length = read_be16(frame + 2U);
    if (body_length != frame_length - NEXUS_WPA2_EAPOL_HEADER_BYTES ||
        body_length < NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES)
        return false;

    const size_t descriptor = NEXUS_WPA2_EAPOL_HEADER_BYTES;
    if (frame[descriptor] != 2U) return false; /* RSN key descriptor. */
    const size_t key_info_offset = descriptor + 1U;
    const size_t key_length_offset = key_info_offset + 2U;
    const size_t replay_offset = key_length_offset + 2U;
    const size_t nonce_offset = replay_offset + 8U;
    const size_t iv_offset = nonce_offset + NEXUS_WPA2_EAPOL_NONCE_BYTES;
    const size_t rsc_offset = iv_offset + NEXUS_WPA2_EAPOL_IV_BYTES;
    const size_t key_id_offset = rsc_offset + NEXUS_WPA2_EAPOL_RSC_BYTES;
    const size_t mic_offset = key_id_offset + 8U;
    const size_t key_data_length_offset =
        mic_offset + NEXUS_WPA2_EAPOL_MIC_BYTES;
    const size_t key_data_offset = key_data_length_offset + 2U;
    if (key_data_offset !=
        NEXUS_WPA2_EAPOL_HEADER_BYTES +
        NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES)
        return false;

    const size_t key_data_length = read_be16(frame + key_data_length_offset);
    if (key_data_length != frame_length - key_data_offset)
        return false;

    const uint16_t key_info = read_be16(frame + key_info_offset);
    const uint16_t descriptor_version =
        (uint16_t)(key_info & NEXUS_WPA2_KEY_INFO_DESCRIPTOR_VERSION_MASK);
    if (descriptor_version == 0U || descriptor_version > 3U)
        return false;

    parsed->eapol_version = version;
    parsed->descriptor_type = frame[descriptor];
    parsed->key_info = key_info;
    parsed->key_length = read_be16(frame + key_length_offset);
    parsed->replay_counter = read_be64(frame + replay_offset);
    copy_bytes(parsed->nonce, frame + nonce_offset, sizeof(parsed->nonce));
    copy_bytes(parsed->key_iv, frame + iv_offset, sizeof(parsed->key_iv));
    copy_bytes(parsed->key_rsc, frame + rsc_offset, sizeof(parsed->key_rsc));
    copy_bytes(parsed->key_id, frame + key_id_offset, sizeof(parsed->key_id));
    copy_bytes(parsed->mic, frame + mic_offset, sizeof(parsed->mic));
    parsed->mic_offset = mic_offset;
    parsed->key_data = frame + key_data_offset;
    parsed->key_data_length = key_data_length;
    parsed->pairwise = (key_info & NEXUS_WPA2_KEY_INFO_PAIRWISE) != 0U;
    parsed->install = (key_info & NEXUS_WPA2_KEY_INFO_INSTALL) != 0U;
    parsed->acknowledgement =
        (key_info & NEXUS_WPA2_KEY_INFO_ACK) != 0U;
    parsed->mic_present = (key_info & NEXUS_WPA2_KEY_INFO_MIC) != 0U;
    parsed->secure = (key_info & NEXUS_WPA2_KEY_INFO_SECURE) != 0U;
    parsed->error = (key_info & NEXUS_WPA2_KEY_INFO_ERROR) != 0U;
    parsed->request = (key_info & NEXUS_WPA2_KEY_INFO_REQUEST) != 0U;
    parsed->encrypted_key_data =
        (key_info & NEXUS_WPA2_KEY_INFO_ENCRYPTED_DATA) != 0U;
    return true;
}

bool nexus_wpa2_eapol_extract_gtk_kde(
    const uint8_t *key_data, size_t key_data_length,
    uint8_t gtk[NEXUS_WPA2_GTK_CCMP_BYTES],
    uint8_t *gtk_info, size_t *gtk_length) {
    static const uint8_t rsn_gtk_kde_prefix[4] = {
        0x00U, 0x0FU, 0xACU, 0x01U
    };
    if (gtk_length == NULL) return false;
    if ((gtk != NULL && gtk_info != NULL &&
         memory_ranges_overlap(gtk, NEXUS_WPA2_GTK_CCMP_BYTES,
                               gtk_info, sizeof(*gtk_info))) ||
        (gtk != NULL && memory_ranges_overlap(gtk, NEXUS_WPA2_GTK_CCMP_BYTES,
                                               gtk_length, sizeof(*gtk_length))) ||
        (gtk_info != NULL && memory_ranges_overlap(gtk_info, sizeof(*gtk_info),
                                                   gtk_length, sizeof(*gtk_length))) ||
        (key_data != NULL && key_data_length != 0U &&
         ((gtk != NULL && memory_ranges_overlap(key_data, key_data_length,
                                                gtk, NEXUS_WPA2_GTK_CCMP_BYTES)) ||
          (gtk_info != NULL && memory_ranges_overlap(key_data, key_data_length,
                                                     gtk_info, sizeof(*gtk_info))) ||
          memory_ranges_overlap(key_data, key_data_length,
                                gtk_length, sizeof(*gtk_length)))))
        return false;

    *gtk_length = 0U;
    if (gtk_info != NULL) *gtk_info = 0U;
    if (gtk != NULL) secure_zero(gtk, NEXUS_WPA2_GTK_CCMP_BYTES);
    if (key_data == NULL || gtk == NULL || gtk_info == NULL ||
        key_data_length == 0U ||
        key_data_length > NEXUS_WPA2_EAPOL_MAX_FRAME_BYTES)
        return false;

    size_t cursor = 0U;
    bool found = false;
    uint8_t candidate[NEXUS_WPA2_GTK_CCMP_BYTES] = {0U};
    uint8_t candidate_info = 0U;
    while (cursor < key_data_length) {
        if (key_data_length - cursor < 2U) goto fail;
        const uint8_t element_id = key_data[cursor];
        const size_t element_length = key_data[cursor + 1U];
        cursor += 2U;
        if (element_length > key_data_length - cursor) goto fail;

        if (element_id == 0xDDU &&
            element_length >= sizeof(rsn_gtk_kde_prefix) &&
            key_data[cursor] == rsn_gtk_kde_prefix[0] &&
            key_data[cursor + 1U] == rsn_gtk_kde_prefix[1] &&
            key_data[cursor + 2U] == rsn_gtk_kde_prefix[2] &&
            key_data[cursor + 3U] == rsn_gtk_kde_prefix[3]) {
            if (found || element_length !=
                         sizeof(rsn_gtk_kde_prefix) + 2U +
                         NEXUS_WPA2_GTK_CCMP_BYTES)
                goto fail;
            candidate_info = key_data[cursor + 4U];
            const uint8_t reserved = key_data[cursor + 5U];
            if ((candidate_info & 0xF8U) != 0U || reserved != 0U)
                goto fail;
            copy_bytes(candidate, key_data + cursor + 6U, sizeof(candidate));
            found = true;
        }
        cursor += element_length;
    }

    if (!found) goto fail;
    copy_bytes(gtk, candidate, sizeof(candidate));
    *gtk_info = candidate_info;
    *gtk_length = sizeof(candidate);
    secure_zero(candidate, sizeof(candidate));
    return true;

fail:
    secure_zero(candidate, sizeof(candidate));
    secure_zero(gtk, NEXUS_WPA2_GTK_CCMP_BYTES);
    *gtk_info = 0U;
    *gtk_length = 0U;
    return false;
}
