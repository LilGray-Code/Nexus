#include "wpa2_psk.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f')
        return (uint8_t)(value - 'a' + 10);
    assert(0);
    return 0U;
}

static void expect_hex(const uint8_t *actual, size_t length,
                       const char *expected) {
    for (size_t i = 0U; i < length; ++i) {
        const uint8_t wanted =
            (uint8_t)((hex_nibble(expected[i * 2U]) << 4) |
                      hex_nibble(expected[i * 2U + 1U]));
        assert(actual[i] == wanted);
    }
}

static void test_wpa2_pmk_known_vector(void) {
    static const uint8_t passphrase[] = "password";
    static const uint8_t ssid[] = "IEEE";
    static const char expected[] =
        "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e";
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES];
    assert(nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U,
        ssid, sizeof(ssid) - 1U, pmk));
    expect_hex(pmk, sizeof(pmk), expected);
}

static void test_binary_ssid_and_repeatability(void) {
    static const uint8_t passphrase[] = "correct horse battery staple";
    static const uint8_t ssid[] = {0x4EU, 0x00U, 0x58U, 0x55U, 0x53U};
    uint8_t first[NEXUS_WPA2_PMK_BYTES];
    uint8_t second[NEXUS_WPA2_PMK_BYTES];
    assert(nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, ssid, sizeof(ssid), first));
    assert(nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, ssid, sizeof(ssid), second));
    for (size_t i = 0U; i < sizeof(first); ++i)
        assert(first[i] == second[i]);
}

static void test_invalid_lengths_are_rejected_and_output_cleared(void) {
    static const uint8_t too_short[] = "short";
    static const uint8_t passphrase[] = "validpassphrase";
    static const uint8_t ssid[] = "Nexus";
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES];

    for (size_t i = 0U; i < sizeof(pmk); ++i) pmk[i] = 0xA5U;
    assert(!nexus_wpa2_psk_derive_pmk(
        too_short, sizeof(too_short) - 1U,
        ssid, sizeof(ssid) - 1U, pmk));
    for (size_t i = 0U; i < sizeof(pmk); ++i) assert(pmk[i] == 0U);

    assert(!nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, ssid, 0U, pmk));
    assert(!nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, ssid, 33U, pmk));
    assert(!nexus_wpa2_psk_derive_pmk(
        NULL, sizeof(passphrase) - 1U, ssid, sizeof(ssid) - 1U, pmk));
    assert(!nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, NULL, sizeof(ssid) - 1U, pmk));
    assert(!nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase) - 1U, ssid, sizeof(ssid) - 1U, NULL));
}

static void test_passphrase_upper_limit(void) {
    uint8_t passphrase[NEXUS_WPA2_MAX_PASSPHRASE_BYTES];
    uint8_t ssid[NEXUS_WPA2_MAX_SSID_BYTES];
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES];
    for (size_t i = 0U; i < sizeof(passphrase); ++i) passphrase[i] = 'p';
    for (size_t i = 0U; i < sizeof(ssid); ++i) ssid[i] = (uint8_t)i;
    assert(nexus_wpa2_psk_derive_pmk(
        passphrase, sizeof(passphrase), ssid, sizeof(ssid), pmk));
}

int main(void) {
    test_wpa2_pmk_known_vector();
    test_binary_ssid_and_repeatability();
    test_invalid_lengths_are_rejected_and_output_cleared();
    test_passphrase_upper_limit();
    return 0;
}
