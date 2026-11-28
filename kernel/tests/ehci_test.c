#include "ehci.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_capability_register_decode(void) {
    struct nexus_ehci_capabilities caps = {0};
    const uint32_t hcsparams = 8U | (1U << 4);
    const uint32_t hccparams = (1U << 0) | (1U << 1) | (0x50U << 8);
    assert(nexus_ehci_decode_capabilities(
        0x20U, 0x0100U, hcsparams, hccparams, &caps));
    assert(caps.capability_length == 0x20U);
    assert(caps.interface_version == 0x0100U);
    assert(caps.port_count == 8U);
    assert(caps.port_power_control);
    assert(caps.programmable_frame_list);
    assert(caps.has_64bit_addressing);
    assert(caps.has_legacy_support);
    assert(caps.legacy_support_offset == 0x50U);
    assert(caps.operational_base_offset == 0x20U);

    assert(!nexus_ehci_decode_capabilities(
        0U, 0x0100U, hcsparams, hccparams, &caps));
    assert(!nexus_ehci_decode_capabilities(
        0x10U, 0U, hcsparams, hccparams, &caps));
    assert(!nexus_ehci_decode_capabilities(
        0x20U, 0x0100U, 0U, hccparams, &caps));
    assert(!nexus_ehci_decode_capabilities(
        0x20U, 0x0100U, hcsparams, (0x3FU << 8), &caps));
}

static void test_legacy_capability_pointer_bounds(void) {
    uint8_t offset = 0xAAU;
    assert(nexus_ehci_validate_legacy_capability_offset(0U, &offset));
    assert(offset == 0U);
    assert(nexus_ehci_validate_legacy_capability_offset(0x40U, &offset));
    assert(offset == 0x40U);
    assert(nexus_ehci_validate_legacy_capability_offset(0xFCU, &offset));
    assert(offset == 0xFCU);
    assert(!nexus_ehci_validate_legacy_capability_offset(0x3CU, &offset));
    assert(!nexus_ehci_validate_legacy_capability_offset(0x41U, &offset));
    assert(!nexus_ehci_validate_legacy_capability_offset(0xFEU, &offset));
    assert(!nexus_ehci_validate_legacy_capability_offset(0x40U, NULL));
}

static void test_port_status_decode(void) {
    struct nexus_ehci_port_status status = {0};
    const uint32_t portsc = (1U << 0) | (1U << 2) | (1U << 7) |
        (1U << 12) | (1U << 10) | (1U << 1) | (1U << 5);
    assert(nexus_ehci_decode_port_status(portsc, &status));
    assert(status.connected && status.enabled && status.suspended);
    assert(status.powered && status.over_current);
    assert(status.line_status == 1U);
    assert(status.speed_hint == NEXUS_EHCI_SPEED_LOW);
    assert(status.change_bits == ((1U << 1) | (1U << 5)));
    assert(!nexus_ehci_decode_port_status(UINT32_MAX, &status));
}

static void test_qtd_builder(void) {
    struct nexus_ehci_qtd qtd = {0};
    const uint32_t pages[] = {
        0x00100000U,0x00101000U,0x00102000U,0x00103000U,0x00104000U
    };
    assert(nexus_ehci_build_qtd(&qtd, 0x00200000U,
        0x00200020U, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_IN, 9000U, true, true, pages, 128U));
    assert(qtd.next_qtd == 0x00200020U);
    assert(qtd.alternate_next_qtd == NEXUS_EHCI_QTD_TERMINATE);
    assert((qtd.token & (1U << 7)) != 0U);
    assert(((qtd.token >> 8) & 3U) == NEXUS_EHCI_PID_IN);
    assert(((qtd.token >> 10) & 3U) == 3U);
    assert(((qtd.token >> 16) & 0x7FFFU) == 9000U);
    assert((qtd.token & (1U << 31)) != 0U);
    assert((qtd.token & (1U << 15)) != 0U);
    assert(qtd.buffer[0] == 0x00100080U);
    assert(qtd.buffer[1] == 0x00101000U);
    assert(qtd.buffer[2] == 0x00102000U);
    assert(qtd.buffer[3] == 0U);

    assert(!nexus_ehci_build_qtd(&qtd, 0x00200004U,
        NEXUS_EHCI_QTD_TERMINATE, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_IN, 1U, false, false, pages, 0U));
    assert(!nexus_ehci_build_qtd(&qtd, 0x00200000U,
        NEXUS_EHCI_QTD_TERMINATE, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_IN, 0x8000U, false, false, pages, 0U));
    assert(!nexus_ehci_build_qtd(&qtd, 0x00200000U,
        NEXUS_EHCI_QTD_TERMINATE, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_SETUP, 32U, false, false, pages, 4096U));
    assert(!nexus_ehci_build_qtd(&qtd, 0x00200000U,
        NEXUS_EHCI_QTD_TERMINATE, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_IN, 100U, false, false,
        (const uint32_t[]){0x00100001U,0U,0U,0U,0U}, 0U));

    /* Status stages have no data buffer and use a zero-byte qTD. */
    assert(nexus_ehci_build_qtd(&qtd, 0x00200100U,
        NEXUS_EHCI_QTD_TERMINATE, NEXUS_EHCI_QTD_TERMINATE,
        NEXUS_EHCI_PID_IN, 0U, true, true, NULL, 0U));
    assert((qtd.token & (0x7FFFU << 16)) == 0U);
    assert(qtd.buffer[0] == 0U && qtd.buffer[4] == 0U);
}

static void test_qh_builder(void) {
    struct nexus_ehci_qh qh = {0};
    assert(nexus_ehci_build_qh(&qh, 0x00300000U, 0x00300040U,
        5U, 0U, NEXUS_EHCI_SPEED_HIGH, 64U, true, false, 4U,
        0x00301000U));
    assert(qh.horizontal_link == (0x00300040U | NEXUS_EHCI_LINK_QH));
    assert((qh.endpoint_characteristics & 0x7FU) == 5U);
    assert(((qh.endpoint_characteristics >> 8) & 0x0FU) == 0U);
    assert(((qh.endpoint_characteristics >> 12) & 3U) ==
           NEXUS_EHCI_SPEED_HIGH);
    assert(((qh.endpoint_characteristics >> 16) & 0x7FFU) == 64U);
    assert((qh.endpoint_characteristics & (1U << 27)) == 0U);
    assert(qh.overlay_next_qtd == 0x00301000U);
    assert(qh.overlay_alternate_next_qtd == NEXUS_EHCI_QTD_TERMINATE);

    assert(nexus_ehci_build_qh(&qh, 0x00300000U, 0x00300040U,
        5U, 0U, NEXUS_EHCI_SPEED_FULL, 8U, true, false, 4U,
        0x00301000U));
    assert((qh.endpoint_characteristics & (1U << 27)) != 0U);
    assert(!nexus_ehci_build_qh(&qh, 0x00300001U, 0x00300040U,
        5U, 0U, NEXUS_EHCI_SPEED_FULL, 8U, true, false, 4U,
        0x00301000U));
}

int main(void) {
    test_capability_register_decode();
    test_legacy_capability_pointer_bounds();
    test_port_status_decode();
    test_qtd_builder();
    test_qh_builder();
    puts("NEXUS EHCI CAPABILITY AND TRANSFER-DESCRIPTOR TESTS PASSED");
    return 0;
}
