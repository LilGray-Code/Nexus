#include "xhci.h"
#include "xhci_speed.h"
#include "xhci_ownership.h"
#include "xhci_ports.h"
#include "xhci_power.h"
#include "usb_host_controller.h"

#include "input.h"
#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"
#include "usb_hid.h"
#include "usb_hid_generic.h"
#include "usb_hid_interfaces.h"
#include "usb_hub.h"
#include "usb_hub_requests.h"

#include <stddef.h>
#include <stdint.h>

#define XHCI_DMA_LIMIT          0x100000000ULL
#define XHCI_PAGE_SIZE          4096U
#define XHCI_RING_ENTRIES       256U
#define XHCI_RING_USABLE        255U
#define XHCI_MAX_MMIO_PAGES     12U
#define XHCI_MAX_ROOT_PORTS     NEXUS_XHCI_MAX_ROOT_PORTS
#define XHCI_MAX_KEYBOARDS      32U
#define XHCI_MAX_MICE           32U
#define XHCI_MAX_GAMEPADS      32U
#define XHCI_MAX_HUBS          16U
#define XHCI_MAX_SCRATCHPADS    512U
#define XHCI_POLL_LIMIT         5000000U
#define XHCI_BIOS_HANDOFF_TIMEOUT 5000000U
#define XHCI_COMMAND_TIMEOUT    5000000U
#define XHCI_TRANSFER_TIMEOUT   5000000U
_Static_assert(XHCI_MAX_KEYBOARDS <= XHCI_MAX_ROOT_PORTS,
               "keyboard slots cannot exceed xHCI root-port address space");
_Static_assert(XHCI_MAX_MICE <= XHCI_MAX_ROOT_PORTS,
               "mouse slots cannot exceed xHCI root-port address space");
_Static_assert(XHCI_MAX_GAMEPADS <= XHCI_MAX_ROOT_PORTS,
               "gamepad slots cannot exceed xHCI root-port address space");

#define XHCI_TRB_TYPE(v)        ((uint32_t)(v) << 10)
#define XHCI_TRB_CYCLE          (1U << 0)
#define XHCI_TRB_TOGGLE_CYCLE   (1U << 1)
#define XHCI_TRB_ISP            (1U << 2)
#define XHCI_TRB_CHAIN          (1U << 4)
#define XHCI_TRB_IOC            (1U << 5)
#define XHCI_TRB_IDT            (1U << 6)
#define XHCI_TRB_DIR_IN         (1U << 16)

#define XHCI_TRB_LINK           6U
#define XHCI_TRB_ENABLE_SLOT    9U
#define XHCI_TRB_DISABLE_SLOT  10U
#define XHCI_TRB_ADDRESS_DEVICE 11U
#define XHCI_TRB_CONFIGURE_EP  12U
#define XHCI_TRB_EVALUATE_CTX  13U
#define XHCI_TRB_TRANSFER_EVENT 32U
#define XHCI_TRB_COMMAND_EVENT  33U
#define XHCI_TRB_PORT_EVENT     34U

#define XHCI_CC_SUCCESS         1U
#define XHCI_CC_SHORT_PACKET   13U

#define XHCI_USBCMD_RUN         (1U << 0)
#define XHCI_USBCMD_RESET       (1U << 1)
#define XHCI_USBSTS_HALTED      (1U << 0)
#define XHCI_USBSTS_CNR         (1U << 11)
#define XHCI_PORT_CCS           (1U << 0)
#define XHCI_PORT_PED           (1U << 1)
#define XHCI_PORT_PR            (1U << 4)
#define XHCI_PORT_PP            (1U << 9)
#define XHCI_PORT_SPEED_MASK    (0x0FU << 10)
#define XHCI_PORT_WPR           (1U << 31)
#define XHCI_PORT_CHANGE_MASK   ((1U << 17) | (1U << 18) | (1U << 19) | \
                                 (1U << 20) | (1U << 21) | (1U << 22) | \
                                 (1U << 23))

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed, aligned(16)));

_Static_assert(sizeof(struct xhci_trb) == 16U, "xHCI TRBs are 16 bytes");

struct xhci_erst_entry {
    uint64_t ring_segment_base;
    uint16_t ring_segment_size;
    uint16_t reserved0;
    uint32_t reserved1;
} __attribute__((packed));

_Static_assert(sizeof(struct xhci_erst_entry) == 16U,
               "xHCI ERST entry is 16 bytes");

struct xhci_ring {
    volatile struct xhci_trb *trbs;
    uint64_t physical;
    uint16_t enqueue;
    uint8_t cycle;
};

struct xhci_mmio_mapping {
    uint64_t physical_page;
    volatile uint32_t *virtual_page;
};

enum xhci_hid_kind {
    XHCI_HID_NONE = 0,
    XHCI_HID_KEYBOARD = 1,
    XHCI_HID_MOUSE = 2,
    XHCI_HID_GAMEPAD = 3,
    XHCI_HID_HUB = 4
};

struct xhci_hid_device {
    bool active;
    enum xhci_hid_kind kind;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint8_t port_number;
    struct nexus_usb_path usb_path;
    uint8_t speed;
    struct nexus_usb_hub_descriptor hub_descriptor;
    uint8_t hub_scan_cursor;
    bool hub_port_connected[NEXUS_USB_HUB_MAX_PORTS + 1U];
    uint16_t max_packet;
    uint16_t vendor_id;
    uint16_t product_id;
    struct nexus_usb_gamepad_layout gamepad_layout;
    struct nexus_usb_keyboard_state keyboard_state;
    struct nexus_usb_hid_generic_multi_layout generic_multi_layout;
    bool generic_report_protocol;
    bool secondary_active;
    enum xhci_hid_kind secondary_kind;
    uint8_t secondary_endpoint_id;
    uint16_t secondary_max_packet;
    struct nexus_usb_keyboard_state secondary_keyboard_state;
    struct nexus_usb_hid_generic_multi_layout secondary_generic_multi_layout;
    bool secondary_generic_report_protocol;
    uint64_t secondary_report_buffer_physical;
    volatile uint8_t *secondary_report_buffer;
    struct xhci_ring secondary_interrupt_ring;
    uint64_t device_context_physical;
    uint64_t input_context_physical;
    uint64_t report_buffer_physical;
    volatile uint8_t *report_buffer;
    struct xhci_ring ep0_ring;
    struct xhci_ring interrupt_ring;
};

struct xhci_controller {
    bool initialized;
    const struct nexus_pci_device *pci_device;
    uint64_t bar_base;
    uint8_t cap_length;
    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t context_size;
    uint16_t version;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hccparams1;
    uint32_t scratchpad_count;
    uint32_t doorbell_offset;
    uint32_t runtime_offset;
    volatile uint64_t *dcbaa;
    uint64_t dcbaa_physical;
    uint64_t scratchpad_array_physical;
    struct xhci_ring command_ring;
    volatile struct xhci_trb *event_ring;
    uint64_t event_ring_physical;
    uint16_t event_dequeue;
    uint8_t event_cycle;
    uint64_t last_root_scan_tick;
    /* Last observed CCS state prevents periodic polling from repeatedly
     * resetting unrelated connected devices after failed HID probing. */
    bool root_port_connected[NEXUS_XHCI_MAX_ROOT_PORTS + 1U];
    uint64_t erst_physical;
    struct xhci_mmio_mapping mappings[XHCI_MAX_MMIO_PAGES];
    size_t mapping_count;
    struct xhci_hid_device keyboards[XHCI_MAX_KEYBOARDS];
    struct xhci_hid_device mice[XHCI_MAX_MICE];
    struct xhci_hid_device gamepads[XHCI_MAX_GAMEPADS];
    struct xhci_hid_device hubs[XHCI_MAX_HUBS];
};

static struct xhci_controller controller;

static bool xhci_alloc_page(uint64_t *physical, volatile uint8_t **virtual_address) {
    if (physical == NULL || virtual_address == NULL ||
        !memory_alloc_dma_page(XHCI_DMA_LIMIT, physical)) return false;
    volatile uint8_t *page =
        (volatile uint8_t *)memory_phys_to_virt(*physical);
    if (page == NULL || *physical > UINT32_MAX - (XHCI_PAGE_SIZE - 1U)) {
        (void)memory_free_page(*physical);
        return false;
    }
    for (size_t i = 0U; i < XHCI_PAGE_SIZE; ++i) page[i] = 0U;
    *virtual_address = page;
    return true;
}

static volatile uint32_t *xhci_map_register(struct xhci_controller *xhci,
                                             uint64_t offset) {
    if (xhci == NULL || xhci->bar_base > UINT64_MAX - offset) return NULL;
    const uint64_t physical = xhci->bar_base + offset;
    const uint64_t page = physical & ~(uint64_t)(XHCI_PAGE_SIZE - 1U);
    for (size_t i = 0U; i < xhci->mapping_count; ++i) {
        if (xhci->mappings[i].physical_page == page) {
            return (volatile uint32_t *)((uintptr_t)xhci->mappings[i].virtual_page +
                                         (uintptr_t)(physical & 0xFFFU));
        }
    }
    if (xhci->mapping_count >= XHCI_MAX_MMIO_PAGES) return NULL;

    volatile uint32_t *mapped = NULL;
    if (!paging_map_mmio_page(page, &mapped) || mapped == NULL) return NULL;
    xhci->mappings[xhci->mapping_count].physical_page = page;
    xhci->mappings[xhci->mapping_count].virtual_page = mapped;
    ++xhci->mapping_count;
    return (volatile uint32_t *)((uintptr_t)mapped +
                                 (uintptr_t)(physical & 0xFFFU));
}

static bool xhci_read32(struct xhci_controller *xhci, uint64_t offset,
                        uint32_t *value) {
    volatile uint32_t *reg = xhci_map_register(xhci, offset);
    if (reg == NULL || value == NULL) return false;
    *value = *reg;
    return *value != UINT32_MAX;
}

static bool xhci_read8(struct xhci_controller *xhci, uint64_t offset,
                       uint8_t *value) {
    volatile uint32_t *reg = xhci_map_register(xhci, offset);
    if (reg == NULL || value == NULL) return false;
    *value = *(volatile uint8_t *)(uintptr_t)reg;
    return *value != UINT8_MAX;
}

static bool xhci_write8(struct xhci_controller *xhci, uint64_t offset,
                        uint8_t value) {
    volatile uint32_t *reg = xhci_map_register(xhci, offset);
    if (reg == NULL) return false;
    *(volatile uint8_t *)(uintptr_t)reg = value;
    __asm__ volatile ("sfence" : : : "memory");
    return true;
}

static bool xhci_write32(struct xhci_controller *xhci, uint64_t offset,
                         uint32_t value) {
    volatile uint32_t *reg = xhci_map_register(xhci, offset);
    if (reg == NULL) return false;
    *reg = value;
    __asm__ volatile ("sfence" : : : "memory");
    return true;
}

static bool xhci_write64(struct xhci_controller *xhci, uint64_t offset,
                         uint64_t value) {
    /*
     * Program the low dword before the high dword. xHCI controllers latch
     * paired 64-bit registers when the high dword is written: CRCR starts
     * the command ring, ERSTBA loads the event-ring segment table, and ERDP
     * refreshes the event dequeue state. Writing high first latches stale
     * low-dword values and can make the controller raise HCE.
     */
    if (!xhci_write32(xhci, offset, (uint32_t)value)) return false;
    return xhci_write32(xhci, offset + 4U, (uint32_t)(value >> 32));
}

static bool xhci_wait_register(struct xhci_controller *xhci, uint64_t offset,
                               uint32_t mask, bool set) {
    for (uint32_t spin = 0U; spin < XHCI_POLL_LIMIT; ++spin) {
        uint32_t value;
        if (!xhci_read32(xhci, offset, &value)) return false;
        if (((value & mask) != 0U) == set) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static void xhci_delay(void) {
    for (volatile uint32_t i = 0U; i < 100000U; ++i)
        __asm__ volatile ("pause");
}

static bool xhci_ring_init(struct xhci_ring *ring) {
    if (ring == NULL) return false;
    volatile uint8_t *page = NULL;
    if (!xhci_alloc_page(&ring->physical, &page)) return false;
    ring->trbs = (volatile struct xhci_trb *)(uintptr_t)page;
    ring->enqueue = 0U;
    ring->cycle = 1U;
    ring->trbs[XHCI_RING_USABLE].parameter = ring->physical;
    ring->trbs[XHCI_RING_USABLE].status = 0U;
    ring->trbs[XHCI_RING_USABLE].control =
        XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    return true;
}

static volatile struct xhci_trb *xhci_ring_push(struct xhci_ring *ring,
                                                uint64_t parameter,
                                                uint32_t status,
                                                uint32_t control) {
    if (ring == NULL || ring->trbs == NULL) return NULL;
    if (ring->enqueue >= XHCI_RING_USABLE) {
        volatile struct xhci_trb *link = &ring->trbs[XHCI_RING_USABLE];
        link->parameter = ring->physical;
        link->status = 0U;
        link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
                        XHCI_TRB_TOGGLE_CYCLE |
                        (ring->cycle != 0U ? XHCI_TRB_CYCLE : 0U);
        ring->enqueue = 0U;
        ring->cycle ^= 1U;
    }

    volatile struct xhci_trb *trb = &ring->trbs[ring->enqueue++];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (ring->cycle != 0U ? XHCI_TRB_CYCLE : 0U);
    __asm__ volatile ("sfence" : : : "memory");
    return trb;
}

static bool xhci_event_next(struct xhci_controller *xhci,
                            struct xhci_trb *event) {
    if (xhci == NULL || event == NULL || xhci->event_ring == NULL) return false;
    volatile struct xhci_trb *source = &xhci->event_ring[xhci->event_dequeue];
    __asm__ volatile ("lfence" : : : "memory");
    const uint32_t control = source->control;
    if ((control & XHCI_TRB_CYCLE) !=
        (xhci->event_cycle != 0U ? XHCI_TRB_CYCLE : 0U)) return false;

    event->parameter = source->parameter;
    event->status = source->status;
    event->control = control;
    ++xhci->event_dequeue;
    if (xhci->event_dequeue >= XHCI_RING_ENTRIES) {
        xhci->event_dequeue = 0U;
        xhci->event_cycle ^= 1U;
    }

    const uint64_t dequeue_address =
        (xhci->event_ring_physical +
         (uint64_t)xhci->event_dequeue * sizeof(struct xhci_trb)) |
        (1ULL << 3);
    (void)xhci_write64(xhci, (uint64_t)xhci->runtime_offset + 0x38U,
                       dequeue_address);
    return true;
}

static uint8_t xhci_event_type(const struct xhci_trb *event) {
    return (uint8_t)((event->control >> 10) & 0x3FU);
}

static uint8_t xhci_event_code(const struct xhci_trb *event) {
    return (uint8_t)(event->status >> 24);
}

static bool xhci_command(struct xhci_controller *xhci, uint64_t parameter,
                         uint32_t control, struct xhci_trb *completion) {
    if (xhci == NULL || !xhci->initialized) return false;
    if (xhci_ring_push(&xhci->command_ring, parameter, 0U,
                       control) == NULL) return false;
    volatile uint32_t *doorbell = xhci_map_register(
        xhci, (uint64_t)xhci->doorbell_offset);
    if (doorbell == NULL) return false;
    *doorbell = 0U;
    __asm__ volatile ("sfence" : : : "memory");

    for (uint32_t spin = 0U; spin < XHCI_COMMAND_TIMEOUT; ++spin) {
        struct xhci_trb event;
        if (!xhci_event_next(xhci, &event)) {
            __asm__ volatile ("pause");
            continue;
        }
        if (xhci_event_type(&event) != XHCI_TRB_COMMAND_EVENT) continue;
        if (completion != NULL) *completion = event;
        if (xhci_event_code(&event) != XHCI_CC_SUCCESS) {
            const uint16_t last_index = xhci->command_ring.enqueue == 0U
                ? (uint16_t)(XHCI_RING_USABLE - 1U)
                : (uint16_t)(xhci->command_ring.enqueue - 1U);
            const volatile struct xhci_trb *submitted =
                &xhci->command_ring.trbs[last_index];
            serial_write("xHCI: command completion error code=");
            serial_write_hex(xhci_event_code(&event));
            serial_write(" command type=");
            serial_write_hex((submitted->control >> 10) & 0x3FU);
            serial_write(" slot=");
            serial_write_hex((submitted->control >> 24) & 0xFFU);
            serial_write(" command parameter=");
            serial_write_hex(submitted->parameter);
            serial_write(" event parameter=");
            serial_write_hex(event.parameter);
            serial_write(" context size=");
            serial_write_hex(xhci->context_size);
            serial_write("\r\n");
            return false;
        }
        return true;
    }
    serial_write("xHCI: command timed out; last command TRB control=");
    serial_write_hex(xhci->command_ring.trbs[
        xhci->command_ring.enqueue == 0U
            ? XHCI_RING_USABLE - 1U : xhci->command_ring.enqueue - 1U].control);
    uint32_t value = 0U;
    if (xhci_read32(xhci, (uint64_t)xhci->cap_length + 4U, &value)) {
        serial_write(" USBSTS=");
        serial_write_hex(value);
    }
    if (xhci_read32(xhci, (uint64_t)xhci->cap_length + 0x18U, &value)) {
        serial_write(" CRCR_LO=");
        serial_write_hex(value);
    }
    if (xhci_read32(xhci, (uint64_t)xhci->runtime_offset + 0x28U, &value)) {
        serial_write(" ERSTSZ=");
        serial_write_hex(value);
    }
    if (xhci_read32(xhci, (uint64_t)xhci->runtime_offset + 0x38U, &value)) {
        serial_write(" ERDP_LO=");
        serial_write_hex(value);
    }
    if (xhci->event_ring != NULL) {
        serial_write(" EVENT_CTRL=");
        serial_write_hex(xhci->event_ring[xhci->event_dequeue].control);
        serial_write(" EVENT_STATUS=");
        serial_write_hex(xhci->event_ring[xhci->event_dequeue].status);
    }
    serial_write("\r\n");
    return false;
}

static bool xhci_wait_transfer(struct xhci_controller *xhci,
                               uint8_t slot_id, uint8_t endpoint_id,
                               struct xhci_trb *completion) {
    for (uint32_t spin = 0U; spin < XHCI_TRANSFER_TIMEOUT; ++spin) {
        struct xhci_trb event;
        if (!xhci_event_next(xhci, &event)) {
            __asm__ volatile ("pause");
            continue;
        }
        if (xhci_event_type(&event) != XHCI_TRB_TRANSFER_EVENT) continue;
        const uint8_t event_slot = (uint8_t)(event.control >> 24);
        const uint8_t event_ep = (uint8_t)((event.control >> 16) & 0x1FU);
        if (event_slot != slot_id || event_ep != endpoint_id) continue;
        if (completion != NULL) *completion = event;
        const uint8_t code = xhci_event_code(&event);
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_SHORT_PACKET) {
            serial_write("xHCI: transfer completion error code=");
            serial_write_hex(code);
            serial_write("\r\n");
            return false;
        }
        return true;
    }
    serial_write("xHCI: control transfer timed out\r\n");
    return false;
}

static bool xhci_control_transfer(struct xhci_controller *xhci,
                                  struct xhci_hid_device *device,
                                  uint8_t request_type, uint8_t request,
                                  uint16_t value, uint16_t index,
                                  void *buffer, uint16_t length) {
    if (xhci == NULL || device == NULL || !device->active) return false;
    uint64_t setup = 0U;
    uint8_t *setup_bytes = (uint8_t *)&setup;
    setup_bytes[0] = request_type;
    setup_bytes[1] = request;
    setup_bytes[2] = (uint8_t)value;
    setup_bytes[3] = (uint8_t)(value >> 8);
    setup_bytes[4] = (uint8_t)index;
    setup_bytes[5] = (uint8_t)(index >> 8);
    setup_bytes[6] = (uint8_t)length;
    setup_bytes[7] = (uint8_t)(length >> 8);

    const uint32_t transfer_type = length == 0U ? 0U :
        ((request_type & 0x80U) != 0U ? 3U : 2U);
    const uint32_t setup_control = XHCI_TRB_TYPE(2U) | XHCI_TRB_IDT |
        XHCI_TRB_CHAIN | (transfer_type << 16);
    if (xhci_ring_push(&device->ep0_ring, setup, 8U, setup_control) == NULL)
        return false;

    if (length != 0U) {
        if (buffer == NULL) return false;
        const uint64_t physical = device->report_buffer_physical;
        const uint32_t data_control = XHCI_TRB_TYPE(3U) | XHCI_TRB_CHAIN |
            (((request_type & 0x80U) != 0U) ? XHCI_TRB_DIR_IN : 0U);
        /* Descriptor/control data shares the DMA buffer used by HID reports. */
        if (xhci_ring_push(&device->ep0_ring, physical, length,
                           data_control) == NULL) return false;
    }

    for (uint16_t i = 0U; i < length; ++i)
        device->report_buffer[i] = 0U;

    const bool status_in = length == 0U ||
                           (request_type & 0x80U) == 0U;
    const uint32_t status_control = XHCI_TRB_TYPE(4U) | XHCI_TRB_IOC |
        (status_in ? XHCI_TRB_DIR_IN : 0U);
    if (xhci_ring_push(&device->ep0_ring, 0U, 0U, status_control) == NULL)
        return false;

    volatile uint32_t *doorbell = xhci_map_register(
        xhci, (uint64_t)xhci->doorbell_offset + 4U * device->slot_id);
    if (doorbell == NULL) return false;
    *doorbell = 1U; /* Device slot, control endpoint DCI=1. */
    __asm__ volatile ("sfence" : : : "memory");

    struct xhci_trb completion;
    if (!xhci_wait_transfer(xhci, device->slot_id, 1U, &completion))
        return false;
    if (length != 0U) {
        const uint32_t residual = completion.status & 0x00FFFFFFU;
        if (residual > length) return false;
        const size_t transferred = (size_t)length - residual;
        if (transferred == 0U) return false;
        (void)transferred;
        if (buffer != (void *)(uintptr_t)device->report_buffer) {
            volatile uint8_t *source = device->report_buffer;
            uint8_t *destination = (uint8_t *)buffer;
            for (size_t i = 0U; i < transferred; ++i)
                destination[i] = source[i];
        }
    }
    return true;
}

static bool xhci_disable_slot(struct xhci_controller *xhci, uint8_t slot) {
    if (slot == 0U) return true;
    return xhci_command(xhci, 0U,
                        XHCI_TRB_TYPE(XHCI_TRB_DISABLE_SLOT) |
                        ((uint32_t)slot << 24), NULL);
}

static bool xhci_claim_firmware(struct xhci_controller *xhci,
                                uint32_t hccparams) {
    uint32_t offset = ((hccparams >> 16) & 0xFFFFU) << 2;
    if (offset == 0U) return true;

    for (uint32_t count = 0U; count < 64U && offset < 0x10000U; ++count) {
        uint32_t capability;
        if (!xhci_read32(xhci, offset, &capability)) return false;
        if ((capability & 0xFFU) == 1U) {
            const uint32_t owned = capability | (1U << 24);
            if (!xhci_write32(xhci, offset, owned)) return false;
            for (uint32_t spin = 0U; spin < XHCI_POLL_LIMIT; ++spin) {
                if (!xhci_read32(xhci, offset, &capability)) return false;
                if ((capability & (1U << 16)) == 0U) break;
                if (spin + 1U == XHCI_POLL_LIMIT) {
                    serial_write("xHCI: BIOS ownership handoff timed out\r\n");
                    return false;
                }
            }
            /* Disable firmware SMI generation after OS ownership is acquired. */
            if (!xhci_write32(xhci, offset + 4U, 0U)) return false;
            return true;
        }
        const uint32_t next = (capability >> 8) & 0xFFU;
        if (next == 0U) break;
        offset += next * 4U;
    }
    return true;
}

static bool xhci_allocate_controller_memory(struct xhci_controller *xhci,
                                             uint32_t hcsparams2) {
    volatile uint8_t *page = NULL;
    if (!xhci_alloc_page(&xhci->dcbaa_physical, &page)) return false;
    xhci->dcbaa = (volatile uint64_t *)(uintptr_t)page;

    if (!xhci_ring_init(&xhci->command_ring)) return false;

    if (!xhci_alloc_page(&xhci->event_ring_physical, &page)) return false;
    xhci->event_ring = (volatile struct xhci_trb *)(uintptr_t)page;
    xhci->event_dequeue = 0U;
    xhci->event_cycle = 1U;

    uint64_t erst_physical = 0U;
    if (!xhci_alloc_page(&erst_physical, &page)) return false;
    xhci->erst_physical = erst_physical;
    struct xhci_erst_entry *erst = (struct xhci_erst_entry *)(uintptr_t)page;
    erst[0].ring_segment_base = xhci->event_ring_physical;
    erst[0].ring_segment_size = XHCI_RING_ENTRIES;
    erst[0].reserved0 = 0U;
    erst[0].reserved1 = 0U;

    const uint32_t scratchpad_hi = (hcsparams2 >> 21) & 0x1FU;
    const uint32_t scratchpad_lo = (hcsparams2 >> 27) & 0x1FU;
    const uint32_t scratchpad_count = (scratchpad_hi << 5) | scratchpad_lo;
    xhci->scratchpad_count = scratchpad_count;
    if (scratchpad_count > XHCI_MAX_SCRATCHPADS) {
        serial_write("xHCI: scratchpad requirement exceeds supported bound\r\n");
        return false;
    }
    if (scratchpad_count != 0U) {
        uint64_t array_physical = 0U;
        if (!xhci_alloc_page(&array_physical, &page)) return false;
        xhci->scratchpad_array_physical = array_physical;
        volatile uint64_t *array = (volatile uint64_t *)(uintptr_t)page;
        for (uint32_t i = 0U; i < scratchpad_count; ++i) {
            volatile uint8_t *scratch_page = NULL;
            uint64_t scratch_physical = 0U;
            if (!xhci_alloc_page(&scratch_physical, &scratch_page)) return false;
            array[i] = scratch_physical;
        }
        xhci->dcbaa[0] = array_physical;
    }
    return true;
}

static bool xhci_start_controller(struct xhci_controller *xhci,
                                  uint32_t hcsparams1,
                                  uint32_t hcsparams2,
                                  uint32_t hccparams) {
    uint32_t command;
    if (!xhci_claim_firmware(xhci, hccparams)) return false;

    const uint64_t operational = xhci->cap_length;
    if (!xhci_read32(xhci, operational, &command)) return false;
    if (!xhci_write32(xhci, operational, command & ~XHCI_USBCMD_RUN))
        return false;
    if (!xhci_wait_register(xhci, operational + 4U,
                            XHCI_USBSTS_HALTED, true)) return false;

    if (!xhci_write32(xhci, operational, XHCI_USBCMD_RESET)) return false;
    if (!xhci_wait_register(xhci, operational, XHCI_USBCMD_RESET, false) ||
        !xhci_wait_register(xhci, operational + 4U, XHCI_USBSTS_CNR, false)) {
        serial_write("xHCI: controller reset timed out\r\n");
        return false;
    }
    xhci_delay();

    uint32_t page_size = 0U;
    if (!xhci_read32(xhci, operational + 8U, &page_size) ||
        (page_size & 1U) == 0U) {
        serial_write("xHCI: controller does not support 4 KiB pages\r\n");
        return false;
    }
    if (!xhci_allocate_controller_memory(xhci, hcsparams2)) return false;

    const uint8_t slots = (uint8_t)(hcsparams1 & 0xFFU);
    if (slots == 0U) return false;
    xhci->max_slots = slots;
    if (!xhci_write32(xhci, operational + 0x38U, 1U)) return false;
    if (!xhci_write64(xhci, operational + 0x30U, xhci->dcbaa_physical))
        return false;
    if (!xhci_write64(xhci, operational + 0x18U,
                      xhci->command_ring.physical | 1U)) return false;

    const uint64_t runtime = xhci->runtime_offset;
    if (!xhci_write32(xhci, runtime + 0x28U, 1U)) return false;
    if (!xhci_write64(xhci, runtime + 0x30U, xhci->erst_physical))
        return false;
    if (!xhci_write64(xhci, runtime + 0x38U,
                      xhci->event_ring_physical | (1ULL << 3))) return false;
    /* Polling mode: do not request an MSI/MSI-X interrupt. */
    if (!xhci_write32(xhci, runtime + 0x20U, 0U)) return false;

    if (!xhci_write32(xhci, operational, XHCI_USBCMD_RUN)) return false;
    if (!xhci_wait_register(xhci, operational + 4U,
                            XHCI_USBSTS_HALTED, false)) {
        serial_write("xHCI: controller failed to enter the running state\r\n");
        return false;
    }

    serial_write("xHCI: controller running; slots=");
    serial_write_hex(slots);
    serial_write(" ports=");
    serial_write_hex(xhci->max_ports);
    serial_write("\r\n");
    return true;
}

static bool xhci_port_reset(struct xhci_controller *xhci, uint8_t port_number,
                            uint8_t *speed) {
    uint64_t port_offset = 0U;
    uint32_t port_status;
    if (xhci == NULL || speed == NULL ||
        !nexus_xhci_port_register_offset(xhci->cap_length,
                                         port_number, &port_offset))
        return false;
    if (!xhci_read32(xhci, port_offset, &port_status) ||
        (port_status & XHCI_PORT_CCS) == 0U) return false;

    const uint32_t speed_value =
        (port_status & XHCI_PORT_SPEED_MASK) >> 10;
    if (!nexus_xhci_speed_supported((uint8_t)speed_value)) return false;

    /* Power the root port before issuing the protocol-specific reset. */
    if ((port_status & XHCI_PORT_PP) == 0U) {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | (port_status & XHCI_PORT_CHANGE_MASK)))
            return false;
        xhci_delay();
    }
    if (nexus_xhci_speed_requires_warm_reset((uint8_t)speed_value)) {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | XHCI_PORT_WPR |
                          (port_status & XHCI_PORT_CHANGE_MASK))) return false;
    } else {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | XHCI_PORT_PR |
                          (port_status & XHCI_PORT_CHANGE_MASK))) return false;
    }

    const uint32_t reset_bit = nexus_xhci_speed_requires_warm_reset((uint8_t)speed_value)
        ? XHCI_PORT_WPR : XHCI_PORT_PR;
    bool reset_done = false;
    for (uint32_t spin = 0U; spin < XHCI_POLL_LIMIT; ++spin) {
        if (!xhci_read32(xhci, port_offset, &port_status)) return false;
        if ((port_status & reset_bit) == 0U) {
            reset_done = true;
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!reset_done || (port_status & XHCI_PORT_PED) == 0U) {
        serial_write("xHCI: port reset did not enable the device\r\n");
        return false;
    }
    (void)xhci_write32(xhci, port_offset,
                       XHCI_PORT_PP | (port_status & XHCI_PORT_CHANGE_MASK));
    *speed = (uint8_t)((port_status & XHCI_PORT_SPEED_MASK) >> 10);
    return *speed != 0U;
}

static bool xhci_command_enable_slot(struct xhci_controller *xhci,
                                     uint8_t *slot_id) {
    struct xhci_trb completion;
    if (!xhci_command(xhci, 0U, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT),
                      &completion)) return false;
    *slot_id = (uint8_t)(completion.control >> 24);
    return *slot_id != 0U && *slot_id <= xhci->max_slots;
}

static bool xhci_setup_device_context(struct xhci_controller *xhci,
                                      struct xhci_hid_device *device,
                                      const struct nexus_usb_path *path,
                                      uint8_t speed, uint16_t ep0_packet) {
    if (path == NULL || path->root_port == 0U ||
        path->depth > NEXUS_USB_ROUTE_MAX_DEPTH ||
        (path->route_string & ~0x000FFFFFU) != 0U) return false;
    volatile uint8_t *input_page = NULL;
    volatile uint8_t *output_page = NULL;
    volatile uint8_t *ep0_page = NULL;
    volatile uint8_t *report_page = NULL;
    if (!xhci_alloc_page(&device->input_context_physical, &input_page) ||
        !xhci_alloc_page(&device->device_context_physical, &output_page) ||
        !xhci_alloc_page(&device->ep0_ring.physical, &ep0_page) ||
        !xhci_alloc_page(&device->report_buffer_physical, &report_page)) {
        return false;
    }
    device->report_buffer = report_page;
    device->ep0_ring.trbs = (volatile struct xhci_trb *)(uintptr_t)ep0_page;
    device->ep0_ring.enqueue = 0U;
    device->ep0_ring.cycle = 1U;
    device->ep0_ring.trbs[XHCI_RING_USABLE].parameter =
        device->ep0_ring.physical;
    device->ep0_ring.trbs[XHCI_RING_USABLE].status = 0U;
    device->ep0_ring.trbs[XHCI_RING_USABLE].control =
        XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TOGGLE_CYCLE |
        XHCI_TRB_CYCLE;

    xhci->dcbaa[device->slot_id] = device->device_context_physical;
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)input_page;
    volatile uint32_t *slot = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + xhci->context_size);
    volatile uint32_t *ep0 = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + 2U * xhci->context_size);

    /* Input Control Context: add slot context and endpoint-zero context. */
    input[1] = 0x3U;
    /*
     * xHCI slot context routing for devices below hubs:
     *   DW0[19:0] route string, DW0[23:20] speed, DW0[31:27] context entries.
     *   DW1[23:16] root hub port; DW2 carries TT Hub Slot ID/Port only
     *   when a low/full-speed device is behind a high-speed USB 2 hub.
     */
    slot[0] = (path->route_string & 0x000FFFFFU) |
              ((uint32_t)speed << 20) | (1U << 27);
    slot[1] = (uint32_t)path->root_port << 16;
    if (path->parent_hub_uses_tt) {
        /* xHCI TT Hub Slot ID and TT Port Number live in Slot Context DW2. */
        volatile uint32_t *slot_dw2 = slot + 2U;
        *slot_dw2 = (uint32_t)path->parent_hub_slot |
                    ((uint32_t)path->parent_port << 8);
    }

    ep0[1] = (3U << 1) | (4U << 3) | ((uint32_t)ep0_packet << 16);
    ep0[2] = (uint32_t)(device->ep0_ring.physical | 1U);
    ep0[3] = (uint32_t)(device->ep0_ring.physical >> 32);
    ep0[4] = 8U;

    struct xhci_trb completion;
    if (!xhci_command(xhci, device->input_context_physical,
                      XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) |
                      ((uint32_t)device->slot_id << 24), &completion)) {
        return false;
    }
    device->active = true;
    return true;
}

static bool xhci_update_ep0_packet(struct xhci_controller *xhci,
                                   struct xhci_hid_device *device,
                                   uint16_t packet_size) {
    if (packet_size == 0U || packet_size > 512U) return false;
    volatile uint8_t *input_page =
        (volatile uint8_t *)memory_phys_to_virt(device->input_context_physical);
    volatile uint8_t *output_page =
        (volatile uint8_t *)memory_phys_to_virt(device->device_context_physical);
    if (input_page == NULL || output_page == NULL) return false;

    for (size_t i = 0U; i < XHCI_PAGE_SIZE; ++i) input_page[i] = 0U;
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)input_page;
    volatile uint32_t *out_ep0 = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)output_page + xhci->context_size);
    volatile uint32_t *in_ep0 = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + 2U * xhci->context_size);
    input[1] = 1U << 1;
    for (size_t i = 0U; i < xhci->context_size / sizeof(uint32_t); ++i)
        in_ep0[i] = out_ep0[i];
    in_ep0[1] = (in_ep0[1] & 0x0000FFFFU) |
                ((uint32_t)packet_size << 16);

    if (!xhci_command(xhci, device->input_context_physical,
                      XHCI_TRB_TYPE(XHCI_TRB_EVALUATE_CTX) |
                      ((uint32_t)device->slot_id << 24), NULL)) return false;
    return true;
}

static bool xhci_get_descriptor(struct xhci_controller *xhci,
                                struct xhci_hid_device *device,
                                uint8_t descriptor_type, uint8_t descriptor_index,
                                uint16_t language, uint16_t length,
                                void *output) {
    const uint16_t value = (uint16_t)(((uint16_t)descriptor_type << 8) |
                                      descriptor_index);
    return xhci_control_transfer(xhci, device, 0x80U, 0x06U,
                                 value, language, output, length);
}

static bool xhci_find_boot_hid(const uint8_t *configuration,
                                size_t length, uint8_t *interface_number,
                                uint8_t *configuration_value,
                                uint8_t *endpoint_address,
                                uint16_t *max_packet, uint8_t *interval,
                                uint16_t *report_descriptor_length,
                                enum xhci_hid_kind *kind,
                                struct nexus_usb_hid_interface_set *all_interfaces) {
    if (configuration == NULL || interface_number == NULL ||
        configuration_value == NULL || endpoint_address == NULL ||
        max_packet == NULL || interval == NULL ||
        report_descriptor_length == NULL || kind == NULL ||
        all_interfaces == NULL) return false;

    struct nexus_usb_hid_interface_set interfaces = {0};
    if (!nexus_usb_hid_collect_interfaces(configuration, length, &interfaces))
        return false;
    *all_interfaces = interfaces;
    if (interfaces.interface_count > 1U) {
        serial_write("xHCI: composite HID receiver detected; HID interfaces=");
        serial_write_hex(interfaces.interface_count);
        serial_write("\r\n");
    }

    const struct nexus_usb_hid_interface *selected = NULL;
    /* Prefer boot keyboards, then boot mice, then generic HID interfaces.
     * The parser collects every interface so composite receivers can be
     * diagnosed; this xHCI endpoint implementation currently activates one. */
    for (uint8_t priority = 0U; priority < 3U && selected == NULL; ++priority) {
        for (uint8_t i = 0U; i < interfaces.interface_count; ++i) {
            const struct nexus_usb_hid_interface *candidate =
                &interfaces.interfaces[i];
            const bool boot_keyboard =
                candidate->subclass == 1U && candidate->protocol == 1U;
            const bool boot_mouse =
                candidate->subclass == 1U && candidate->protocol == 2U;
            const bool generic = !boot_keyboard && !boot_mouse &&
                                 candidate->report_descriptor_length != 0U;
            if ((priority == 0U && boot_keyboard) ||
                (priority == 1U && boot_mouse) ||
                (priority == 2U && generic)) {
                selected = candidate;
                break;
            }
        }
    }
    if (selected == NULL || selected->report_descriptor_length > 512U)
        return false;

    *interface_number = selected->number;
    *configuration_value = interfaces.configuration_value;
    *endpoint_address = selected->endpoint_in;
    *max_packet = selected->max_packet;
    *interval = selected->interval;
    *report_descriptor_length = selected->report_descriptor_length;
    *kind = selected->subclass == 1U && selected->protocol == 1U
        ? XHCI_HID_KEYBOARD
        : selected->subclass == 1U && selected->protocol == 2U
            ? XHCI_HID_MOUSE : XHCI_HID_GAMEPAD;
    return *configuration_value != 0U;
}

static uint8_t xhci_interval(uint8_t speed, uint8_t interval) {
    if (interval == 0U) interval = 1U;
    if (speed >= 3U) {
        return interval > 16U ? 15U : (uint8_t)(interval - 1U);
    }
    uint8_t logarithm = 0U;
    uint8_t value = 1U;
    while ((uint16_t)value * 2U <= interval && logarithm < 13U) {
        value = (uint8_t)(value << 1);
        ++logarithm;
    }
    const uint16_t encoded = (uint16_t)logarithm + 3U;
    return encoded > 15U ? 15U : (uint8_t)encoded;
}

static bool xhci_configure_interrupt_endpoint(struct xhci_controller *xhci,
                                              struct xhci_hid_device *device,
                                              uint8_t endpoint_address,
                                              uint16_t max_packet,
                                              uint8_t interval, uint8_t speed) {
    if (!xhci_ring_init(&device->interrupt_ring)) return false;
    device->endpoint_id = (uint8_t)(((endpoint_address & 0x0FU) * 2U) + 1U);
    device->max_packet = max_packet;

    volatile uint8_t *input_page =
        (volatile uint8_t *)memory_phys_to_virt(device->input_context_physical);
    volatile uint8_t *output_page =
        (volatile uint8_t *)memory_phys_to_virt(device->device_context_physical);
    if (input_page == NULL || output_page == NULL) return false;
    for (size_t i = 0U; i < XHCI_PAGE_SIZE; ++i) input_page[i] = 0U;

    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)input_page;
    volatile uint32_t *out_slot = (volatile uint32_t *)(uintptr_t)output_page;
    volatile uint32_t *out_ep0 = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)output_page + xhci->context_size);
    volatile uint32_t *in_slot = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + xhci->context_size);
    volatile uint32_t *in_ep0 = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + 2U * xhci->context_size);
    const uint8_t dci = device->endpoint_id;
    volatile uint32_t *in_ep = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + (size_t)(dci + 1U) * xhci->context_size);

    /*
     * The slot context and the new interrupt endpoint are added here.
     * EP0 is already present in the addressed device context, so do not
     * set Add Context Flag 1 a second time.
     */
    input[1] = 1U | (1U << dci);
    for (size_t i = 0U; i < xhci->context_size / sizeof(uint32_t); ++i) {
        in_slot[i] = out_slot[i];
        in_ep0[i] = out_ep0[i];
    }
    in_slot[0] = (in_slot[0] & 0x07FFFFFFU) | ((uint32_t)dci << 27);

    const uint8_t encoded_interval = xhci_interval(speed, interval);
    in_ep[0] = (uint32_t)encoded_interval << 16;
    in_ep[1] = (3U << 1) | (7U << 3) | ((uint32_t)max_packet << 16);
    in_ep[2] = (uint32_t)(device->interrupt_ring.physical | 1U);
    in_ep[3] = (uint32_t)(device->interrupt_ring.physical >> 32);
    in_ep[4] = max_packet;

    return xhci_command(xhci, device->input_context_physical,
                        XHCI_TRB_TYPE(XHCI_TRB_CONFIGURE_EP) |
                        ((uint32_t)device->slot_id << 24), NULL);
}

static bool xhci_schedule_interrupt_endpoint(
    struct xhci_controller *xhci, uint8_t slot_id, uint8_t endpoint_id,
    struct xhci_ring *ring, uint64_t report_buffer_physical,
    uint16_t max_packet) {
    if (xhci == NULL || slot_id == 0U || endpoint_id == 0U ||
        ring == NULL || ring->trbs == NULL ||
        report_buffer_physical == 0U || max_packet == 0U) return false;
    volatile struct xhci_trb *trb = xhci_ring_push(
        ring, report_buffer_physical, max_packet,
        XHCI_TRB_TYPE(1U) | XHCI_TRB_IOC | XHCI_TRB_ISP);
    if (trb == NULL) return false;
    volatile uint32_t *doorbell = xhci_map_register(
        xhci, (uint64_t)xhci->doorbell_offset +
              (uint64_t)slot_id * 4U);
    if (doorbell == NULL) return false;
    *doorbell = endpoint_id;
    __asm__ volatile ("sfence" : : : "memory");
    return true;
}

static bool xhci_schedule_interrupt_read(struct xhci_controller *xhci,
                                         struct xhci_hid_device *device) {
    if (device == NULL || !device->active) return false;
    return xhci_schedule_interrupt_endpoint(xhci, device->slot_id,
        device->endpoint_id, &device->interrupt_ring,
        device->report_buffer_physical, device->max_packet);
}

static void xhci_free_hid_device_dma(struct xhci_controller *xhci,
                                       struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL) return;
    if (device->slot_id != 0U && xhci->dcbaa != NULL)
        xhci->dcbaa[device->slot_id] = 0U;
    if (device->input_context_physical != 0U)
        (void)memory_free_page(device->input_context_physical);
    if (device->device_context_physical != 0U)
        (void)memory_free_page(device->device_context_physical);
    if (device->ep0_ring.physical != 0U)
        (void)memory_free_page(device->ep0_ring.physical);
    if (device->interrupt_ring.physical != 0U)
        (void)memory_free_page(device->interrupt_ring.physical);
    if (device->report_buffer_physical != 0U)
        (void)memory_free_page(device->report_buffer_physical);
    if (device->secondary_interrupt_ring.physical != 0U)
        (void)memory_free_page(device->secondary_interrupt_ring.physical);
    if (device->secondary_report_buffer_physical != 0U)
        (void)memory_free_page(device->secondary_report_buffer_physical);
    *device = (struct xhci_hid_device){0};
}

static void xhci_release_hid_device(struct xhci_controller *xhci,
                                    struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL) return;
    if (device->secondary_active) {
        if (device->secondary_kind == XHCI_HID_KEYBOARD) {
            const uint8_t release[8] = {0U};
            input_usb_keyboard_report_state(
                &device->secondary_keyboard_state, release);
        } else if (device->secondary_kind == XHCI_HID_MOUSE) {
            const uint8_t release[3] = {0U};
            input_usb_mouse_report(release);
        }
    }
    if (device->slot_id != 0U)
        (void)xhci_disable_slot(xhci, device->slot_id);
    xhci_free_hid_device_dma(xhci, device);
}

static bool xhci_has_keyboard(const struct xhci_controller *xhci) {
    if (xhci == NULL) return false;
    for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i)
        if (xhci->keyboards[i].active) return true;
    return false;
}

static bool xhci_has_mouse(const struct xhci_controller *xhci) {
    if (xhci == NULL) return false;
    for (size_t i = 0U; i < XHCI_MAX_MICE; ++i)
        if (xhci->mice[i].active) return true;
    return false;
}

static bool xhci_attach_hid_device(struct xhci_controller *xhci,
                                   const struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL || !device->active) return false;
    if (device->kind == XHCI_HID_KEYBOARD) {
        for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i) {
            if (!xhci->keyboards[i].active) {
                xhci->keyboards[i] = *device;
                usb_hid_keyboard_state_reset(&xhci->keyboards[i].keyboard_state);
                serial_write("NEXUS USB HID KEYBOARD READY slot=");
                serial_write_hex(device->slot_id);
                serial_write("\r\n");
                return true;
            }
        }
    } else if (device->kind == XHCI_HID_MOUSE) {
        for (size_t i = 0U; i < XHCI_MAX_MICE; ++i) {
            if (!xhci->mice[i].active) {
                xhci->mice[i] = *device;
                serial_write("NEXUS USB HID MOUSE READY slot=");
                serial_write_hex(device->slot_id);
                serial_write("\r\n");
                return true;
            }
        }
    } else if (device->kind == XHCI_HID_GAMEPAD) {
        for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i) {
            if (!xhci->gamepads[i].active) {
                xhci->gamepads[i] = *device;
                serial_write("NEXUS USB HID GAMEPAD READY slot=");
                serial_write_hex(device->slot_id);
                serial_write(" vendor/product=");
                serial_write_hex(device->vendor_id);
                serial_write("/");
                serial_write_hex(device->product_id);
                serial_write("\r\n");
                return true;
            }
        }
    } else if (device->kind == XHCI_HID_HUB) {
        for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i) {
            if (!xhci->hubs[i].active) {
                xhci->hubs[i] = *device;
                serial_write("NEXUS USB HUB READY slot=");
                serial_write_hex(device->slot_id);
                serial_write(" ports=");
                serial_write_hex(device->hub_descriptor.port_count);
                serial_write("\r\n");
                return true;
            }
        }
    }
    return false;
}

static void xhci_notify_gamepad_disconnected(
    const struct xhci_hid_device *device) {
    if (device == NULL || device->kind != XHCI_HID_GAMEPAD) return;
    const struct nexus_usb_gamepad_event event = {
        .device_id = device->slot_id,
        .vendor_id = device->vendor_id,
        .product_id = device->product_id,
        .flags = NEXUS_USB_GAMEPAD_EVENT_DISCONNECTED,
        .buttons = 0U,
        .axis_count = 0U,
        .hat = 0xFFU
    };
    input_usb_gamepad_event(&event);
}

static void xhci_drop_hid_device(struct xhci_controller *xhci,
                                 struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL || !device->active) return;
    if (device->kind == XHCI_HID_KEYBOARD) {
        const uint8_t release[8] = {0U};
        input_usb_keyboard_report_state(&device->keyboard_state, release);
    } else if (device->kind == XHCI_HID_MOUSE) {
        const uint8_t release[3] = {0U};
        input_usb_mouse_report(release);
    } else if (device->kind == XHCI_HID_GAMEPAD) {
        xhci_notify_gamepad_disconnected(device);
    }
    xhci_release_hid_device(xhci, device);
}

static bool xhci_mark_slot_as_hub(struct xhci_controller *xhci,
                                   struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL || device->slot_id == 0U)
        return false;
    volatile uint8_t *input_page = (volatile uint8_t *)memory_phys_to_virt(
        device->input_context_physical);
    volatile uint8_t *output_page = (volatile uint8_t *)memory_phys_to_virt(
        device->device_context_physical);
    if (input_page == NULL || output_page == NULL) return false;

    for (size_t i = 0U; i < XHCI_PAGE_SIZE; ++i) input_page[i] = 0U;
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)input_page;
    volatile uint32_t *out_slot = (volatile uint32_t *)(uintptr_t)output_page;
    volatile uint32_t *in_slot = (volatile uint32_t *)(uintptr_t)(
        (uintptr_t)input_page + xhci->context_size);

    input[1] = 1U; /* Add Slot Context only. */
    for (size_t i = 0U; i < xhci->context_size / sizeof(uint32_t); ++i)
        in_slot[i] = out_slot[i];
    in_slot[0] |= 1U << 26; /* Hub = 1. */
    if (device->hub_descriptor.multi_tt) in_slot[0] |= 1U << 25;
    in_slot[1] = (in_slot[1] & 0x00FFFFFFU) |
        ((uint32_t)device->hub_descriptor.port_count << 24);

    return xhci_command(xhci, device->input_context_physical,
        XHCI_TRB_TYPE(XHCI_TRB_EVALUATE_CTX) |
        ((uint32_t)device->slot_id << 24), NULL);
}

static bool xhci_hub_control(struct xhci_controller *xhci,
                             struct xhci_hid_device *hub,
                             const struct nexus_usb_setup_packet *setup,
                             void *buffer) {
    if (xhci == NULL || hub == NULL || setup == NULL) return false;
    return xhci_control_transfer(xhci, hub,
        setup->request_type, setup->request,
        setup->value, setup->index, buffer, setup->length);
}

static bool xhci_hub_set_feature(struct xhci_controller *xhci,
                                 struct xhci_hid_device *hub,
                                 uint8_t port, uint16_t feature) {
    struct nexus_usb_setup_packet setup = {0};
    return nexus_usb_hub_set_port_feature_request(port, feature, &setup) &&
           xhci_hub_control(xhci, hub, &setup, NULL);
}

static bool xhci_hub_get_port_status(
    struct xhci_controller *xhci, struct xhci_hid_device *hub,
    uint8_t port, struct nexus_usb_hub_port_status *decoded) {
    struct nexus_usb_setup_packet setup = {0};
    if (!nexus_usb_hub_get_port_status_request(port, &setup)) return false;
    if (!xhci_hub_control(xhci, hub, &setup,
            (void *)(uintptr_t)hub->report_buffer)) return false;
    const uint8_t *status = (const uint8_t *)(uintptr_t)hub->report_buffer;
    return hub->hub_descriptor.superspeed
        ? nexus_usb3_hub_decode_port_status(status, decoded)
        : nexus_usb_hub_decode_port_status(status, decoded);
}

static bool xhci_hub_clear_change(struct xhci_controller *xhci,
                                  struct xhci_hid_device *hub,
                                  uint8_t port, uint16_t feature) {
    struct nexus_usb_setup_packet setup = {0};
    return nexus_usb_hub_clear_port_feature_request(port, feature, &setup) &&
           xhci_hub_control(xhci, hub, &setup, NULL);
}

static bool xhci_initialize_hub(struct xhci_controller *xhci,
                                struct xhci_hid_device *device,
                                uint8_t speed, uint8_t configuration_value,
                                bool multi_tt) {
    if (xhci == NULL || device == NULL || configuration_value == 0U)
        return false;
    if (!xhci_control_transfer(xhci, device, 0x00U, 0x09U,
                               configuration_value, 0U, NULL, 0U))
        return false;

    struct nexus_usb_setup_packet descriptor_request = {0};
    if (!nexus_usb_hub_get_descriptor_request(speed >= 4U,
                                               &descriptor_request))
        return false;
    descriptor_request.length = 2U;
    if (!xhci_hub_control(xhci, device, &descriptor_request,
            (void *)(uintptr_t)device->report_buffer)) return false;

    const uint8_t descriptor_length = device->report_buffer[0];
    if (descriptor_length < (speed >= 4U ? 10U : 9U) ||
        descriptor_length > 64U) return false;
    descriptor_request.length = descriptor_length;
    if (!xhci_hub_control(xhci, device, &descriptor_request,
            (void *)(uintptr_t)device->report_buffer)) return false;

    const uint8_t *descriptor =
        (const uint8_t *)(uintptr_t)device->report_buffer;
    if (speed >= 4U) {
        if (!nexus_usb3_hub_parse_descriptor(descriptor,
                descriptor_length, &device->hub_descriptor)) return false;
    } else if (!nexus_usb_hub_parse_descriptor(descriptor,
                descriptor_length, multi_tt, &device->hub_descriptor)) {
        return false;
    }

    if (!xhci_mark_slot_as_hub(xhci, device)) return false;

    /* Power every downstream port when the hub implements port power
     * switching. Ganged and individual switching both use PORT_POWER. */
    if (!device->hub_descriptor.no_power_switching) {
        for (uint8_t port = 1U;
             port <= device->hub_descriptor.port_count; ++port) {
            if (!xhci_hub_set_feature(xhci, device, port,
                    NEXUS_USB_HUB_FEATURE_PORT_POWER)) return false;
        }
        const uint64_t loops =
            (uint64_t)device->hub_descriptor.power_on_to_good_2ms *
            2000000ULL;
        for (uint64_t i = 0U; i < loops; ++i)
            __asm__ volatile ("pause");
    }

    device->speed = speed;
    device->kind = XHCI_HID_HUB;
    device->active = true;
    device->hub_scan_cursor = 0U;
    serial_write(speed >= 4U
        ? "USB: SuperSpeed hub initialized on xHCI root port "
        : "USB: USB 2 hub initialized on xHCI root port ");
    serial_write_hex(device->port_number);
    serial_write(" ports=");
    serial_write_hex(device->hub_descriptor.port_count);
    serial_write("\r\n");
    return true;
}

static bool xhci_enumerate_hid_port(struct xhci_controller *xhci,
                                    const struct nexus_usb_path *path,
                                    uint8_t speed,
                                    struct xhci_hid_device *device) {
    if (xhci == NULL || path == NULL || device == NULL ||
        path->root_port == 0U) return false;
    *device = (struct xhci_hid_device){0};
    device->usb_path = *path;
    device->port_number = path->root_port;
    if (!xhci_command_enable_slot(xhci, &device->slot_id)) return false;

    if (!nexus_xhci_speed_supported(speed)) goto failed;
    const uint16_t default_packet =
        nexus_xhci_default_ep0_packet_size(speed);
    if (!xhci_setup_device_context(xhci, device, path, speed,
                                   default_packet)) goto failed;

    uint8_t descriptor[18] = {0U};
    if (!xhci_get_descriptor(xhci, device, 1U, 0U, 0U, 8U, descriptor))
        goto failed;
    const uint16_t descriptor_packet =
        nexus_xhci_descriptor_ep0_packet_size(speed, descriptor[7]);
    if (descriptor_packet != default_packet &&
        !xhci_update_ep0_packet(xhci, device, descriptor_packet)) goto failed;
    if (!xhci_get_descriptor(xhci, device, 1U, 0U, 0U,
                             sizeof(descriptor), descriptor)) goto failed;
    device->vendor_id = (uint16_t)(descriptor[8] |
        ((uint16_t)descriptor[9] << 8));
    device->product_id = (uint16_t)(descriptor[10] |
        ((uint16_t)descriptor[11] << 8));

    uint8_t config_header[9] = {0U};
    if (!xhci_get_descriptor(xhci, device, 2U, 0U, 0U,
                             sizeof(config_header), config_header)) goto failed;
    const uint16_t total_length = (uint16_t)(
        config_header[2] | ((uint16_t)config_header[3] << 8));
    if (total_length < 9U || total_length > 512U) goto failed;

    volatile uint8_t *config_buffer = NULL;
    uint64_t config_buffer_physical = 0U;
    if (!xhci_alloc_page(&config_buffer_physical, &config_buffer)) goto failed;
    for (size_t i = 0U; i < XHCI_PAGE_SIZE; ++i) config_buffer[i] = 0U;
    if (!xhci_control_transfer(xhci, device, 0x80U, 0x06U, 0x0200U, 0U,
                               (void *)(uintptr_t)config_buffer, total_length)) {
        (void)memory_free_page(config_buffer_physical);
        goto failed;
    }

    bool has_usb_hub_interface = false;
    bool hub_multi_tt = false;
    for (size_t descriptor_offset = 0U;
         descriptor_offset + 2U <= total_length;) {
        const uint8_t descriptor_length = config_buffer[descriptor_offset];
        const uint8_t descriptor_type =
            config_buffer[descriptor_offset + 1U];
        if (descriptor_length < 2U ||
            descriptor_offset + descriptor_length > total_length) {
            (void)memory_free_page(config_buffer_physical);
            goto failed;
        }
        if (descriptor_type == 4U && descriptor_length >= 9U &&
            config_buffer[descriptor_offset + 5U] == 9U) {
            has_usb_hub_interface = true;
            hub_multi_tt = config_buffer[descriptor_offset + 7U] == 2U;
        }
        descriptor_offset += descriptor_length;
    }
    if (has_usb_hub_interface) {
        const bool initialized = xhci_initialize_hub(xhci, device, speed,
            config_buffer[5], hub_multi_tt);
        (void)memory_free_page(config_buffer_physical);
        if (!initialized) goto failed;
        return true;
    }

    uint8_t interface_number = 0U;
    uint8_t configuration_value = 0U;
    uint8_t endpoint_address = 0U;
    uint16_t max_packet = 0U;
    uint8_t interval = 0U;
    uint16_t report_descriptor_length = 0U;
    enum xhci_hid_kind kind = XHCI_HID_NONE;
    struct nexus_usb_hid_interface_set hid_interfaces = {0};
    const bool boot_hid = xhci_find_boot_hid(
        (const uint8_t *)(uintptr_t)config_buffer, total_length,
        &interface_number, &configuration_value, &endpoint_address,
        &max_packet, &interval, &report_descriptor_length, &kind,
        &hid_interfaces);
    (void)memory_free_page(config_buffer_physical);
    if (!boot_hid) goto failed;

    if (!xhci_control_transfer(xhci, device, 0x00U, 0x09U,
                               configuration_value, 0U, NULL, 0U)) goto failed;
    if (kind == XHCI_HID_GAMEPAD) {
        if (!xhci_control_transfer(xhci, device, 0x81U, 0x06U,
                0x2200U, interface_number,
                (void *)(uintptr_t)device->report_buffer,
                report_descriptor_length)) goto failed;

        if (usb_hid_generic_parse_descriptor_multi(
                (const uint8_t *)(uintptr_t)device->report_buffer,
                report_descriptor_length, &device->generic_multi_layout)) {
            device->generic_report_protocol = true;
            kind = device->generic_multi_layout.reports[0].kind ==
                    NEXUS_USB_HID_GENERIC_KEYBOARD
                ? XHCI_HID_KEYBOARD : XHCI_HID_MOUSE;
            if (device->generic_multi_layout.max_report_bytes > max_packet)
                goto failed;
        } else if (!usb_hid_gamepad_parse_descriptor(
                (const uint8_t *)(uintptr_t)device->report_buffer,
                report_descriptor_length, &device->gamepad_layout)) {
            goto failed;
        }
    } else {
        /* HID Set_Protocol(Boot) on the selected keyboard/mouse interface. */
        if (!xhci_control_transfer(xhci, device, 0x21U, 0x0BU,
                                   0U, interface_number, NULL, 0U))
            goto failed;
    }
    if (!xhci_configure_interrupt_endpoint(xhci, device, endpoint_address,
                                           max_packet, interval, speed))
        goto failed;
    device->kind = kind;
    device->port_number = path->root_port;
    device->active = true;
    if (!xhci_schedule_interrupt_read(xhci, device)) goto failed;
    serial_write(kind == XHCI_HID_KEYBOARD
        ? "USB: HID boot keyboard attached to xHCI port "
        : kind == XHCI_HID_MOUSE
        ? "USB: HID boot mouse attached to xHCI port "
        : "USB: generic HID gamepad attached to xHCI port ");
    serial_write_hex(path->root_port);
    serial_write("\r\n");
    return true;

failed:
    serial_write("xHCI: HID enumeration failed on root port ");
    serial_write_hex(path->root_port);
    serial_write(" VID/PID=");
    serial_write_hex(device->vendor_id);
    serial_write("/");
    serial_write_hex(device->product_id);
    serial_write("\r\n");
    xhci_release_hid_device(xhci, device);
    return false;
}

/*
 * Request exclusive xHCI ownership from system firmware before resetting or
 * programming the controller. The ownership semaphores are byte-addressable;
 * use byte accesses so the OS never races a BIOS update to the adjacent byte.
 */
static bool xhci_handoff_bios(struct xhci_controller *xhci,
                              uint32_t hccparams1) {
    uint32_t offset = nexus_xhci_extended_cap_start(hccparams1);
    if (offset == 0U) return true; /* Legacy handoff capability is optional. */

    for (uint32_t visited = 0U; visited < 256U; ++visited) {
        if (offset < 0x20U || (offset & 0x03U) != 0U) {
            serial_write("xHCI: invalid extended-capability pointer\r\n");
            return false;
        }
        uint32_t header = 0U;
        if (!xhci_read32(xhci, offset, &header)) {
            serial_write("xHCI: unable to read extended-capability chain\r\n");
            return false;
        }
        if (nexus_xhci_legacy_capability_id(header) == 1U) {
            uint8_t bios_owned = 0U;
            uint8_t os_owned = 0U;
            if (!xhci_read8(xhci, (uint64_t)offset + 2U, &bios_owned) ||
                !xhci_read8(xhci, (uint64_t)offset + 3U, &os_owned)) {
                serial_write("xHCI: unable to read BIOS/OS ownership semaphores\r\n");
                return false;
            }
            if (!xhci_write8(xhci, (uint64_t)offset + 3U,
                    nexus_xhci_request_os_ownership(os_owned))) {
                serial_write("xHCI: unable to request OS controller ownership\r\n");
                return false;
            }

            bool released = nexus_xhci_bios_ownership_released(bios_owned);
            for (uint32_t spin = 0U; !released &&
                 spin < XHCI_BIOS_HANDOFF_TIMEOUT; ++spin) {
                if (!xhci_read8(xhci, (uint64_t)offset + 2U, &bios_owned))
                    return false;
                released = nexus_xhci_bios_ownership_released(bios_owned);
                if (!released) __asm__ volatile ("pause");
            }
            if (!released) {
                serial_write("xHCI: firmware did not release BIOS ownership; refusing unsafe takeover\r\n");
                return false;
            }

            uint32_t control_status = 0U;
            if (!xhci_read32(xhci, (uint64_t)offset + 4U,
                             &control_status) ||
                !xhci_write32(xhci, (uint64_t)offset + 4U,
                    nexus_xhci_disable_legacy_smi(control_status))) {
                serial_write("xHCI: unable to disable firmware SMI sources\r\n");
                return false;
            }
            serial_write("xHCI: BIOS ownership released; OS owns controller, legacy SMIs disabled\r\n");
            return true;
        }

        offset = nexus_xhci_extended_cap_next(offset, header);
        if (offset == 0U) return true;
    }
    serial_write("xHCI: extended-capability chain exceeded safety bound\r\n");
    return false;
}

static bool xhci_try_controller(const struct nexus_pci_device *pci_device) {
    struct xhci_controller *xhci = &controller;
    *xhci = (struct xhci_controller){0};
    if (pci_device == NULL || pci_device->class_code != 0x0CU ||
        pci_device->subclass != 0x03U ||
        pci_device->programming_interface != 0x30U) return false;

    struct nexus_pci_bar bar;
    if (!pci_decode_bar(pci_device, 0U, &bar) || bar.io_space ||
        !bar.assigned || !pci_enable_memory_bus_master(pci_device)) {
        serial_write("xHCI: unavailable BAR0 or PCI bus-master setup failed\r\n");
        return false;
    }
    xhci->bar_base = bar.base;

    volatile uint32_t *cap = xhci_map_register(xhci, 0U);
    if (cap == NULL) return false;
    const uint32_t cap0 = *cap;
    xhci->cap_length = (uint8_t)(cap0 & 0xFFU);
    xhci->version = (uint16_t)(cap0 >> 16);
    if (xhci->cap_length < 0x20U) return false;

    uint32_t hcsparams1, hcsparams2, hccparams;
    uint32_t db_offset, rt_offset;
    if (!xhci_read32(xhci, 0x04U, &hcsparams1) ||
        !xhci_read32(xhci, 0x08U, &hcsparams2) ||
        !xhci_read32(xhci, 0x10U, &hccparams) ||
        !xhci_read32(xhci, 0x14U, &db_offset) ||
        !xhci_read32(xhci, 0x18U, &rt_offset)) return false;

    if (!xhci_handoff_bios(xhci, hccparams)) return false;

    xhci->hcsparams1 = hcsparams1;
    xhci->hcsparams2 = hcsparams2;
    xhci->hccparams1 = hccparams;
    xhci->max_ports = nexus_xhci_root_port_count(hcsparams1);
    if (xhci->max_ports == 0U) {
        serial_write("xHCI: controller reports zero root ports\r\n");
        return false;
    }
    xhci->doorbell_offset = db_offset & ~0x03U;
    xhci->runtime_offset = rt_offset & ~0x1FU;
    /* HCCPARAMS1.CSZ is bit 2; bit 0 is AC64. */
    xhci->context_size = (hccparams & (1U << 2)) != 0U ? 64U : 32U;

    if (!xhci_start_controller(xhci, hcsparams1, hcsparams2, hccparams))
        return false;
    xhci->initialized = true;

    /*
     * The hardware advertises an 8-bit MaxPorts value. Use a 16-bit loop
     * counter so a controller with 255 ports cannot wrap back to port zero.
     * Input-device slot limits are independent of port discovery.
     */
    for (uint16_t port = 1U; port <= xhci->max_ports; ++port) {
        const uint8_t port_number = (uint8_t)port;
        uint64_t port_offset = 0U;
        uint32_t port_status = 0U;
        if (!nexus_xhci_port_register_offset(xhci->cap_length,
                port_number, &port_offset) ||
            !xhci_read32(xhci, port_offset, &port_status)) continue;
        xhci->root_port_connected[port_number] =
            nexus_xhci_port_connected(port_status);
        if (!xhci->root_port_connected[port_number]) continue;
        uint8_t speed = 0U;
        if (!xhci_port_reset(xhci, port_number, &speed)) continue;
        serial_write("xHCI: connected root port ");
        serial_write_hex(port_number);
        serial_write(" speed=");
        serial_write_hex(speed);
        serial_write("\r\n");

        struct nexus_usb_path path = {0};
        if (!nexus_usb_path_root(port_number, &path)) continue;
        struct xhci_hid_device device = {0};
        if (!xhci_enumerate_hid_port(xhci, &path, speed, &device))
            continue;
        if (!xhci_attach_hid_device(xhci, &device))
            xhci_release_hid_device(xhci, &device);
    }
    bool any_gamepad = false;
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i)
        any_gamepad = any_gamepad || xhci->gamepads[i].active;
    bool any_hub = false;
    for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i)
        any_hub = any_hub || xhci->hubs[i].active;
    if (!xhci_has_keyboard(xhci) && !xhci_has_mouse(xhci) &&
        !any_gamepad && !any_hub) {
        serial_write("xHCI: no supported HID input or downstream hub found\r\n");
        return false;
    }
    xhci->pci_device = pci_device;
    return true;
}


static bool xhci_device_on_port(const struct xhci_controller *xhci,
                                uint8_t port_number) {
    if (xhci == NULL) return false;
    for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i)
        if (xhci->keyboards[i].active &&
            xhci->keyboards[i].port_number == port_number) return true;
    for (size_t i = 0U; i < XHCI_MAX_MICE; ++i)
        if (xhci->mice[i].active &&
            xhci->mice[i].port_number == port_number) return true;
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i)
        if (xhci->gamepads[i].active &&
            xhci->gamepads[i].port_number == port_number) return true;
    return false;
}

static void xhci_detach_hub_children(struct xhci_controller *xhci,
                                       uint8_t parent_hub_slot,
                                       uint8_t parent_port) {
    if (xhci == NULL || parent_hub_slot == 0U || parent_port == 0U)
        return;
    for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i) {
        struct xhci_hid_device *device = &xhci->keyboards[i];
        if (!device->active ||
            device->usb_path.parent_hub_slot != parent_hub_slot ||
            device->usb_path.parent_port != parent_port) continue;
        const uint8_t release[8] = {0U};
        input_usb_keyboard_report_state(&device->keyboard_state, release);
        serial_write("USB: HID keyboard disconnected from hub slot=");
        serial_write_hex(parent_hub_slot);
        serial_write(" port=");
        serial_write_hex(parent_port);
        serial_write("\r\n");
        xhci_release_hid_device(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_MICE; ++i) {
        struct xhci_hid_device *device = &xhci->mice[i];
        if (!device->active ||
            device->usb_path.parent_hub_slot != parent_hub_slot ||
            device->usb_path.parent_port != parent_port) continue;
        const uint8_t release[3] = {0U};
        input_usb_mouse_report(release);
        serial_write("USB: HID mouse disconnected from hub slot=");
        serial_write_hex(parent_hub_slot);
        serial_write(" port=");
        serial_write_hex(parent_port);
        serial_write("\r\n");
        xhci_release_hid_device(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i) {
        struct xhci_hid_device *device = &xhci->gamepads[i];
        if (!device->active ||
            device->usb_path.parent_hub_slot != parent_hub_slot ||
            device->usb_path.parent_port != parent_port) continue;
        xhci_notify_gamepad_disconnected(device);
        xhci_release_hid_device(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i) {
        struct xhci_hid_device *child_hub = &xhci->hubs[i];
        if (!child_hub->active ||
            child_hub->usb_path.parent_hub_slot != parent_hub_slot ||
            child_hub->usb_path.parent_port != parent_port) continue;
        const uint8_t child_slot = child_hub->slot_id;
        for (uint8_t port = 1U;
             port <= child_hub->hub_descriptor.port_count; ++port)
            xhci_detach_hub_children(xhci, child_slot, port);
        xhci_release_hid_device(xhci, child_hub);
    }
}

static void xhci_detach_hub_subtree(struct xhci_controller *xhci,
                                    struct xhci_hid_device *hub) {
    if (xhci == NULL || hub == NULL || !hub->active ||
        hub->kind != XHCI_HID_HUB) return;
    const uint8_t hub_slot = hub->slot_id;
    for (uint8_t port = 1U; port <= hub->hub_descriptor.port_count; ++port)
        xhci_detach_hub_children(xhci, hub_slot, port);
}

static void xhci_detach_port(struct xhci_controller *xhci,
                             uint8_t port_number) {
    for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i) {
        struct xhci_hid_device *device = &xhci->keyboards[i];
        if (device->active && device->port_number == port_number) {
            const uint8_t release[8] = {0U};
            input_usb_keyboard_report_state(&device->keyboard_state, release);
            xhci_release_hid_device(xhci, device);
            serial_write("USB: HID keyboard disconnected from xHCI port ");
            serial_write_hex(port_number);
            serial_write("\r\n");
        }
    }
    for (size_t i = 0U; i < XHCI_MAX_MICE; ++i) {
        struct xhci_hid_device *device = &xhci->mice[i];
        if (device->active && device->port_number == port_number) {
            const uint8_t release[3] = {0U};
            input_usb_mouse_report(release);
            xhci_release_hid_device(xhci, device);
            serial_write("USB: HID mouse disconnected from xHCI port ");
            serial_write_hex(port_number);
            serial_write("\r\n");
        }
    }
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i) {
        if (xhci->gamepads[i].active &&
            xhci->gamepads[i].port_number == port_number) {
            serial_write("USB: HID gamepad disconnected from xHCI port ");
            serial_write_hex(port_number);
            serial_write("\r\n");
            xhci_notify_gamepad_disconnected(&xhci->gamepads[i]);
            xhci_release_hid_device(xhci, &xhci->gamepads[i]);
        }
    }
    for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i) {
        struct xhci_hid_device *hub = &xhci->hubs[i];
        if (hub->active && hub->port_number == port_number &&
            hub->usb_path.depth == 0U) {
            xhci_detach_hub_subtree(xhci, hub);
            xhci_release_hid_device(xhci, hub);
        }
    }
}

static void xhci_hub_clear_port_changes(
    struct xhci_controller *xhci, struct xhci_hid_device *hub,
    uint8_t port, uint16_t changes) {
    if ((changes & (1U << 0)) != 0U)
        (void)xhci_hub_clear_change(xhci, hub, port,
            NEXUS_USB_HUB_FEATURE_C_PORT_CONNECTION);
    if ((changes & (1U << 1)) != 0U)
        (void)xhci_hub_clear_change(xhci, hub, port,
            NEXUS_USB_HUB_FEATURE_C_PORT_ENABLE);
    if ((changes & (1U << 3)) != 0U)
        (void)xhci_hub_clear_change(xhci, hub, port,
            NEXUS_USB_HUB_FEATURE_C_PORT_OVER_CURRENT);
    if ((changes & (1U << 4)) != 0U)
        (void)xhci_hub_clear_change(xhci, hub, port,
            hub->hub_descriptor.superspeed
                ? NEXUS_USB3_HUB_FEATURE_C_PORT_BH_RESET
                : NEXUS_USB_HUB_FEATURE_C_PORT_RESET);
    if (hub->hub_descriptor.superspeed &&
        (changes & (1U << 5)) != 0U)
        (void)xhci_hub_clear_change(xhci, hub, port,
            NEXUS_USB3_HUB_FEATURE_C_PORT_BH_RESET);
}

static bool xhci_hub_reset_and_enumerate_child(
    struct xhci_controller *xhci, struct xhci_hid_device *hub,
    uint8_t port) {
    const uint16_t reset_feature = hub->hub_descriptor.superspeed
        ? NEXUS_USB3_HUB_FEATURE_PORT_BH_RESET
        : NEXUS_USB_HUB_FEATURE_PORT_RESET;
    const uint16_t reset_change = hub->hub_descriptor.superspeed
        ? NEXUS_USB3_HUB_FEATURE_C_PORT_BH_RESET
        : NEXUS_USB_HUB_FEATURE_C_PORT_RESET;

    if (!xhci_hub_set_feature(xhci, hub, port, reset_feature)) return false;

    struct nexus_usb_hub_port_status status = {0};
    bool reset_done = false;
    for (uint32_t spin = 0U; spin < 5000U; ++spin) {
        if (!xhci_hub_get_port_status(xhci, hub, port, &status))
            return false;
        if (!status.connected) return false;
        if (!status.resetting && status.enabled) {
            reset_done = true;
            break;
        }
        __asm__ volatile ("pause");
    }
    if (!reset_done || status.speed == NEXUS_USB_SPEED_UNKNOWN)
        return false;

    (void)xhci_hub_clear_change(xhci, hub, port, reset_change);
    struct nexus_usb_path child_path = {0};
    if (!nexus_usb_path_child(&hub->usb_path, hub->slot_id,
                              port, &child_path)) return false;
    child_path.parent_hub_uses_tt =
        hub->speed == 3U &&
        (status.speed == NEXUS_USB_SPEED_LOW ||
         status.speed == NEXUS_USB_SPEED_FULL);

    struct xhci_hid_device child = {0};
    if (!xhci_enumerate_hid_port(xhci, &child_path,
            (uint8_t)status.speed, &child)) {
        /* The port is still occupied even when its child is not a supported
         * HID or hub class. Avoid resetting unrelated storage/audio devices
         * on every polling cycle; wait for a connection-change edge. */
        serial_write("USB: downstream device enumerated but no supported HID interface found on hub slot=");
        serial_write_hex(hub->slot_id);
        serial_write(" port=");
        serial_write_hex(port);
        serial_write("\r\n");
        return true;
    }
    if (!xhci_attach_hid_device(xhci, &child)) {
        xhci_release_hid_device(xhci, &child);
        return true;
    }

    serial_write("USB: downstream input device enumerated through hub slot ");
    serial_write_hex(hub->slot_id);
    serial_write(" port=");
    serial_write_hex(port);
    serial_write("\r\n");
    return true;
}

/* Poll one downstream port per hub per scheduler pass. Hub class GET_STATUS
 * is used as a fallback notification path until the hub interrupt-IN endpoint
 * is connected to the generic xHCI endpoint scheduler. */
static void xhci_poll_usb_hubs(struct xhci_controller *xhci) {
    if (xhci == NULL || !xhci->initialized) return;
    for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i) {
        struct xhci_hid_device *hub = &xhci->hubs[i];
        if (!hub->active || hub->kind != XHCI_HID_HUB ||
            hub->hub_descriptor.port_count == 0U) continue;

        hub->hub_scan_cursor = (uint8_t)(
            (hub->hub_scan_cursor % hub->hub_descriptor.port_count) + 1U);
        const uint8_t port = hub->hub_scan_cursor;
        struct nexus_usb_hub_port_status status = {0};
        if (!xhci_hub_get_port_status(xhci, hub, port, &status)) continue;

        const bool was_connected = hub->hub_port_connected[port];
        const bool connection_change = (status.change_bits & 1U) != 0U;
        if (was_connected != status.connected ||
            (connection_change && status.connected)) {
            if (was_connected || connection_change)
                xhci_detach_hub_children(xhci, hub->slot_id, port);
            hub->hub_port_connected[port] = false;
            if (status.connected) {
                hub->hub_port_connected[port] =
                    xhci_hub_reset_and_enumerate_child(xhci, hub, port);
            } else {
                serial_write("USB: downstream hub port disconnected slot=");
                serial_write_hex(hub->slot_id);
                serial_write(" port=");
                serial_write_hex(port);
                serial_write("\r\n");
            }
        }
        xhci_hub_clear_port_changes(xhci, hub, port, status.change_bits);
    }
}

static void xhci_rescan_root_ports_internal(struct xhci_controller *xhci) {
    if (xhci == NULL || !xhci->initialized) return;
    for (uint16_t port = 1U; port <= xhci->max_ports; ++port) {
        const uint8_t port_number = (uint8_t)port;
        uint64_t port_offset = 0U;
        uint32_t port_status = 0U;
        if (!nexus_xhci_port_register_offset(xhci->cap_length,
                port_number, &port_offset) ||
            !xhci_read32(xhci, port_offset, &port_status)) continue;

        /* Only enumerate on an observed disconnected -> connected edge.
         * This keeps periodic recovery polling from resetting attached
         * storage, hubs, audio, or other non-HID devices over and over. */
        if (!nexus_xhci_port_connection_changed(
                xhci->root_port_connected[port_number], port_status))
            continue;
        xhci->root_port_connected[port_number] =
            nexus_xhci_port_connected(port_status);

        if (!xhci->root_port_connected[port_number]) {
            xhci_detach_port(xhci, port_number);
            continue;
        }

        uint8_t speed = 0U;
        if (!xhci_port_reset(xhci, port_number, &speed)) continue;
        struct nexus_usb_path path = {0};
        if (!nexus_usb_path_root(port_number, &path)) continue;
        struct xhci_hid_device device = {0};
        if (!xhci_enumerate_hid_port(xhci, &path, speed, &device))
            continue;
        if (!xhci_attach_hid_device(xhci, &device))
            xhci_release_hid_device(xhci, &device);
    }
}

static void xhci_handle_port_status_change(struct xhci_controller *xhci,
                                           const struct xhci_trb *event) {
    if (xhci == NULL || event == NULL) return;
    /* xHCI Port Status Change Event: Port ID occupies parameter[31:24]. */
    const uint8_t port_number = (uint8_t)(event->parameter >> 24);
    if (port_number == 0U || port_number > xhci->max_ports) return;

    uint64_t port_offset = 0U;
    uint32_t port_status = 0U;
    if (!nexus_xhci_port_register_offset(xhci->cap_length,
                                         port_number, &port_offset)) return;
    if (!xhci_read32(xhci, port_offset, &port_status)) return;

    const bool was_connected = xhci->root_port_connected[port_number];
    xhci->root_port_connected[port_number] =
        nexus_xhci_port_connected(port_status);
    if (!xhci->root_port_connected[port_number]) {
        xhci_detach_port(xhci, port_number);
        return;
    }
    if (was_connected || xhci_device_on_port(xhci, port_number)) return;

    /* A new keyboard or receiver may be plugged in after the desktop boots. */
    uint8_t speed = 0U;
    if (!xhci_port_reset(xhci, port_number, &speed)) return;

    struct nexus_usb_path path = {0};
    if (!nexus_usb_path_root(port_number, &path)) return;
    struct xhci_hid_device device = {0};
    if (!xhci_enumerate_hid_port(xhci, &path, speed, &device))
        return;

    if (!xhci_attach_hid_device(xhci, &device))
        xhci_release_hid_device(xhci, &device);
}

bool xhci_init(void) {
    usb_hid_keyboard_reset();
    uint32_t candidates = 0U;
    serial_write("USB: scanning PCI inventory for xHCI host controllers\r\n");
    for (uint32_t i = 0U; i < pci_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL) continue;
        const enum nexus_usb_host_controller_kind host_kind =
            nexus_usb_host_controller_classify(device->class_code,
                device->subclass, device->programming_interface);
        if (host_kind == NEXUS_USB_HOST_UNKNOWN) continue;
        if (!nexus_usb_host_controller_driver_available(host_kind)) {
            serial_write("USB: detected legacy ");
            serial_write(nexus_usb_host_controller_name(host_kind));
            serial_write(" host controller; controller driver is not yet available\r\n");
            continue;
        }
        ++candidates;
        serial_write("USB: xHCI controller BDF/vendor/device=");
        serial_write_hex(device->bus);
        serial_write(":");
        serial_write_hex(device->slot);
        serial_write(".");
        serial_write_hex(device->function);
        serial_write(" ");
        serial_write_hex(device->vendor_id);
        serial_write("/");
        serial_write_hex(device->device_id);
        serial_write("\r\n");
        if (xhci_try_controller(device)) return true;
    }
    if (candidates == 0U) {
        serial_write("USB: no operational USB host-controller driver found; use another supported input path\r\n");
    } else {
        serial_write("USB: xHCI controllers found but no supported directly attached HID device was initialized\r\n");
    }
    return false;
}

void xhci_rescan(void) {
    xhci_rescan_root_ports_internal(&controller);
}

static bool xhci_stop_for_reinitialization(struct xhci_controller *xhci) {
    uint32_t command = 0U;
    if (!xhci_read32(xhci, (uint64_t)xhci->cap_length, &command))
        return false;
    if ((command & XHCI_USBCMD_RUN) != 0U &&
        !xhci_write32(xhci, (uint64_t)xhci->cap_length,
                      command & ~XHCI_USBCMD_RUN)) return false;
    return xhci_wait_register(xhci, (uint64_t)xhci->cap_length + 4U,
                              XHCI_USBSTS_HALTED, true);
}

static void xhci_release_all_input_devices(struct xhci_controller *xhci) {
    for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS; ++i) {
        struct xhci_hid_device *device = &xhci->keyboards[i];
        if (!device->active) continue;
        const uint8_t release[8] = {0U};
        input_usb_keyboard_report_state(&device->keyboard_state, release);
        xhci_free_hid_device_dma(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_MICE; ++i) {
        struct xhci_hid_device *device = &xhci->mice[i];
        if (!device->active) continue;
        const uint8_t release[3] = {0U};
        input_usb_mouse_report(release);
        xhci_free_hid_device_dma(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i) {
        struct xhci_hid_device *device = &xhci->gamepads[i];
        if (!device->active) continue;
        xhci_notify_gamepad_disconnected(device);
        xhci_free_hid_device_dma(xhci, device);
    }
    for (size_t i = 0U; i < XHCI_MAX_HUBS; ++i) {
        struct xhci_hid_device *hub = &xhci->hubs[i];
        if (hub->active) xhci_free_hid_device_dma(xhci, hub);
    }
}

static void xhci_release_controller_dma(struct xhci_controller *xhci) {
    if (xhci == NULL) return;
    if (xhci->scratchpad_array_physical != 0U) {
        volatile uint64_t *array = (volatile uint64_t *)memory_phys_to_virt(
            xhci->scratchpad_array_physical);
        if (array != NULL) {
            for (uint32_t i = 0U; i < xhci->scratchpad_count; ++i)
                if (array[i] != 0U) (void)memory_free_page(array[i]);
        }
        (void)memory_free_page(xhci->scratchpad_array_physical);
    }
    if (xhci->command_ring.physical != 0U)
        (void)memory_free_page(xhci->command_ring.physical);
    if (xhci->event_ring_physical != 0U)
        (void)memory_free_page(xhci->event_ring_physical);
    if (xhci->erst_physical != 0U)
        (void)memory_free_page(xhci->erst_physical);
    if (xhci->dcbaa_physical != 0U)
        (void)memory_free_page(xhci->dcbaa_physical);

    xhci->dcbaa = NULL;
    xhci->dcbaa_physical = 0U;
    xhci->scratchpad_array_physical = 0U;
    xhci->scratchpad_count = 0U;
    xhci->command_ring = (struct xhci_ring){0};
    xhci->event_ring = NULL;
    xhci->event_ring_physical = 0U;
    xhci->event_dequeue = 0U;
    xhci->event_cycle = 1U;
    xhci->erst_physical = 0U;
}

static bool xhci_reinitialize_after_power_loss(struct xhci_controller *xhci) {
    if (xhci == NULL || xhci->pci_device == NULL ||
        xhci->hcsparams1 == 0U) return false;

    /* Stop DMA before releasing any controller/device memory. If halt cannot
     * be confirmed, deliberately retain the DMA buffers to avoid use-after-free
     * by a controller that may still be bus-mastering. */
    if (!xhci_stop_for_reinitialization(xhci)) {
        serial_write("xHCI: cannot confirm controller halt; retaining DMA memory for safety\r\n");
        return false;
    }

    xhci_release_all_input_devices(xhci);
    xhci_release_controller_dma(xhci);
    xhci->initialized = false;
    for (size_t i = 0U; i <= NEXUS_XHCI_MAX_ROOT_PORTS; ++i)
        xhci->root_port_connected[i] = false;

    serial_write("xHCI: rebuilding command/event rings and device contexts after power loss\r\n");
    if (!xhci_start_controller(xhci, xhci->hcsparams1,
                               xhci->hcsparams2, xhci->hccparams1)) {
        serial_write("xHCI: controller reinitialization failed; USB input remains unavailable\r\n");
        return false;
    }

    xhci->initialized = true;
    xhci->last_root_scan_tick = timer_ticks();
    xhci_rescan_root_ports_internal(xhci);
    serial_write("xHCI: controller reinitialized; root ports rescanned\r\n");
    return true;
}

bool xhci_resume(void) {
    struct xhci_controller *xhci = &controller;
    if (!xhci->initialized) return false;
    uint32_t command = 0U;
    uint32_t status = 0U;
    const bool registers_read =
        xhci_read32(xhci, (uint64_t)xhci->cap_length, &command) &&
        xhci_read32(xhci, (uint64_t)xhci->cap_length + 4U, &status);
    if (registers_read &&
        nexus_xhci_resume_decide(command, status) ==
            NEXUS_XHCI_RESUME_RETAINED) {
        xhci_rescan_root_ports_internal(xhci);
        return true;
    }

    serial_write("xHCI: controller state lost across resume; attempting full reinitialization\r\n");
    return xhci_reinitialize_after_power_loss(xhci);
}

bool xhci_controller_owns_pci_device(
    const struct nexus_pci_device *device) {
    return controller.initialized && device != NULL &&
           controller.pci_device == device;
}

bool xhci_keyboard_ready(void) {
    return controller.initialized && xhci_has_keyboard(&controller);
}

bool xhci_mouse_ready(void) {
    return controller.initialized && xhci_has_mouse(&controller);
}

bool xhci_gamepads_ready(void) {
    if (!controller.initialized) return false;
    for (size_t i = 0U; i < XHCI_MAX_GAMEPADS; ++i)
        if (controller.gamepads[i].active) return true;
    return false;
}

void xhci_poll(void) {
    struct xhci_controller *xhci = &controller;
    if (!xhci->initialized) return;

    const uint64_t now = timer_ticks();
    if (now >= xhci->last_root_scan_tick &&
        now - xhci->last_root_scan_tick >= 20U) {
        xhci->last_root_scan_tick = now;

        /* A kernel may return from firmware/platform sleep while the xHCI
         * controller has lost power or reset its operational state. Monitor
         * RUN/HALTED/CNR after wake and rebuild DMA state before queueing any
         * new endpoint transfers. If MMIO is temporarily unavailable, resume
         * fails closed and the next watchdog interval retries. */
        uint32_t command = 0U;
        uint32_t status = 0U;
        if (!xhci_read32(xhci, (uint64_t)xhci->cap_length, &command) ||
            !xhci_read32(xhci, (uint64_t)xhci->cap_length + 4U, &status) ||
            nexus_xhci_resume_decide(command, status) !=
                NEXUS_XHCI_RESUME_RETAINED) {
            (void)xhci_resume();
            if (!xhci->initialized) return;
        } else {
            xhci_rescan_root_ports_internal(xhci);
        }
    }

    struct xhci_trb event;
    while (xhci_event_next(xhci, &event)) {
        const uint8_t event_type = xhci_event_type(&event);
        if (event_type == XHCI_TRB_PORT_EVENT) {
            xhci_handle_port_status_change(xhci, &event);
            continue;
        }
        if (event_type != XHCI_TRB_TRANSFER_EVENT) continue;
        const uint8_t slot = (uint8_t)(event.control >> 24);
        const uint8_t endpoint =
            (uint8_t)((event.control >> 16) & 0x1FU);
        struct xhci_hid_device *device = NULL;
        for (size_t i = 0U; i < XHCI_MAX_KEYBOARDS && device == NULL; ++i) {
            if (xhci->keyboards[i].active &&
                slot == xhci->keyboards[i].slot_id &&
                endpoint == xhci->keyboards[i].endpoint_id)
                device = &xhci->keyboards[i];
        }
        for (size_t i = 0U; i < XHCI_MAX_MICE && device == NULL; ++i) {
            if (xhci->mice[i].active &&
                slot == xhci->mice[i].slot_id &&
                endpoint == xhci->mice[i].endpoint_id)
                device = &xhci->mice[i];
        }
        for (size_t i = 0U; i < XHCI_MAX_GAMEPADS && device == NULL; ++i) {
            if (xhci->gamepads[i].active &&
                slot == xhci->gamepads[i].slot_id &&
                endpoint == xhci->gamepads[i].endpoint_id)
                device = &xhci->gamepads[i];
        }
        if (device == NULL || !device->active) continue;

        const uint8_t code = xhci_event_code(&event);
        if (code == XHCI_CC_SUCCESS || code == XHCI_CC_SHORT_PACKET) {
            const uint32_t residual = event.status & 0x00FFFFFFU;
            if (residual <= device->max_packet) {
                const size_t received = (size_t)device->max_packet - residual;
                const uint8_t *report =
                    (const uint8_t *)(uintptr_t)device->report_buffer;
                if (device->kind == XHCI_HID_KEYBOARD) {
                    if (device->generic_report_protocol) {
                        uint8_t boot_report[8];
                        if (usb_hid_generic_decode_keyboard_multi(
                                &device->generic_multi_layout, report, received,
                                boot_report)) {
                            input_usb_keyboard_report_state(
                                &device->keyboard_state, boot_report);
                        }
                    } else if (received >= 8U) {
                        input_usb_keyboard_report_state(&device->keyboard_state,
                                                        report);
                    }
                } else if (device->kind == XHCI_HID_MOUSE) {
                    if (device->generic_report_protocol) {
                        struct nexus_usb_mouse_event decoded;
                        if (usb_hid_generic_decode_mouse_multi(
                                &device->generic_multi_layout, report, received,
                                &decoded)) {
                            const uint8_t normalized[4] = {
                                decoded.buttons,
                                (uint8_t)(int8_t)decoded.dx,
                                (uint8_t)(int8_t)-decoded.dy,
                                (uint8_t)decoded.wheel
                            };
                            input_usb_mouse_report_sized(normalized,
                                                         sizeof(normalized));
                        }
                    } else if (received >= 3U) {
                        input_usb_mouse_report_sized(report, received);
                    }
                } else if (device->kind == XHCI_HID_GAMEPAD) {
                    struct nexus_usb_gamepad_event gamepad_event;
                    if (usb_hid_gamepad_decode(&device->gamepad_layout,
                            report, received, device->slot_id, device->vendor_id,
                            device->product_id, &gamepad_event))
                        input_usb_gamepad_event(&gamepad_event);
                }
            }
        } else {
            serial_write(device->kind == XHCI_HID_KEYBOARD
                ? "xHCI: USB keyboard transfer failed; code="
                : device->kind == XHCI_HID_MOUSE
                ? "xHCI: USB mouse transfer failed; code="
                : "xHCI: USB gamepad transfer failed; code=");
            serial_write_hex(code);
            serial_write("\r\n");
            xhci_drop_hid_device(xhci, device);
            continue;
        }
        if (!xhci_schedule_interrupt_read(xhci, device)) {
            serial_write(device->kind == XHCI_HID_KEYBOARD
                ? "xHCI: unable to re-arm USB keyboard endpoint\r\n"
                : device->kind == XHCI_HID_MOUSE
                ? "xHCI: unable to re-arm USB mouse endpoint\r\n"
                : "xHCI: unable to re-arm USB gamepad endpoint\r\n");
            xhci_drop_hid_device(xhci, device);
        }
    }
    xhci_poll_usb_hubs(xhci);
}
