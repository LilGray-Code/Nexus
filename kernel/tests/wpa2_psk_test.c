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


static void test_wpa2_ptk_known_vector_and_ordering(void) {
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES];
    uint8_t anonce[32], snonce[32];
    uint8_t authenticator[6] = {0x00U,0x11U,0x22U,0x33U,0x44U,0x55U};
    uint8_t supplicant[6] = {0xAAU,0xBBU,0xCCU,0xDDU,0xEEU,0xFFU};
    uint8_t ptk[NEXUS_WPA2_PTK_BYTES];
    uint8_t reversed[NEXUS_WPA2_PTK_BYTES];
    static const char expected[] =
        "a764e39db7c83bd6a55af0f1f3973d5110c6e21b8fc8dd9a9e527527f5052ca1"
        "b4fd3ed087334fe12bfb8e3071bac49fcde9d6c565f847504a381f1c8ccd216c";

    for (size_t i = 0U; i < sizeof(pmk); ++i) pmk[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(anonce); ++i) {
        anonce[i] = (uint8_t)i;
        snonce[i] = (uint8_t)(i + 32U);
    }
    assert(nexus_wpa2_derive_ptk(pmk, authenticator, supplicant,
                                  anonce, snonce, ptk));
    expect_hex(ptk, sizeof(ptk), expected);

    /* Reversing peer order must not change the pairwise expansion. */
    assert(nexus_wpa2_derive_ptk(pmk, supplicant, authenticator,
                                  snonce, anonce, reversed));
    for (size_t i = 0U; i < sizeof(ptk); ++i)
        assert(ptk[i] == reversed[i]);

    snonce[0] ^= 1U;
    assert(nexus_wpa2_derive_ptk(pmk, authenticator, supplicant,
                                  anonce, snonce, reversed));
    bool same = true;
    for (size_t i = 0U; i < sizeof(ptk); ++i)
        if (ptk[i] != reversed[i]) same = false;
    assert(!same);
}

static void test_eapol_mic_compute_and_verify(void) {
    uint8_t kck[NEXUS_WPA2_KCK_BYTES];
    uint8_t frame[64];
    uint8_t mic[NEXUS_WPA2_EAPOL_MIC_BYTES];
    static const char expected[] = "40a45db64dcf2521045982b975858a2c";
    const size_t mic_offset = 20U;
    for (size_t i = 0U; i < sizeof(kck); ++i) kck[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    for (size_t i = 0U; i < sizeof(mic); ++i) frame[mic_offset + i] = 0xA5U;

    assert(nexus_wpa2_eapol_mic(kck, frame, sizeof(frame), mic_offset, mic));
    expect_hex(mic, sizeof(mic), expected);
    /* Calculation must not mutate the caller's frame or its MIC field. */
    for (size_t i = 0U; i < sizeof(mic); ++i)
        assert(frame[mic_offset + i] == 0xA5U);
    for (size_t i = 0U; i < sizeof(mic); ++i)
        frame[mic_offset + i] = mic[i];
    assert(nexus_wpa2_eapol_mic_verify(kck, frame, sizeof(frame), mic_offset));
    frame[7] ^= 0x01U;
    assert(!nexus_wpa2_eapol_mic_verify(kck, frame, sizeof(frame), mic_offset));
    frame[7] ^= 0x01U;
    frame[mic_offset + 15U] ^= 0x80U;
    assert(!nexus_wpa2_eapol_mic_verify(kck, frame, sizeof(frame), mic_offset));
}

static void test_ptk_and_mic_invalid_inputs(void) {
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES] = {0U};
    uint8_t mac[6] = {0U};
    uint8_t nonce[32] = {0U};
    uint8_t ptk[NEXUS_WPA2_PTK_BYTES];
    uint8_t kck[NEXUS_WPA2_KCK_BYTES] = {0U};
    uint8_t frame[32] = {0U};
    uint8_t mic[NEXUS_WPA2_EAPOL_MIC_BYTES];

    for (size_t i = 0U; i < sizeof(ptk); ++i) ptk[i] = 0xA5U;
    assert(!nexus_wpa2_derive_ptk(NULL, mac, mac, nonce, nonce, ptk));
    for (size_t i = 0U; i < sizeof(ptk); ++i) assert(ptk[i] == 0U);
    assert(!nexus_wpa2_derive_ptk(pmk, NULL, mac, nonce, nonce, ptk));
    assert(!nexus_wpa2_derive_ptk(pmk, mac, NULL, nonce, nonce, ptk));
    assert(!nexus_wpa2_derive_ptk(pmk, mac, mac, NULL, nonce, ptk));
    assert(!nexus_wpa2_derive_ptk(pmk, mac, mac, nonce, NULL, ptk));

    for (size_t i = 0U; i < sizeof(mic); ++i) mic[i] = 0xA5U;
    assert(!nexus_wpa2_eapol_mic(kck, frame, sizeof(frame), 20U, mic));
    for (size_t i = 0U; i < sizeof(mic); ++i) assert(mic[i] == 0U);
    assert(!nexus_wpa2_eapol_mic(kck, frame, sizeof(frame), 17U, mic));
    assert(!nexus_wpa2_eapol_mic(kck, frame,
                                  NEXUS_WPA2_MAX_EAPOL_BYTES + 1U, 0U, mic));
    assert(!nexus_wpa2_eapol_mic_verify(kck, frame, sizeof(frame), 20U));
}

int main(void) {
    test_wpa2_pmk_known_vector();
    test_binary_ssid_and_repeatability();
    test_invalid_lengths_are_rejected_and_output_cleared();
    test_passphrase_upper_limit();
    test_wpa2_ptk_known_vector_and_ordering();
    test_eapol_mic_compute_and_verify();
    test_ptk_and_mic_invalid_inputs();
    return 0;
}
