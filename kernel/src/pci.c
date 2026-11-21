#include "pci.h"

#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0CF8U
#define PCI_CONFIG_DATA    0x0CF CU
#define PCI_ENABLE_BIT     0x80000000U
#define PCI_MAX_BUSES      256U
#define PCI_MAX_DEVICES    32U
#define PCI_MAX_FUNCTIONS  8U

static struct nexus_pci_device devices[NEXUS_PCI_MAX_DEVICES];
static size_t stored_devices;

static inline void port_out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t port_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/*
 * PCI Configuration Mechanism #1. All accesses are read-only; the kernel
 * never sets command bits, sizes BARs, or changes bus routing during probe.
 */
static uint32_t config_read32(uint8_t bus, uint8_t device,
                              uint8_t function, uint8_t offset) {
    const uint32_t address = PCI_ENABLE_BIT |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCU);
    port_out32(PCI_CONFIG_ADDRESS, address);
    return port_in32(PCI_CONFIG_DATA);
}

static void print_device(const struct nexus_pci_device *d) {
    serial_write("PCI: device ");
    serial_write_hex(d->bus);
    serial_write(":");
    serial_write_hex(d->device);
    serial_write(".");
    serial_write_hex(d->function);
    serial_write(" vendor=");
    serial_write_hex(d->vendor_id);
    serial_write(" device=");
    serial_write_hex(d->device_id);
    serial_write(" class=");
    serial_write_hex(d->class_code);
    serial_write(" subclass=");
    serial_write_hex(d->subclass);
    serial_write(" prog-if=");
    serial_write_hex(d->programming_interface);
    serial_write("\r\n");
}

static void record_function(uint8_t bus, uint8_t device, uint8_t function,
                            uint32_t identity) {
    const uint32_t class_register =
        config_read32(bus, device, function, 0x08);
    const uint32_t header_register =
        config_read32(bus, device, function, 0x0C);
    const uint32_t interrupt_register =
        config_read32(bus, device, function, 0x3C);

    struct nexus_pci_device entry = {
        .bus = bus,
        .device = device,
        .function = function,
        .header_type = (uint8_t)(header_register >> 16),
        .vendor_id = (uint16_t)(identity & 0xFFFFU),
        .device_id = (uint16_t)(identity >> 16),
        .class_code = (uint8_t)(class_register >> 24),
        .subclass = (uint8_t)(class_register >> 16),
        .programming_interface = (uint8_t)(class_register >> 8),
        .revision = (uint8_t)class_register,
        .interrupt_line = (uint8_t)interrupt_register,
        .interrupt_pin = (uint8_t)(interrupt_register >> 8),
        .bars = {0}
    };

    uint8_t bar_count = 0;
    switch (entry.header_type & 0x7FU) {
        case 0: bar_count = 6; break; /* Endpoint / normal device. */
        case 1: bar_count = 2; break; /* PCI-to-PCI bridge. */
        case 2: bar_count = 1; break; /* CardBus bridge. */
        default: break;
    }

    for (uint8_t i = 0; i < bar_count; ++i) {
        entry.bars[i] =
            config_read32(bus, device, function, (uint8_t)(0x10U + i * 4U));
    }

    if (stored_devices < NEXUS_PCI_MAX_DEVICES) {
        devices[stored_devices++] = entry;
        print_device(&entry);
    }
}

bool pci_init(void) {
    stored_devices = 0;
    serial_write("PCI: scanning configuration mechanism 1 (read-only)\r\n");

    for (uint16_t bus = 0; bus < PCI_MAX_BUSES; ++bus) {
        for (uint8_t device = 0; device < PCI_MAX_DEVICES; ++device) {
            const uint32_t function_zero =
                config_read32((uint8_t)bus, device, 0, 0x00);
            if ((uint16_t)function_zero == 0xFFFFU) continue;

            record_function((uint8_t)bus, device, 0, function_zero);

            const uint32_t header =
                config_read32((uint8_t)bus, device, 0, 0x0C);
            if (((header >> 16) & 0x80U) == 0) continue;

            for (uint8_t function = 1; function < PCI_MAX_FUNCTIONS; ++function) {
                const uint32_t identity =
                    config_read32((uint8_t)bus, device, function, 0x00);
                if ((uint16_t)identity == 0xFFFFU) continue;
                record_function((uint8_t)bus, device, function, identity);
            }
        }
    }

    serial_write("PCI: devices discovered: ");
    serial_write_hex(stored_devices);
    serial_write("\r\n");
    return stored_devices != 0;
}

size_t pci_device_count(void) {
    return stored_devices;
}

const struct nexus_pci_device *pci_device_at(size_t index) {
    return index < stored_devices ? &devices[index] : NULL;
}
