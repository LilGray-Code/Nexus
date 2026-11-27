#ifndef NEXUS_XHCI_PORTS_H
#define NEXUS_XHCI_PORTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * xHCI HCSPARAMS1.MaxPorts is an eight-bit field. Keep the controller's
 * advertised root-port range separate from the bounded number of HID devices
 * Nexus can track concurrently.
 */
#define NEXUS_XHCI_MAX_ROOT_PORTS 255U

#define NEXUS_XHCI_PORT_CCS        (1U << 0)
#define NEXUS_XHCI_PORT_SPEED_MASK  (0x0FU << 10)
#define NEXUS_XHCI_PORT_CHANGE_MASK ((1U << 17) | (1U << 18) | (1U << 19) | \
                                    (1U << 20) | (1U << 21) | (1U << 22) | \
                                    (1U << 23))

static inline uint8_t nexus_xhci_root_port_count(uint32_t hcsparams1) {
    return (uint8_t)((hcsparams1 >> 24) & 0xFFU);
}

static inline bool nexus_xhci_port_register_offset(uint8_t capability_length,
                                                    uint16_t port_number,
                                                    uint64_t *offset) {
    if (offset == NULL || capability_length < 0x20U ||
        port_number == 0U ||
        port_number > NEXUS_XHCI_MAX_ROOT_PORTS) return false;

    *offset = (uint64_t)capability_length + 0x400U +
              ((uint64_t)(port_number - 1U) * 0x10U);
    return true;
}

static inline bool nexus_xhci_port_connected(uint32_t port_status) {
    return (port_status & NEXUS_XHCI_PORT_CCS) != 0U;
}

static inline uint8_t nexus_xhci_port_speed_id(uint32_t port_status) {
    return (uint8_t)((port_status & NEXUS_XHCI_PORT_SPEED_MASK) >> 10);
}

static inline uint32_t nexus_xhci_port_change_bits(uint32_t port_status) {
    return port_status & NEXUS_XHCI_PORT_CHANGE_MASK;
}

#endif
