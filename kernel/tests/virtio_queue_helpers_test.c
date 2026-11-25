#include "virtio_queue_helpers.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_queue_layouts(void) {
    struct nexus_virtio_queue_layout layout = {0};
    assert(!nexus_virtio_queue_layout_compute(0U, &layout));
    assert(!nexus_virtio_queue_layout_compute(1U, &layout));

    assert(nexus_virtio_queue_layout_compute(8U, &layout));
    assert(layout.descriptor_bytes == 128U);
    assert(layout.available_offset == 128U);
    assert(layout.used_offset == 152U);
    assert(layout.total_bytes == 222U);
    assert(layout.page_count == 1U);

    assert(nexus_virtio_queue_layout_compute(256U, &layout));
    assert(layout.descriptor_bytes == 4096U);
    assert(layout.available_offset == 4096U);
    assert(layout.used_offset == 4616U);
    assert(layout.total_bytes == 6670U);
    assert(layout.page_count == 2U);

    for (uint32_t size = 2U; size <= 32768U; size <<= 1U) {
        assert(nexus_virtio_queue_layout_compute((uint16_t)size, &layout));
        assert((layout.available_offset & 1U) == 0U);
        assert((layout.used_offset & 3U) == 0U);
        assert(layout.used_offset >= layout.available_offset +
               6U + (uint64_t)size * 2U);
        assert(layout.total_bytes == layout.used_offset +
               6U + (uint64_t)size * 8U);
        assert(layout.page_count ==
               (layout.total_bytes + 4095U) / 4096U);
    }
}

static void test_notify_address(void) {
    uint64_t physical = 0U;
    /* A zero multiplier is valid and points every queue at BAR+offset. */
    assert(nexus_virtio_notify_address(
        0x10000000U, 0x2000U, 0x100U, 7U, 0U, &physical));
    assert(physical == 0x10002000U);

    assert(nexus_virtio_notify_address(
        0x10000000U, 0x2000U, 0x100U, 7U, 4U, &physical));
    assert(physical == 0x1000201CU);

    assert(!nexus_virtio_notify_address(
        0x10000000U, 0x2000U, 0x20U, 7U, 4U, &physical));
    assert(!nexus_virtio_notify_address(
        UINT64_MAX - 0x10U, 0x20U, 0x100U, 0U, 0U, &physical));
    assert(!nexus_virtio_notify_address(
        0x10000000U, 0x2FFFU, 0x10U, 0U, 0U, &physical));
    assert(!nexus_virtio_notify_address(
        0x10000000U, 0x2001U, 0x10U, 0U, 0U, &physical));
}

int main(void) {
    test_queue_layouts();
    test_notify_address();
    puts("VirtIO queue layout and notification tests passed.");
    return 0;
}
