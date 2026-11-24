#include "e1000.h"
#include "netdev.h"

#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define E1000_VENDOR_INTEL 0x8086U
#define E1000_DEVICE_82540EM 0x100EU
#define E1000_DEVICE_82540EP 0x101EU
#define E1000_DEVICE_82545EM 0x100FU
#define E1000_DEVICE_82545GM 0x1026U
#define E1000_DEVICE_82546EB 0x1010U
#define E1000_DEVICE_82574L  0x10D3U

#define E1000_PAGE_SIZE 4096U
#define E1000_DMA_LIMIT 0x100000000ULL
#define E1000_DESCRIPTOR_COUNT 128U
#define E1000_BUFFER_SIZE 2048U
#define E1000_MMIO_PAGE_COUNT 4U
#define E1000_TX_WAIT_LIMIT 1000000U

#define E1000_REG_CTRL   0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_REG_ICR    0x00C0U
#define E1000_REG_IMC    0x00D8U
#define E1000_REG_RCTL   0x0100U
#define E1000_REG_TCTL   0x0400U
#define E1000_REG_TIPG   0x0410U
#define E1000_REG_RDBAL  0x2800U
#define E1000_REG_RDBAH  0x2804U
#define E1000_REG_RDLEN  0x2808U
#define E1000_REG_RDH    0x2810U
#define E1000_REG_RDT    0x2818U
#define E1000_REG_TDBAL  0x3800U
#define E1000_REG_TDBAH  0x3804U
#define E1000_REG_TDLEN  0x3808U
#define E1000_REG_TDH    0x3810U
#define E1000_REG_TDT    0x3818U
#define E1000_REG_RAL0   0x5400U
#define E1000_REG_RAH0   0x5404U

#define E1000_CTRL_RST (1U << 26)
#define E1000_CTRL_SLU (1U << 6)
#define E1000_CTRL_FD  (1U << 0)
#define E1000_RCTL_EN    (1U << 1)
#define E1000_RCTL_BAM   (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_TXD_STATUS_DD 0x01U
#define E1000_TXD_CMD_EOP  0x01U
#define E1000_TXD_CMD_IFCS 0x02U
#define E1000_TXD_CMD_RS   0x08U
#define E1000_RXD_STATUS_DD 0x01U

struct e1000_tx_descriptor {
    uint64_t buffer_address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

struct e1000_rx_descriptor {
    uint64_t buffer_address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

_Static_assert(sizeof(struct e1000_tx_descriptor) == 16U,
               "E1000 TX descriptor must be 16 bytes");
_Static_assert(sizeof(struct e1000_rx_descriptor) == 16U,
               "E1000 RX descriptor must be 16 bytes");

struct e1000_state {
    bool ready;
    uint64_t bar_base;
    uint64_t mapped_physical[E1000_MMIO_PAGE_COUNT];
    volatile uint32_t *mapped_virtual[E1000_MMIO_PAGE_COUNT];
    size_t mapped_page_count;
    volatile struct e1000_tx_descriptor *tx_ring;
    volatile struct e1000_rx_descriptor *rx_ring;
    uint64_t tx_ring_physical;
    uint64_t rx_ring_physical;
    uint64_t tx_buffer_physical[E1000_DESCRIPTOR_COUNT];
    uint64_t rx_buffer_physical[E1000_DESCRIPTOR_COUNT];
    uint8_t *tx_buffers[E1000_DESCRIPTOR_COUNT];
    uint8_t *rx_buffers[E1000_DESCRIPTOR_COUNT];
    uint32_t tx_tail;
    uint32_t rx_head;
    uint8_t mac[6];
};

static struct e1000_state state;

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
}

static bool supported_device(uint16_t vendor, uint16_t device) {
    if (vendor != E1000_VENDOR_INTEL) return false;
    switch (device) {
        case E1000_DEVICE_82540EM:
        case E1000_DEVICE_82540EP:
        case E1000_DEVICE_82545EM:
        case E1000_DEVICE_82545GM:
        case E1000_DEVICE_82546EB:
        case E1000_DEVICE_82574L:
            return true;
        default:
            return false;
    }
}

static volatile uint32_t *map_mmio_page(uint64_t physical_page) {
    for (size_t i = 0; i < state.mapped_page_count; ++i) {
        if (state.mapped_physical[i] == physical_page) {
            return state.mapped_virtual[i];
        }
    }
    if (state.mapped_page_count >= E1000_MMIO_PAGE_COUNT) return NULL;

    volatile uint32_t *mapped = NULL;
    if (!paging_map_mmio_page(physical_page, &mapped) || mapped == NULL) {
        return NULL;
    }
    const size_t slot = state.mapped_page_count++;
    state.mapped_physical[slot] = physical_page;
    state.mapped_virtual[slot] = mapped;
    return mapped;
}

static volatile uint32_t *register_pointer(uint32_t offset) {
    const uint64_t physical = state.bar_base + (uint64_t)offset;
    const uint64_t physical_page = physical & ~(uint64_t)(E1000_PAGE_SIZE - 1U);
    volatile uint32_t *page = map_mmio_page(physical_page);
    if (page == NULL) return NULL;
    return page + ((physical & (E1000_PAGE_SIZE - 1U)) / sizeof(uint32_t));
}

static bool register_write(uint32_t offset, uint32_t value) {
    volatile uint32_t *reg = register_pointer(offset);
    if (reg == NULL) return false;
    *reg = value;
    __asm__ volatile ("mfence" : : : "memory");
    return true;
}

static bool register_read(uint32_t offset, uint32_t *value) {
    if (value == NULL) return false;
    volatile uint32_t *reg = register_pointer(offset);
    if (reg == NULL) return false;
    *value = *reg;
    return true;
}

static bool allocate_dma_page(uint64_t *physical, uint8_t **virtual_address) {
    if (physical == NULL || virtual_address == NULL ||
        !memory_alloc_dma_page(E1000_DMA_LIMIT, physical)) {
        return false;
    }
    uint8_t *mapped = (uint8_t *)memory_phys_to_virt(*physical);
    if (mapped == NULL) {
        (void)memory_free_page(*physical);
        *physical = 0U;
        return false;
    }
    zero_bytes(mapped, E1000_PAGE_SIZE);
    *virtual_address = mapped;
    return true;
}

static void release_dma_resources(void) {
    if (state.tx_ring_physical != 0U) {
        (void)memory_free_page(state.tx_ring_physical);
        state.tx_ring_physical = 0U;
    }
    if (state.rx_ring_physical != 0U) {
        (void)memory_free_page(state.rx_ring_physical);
        state.rx_ring_physical = 0U;
    }
    for (size_t i = 0; i < E1000_DESCRIPTOR_COUNT; ++i) {
        if (state.tx_buffer_physical[i] != 0U) {
            (void)memory_free_page(state.tx_buffer_physical[i]);
            state.tx_buffer_physical[i] = 0U;
        }
        if (state.rx_buffer_physical[i] != 0U) {
            (void)memory_free_page(state.rx_buffer_physical[i]);
            state.rx_buffer_physical[i] = 0U;
        }
        state.tx_buffers[i] = NULL;
        state.rx_buffers[i] = NULL;
    }
    state.tx_ring = NULL;
    state.rx_ring = NULL;
}

static bool allocate_rings(void) {
    uint8_t *page = NULL;
    if (!allocate_dma_page(&state.tx_ring_physical, &page)) return false;
    state.tx_ring = (volatile struct e1000_tx_descriptor *)page;

    if (!allocate_dma_page(&state.rx_ring_physical, &page)) return false;
    state.rx_ring = (volatile struct e1000_rx_descriptor *)page;

    for (size_t i = 0; i < E1000_DESCRIPTOR_COUNT; ++i) {
        if (!allocate_dma_page(&state.tx_buffer_physical[i],
                               &state.tx_buffers[i])) return false;
        if (!allocate_dma_page(&state.rx_buffer_physical[i],
                               &state.rx_buffers[i])) return false;

        state.tx_ring[i].buffer_address = state.tx_buffer_physical[i];
        state.tx_ring[i].status = E1000_TXD_STATUS_DD;
        state.rx_ring[i].buffer_address = state.rx_buffer_physical[i];
    }
    return true;
}

static bool read_mac_address(void) {
    uint32_t low, high;
    if (!register_read(E1000_REG_RAL0, &low) ||
        !register_read(E1000_REG_RAH0, &high) ||
        (high & 0x80000000U) == 0U) {
        return false;
    }
    state.mac[0] = (uint8_t)low;
    state.mac[1] = (uint8_t)(low >> 8);
    state.mac[2] = (uint8_t)(low >> 16);
    state.mac[3] = (uint8_t)(low >> 24);
    state.mac[4] = (uint8_t)high;
    state.mac[5] = (uint8_t)(high >> 8);
    return true;
}

static bool configure_transmit_ring(void) {
    const uint32_t bytes =
        E1000_DESCRIPTOR_COUNT * (uint32_t)sizeof(struct e1000_tx_descriptor);
    return register_write(E1000_REG_TDBAL, (uint32_t)state.tx_ring_physical) &&
           register_write(E1000_REG_TDBAH, (uint32_t)(state.tx_ring_physical >> 32)) &&
           register_write(E1000_REG_TDLEN, bytes) &&
           register_write(E1000_REG_TDH, 0U) &&
           register_write(E1000_REG_TDT, 0U) &&
           register_write(E1000_REG_TIPG, 0x0060200AU) &&
           register_write(E1000_REG_TCTL, 0x0004010AU);
}

static bool configure_receive_ring(void) {
    const uint32_t bytes =
        E1000_DESCRIPTOR_COUNT * (uint32_t)sizeof(struct e1000_rx_descriptor);
    return register_write(E1000_REG_RDBAL, (uint32_t)state.rx_ring_physical) &&
           register_write(E1000_REG_RDBAH, (uint32_t)(state.rx_ring_physical >> 32)) &&
           register_write(E1000_REG_RDLEN, bytes) &&
           register_write(E1000_REG_RDH, 0U) &&
           register_write(E1000_REG_RDT, E1000_DESCRIPTOR_COUNT - 1U) &&
           register_write(E1000_REG_RCTL,
                          E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static bool e1000_netdev_send(void *context, const void *frame,
                               size_t length) {
    (void)context;
    return e1000_send_frame(frame, length);
}

static bool e1000_netdev_receive(void *context, void *buffer, size_t capacity,
                                 size_t *length) {
    (void)context;
    return e1000_receive_frame(buffer, capacity, length);
}

static bool e1000_netdev_link_up(void *context) {
    (void)context;
    uint32_t status = 0U;
    /* Intel 8254x STATUS.LU (bit 1) reports physical link establishment. */
    return state.ready && register_read(E1000_REG_STATUS, &status) &&
           (status & (1U << 1)) != 0U;
}

bool e1000_init(void) {
    state = (struct e1000_state){0};
    const struct nexus_pci_device *device = NULL;

    for (uint32_t i = 0; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *candidate = pci_device_at(i);
        if (candidate == NULL || candidate->class_code != 0x02U ||
            candidate->subclass != 0x00U ||
            !supported_device(candidate->vendor_id, candidate->device_id)) {
            continue;
        }
        device = candidate;
        break;
    }
    if (device == NULL) {
        serial_write("E1000: no supported Intel PCI Ethernet controller found\r\n");
        return false;
    }

    struct nexus_pci_bar bar0;
    if (!pci_decode_bar(device, 0U, &bar0) || bar0.io_space ||
        !bar0.assigned || !pci_enable_memory_bus_master(device)) {
        serial_write("E1000: BAR0 or PCI memory/bus-master setup unavailable\r\n");
        return false;
    }
    state.bar_base = bar0.base;

    uint32_t control;
    if (!register_read(E1000_REG_CTRL, &control) ||
        !register_write(E1000_REG_CTRL, control | E1000_CTRL_RST)) {
        serial_write("E1000: unable to reset MMIO controller\r\n");
        return false;
    }
    bool reset_complete = false;
    for (uint32_t spin = 0; spin < 1000000U; ++spin) {
        if (!register_read(E1000_REG_CTRL, &control)) return false;
        if ((control & E1000_CTRL_RST) == 0U) {
            reset_complete = true;
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!reset_complete) {
        serial_write("E1000: controller reset timed out\r\n");
        return false;
    }

    if (!register_write(E1000_REG_IMC, UINT32_MAX)) return false;
    uint32_t ignored;
    if (!register_read(E1000_REG_ICR, &ignored) || !read_mac_address()) {
        serial_write("E1000: unable to read MAC/interrupt registers\r\n");
        return false;
    }

    if (!allocate_rings()) {
        release_dma_resources();
        serial_write("E1000: unable to allocate DMA32 rings/buffers\r\n");
        return false;
    }

    if (!configure_transmit_ring() || !configure_receive_ring()) {
        (void)register_write(E1000_REG_RCTL, 0U);
        (void)register_write(E1000_REG_TCTL, 0U);
        release_dma_resources();
        serial_write("E1000: descriptor-ring setup failed\r\n");
        return false;
    }

    control &= ~E1000_CTRL_RST;
    control |= E1000_CTRL_SLU | E1000_CTRL_FD;
    if (!register_write(E1000_REG_CTRL, control)) {
        (void)register_write(E1000_REG_RCTL, 0U);
        (void)register_write(E1000_REG_TCTL, 0U);
        release_dma_resources();
        return false;
    }

    state.ready = true;
    static const struct nexus_netdev_ops netdev_ops = {
        .send_frame = e1000_netdev_send,
        .receive_frame = e1000_netdev_receive,
        .link_up = e1000_netdev_link_up
    };
    if (!netdev_register("e1000", NEXUS_NETDEV_MEDIUM_ETHERNET,
                         state.mac, E1000_BUFFER_SIZE, &netdev_ops, NULL)) {
        state.ready = false;
        (void)register_write(E1000_REG_RCTL, 0U);
        (void)register_write(E1000_REG_TCTL, 0U);
        release_dma_resources();
        serial_write("E1000: network-device registration failed\r\n");
        return false;
    }
    serial_write("E1000: initialized in polling mode; MAC=");
    for (size_t i = 0; i < 6U; ++i) {
        serial_write_hex(state.mac[i]);
        if (i != 5U) serial_write(":");
    }
    serial_write("\r\nNEXUS E1000 NIC READY\r\n");
    return true;
}

bool e1000_ready(void) {
    return state.ready;
}

bool e1000_get_mac(uint8_t address[6]) {
    if (!state.ready || address == NULL) return false;
    copy_bytes(address, state.mac, 6U);
    return true;
}

bool e1000_send_frame(const void *frame, size_t length) {
    if (!state.ready || frame == NULL || length < 14U ||
        length > E1000_BUFFER_SIZE) {
        return false;
    }

    const uint32_t index = state.tx_tail;
    volatile struct e1000_tx_descriptor *descriptor = &state.tx_ring[index];
    bool descriptor_ready = false;
    for (uint32_t spin = 0; spin < E1000_TX_WAIT_LIMIT; ++spin) {
        if ((descriptor->status & E1000_TXD_STATUS_DD) != 0U) {
            descriptor_ready = true;
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!descriptor_ready) return false;

    copy_bytes(state.tx_buffers[index], frame, length);
    descriptor->length = (uint16_t)length;
    descriptor->checksum_offset = 0U;
    descriptor->command = E1000_TXD_CMD_EOP |
                          E1000_TXD_CMD_IFCS |
                          E1000_TXD_CMD_RS;
    descriptor->status = 0U;
    descriptor->checksum_start = 0U;
    descriptor->special = 0U;
    __asm__ volatile ("mfence" : : : "memory");

    state.tx_tail = (index + 1U) % E1000_DESCRIPTOR_COUNT;
    if (!register_write(E1000_REG_TDT, state.tx_tail)) return false;

    for (uint32_t spin = 0; spin < E1000_TX_WAIT_LIMIT; ++spin) {
        if ((descriptor->status & E1000_TXD_STATUS_DD) != 0U) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

bool e1000_receive_frame(void *buffer, size_t capacity, size_t *length) {
    if (length != NULL) *length = 0U;
    if (!state.ready || buffer == NULL || length == NULL) return false;

    volatile struct e1000_rx_descriptor *descriptor = &state.rx_ring[state.rx_head];
    if ((descriptor->status & E1000_RXD_STATUS_DD) == 0U) return false;

    __asm__ volatile ("mfence" : : : "memory");
    const uint16_t packet_length = descriptor->length;
    const uint8_t errors = descriptor->errors;
    const bool copied = packet_length >= 14U &&
                        packet_length <= E1000_BUFFER_SIZE &&
                        packet_length <= capacity &&
                        errors == 0U;
    if (copied) {
        copy_bytes(buffer, state.rx_buffers[state.rx_head], packet_length);
        *length = packet_length;
    }

    descriptor->length = 0U;
    descriptor->checksum = 0U;
    descriptor->status = 0U;
    descriptor->errors = 0U;
    descriptor->special = 0U;
    __asm__ volatile ("mfence" : : : "memory");

    const uint32_t recycled = state.rx_head;
    state.rx_head = (state.rx_head + 1U) % E1000_DESCRIPTOR_COUNT;
    (void)register_write(E1000_REG_RDT, recycled);
    return copied;
}

bool e1000_send_arp_probe(void) {
    if (!state.ready) return false;
    uint8_t packet[42];
    zero_bytes(packet, sizeof(packet));

    for (size_t i = 0; i < 6U; ++i) packet[i] = 0xFFU;
    copy_bytes(packet + 6U, state.mac, 6U);
    packet[12] = 0x08U;
    packet[13] = 0x06U;
    packet[14] = 0x00U; packet[15] = 0x01U;
    packet[16] = 0x08U; packet[17] = 0x00U;
    packet[18] = 6U; packet[19] = 4U;
    packet[20] = 0x00U; packet[21] = 0x01U;
    copy_bytes(packet + 22U, state.mac, 6U);
    packet[28] = 0U; packet[29] = 0U;
    packet[30] = 0U; packet[31] = 0U;
    packet[38] = 10U; packet[39] = 0U;
    packet[40] = 2U; packet[41] = 2U;
    return e1000_send_frame(packet, sizeof(packet));
}
