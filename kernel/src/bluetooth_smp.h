#ifndef NEXUS_BLUETOOTH_SMP_H
#define NEXUS_BLUETOOTH_SMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_SMP_CID 0x0006U
#define NEXUS_BT_SMP_PAIRING_REQUEST 0x01U
#define NEXUS_BT_SMP_PAIRING_RESPONSE 0x02U
#define NEXUS_BT_SMP_PAIRING_CONFIRM 0x03U
#define NEXUS_BT_SMP_PAIRING_RANDOM 0x04U
#define NEXUS_BT_SMP_PAIRING_FAILED 0x05U

#define NEXUS_BT_SMP_REASON_PASSKEY_ENTRY_FAILED 0x01U
#define NEXUS_BT_SMP_REASON_OOB_NOT_AVAILABLE 0x02U
#define NEXUS_BT_SMP_REASON_AUTHENTICATION_REQUIREMENTS 0x03U
#define NEXUS_BT_SMP_REASON_CONFIRM_VALUE_FAILED 0x04U
#define NEXUS_BT_SMP_REASON_PAIRING_NOT_SUPPORTED 0x05U
#define NEXUS_BT_SMP_REASON_ENCRYPTION_KEY_SIZE 0x06U
#define NEXUS_BT_SMP_REASON_COMMAND_NOT_SUPPORTED 0x07U
#define NEXUS_BT_SMP_REASON_INVALID_PARAMETERS 0x0AU
#define NEXUS_BT_SMP_REASON_UNSPECIFIED 0x08U
#define NEXUS_BT_SMP_REASON_BUSY 0x10U

enum nexus_bt_smp_state {
    NEXUS_BT_SMP_IDLE = 0U,
    NEXUS_BT_SMP_WAIT_PAIRING_RESPONSE,
    NEXUS_BT_SMP_WAIT_PEER_CONFIRM,
    NEXUS_BT_SMP_WAIT_PEER_RANDOM,
    NEXUS_BT_SMP_ENCRYPTION_PENDING,
    NEXUS_BT_SMP_ENCRYPTED,
    NEXUS_BT_SMP_FAILED
};

struct nexus_bt_smp_session {
    enum nexus_bt_smp_state state;
    uint8_t preq[7];
    uint8_t pres[7];
    uint8_t tk[16];
    uint8_t local_random[16];
    uint8_t remote_random[16];
    uint8_t local_confirm[16];
    uint8_t remote_confirm[16];
    uint8_t stk[16];
    uint8_t local_address_type;
    uint8_t peer_address_type;
    uint8_t local_address[6];
    uint8_t peer_address[6];
    uint8_t failure_reason;
};

/* Bluetooth LE legacy pairing primitives (security function e / c1 / s1). */
bool nexus_bt_smp_legacy_confirm(
    const uint8_t tk[16], const uint8_t random[16],
    const uint8_t pairing_request[7], const uint8_t pairing_response[7],
    uint8_t initiator_address_type, uint8_t responder_address_type,
    const uint8_t initiator_address[6], const uint8_t responder_address[6],
    uint8_t confirm[16]);
bool nexus_bt_smp_legacy_stk(
    const uint8_t tk[16], const uint8_t responder_random[16],
    const uint8_t initiator_random[16], uint8_t stk[16]);

/* Start a non-bonding, no-MITM legacy Just Works pairing exchange. The
 * deterministic variant accepts caller-supplied random bytes for testing;
 * production callers must use nexus_bt_smp_start and its hardware RNG. */
bool nexus_bt_smp_start_with_random(
    struct nexus_bt_smp_session *session,
    uint8_t local_address_type, const uint8_t local_address[6],
    uint8_t peer_address_type, const uint8_t peer_address[6],
    const uint8_t local_random[16],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length);
bool nexus_bt_smp_start(
    struct nexus_bt_smp_session *session,
    uint8_t local_address_type, const uint8_t local_address[6],
    uint8_t peer_address_type, const uint8_t peer_address[6],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length);

/* Continue legacy pairing when the peer's response to an SC-capable request
 * explicitly omits the Secure Connections bit. Reuses the already exchanged
 * Pairing Request/Response and sends Confirm without starting a second pair. */
bool nexus_bt_smp_start_from_exchange(
    struct nexus_bt_smp_session *session,
    const uint8_t pairing_request[7], const uint8_t pairing_response[7],
    uint8_t local_address_type, const uint8_t local_address[6],
    uint8_t peer_address_type, const uint8_t peer_address[6],
    const uint8_t local_random[16],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length);

/* Process one SMP command PDU. start_encryption becomes true once the peer's
 * confirm is verified and the Short Term Key is ready. Pairing Failed is
 * emitted for rejected feature sets or failed confirm verification. */
bool nexus_bt_smp_handle_pdu(
    struct nexus_bt_smp_session *session,
    const uint8_t *pdu, size_t pdu_length,
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length,
    bool *start_encryption);
bool nexus_bt_smp_get_stk(
    const struct nexus_bt_smp_session *session, uint8_t stk[16]);
bool nexus_bt_smp_mark_encrypted(
    struct nexus_bt_smp_session *session, bool success);

#endif
