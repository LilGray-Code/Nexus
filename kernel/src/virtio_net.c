#include "virtio_net.h"

#include "memory.h"
#include "netdev.h"
#include "pci.h"
#include "serial.h"
#include "virtio_pci.h"

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_NET_F_MAC (1U << 5)
#define VIRTIO_NET_F_STATUS (1U << 16)
#define VIRTIO_NET_QUEUE_RX 0U
#define VIRTIO_NET_QUEUE_TX 1U
#define VIRTIO_NET_QUEUE_SIZE 64U
#define VIRTIO_NET_HEADER_BYTES 10U
#define VIRTIO_NET_RX_BUFFER_BYTES (NEXUS_NETDEV_MAX_FRAME + VIRTIO_NET_HEADER_BYTES)
#define VIRTIO_NET_TX_TIMEOUT 300U
#define VIRTIO_NET_DMA_LIMIT 0x100000000ULL

static struct nexus_virtio_pci transport;
static struct nexus_virtio_queue receive_queue;
static struct nexus_virtio_queue transmit_queue;
static uint64_t receive_physical[VIRTIO_NET_QUEUE_SIZE];
static volatile uint8_t *receive_buffers[VIRTIO_NET_QUEUE_SIZE];
static uint64_t transmit_physical;
static volatile uint8_t *transmit_buffer;
static uint8_t network_mac[6];
static bool initialized;
static bool transmit_busy;

static void copy_bytes(volatile uint8_t *destination,
                       const uint8_t *source, size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool valid_mac(const uint8_t mac[6]) {
    uint8_t any = 0U;
    for (size_t i = 0U; i < 6U; ++i) any |= mac[i];
    return any != 0U && (mac[0] & 1U) == 0U;
}

static const struct nexus_pci_device *find_virtio_net(void) {
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *pci = pci_device_at(i);
        if (pci != NULL && pci->vendor_id == NEXUS_VIRTIO_PCI_VENDOR &&
            pci->device_id == NEXUS_VIRTIO_PCI_DEVICE_NET) return pci;
    }
    return NULL;
}

static bool virtio_net_link_up(void *context) {
    const struct nexus_virtio_pci *device =
        (const struct nexus_virtio_pci *)context;
    if (!initialized || device == NULL) return false;
    if ((device->features & ((uint64_t)1U << 16)) == 0U ||
        device->device_config_length < 8U) return true;
    const uint16_t status =
        (uint16_t)device->device_config[6] |
        ((uint16_t)device->device_config[7] << 8);
    return (status & 1U) != 0U;
}

static bool virtio_net_send(void *context,
                            const void *frame, size_t length) {
    (void)context;
    if (!initialized || transmit_busy || frame == NULL ||
        length < NEXUS_NETDEV_MIN_FRAME ||
        length > NEXUS_NETDEV_MAX_FRAME) return false;

    for (size_t i = 0U; i < VIRTIO_NET_HEADER_BYTES; ++i)
        transmit_buffer[i] = 0U;
    copy_bytes(transmit_buffer + VIRTIO_NET_HEADER_BYTES,
               (const uint8_t *)frame, length);

    virtio_pci_queue_set_descriptor(
        &transmit_queue, 0U, transmit_physical,
        VIRTIO_NET_HEADER_BYTES, NEXUS_VIRTQ_DESC_F_NEXT, 1U);
    virtio_pci_queue_set_descriptor(
        &transmit_queue, 1U, transmit_physical + VIRTIO_NET_HEADER_BYTES,
        (uint32_t)length, 0U, 0U);
    transmit_busy = true;
    if (!virtio_pci_queue_post(&transmit_queue, 0U)) return false;

    /* Stop-and-wait keeps the single DMA packet buffer immutable until the
     * device has returned descriptor zero in the used ring.
     */
    const uint64_t start = 0U; /* The caller's next net poll drives retries. */
    (void)start;
    uint32_t descriptor = 0U, used_length = 0U;
    for (uint64_t spin = 0U; spin < 5000000U; ++spin) {
        if (virtio_pci_queue_pop_used(
                &transmit_queue, &descriptor, &used_length)) {
            transmit_busy = false;
            return descriptor == 0U;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    /* Do not reuse a buffer whose ownership by the device is uncertain. */
    return false;
}

static bool virtio_net_receive(void *context, void *buffer,
                               size_t capacity, size_t *length) {
    (void)context;
    if (length != NULL) *length = 0U;
    if (!initialized || buffer == NULL || length == NULL ||
        capacity < NEXUS_NETDEV_MIN_FRAME) return false;

    uint32_t descriptor = 0U, used_length = 0U;
    if (!virtio_pci_queue_pop_used(
            &receive_queue, &descriptor, &used_length)) return false;
    if (descriptor >= receive_queue.size ||
        receive_buffers[descriptor] == NULL) return false;

    bool valid = used_length >=
                     VIRTIO_NET_HEADER_BYTES + NEXUS_NETDEV_MIN_FRAME &&
                 used_length <= VIRTIO_NET_RX_BUFFER_BYTES;
    if (valid) {
        const volatile uint8_t *header = receive_buffers[descriptor];
        /* No checksum or GSO offload features were negotiated, so a valid
         * packet arrives with a plain ten-byte all-zero virtio_net_hdr.
         */
        for (size_t i = 0U; i < VIRTIO_NET_HEADER_BYTES; ++i) {
            if (header[i] != 0U) valid = false;
        }
    }

    size_t frame_length = 0U;
    if (valid) {
        frame_length = used_length - VIRTIO_NET_HEADER_BYTES;
        if (frame_length > capacity) valid = false;
    }
    if (valid) {
        const volatile uint8_t *source =
            receive_buffers[descriptor] + VIRTIO_NET_HEADER_BYTES;
        uint8_t *destination = (uint8_t *)buffer;
        for (size_t i = 0U; i < frame_length; ++i)
            destination[i] = source[i];
    }

    virtio_pci_queue_set_descriptor(
        &receive_queue, (uint16_t)descriptor,
        receive_physical[descriptor], VIRTIO_NET_RX_BUFFER_BYTES,
        NEXUS_VIRTQ_DESC_F_WRITE, 0U);
    if (!virtio_pci_queue_post(&receive_queue, (uint16_t)descriptor))
        return false;
    if (!valid) return false;
    *length = frame_length;
    return true;
}

static const struct nexus_netdev_ops network_ops = {
    .send_frame = virtio_net_send,
    .receive_frame = virtio_net_receive,
    .link_up = virtio_net_link_up
};

bool virtio_net_init(void) {
    initialized = false;
    transmit_busy = false;
    const struct nexus_pci_device *pci = find_virtio_net();
    if (pci == NULL) return false;

    if (!virtio_pci_open(pci, VIRTIO_NET_F_MAC,
                         VIRTIO_NET_F_STATUS, &transport)) {
        serial_write("VIRTIO NET: modern PCI feature/capability negotiation failed\r\n");
        return false;
    }
    if (transport.device_config_length < 6U) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO NET: MAC address configuration is unavailable\r\n");
        return false;
    }
    for (size_t i = 0U; i < sizeof(network_mac); ++i)
        network_mac[i] = transport.device_config[i];
    if (!valid_mac(network_mac)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO NET: device supplied an invalid MAC address\r\n");
        return false;
    }

    if (!virtio_pci_queue_init(&transport, &receive_queue,
                               VIRTIO_NET_QUEUE_RX, VIRTIO_NET_QUEUE_SIZE) ||
        !virtio_pci_queue_init(&transport, &transmit_queue,
                               VIRTIO_NET_QUEUE_TX, VIRTIO_NET_QUEUE_SIZE)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO NET: RX/TX virtqueue setup failed\r\n");
        return false;
    }
    if (!virtio_pci_finish(&transport)) return false;

    for (uint16_t i = 0U; i < receive_queue.size; ++i) {
        uint64_t physical = 0U;
        if (!memory_alloc_dma_page(VIRTIO_NET_DMA_LIMIT, &physical)) {
            virtio_pci_fail(&transport);
            serial_write("VIRTIO NET: low-memory RX DMA allocation failed\r\n");
            return false;
        }
        volatile uint8_t *memory =
            (volatile uint8_t *)memory_phys_to_virt(physical);
        if (memory == NULL) {
            (void)memory_free_page(physical);
            virtio_pci_fail(&transport);
            return false;
        }
        for (size_t byte = 0U; byte < VIRTIO_NET_RX_BUFFER_BYTES; ++byte)
            memory[byte] = 0U;
        receive_physical[i] = physical;
        receive_buffers[i] = memory;
        virtio_pci_queue_set_descriptor(
            &receive_queue, i, physical, VIRTIO_NET_RX_BUFFER_BYTES,
            NEXUS_VIRTQ_DESC_F_WRITE, 0U);
    }

    if (!memory_alloc_dma_page(VIRTIO_NET_DMA_LIMIT, &transmit_physical)) {
        virtio_pci_fail(&transport);
        serial_write("VIRTIO NET: low-memory TX DMA allocation failed\r\n");
        return false;
    }
    transmit_buffer =
        (volatile uint8_t *)memory_phys_to_virt(transmit_physical);
    if (transmit_buffer == NULL) {
        (void)memory_free_page(transmit_physical);
        transmit_physical = 0U;
        virtio_pci_fail(&transport);
        return false;
    }

    for (uint16_t i = 0U; i < receive_queue.size; ++i) {
        if (!virtio_pci_queue_post(&receive_queue, i)) {
            virtio_pci_fail(&transport);
            serial_write("VIRTIO NET: failed to publish receive DMA buffers\r\n");
            return false;
        }
    }

    initialized = true;
    if (!netdev_register("virtio0", NEXUS_NETDEV_MEDIUM_ETHERNET,
                         network_mac, NEXUS_NETDEV_MAX_FRAME,
                         &network_ops, &transport)) {
        initialized = false;
        virtio_pci_fail(&transport);
        serial_write("VIRTIO NET: shared Ethernet stack rejected the adapter\r\n");
        return false;
    }
    serial_write("VIRTIO NET: modern PCI adapter registered MAC=");
    for (size_t i = 0U; i < 6U; ++i) {
        serial_write_hex(network_mac[i]);
        if (i != 5U) serial_write(":");
    }
    serial_write("\r\n");
    return true;
}

bool virtio_net_ready(void) {
    return initialized;
}
