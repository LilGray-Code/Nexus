#include "bluetooth_smp_sc.h"

#include "bluetooth_sc.h"
#include "bluetooth_smp.h"

#include <string.h>

#define SMP_REASON_PASSKEY_ENTRY_FAILED 0x01U
#define SMP_REASON_OOB_NOT_AVAILABLE 0x02U
#define SMP_REASON_AUTHENTICATION_REQUIREMENTS 0x03U
#define SMP_REASON_CONFIRM_VALUE_FAILED 0x04U
#define SMP_REASON_PAIRING_NOT_SUPPORTED 0x05U
#define SMP_REASON_ENCRYPTION_KEY_SIZE 0x06U
#define SMP_REASON_DHKEY_CHECK_FAILED 0x0BU
#define SMP_REASON_INVALID_PARAMETERS 0x0AU
#define SMP_REASON_UNSPECIFIED 0x08U

static void secure_clear(void *memory, size_t length) {
    if (memory == NULL) return;
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    while (length-- != 0U) *bytes++ = 0U;
}

static bool constant_time_equal(
    const uint8_t *left, const uint8_t *right, size_t length) {
    if (left == NULL || right == NULL) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0U;
}

static bool all_equal(const uint8_t *bytes, size_t length, uint8_t value) {
    if (bytes == NULL) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < length; ++i)
        difference |= (uint8_t)(bytes[i] ^ value);
    return difference == 0U;
}

static bool emit_failed(
    struct nexus_bt_smp_sc_session *session, uint8_t reason,
    uint8_t *out_pdu, size_t capacity, size_t *out_length) {
    if (session == NULL || out_pdu == NULL || out_length == NULL ||
        capacity < 2U) return false;
    out_pdu[0] = NEXUS_BT_SMP_SC_PAIRING_FAILED;
    out_pdu[1] = reason;
    *out_length = 2U;
    session->state = NEXUS_BT_SMP_SC_FAILED;
    session->failure_reason = reason;
    secure_clear(session->dhkey, sizeof(session->dhkey));
    secure_clear(session->mac_key, sizeof(session->mac_key));
    secure_clear(session->ltk, sizeof(session->ltk));
    secure_clear(session->local_nonce, sizeof(session->local_nonce));
    secure_clear(session->peer_nonce, sizeof(session->peer_nonce));
    secure_clear(session->peer_confirm, sizeof(session->peer_confirm));
    return true;
}

void nexus_bt_smp_sc_fail(
    struct nexus_bt_smp_sc_session *session, uint8_t reason) {
    if (session == NULL) return;
    session->state = NEXUS_BT_SMP_SC_FAILED;
    session->failure_reason = reason;
    secure_clear(session->dhkey, sizeof(session->dhkey));
    secure_clear(session->mac_key, sizeof(session->mac_key));
    secure_clear(session->ltk, sizeof(session->ltk));
    secure_clear(session->local_nonce, sizeof(session->local_nonce));
    secure_clear(session->peer_nonce, sizeof(session->peer_nonce));
    secure_clear(session->peer_confirm, sizeof(session->peer_confirm));
}

bool nexus_bt_smp_sc_start(
    struct nexus_bt_smp_sc_session *session,
    const uint8_t local_address[7], const uint8_t peer_address[7],
    const uint8_t local_public_key[64],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length) {
    if (session == NULL || local_address == NULL || peer_address == NULL ||
        local_public_key == NULL || out_pdu == NULL || out_length == NULL ||
        out_capacity < 7U ||
        all_equal(local_public_key, 64U, 0U) ||
        all_equal(local_public_key, 64U, 0xFFU)) return false;

    *session = (struct nexus_bt_smp_sc_session){0};
    memcpy(session->local_address, local_address, 7U);
    memcpy(session->peer_address, peer_address, 7U);
    memcpy(session->local_public_key, local_public_key, 64U);
    /* AuthReq = SC only; no bonding, no MITM, no OOB, no key distribution.
     * IO capability 0x03 is NoInputNoOutput. */
    const uint8_t request[7] = {
        NEXUS_BT_SMP_SC_PAIRING_REQUEST,
        0x03U, 0x00U, 0x08U, 0x10U, 0x00U, 0x00U
    };
    memcpy(session->preq, request, sizeof(request));
    memcpy(out_pdu, request, sizeof(request));
    *out_length = sizeof(request);
    session->local_io_capability[0] = 0x08U; /* AuthReq */
    session->local_io_capability[1] = 0x00U; /* OOB */
    session->local_io_capability[2] = 0x03U; /* IO capability */
    session->state = NEXUS_BT_SMP_SC_WAIT_PAIRING_RESPONSE;
    return true;
}

bool nexus_bt_smp_sc_handle_pdu(
    struct nexus_bt_smp_sc_session *session,
    const uint8_t *pdu, size_t pdu_length,
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length,
    bool *generate_dhkey, bool *start_encryption,
    bool *fallback_legacy) {
    if (session == NULL || pdu == NULL || pdu_length == 0U ||
        out_pdu == NULL || out_length == NULL ||
        generate_dhkey == NULL || start_encryption == NULL ||
        fallback_legacy == NULL) return false;
    *out_length = 0U;
    *generate_dhkey = false;
    *start_encryption = false;
    *fallback_legacy = false;

    const uint8_t code = pdu[0];
    if (code > 0x0EU) return true; /* Reserved SMP commands are ignored. */
    if (session->state == NEXUS_BT_SMP_SC_FAILED ||
        session->state == NEXUS_BT_SMP_SC_ENCRYPTED ||
        session->state == NEXUS_BT_SMP_SC_IDLE ||
        session->state == NEXUS_BT_SMP_SC_FALLBACK_LEGACY) return false;

    if (code == NEXUS_BT_SMP_SC_PAIRING_FAILED) {
        if (pdu_length != 2U) return false;
        nexus_bt_smp_sc_fail(session, pdu[1]);
        return true;
    }

    if (session->state == NEXUS_BT_SMP_SC_WAIT_PAIRING_RESPONSE) {
        if (code != NEXUS_BT_SMP_SC_PAIRING_RESPONSE || pdu_length != 7U)
            return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
                out_pdu,out_capacity,out_length);
        const uint8_t io_capability = pdu[1];
        const uint8_t oob = pdu[2];
        const uint8_t auth_req = pdu[3];
        const uint8_t key_size = pdu[4];
        const uint8_t initiator_keys = pdu[5];
        const uint8_t responder_keys = pdu[6];
        if ((auth_req & 0x08U) == 0U) {
            memcpy(session->pres,pdu,7U);
            session->state = NEXUS_BT_SMP_SC_FALLBACK_LEGACY;
            *fallback_legacy = true;
            return true;
        }
        if (io_capability > 4U || oob > 1U ||
            (auth_req & 0xC0U) != 0U || (auth_req & 0x04U) != 0U) {
            return emit_failed(session,
                SMP_REASON_AUTHENTICATION_REQUIREMENTS,
                out_pdu,out_capacity,out_length);
        }
        if ((auth_req & 0x03U) != 0U || key_size != 16U ||
            initiator_keys != 0U || responder_keys != 0U) {
            const uint8_t reason = key_size != 16U
                ? SMP_REASON_ENCRYPTION_KEY_SIZE
                : SMP_REASON_PAIRING_NOT_SUPPORTED;
            return emit_failed(session,reason,
                out_pdu,out_capacity,out_length);
        }
        if (oob != 0U) {
            return emit_failed(session,SMP_REASON_OOB_NOT_AVAILABLE,
                out_pdu,out_capacity,out_length);
        }
        memcpy(session->pres,pdu,7U);
        session->peer_io_capability[0] = auth_req;
        session->peer_io_capability[1] = oob;
        session->peer_io_capability[2] = io_capability;
        if (out_capacity < 65U) return false;
        out_pdu[0] = NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY;
        memcpy(out_pdu+1U,session->local_public_key,64U);
        *out_length = 65U;
        session->state = NEXUS_BT_SMP_SC_WAIT_PEER_PUBLIC_KEY;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_SC_WAIT_PEER_PUBLIC_KEY) {
        if (code != NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY || pdu_length != 65U)
            return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
                out_pdu,out_capacity,out_length);
        const uint8_t *remote = pdu + 1U;
        if (all_equal(remote,64U,0U) || all_equal(remote,64U,0xFFU) ||
            constant_time_equal(remote,session->local_public_key,32U)) {
            return emit_failed(session,SMP_REASON_DHKEY_CHECK_FAILED,
                out_pdu,out_capacity,out_length);
        }
        memcpy(session->peer_public_key,remote,64U);
        session->state = NEXUS_BT_SMP_SC_DHKEY_PENDING;
        *generate_dhkey = true;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_SC_WAIT_PEER_CONFIRM) {
        if (code != NEXUS_BT_SMP_SC_PAIRING_CONFIRM || pdu_length != 17U)
            return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
                out_pdu,out_capacity,out_length);
        if (out_capacity < 17U) return false;
        memcpy(session->peer_confirm,pdu+1U,16U);
        out_pdu[0] = NEXUS_BT_SMP_SC_PAIRING_RANDOM;
        memcpy(out_pdu+1U,session->local_nonce,16U);
        *out_length = 17U;
        session->state = NEXUS_BT_SMP_SC_WAIT_PEER_RANDOM;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_SC_WAIT_PEER_RANDOM) {
        if (code != NEXUS_BT_SMP_SC_PAIRING_RANDOM || pdu_length != 17U)
            return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
                out_pdu,out_capacity,out_length);
        memcpy(session->peer_nonce,pdu+1U,16U);
        uint8_t expected[16] = {0U};
        const bool confirm_ok = nexus_bt_sc_f4(
            session->peer_public_key,
            session->local_public_key,
            session->peer_nonce,0U,expected) &&
            constant_time_equal(expected,session->peer_confirm,16U);
        secure_clear(expected,sizeof(expected));
        if (!confirm_ok)
            return emit_failed(session,SMP_REASON_CONFIRM_VALUE_FAILED,
                out_pdu,out_capacity,out_length);

        if (!nexus_bt_sc_f5(session->dhkey,
                session->local_nonce,session->peer_nonce,
                session->local_address,session->peer_address,
                session->mac_key,session->ltk))
            return emit_failed(session,SMP_REASON_UNSPECIFIED,
                out_pdu,out_capacity,out_length);

        uint8_t zero_r[16] = {0U};
        uint8_t check[16] = {0U};
        const bool check_ok = nexus_bt_sc_f6(
            session->mac_key,session->local_nonce,session->peer_nonce,
            zero_r,session->local_io_capability,
            session->local_address,session->peer_address,check);
        secure_clear(zero_r,sizeof(zero_r));
        if (!check_ok)
            return emit_failed(session,SMP_REASON_UNSPECIFIED,
                out_pdu,out_capacity,out_length);
        if (out_capacity < 17U) {
            secure_clear(check,sizeof(check));
            return false;
        }
        out_pdu[0] = NEXUS_BT_SMP_SC_PAIRING_DHKEY_CHECK;
        memcpy(out_pdu+1U,check,16U);
        *out_length = 17U;
        secure_clear(check,sizeof(check));
        session->state = NEXUS_BT_SMP_SC_WAIT_PEER_DHKEY_CHECK;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_SC_WAIT_PEER_DHKEY_CHECK) {
        if (code != NEXUS_BT_SMP_SC_PAIRING_DHKEY_CHECK || pdu_length != 17U)
            return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
                out_pdu,out_capacity,out_length);
        uint8_t zero_r[16] = {0U};
        uint8_t expected[16] = {0U};
        const bool check_ok = nexus_bt_sc_f6(
            session->mac_key,session->peer_nonce,session->local_nonce,
            zero_r,session->peer_io_capability,
            session->peer_address,session->local_address,expected) &&
            constant_time_equal(expected,pdu+1U,16U);
        secure_clear(zero_r,sizeof(zero_r));
        secure_clear(expected,sizeof(expected));
        if (!check_ok)
            return emit_failed(session,SMP_REASON_DHKEY_CHECK_FAILED,
                out_pdu,out_capacity,out_length);
        session->state = NEXUS_BT_SMP_SC_ENCRYPTION_PENDING;
        *start_encryption = true;
        return true;
    }

    return emit_failed(session,SMP_REASON_INVALID_PARAMETERS,
        out_pdu,out_capacity,out_length);
}

bool nexus_bt_smp_sc_handle_dhkey(
    struct nexus_bt_smp_sc_session *session, const uint8_t dhkey[32],
    const uint8_t local_nonce[16],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length) {
    if (session == NULL || dhkey == NULL || local_nonce == NULL ||
        out_pdu == NULL || out_length == NULL ||
        session->state != NEXUS_BT_SMP_SC_DHKEY_PENDING ||
        all_equal(dhkey,32U,0U) || all_equal(dhkey,32U,0xFFU))
        return false;
    if (out_capacity < 17U) return false;
    memcpy(session->dhkey,dhkey,32U);
    memcpy(session->local_nonce,local_nonce,16U);
    if (all_equal(local_nonce,16U,0U) ||
        all_equal(local_nonce,16U,0xFFU)) {
        nexus_bt_smp_sc_fail(session,SMP_REASON_UNSPECIFIED);
        return false;
    }
    uint8_t confirm[16] = {0U};
    if (!nexus_bt_sc_f4(session->local_public_key,
            session->peer_public_key,session->local_nonce,0U,confirm)) {
        secure_clear(confirm,sizeof(confirm));
        nexus_bt_smp_sc_fail(session,SMP_REASON_UNSPECIFIED);
        return false;
    }
    out_pdu[0] = NEXUS_BT_SMP_SC_PAIRING_CONFIRM;
    memcpy(out_pdu+1U,confirm,16U);
    *out_length = 17U;
    secure_clear(confirm,sizeof(confirm));
    session->state = NEXUS_BT_SMP_SC_WAIT_PEER_CONFIRM;
    return true;
}

bool nexus_bt_smp_sc_get_ltk(
    const struct nexus_bt_smp_sc_session *session, uint8_t ltk[16]) {
    if (session == NULL || ltk == NULL ||
        session->state != NEXUS_BT_SMP_SC_ENCRYPTION_PENDING) return false;
    memcpy(ltk,session->ltk,16U);
    return true;
}

bool nexus_bt_smp_sc_mark_encrypted(
    struct nexus_bt_smp_sc_session *session, bool success) {
    if (session == NULL ||
        session->state != NEXUS_BT_SMP_SC_ENCRYPTION_PENDING) return false;
    if (!success) {
        nexus_bt_smp_sc_fail(session,SMP_REASON_UNSPECIFIED);
        return true;
    }
    session->state = NEXUS_BT_SMP_SC_ENCRYPTED;
    secure_clear(session->dhkey,sizeof(session->dhkey));
    secure_clear(session->mac_key,sizeof(session->mac_key));
    secure_clear(session->ltk,sizeof(session->ltk));
    secure_clear(session->local_nonce,sizeof(session->local_nonce));
    secure_clear(session->peer_nonce,sizeof(session->peer_nonce));
    secure_clear(session->peer_confirm,sizeof(session->peer_confirm));
    secure_clear(session->local_public_key,sizeof(session->local_public_key));
    secure_clear(session->peer_public_key,sizeof(session->peer_public_key));
    return true;
}
