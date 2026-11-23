#include "usb_xhci.h"

#define XHCI_TRB_TYPE_NORMAL (1U << 10)
#define XHCI_TRB_IOC         (1U << 5)
#define XHCI_TRB_ISP         (1U << 2)
#define XHCI_TRB_DIR_IN      0x80U
#define XHCI_MAX_TRANSFER    0x1FFFFU

static bool usb_valid_bulk_packet(uint16_t packet) {
    return packet == 8U || packet == 16U || packet == 32U ||
           packet == 64U || packet == 512U || packet == 1024U;
}

bool nexus_xhci_bulk_endpoint_context(
    uint8_t endpoint_address, uint16_t max_packet, uint64_t ring_physical,
    uint32_t context[5], uint8_t *endpoint_id) {
    const uint8_t number = endpoint_address & 0x0FU;
    if (context == 0 || endpoint_id == 0 || number == 0U ||
        number > 15U || (endpoint_address & 0x70U) != 0U ||
        !usb_valid_bulk_packet(max_packet) ||
        ring_physical == 0U || (ring_physical & 0x0FU) != 0U) return false;

    const bool input = (endpoint_address & XHCI_TRB_DIR_IN) != 0U;
    const uint8_t dci = (uint8_t)(number * 2U + (input ? 1U : 0U));
    const uint8_t endpoint_type = input ? 6U : 2U; /* Bulk IN / Bulk OUT. */

    context[0] = 0U; /* Bulk endpoints do not use an interval. */
    context[1] = (3U << 1) | ((uint32_t)endpoint_type << 3) |
                 ((uint32_t)max_packet << 16);
    context[2] = (uint32_t)ring_physical | 1U; /* DCS=1 */
    context[3] = (uint32_t)(ring_physical >> 32);
    context[4] = max_packet; /* Average TRB length; no ESIT payload. */
    *endpoint_id = dci;
    return true;
}

bool nexus_xhci_bulk_transfer_trb(
    uint8_t endpoint_address, uint64_t buffer_physical, uint32_t length,
    struct nexus_xhci_bulk_trb *trb) {
    if (trb == 0 || (endpoint_address & 0x0FU) == 0U ||
        (endpoint_address & 0x70U) != 0U ||
        buffer_physical == 0U || length == 0U ||
        length > XHCI_MAX_TRANSFER ||
        buffer_physical > UINT64_MAX - (uint64_t)length) return false;

    trb->parameter = buffer_physical;
    trb->status = length; /* TD Size is zero for a single-TRB transfer. */
    trb->control = XHCI_TRB_TYPE_NORMAL | XHCI_TRB_IOC |
        (((endpoint_address & XHCI_TRB_DIR_IN) != 0U) ? XHCI_TRB_ISP : 0U);
    return true;
}
