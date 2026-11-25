#include "rtl8139.h"

#include "memory.h"
#include "netdev.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define RTL8139_VENDOR 0x10ECU
#define RTL8139_DEVICE 0x8139U
#define RTL8139_RX_RING_BYTES 8192U
#define RTL8139_RX_WRAP_BYTES 1536U
#define RTL8139_RX_ALLOCATION_BYTES \
    (RTL8139_RX_RING_BYTES + 16U + RTL8139_RX_WRAP_BYTES)
#define RTL8139_DMA_LIMIT 0x100000000ULL
#define RTL8139_TX_DESCRIPTORS 4U
#define RTL8139_TX_BUFFER_BYTES 4096U
#define RTL8139_TX_MAX_BYTES 1792U
#define RTL8139_TX_MIN_BYTES 60U
#define RTL8139_TX_OWN (1U << 13)
#define RTL8139_TX_OK (1U << 15)
#define RTL8139_RX_OK 0x0001U
#define RTL8139_CMD_RESET 0x10U
#define RTL8139_CMD_RX_ENABLE 0x08U
#define RTL8139_CMD_TX_ENABLE 0x04U
#define RTL8139_CMD_RX_EMPTY 0x01U
#define RTL8139_RCR_ACCEPT_PHYSICAL 0x02U
#define RTL8139_RCR_ACCEPT_MULTICAST 0x04U
#define RTL8139_RCR_ACCEPT_BROADCAST 0x08U
#define RTL8139_RCR_WRAP 0x80U

#define RTL8139_REG_MAC0 0x00U
#define RTL8139_REG_TX_STATUS0 0x10U
#define RTL8139_REG_TX_ADDRESS0 0x20U
#define RTL8139_REG_RX_BUFFER 0x30U
#define RTL8139_REG_COMMAND 0x37U
#define RTL8139_REG_CAPR 0x38U
#define RTL8139_REG_INTERRUPT_MASK 0x3CU
#define RTL8139_REG_INTERRUPT_STATUS 0x3EU
#define RTL8139_REG_TX_CONFIG 0x40U
#define RTL8139_REG_RX_CONFIG 0x44U
#define RTL8139_REG_CONFIG1 0x52U
#define RTL8139_REG_MEDIA_STATUS 0x58U
#define RTL8139_MEDIA_LINK_FAIL 0x04U

struct rtl8139_state {
    bool ready;
    uint16_t io_base;
    uint64_t rx_physical;
    uint64_t rx_pages;
    volatile uint8_t *rx_memory;
    uint16_t rx_offset;
    uint64_t tx_physical[RTL8139_TX_DESCRIPTORS];
    volatile uint8_t *tx_memory[RTL8139_TX_DESCRIPTORS];
    uint8_t tx_index;
    uint8_t mac[6];
};

static struct rtl8139_state state;

static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline void io_out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline void io_out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint16_t io_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void copy_to_dma(volatile uint8_t *destination,
                        const uint8_t *source, size_t length) {
    for (size_t i = 0U; i < length; ++i) destination[i] = source[i];
}

static bool valid_mac(const uint8_t mac[6]) {
    uint8_t nonzero = 0U;
    for (size_t i = 0U; i < 6U; ++i) nonzero |= mac[i];
    return nonzero != 0U && (mac[0] & 1U) == 0U;
}

static const struct nexus_pci_device *find_device(void) {
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device != NULL && device->vendor_id == RTL8139_VENDOR &&
            device->device_id == RTL8139_DEVICE &&
            device->class_code == 0x02U) return device;
    }
    return NULL;
}

static void release_dma(void) {
    state.ready = false;
    if (state.rx_physical != 0U && state.rx_pages != 0U)
        (void)memory_free_contiguous_pages(state.rx_physical, state.rx_pages);
    state.rx_physical = 0U;
    state.rx_pages = 0U;
    state.rx_memory = NULL;
    for (size_t i = 0U; i < RTL8139_TX_DESCRIPTORS; ++i) {
        if (state.tx_physical[i] != 0U)
            (void)memory_free_page(state.tx_physical[i]);
        state.tx_physical[i] = 0U;
        state.tx_memory[i] = NULL;
    }
}

static uint16_t ring_read16(uint16_t offset) {
    const uint16_t index = (uint16_t)(offset & (RTL8139_RX_RING_BYTES - 1U));
    return (uint16_t)state.rx_memory[index] |
           ((uint16_t)state.rx_memory[index + 1U] << 8);
}

static bool rtl8139_link_up(void *context) {
    (void)context;
    return state.ready &&
           (io_in8((uint16_t)(state.io_base + RTL8139_REG_MEDIA_STATUS)) &
            RTL8139_MEDIA_LINK_FAIL) == 0U;
}

static bool rtl8139_send(void *context, const void *frame, size_t length) {
    (void)context;
    if (!state.ready || frame == NULL ||
        length < NEXUS_NETDEV_MIN_FRAME || length > RTL8139_TX_MAX_BYTES)
        return false;

    const uint8_t slot = state.tx_index;
    const uint16_t status_port = (uint16_t)(
        state.io_base + RTL8139_REG_TX_STATUS0 + (uint16_t)slot * 4U);
    bool idle = false;
    for (uint32_t spin = 0U; spin < 2000000U; ++spin) {
        if ((io_in32(status_port) & RTL8139_TX_OWN) != 0U) {
            idle = true;
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    if (!idle) return false;

    size_t wire_length = length < RTL8139_TX_MIN_BYTES
        ? RTL8139_TX_MIN_BYTES : length;
    for (size_t i = 0U; i < wire_length; ++i)
        state.tx_memory[slot][i] = 0U;
    copy_to_dma(state.tx_memory[slot], (const uint8_t *)frame, length);
    __asm__ volatile ("mfence" ::: "memory");

    /* Writing SIZE with OWN cleared starts DMA; hardware sets OWN when the
     * buffer has been consumed and TOK after the frame is transmitted.
     */
    io_out32(status_port, (uint32_t)wire_length);
    bool dma_complete = false;
    bool transmit_ok = false;
    for (uint32_t spin = 0U; spin < 5000000U; ++spin) {
        const uint32_t status = io_in32(status_port);
        if ((status & RTL8139_TX_OWN) != 0U) {
            dma_complete = true;
            transmit_ok = (status & RTL8139_TX_OK) != 0U;
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    if (!dma_complete || !transmit_ok) return false;
    state.tx_index = (uint8_t)((slot + 1U) % RTL8139_TX_DESCRIPTORS);
    return true;
}

static bool rtl8139_receive(void *context, void *buffer,
                            size_t capacity, size_t *length) {
    (void)context;
    if (length != NULL) *length = 0U;
    if (!state.ready || buffer == NULL || length == NULL ||
        capacity < NEXUS_NETDEV_MIN_FRAME) return false;

    /* Drain at most one complete packet per poll, then leave the next one
     * queued for the next net_poll iteration.
     */
    if ((io_in8((uint16_t)(state.io_base + RTL8139_REG_COMMAND)) &
         RTL8139_CMD_RX_EMPTY) != 0U) return false;

    const uint16_t header_status = ring_read16(state.rx_offset);
    const uint16_t packet_length = ring_read16((uint16_t)(state.rx_offset + 2U));
    const uint16_t payload_length = packet_length >= 4U
        ? (uint16_t)(packet_length - 4U) : 0U;
    const uint32_t next_unwrapped = (uint32_t)state.rx_offset + 4U +
                                   (uint32_t)packet_length;
    const uint16_t next_offset = (uint16_t)((next_unwrapped + 3U) & ~3U);
    const uint16_t old_offset = state.rx_offset;
    state.rx_offset = (uint16_t)(next_offset &
                                 (RTL8139_RX_RING_BYTES - 1U));

    const bool packet_valid = (header_status & RTL8139_RX_OK) != 0U &&
        payload_length >= NEXUS_NETDEV_MIN_FRAME &&
        payload_length <= RTL8139_TX_MAX_BYTES &&
        payload_length <= capacity &&
        packet_length <= RTL8139_TX_MAX_BYTES + 4U &&
        next_unwrapped + 3U <= RTL8139_RX_ALLOCATION_BYTES;

    if (packet_valid) {
        uint8_t *destination = (uint8_t *)buffer;
        const volatile uint8_t *source =
            state.rx_memory + old_offset + 4U;
        for (size_t i = 0U; i < payload_length; ++i)
            destination[i] = source[i];
        *length = payload_length;
    }

    /* CAPR is the last consumed byte minus 16. */
    io_out16((uint16_t)(state.io_base + RTL8139_REG_CAPR),
             (uint16_t)(state.rx_offset - 16U));
    return packet_valid;
}

static const struct nexus_netdev_ops network_ops = {
    .send_frame = rtl8139_send,
    .receive_frame = rtl8139_receive,
    .link_up = rtl8139_link_up
};

bool rtl8139_init(void) {
    state = (struct rtl8139_state){0};
    const struct nexus_pci_device *device = find_device();
    if (device == NULL) {
        serial_write("RTL8139: no supported Realtek 10EC:8139 device found
");
        return false;
    }

    struct nexus_pci_bar bar0;
    if (!pci_decode_bar(device, 0U, &bar0) || !bar0.present ||
        !bar0.assigned || !bar0.io_space || bar0.base > 0xFFFFU ||
        bar0.base + 0x80U > 0x10000U ||
        !pci_enable_io_bus_master(device)) {
        serial_write("RTL8139: I/O BAR or PCI bus-master setup failed
");
        return false;
    }
    state.io_base = (uint16_t)bar0.base;

    /* Power on the PHY, reset the device, and keep it in polling mode. */
    io_out8((uint16_t)(state.io_base + RTL8139_REG_CONFIG1), 0U);
    io_out8((uint16_t)(state.io_base + RTL8139_REG_COMMAND),
            RTL8139_CMD_RESET);
    bool reset_complete = false;
    for (uint32_t spin = 0U; spin < 2000000U; ++spin) {
        if ((io_in8((uint16_t)(state.io_base + RTL8139_REG_COMMAND)) &
             RTL8139_CMD_RESET) == 0U) {
            reset_complete = true;
            break;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    if (!reset_complete) {
        serial_write("RTL8139: software reset timed out
");
        return false;
    }

    for (size_t i = 0U; i < sizeof(state.mac); ++i)
        state.mac[i] = io_in8((uint16_t)(state.io_base + RTL8139_REG_MAC0 + i));
    if (!valid_mac(state.mac)) {
        serial_write("RTL8139: adapter reported an invalid MAC address
");
        return false;
    }

    state.rx_pages = (RTL8139_RX_ALLOCATION_BYTES + NEXUS_PAGE_SIZE - 1U) /
                     NEXUS_PAGE_SIZE;
    if (!memory_alloc_contiguous_pages(state.rx_pages, RTL8139_DMA_LIMIT,
                                       &state.rx_physical)) {
        serial_write("RTL8139: low-memory RX ring allocation failed
");
        return false;
    }
    state.rx_memory = (volatile uint8_t *)memory_phys_to_virt(state.rx_physical);
    if (state.rx_memory == NULL) {
        (void)memory_free_contiguous_pages(state.rx_physical, state.rx_pages);
        state.rx_physical = 0U;
        state.rx_pages = 0U;
        return false;
    }
    for (size_t i = 0U; i < RTL8139_RX_ALLOCATION_BYTES; ++i)
        state.rx_memory[i] = 0U;

    for (size_t i = 0U; i < RTL8139_TX_DESCRIPTORS; ++i) {
        if (!memory_alloc_dma_page(RTL8139_DMA_LIMIT, &state.tx_physical[i])) {
            release_dma();
            serial_write("RTL8139: low-memory TX buffer allocation failed
");
            return false;
        }
        state.tx_memory[i] =
            (volatile uint8_t *)memory_phys_to_virt(state.tx_physical[i]);
        if (state.tx_memory[i] == NULL) {
            release_dma();
            serial_write("RTL8139: TX buffer mapping failed
");
            return false;
        }
        for (size_t j = 0U; j < RTL8139_TX_BUFFER_BYTES; ++j)
            state.tx_memory[i][j] = 0U;
        io_out32((uint16_t)(state.io_base + RTL8139_REG_TX_ADDRESS0 + i * 4U),
                 (uint32_t)state.tx_physical[i]);
    }

    io_out16((uint16_t)(state.io_base + RTL8139_REG_INTERRUPT_MASK), 0U);
    io_out16((uint16_t)(state.io_base + RTL8139_REG_INTERRUPT_STATUS), 0xFFFFU);
    io_out32((uint16_t)(state.io_base + RTL8139_REG_RX_BUFFER),
             (uint32_t)state.rx_physical);
    io_out32((uint16_t)(state.io_base + RTL8139_REG_TX_CONFIG), 0x03000700U);
    io_out32((uint16_t)(state.io_base + RTL8139_REG_RX_CONFIG),
             RTL8139_RCR_ACCEPT_PHYSICAL |
             RTL8139_RCR_ACCEPT_MULTICAST |
             RTL8139_RCR_ACCEPT_BROADCAST |
             RTL8139_RCR_WRAP);
    io_out16((uint16_t)(state.io_base + RTL8139_REG_CAPR), 0xFFF0U);
    io_out8((uint16_t)(state.io_base + RTL8139_REG_COMMAND),
            RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    state.ready = true;

    if (!netdev_register("rtl8139", NEXUS_NETDEV_MEDIUM_ETHERNET,
                         state.mac, RTL8139_TX_MAX_BYTES,
                         &network_ops, NULL)) {
        io_out8((uint16_t)(state.io_base + RTL8139_REG_COMMAND), 0U);
        release_dma();
        serial_write("RTL8139: shared Ethernet stack rejected the adapter
");
        return false;
    }
    serial_write("RTL8139: Realtek 10EC:8139 registered with 8 KiB polling RX ring
");
    return true;
}

bool rtl8139_ready(void) {
    return state.ready;
}
