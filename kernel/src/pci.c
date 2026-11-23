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
static uint32_t wireless_device_count;

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
    if (class_code == 0x02U) ++network_device_count;
    if (class_code == 0x0DU) ++wireless_device_count;

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
    wireless_device_count = 0;

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
    serial_write("\r\nPCI: wireless-controller functions: ");
    serial_write_hex(wireless_device_count);
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

    for (uint32_t i = 0U; i < stored_device_count; ++i) {
        const struct nexus_pci_device *device = &stored_devices[i];
        if (!pci_is_wireless_lan_candidate(device)) continue;
        serial_write("PCI: WLAN-CANDIDATE BDF ");
        serial_write_hex(device->bus);
        serial_write(":");
        serial_write_hex(device->slot);
        serial_write(".");
        serial_write_hex(device->function);
        serial_write(" VID:PID ");
        serial_write_hex(device->vendor_id);
        serial_write(":");
        serial_write_hex(device->device_id);
        serial_write(" class/subclass ");
        serial_write_hex(device->class_code);
        serial_write("/");
        serial_write_hex(device->subclass);
        serial_write(" status=NO-CHIPSET-DRIVER-BOUND\r\n");
    }

    serial_write("PCI: scan complete; device configuration remains read-only\r\n");
    return discovered_device_count != 0;
}

uint32_t pci_device_count(void) {
    return stored_device_count;
}

uint32_t pci_discovered_device_count(void) {
    return discovered_device_count;
}

uint32_t pci_wireless_device_count(void) {
    return wireless_device_count;
}

const struct nexus_pci_device *pci_device_at(uint32_t index) {
    if (index >= stored_device_count) return NULL;
    return &stored_devices[index];
}


const struct nexus_pci_device *pci_find_matching_device(
    uint16_t vendor_id, uint16_t device_id, uint8_t class_code,
    uint8_t subclass, uint8_t programming_interface) {
    for (uint32_t i = 0U; i < stored_device_count; ++i) {
        if (pci_device_matches(&stored_devices[i], vendor_id, device_id,
                               class_code, subclass,
                               programming_interface)) {
            return &stored_devices[i];
        }
    }
    return NULL;
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


bool pci_get_msi_info(const struct nexus_pci_device *device,
                      struct nexus_pci_msi_info *info) {
    if (device == NULL || info == NULL) return false;

    *info = (struct nexus_pci_msi_info){0};

    uint8_t capability_offset = 0U;
    /* PCI_CAP_ID_MSI is 0x05 in the conventional capability list. */
    if (!pci_find_capability(device, 0x05U, &capability_offset)) {
        return false;
    }

    const uint32_t control_register = pci_config_read(
        device->bus, device->slot, device->function,
        (uint8_t)(capability_offset + 0x00U));
    if (control_register == UINT32_MAX) return false;

    const uint16_t message_control = (uint16_t)(control_register >> 16);
    const uint8_t message_exponent =
        (uint8_t)((message_control >> 1) & 0x07U);

    info->present = true;
    info->enabled = (message_control & 0x0001U) != 0U;
    info->maximum_message_exponent = message_exponent;
    info->maximum_message_count = (uint16_t)(1U << message_exponent);
    info->supports_64bit_address = (message_control & 0x0080U) != 0U;
    info->per_vector_masking = (message_control & 0x0100U) != 0U;
    return true;
}
