#ifndef NEXUS_AHCI_H
#define NEXUS_AHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/*
 * Initializes AHCI ports with attached ATA disks. Devices stay read-only for
 * ordinary I/O; the shared block-device policy controls installer/data writes.
 */
bool ahci_init_controller(const struct nexus_pci_device *device);

/* Run the disposable-device write/restore self-test after boot provenance. */
bool ahci_storage_selftest(void);

#endif
