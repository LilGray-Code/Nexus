#include "pci.h"

#include <stddef.h>

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


bool pci_is_network_controller(const struct nexus_pci_device *device) {
    return device != NULL &&
           (device->class_code == 0x02U || device->class_code == 0x0DU);
}

bool pci_is_wireless_controller(const struct nexus_pci_device *device) {
    return device != NULL && device->class_code == 0x0DU;
}

bool pci_is_wireless_lan_candidate(const struct nexus_pci_device *device) {
    if (device == NULL) return false;

    /* PCI wireless-controller subclasses 802.11a/b identify WLAN directly.
     * Network-controller subclass 0x80 is vendor-specific "other network";
     * many WLAN cards use it, but it is only a candidate, not proof.
     */
    if (device->class_code == 0x0DU)
        return device->subclass == 0x20U || device->subclass == 0x21U ||
               device->subclass == 0x80U;
    return device->class_code == 0x02U && device->subclass == 0x80U;
}
