#ifndef NEXUS_XHCI_OWNERSHIP_H
#define NEXUS_XHCI_OWNERSHIP_H

#include <stdint.h>

/* HCCPARAMS1.xECP is a dword offset from the xHCI BAR. */
static inline uint32_t nexus_xhci_extended_cap_start(uint32_t hccparams1) {
    return ((hccparams1 >> 16) & 0xFFFFU) * 4U;
}

/* Extended-capability NEXT is a relative dword offset from this capability. */
static inline uint32_t nexus_xhci_extended_cap_next(
    uint32_t current_offset, uint32_t header) {
    const uint32_t next_dwords = (header >> 8) & 0xFFU;
    if (next_dwords == 0U) return 0U;
    if (next_dwords > (UINT32_MAX - current_offset) / 4U) return 0U;
    return current_offset + next_dwords * 4U;
}

static inline uint8_t nexus_xhci_legacy_capability_id(uint32_t header) {
    return (uint8_t)(header & 0xFFU);
}

static inline uint8_t nexus_xhci_request_os_ownership(uint8_t os_semaphore) {
    return (uint8_t)(os_semaphore | 0x01U);
}

static inline int nexus_xhci_bios_ownership_released(uint8_t bios_semaphore) {
    return (bios_semaphore & 0x01U) == 0U;
}

/*
 * USBLEGCTLSTS: bits 0,4,13,14,15 enable SMIs; bits 29..31 are RW1C
 * event latches. Preserve unrelated fields while disabling SMI sources and
 * clearing pending latches. Read-only status bits naturally ignore writes.
 */
static inline uint32_t nexus_xhci_disable_legacy_smi(uint32_t value) {
    const uint32_t smi_enable_mask =
        (1U << 0) | (1U << 4) | (1U << 13) | (1U << 14) | (1U << 15);
    const uint32_t smi_event_w1c_mask =
        (1U << 29) | (1U << 30) | (1U << 31);
    return (value & ~smi_enable_mask) | smi_event_w1c_mask;
}

#endif
