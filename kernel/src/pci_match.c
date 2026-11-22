#include "pci.h"

bool pci_device_matches(const struct nexus_pci_device *device,
                        uint16_t vendor_id, uint16_t device_id,
                        uint8_t class_code, uint8_t subclass,
                        uint8_t programming_interface) {
    if (device == NULL) return false;
    return (vendor_id == NEXUS_PCI_MATCH_ANY_ID ||
            device->vendor_id == vendor_id) &&
           (device_id == NEXUS_PCI_MATCH_ANY_ID ||
            device->device_id == device_id) &&
           (class_code == NEXUS_PCI_MATCH_ANY_CLASS ||
            device->class_code == class_code) &&
           (subclass == NEXUS_PCI_MATCH_ANY_CLASS ||
            device->subclass == subclass) &&
           (programming_interface == NEXUS_PCI_MATCH_ANY_CLASS ||
            device->programming_interface == programming_interface);
}

const struct nexus_pci_device *pci_find_matching_device(
    uint16_t vendor_id, uint16_t device_id, uint8_t class_code,
    uint8_t subclass, uint8_t programming_interface) {
    for (uint32_t i = 0U; i < stored_device_count; ++i) {
        if (pci_device_matches(&stored_devices[i], vendor_id, device_id,
                               class_code, subclass,
                               programming_interface)) {
            return &stored_devices[i];
        }
    }
    return NULL;
}

