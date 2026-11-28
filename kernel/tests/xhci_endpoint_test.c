#include "xhci_endpoint.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_bulk_endpoint_direction_and_dci(void) {
    struct nexus_xhci_endpoint_context context = {0};
    assert(nexus_xhci_build_endpoint_context(
        0x02U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 512U,
        0x00102000U, 512U, 1U, &context));
    assert(context.endpoint_id == 4U);
    assert(context.context_entries == 4U);
    assert(((context.dword1 >> 3) & 7U) == 2U);
    assert(((context.dword1 >> 16) & 0xFFFFU) == 512U);
    assert(context.dword2 == 0x00102001U);
    assert(context.dword3 == 0U);
    assert(context.dword4 == 512U);

    assert(nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 512U,
        0x00103000U, 512U, 4U, &context));
    assert(context.endpoint_id == 5U);
    assert(context.context_entries == 5U);
    assert(((context.dword1 >> 3) & 7U) == 6U);
}

static void test_interrupt_endpoint_context(void) {
    struct nexus_xhci_endpoint_context context = {0};
    assert(nexus_xhci_build_endpoint_context(
        0x81U, NEXUS_XHCI_ENDPOINT_INTERRUPT, 4U, 0U, 16U,
        0x000A0000U, 16U, 1U, &context));
    assert(context.endpoint_id == 3U);
    assert(context.context_entries == 3U);
    assert(((context.dword0 >> 16) & 0xFFU) == 4U);
    assert(((context.dword1 >> 3) & 7U) == 7U);
    assert(((context.dword1 >> 16) & 0xFFFFU) == 16U);
}

static void test_context_entries_never_shrink(void) {
    struct nexus_xhci_endpoint_context context = {0};
    assert(nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00200000U, 64U, 10U, &context));
    assert(context.endpoint_id == 5U);
    assert(context.context_entries == 10U);
}

static void test_rejects_invalid_endpoint_and_context_values(void) {
    struct nexus_xhci_endpoint_context context = {0};
    assert(!nexus_xhci_build_endpoint_context(
        0x00U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00100000U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x72U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00100000U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x02U, NEXUS_XHCI_ENDPOINT_BULK, 1U, 0U, 64U,
        0x00100000U, 64U, 1U, &context));
    /* xHCI Interval is an encoded exponent; zero is valid (one
     * service interval), including for high-speed interrupt endpoints. */
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_INTERRUPT, 16U, 0U, 64U,
        0x00100000U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 16U, 64U,
        0x00100000U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 1025U,
        0x00100000U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00100001U, 64U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00100000U, 0U, 1U, &context));
    assert(!nexus_xhci_build_endpoint_context(
        0x82U, NEXUS_XHCI_ENDPOINT_BULK, 0U, 0U, 64U,
        0x00100000U, 64U, 32U, &context));
}

int main(void) {
    test_bulk_endpoint_direction_and_dci();
    test_interrupt_endpoint_context();
    test_context_entries_never_shrink();
    test_rejects_invalid_endpoint_and_context_values();
    puts("NEXUS xHCI BULK/INTERRUPT ENDPOINT CONTEXT TESTS PASSED");
    return 0;
}
