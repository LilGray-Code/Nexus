#include "storage.h"

#include "pci.h"
#include "nvme.h"
#include "ahci.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static size_t controller_count;

static bool ahci_register_probe(const struct nexus_pci_device *device) {
    if (device == NULL || device->subclass != 0x06 ||
        device->programming_interface != 0x01) {
        return false;
    }

    /* PCI Command bit 1 is Memory Space Enable. Never turn it on here. */
    if ((device->command & 0x0002U) == 0) {
        serial_write("AHCI: PCI memory decode is disabled; MMIO probe skipped\r\n");
        return false;
    }

    struct nexus_pci_bar abar;
    if (!pci_decode_bar(device, 5, &abar) || abar.io_space || !abar.assigned) {
        serial_write("AHCI: BAR5 is not an assigned memory BAR; MMIO probe skipped\r\n");
        return false;
    }

    volatile uint32_t *mapped_page = NULL;
    if (!paging_map_mmio_page(abar.base, &mapped_page) || mapped_page == NULL) {
        serial_write("AHCI: unable to map ABAR page; MMIO probe skipped\r\n");
        return false;
    }

    const uintptr_t page_offset = (uintptr_t)(abar.base & 0xFFFU);
    volatile uint32_t *registers =
        (volatile uint32_t *)((uintptr_t)mapped_page + page_offset);

    /* AHCI 1.x: CAP 0x00, GHC 0x04, PI 0x0C, VS 0x10. Read only. */
    const uint32_t cap = registers[0x00 / 4];
    const uint32_t ghc = registers[0x04 / 4];
    const uint32_t pi  = registers[0x0C / 4];
    const uint32_t vs  = registers[0x10 / 4];

    if (cap == 0 || cap == UINT32_MAX ||
        ghc == UINT32_MAX || pi == UINT32_MAX ||
        vs == 0 || vs == UINT32_MAX || ((vs >> 16) & 0xFFU) == 0) {
        serial_write("AHCI: ABAR did not return a valid register signature\r\n");
        return false;
    }

    uint32_t implemented_ports = 0;
    for (uint8_t bit = 0; bit < 32; ++bit) {
        if ((pi & (1U << bit)) != 0) ++implemented_ports;
    }

    serial_write("AHCI: CAP: ");
    serial_write_hex(cap);
    serial_write("\r\nAHCI: GHC: ");
    serial_write_hex(ghc);
    serial_write("\r\nAHCI: PI: ");
    serial_write_hex(pi);
    serial_write("\r\nAHCI: version: ");
    serial_write_hex(vs);
    serial_write("\r\nAHCI: ports implemented: ");
    serial_write_hex(implemented_ports);
    serial_write("\r\n");
    uint32_t connected_ports = 0;
    for (uint8_t port = 0; port < 32; ++port) {
        if ((pi & (1U << port)) == 0) continue;

        const uint32_t port_offset = 0x100U + (uint32_t)port * 0x80U;
        /* The current MMIO mapper maps one 4 KiB page at a time. */
        if (page_offset + port_offset + 0x2CU >= 0x1000U) {
            serial_write("AHCI: port registers outside mapped page; skipped\r\n");
            continue;
        }

        const uint32_t ssts = registers[(port_offset + 0x28U) / 4U];
        const uint32_t signature = registers[(port_offset + 0x24U) / 4U];
        if (ssts == UINT32_MAX) continue;

        const uint32_t det = ssts & 0x0FU;
        serial_write("AHCI: port ");
        serial_write_hex(port);
        serial_write(det == 3U ? " link=up" :
                     det == 1U ? " link=device-detected" : " link=down");
        serial_write(" SSTS=");
        serial_write_hex(ssts);
        serial_write(" SIG=");
        serial_write_hex(signature);

        if (det == 3U) {
            ++connected_ports;
            if (signature == 0x00000101U) {
                serial_write(" type=ATA");
            } else if (signature == 0xEB140101U) {
                serial_write(" type=ATAPI");
            } else if (signature == 0x96690101U) {
                serial_write(" type=port-multiplier");
            } else if (signature == 0xC33C0101U) {
                serial_write(" type=enclosure");
            } else {
                serial_write(" type=unknown");
            }
        }
        serial_write("\r\n");
    }

    serial_write("AHCI: connected ports: ");
    serial_write_hex(connected_ports);
    serial_write("\r\n");
    serial_write("AHCI: register probe was read-only; no commands issued\r\n");
    serial_write("NEXUS AHCI PORT DISCOVERY READY\r\n");
    serial_write("NEXUS AHCI REGISTER PROBE READY\r\n");
    return true;
}

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
    serial_write("STORAGE: classifying controllers and running read-only identify probes\r\n");

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
        serial_write_hex(device->slot);
        serial_write(".");
        serial_write_hex(device->function);
        serial_write(" vendor=");
        serial_write_hex(device->vendor_id);
        serial_write(" device=");
        serial_write_hex(device->device_id);
        serial_write(" prog-if=");
        serial_write_hex(device->programming_interface);
        serial_write("\r\n");
        
        for (uint8_t bar_index = 0; bar_index < NEXUS_PCI_BAR_COUNT; ++bar_index) {
            struct nexus_pci_bar bar;
            if (!pci_decode_bar(device, bar_index, &bar)) continue;

            serial_write("STORAGE: BAR ");
            serial_write_hex(bar_index);
            serial_write(bar.io_space ? " I/O" : " MMIO");
            serial_write(bar.is_64bit ? " 64-bit" : " 32-bit");
            if (bar.prefetchable) serial_write(" prefetchable");
            serial_write(bar.assigned ? " base=" : " unassigned base=");
            serial_write_hex(bar.base);
            serial_write("\r\n");
        }

        if (device->subclass == 0x06 &&
            device->programming_interface == 0x01) {
            (void)ahci_init_controller(device);
            (void)ahci_register_probe(device);
        } else if (device->subclass == 0x08 &&
                   device->programming_interface == 0x02) {
            (void)nvme_identify_probe(device);
        }

    }

    serial_write("STORAGE: controllers discovered: ");
    serial_write_hex(controller_count);
    serial_write("\r\n");

    return controller_count != 0;
}

size_t storage_controller_count(void) {
    return controller_count;
}
