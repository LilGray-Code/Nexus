#include "virtio_pci.h"

#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CAPABILITY_LIST 0x34U
#define PCI_CAP_ID_VENDOR 0x09U
#define VIRTIO_PCI_CAP_COMMON_CFG 1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2U
#define VIRTIO_PCI_CAP_DEVICE_CFG 4U
#define VIRTIO_COMMON_MIN_LENGTH 56U
#define VIRTIO_NOTIFY_MULTIPLIER_OFFSET 16U
#define VIRTIO_QUEUE_SELECT 22U
#define VIRTIO_QUEUE_SIZE 24U
#define VIRTIO_QUEUE_ENABLE 28U
#define VIRTIO_QUEUE_NOTIFY_OFF 30U
#define VIRTIO_QUEUE_DESC 32U
#define VIRTIO_QUEUE_DRIVER 40U
#define VIRTIO_QUEUE_DEVICE 48U
#define VIRTIO_DMA_LIMIT 0x100000000ULL
#define VIRTQ_DESC_BYTES 16U

static uint16_t read_le16(const volatile uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t read_le32(const volatile uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t read_le64(const volatile uint8_t *p) {
    return (uint64_t)read_le32(p) |
           ((uint64_t)read_le32(p + 4U) << 32);
}
static void write_le16(volatile uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void write_le32(volatile uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
static void write_le64(volatile uint8_t *p, uint64_t value) {
    write_le32(p, (uint32_t)value);
    write_le32(p + 4U, (uint32_t)(value >> 32));
}
static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}
static void memory_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static bool map_bar_region(const struct nexus_pci_device *pci,
                           uint8_t bar_index, uint32_t offset,
                           uint32_t length, volatile uint8_t **mapped,
                           uint64_t *bar_physical) {
    if (pci == NULL || mapped == NULL || length == 0U) return false;
    *mapped = NULL;
    struct nexus_pci_bar bar;
    if (!pci_decode_bar(pci, bar_index, &bar) || !bar.present ||
        !bar.assigned || bar.io_space || bar.base > UINT64_MAX - offset)
        return false;
    const uint64_t physical = bar.base + offset;
    if (physical > UINT64_MAX - length ||
        (physical & 0xFFFU) + (uint64_t)length > 0x1000U) return false;

    volatile uint32_t *page = NULL;
    if (!paging_map_mmio_page(physical & ~0xFFFULL, &page) || page == NULL)
        return false;
    *mapped = (volatile uint8_t *)page + (physical & 0xFFFU);
    if (bar_physical != NULL) *bar_physical = bar.base;
    return true;
}

static void set_status(struct nexus_virtio_pci *device, uint8_t status) {
    if (device != NULL && device->common != NULL)
        device->common[20] = status;
}

void virtio_pci_fail(struct nexus_virtio_pci *device) {
    if (device == NULL || device->common == NULL) return;
    set_status(device, (uint8_t)(device->common[20] |
                                 NEXUS_VIRTIO_STATUS_FAILED));
    device->opened = false;
}

bool virtio_pci_open(const struct nexus_pci_device *pci,
                     uint32_t required_features_low,
                     uint32_t optional_features_low,
                     struct nexus_virtio_pci *device) {
    if (pci == NULL || device == NULL ||
        pci->vendor_id != NEXUS_VIRTIO_PCI_VENDOR ||
        pci->status == UINT16_MAX) return false;
    *device = (struct nexus_virtio_pci){0};

    uint8_t capability = 0U;
    if (!pci_read_config8(pci, PCI_CAPABILITY_LIST, &capability))
        return false;
    capability &= 0xFCU;
    bool found_common = false;
    bool found_notify = false;
    bool found_device = false;
    uint8_t common_bar = 0U, notify_bar = 0U, device_bar = 0U;
    uint32_t common_offset = 0U, common_length = 0U;
    uint32_t notify_offset = 0U, notify_length = 0U;
    uint32_t device_offset = 0U, device_length = 0U;
    uint8_t visited[64] = {0U};
    size_t visited_count = 0U;
    while (capability >= 0x40U && capability <= 0xFCU &&
           visited_count < sizeof(visited)) {
        bool already_seen = false;
        for (size_t i = 0U; i < visited_count; ++i)
            if (visited[i] == capability) already_seen = true;
        if (already_seen) break;
        visited[visited_count++] = capability;

        uint8_t id = 0U, next = 0U, cap_length = 0U, type = 0U;
        if (!pci_read_config8(pci, capability, &id) ||
            !pci_read_config8(pci, (uint8_t)(capability + 1U), &next) ||
            !pci_read_config8(pci, (uint8_t)(capability + 2U), &cap_length) ||
            !pci_read_config8(pci, (uint8_t)(capability + 3U), &type))
            return false;
        if (id == PCI_CAP_ID_VENDOR && cap_length >= 16U) {
            uint8_t bar = 0U;
            uint32_t offset = 0U, length = 0U;
            if (!pci_read_config8(pci, (uint8_t)(capability + 4U), &bar) ||
                !pci_read_config32(pci, (uint8_t)(capability + 8U), &offset) ||
                !pci_read_config32(pci, (uint8_t)(capability + 12U), &length))
                return false;
            if (bar < NEXUS_PCI_BAR_COUNT && length != 0U) {
                if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
                    common_bar = bar; common_offset = offset;
                    common_length = length; found_common = true;
                } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG &&
                           cap_length >= VIRTIO_NOTIFY_MULTIPLIER_OFFSET + 4U) {
                    notify_bar = bar; notify_offset = offset;
                    notify_length = length; found_notify = true;
                } else if (type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                    device_bar = bar; device_offset = offset;
                    device_length = length; found_device = true;
                }
            }
        }
        if (next == 0U) break;
        if (next < 0x40U || next > 0xFCU || (next & 3U) != 0U)
            return false;
        capability = next;
    }
    if (!found_common || !found_notify || !found_device ||
        common_length < VIRTIO_COMMON_MIN_LENGTH ||
        common_offset > UINT32_MAX - common_length ||
        notify_offset > UINT32_MAX - notify_length ||
        device_offset > UINT32_MAX - device_length) return false;

    if (!map_bar_region(pci, common_bar, common_offset,
                        VIRTIO_COMMON_MIN_LENGTH, &device->common, NULL))
        return false;
    /* Device config may be optional for a given class, but its capability
     * must be mapped so drivers can validate and read its fixed header.
     */
    if (!map_bar_region(pci, device_bar, device_offset,
                        device_length > 6U ? 6U : device_length,
                        &device->device_config, NULL)) return false;

    struct nexus_pci_bar notify_info;
    if (!pci_decode_bar(pci, notify_bar, &notify_info) ||
        !notify_info.present || !notify_info.assigned ||
        notify_info.io_space || notify_info.base > UINT64_MAX - notify_offset)
        return false;

    device->pci = pci;
    device->notify_bar_physical = notify_info.base;
    device->notify_cap_offset = notify_offset;
    device->notify_cap_length = notify_length;
    uint8_t cap_next = 0U, cap_pointer = 0U;
    if (!pci_read_config8(pci, PCI_CAPABILITY_LIST, &cap_pointer))
        return false;
    cap_pointer &= 0xFCU;
    size_t scan = 0U;
    while (cap_pointer >= 0x40U && cap_pointer <= 0xFCU && scan++ < 64U) {
        uint8_t id = 0U, next = 0U, length = 0U, type = 0U;
        if (!pci_read_config8(pci, cap_pointer, &id) ||
            !pci_read_config8(pci, (uint8_t)(cap_pointer + 1U), &next) ||
            !pci_read_config8(pci, (uint8_t)(cap_pointer + 2U), &length) ||
            !pci_read_config8(pci, (uint8_t)(cap_pointer + 3U), &type))
            return false;
        if (id == PCI_CAP_ID_VENDOR && type == VIRTIO_PCI_CAP_NOTIFY_CFG &&
            length >= VIRTIO_NOTIFY_MULTIPLIER_OFFSET + 4U) {
            if (!pci_read_config32(pci,
                    (uint8_t)(cap_pointer + VIRTIO_NOTIFY_MULTIPLIER_OFFSET),
                    &device->notify_multiplier)) return false;
            break;
        }
        cap_next = next;
        if (cap_next == 0U) break;
        if (cap_next < 0x40U || cap_next > 0xFCU) return false;
        cap_pointer = cap_next;
    }
    if (device->notify_multiplier == 0U) return false;

    if (!pci_enable_memory_bus_master(pci)) return false;

    /* Reset, acknowledge, and identify the driver before feature negotiation. */
    set_status(device, 0U);
    for (volatile uint32_t wait = 0U; wait < 10000U; ++wait)
        __asm__ volatile ("pause" ::: "memory");
    if (device->common[20] != 0U) return false;
    set_status(device, NEXUS_VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(device, NEXUS_VIRTIO_STATUS_ACKNOWLEDGE |
                       NEXUS_VIRTIO_STATUS_DRIVER);

    write_le32(device->common + 0U, 0U);
    const uint32_t host_low = read_le32(device->common + 4U);
    write_le32(device->common + 0U, 1U);
    const uint32_t host_high = read_le32(device->common + 4U);
    if ((host_high & 1U) == 0U) {
        virtio_pci_fail(device);
        return false;
    }

    const uint32_t accepted_low =
        required_features_low | (optional_features_low & host_low);
    if ((accepted_low & required_features_low) != required_features_low) {
        virtio_pci_fail(device);
        return false;
    }
    write_le32(device->common + 8U, 0U);
    write_le32(device->common + 12U, accepted_low);
    write_le32(device->common + 8U, 1U);
    write_le32(device->common + 12U, 1U); /* VIRTIO_F_VERSION_1 */

    set_status(device, NEXUS_VIRTIO_STATUS_ACKNOWLEDGE |
                       NEXUS_VIRTIO_STATUS_DRIVER |
                       NEXUS_VIRTIO_STATUS_FEATURES_OK);
    if ((device->common[20] & NEXUS_VIRTIO_STATUS_FEATURES_OK) == 0U) {
        virtio_pci_fail(device);
        return false;
    }
    device->features = ((uint64_t)1U << 32) | accepted_low;
    device->opened = true;
    return true;
}

volatile uint8_t *virtio_pci_device_config(
    const struct nexus_virtio_pci *device) {
    return device == NULL || !device->opened
        ? NULL : device->device_config;
}

bool virtio_pci_queue_init(struct nexus_virtio_pci *device,
                           struct nexus_virtio_queue *queue,
                           uint16_t queue_index, uint16_t requested_size) {
    if (device == NULL || !device->opened || queue == NULL ||
        requested_size < 2U) return false;
    volatile uint8_t *common = device->common;
    write_le16(common + VIRTIO_QUEUE_SELECT, queue_index);
    const uint16_t maximum = read_le16(common + VIRTIO_QUEUE_SIZE);
    if (maximum < 2U) return false;
    uint16_t size = requested_size < maximum ? requested_size : maximum;
    uint16_t power = 1U;
    while ((uint16_t)(power << 1U) != 0U &&
           (uint16_t)(power << 1U) <= size) power <<= 1U;
    size = power;
    if (size < 2U) return false;
    write_le16(common + VIRTIO_QUEUE_SIZE, size);

    const uint64_t descriptor_bytes = (uint64_t)size * VIRTQ_DESC_BYTES;
    const uint64_t available_offset = align_up(descriptor_bytes, 2U);
    const uint64_t available_bytes = 6U + (uint64_t)size * 2U;
    const uint64_t used_offset = align_up(available_offset + available_bytes, 4U);
    const uint64_t used_bytes = 6U + (uint64_t)size * 8U;
    const uint64_t total_bytes = used_offset + used_bytes;
    const uint64_t pages = (total_bytes + NEXUS_PAGE_SIZE - 1U) /
                           NEXUS_PAGE_SIZE;
    uint64_t physical = 0U;
    if (!memory_alloc_contiguous_pages(pages, VIRTIO_DMA_LIMIT, &physical))
        return false;
    volatile uint8_t *memory =
        (volatile uint8_t *)memory_phys_to_virt(physical);
    if (memory == NULL) {
        (void)memory_free_contiguous_pages(physical, pages);
        return false;
    }
    for (uint64_t i = 0U; i < pages * NEXUS_PAGE_SIZE; ++i)
        memory[i] = 0U;

    *queue = (struct nexus_virtio_queue){0};
    queue->device = device;
    queue->physical_base = physical;
    queue->page_count = pages;
    queue->index = queue_index;
    queue->size = size;
    queue->descriptors = (volatile struct nexus_virtq_desc *)memory;
    queue->available_flags = (volatile uint16_t *)(memory + available_offset);
    queue->available_index = 0U;
    queue->available_index_ptr = queue->available_flags + 1U;
    queue->available_ring = queue->available_flags + 2U;
    queue->used_flags = (volatile uint16_t *)(memory + used_offset);
    queue->used_index_ptr = queue->used_flags + 1U;
    queue->used_ring = (volatile struct nexus_virtq_used_element *)
        (memory + used_offset + 4U);
    queue->last_used_index = 0U;

    write_le64(common + VIRTIO_QUEUE_DESC, physical);
    write_le64(common + VIRTIO_QUEUE_DRIVER, physical + available_offset);
    write_le64(common + VIRTIO_QUEUE_DEVICE, physical + used_offset);
    write_le16(common + VIRTIO_QUEUE_ENABLE, 1U);
    return true;
}

bool virtio_pci_finish(struct nexus_virtio_pci *device) {
    if (device == NULL || !device->opened) return false;
    memory_barrier();
    set_status(device, NEXUS_VIRTIO_STATUS_ACKNOWLEDGE |
                       NEXUS_VIRTIO_STATUS_DRIVER |
                       NEXUS_VIRTIO_STATUS_FEATURES_OK |
                       NEXUS_VIRTIO_STATUS_DRIVER_OK);
    if ((device->common[20] & NEXUS_VIRTIO_STATUS_DRIVER_OK) == 0U) {
        virtio_pci_fail(device);
        return false;
    }
    return true;
}

void virtio_pci_queue_set_descriptor(struct nexus_virtio_queue *queue,
                                     uint16_t descriptor,
                                     uint64_t physical_address,
                                     uint32_t length, uint16_t flags,
                                     uint16_t next) {
    if (queue == NULL || descriptor >= queue->size) return;
    queue->descriptors[descriptor].address = physical_address;
    queue->descriptors[descriptor].length = length;
    queue->descriptors[descriptor].flags = flags;
    queue->descriptors[descriptor].next = next;
}

bool virtio_pci_queue_post(struct nexus_virtio_queue *queue,
                           uint16_t descriptor) {
    if (queue == NULL || descriptor >= queue->size) return false;
    queue->available_ring[queue->available_index % queue->size] = descriptor;
    memory_barrier();
    ++queue->available_index;
    *queue->available_index_ptr = queue->available_index;
    memory_barrier();
    return virtio_pci_queue_notify(queue);
}

bool virtio_pci_queue_pop_used(struct nexus_virtio_queue *queue,
                               uint32_t *descriptor, uint32_t *length) {
    if (queue == NULL || descriptor == NULL || length == NULL) return false;
    memory_barrier();
    const uint16_t used_index = *queue->used_index_ptr;
    if (queue->last_used_index == used_index) return false;
    const volatile struct nexus_virtq_used_element *element =
        &queue->used_ring[queue->last_used_index % queue->size];
    *descriptor = element->id;
    *length = element->length;
    ++queue->last_used_index;
    memory_barrier();
    return true;
}

bool virtio_pci_queue_notify(struct nexus_virtio_queue *queue) {
    if (queue == NULL || queue->device == NULL || !queue->device->opened)
        return false;
    volatile uint8_t *common = queue->device->common;
    write_le16(common + VIRTIO_QUEUE_SELECT, queue->index);
    const uint16_t notify_offset = read_le16(common + VIRTIO_QUEUE_NOTIFY_OFF);
    const uint64_t notify_relative =
        (uint64_t)notify_offset * queue->device->notify_multiplier;
    if (notify_relative > UINT32_MAX ||
        notify_relative + 2U > queue->device->notify_cap_length ||
        queue->device->notify_cap_offset >
            UINT64_MAX - notify_relative) return false;
    const uint64_t physical = queue->device->notify_bar_physical +
        queue->device->notify_cap_offset + notify_relative;
    volatile uint32_t *page = NULL;
    if (!paging_map_mmio_page(physical & ~0xFFFULL, &page) || page == NULL)
        return false;
    volatile uint16_t *doorbell = (volatile uint16_t *)
        ((volatile uint8_t *)page + (physical & 0xFFFU));
    *doorbell = queue->index;
    __asm__ volatile ("sfence" ::: "memory");
    return true;
}

bool virtio_pci_queue_submit_pair(struct nexus_virtio_queue *queue,
                                  uint64_t request_physical,
                                  uint32_t request_length,
                                  uint64_t response_physical,
                                  uint32_t response_capacity,
                                  uint64_t timeout_ticks,
                                  uint32_t *response_length) {
    if (queue == NULL || queue->size < 2U || request_length == 0U ||
        response_capacity == 0U || response_length == NULL ||
        !memory_page_is_allocated(request_physical &
                                  ~(NEXUS_PAGE_SIZE - 1U)) ||
        !memory_page_is_allocated(response_physical &
                                  ~(NEXUS_PAGE_SIZE - 1U))) return false;

    virtio_pci_queue_set_descriptor(queue, 0U, request_physical,
        request_length, NEXUS_VIRTQ_DESC_F_NEXT, 1U);
    virtio_pci_queue_set_descriptor(queue, 1U, response_physical,
        response_capacity, NEXUS_VIRTQ_DESC_F_WRITE, 0U);
    if (!virtio_pci_queue_post(queue, 0U)) return false;

    const uint64_t start = timer_ticks();
    for (;;) {
        uint32_t id = 0U, used_length = 0U;
        if (virtio_pci_queue_pop_used(queue, &id, &used_length)) {
            if (id != 0U || used_length > response_capacity) return false;
            *response_length = used_length;
            return true;
        }
        if (timeout_ticks != 0U && timer_ticks() - start >= timeout_ticks)
            return false;
        __asm__ volatile ("pause" ::: "memory");
    }
}
