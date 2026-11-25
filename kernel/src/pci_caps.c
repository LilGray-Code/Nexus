#include "pci_caps.h"
#include <stddef.h>
#include <stdint.h>
#define PCI_STATUS_CAPABILITIES_LIST (1U << 20)
#define PCI_CAP_MIN 0x40U
#define PCI_CAP_MAX 0xFCU
#define PCI_EXT_MIN 0x100U
#define PCI_EXT_MAX 0xFFCU
#define PCI_STANDARD_LIMIT 48U

static bool bit_test_and_set(uint8_t *bits, size_t bytes, uint16_t index) {
    const size_t byte_index = (size_t)index >> 3;
    if (byte_index >= bytes) return true;
    const uint8_t mask = (uint8_t)(1U << (index & 7U));
    const bool was_set = (bits[byte_index] & mask) != 0U;
    bits[byte_index] |= mask;
    return was_set;
}
static bool standard_pointer_valid(uint8_t p) {
    return p >= PCI_CAP_MIN && p <= PCI_CAP_MAX && (p & 3U) == 0U;
}
static bool extended_pointer_valid(uint16_t p) {
    return p >= PCI_EXT_MIN && p <= PCI_EXT_MAX && (p & 3U) == 0U;
}
static void record_standard(struct nexus_pci_capability_summary *s, uint8_t id) {
    switch (id) {
        case 0x01U: s->standard_flags |= NEXUS_PCI_CAP_POWER_MANAGEMENT; break;
        case 0x05U: s->standard_flags |= NEXUS_PCI_CAP_MSI; break;
        case 0x07U: s->standard_flags |= NEXUS_PCI_CAP_PCIX; break;
        case 0x10U: s->standard_flags |= NEXUS_PCI_CAP_PCIE; break;
        case 0x11U: s->standard_flags |= NEXUS_PCI_CAP_MSIX; break;
        default: break;
    }
}
static void record_extended(struct nexus_pci_capability_summary *s, uint16_t id) {
    switch (id) {
        case 0x0001U: s->extended_flags |= NEXUS_PCIE_EXT_AER; break;
        case 0x000DU: s->extended_flags |= NEXUS_PCIE_EXT_ACS; break;
        case 0x0010U: s->extended_flags |= NEXUS_PCIE_EXT_SRIOV; break;
        case 0x0015U: s->extended_flags |= NEXUS_PCIE_EXT_TPH; break;
        case 0x001BU: s->extended_flags |= NEXUS_PCIE_EXT_PASID; break;
        case 0x001EU: s->extended_flags |= NEXUS_PCIE_EXT_L1_PM; break;
        default: break;
    }
}
static bool scan_standard(nexus_pci_config_reader read_config, void *context,
                          uint8_t header_type,
                          struct nexus_pci_capability_summary *s) {
    uint32_t status = 0U;
    if (!read_config(context, 0x04U, &status)) return false;
    if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0U) return true;
    const uint16_t pointer_register =
        ((header_type & 0x7FU) == 2U) ? 0x14U : 0x34U;
    uint32_t raw_pointer = 0U;
    if (!read_config(context, pointer_register, &raw_pointer)) return false;
    uint8_t pointer = (uint8_t)raw_pointer;
    uint8_t visited[16] = {0};
    for (uint16_t n = 0U; pointer != 0U && n < PCI_STANDARD_LIMIT; ++n) {
        if (!standard_pointer_valid(pointer) ||
            bit_test_and_set(visited, sizeof(visited),
                             (uint16_t)(pointer >> 2))) return false;
        uint32_t header = 0U;
        if (!read_config(context, pointer, &header)) return false;
        const uint8_t id = (uint8_t)header;
        const uint8_t next = (uint8_t)(header >> 8);
        if (id == 0U || id == 0xFFU) return false;
        ++s->standard_count;
        record_standard(s, id);
        pointer = next;
    }
    return pointer == 0U;
}
static bool scan_extended(nexus_pci_config_reader read_config, void *context,
                          struct nexus_pci_capability_summary *s) {
    uint8_t visited[128] = {0};
    uint16_t pointer = PCI_EXT_MIN;
    for (uint16_t n = 0U; n < NEXUS_PCI_CAPABILITY_SCAN_MAX; ++n) {
        if (!extended_pointer_valid(pointer) ||
            bit_test_and_set(visited, sizeof(visited),
                             (uint16_t)(pointer >> 2))) return false;
        uint32_t header = 0U;
        if (!read_config(context, pointer, &header)) return false;
        const uint16_t id = (uint16_t)header;
        if (id == 0U || id == 0xFFFFU) return true;
        const uint8_t version = (uint8_t)((header >> 16) & 0x0FU);
        const uint16_t next = (uint16_t)((header >> 20) & 0x0FFFU);
        if (version == 0U) return false;
        ++s->extended_count;
        record_extended(s, id);
        if (next == 0U) return true;
        if (!extended_pointer_valid(next)) return false;
        pointer = next;
    }
    return false;
}
bool nexus_pci_scan_capabilities(
    nexus_pci_config_reader read_config, void *context,
    uint8_t header_type, bool extended_config_available,
    struct nexus_pci_capability_summary *summary) {
    if (summary == NULL) return false;
    *summary = (struct nexus_pci_capability_summary){
        .standard_valid = false,
        .extended_supported = extended_config_available,
        .extended_valid = false
    };
    if (read_config == NULL) return false;
    summary->standard_valid =
        scan_standard(read_config, context, header_type, summary);
    if (extended_config_available)
        summary->extended_valid = scan_extended(read_config, context, summary);
    return summary->standard_valid &&
           (!extended_config_available || summary->extended_valid);
}
