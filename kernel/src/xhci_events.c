#include "xhci_events.h"

#include <stddef.h>

bool nexus_xhci_event_queue_push(
    struct nexus_xhci_event_queue *queue,
    const struct nexus_xhci_event *event) {
    if (queue == NULL || event == NULL) return false;
    const uint8_t next = (uint8_t)(
        (queue->tail + 1U) & (NEXUS_XHCI_DEFERRED_EVENT_CAPACITY - 1U));
    if (next == queue->head) return false;
    queue->events[queue->tail] = *event;
    queue->tail = next;
    return true;
}

bool nexus_xhci_event_queue_pop(
    struct nexus_xhci_event_queue *queue,
    struct nexus_xhci_event *event) {
    if (queue == NULL || event == NULL || queue->head == queue->tail)
        return false;
    *event = queue->events[queue->head];
    queue->head = (uint8_t)(
        (queue->head + 1U) & (NEXUS_XHCI_DEFERRED_EVENT_CAPACITY - 1U));
    return true;
}
