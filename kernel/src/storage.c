#include "storage.h"

#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static size_t controller_count;

static const char *storage_kind(uint8_t subclass, uint8_t programming_interface) {
    switch (subclass) {
        case 0x00: return "SCSI";
        case 0x01: return "IDE";
        case 0x04: return "RAID";
        case 0x05: return "ATA";
        case 0x06:
            return (programming_interface == 0x01)
                ? "AHCI/SATA" : "SATA";
        case 0x07: return "SAS";
        case 0x08: return "NVMe";
        case 0x09: return "UFS";
        default:   return "Other mass-storage";
    }
}

bool storage_probe(void) {
    controller_count = 0;
    serial_write("STORAGE: classifying PCI mass-storage controllers (read-only)\r\n");

    const size_t count = pci_device_count();
    for (size_t i = 0; i < count; ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL || device->class_code != 0x01) continue;

        ++controller_count;
        serial_write("STORAGE: controller ");
        serial_write(storage_kind(device->subclass,
                                  device->programming_interface));
        serial_write(" at ");
        serial_write_hex(device->bus);
        serial_write(":");
        serial_write_hex(device->device);
        serial_write(".");
        serial_write_hex(device->function);
        serial_write(" vendor=");
        serial_write_hex(device->vendor_id);
        serial_write(" device=");
        serial_write_hex(device->device_id);
        serial_write(" prog-if=");
        serial_write_hex(device->programming_interface);
        serial_write("\r\n");
    }

    serial_write("STORAGE: controllers discovered: ");
    serial_write_hex(controller_count);
    serial_write("\r\n");

    return controller_count != 0;
}

size_t storage_controller_count(void) {
    return controller_count;
}
