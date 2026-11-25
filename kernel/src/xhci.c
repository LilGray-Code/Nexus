#include "xhci.h"

#include "input.h"
#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "usb_hid.h"

#include <stddef.h>
#include <stdint.h>

#define XHCI_DMA_LIMIT          0x100000000ULL
#define XHCI_PAGE_SIZE          4096U
#define XHCI_RING_ENTRIES       256U
#define XHCI_RING_USABLE        255U
#define XHCI_MAX_MMIO_PAGES     12U
#define XHCI_MAX_PORTS          32U
#define XHCI_MAX_SCRATCHPADS    512U
#define XHCI_POLL_LIMIT         5000000U
#define XHCI_COMMAND_TIMEOUT    5000000U
#define XHCI_TRANSFER_TIMEOUT   5000000U

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
    XHCI_HID_MOUSE = 2
};

struct xhci_hid_device {
    bool active;
    enum xhci_hid_kind kind;
    uint8_t slot_id;
    uint8_t endpoint_id;
    uint8_t port_number;
    uint16_t max_packet;
    uint64_t device_context_physical;
    uint64_t input_context_physical;
    uint64_t report_buffer_physical;
    volatile uint8_t *report_buffer;
    struct xhci_ring ep0_ring;
    struct xhci_ring interrupt_ring;
};

struct xhci_controller {
    bool initialized;
    bool keyboard_active;
    bool mouse_active;
    uint64_t bar_base;
    uint8_t cap_length;
    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t context_size;
    uint16_t version;
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
    uint64_t erst_physical;
    struct xhci_mmio_mapping mappings[XHCI_MAX_MMIO_PAGES];
    size_t mapping_count;
    /* One directly attached boot keyboard and one directly attached boot mouse. */
    struct xhci_hid_device keyboard;
    struct xhci_hid_device mouse;
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
    const uint64_t port_offset = (uint64_t)xhci->cap_length + 0x400U +
        ((uint64_t)(port_number - 1U) * 0x10U);
    uint32_t port_status;
    if (!xhci_read32(xhci, port_offset, &port_status) ||
        (port_status & XHCI_PORT_CCS) == 0U) return false;

    const uint32_t speed_value =
        (port_status & XHCI_PORT_SPEED_MASK) >> 10;
    if (speed_value == 0U || speed_value > 4U) return false;

    /* Power the root port before issuing the protocol-specific reset. */
    if ((port_status & XHCI_PORT_PP) == 0U) {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | (port_status & XHCI_PORT_CHANGE_MASK)))
            return false;
        xhci_delay();
    }
    if (speed_value == 4U) {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | XHCI_PORT_WPR |
                          (port_status & XHCI_PORT_CHANGE_MASK))) return false;
    } else {
        if (!xhci_write32(xhci, port_offset,
                          XHCI_PORT_PP | XHCI_PORT_PR |
                          (port_status & XHCI_PORT_CHANGE_MASK))) return false;
    }

    const uint32_t reset_bit = speed_value == 4U ? XHCI_PORT_WPR : XHCI_PORT_PR;
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
                                      uint8_t port_number, uint8_t speed,
                                      uint16_t ep0_packet) {
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
    slot[0] = ((uint32_t)speed << 20) | (1U << 27);
    slot[1] = (uint32_t)port_number << 16;

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
                                enum xhci_hid_kind *kind) {
    if (configuration == NULL || length < 9U || interface_number == NULL ||
        configuration_value == NULL || endpoint_address == NULL ||
        max_packet == NULL || interval == NULL || kind == NULL) return false;

    bool in_boot_hid = false;
    bool found = false;
    *configuration_value = configuration[5];
    *kind = XHCI_HID_NONE;
    size_t offset = 0U;
    while (offset + 2U <= length) {
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U || offset + descriptor_length > length)
            return false;

        if (descriptor_type == 4U && descriptor_length >= 9U) {
            const bool boot_interface = configuration[offset + 5U] == 3U &&
                configuration[offset + 6U] == 1U &&
                (configuration[offset + 7U] == 1U ||
                 configuration[offset + 7U] == 2U);
            in_boot_hid = boot_interface;
            if (in_boot_hid) {
                *interface_number = configuration[offset + 2U];
                *kind = configuration[offset + 7U] == 1U
                    ? XHCI_HID_KEYBOARD : XHCI_HID_MOUSE;
            }
        } else if (descriptor_type == 5U && descriptor_length >= 7U &&
                   in_boot_hid) {
            const uint8_t address = configuration[offset + 2U];
            const uint8_t attributes = configuration[offset + 3U] & 0x03U;
            const uint16_t packet = (uint16_t)(
                configuration[offset + 4U] |
                ((uint16_t)(configuration[offset + 5U] & 0x07U) << 8));
            const uint16_t minimum_packet =
                *kind == XHCI_HID_KEYBOARD ? 8U : 3U;
            if ((address & 0x80U) != 0U && attributes == 3U &&
                packet >= minimum_packet && packet <= 64U) {
                *endpoint_address = address;
                *max_packet = packet;
                *interval = configuration[offset + 6U];
                found = true;
                break;
            }
        }
        offset += descriptor_length;
    }
    return found && *configuration_value != 0U && *kind != XHCI_HID_NONE;
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

static bool xhci_schedule_interrupt_read(struct xhci_controller *xhci,
                                         struct xhci_hid_device *device) {
    if (device == NULL || !device->active ||
        device->interrupt_ring.trbs == NULL) return false;
    volatile struct xhci_trb *trb = xhci_ring_push(
        &device->interrupt_ring, device->report_buffer_physical,
        device->max_packet,
        XHCI_TRB_TYPE(1U) | XHCI_TRB_IOC | XHCI_TRB_ISP);
    if (trb == NULL) return false;
    volatile uint32_t *doorbell = xhci_map_register(
        xhci, (uint64_t)xhci->doorbell_offset +
              (uint64_t)device->slot_id * 4U);
    if (doorbell == NULL) return false;
    *doorbell = device->endpoint_id;
    __asm__ volatile ("sfence" : : : "memory");
    return true;
}

static void xhci_release_hid_device(struct xhci_controller *xhci,
                                    struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL) return;
    if (device->slot_id != 0U) {
        (void)xhci_disable_slot(xhci, device->slot_id);
        if (xhci->dcbaa != NULL) xhci->dcbaa[device->slot_id] = 0U;
    }
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
    *device = (struct xhci_hid_device){0};
}

static bool xhci_enumerate_hid_port(struct xhci_controller *xhci,
                                    uint8_t port_number, uint8_t speed,
                                    struct xhci_hid_device *device) {
    if (xhci == NULL || device == NULL) return false;
    *device = (struct xhci_hid_device){0};
    if (!xhci_command_enable_slot(xhci, &device->slot_id)) return false;

    const uint16_t default_packet = speed == 3U ? 64U :
                                   speed == 4U ? 512U : 8U;
    if (!xhci_setup_device_context(xhci, device, port_number, speed,
                                   default_packet)) goto failed;

    uint8_t descriptor[18] = {0U};
    if (!xhci_get_descriptor(xhci, device, 1U, 0U, 0U, 8U, descriptor))
        goto failed;
    const uint16_t descriptor_packet = speed == 4U ? 512U :
        (descriptor[7] == 0U ? default_packet : descriptor[7]);
    if (descriptor_packet != default_packet &&
        !xhci_update_ep0_packet(xhci, device, descriptor_packet)) goto failed;
    if (!xhci_get_descriptor(xhci, device, 1U, 0U, 0U,
                             sizeof(descriptor), descriptor)) goto failed;

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

    uint8_t interface_number = 0U;
    uint8_t configuration_value = 0U;
    uint8_t endpoint_address = 0U;
    uint16_t max_packet = 0U;
    uint8_t interval = 0U;
    enum xhci_hid_kind kind = XHCI_HID_NONE;
    const bool boot_hid = xhci_find_boot_hid(
        (const uint8_t *)(uintptr_t)config_buffer, total_length,
        &interface_number, &configuration_value, &endpoint_address,
        &max_packet, &interval, &kind);
    (void)memory_free_page(config_buffer_physical);
    if (!boot_hid) goto failed;

    if (!xhci_control_transfer(xhci, device, 0x00U, 0x09U,
                               configuration_value, 0U, NULL, 0U)) goto failed;
    /* HID Set_Protocol(Boot) on the selected boot interface. */
    if (!xhci_control_transfer(xhci, device, 0x21U, 0x0BU,
                               0U, interface_number, NULL, 0U)) goto failed;
    if (!xhci_configure_interrupt_endpoint(xhci, device, endpoint_address,
                                           max_packet, interval, speed))
        goto failed;
    device->kind = kind;
    device->port_number = port_number;
    device->active = true;
    if (!xhci_schedule_interrupt_read(xhci, device)) goto failed;
    serial_write(kind == XHCI_HID_KEYBOARD
        ? "USB: HID boot keyboard attached to xHCI port "
        : "USB: HID boot mouse attached to xHCI port ");
    serial_write_hex(port_number);
    serial_write("\r\n");
    return true;

failed:
    xhci_release_hid_device(xhci, device);
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

    xhci->max_ports = (uint8_t)((hcsparams1 >> 24) & 0xFFU);
    if (xhci->max_ports == 0U) return false;
    if (xhci->max_ports > XHCI_MAX_PORTS)
        xhci->max_ports = XHCI_MAX_PORTS;
    xhci->doorbell_offset = db_offset & ~0x03U;
    xhci->runtime_offset = rt_offset & ~0x1FU;
    /* HCCPARAMS1.CSZ is bit 2; bit 0 is AC64. */
    xhci->context_size = (hccparams & (1U << 2)) != 0U ? 64U : 32U;

    if (!xhci_start_controller(xhci, hcsparams1, hcsparams2, hccparams))
        return false;
    xhci->initialized = true;

    for (uint8_t port = 1U; port <= xhci->max_ports; ++port) {
        if (xhci->keyboard_active && xhci->mouse_active) break;
        uint8_t speed = 0U;
        if (!xhci_port_reset(xhci, port, &speed)) continue;
        serial_write("xHCI: connected root port ");
        serial_write_hex(port);
        serial_write(" speed=");
        serial_write_hex(speed);
        serial_write("\r\n");

        struct xhci_hid_device device = {0};
        if (!xhci_enumerate_hid_port(xhci, port, speed, &device))
            continue;
        if (device.kind == XHCI_HID_KEYBOARD && !xhci->keyboard_active) {
            xhci->keyboard = device;
            xhci->keyboard_active = true;
            serial_write("NEXUS USB HID KEYBOARD READY\r\n");
        } else if (device.kind == XHCI_HID_MOUSE && !xhci->mouse_active) {
            xhci->mouse = device;
            xhci->mouse_active = true;
            serial_write("NEXUS USB HID MOUSE READY\r\n");
        } else {
            /* Only one boot keyboard and one boot mouse are retained. */
            xhci_release_hid_device(xhci, &device);
        }
    }
    if (!xhci->keyboard_active && !xhci->mouse_active) {
        serial_write("xHCI: no directly attached HID boot input found\r\n");
        return false;
    }
    return true;
}


static bool xhci_device_on_port(const struct xhci_controller *xhci,
                                uint8_t port_number) {
    return (xhci->keyboard_active && xhci->keyboard.active &&
            xhci->keyboard.port_number == port_number) ||
           (xhci->mouse_active && xhci->mouse.active &&
            xhci->mouse.port_number == port_number);
}

static void xhci_detach_port(struct xhci_controller *xhci,
                             uint8_t port_number) {
    if (xhci->keyboard_active && xhci->keyboard.active &&
        xhci->keyboard.port_number == port_number) {
        /* Synthesize releases so unplugging while a key is held cannot stick. */
        const uint8_t released_report[8] = {0U};
        input_usb_keyboard_report(released_report);
        usb_hid_keyboard_reset();
        xhci_release_hid_device(xhci, &xhci->keyboard);
        xhci->keyboard_active = false;
        serial_write("USB: HID keyboard disconnected from xHCI port ");
        serial_write_hex(port_number);
        serial_write("\r\n");
    }
    if (xhci->mouse_active && xhci->mouse.active &&
        xhci->mouse.port_number == port_number) {
        const uint8_t released_report[3] = {0U};
        input_usb_mouse_report(released_report);
        xhci_release_hid_device(xhci, &xhci->mouse);
        xhci->mouse_active = false;
        serial_write("USB: HID mouse disconnected from xHCI port ");
        serial_write_hex(port_number);
        serial_write("\r\n");
    }
}

static void xhci_handle_port_status_change(struct xhci_controller *xhci,
                                           const struct xhci_trb *event) {
    if (xhci == NULL || event == NULL) return;
    /* xHCI Port Status Change Event: Port ID occupies parameter[31:24]. */
    const uint8_t port_number = (uint8_t)(event->parameter >> 24);
    if (port_number == 0U || port_number > xhci->max_ports) return;

    const uint64_t port_offset = (uint64_t)xhci->cap_length + 0x400U +
        ((uint64_t)(port_number - 1U) * 0x10U);
    uint32_t port_status = 0U;
    if (!xhci_read32(xhci, port_offset, &port_status)) return;

    if ((port_status & XHCI_PORT_CCS) == 0U) {
        xhci_detach_port(xhci, port_number);
        return;
    }
    if (xhci_device_on_port(xhci, port_number)) return;

    /* A new keyboard or receiver may be plugged in after the desktop boots. */
    uint8_t speed = 0U;
    if (!xhci_port_reset(xhci, port_number, &speed)) return;

    struct xhci_hid_device device = {0};
    if (!xhci_enumerate_hid_port(xhci, port_number, speed, &device))
        return;

    if (device.kind == XHCI_HID_KEYBOARD && !xhci->keyboard_active) {
        usb_hid_keyboard_reset();
        xhci->keyboard = device;
        xhci->keyboard_active = true;
        serial_write("NEXUS USB HID KEYBOARD HOTPLUG READY\r\n");
    } else if (device.kind == XHCI_HID_MOUSE && !xhci->mouse_active) {
        xhci->mouse = device;
        xhci->mouse_active = true;
        serial_write("NEXUS USB HID MOUSE HOTPLUG READY\r\n");
    } else {
        /* Keep one keyboard and one mouse; do not leak an unused xHCI slot. */
        xhci_release_hid_device(xhci, &device);
    }
}

bool xhci_init(void) {
    usb_hid_keyboard_reset();
    for (uint32_t i = 0U; i < pci_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL || device->class_code != 0x0CU ||
            device->subclass != 0x03U ||
            device->programming_interface != 0x30U) continue;
        if (xhci_try_controller(device)) return true;
    }
    serial_write("USB: no usable xHCI HID keyboard or mouse\r\n");
    return false;
}

bool xhci_keyboard_ready(void) {
    return controller.initialized && controller.keyboard_active &&
           controller.keyboard.active;
}

bool xhci_mouse_ready(void) {
    return controller.initialized && controller.mouse_active &&
           controller.mouse.active;
}

void xhci_poll(void) {
    struct xhci_controller *xhci = &controller;
    if (!xhci->initialized) return;

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
        if (xhci->keyboard_active &&
            slot == xhci->keyboard.slot_id &&
            endpoint == xhci->keyboard.endpoint_id) {
            device = &xhci->keyboard;
        } else if (xhci->mouse_active &&
                   slot == xhci->mouse.slot_id &&
                   endpoint == xhci->mouse.endpoint_id) {
            device = &xhci->mouse;
        }
        if (device == NULL || !device->active) continue;

        const uint8_t code = xhci_event_code(&event);
        if (code == XHCI_CC_SUCCESS || code == XHCI_CC_SHORT_PACKET) {
            if (device->kind == XHCI_HID_KEYBOARD) {
                input_usb_keyboard_report(
                    (const uint8_t *)(uintptr_t)device->report_buffer);
            } else if (device->kind == XHCI_HID_MOUSE) {
                input_usb_mouse_report(
                    (const uint8_t *)(uintptr_t)device->report_buffer);
            }
        } else {
            serial_write(device->kind == XHCI_HID_KEYBOARD
                ? "xHCI: USB keyboard transfer failed; code="
                : "xHCI: USB mouse transfer failed; code=");
            serial_write_hex(code);
            serial_write("\r\n");
            device->active = false;
            if (device->kind == XHCI_HID_KEYBOARD)
                xhci->keyboard_active = false;
            else
                xhci->mouse_active = false;
            continue;
        }
        if (!xhci_schedule_interrupt_read(xhci, device)) {
            serial_write(device->kind == XHCI_HID_KEYBOARD
                ? "xHCI: unable to re-arm USB keyboard endpoint\r\n"
                : "xHCI: unable to re-arm USB mouse endpoint\r\n");
            device->active = false;
            if (device->kind == XHCI_HID_KEYBOARD)
                xhci->keyboard_active = false;
            else
                xhci->mouse_active = false;
        }
    }
}
