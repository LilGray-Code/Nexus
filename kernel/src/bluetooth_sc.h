#ifndef NEXUS_BLUETOOTH_SC_H
#define NEXUS_BLUETOOTH_SC_H

#include <stdbool.h>
#include <stdint.h>

/* Bluetooth LE Secure Connections cryptographic functions. Address inputs
 * are the 7-octet SMP format: address type followed by the 48-bit address. */
bool nexus_bt_sc_f4(
    const uint8_t u[32], const uint8_t v[32],
    const uint8_t nonce[16], uint8_t z,
    uint8_t confirm[16]);

bool nexus_bt_sc_f5(
    const uint8_t dhkey[32],
    const uint8_t nonce_central[16],
    const uint8_t nonce_peripheral[16],
    const uint8_t address_central[7],
    const uint8_t address_peripheral[7],
    uint8_t mac_key[16], uint8_t ltk[16]);

bool nexus_bt_sc_f6(
    const uint8_t mac_key[16],
    const uint8_t nonce_initiator[16],
    const uint8_t nonce_responder[16],
    const uint8_t r[16],
    const uint8_t io_capability[3],
    const uint8_t address_a[7],
    const uint8_t address_b[7],
    uint8_t check[16]);

#endif
