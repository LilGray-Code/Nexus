#ifndef NEXUS_VIRTIO_PCI_H
#define NEXUS_VIRTIO_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_VIRTIO_PCI_VENDOR 0x1AF4U
#define NEXUS_VIRTIO_PCI_DEVICE_NET 0x1041U
#define NEXUS_VIRTIO_PCI_DEVICE_GPU 0x1050U
#define NEXUS_VIRTIO_F_VERSION_1 32U
#define NEXUS_VIRTIO_STATUS_ACKNOWLEDGE 0x01U
#define NEXUS_VIRTIO_STATUS_DRIVER 0x02U
#define NEXUS_VIRTIO_STATUS_DRIVER_OK 0x04U
#define NEXUS_VIRTIO_STATUS_FEATURES_OK 0x08U
#define NEXUS_VIRTIO_STATUS_FAILED 0x80U
#define NEXUS_VIRTQ_DESC_F_NEXT 0x01U
#define NEXUS_VIRTQ_DESC_F_WRITE 0x02U

struct nexus_pci_device;
struct nexus_virtio_pci;

struct nexus_virtio_pci {
    const struct nexus_pci_device *pci;
    volatile uint8_t *common;
    volatile uint8_t *device_config;
    uint32_t device_config_length;
    uint64_t notify_bar_physical;
    uint32_t notify_cap_offset;
    uint32_t notify_cap_length;
    uint32_t notify_multiplier;
    uint64_t features;
    bool opened;
};

struct nexus_virtq_desc {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct nexus_virtq_used_element {
    uint32_t id;
    uint32_t length;
} __attribute__((packed));

struct nexus_virtio_queue {
    struct nexus_virtio_pci *device;
    uint64_t physical_base;
    uint64_t page_count;
    uint16_t index;
    uint16_t size;
    uint16_t available_index;
    uint16_t last_used_index;
    volatile struct nexus_virtq_desc *descriptors;
    volatile uint16_t *available_flags;
    volatile uint16_t *available_ring;
    volatile uint16_t *available_index_ptr;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_index_ptr;
    volatile struct nexus_virtq_used_element *used_ring;
    volatile uint16_t *notify_address;
};

bool virtio_pci_open(const struct nexus_pci_device *pci,
                     uint32_t required_features_low,
                     uint32_t optional_features_low,
                     struct nexus_virtio_pci *device);
bool virtio_pci_queue_init(struct nexus_virtio_pci *device,
                           struct nexus_virtio_queue *queue,
                           uint16_t queue_index, uint16_t requested_size);
bool virtio_pci_finish(struct nexus_virtio_pci *device);
void virtio_pci_fail(struct nexus_virtio_pci *device);
void virtio_pci_queue_set_descriptor(struct nexus_virtio_queue *queue,
                                     uint16_t descriptor,
                                     uint64_t physical_address,
                                     uint32_t length, uint16_t flags,
                                     uint16_t next);
bool virtio_pci_queue_post(struct nexus_virtio_queue *queue,
                           uint16_t descriptor);
bool virtio_pci_queue_pop_used(struct nexus_virtio_queue *queue,
                               uint32_t *descriptor, uint32_t *length);
bool virtio_pci_queue_notify(struct nexus_virtio_queue *queue);
bool virtio_pci_queue_submit_pair(struct nexus_virtio_queue *queue,
                                  uint64_t request_physical,
                                  uint32_t request_length,
                                  uint64_t response_physical,
                                  uint32_t response_capacity,
                                  uint64_t timeout_ticks,
                                  uint32_t *response_length);
volatile uint8_t *virtio_pci_device_config(
    const struct nexus_virtio_pci *device);

#endif
