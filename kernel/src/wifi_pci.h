#ifndef NEXUS_WIFI_PCI_H
#define NEXUS_WIFI_PCI_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_WIFI_PCI_MAX_CANDIDATES 16U

struct nexus_wifi_pci_candidate {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t revision_id;
    const char *vendor_name;
};

/* Inventory only: matching class codes does not bind or operate a radio. */
bool wifi_pci_discover(void);
uint32_t wifi_pci_candidate_count(void);
const struct nexus_wifi_pci_candidate *wifi_pci_candidate_at(uint32_t index);

#endif
