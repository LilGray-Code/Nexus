#include "bluetooth_smp.h"

#include "aes_ccm.h"
#include "random.h"

#include <string.h>

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

static bool emit_pairing_failed(
    struct nexus_bt_smp_session *session, uint8_t reason,
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length) {
    if (session == NULL || out_pdu == NULL || out_length == NULL ||
        out_capacity < 2U) return false;
    out_pdu[0] = NEXUS_BT_SMP_PAIRING_FAILED;
    out_pdu[1] = reason;
    *out_length = 2U;
    session->failure_reason = reason;
    session->state = NEXUS_BT_SMP_FAILED;
    secure_clear(session->tk, sizeof(session->tk));
    secure_clear(session->stk, sizeof(session->stk));
    return true;
}

bool nexus_bt_smp_legacy_confirm(
    const uint8_t tk[16], const uint8_t random[16],
    const uint8_t pairing_request[7], const uint8_t pairing_response[7],
    uint8_t initiator_address_type, uint8_t responder_address_type,
    const uint8_t initiator_address[6], const uint8_t responder_address[6],
    uint8_t confirm[16]) {
    if (tk == NULL || random == NULL || pairing_request == NULL ||
        pairing_response == NULL || initiator_address == NULL ||
        responder_address == NULL || confirm == NULL ||
        initiator_address_type > 1U || responder_address_type > 1U)
        return false;

    uint8_t p1[16], p2[16], input[16], first[16];
    for (size_t i = 0U; i < 7U; ++i) {
        p1[i] = pairing_response[i];
        p1[i + 7U] = pairing_request[i];
    }
    p1[14] = responder_address_type;
    p1[15] = initiator_address_type;

    p2[0] = 0U;
    p2[1] = 0U;
    p2[2] = 0U;
    p2[3] = 0U;
    for (size_t i = 0U; i < 6U; ++i) {
        p2[4U + i] = initiator_address[i];
        p2[10U + i] = responder_address[i];
    }

    for (size_t i = 0U; i < 16U; ++i)
        input[i] = (uint8_t)(random[i] ^ p1[i]);
    const bool first_ok =
        nexus_aes128_encrypt_block(tk, input, first);
    if (!first_ok) {
        secure_clear(p1,sizeof(p1));
        secure_clear(p2,sizeof(p2));
        secure_clear(input,sizeof(input));
        secure_clear(first,sizeof(first));
        return false;
    }
    for (size_t i = 0U; i < 16U; ++i)
        input[i] = (uint8_t)(first[i] ^ p2[i]);
    const bool final_ok = nexus_aes128_encrypt_block(tk, input, confirm);
    secure_clear(p1,sizeof(p1));
    secure_clear(p2,sizeof(p2));
    secure_clear(input,sizeof(input));
    secure_clear(first,sizeof(first));
    return final_ok;
}

bool nexus_bt_smp_legacy_stk(
    const uint8_t tk[16], const uint8_t responder_random[16],
    const uint8_t initiator_random[16], uint8_t stk[16]) {
    if (tk == NULL || responder_random == NULL ||
        initiator_random == NULL || stk == NULL) return false;
    uint8_t input[16];
    for (size_t i = 0U; i < 8U; ++i) {
        input[i] = responder_random[i + 8U];
        input[i + 8U] = initiator_random[i + 8U];
    }
    const bool result = nexus_aes128_encrypt_block(tk, input, stk);
    secure_clear(input, sizeof(input));
    return result;
}

bool nexus_bt_smp_start_with_random(
    struct nexus_bt_smp_session *session,
    uint8_t local_address_type, const uint8_t local_address[6],
    uint8_t peer_address_type, const uint8_t peer_address[6],
    const uint8_t local_random[16],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length) {
    if (session == NULL || local_address == NULL || peer_address == NULL ||
        local_random == NULL || out_pdu == NULL || out_length == NULL ||
        out_capacity < 7U || local_address_type > 1U ||
        peer_address_type > 1U) return false;

    *session = (struct nexus_bt_smp_session){0};
    session->state = NEXUS_BT_SMP_WAIT_PAIRING_RESPONSE;
    session->local_address_type = local_address_type;
    session->peer_address_type = peer_address_type;
    memcpy(session->local_address, local_address, 6U);
    memcpy(session->peer_address, peer_address, 6U);
    memcpy(session->local_random, local_random, 16U);

    /* NoInputNoOutput + no OOB + no MITM + no bonding. A zero key
     * distribution mask prevents persistent keys from being exchanged. */
    const uint8_t request[7] = {
        NEXUS_BT_SMP_PAIRING_REQUEST,
        0x03U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U
    };
    memcpy(session->preq, request, sizeof(request));
    memcpy(out_pdu, request, sizeof(request));
    *out_length = sizeof(request);
    return true;
}

bool nexus_bt_smp_start(
    struct nexus_bt_smp_session *session,
    uint8_t local_address_type, const uint8_t local_address[6],
    uint8_t peer_address_type, const uint8_t peer_address[6],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length) {
    uint8_t random[16] = {0U};
    if (!nexus_rng_fill(random, sizeof(random))) {
        secure_clear(random, sizeof(random));
        return false;
    }
    const bool result = nexus_bt_smp_start_with_random(
        session, local_address_type, local_address,
        peer_address_type, peer_address, random,
        out_pdu, out_capacity, out_length);
    secure_clear(random, sizeof(random));
    return result;
}

bool nexus_bt_smp_handle_pdu(
    struct nexus_bt_smp_session *session,
    const uint8_t *pdu, size_t pdu_length,
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length,
    bool *start_encryption) {
    if (session == NULL || pdu == NULL || pdu_length == 0U ||
        out_pdu == NULL || out_length == NULL || start_encryption == NULL)
        return false;
    *out_length = 0U;
    *start_encryption = false;
    if (session->state == NEXUS_BT_SMP_FAILED ||
        session->state == NEXUS_BT_SMP_ENCRYPTED ||
        session->state == NEXUS_BT_SMP_IDLE) return false;

    const uint8_t code = pdu[0];
    if (code == NEXUS_BT_SMP_PAIRING_FAILED) {
        if (pdu_length != 2U) return false;
        session->failure_reason = pdu[1];
        session->state = NEXUS_BT_SMP_FAILED;
        secure_clear(session->stk, sizeof(session->stk));
        secure_clear(session->tk, sizeof(session->tk));
        return true;
    }

    if (session->state == NEXUS_BT_SMP_WAIT_PAIRING_RESPONSE) {
        if (code != NEXUS_BT_SMP_PAIRING_RESPONSE || pdu_length != 7U) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_INVALID_PARAMETERS,
                out_pdu, out_capacity, out_length);
        }
        const uint8_t io_capability = pdu[1U];
        const uint8_t oob_flag = pdu[2U];
        const uint8_t auth_requirements = pdu[3U];
        const uint8_t key_size = pdu[4U];
        const uint8_t initiator_keys = pdu[5U];
        const uint8_t responder_keys = pdu[6U];
        if (io_capability > 4U || oob_flag > 1U ||
            (auth_requirements & 0x04U) != 0U ||
            (auth_requirements & 0x03U) != 0U) {
            const uint8_t reason = (auth_requirements & 0x04U) != 0U
                ? NEXUS_BT_SMP_REASON_AUTHENTICATION_REQUIREMENTS
                : oob_flag != 0U ? NEXUS_BT_SMP_REASON_OOB_NOT_AVAILABLE
                                 : NEXUS_BT_SMP_REASON_INVALID_PARAMETERS;
            return emit_pairing_failed(session, reason,
                out_pdu, out_capacity, out_length);
        }
        if (key_size != 16U) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_ENCRYPTION_KEY_SIZE,
                out_pdu, out_capacity, out_length);
        }
        if (initiator_keys != 0U || responder_keys != 0U) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_PAIRING_NOT_SUPPORTED,
                out_pdu, out_capacity, out_length);
        }
        memcpy(session->pres, pdu, 7U);
        if (!nexus_bt_smp_legacy_confirm(session->tk,
                session->local_random, session->preq, session->pres,
                session->local_address_type, session->peer_address_type,
                session->local_address, session->peer_address,
                session->local_confirm)) return false;
        if (out_capacity < 17U) return false;
        out_pdu[0] = NEXUS_BT_SMP_PAIRING_CONFIRM;
        memcpy(out_pdu + 1U, session->local_confirm, 16U);
        *out_length = 17U;
        session->state = NEXUS_BT_SMP_WAIT_PEER_CONFIRM;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_WAIT_PEER_CONFIRM) {
        if (code != NEXUS_BT_SMP_PAIRING_CONFIRM || pdu_length != 17U) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_INVALID_PARAMETERS,
                out_pdu, out_capacity, out_length);
        }
        if (out_capacity < 17U) return false;
        memcpy(session->remote_confirm, pdu + 1U, 16U);
        out_pdu[0] = NEXUS_BT_SMP_PAIRING_RANDOM;
        memcpy(out_pdu + 1U, session->local_random, 16U);
        *out_length = 17U;
        session->state = NEXUS_BT_SMP_WAIT_PEER_RANDOM;
        return true;
    }

    if (session->state == NEXUS_BT_SMP_WAIT_PEER_RANDOM) {
        if (code != NEXUS_BT_SMP_PAIRING_RANDOM || pdu_length != 17U) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_INVALID_PARAMETERS,
                out_pdu, out_capacity, out_length);
        }
        memcpy(session->remote_random, pdu + 1U, 16U);
        uint8_t expected[16] = {0U};
        const bool calculated = nexus_bt_smp_legacy_confirm(
            session->tk, session->remote_random,
            session->preq, session->pres,
            session->local_address_type, session->peer_address_type,
            session->local_address, session->peer_address, expected);
        const bool valid = calculated && constant_time_equal(
            expected, session->remote_confirm, sizeof(expected));
        secure_clear(expected, sizeof(expected));
        if (!valid) {
            return emit_pairing_failed(session,
                NEXUS_BT_SMP_REASON_CONFIRM_VALUE_FAILED,
                out_pdu, out_capacity, out_length);
        }
        if (!nexus_bt_smp_legacy_stk(session->tk,
                session->remote_random, session->local_random,
                session->stk)) return false;
        session->state = NEXUS_BT_SMP_ENCRYPTION_PENDING;
        *start_encryption = true;
        return true;
    }

    return false;
}

bool nexus_bt_smp_get_stk(
    const struct nexus_bt_smp_session *session, uint8_t stk[16]) {
    if (session == NULL || stk == NULL ||
        session->state != NEXUS_BT_SMP_ENCRYPTION_PENDING) return false;
    memcpy(stk, session->stk, 16U);
    return true;
}

bool nexus_bt_smp_mark_encrypted(
    struct nexus_bt_smp_session *session, bool success) {
    if (session == NULL ||
        session->state != NEXUS_BT_SMP_ENCRYPTION_PENDING) return false;
    if (!success) {
        session->state = NEXUS_BT_SMP_FAILED;
        session->failure_reason = NEXUS_BT_SMP_REASON_UNSPECIFIED;
        secure_clear(session->stk, sizeof(session->stk));
        secure_clear(session->tk, sizeof(session->tk));
        return true;
    }
    session->state = NEXUS_BT_SMP_ENCRYPTED;
    secure_clear(session->stk, sizeof(session->stk));
    secure_clear(session->tk, sizeof(session->tk));
    secure_clear(session->local_random, sizeof(session->local_random));
    secure_clear(session->remote_random, sizeof(session->remote_random));
    secure_clear(session->local_confirm, sizeof(session->local_confirm));
    secure_clear(session->remote_confirm, sizeof(session->remote_confirm));
    return true;
}
