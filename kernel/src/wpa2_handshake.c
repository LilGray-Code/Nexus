#include "wpa2_handshake.h"

#include "aes_ccm.h"
#include "wpa2_eapol.h"

#include <stdint.h>

#define EAPOL_RESPONSE_FRAME_BYTES \
    (NEXUS_WPA2_EAPOL_HEADER_BYTES + NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES)
#define EAPOL_DESCRIPTOR_VERSION 2U
#define EAPOL_KEY_INFO_M1 \
    (EAPOL_DESCRIPTOR_VERSION | NEXUS_WPA2_KEY_INFO_PAIRWISE | \
     NEXUS_WPA2_KEY_INFO_ACK)
#define EAPOL_KEY_INFO_M2 \
    (EAPOL_DESCRIPTOR_VERSION | NEXUS_WPA2_KEY_INFO_PAIRWISE | \
     NEXUS_WPA2_KEY_INFO_MIC)
#define EAPOL_KEY_INFO_M4 \
    (EAPOL_DESCRIPTOR_VERSION | NEXUS_WPA2_KEY_INFO_PAIRWISE | \
     NEXUS_WPA2_KEY_INFO_MIC | NEXUS_WPA2_KEY_INFO_SECURE)
#define EAPOL_KEY_INFO_M3_REQUIRED \
    (EAPOL_DESCRIPTOR_VERSION | NEXUS_WPA2_KEY_INFO_PAIRWISE | \
     NEXUS_WPA2_KEY_INFO_INSTALL | NEXUS_WPA2_KEY_INFO_ACK | \
     NEXUS_WPA2_KEY_INFO_MIC | NEXUS_WPA2_KEY_INFO_SECURE | \
     NEXUS_WPA2_KEY_INFO_ENCRYPTED_DATA)

static void secure_zero(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    for (size_t i = 0U; i < length; ++i) bytes[i] = 0U;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool bytes_equal_constant_time(const uint8_t *a, const uint8_t *b,
                                      size_t length) {
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(a[i] ^ b[i]);
    return difference == 0U;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t length) {
    uint8_t aggregate = 0U;
    for (size_t i = 0U; i < length; ++i) aggregate |= bytes[i];
    return aggregate == 0U;
}

static bool valid_unicast_mac(const uint8_t mac[6]) {
    return mac != NULL && (mac[0] & 1U) == 0U &&
           !bytes_are_zero(mac, 6U);
}

static void write_be16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void write_be64(uint8_t *destination, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i)
        destination[i] = (uint8_t)(value >> (56U - 8U * i));
}

static bool build_key_response(
    uint8_t eapol_version, uint16_t key_info, uint64_t replay_counter,
    const uint8_t nonce[32], const uint8_t kck[NEXUS_WPA2_KCK_BYTES],
    uint8_t frame[EAPOL_RESPONSE_FRAME_BYTES]) {
    for (size_t i = 0U; i < EAPOL_RESPONSE_FRAME_BYTES; ++i) frame[i] = 0U;
    frame[0] = eapol_version;
    frame[1] = 3U; /* EAPOL-Key packet. */
    write_be16(frame + 2U, NEXUS_WPA2_EAPOL_KEY_DESCRIPTOR_BYTES);
    frame[4] = 2U; /* RSN key descriptor. */
    write_be16(frame + 5U, key_info);
    write_be16(frame + 7U, NEXUS_WPA2_TK_CCMP_BYTES);
    write_be64(frame + 9U, replay_counter);
    if (nonce != NULL) copy_bytes(frame + 17U, nonce, 32U);
    write_be16(frame + 97U, 0U);
    return nexus_wpa2_eapol_mic(
        kck, frame, EAPOL_RESPONSE_FRAME_BYTES, 81U, frame + 81U);
}

static bool is_message_one(const struct nexus_wpa2_eapol_key *key) {
    return key->key_info == EAPOL_KEY_INFO_M1 &&
           key->pairwise && key->acknowledgement &&
           !key->install && !key->mic_present && !key->secure &&
           !key->error && !key->request && !key->encrypted_key_data &&
           key->key_length == NEXUS_WPA2_TK_CCMP_BYTES &&
           key->key_data_length == 0U &&
           !bytes_are_zero(key->nonce, sizeof(key->nonce));
}

static bool is_message_three(const struct nexus_wpa2_eapol_key *key) {
    return key->key_info == EAPOL_KEY_INFO_M3_REQUIRED &&
           key->pairwise && key->install && key->acknowledgement &&
           key->mic_present && key->secure && !key->error && !key->request &&
           key->encrypted_key_data &&
           key->key_length == NEXUS_WPA2_TK_CCMP_BYTES &&
           key->key_data_length >= NEXUS_AES_KEY_WRAP_MIN_CIPHERTEXT_BYTES &&
           (key->key_data_length % 8U) == 0U;
}

static bool process_message_one(
    struct nexus_wpa2_supplicant *supplicant,
    const struct nexus_wpa2_eapol_key *key) {
    if (supplicant->state != NEXUS_WPA2_SUPPLICANT_WAIT_M1 ||
        !is_message_one(key) ||
        bytes_equal_constant_time(key->nonce, supplicant->supplicant_nonce, 32U))
        return false;

    uint8_t candidate_ptk[NEXUS_WPA2_PTK_BYTES];
    uint8_t response[EAPOL_RESPONSE_FRAME_BYTES];
    if (!nexus_wpa2_derive_ptk(
            supplicant->pmk, supplicant->authenticator_mac,
            supplicant->supplicant_mac, key->nonce,
            supplicant->supplicant_nonce, candidate_ptk)) {
        secure_zero(candidate_ptk, sizeof(candidate_ptk));
        return false;
    }
    if (!build_key_response(key->eapol_version, EAPOL_KEY_INFO_M2,
                            key->replay_counter, supplicant->supplicant_nonce,
                            candidate_ptk, response)) {
        secure_zero(candidate_ptk, sizeof(candidate_ptk));
        secure_zero(response, sizeof(response));
        return false;
    }
    if (!supplicant->ops.send_eapol(supplicant->ops_context,
                                    response, sizeof(response))) {
        secure_zero(candidate_ptk, sizeof(candidate_ptk));
        secure_zero(response, sizeof(response));
        return false;
    }

    copy_bytes(supplicant->ptk, candidate_ptk, sizeof(supplicant->ptk));
    copy_bytes(supplicant->authenticator_nonce, key->nonce,
               sizeof(supplicant->authenticator_nonce));
    supplicant->m1_replay_counter = key->replay_counter;
    supplicant->eapol_version = key->eapol_version;
    supplicant->state = NEXUS_WPA2_SUPPLICANT_WAIT_M3;

    secure_zero(candidate_ptk, sizeof(candidate_ptk));
    secure_zero(response, sizeof(response));
    return true;
}

static bool process_message_three(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t *raw_frame, size_t raw_frame_length,
    const struct nexus_wpa2_eapol_key *key) {
    if (supplicant->state != NEXUS_WPA2_SUPPLICANT_WAIT_M3 ||
        !is_message_three(key) ||
        key->eapol_version != supplicant->eapol_version ||
        key->replay_counter <= supplicant->m1_replay_counter ||
        !bytes_equal_constant_time(key->nonce,
                                   supplicant->authenticator_nonce, 32U) ||
        !nexus_wpa2_eapol_mic_verify(
            supplicant->ptk, raw_frame, raw_frame_length, key->mic_offset))
        return false;

    uint8_t decrypted_key_data[NEXUS_WPA2_HANDSHAKE_KEY_DATA_MAX];
    size_t decrypted_length = 0U;
    uint8_t gtk[NEXUS_WPA2_GTK_CCMP_BYTES];
    uint8_t gtk_info = 0U;
    size_t gtk_length = 0U;
    if (!nexus_aes_key_unwrap_128(
            supplicant->ptk + NEXUS_WPA2_KCK_BYTES,
            key->key_data, key->key_data_length,
            decrypted_key_data, sizeof(decrypted_key_data),
            &decrypted_length)) {
        secure_zero(decrypted_key_data, sizeof(decrypted_key_data));
        return false;
    }
    const bool gtk_ok = nexus_wpa2_eapol_extract_gtk_kde(
        decrypted_key_data, decrypted_length, gtk, &gtk_info, &gtk_length);
    secure_zero(decrypted_key_data, sizeof(decrypted_key_data));
    if (!gtk_ok || gtk_length != NEXUS_WPA2_GTK_CCMP_BYTES) {
        secure_zero(gtk, sizeof(gtk));
        return false;
    }

    const uint8_t *pairwise_tk = supplicant->ptk +
        NEXUS_WPA2_KCK_BYTES + NEXUS_AES128_KEY_BYTES;
    if (!supplicant->ops.install_ccmp128_keys(
            supplicant->ops_context, pairwise_tk, gtk, gtk_info, key->key_rsc)) {
        if (supplicant->ops.clear_ccmp128_keys != NULL)
            supplicant->ops.clear_ccmp128_keys(supplicant->ops_context);
        secure_zero(gtk, sizeof(gtk));
        return false;
    }
    supplicant->keys_installed = true;

    uint8_t response[EAPOL_RESPONSE_FRAME_BYTES];
    if (!build_key_response(key->eapol_version, EAPOL_KEY_INFO_M4,
                            key->replay_counter, NULL, supplicant->ptk,
                            response) ||
        !supplicant->ops.send_eapol(supplicant->ops_context,
                                    response, sizeof(response))) {
        if (supplicant->ops.clear_ccmp128_keys != NULL)
            supplicant->ops.clear_ccmp128_keys(supplicant->ops_context);
        supplicant->keys_installed = false;
        secure_zero(response, sizeof(response));
        secure_zero(gtk, sizeof(gtk));
        return false;
    }

    supplicant->m1_replay_counter = key->replay_counter;
    supplicant->state = NEXUS_WPA2_SUPPLICANT_COMPLETE;
    secure_zero(supplicant->pmk, sizeof(supplicant->pmk));
    secure_zero(supplicant->ptk, sizeof(supplicant->ptk));
    secure_zero(supplicant->supplicant_nonce, sizeof(supplicant->supplicant_nonce));
    secure_zero(supplicant->authenticator_nonce, sizeof(supplicant->authenticator_nonce));
    secure_zero(response, sizeof(response));
    secure_zero(gtk, sizeof(gtk));
    return true;
}

bool nexus_wpa2_supplicant_init(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t pmk[NEXUS_WPA2_PMK_BYTES],
    const uint8_t supplicant_mac[6],
    const uint8_t authenticator_mac[6],
    const uint8_t fresh_snonce[32],
    const struct nexus_wpa2_handshake_ops *ops,
    void *ops_context) {
    if (supplicant == NULL) return false;
    *supplicant = (struct nexus_wpa2_supplicant){0};
    if (pmk == NULL || !valid_unicast_mac(supplicant_mac) ||
        !valid_unicast_mac(authenticator_mac) || fresh_snonce == NULL ||
        bytes_are_zero(fresh_snonce, 32U) || ops == NULL ||
        ops->send_eapol == NULL || ops->install_ccmp128_keys == NULL ||
        bytes_equal_constant_time(supplicant_mac, authenticator_mac, 6U))
        return false;

    copy_bytes(supplicant->pmk, pmk, sizeof(supplicant->pmk));
    copy_bytes(supplicant->supplicant_mac, supplicant_mac,
               sizeof(supplicant->supplicant_mac));
    copy_bytes(supplicant->authenticator_mac, authenticator_mac,
               sizeof(supplicant->authenticator_mac));
    copy_bytes(supplicant->supplicant_nonce, fresh_snonce,
               sizeof(supplicant->supplicant_nonce));
    supplicant->ops = *ops;
    supplicant->ops_context = ops_context;
    supplicant->state = NEXUS_WPA2_SUPPLICANT_WAIT_M1;
    return true;
}

bool nexus_wpa2_supplicant_handle_eapol(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t *frame, size_t frame_length) {
    if (supplicant == NULL || frame == NULL ||
        supplicant->ops.send_eapol == NULL ||
        supplicant->ops.install_ccmp128_keys == NULL)
        return false;

    struct nexus_wpa2_eapol_key parsed;
    if (!nexus_wpa2_eapol_parse_key(frame, frame_length, &parsed))
        return false;

    bool accepted = false;
    if (supplicant->state == NEXUS_WPA2_SUPPLICANT_WAIT_M1)
        accepted = process_message_one(supplicant, &parsed);
    else if (supplicant->state == NEXUS_WPA2_SUPPLICANT_WAIT_M3)
        accepted = process_message_three(supplicant, frame, frame_length, &parsed);
    secure_zero(&parsed, sizeof(parsed));
    return accepted;
}

void nexus_wpa2_supplicant_deinit(
    struct nexus_wpa2_supplicant *supplicant) {
    if (supplicant == NULL) return;
    if (supplicant->keys_installed &&
        supplicant->ops.clear_ccmp128_keys != NULL)
        supplicant->ops.clear_ccmp128_keys(supplicant->ops_context);
    secure_zero(supplicant, sizeof(*supplicant));
}
