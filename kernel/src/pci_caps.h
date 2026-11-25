#ifndef NEXUS_PCI_CAPS_H
#define NEXUS_PCI_CAPS_H
#include <stdbool.h>
#include <stdint.h>
#define NEXUS_PCI_CAP_POWER_MANAGEMENT (1U << 0)
#define NEXUS_PCI_CAP_MSI              (1U << 1)
#define NEXUS_PCI_CAP_PCIE             (1U << 2)
#define NEXUS_PCI_CAP_MSIX             (1U << 3)
#define NEXUS_PCI_CAP_PCIX             (1U << 4)
#define NEXUS_PCIE_EXT_AER             (1U << 0)
#define NEXUS_PCIE_EXT_ACS             (1U << 1)
#define NEXUS_PCIE_EXT_SRIOV           (1U << 2)
#define NEXUS_PCIE_EXT_TPH             (1U << 3)
#define NEXUS_PCIE_EXT_PASID           (1U << 4)
#define NEXUS_PCIE_EXT_L1_PM           (1U << 5)
#define NEXUS_PCI_CAPABILITY_SCAN_MAX 128U
typedef bool (*nexus_pci_config_reader)(void *context, uint16_t offset,
                                        uint32_t *value);
struct nexus_pci_capability_summary {
    uint32_t standard_flags, extended_flags;
    uint16_t standard_count, extended_count;
    bool standard_valid, extended_supported, extended_valid;
};
/* Read-only bounded PCI capability walker; malformed lists are marked invalid. */
bool nexus_pci_scan_capabilities(
    nexus_pci_config_reader read_config, void *context,
    uint8_t header_type, bool extended_config_available,
    struct nexus_pci_capability_summary *summary);
#endif
