#ifndef NEXUS_BLUETOOTH_SMP_SC_H
#define NEXUS_BLUETOOTH_SMP_SC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_BT_SMP_SC_PAIRING_REQUEST 0x01U
#define NEXUS_BT_SMP_SC_PAIRING_RESPONSE 0x02U
#define NEXUS_BT_SMP_SC_PAIRING_CONFIRM 0x03U
#define NEXUS_BT_SMP_SC_PAIRING_RANDOM 0x04U
#define NEXUS_BT_SMP_SC_PAIRING_FAILED 0x05U
#define NEXUS_BT_SMP_SC_PAIRING_PUBLIC_KEY 0x0CU
#define NEXUS_BT_SMP_SC_PAIRING_DHKEY_CHECK 0x0DU

enum nexus_bt_smp_sc_state {
    NEXUS_BT_SMP_SC_IDLE = 0U,
    NEXUS_BT_SMP_SC_WAIT_PAIRING_RESPONSE,
    NEXUS_BT_SMP_SC_WAIT_PEER_PUBLIC_KEY,
    NEXUS_BT_SMP_SC_DHKEY_PENDING,
    NEXUS_BT_SMP_SC_WAIT_PEER_CONFIRM,
    NEXUS_BT_SMP_SC_WAIT_PEER_RANDOM,
    NEXUS_BT_SMP_SC_WAIT_PEER_DHKEY_CHECK,
    NEXUS_BT_SMP_SC_ENCRYPTION_PENDING,
    NEXUS_BT_SMP_SC_ENCRYPTED,
    NEXUS_BT_SMP_SC_FALLBACK_LEGACY,
    NEXUS_BT_SMP_SC_FAILED
};

struct nexus_bt_smp_sc_session {
    enum nexus_bt_smp_sc_state state;
    uint8_t preq[7];
    uint8_t pres[7];
    uint8_t local_address[7];
    uint8_t peer_address[7];
    uint8_t local_public_key[64];
    uint8_t peer_public_key[64];
    uint8_t dhkey[32];
    uint8_t mac_key[16];
    uint8_t ltk[16];
    uint8_t local_nonce[16];
    uint8_t peer_nonce[16];
    uint8_t peer_confirm[16];
    uint8_t local_io_capability[3];
    uint8_t peer_io_capability[3];
    uint8_t failure_reason;
};

/* Secure Connections NoInputNoOutput, no-MITM, no-bonding initiator. Addresses
 * are 7-octet SMP values (address type followed by six address octets). */
bool nexus_bt_smp_sc_start(
    struct nexus_bt_smp_sc_session *session,
    const uint8_t local_address[7], const uint8_t peer_address[7],
    const uint8_t local_public_key[64],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length);

/* Returns generate_dhkey=true when a peer P-256 point is ready for HCI
 * LE Generate DHKey. fallback_legacy is true only when the peer response
 * explicitly says that it does not support Secure Connections. */
bool nexus_bt_smp_sc_handle_pdu(
    struct nexus_bt_smp_sc_session *session,
    const uint8_t *pdu, size_t pdu_length,
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length,
    bool *generate_dhkey, bool *start_encryption,
    bool *fallback_legacy);

/* Supply the HCI-controller-generated DHKey and fresh local nonce. Produces
 * the local Pairing Confirm PDU and advances the exchange. */
bool nexus_bt_smp_sc_handle_dhkey(
    struct nexus_bt_smp_sc_session *session,
    const uint8_t dhkey[32], const uint8_t local_nonce[16],
    uint8_t *out_pdu, size_t out_capacity, size_t *out_length);

bool nexus_bt_smp_sc_get_ltk(
    const struct nexus_bt_smp_sc_session *session, uint8_t ltk[16]);
bool nexus_bt_smp_sc_mark_encrypted(
    struct nexus_bt_smp_sc_session *session, bool success);
void nexus_bt_smp_sc_fail(
    struct nexus_bt_smp_sc_session *session, uint8_t reason);

#endif
