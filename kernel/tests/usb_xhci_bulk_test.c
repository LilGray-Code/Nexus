#include "usb_xhci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_bulk_in_context(void) {
    uint32_t context[5] = {0};
    uint8_t endpoint_id = 0;
    assert(nexus_xhci_bulk_endpoint_context(
        0x81, 512, 0x12345000ULL, context, &endpoint_id));
    assert(endpoint_id == 3);
    assert(context[0] == 0);
    assert(((context[1] >> 3) & 7U) == 6U);
    assert((context[1] >> 16) == 512U);
    assert(context[2] == 0x12345001U);
    assert(context[3] == 0);
    assert(context[4] == 512U);
}

static void test_bulk_out_context(void) {
    uint32_t context[5] = {0};
    uint8_t endpoint_id = 0;
    assert(nexus_xhci_bulk_endpoint_context(
        0x02, 64, 0xABCDEF000ULL, context, &endpoint_id));
    assert(endpoint_id == 4);
    assert(((context[1] >> 3) & 7U) == 2U);
    assert(context[2] == 0xCDEF0001U);
    assert(context[3] == 0xAU);
}

static void test_rejects_invalid_endpoint_or_ring(void) {
    uint32_t context[5];
    uint8_t endpoint_id;
    assert(!nexus_xhci_bulk_endpoint_context(
        0x00, 64, 0x1000, context, &endpoint_id));
    assert(!nexus_xhci_bulk_endpoint_context(
        0x91, 64, 0x1000, context, &endpoint_id));
    assert(!nexus_xhci_bulk_endpoint_context(
        0x81, 128, 0x1000, context, &endpoint_id));
    assert(!nexus_xhci_bulk_endpoint_context(
        0x81, 512, 0x1008, context, &endpoint_id));
    assert(!nexus_xhci_bulk_endpoint_context(
        0x81, 512, 0x1000, NULL, &endpoint_id));
}

static void test_bulk_in_transfer_trb(void) {
    struct nexus_xhci_bulk_trb trb;
    memset(&trb, 0, sizeof(trb));
    assert(nexus_xhci_bulk_transfer_trb(0x81, 0x200000, 1500, &trb));
    assert(trb.parameter == 0x200000);
    assert(trb.status == 1500);
    assert(((trb.control >> 10) & 0x3FU) == 1U);
    assert((trb.control & (1U << 5)) != 0U);
    assert((trb.control & (1U << 2)) != 0U);
    assert((trb.control & 1U) == 0U); /* Ring owner supplies cycle bit. */
}

static void test_bulk_out_transfer_trb(void) {
    struct nexus_xhci_bulk_trb trb;
    assert(nexus_xhci_bulk_transfer_trb(0x02, 0x300000, 64, &trb));
    assert((trb.control & (1U << 2)) == 0U);
    assert((trb.control & (1U << 5)) != 0U);
}

static void test_rejects_invalid_transfer(void) {
    struct nexus_xhci_bulk_trb trb;
    assert(!nexus_xhci_bulk_transfer_trb(0x01, 0x200000, 64, &trb));
    assert(!nexus_xhci_bulk_transfer_trb(0x81, 0, 64, &trb));
    assert(!nexus_xhci_bulk_transfer_trb(0x81, 0x200000, 0, &trb));
    assert(!nexus_xhci_bulk_transfer_trb(0x81, 0x200000, 0x20000, &trb));
    assert(!nexus_xhci_bulk_transfer_trb(
        0x81, UINT64_MAX - 10U, 64, &trb));
    assert(!nexus_xhci_bulk_transfer_trb(0x81, 0x200000, 64, NULL));
}

int main(void) {
    test_bulk_in_context();
    test_bulk_out_context();
    test_rejects_invalid_endpoint_or_ring();
    test_bulk_in_transfer_trb();
    test_bulk_out_transfer_trb();
    test_rejects_invalid_transfer();
    puts("NEXUS xHCI BULK ENDPOINT AND TRANSFER BUILDER TESTS PASSED");
    return 0;
}
