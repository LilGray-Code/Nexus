#ifndef NEXUS_VIRTIO_QUEUE_HELPERS_H
#define NEXUS_VIRTIO_QUEUE_HELPERS_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_VIRTIO_QUEUE_PAGE_BYTES 4096U
#define NEXUS_VIRTIO_QUEUE_DESCRIPTOR_BYTES 16U
#define NEXUS_VIRTIO_QUEUE_AVAILABLE_HEADER_BYTES 6U
#define NEXUS_VIRTIO_QUEUE_USED_HEADER_BYTES 6U

struct nexus_virtio_queue_layout {
    uint64_t descriptor_bytes;
    uint64_t available_offset;
    uint64_t used_offset;
    uint64_t total_bytes;
    uint64_t page_count;
};

/* Compute a split virtqueue layout with the required 16/2/4-byte alignment. */
static inline bool nexus_virtio_queue_layout_compute(
    uint16_t queue_size, struct nexus_virtio_queue_layout *layout) {
    if (queue_size < 2U || layout == 0) return false;

    const uint64_t descriptor_bytes =
        (uint64_t)queue_size * NEXUS_VIRTIO_QUEUE_DESCRIPTOR_BYTES;
    const uint64_t available_offset =
        (descriptor_bytes + 1U) & ~1ULL;
    const uint64_t available_bytes =
        NEXUS_VIRTIO_QUEUE_AVAILABLE_HEADER_BYTES +
        (uint64_t)queue_size * 2U;
    if (available_offset > UINT64_MAX - available_bytes) return false;
    const uint64_t available_end = available_offset + available_bytes;
    if (available_end > UINT64_MAX - 3U) return false;
    const uint64_t used_offset = (available_end + 3U) & ~3ULL;
    const uint64_t used_bytes =
        NEXUS_VIRTIO_QUEUE_USED_HEADER_BYTES +
        (uint64_t)queue_size * 8U;
    if (used_offset > UINT64_MAX - used_bytes) return false;
    const uint64_t total_bytes = used_offset + used_bytes;
    if (total_bytes > UINT64_MAX -
                      (NEXUS_VIRTIO_QUEUE_PAGE_BYTES - 1U)) return false;

    *layout = (struct nexus_virtio_queue_layout){
        .descriptor_bytes = descriptor_bytes,
        .available_offset = available_offset,
        .used_offset = used_offset,
        .total_bytes = total_bytes,
        .page_count = (total_bytes + NEXUS_VIRTIO_QUEUE_PAGE_BYTES - 1U) /
                      NEXUS_VIRTIO_QUEUE_PAGE_BYTES
    };
    return layout->page_count != 0U &&
           (layout->available_offset & 1U) == 0U &&
           (layout->used_offset & 3U) == 0U;
}

/*
 * VirtIO PCI allows notify_off_multiplier == 0. Keep all arithmetic bounded
 * and ensure the 16-bit doorbell access fits within the advertised capability
 * and one mapped MMIO page.
 */
static inline bool nexus_virtio_notify_address(
    uint64_t bar_base, uint32_t capability_offset,
    uint32_t capability_length, uint16_t queue_notify_offset,
    uint32_t notify_multiplier, uint64_t *physical_address) {
    if (physical_address == 0 || capability_length < 2U) return false;
    const uint64_t notify_relative =
        (uint64_t)queue_notify_offset * notify_multiplier;
    if (notify_relative > capability_length ||
        (uint64_t)capability_length - notify_relative < 2U) return false;
    if (bar_base > UINT64_MAX - capability_offset) return false;
    const uint64_t capability_base = bar_base + capability_offset;
    if (capability_base > UINT64_MAX - notify_relative) return false;
    const uint64_t physical = capability_base + notify_relative;
    if ((physical & 1U) != 0U || (physical & 0xFFFU) > 0xFFEU)
        return false;
    *physical_address = physical;
    return true;
}

#endif
