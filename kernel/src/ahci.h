#ifndef NEXUS_AHCI_H
#define NEXUS_AHCI_H

#include <stdbool.h>

struct nexus_pci_device;

/*
 * Initializes AHCI ports with attached ATA disks and registers them as
 * read-only kernel block devices. Installer writes remain unsupported until
 * a separate fault-tested write authorization path exists.
 */
bool ahci_init_controller(const struct nexus_pci_device *device);

#endif
