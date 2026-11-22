#include "pci.h"

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
 * PCI Configuration Mechanism #1. This writes only the address selector at
 * CF8 and reads the selected config dword at CFC; it never writes device
 * configuration space, BARs, command registers, or bus-master controls.
 */
static uint32_t pci_config_read(uint8_t bus, uint8_t slot,
                                uint8_t function, uint8_t offset) {
    if (slot >= 32 || function >= 8) return UINT32_MAX;

    const uint32_t address = 0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCU);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void record_device(uint8_t bus, uint8_t slot, uint8_t function,
                          uint32_t identity, uint32_t class_revision,
                          uint32_t header_dword) {
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
        saved->bus = bus;
        saved->slot = slot;
        saved->function = function;
        saved->header_type = header_type;
        saved->class_code = class_code;
        saved->subclass = subclass;
        saved->programming_interface = prog_if;
        saved->vendor_id = vendor;
        saved->device_id = device;

        const uint32_t command_status =
            pci_config_read(bus, slot, function, 0x04);
        const uint32_t interrupt_register =
            pci_config_read(bus, slot, function, 0x3C);
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
                pci_config_read(bus, slot, function,
                                (uint8_t)(0x10U + i * 4U));
        }
    }
}

bool pci_enable_memory_bus_master(const struct nexus_pci_device *device) {
    if (device == NULL) return false;

    /*
     * PCI Status is write-one-to-clear. Write only the low 16-bit Command
     * register so we do not accidentally clear status bits by echoing them.
     */
    const uint32_t address = 0x80000000U |
        ((uint32_t)device->bus << 16) |
        ((uint32_t)device->slot << 11) |
        ((uint32_t)device->function << 8) |
        0x04U;
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA, (uint16_t)(device->command | 0x0006U));

    const uint32_t updated = pci_config_read(device->bus, device->slot,
                                             device->function, 0x04);
    return (updated & 0x0006U) == 0x0006U;
}

bool pci_init(void) {
    stored_device_count = 0;
    discovered_device_count = 0;
    storage_device_count = 0;
    display_device_count = 0;
    network_device_count = 0;

    serial_write("PCI: read-only configuration mechanism #1 scan started\r\n");

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            const uint32_t function0_identity =
                pci_config_read((uint8_t)bus, slot, 0, 0x00);
            if ((function0_identity & 0xFFFFU) == PCI_VENDOR_ABSENT) {
                continue;
            }

            const uint32_t function0_header =
                pci_config_read((uint8_t)bus, slot, 0, 0x0C);
            const uint8_t header_type =
                (uint8_t)(function0_header >> 16);
            const uint8_t function_limit =
                (header_type & 0x80U) ? 8U : 1U;

            for (uint8_t function = 0; function < function_limit; ++function) {
                const uint32_t identity = function == 0
                    ? function0_identity
                    : pci_config_read((uint8_t)bus, slot, function, 0x00);
                if ((identity & 0xFFFFU) == PCI_VENDOR_ABSENT) continue;

                const uint32_t class_revision =
                    pci_config_read((uint8_t)bus, slot, function, 0x08);
                const uint32_t header =
                    function == 0
                    ? function0_header
                    : pci_config_read((uint8_t)bus, slot, function, 0x0C);

                record_device((uint8_t)bus, slot, function,
                              identity, class_revision, header);
            }
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
        serial_write("PCI: ");
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

const struct nexus_pci_device *pci_device_at(uint32_t index) {
    if (index >= stored_device_count) return NULL;
    return &stored_devices[index];
}


bool pci_find_capability(const struct nexus_pci_device *device,
                         uint8_t capability_id, uint8_t *offset) {
    if (device == NULL || offset == NULL ||
        (device->status & 0x0010U) == 0U) {
        return false;
    }

    /* Status bit 4 advertises a conventional capability list. */
    uint8_t cursor = (uint8_t)pci_config_read(
        device->bus, device->slot, device->function, 0x34U);

    /* Config space from 0x40 through 0xFC contains at most 48 dwords.
     * The hard bound also makes malformed/cyclic lists harmless.
     */
    for (uint8_t step = 0U; step < 48U; ++step) {
        if (cursor < 0x40U || cursor > 0xFCU ||
            (cursor & 0x03U) != 0U) {
            return false;
        }

        const uint32_t capability = pci_config_read(
            device->bus, device->slot, device->function, cursor);
        if (capability == UINT32_MAX) return false;

        if ((uint8_t)capability == capability_id) {
            *offset = cursor;
            return true;
        }

        const uint8_t next = (uint8_t)(capability >> 8);
        if (next == 0U) return false;
        cursor = next;
    }

    /* More than 48 entries implies a corrupt or cyclic list. */
    return false;
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
