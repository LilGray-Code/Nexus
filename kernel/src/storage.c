#include "storage.h"
#include "driver_manager.h"

#include "e1000.h"
#include "rtl8139.h"
#include "virtio_net.h"
#include "ac97.h"
#include "xhci.h"
#include "virtio_gpu.h"

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


static bool storage_ahci_driver_probe(
    const struct nexus_driver_device *device, void *context) {
    (void)context;
    if (device == NULL || device->native_device == NULL) return false;
    const struct nexus_pci_device *pci =
        (const struct nexus_pci_device *)device->native_device;
    if (!ahci_init_controller(pci)) return false;
    (void)ahci_register_probe(pci); /* Additional register inventory is read-only. */
    return true;
}
static bool storage_nvme_driver_probe(
    const struct nexus_driver_device *device, void *context) {
    (void)context;
    if (device == NULL || device->native_device == NULL) return false;
    return nvme_identify_probe(
        (const struct nexus_pci_device *)device->native_device);
}
static bool e1000_device_supported(const struct nexus_pci_device *pci) {
    if (pci == NULL || pci->vendor_id != 0x8086U ||
        pci->class_code != 0x02U || pci->subclass != 0x00U) return false;
    switch (pci->device_id) {
        case 0x100EU: case 0x101EU: case 0x100FU:
        case 0x1026U: case 0x1010U: case 0x10D3U:
            return true;
        default:
            return false;
    }
}

static bool first_matching_pci_device(const struct nexus_pci_device *device,
                                      bool (*predicate)(const struct nexus_pci_device *)) {
    if (device == NULL || predicate == NULL) return false;
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *candidate = pci_device_at(i);
        if (predicate(candidate)) return candidate == device;
    }
    return false;
}

static bool e1000_pnp_probe(const struct nexus_driver_device *device,
                            void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
           e1000_ready() &&
           first_matching_pci_device(
               (const struct nexus_pci_device *)device->native_device,
               e1000_device_supported);
}

static bool rtl8139_device_supported(const struct nexus_pci_device *pci) {
    return pci != NULL && pci->vendor_id == 0x10ECU &&
           pci->device_id == 0x8139U && pci->class_code == 0x02U;
}

static bool rtl8139_pnp_probe(const struct nexus_driver_device *device,
                              void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
           rtl8139_ready() &&
           first_matching_pci_device(
               (const struct nexus_pci_device *)device->native_device,
               rtl8139_device_supported);
}

static bool virtio_net_device_supported(const struct nexus_pci_device *pci) {
    return pci != NULL && pci->vendor_id == 0x1AF4U &&
           pci->device_id == 0x1041U;
}

static bool virtio_net_pnp_probe(const struct nexus_driver_device *device,
                                 void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
           virtio_net_ready() &&
           first_matching_pci_device(
               (const struct nexus_pci_device *)device->native_device,
               virtio_net_device_supported);
}

static bool ac97_pnp_probe(const struct nexus_driver_device *device,
                           void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
        device->class_code == 0x04U && device->subclass == 0x01U &&
        ac97_owns_pci_device(
            (const struct nexus_pci_device *)device->native_device);
}

static bool xhci_pnp_probe(const struct nexus_driver_device *device,
                           void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
        device->class_code == 0x0CU && device->subclass == 0x03U &&
        device->programming_interface == 0x30U &&
        xhci_controller_owns_pci_device(
            (const struct nexus_pci_device *)device->native_device);
}

static bool virtio_gpu_pnp_probe(const struct nexus_driver_device *device,
                                 void *context) {
    (void)context;
    return device != NULL && device->native_device != NULL &&
        device->vendor_id == 0x1AF4U && device->device_id == 0x1050U &&
        virtio_gpu_owns_pci_device(
            (const struct nexus_pci_device *)device->native_device);
}

static bool storage_register_drivers(void) {
    const struct nexus_driver_descriptor descriptors[] = {
        {
            .name = "ahci", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = NEXUS_DRIVER_WILDCARD_U16,
                .device_id = NEXUS_DRIVER_WILDCARD_U16,
                .class_code = 0x01U, .subclass = 0x06U,
                .programming_interface = 0x01U
            },
            .probe = storage_ahci_driver_probe
        },
        {
            .name = "nvme", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = NEXUS_DRIVER_WILDCARD_U16,
                .device_id = NEXUS_DRIVER_WILDCARD_U16,
                .class_code = 0x01U, .subclass = 0x08U,
                .programming_interface = 0x02U
            },
            .probe = storage_nvme_driver_probe
        },
        {
            .name = "e1000", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = 0x8086U,
                .device_id = NEXUS_DRIVER_WILDCARD_U16,
                .class_code = 0x02U, .subclass = 0x00U,
                .programming_interface = NEXUS_DRIVER_WILDCARD_U8
            },
            .probe = e1000_pnp_probe
        },
        {
            .name = "rtl8139", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = 0x10ECU, .device_id = 0x8139U,
                .class_code = 0x02U,
                .subclass = NEXUS_DRIVER_WILDCARD_U8,
                .programming_interface = NEXUS_DRIVER_WILDCARD_U8
            },
            .probe = rtl8139_pnp_probe
        },
        {
            .name = "virtio-net", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = 0x1AF4U, .device_id = 0x1041U,
                .class_code = NEXUS_DRIVER_WILDCARD_U8,
                .subclass = NEXUS_DRIVER_WILDCARD_U8,
                .programming_interface = NEXUS_DRIVER_WILDCARD_U8
            },
            .probe = virtio_net_pnp_probe
        },
        {
            .name = "ac97", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = NEXUS_DRIVER_WILDCARD_U16,
                .device_id = NEXUS_DRIVER_WILDCARD_U16,
                .class_code = 0x04U, .subclass = 0x01U,
                .programming_interface = NEXUS_DRIVER_WILDCARD_U8
            },
            .probe = ac97_pnp_probe
        },
        {
            .name = "xhci", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = NEXUS_DRIVER_WILDCARD_U16,
                .device_id = NEXUS_DRIVER_WILDCARD_U16,
                .class_code = 0x0CU, .subclass = 0x03U,
                .programming_interface = 0x30U
            },
            .probe = xhci_pnp_probe
        },
        {
            .name = "virtio-gpu", .version = 1U, .priority = 100,
            .match = {
                .bus = NEXUS_DRIVER_BUS_PCI,
                .vendor_id = 0x1AF4U, .device_id = 0x1050U,
                .class_code = 0x03U,
                .subclass = NEXUS_DRIVER_WILDCARD_U8,
                .programming_interface = NEXUS_DRIVER_WILDCARD_U8
            },
            .probe = virtio_gpu_pnp_probe
        }
    };
    for (size_t i = 0U; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i)
        if (!nexus_driver_manager_register(&descriptors[i])) return false;
    return true;
}

bool storage_probe(void) {
    controller_count = 0U;
    nexus_driver_manager_init();
    if (!storage_register_drivers()) {
        serial_write("STORAGE: unable to register built-in PCI drivers\r\n");
        return false;
    }
    serial_write("STORAGE: classifying controllers and dispatching modular probes\r\n");

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

    }

    /*
     * Publish the entire retained PCI inventory, not just storage-class
     * functions. Unsupported and unbound hardware remains visible to PnP
     * diagnostics, and newly registered drivers can retry it later.
     */
    const size_t newly_discovered =
        nexus_driver_manager_discover_pci_devices();
    serial_write("PNP: PCI devices inventoried=");
    serial_write_hex(nexus_driver_manager_device_count());
    serial_write(" new=");
    serial_write_hex(newly_discovered);
    serial_write(" binding records=");
    serial_write_hex(nexus_driver_manager_binding_count());
    serial_write(" failures=");
    serial_write_hex(nexus_driver_manager_failure_count());
    serial_write("\r\n");

    serial_write("STORAGE: controllers discovered: ");
    serial_write_hex(controller_count);
    serial_write(" driver probe records=");
    serial_write_hex(nexus_driver_manager_binding_count());
    serial_write("\r\n");

    return controller_count != 0;
}

size_t storage_controller_count(void) {
    return controller_count;
}

bool storage_run_selftests(void) {
#ifdef NEXUS_STORAGE_SELFTEST
    return ahci_storage_selftest();
#else
    return true;
#endif
}
