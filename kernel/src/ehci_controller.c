#include "ehci_controller.h"

#include "ehci.h"
#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "timer.h"
#include "usb_host_controller.h"

#include <stddef.h>
#include <stdint.h>

#define EHCI_MAX_CONTROLLERS 4U
#define EHCI_MAX_MMIO_PAGES 4U
#define EHCI_DMA_LIMIT 0x100000000ULL
#define EHCI_POLL_LIMIT 2000000U
#define EHCI_PORTSC_BASE 0x44U
#define EHCI_PORTSC_POWER (1U << 12)
#define EHCI_PORTSC_CONNECT (1U << 0)
#define EHCI_USBCMD_RUN (1U << 0)
#define EHCI_USBCMD_RESET (1U << 1)
#define EHCI_USBCMD_PERIODIC_ENABLE (1U << 4)
#define EHCI_USBCMD_ASYNC_ENABLE (1U << 5)
#define EHCI_USBSTS_HALTED (1U << 0)
#define EHCI_USBSTS_HOST_ERROR (1U << 4)
#define EHCI_USBSTS_ASYNC_STATUS (1U << 15)
#define EHCI_USBSTS_W1C_MASK 0x3FU
#define EHCI_LEGACY_CAP_ID 0x01U
#define EHCI_LEGACY_BIOS_OWNED (1U << 16)
#define EHCI_LEGACY_OS_OWNED (1U << 24)
#define EHCI_CONFIGFLAG_ROUTE_EHCI 1U
#define EHCI_PORT_SCAN_INTERVAL_TICKS 20U

struct ehci_mmio_mapping {
    uint64_t physical_page;
    volatile uint32_t *virtual_page;
};

struct ehci_controller {
    bool present;
    bool running;
    bool dma_quarantined;
    const struct nexus_pci_device *pci_device;
    uint64_t bar_base;
    uint8_t capability_length;
    uint8_t port_count;
    bool port_power_control;
    uint64_t async_head_physical;
    volatile struct nexus_ehci_qh *async_head;
    bool port_connected[16];
    uint64_t last_port_scan_tick;
    struct ehci_mmio_mapping mappings[EHCI_MAX_MMIO_PAGES];
    uint8_t mapping_count;
};

static struct ehci_controller controllers[EHCI_MAX_CONTROLLERS];

static volatile uint32_t *ehci_map_register(
    struct ehci_controller *controller, uint32_t offset) {
    if (controller == NULL ||
        controller->bar_base > UINT64_MAX - offset) return NULL;
    const uint64_t physical = controller->bar_base + offset;
    const uint64_t page = physical & ~(uint64_t)(NEXUS_EHCI_PAGE_SIZE - 1U);

    for (uint8_t i = 0U; i < controller->mapping_count; ++i) {
        if (controller->mappings[i].physical_page == page) {
            return (volatile uint32_t *)(
                (uintptr_t)controller->mappings[i].virtual_page +
                (uintptr_t)(physical & (NEXUS_EHCI_PAGE_SIZE - 1U)));
        }
    }
    if (controller->mapping_count >= EHCI_MAX_MMIO_PAGES) return NULL;

    volatile uint32_t *mapped_page = NULL;
    if (!paging_map_mmio_page(page, &mapped_page) ||
        mapped_page == NULL) return NULL;
    controller->mappings[controller->mapping_count] =
        (struct ehci_mmio_mapping){
            .physical_page = page,
            .virtual_page = mapped_page
        };
    ++controller->mapping_count;
    return (volatile uint32_t *)(
        (uintptr_t)mapped_page +
        (uintptr_t)(physical & (NEXUS_EHCI_PAGE_SIZE - 1U)));
}

static bool ehci_read32(struct ehci_controller *controller,
                        uint32_t offset, uint32_t *value) {
    if (value == NULL) return false;
    volatile uint32_t *reg = ehci_map_register(controller, offset);
    if (reg == NULL) return false;
    *value = *reg;
    return *value != UINT32_MAX;
}

static bool ehci_write32(struct ehci_controller *controller,
                         uint32_t offset, uint32_t value) {
    volatile uint32_t *reg = ehci_map_register(controller, offset);
    if (reg == NULL) return false;
    *reg = value;
    __asm__ volatile ("sfence" : : : "memory");
    return true;
}

static bool ehci_wait_register(struct ehci_controller *controller,
                               uint32_t offset, uint32_t mask,
                               bool should_be_set) {
    for (uint32_t spin = 0U; spin < EHCI_POLL_LIMIT; ++spin) {
        uint32_t value = 0U;
        if (!ehci_read32(controller, offset, &value)) return false;
        if (((value & mask) != 0U) == should_be_set) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool ehci_handoff_legacy_ownership(
    const struct nexus_pci_device *device, uint8_t first_capability) {
    uint8_t offset = 0U;
    if (!nexus_ehci_validate_legacy_capability_offset(
            first_capability, &offset)) return false;
    if (offset == 0U) return true;

    /* PCI EHCI Extended Capabilities are a forward-linked list. Capabilities
     * are read from PCI configuration space, not the controller BAR. */
    uint8_t visited[64] = {0U};
    uint8_t visited_count = 0U;
    while (offset != 0U) {
        if (visited_count >= sizeof(visited) ||
            !nexus_ehci_validate_legacy_capability_offset(offset, NULL))
            return false;
        for (uint8_t i = 0U; i < visited_count; ++i)
            if (visited[i] == offset) return false;
        visited[visited_count++] = offset;

        uint32_t capability = 0U;
        if (!pci_read_config32(device, offset, &capability))
            return false;
        const uint8_t capability_id = (uint8_t)capability;
        const uint8_t next = (uint8_t)(capability >> 8);
        if (capability_id == EHCI_LEGACY_CAP_ID) {
            const uint32_t request_os_ownership =
                capability | EHCI_LEGACY_OS_OWNED;
            if (!pci_write_config32(device, offset,
                    request_os_ownership)) return false;

            for (uint32_t spin = 0U; spin < EHCI_POLL_LIMIT; ++spin) {
                uint32_t current = 0U;
                if (!pci_read_config32(device, offset, &current))
                    return false;
                if ((current & EHCI_LEGACY_BIOS_OWNED) == 0U) {
                    /* Disable legacy SMI sources before the OS begins polling
                     * the controller. Status fields are W1C, so writing zero
                     * preserves their pending state while clearing enables. */
                    return pci_write_config32(device,
                        (uint16_t)(offset + 4U), 0U);
                }
                __asm__ volatile ("pause");
            }
            serial_write("EHCI: BIOS ownership semaphore did not clear; refusing controller takeover\r\n");
            return false;
        }
        if (next == 0U) return true;
        if (!nexus_ehci_validate_legacy_capability_offset(next, NULL) ||
            next <= offset) return false;
        offset = next;
    }
    return true;
}

static bool ehci_stop_and_reset(struct ehci_controller *controller) {
    const uint32_t operational = controller->capability_length;
    if (!ehci_write32(controller, operational + 0x08U, 0U))
        return false; /* USBINTR */

    uint32_t command = 0U;
    if (!ehci_read32(controller, operational, &command)) return false;
    command &= ~(EHCI_USBCMD_RUN | EHCI_USBCMD_ASYNC_ENABLE |
                 EHCI_USBCMD_PERIODIC_ENABLE | EHCI_USBCMD_RESET);
    if (!ehci_write32(controller, operational, command) ||
        !ehci_wait_register(controller, operational + 0x04U,
                            EHCI_USBSTS_HALTED, true))
        return false;

    if (!ehci_write32(controller, operational,
                      EHCI_USBCMD_RESET) ||
        !ehci_wait_register(controller, operational,
                            EHCI_USBCMD_RESET, false) ||
        !ehci_wait_register(controller, operational + 0x04U,
                            EHCI_USBSTS_HALTED, true))
        return false;

    /* Clear only the defined W1C status bits. */
    if (!ehci_write32(controller, operational + 0x04U,
                      EHCI_USBSTS_W1C_MASK) ||
        !ehci_write32(controller, operational + 0x08U, 0U) ||
        !ehci_write32(controller, operational + 0x10U, 0U))
        return false; /* Interrupts and 32-bit CTRLDSSEGMENT. */
    return true;
}

static bool ehci_halt_for_cleanup(struct ehci_controller *controller) {
    uint32_t command = 0U;
    if (!ehci_read32(controller, controller->capability_length,
                     &command)) return false;
    command &= ~(EHCI_USBCMD_RUN | EHCI_USBCMD_ASYNC_ENABLE |
                 EHCI_USBCMD_PERIODIC_ENABLE);
    if (!ehci_write32(controller, controller->capability_length, command))
        return false;
    return ehci_wait_register(controller,
        (uint32_t)controller->capability_length + 0x04U,
        EHCI_USBSTS_HALTED, true);
}

static void ehci_zero_page(volatile uint8_t *page) {
    for (size_t i = 0U; i < NEXUS_EHCI_PAGE_SIZE; ++i) page[i] = 0U;
}

static struct ehci_controller *ehci_find_controller(
    const struct nexus_pci_device *device) {
    if (device == NULL) return NULL;
    for (size_t i = 0U; i < EHCI_MAX_CONTROLLERS; ++i)
        if (controllers[i].present &&
            controllers[i].pci_device == device) return &controllers[i];
    return NULL;
}

static struct ehci_controller *ehci_allocate_controller_slot(void) {
    for (size_t i = 0U; i < EHCI_MAX_CONTROLLERS; ++i)
        if (!controllers[i].present) return &controllers[i];
    return NULL;
}

static bool ehci_try_controller(const struct nexus_pci_device *device) {
    if (device == NULL ||
        nexus_usb_host_controller_classify(
            device->class_code, device->subclass,
            device->programming_interface) != NEXUS_USB_HOST_EHCI)
        return false;

    struct ehci_controller *controller =
        ehci_allocate_controller_slot();
    if (controller == NULL) return false;
    *controller = (struct ehci_controller){0};
    controller->present = true;
    controller->pci_device = device;

    struct nexus_pci_bar bar = {0};
    if (!pci_decode_bar(device, 0U, &bar) || bar.io_space ||
        !bar.assigned || bar.base == 0U ||
        !pci_enable_memory_bus_master(device)) {
        serial_write("EHCI: unusable memory BAR0 or PCI memory/bus-master enable failed\r\n");
        *controller = (struct ehci_controller){0};
        return false;
    }
    controller->bar_base = bar.base;

    uint32_t cap0 = 0U, hcsparams = 0U, hccparams = 0U;
    if (!ehci_read32(controller, 0x00U, &cap0) ||
        !ehci_read32(controller, 0x04U, &hcsparams) ||
        !ehci_read32(controller, 0x08U, &hccparams)) {
        serial_write("EHCI: capability registers are not accessible\r\n");
        *controller = (struct ehci_controller){0};
        return false;
    }

    struct nexus_ehci_capabilities capabilities = {0};
    const uint8_t capability_length = (uint8_t)(cap0 & 0xFFU);
    const uint16_t interface_version = (uint16_t)(cap0 >> 16);
    if (!nexus_ehci_decode_capabilities(
            capability_length, interface_version,
            hcsparams, hccparams, &capabilities)) {
        serial_write("EHCI: unsupported or malformed capability register set\r\n");
        *controller = (struct ehci_controller){0};
        return false;
    }

    if (capabilities.has_legacy_support &&
        !ehci_handoff_legacy_ownership(
            device, capabilities.legacy_support_offset)) {
        *controller = (struct ehci_controller){0};
        return false;
    }

    controller->capability_length = capabilities.capability_length;
    controller->port_count = capabilities.port_count;
    controller->port_power_control = capabilities.port_power_control;

    if (!ehci_stop_and_reset(controller)) {
        serial_write("EHCI: controller halt/reset sequence failed; no DMA schedule was published\r\n");
        *controller = (struct ehci_controller){0};
        return false;
    }

    uint64_t async_head_physical = 0U;
    if (!memory_alloc_dma_page(EHCI_DMA_LIMIT, &async_head_physical)) {
        serial_write("EHCI: no DMA32 page available for asynchronous schedule head\r\n");
        *controller = (struct ehci_controller){0};
        return false;
    }
    volatile uint8_t *async_page =
        (volatile uint8_t *)memory_phys_to_virt(async_head_physical);
    if (async_page == NULL ||
        async_head_physical > UINT32_MAX - (NEXUS_EHCI_PAGE_SIZE - 1U) ||
        (async_head_physical & (NEXUS_EHCI_PAGE_SIZE - 1U)) != 0U) {
        (void)memory_free_page(async_head_physical);
        *controller = (struct ehci_controller){0};
        return false;
    }
    ehci_zero_page(async_page);
    volatile struct nexus_ehci_qh *head =
        (volatile struct nexus_ehci_qh *)(uintptr_t)async_page;
    if (!nexus_ehci_build_async_head_qh(
            (struct nexus_ehci_qh *)(uintptr_t)head,
            (uint32_t)async_head_physical)) {
        (void)memory_free_page(async_head_physical);
        *controller = (struct ehci_controller){0};
        return false;
    }

    controller->async_head_physical = async_head_physical;
    controller->async_head = head;

    const uint32_t operational = controller->capability_length;
    if (!ehci_write32(controller, operational + 0x18U,
                      (uint32_t)async_head_physical) ||
        !ehci_write32(controller, operational + 0x40U,
                      EHCI_CONFIGFLAG_ROUTE_EHCI)) {
        (void)memory_free_page(async_head_physical);
        *controller = (struct ehci_controller){0};
        return false;
    }

    /* Run with only the asynchronous schedule active. The reclamation head
     * points to itself and contains no qTDs, so it is safe before USB
     * enumeration/control/interrupt transfers have been implemented. */
    if (!ehci_write32(controller, operational,
            EHCI_USBCMD_RUN | EHCI_USBCMD_ASYNC_ENABLE) ||
        !ehci_wait_register(controller, operational + 0x04U,
            EHCI_USBSTS_HALTED, false) ||
        !ehci_wait_register(controller, operational + 0x04U,
            EHCI_USBSTS_ASYNC_STATUS, true)) {
        if (ehci_halt_for_cleanup(controller)) {
            (void)memory_free_page(async_head_physical);
            *controller = (struct ehci_controller){0};
        } else {
            controller->dma_quarantined = true;
            serial_write("EHCI: asynchronous schedule start failed; retaining DMA head because halt was not confirmed\r\n");
        }
        return false;
    }

    controller->running = true;
    for (uint8_t port = 0U; port < controller->port_count; ++port) {
        const uint32_t port_offset = operational +
            EHCI_PORTSC_BASE + (uint32_t)port * 4U;
        uint32_t portsc = 0U;
        if (!ehci_read32(controller, port_offset, &portsc)) {
            (void)ehci_halt_for_cleanup(controller);
            controller->running = false;
            controller->dma_quarantined = true;
            serial_write("EHCI: port register unreadable after start; retaining controller DMA state\r\n");
            return false;
        }
        if (controller->port_power_control &&
            (portsc & EHCI_PORTSC_POWER) == 0U) {
            /* Write only PP. Do not echo W1C connect/enable/over-current
             * change bits back into PORTSC during the power operation. */
            if (!ehci_write32(controller, port_offset,
                              EHCI_PORTSC_POWER)) {
                controller->running = false;
                controller->dma_quarantined = true;
                return false;
            }
        }
        controller->port_connected[port] =
            (portsc & EHCI_PORTSC_CONNECT) != 0U;
    }

    serial_write("EHCI: controller reset, DMA32 async head and polling-mode schedule are active; USB endpoint enumeration is not enabled yet\r\n");
    return true;
}

bool ehci_controller_init_all(void) {
    bool initialized_any = false;
    uint32_t candidates = 0U;
    for (uint32_t i = 0U; i < pci_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL ||
            nexus_usb_host_controller_classify(
                device->class_code, device->subclass,
                device->programming_interface) != NEXUS_USB_HOST_EHCI)
            continue;
        ++candidates;
        struct ehci_controller *existing = ehci_find_controller(device);
        if (existing != NULL) {
            initialized_any = initialized_any || existing->running;
            continue;
        }
        if (ehci_try_controller(device)) initialized_any = true;
    }
    if (candidates == 0U) return false;
    if (!initialized_any)
        serial_write("EHCI: PCI controllers were detected but none completed safe ownership/reset/schedule startup\r\n");
    return initialized_any;
}

void ehci_controller_poll(void) {
    const uint64_t now = timer_ticks();
    for (size_t i = 0U; i < EHCI_MAX_CONTROLLERS; ++i) {
        struct ehci_controller *controller = &controllers[i];
        if (!controller->present || !controller->running) continue;
        if (now < controller->last_port_scan_tick ||
            now - controller->last_port_scan_tick <
                EHCI_PORT_SCAN_INTERVAL_TICKS) continue;
        controller->last_port_scan_tick = now;

        uint32_t status = 0U;
        if (!ehci_read32(controller,
                (uint32_t)controller->capability_length + 0x04U,
                &status) || (status & EHCI_USBSTS_HOST_ERROR) != 0U ||
            (status & EHCI_USBSTS_HALTED) != 0U) {
            controller->running = false;
            serial_write("EHCI: controller halted or raised Host System Error; DMA pages retained pending verified recovery\r\n");
            continue;
        }

        for (uint8_t port = 0U; port < controller->port_count; ++port) {
            uint32_t portsc = 0U;
            const uint32_t offset =
                (uint32_t)controller->capability_length +
                EHCI_PORTSC_BASE + (uint32_t)port * 4U;
            if (!ehci_read32(controller, offset, &portsc)) {
                controller->running = false;
                serial_write("EHCI: root-port MMIO lost; controller DMA retained\r\n");
                break;
            }
            const bool connected =
                (portsc & EHCI_PORTSC_CONNECT) != 0U;
            if (connected != controller->port_connected[port]) {
                controller->port_connected[port] = connected;
                serial_write("EHCI: root port ");
                serial_write_hex((uint32_t)port + 1U);
                serial_write(connected
                    ? " reports a connected USB device (enumeration pending)\r\n"
                    : " reports USB device disconnect (enumeration pending)\r\n");
            }
        }
    }
}

uint32_t ehci_controller_count(void) {
    uint32_t count = 0U;
    for (size_t i = 0U; i < EHCI_MAX_CONTROLLERS; ++i)
        if (controllers[i].present) ++count;
    return count;
}

uint32_t ehci_controller_running_count(void) {
    uint32_t count = 0U;
    for (size_t i = 0U; i < EHCI_MAX_CONTROLLERS; ++i)
        if (controllers[i].present && controllers[i].running) ++count;
    return count;
}
