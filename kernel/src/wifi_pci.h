#ifndef NEXUS_WIFI_PCI_H
#define NEXUS_WIFI_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WIFI_PCI_MAX_CANDIDATES 16U

struct nexus_win_inf_match;
struct nexus_win_driver_package_assessment;

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

/* Match an adapter against a Windows INF package without loading its .sys. */
bool wifi_pci_match_windows_inf(uint32_t candidate_index,
                                const char *inf_text, size_t text_length,
                                struct nexus_win_inf_match *match);

/* Assess an INF plus its .sys against a discovered adapter. */
bool wifi_pci_assess_windows_package(
    uint32_t candidate_index, const char *inf_data, size_t inf_size,
    const void *sys_data, size_t sys_size,
    struct nexus_win_driver_package_assessment *assessment);

#endif
