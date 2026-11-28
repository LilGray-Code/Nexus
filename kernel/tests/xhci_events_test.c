#include "xhci_events.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_fifo_order_and_wraparound(void) {
    struct nexus_xhci_event_queue queue = {0};
    struct nexus_xhci_event input = {0}, output = {0};

    for (uint32_t i = 0U; i < 50U; ++i) {
        input.parameter = 0x100000000ULL + i;
        input.status = 0x20000000U + i;
        input.control = 0x30000000U + i;
        assert(nexus_xhci_event_queue_push(&queue, &input));
        assert(nexus_xhci_event_queue_pop(&queue, &output));
        assert(output.parameter == input.parameter);
        assert(output.status == input.status);
        assert(output.control == input.control);
    }

    for (uint32_t i = 0U; i < 63U; ++i) {
        input.parameter = i;
        assert(nexus_xhci_event_queue_push(&queue, &input));
    }
    assert(!nexus_xhci_event_queue_push(&queue, &input));
    for (uint32_t i = 0U; i < 63U; ++i) {
        assert(nexus_xhci_event_queue_pop(&queue, &output));
        assert(output.parameter == i);
    }
    assert(!nexus_xhci_event_queue_pop(&queue, &output));
}

static void test_rejects_null_arguments(void) {
    struct nexus_xhci_event_queue queue = {0};
    struct nexus_xhci_event event = {0};
    assert(!nexus_xhci_event_queue_push(NULL, &event));
    assert(!nexus_xhci_event_queue_push(&queue, NULL));
    assert(!nexus_xhci_event_queue_pop(NULL, &event));
    assert(!nexus_xhci_event_queue_pop(&queue, NULL));
}

int main(void) {
    test_fifo_order_and_wraparound();
    test_rejects_null_arguments();
    puts("NEXUS xHCI DEFERRED EVENT FIFO TESTS PASSED");
    return 0;
}
