#ifndef NEXUS_XHCI_EVENTS_H
#define NEXUS_XHCI_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_XHCI_DEFERRED_EVENT_CAPACITY 64U

struct nexus_xhci_event {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct nexus_xhci_event_queue {
    struct nexus_xhci_event events[NEXUS_XHCI_DEFERRED_EVENT_CAPACITY];
    uint8_t head;
    uint8_t tail;
};

/* Single-CPU bounded FIFO. One slot is reserved to distinguish full from
 * empty, so 63 hardware events can be deferred while a synchronous command
 * or endpoint-zero transfer waits for its own completion. */
bool nexus_xhci_event_queue_push(
    struct nexus_xhci_event_queue *queue,
    const struct nexus_xhci_event *event);
bool nexus_xhci_event_queue_pop(
    struct nexus_xhci_event_queue *queue,
    struct nexus_xhci_event *event);

#endif
