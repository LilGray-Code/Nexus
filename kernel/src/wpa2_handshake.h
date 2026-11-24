#ifndef NEXUS_WPA2_HANDSHAKE_H
#define NEXUS_WPA2_HANDSHAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wpa2_psk.h"
#include "wpa2_eapol.h"

#define NEXUS_WPA2_TK_CCMP_BYTES 16U
#define NEXUS_WPA2_HANDSHAKE_KEY_DATA_MAX 256U
#define NEXUS_WPA2_HANDSHAKE_EAPOL_RESPONSE_MAX 99U

enum nexus_wpa2_supplicant_state {
    NEXUS_WPA2_SUPPLICANT_UNINITIALIZED = 0,
    NEXUS_WPA2_SUPPLICANT_WAIT_M1,
    NEXUS_WPA2_SUPPLICANT_WAIT_M3,
    NEXUS_WPA2_SUPPLICANT_COMPLETE
};

/*
 * Callbacks are invoked synchronously. The caller owns the radio and must
 * serialize access to this context. install_ccmp128_keys receives the
 * 16-byte pairwise TK, the 16-byte GTK, GTK KDE info, and the GTK RSC.
 */
struct nexus_wpa2_handshake_ops {
    bool (*send_eapol)(void *context, const uint8_t *frame, size_t length);
    bool (*install_ccmp128_keys)(
        void *context,
        const uint8_t pairwise_tk[NEXUS_WPA2_TK_CCMP_BYTES],
        const uint8_t group_tk[NEXUS_WPA2_GTK_CCMP_BYTES],
        uint8_t gtk_info,
        const uint8_t gtk_rsc[8]);
    void (*clear_ccmp128_keys)(void *context);
};

struct nexus_wpa2_supplicant {
    enum nexus_wpa2_supplicant_state state;
    uint8_t pmk[NEXUS_WPA2_PMK_BYTES];
    uint8_t ptk[NEXUS_WPA2_PTK_BYTES];
    uint8_t supplicant_mac[6];
    uint8_t authenticator_mac[6];
    uint8_t supplicant_nonce[32];
    uint8_t authenticator_nonce[32];
    uint64_t m1_replay_counter;
    uint8_t eapol_version;
    bool keys_installed;
    struct nexus_wpa2_handshake_ops ops;
    void *ops_context;
};

/*
 * Initialize a WPA2-Personal CCMP-128 supplicant handshake. The caller MUST
 * provide a fresh, unpredictable 32-byte SNonce obtained from a cryptographic
 * random-number source; this function intentionally has no weak fallback RNG.
 */
bool nexus_wpa2_supplicant_init(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t pmk[NEXUS_WPA2_PMK_BYTES],
    const uint8_t supplicant_mac[6],
    const uint8_t authenticator_mac[6],
    const uint8_t fresh_snonce[32],
    const struct nexus_wpa2_handshake_ops *ops,
    void *ops_context);

/*
 * Process one authenticator EAPOL-Key frame. Accepts a valid M1 while waiting
 * for M1, then a valid MIC-protected M3 with wrapped GTK data while waiting
 * for M3. Sends M2/M4 through send_eapol and installs keys through the supplied
 * callback. It rejects replayed, malformed, or unauthenticated messages.
 *
 * This is a bounded CCMP-128 WPA2-Personal supplicant path, not an 802.11
 * station driver. Scanning, association, radio transport, retransmission
 * timers, roaming, PMF, and hardware key programming remain platform work.
 */
bool nexus_wpa2_supplicant_handle_eapol(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t *frame, size_t frame_length);

/*
 * Initialize using a fresh nonce from the kernel hardware RNG. This helper
 * fails closed when a trustworthy CPU RDRAND source is unavailable or fails;
 * callers must not substitute timestamps or other predictable values.
 */
bool nexus_wpa2_supplicant_init_random(
    struct nexus_wpa2_supplicant *supplicant,
    const uint8_t pmk[NEXUS_WPA2_PMK_BYTES],
    const uint8_t supplicant_mac[6],
    const uint8_t authenticator_mac[6],
    const struct nexus_wpa2_handshake_ops *ops,
    void *ops_context);

/* Clear installed keys (when supported) and wipe all handshake secrets. */
void nexus_wpa2_supplicant_deinit(
    struct nexus_wpa2_supplicant *supplicant);

#endif
