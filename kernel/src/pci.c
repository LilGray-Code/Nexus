#include "pci.h"

#include "acpi.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA    0x0CFC
#define PCI_VENDOR_ABSENT  0xFFFFU
#define PCI_MAX_PRINTED_DEVICES 12U

static struct nexus_pci_device stored_devices[NEXUS_PCI_MAX_STORED_DEVICES];
static uint32_t stored_device_count;
static uint32_t discovered_device_count;
static uint32_t storage_device_count;
static uint32_t display_device_count;
static uint32_t network_device_count;

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

/*
 * Use PCIe Enhanced Configuration Access Mechanism (ECAM) whenever ACPI
 * publishes an MCFG window. Each function's 4 KiB config page is mapped
 * through the kernel's uncached MMIO window. Legacy CF8/CFC is retained only
 * for segment zero buses that firmware did not describe in MCFG.
 */
static uint32_t pci_config_read(uint16_t segment, uint8_t bus,
                                uint8_t slot, uint8_t function,
                                uint16_t offset) {
    if (slot >= 32U || function >= 8U || offset > 0x0FFFU)
        return UINT32_MAX;

    const struct nexus_acpi_mcfg_region *region =
        acpi_find_mcfg_region(segment, bus);
    if (region != NULL) {
        const uint64_t bus_offset =
            ((uint64_t)bus - region->start_bus) << 20;
        const uint64_t physical =
            region->base_address + bus_offset +
            ((uint64_t)slot << 15) + ((uint64_t)function << 12) +
            (uint64_t)(offset & 0x0FFCU);
        volatile uint32_t *page = NULL;
        if (!paging_map_mmio_page(physical, &page)) return UINT32_MAX;
        return page[(physical & 0xFFFU) / sizeof(uint32_t)];
    }

    if (segment != 0U || offset > 0x00FCU) return UINT32_MAX;
    const uint32_t address = 0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static bool pci_config_write16(uint16_t segment, uint8_t bus,
                               uint8_t slot, uint8_t function,
                               uint16_t offset, uint16_t value) {
    if (slot >= 32U || function >= 8U || offset > 0x0FFEU ||
        (offset & 1U) != 0U) return false;

    const struct nexus_acpi_mcfg_region *region =
        acpi_find_mcfg_region(segment, bus);
    if (region != NULL) {
        const uint64_t physical =
            region->base_address +
            (((uint64_t)bus - region->start_bus) << 20) +
            ((uint64_t)slot << 15) + ((uint64_t)function << 12) +
            (uint64_t)(offset & 0x0FFCU);
        volatile uint32_t *page = NULL;
        if (!paging_map_mmio_page(physical, &page)) return false;
        volatile uint16_t *register_value =
            (volatile uint16_t *)((uintptr_t)page + (physical & 0xFFFU));
        *register_value = value;
        return true;
    }

    if (segment != 0U || offset > 0x00FEU) return false;
    const uint32_t address = 0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA, value);
    return true;
}

static void record_device(uint16_t segment, uint8_t bus, uint8_t slot,
                          uint8_t function, uint32_t identity,
                          uint32_t class_revision, uint32_t header_dword) {
    const uint16_t vendor = (uint16_t)(identity & 0xFFFFU);
    const uint16_t device = (uint16_t)(identity >> 16);
    const uint8_t class_code = (uint8_t)(class_revision >> 24);
    const uint8_t subclass = (uint8_t)(class_revision >> 16);
    const uint8_t prog_if = (uint8_t)(class_revision >> 8);
    const uint8_t header_type = (uint8_t)(header_dword >> 16);

    if (vendor == PCI_VENDOR_ABSENT) return;

    ++discovered_device_count;
    if (class_code == 0x01) ++storage_device_count;
    if (class_code == 0x03) ++display_device_count;
    if (class_code == 0x02) ++network_device_count;

    if (stored_device_count < NEXUS_PCI_MAX_STORED_DEVICES) {
        struct nexus_pci_device *saved =
            &stored_devices[stored_device_count++];
        saved->segment_group = segment;
        saved->bus = bus;
        saved->slot = slot;
        saved->function = function;
        saved->header_type = header_type;
        saved->class_code = class_code;
        saved->subclass = subclass;
        saved->programming_interface = prog_if;
        saved->revision_id = (uint8_t)class_revision;
        saved->vendor_id = vendor;
        saved->device_id = device;

        /* Subsystem IDs distinguish OEM board variants that share one
         * chipset/device ID. Offset 0x2C is present in Type 0 and Type 1
         * PCI headers. This is a read-only inventory operation.
         */
        if ((header_type & 0x7FU) == 0U ||
            (header_type & 0x7FU) == 1U) {
            const uint32_t subsystem_ids =
                pci_config_read(segment, bus, slot, function, 0x2CU);
            if (subsystem_ids != UINT32_MAX) {
                saved->subsystem_vendor_id = (uint16_t)subsystem_ids;
                saved->subsystem_device_id =
                    (uint16_t)(subsystem_ids >> 16);
            }
        }

        const uint32_t command_status =
            pci_config_read(segment, bus, slot, function, 0x04);
        const uint32_t interrupt_register =
            pci_config_read(segment, bus, slot, function, 0x3C);
        saved->command = (uint16_t)command_status;
        saved->status = (uint16_t)(command_status >> 16);
        saved->interrupt_line = (uint8_t)interrupt_register;
        saved->interrupt_pin = (uint8_t)(interrupt_register >> 8);

        for (uint8_t i = 0; i < NEXUS_PCI_BAR_COUNT; ++i) {
            saved->bars[i] = 0;
        }

        uint8_t bar_count = 0;
        switch (header_type & 0x7FU) {
            case 0: bar_count = 6; break;
            case 1: bar_count = 2; break;
            case 2: bar_count = 1; break;
            default: break;
        }
        for (uint8_t i = 0; i < bar_count; ++i) {
            saved->bars[i] =
                pci_config_read(segment, bus, slot, function,
                                (uint16_t)(0x10U + i * 4U));
        }
    }
}

bool pci_read_config8(const struct nexus_pci_device *device,
                      uint16_t offset, uint8_t *value) {
    if (device == NULL || value == NULL || offset > 0x0FFFU) return false;
    const uint32_t raw = pci_config_read(device->segment_group, device->bus,
                                         device->slot, device->function, offset);
    if (raw == UINT32_MAX) return false;
    *value = (uint8_t)(raw >> ((offset & 3U) * 8U));
    return true;
}

bool pci_read_config16(const struct nexus_pci_device *device,
                       uint16_t offset, uint16_t *value) {
    if (device == NULL || value == NULL || offset > 0x0FFEU) return false;
    uint8_t low = 0U, high = 0U;
    if (!pci_read_config8(device, offset, &low) ||
        !pci_read_config8(device, (uint16_t)(offset + 1U), &high))
        return false;
    *value = (uint16_t)low | ((uint16_t)high << 8);
    return true;
}

bool pci_read_config32(const struct nexus_pci_device *device,
                       uint16_t offset, uint32_t *value) {
    if (device == NULL || value == NULL || offset > 0x0FFCU ||
        (offset & 3U) != 0U) return false;
    const uint32_t raw = pci_config_read(device->segment_group, device->bus,
                                         device->slot, device->function, offset);
    if (raw == UINT32_MAX) return false;
    *value = raw;
    return true;
}

bool pci_enable_io_bus_master(const struct nexus_pci_device *device) {
    if (device == NULL) return false;
    uint16_t command = 0U, updated = 0U;
    if (!pci_read_config16(device, 0x04U, &command) ||
        !pci_config_write16(device->segment_group, device->bus,
                            device->slot, device->function, 0x04U,
                            (uint16_t)(command | 0x0005U)) ||
        !pci_read_config16(device, 0x04U, &updated)) return false;
    return (updated & 0x0005U) == 0x0005U;
}

bool pci_enable_memory_bus_master(const struct nexus_pci_device *device) {
    if (device == NULL) return false;
    uint16_t command = 0U, updated = 0U;
    if (!pci_read_config16(device, 0x04U, &command) ||
        !pci_config_write16(device->segment_group, device->bus,
                            device->slot, device->function, 0x04U,
                            (uint16_t)(command | 0x0006U)) ||
        !pci_read_config16(device, 0x04U, &updated)) return false;
    return (updated & 0x0006U) == 0x0006U;
}

static void pci_scan_bus(uint16_t segment, uint8_t bus) {
    for (uint8_t slot = 0U; slot < 32U; ++slot) {
        const uint32_t function0_identity =
            pci_config_read(segment, bus, slot, 0U, 0x00U);
        if ((function0_identity & 0xFFFFU) == PCI_VENDOR_ABSENT) continue;

        const uint32_t function0_header =
            pci_config_read(segment, bus, slot, 0U, 0x0CU);
        const uint8_t header_type = (uint8_t)(function0_header >> 16);
        const uint8_t function_limit = (header_type & 0x80U) ? 8U : 1U;

        for (uint8_t function = 0U; function < function_limit; ++function) {
            const uint32_t identity = function == 0U
                ? function0_identity
                : pci_config_read(segment, bus, slot, function, 0x00U);
            if ((identity & 0xFFFFU) == PCI_VENDOR_ABSENT) continue;

            const uint32_t class_revision =
                pci_config_read(segment, bus, slot, function, 0x08U);
            const uint32_t header = function == 0U
                ? function0_header
                : pci_config_read(segment, bus, slot, function, 0x0CU);
            record_device(segment, bus, slot, function, identity,
                          class_revision, header);
        }
    }
}

bool pci_init(void) {
    stored_device_count = 0;
    discovered_device_count = 0;
    storage_device_count = 0;
    display_device_count = 0;
    network_device_count = 0;

    const struct nexus_acpi_topology *acpi = acpi_get_topology();
    const uint32_t mcfg_count = acpi != NULL ? acpi->mcfg_region_count : 0U;
    serial_write("PCI: scanning segment zero with ECAM where available; MCFG regions=");
    serial_write_hex(mcfg_count);
    serial_write("\r\n");

    /* Segment zero retains the legacy fallback for buses outside MCFG. */
    for (uint16_t bus = 0U; bus < 256U; ++bus)
        pci_scan_bus(0U, (uint8_t)bus);

    /* Enumerate every firmware-described nonzero segment as its own hierarchy. */
    if (acpi != NULL) {
        for (uint32_t region_index = 0U;
             region_index < acpi->mcfg_region_count; ++region_index) {
            const struct nexus_acpi_mcfg_region *region =
                &acpi->mcfg_regions[region_index];
            if (region->segment_group == 0U) continue;
            for (uint16_t bus = region->start_bus;
                 bus <= region->end_bus; ++bus)
                pci_scan_bus(region->segment_group, (uint8_t)bus);
        }
    }

    serial_write("PCI: functions discovered: ");
    serial_write_hex(discovered_device_count);
    serial_write("\r\nPCI: storage-class functions: ");
    serial_write_hex(storage_device_count);
    serial_write("\r\nPCI: display-class functions: ");
    serial_write_hex(display_device_count);
    serial_write("\r\nPCI: network-class functions: ");
    serial_write_hex(network_device_count);
    serial_write("\r\n");

    const uint32_t printed = stored_device_count < PCI_MAX_PRINTED_DEVICES
        ? stored_device_count : PCI_MAX_PRINTED_DEVICES;
    for (uint32_t i = 0; i < printed; ++i) {
        const struct nexus_pci_device *device = &stored_devices[i];
        serial_write("PCI: segment/bus ");
        serial_write_hex(device->segment_group);
        serial_write("/");
        serial_write_hex(device->bus);
        serial_write(":");
        serial_write_hex(device->slot);
        serial_write(".");
        serial_write_hex(device->function);
        serial_write(" vendor/device ");
        serial_write_hex(device->vendor_id);
        serial_write("/");
        serial_write_hex(device->device_id);
        serial_write(" class/subclass ");
        serial_write_hex(device->class_code);
        serial_write("/");
        serial_write_hex(device->subclass);
        serial_write("\r\n");
    }

    serial_write("PCI: scan complete; device configuration remains read-only\r\n");
    return discovered_device_count != 0;
}

uint32_t pci_device_count(void) {
    return discovered_device_count;
}

uint32_t pci_stored_device_count(void) {
    return stored_device_count;
}

const struct nexus_pci_device *pci_device_at(uint32_t index) {
    if (index >= stored_device_count) return NULL;
    return &stored_devices[index];
}

const struct nexus_pci_device *pci_find_class(uint8_t class_code,
                                               uint8_t subclass,
                                               uint32_t start_index,
                                               uint32_t *found_index) {
    if (found_index != NULL) *found_index = UINT32_MAX;
    if (start_index >= stored_device_count) return NULL;
    for (uint32_t i = start_index; i < stored_device_count; ++i) {
        if (stored_devices[i].class_code == class_code &&
            stored_devices[i].subclass == subclass) {
            if (found_index != NULL) *found_index = i;
            return &stored_devices[i];
        }
    }
    return NULL;
}

bool pci_decode_bar(const struct nexus_pci_device *device, uint8_t index,
                    struct nexus_pci_bar *bar) {
    if (device == NULL || bar == NULL || index >= NEXUS_PCI_BAR_COUNT)
        return false;

    *bar = (struct nexus_pci_bar){0};

    /* A 64-bit BAR consumes the following BAR slot as its upper dword. */
    for (uint8_t i = 0; i < index; ++i) {
        const uint32_t previous = device->bars[i];
        if (previous != 0 && (previous & 1U) == 0 &&
            ((previous >> 1) & 3U) == 2U) {
            if ((uint8_t)(i + 1U) == index) return false;
            ++i;
        }
    }

    const uint32_t raw = device->bars[index];
    if (raw == 0 || raw == UINT32_MAX) return false;

    bar->present = true;
    if ((raw & 1U) != 0) {
        bar->io_space = true;
        bar->base = (uint64_t)(raw & ~3U);
        bar->assigned = bar->base != 0;
        return true;
    }

    const uint32_t memory_type = (raw >> 1) & 3U;
    if (memory_type == 3U) {
        *bar = (struct nexus_pci_bar){0};
        return false;
    }

    bar->prefetchable = (raw & 8U) != 0;
    if (memory_type == 2U) {
        if (index + 1U >= NEXUS_PCI_BAR_COUNT) {
            *bar = (struct nexus_pci_bar){0};
            return false;
        }
        bar->is_64bit = true;
        bar->base = ((uint64_t)device->bars[index + 1U] << 32) |
                    (uint64_t)(raw & ~0xFU);
    } else if (memory_type == 1U) {
        bar->base = (uint64_t)(raw & 0x000FFFF0U);
    } else {
        bar->base = (uint64_t)(raw & ~0xFU);
    }

    bar->assigned = bar->base != 0;
    return true;
}
