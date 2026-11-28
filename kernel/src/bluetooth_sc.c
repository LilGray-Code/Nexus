#include "bluetooth_sc.h"

#include "aes_ccm.h"

#include <stddef.h>
#include <stdint.h>

static void secure_clear(void *memory, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)memory;
    if (bytes == NULL) return;
    while (length-- != 0U) *bytes++ = 0U;
}

bool nexus_bt_sc_f4(
    const uint8_t u[32], const uint8_t v[32],
    const uint8_t nonce[16], uint8_t z,
    uint8_t confirm[16]) {
    if (u == NULL || v == NULL || nonce == NULL || confirm == NULL)
        return false;
    uint8_t message[65];
    for (size_t i = 0U; i < 32U; ++i) {
        message[i] = u[i];
        message[32U+i] = v[i];
    }
    message[64] = z;
    const bool result = nexus_aes128_cmac(
        nonce, message, sizeof(message), confirm);
    secure_clear(message, sizeof(message));
    return result;
}

bool nexus_bt_sc_f5(
    const uint8_t dhkey[32],
    const uint8_t nonce_central[16],
    const uint8_t nonce_peripheral[16],
    const uint8_t address_central[7],
    const uint8_t address_peripheral[7],
    uint8_t mac_key[16], uint8_t ltk[16]) {
    if (dhkey == NULL || nonce_central == NULL ||
        nonce_peripheral == NULL || address_central == NULL ||
        address_peripheral == NULL || mac_key == NULL || ltk == NULL)
        return false;

    static const uint8_t salt[16] = {
        0x6CU,0x88U,0x83U,0x91U,0xAAU,0xF5U,0xA5U,0x38U,
        0x60U,0x37U,0x0BU,0xDBU,0x5AU,0x60U,0x83U,0xBEU
    };
    static const uint8_t key_id[4] = {0x62U,0x74U,0x6CU,0x65U};
    uint8_t t[16] = {0U};
    uint8_t message[53] = {0U};
    uint8_t output[16] = {0U};
    if (!nexus_aes128_cmac(salt, dhkey, 32U, t)) {
        secure_clear(t, sizeof(t));
        return false;
    }

    message[0] = 0U;
    for (size_t i = 0U; i < sizeof(key_id); ++i)
        message[1U+i] = key_id[i];
    for (size_t i = 0U; i < 16U; ++i) {
        message[5U+i] = nonce_central[i];
        message[21U+i] = nonce_peripheral[i];
    }
    for (size_t i = 0U; i < 7U; ++i) {
        message[37U+i] = address_central[i];
        message[44U+i] = address_peripheral[i];
    }
    message[51] = 0x01U;
    message[52] = 0x00U;

    const bool mac_ok = nexus_aes128_cmac(t, message, sizeof(message), output);
    message[0] = 1U;
    const bool ltk_ok = mac_ok && nexus_aes128_cmac(
        t, message, sizeof(message), ltk);
    if (ltk_ok) {
        for (size_t i = 0U; i < 16U; ++i) mac_key[i] = output[i];
    }
    secure_clear(t, sizeof(t));
    secure_clear(message, sizeof(message));
    secure_clear(output, sizeof(output));
    if (!ltk_ok) {
        secure_clear(mac_key, 16U);
        secure_clear(ltk, 16U);
    }
    return ltk_ok;
}

bool nexus_bt_sc_f6(
    const uint8_t mac_key[16],
    const uint8_t nonce_initiator[16],
    const uint8_t nonce_responder[16],
    const uint8_t r[16],
    const uint8_t io_capability[3],
    const uint8_t address_a[7],
    const uint8_t address_b[7],
    uint8_t check[16]) {
    if (mac_key == NULL || nonce_initiator == NULL ||
        nonce_responder == NULL || r == NULL || io_capability == NULL ||
        address_a == NULL || address_b == NULL || check == NULL)
        return false;

    uint8_t message[65];
    for (size_t i = 0U; i < 16U; ++i) {
        message[i] = nonce_initiator[i];
        message[16U+i] = nonce_responder[i];
        message[32U+i] = r[i];
    }
    for (size_t i = 0U; i < 3U; ++i) message[48U+i] = io_capability[i];
    for (size_t i = 0U; i < 7U; ++i) {
        message[51U+i] = address_a[i];
        message[58U+i] = address_b[i];
    }
    const bool result = nexus_aes128_cmac(
        mac_key, message, sizeof(message), check);
    secure_clear(message, sizeof(message));
    return result;
}
