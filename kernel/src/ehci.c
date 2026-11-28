#include "ehci.h"

#include <string.h>

#define EHCI_HCSPARAMS_N_PORTS_MASK 0x0000000FU
#define EHCI_HCSPARAMS_PPC          (1U << 4)
#define EHCI_HCCPARAMS_PFL          (1U << 1)
#define EHCI_HCCPARAMS_EECP_MASK    0x0000FF00U
#define EHCI_HCCPARAMS_64BIT        (1U << 0)

#define EHCI_PORTSC_CCS             (1U << 0)
#define EHCI_PORTSC_CSC             (1U << 1)
#define EHCI_PORTSC_PE              (1U << 2)
#define EHCI_PORTSC_PEC             (1U << 3)
#define EHCI_PORTSC_OCA             (1U << 4)
#define EHCI_PORTSC_OCC             (1U << 5)
#define EHCI_PORTSC_FPR             (1U << 6)
#define EHCI_PORTSC_SUSPEND         (1U << 7)
#define EHCI_PORTSC_RESET           (1U << 8)
#define EHCI_PORTSC_LINE_STATUS     (3U << 10)
#define EHCI_PORTSC_POWER           (1U << 12)
#define EHCI_PORTSC_OWNER           (1U << 13)
#define EHCI_PORTSC_CHANGE_MASK     (EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | \
                                     EHCI_PORTSC_OCC)

bool nexus_ehci_decode_capabilities(
    uint8_t capability_length, uint16_t interface_version,
    uint32_t structural_parameters, uint32_t capability_parameters,
    struct nexus_ehci_capabilities *capabilities) {
    if (capabilities == NULL || capability_length < 0x10U ||
        capability_length > 0x80U || interface_version < 0x0100U ||
        interface_version > 0x0200U) return false;

    const uint8_t ports =
        (uint8_t)(structural_parameters & EHCI_HCSPARAMS_N_PORTS_MASK);
    if (ports == 0U || ports > 15U) return false;

    const uint8_t legacy = (uint8_t)(
        (capability_parameters & EHCI_HCCPARAMS_EECP_MASK) >> 8);
    if (legacy != 0U &&
        (legacy < 0x40U || legacy > 0xFCU || (legacy & 0x03U) != 0U))
        return false;

    *capabilities = (struct nexus_ehci_capabilities){
        .capability_length = capability_length,
        .interface_version = interface_version,
        .port_count = ports,
        .port_power_control =
            (structural_parameters & EHCI_HCSPARAMS_PPC) != 0U,
        .programmable_frame_list =
            (capability_parameters & EHCI_HCCPARAMS_PFL) != 0U,
        .has_64bit_addressing =
            (capability_parameters & EHCI_HCCPARAMS_64BIT) != 0U,
        .has_legacy_support = legacy != 0U,
        .legacy_support_offset = legacy,
        .operational_base_offset = capability_length
    };
    return true;
}

bool nexus_ehci_validate_legacy_capability_offset(
    uint8_t extended_capability_pointer, uint8_t *offset) {
    if (offset == NULL) return false;
    if (extended_capability_pointer == 0U) {
        *offset = 0U;
        return true;
    }
    if (extended_capability_pointer < 0x40U ||
        extended_capability_pointer > 0xFCU ||
        (extended_capability_pointer & 0x03U) != 0U) return false;
    *offset = extended_capability_pointer;
    return true;
}

bool nexus_ehci_decode_port_status(
    uint32_t portsc, struct nexus_ehci_port_status *status) {
    if (status == NULL || portsc == UINT32_MAX) return false;
    *status = (struct nexus_ehci_port_status){
        .connected = (portsc & EHCI_PORTSC_CCS) != 0U,
        .enabled = (portsc & EHCI_PORTSC_PE) != 0U,
        .suspended = (portsc & EHCI_PORTSC_SUSPEND) != 0U,
        .over_current = (portsc & EHCI_PORTSC_OCA) != 0U,
        .resetting = (portsc & EHCI_PORTSC_RESET) != 0U,
        .powered = (portsc & EHCI_PORTSC_POWER) != 0U,
        .line_status = (uint8_t)((portsc & EHCI_PORTSC_LINE_STATUS) >> 10),
        .change_bits = (uint16_t)(portsc & EHCI_PORTSC_CHANGE_MASK)
    };
    /* USB 2 line state J/K is useful to the reset state machine for low-speed
     * detection, but it is not a reliable final speed result on every PHY. */
    status->speed_hint = status->line_status == 1U
        ? NEXUS_EHCI_SPEED_LOW : NEXUS_EHCI_SPEED_FULL;
    return true;
}

bool nexus_ehci_build_qtd(
    struct nexus_ehci_qtd *qtd, uint32_t qtd_physical,
    uint32_t next_qtd_physical, uint32_t alternate_qtd_physical,
    enum nexus_ehci_pid pid, uint16_t byte_count, bool data_toggle,
    bool interrupt_on_complete, const uint32_t page_addresses[5],
    uint16_t first_page_offset) {
    if (qtd == NULL ||
        (qtd_physical & 0x1FU) != 0U || qtd_physical == 0U ||
        (next_qtd_physical != NEXUS_EHCI_QTD_TERMINATE &&
         (next_qtd_physical == 0U ||
          (next_qtd_physical & 0x1FU) != 0U ||
          next_qtd_physical > UINT32_MAX - 0x1FU)) ||
        (alternate_qtd_physical != NEXUS_EHCI_QTD_TERMINATE &&
         (alternate_qtd_physical == 0U ||
          (alternate_qtd_physical & 0x1FU) != 0U ||
          alternate_qtd_physical > UINT32_MAX - 0x1FU)) ||
        pid > NEXUS_EHCI_PID_SETUP || byte_count > NEXUS_EHCI_QTD_MAX_BYTES ||
        first_page_offset >= NEXUS_EHCI_PAGE_SIZE) return false;

    uint32_t pointers[5] = {0U};
    if (byte_count != 0U) {
        if (page_addresses == NULL) return false;
        const uint32_t first_page = page_addresses[0];
        if (first_page == 0U || first_page > UINT32_MAX -
                (NEXUS_EHCI_PAGE_SIZE - 1U) ||
            (first_page & (NEXUS_EHCI_PAGE_SIZE - 1U)) != 0U)
            return false;

        const uint32_t first_capacity =
            NEXUS_EHCI_PAGE_SIZE - first_page_offset;
        const uint32_t page_capacity = first_capacity +
            4U * NEXUS_EHCI_PAGE_SIZE;
        if (byte_count > page_capacity) return false;

        pointers[0] = first_page + first_page_offset;
        uint32_t remaining = byte_count > first_capacity
            ? (uint32_t)byte_count - first_capacity : 0U;
        for (uint8_t i = 1U; i < 5U; ++i) {
            if (remaining == 0U) break;
            const uint32_t page = page_addresses[i];
            if (page == 0U || page > UINT32_MAX -
                    (NEXUS_EHCI_PAGE_SIZE - 1U) ||
                (page & (NEXUS_EHCI_PAGE_SIZE - 1U)) != 0U)
                return false;
            pointers[i] = page;
            remaining = remaining > NEXUS_EHCI_PAGE_SIZE
                ? remaining - NEXUS_EHCI_PAGE_SIZE : 0U;
        }
        if (remaining != 0U) return false;
    } else if (first_page_offset != 0U) {
        return false;
    }

    uint32_t token = (1U << 7) | ((uint32_t)pid << 8) |
                     (3U << 10) | ((uint32_t)byte_count << 16);
    if (data_toggle) token |= 1U << 31;
    if (interrupt_on_complete) token |= 1U << 15;

    *qtd = (struct nexus_ehci_qtd){
        .next_qtd = next_qtd_physical,
        .alternate_next_qtd = alternate_qtd_physical,
        .token = token,
        .buffer = { pointers[0], pointers[1], pointers[2],
                    pointers[3], pointers[4] }
    };
    return true;
}

bool nexus_ehci_build_qh(
    struct nexus_ehci_qh *qh, uint32_t qh_physical,
    uint32_t next_qh_physical, uint8_t device_address,
    uint8_t endpoint_number, enum nexus_ehci_speed speed,
    uint16_t maximum_packet, bool control_endpoint,
    bool data_toggle_control, uint8_t nak_reload,
    uint32_t first_qtd_physical) {
    if (qh == NULL || qh_physical == 0U || (qh_physical & 0x1FU) != 0U ||
        qh_physical > UINT32_MAX - 0x3FU ||
        next_qh_physical == 0U ||
        (next_qh_physical & 0x1FU) != 0U ||
        next_qh_physical > UINT32_MAX - 0x1FU ||
        device_address > 127U || endpoint_number > 15U ||
        speed > NEXUS_EHCI_SPEED_HIGH || maximum_packet == 0U ||
        maximum_packet > 1024U || nak_reload > 15U ||
        first_qtd_physical == 0U ||
        (first_qtd_physical & 0x1FU) != 0U ||
        first_qtd_physical > UINT32_MAX - 0x1FU)
        return false;

    uint32_t endpoint_characteristics =
        (uint32_t)device_address |
        ((uint32_t)endpoint_number << 8) |
        ((uint32_t)speed << 12) |
        ((uint32_t)maximum_packet << 16);
    if (data_toggle_control) endpoint_characteristics |= 1U << 14;
    if (control_endpoint && speed != NEXUS_EHCI_SPEED_HIGH)
        endpoint_characteristics |= 1U << 27;

    const uint32_t endpoint_capabilities =
        (uint32_t)nak_reload << 28;

    *qh = (struct nexus_ehci_qh){
        .horizontal_link = next_qh_physical | NEXUS_EHCI_LINK_QH,
        .endpoint_characteristics = endpoint_characteristics,
        .endpoint_capabilities = endpoint_capabilities,
        .current_qtd = 0U,
        .overlay_next_qtd = first_qtd_physical,
        .overlay_alternate_next_qtd = NEXUS_EHCI_QTD_TERMINATE,
        .overlay_token = 0U,
        .overlay_buffer = {0U,0U,0U,0U,0U},
        .reserved = {0U,0U,0U}
    };
    return true;
}
