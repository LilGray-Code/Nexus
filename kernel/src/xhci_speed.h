#ifndef NEXUS_XHCI_SPEED_H
#define NEXUS_XHCI_SPEED_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Conventional xHCI PORTSC speed IDs:
 * 1 low speed, 2 full speed, 3 high speed, 4 SuperSpeed, 5 SuperSpeedPlus.
 * Other IDs are reserved or controller-specific and are not treated as
 * standard USB link speeds by this driver.
 */
static inline bool nexus_xhci_speed_supported(uint8_t speed_id) {
    return speed_id >= 1U && speed_id <= 5U;
}

static inline bool nexus_xhci_speed_is_superspeed(uint8_t speed_id) {
    return speed_id == 4U || speed_id == 5U;
}

static inline bool nexus_xhci_speed_requires_warm_reset(uint8_t speed_id) {
    return nexus_xhci_speed_is_superspeed(speed_id);
}

static inline uint16_t nexus_xhci_default_ep0_packet_size(uint8_t speed_id) {
    if (speed_id == 3U) return 64U;
    if (nexus_xhci_speed_is_superspeed(speed_id)) return 512U;
    return 8U;
}

static inline uint16_t nexus_xhci_descriptor_ep0_packet_size(
    uint8_t speed_id, uint8_t descriptor_value) {
    /* USB 3.x encodes EP0's 512-byte packet size as exponent value 9. */
    if (nexus_xhci_speed_is_superspeed(speed_id)) return 512U;
    return descriptor_value == 0U
        ? nexus_xhci_default_ep0_packet_size(speed_id)
        : descriptor_value;
}

#endif
