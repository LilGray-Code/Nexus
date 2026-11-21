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
    }
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
