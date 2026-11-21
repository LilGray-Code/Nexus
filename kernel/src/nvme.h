#ifndef NEXUS_NVME_H
#define NEXUS_NVME_H

#include <stdbool.h>

struct nexus_pci_device;

/* Initializes one NVMe admin queue and issues Identify Controller (read-only). */
bool nvme_identify_probe(const struct nexus_pci_device *device);

#endif
