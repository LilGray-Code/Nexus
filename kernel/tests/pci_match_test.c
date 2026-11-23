#include "pci.h"

#include <assert.h>
#include <stddef.h>

int main(void) {
    const struct nexus_pci_device e1000 = {
        .vendor_id = 0x8086U,
        .device_id = 0x100EU,
        .class_code = 0x02U,
        .subclass = 0x00U,
        .programming_interface = 0x00U
    };
    const struct nexus_pci_device wifi = {
        .vendor_id = 0x8086U,
        .device_id = 0x2723U,
        .class_code = 0x02U,
        .subclass = 0x80U,
        .programming_interface = 0x00U
    };

    assert(pci_device_matches(&e1000, 0x8086U, 0x100EU,
                              0x02U, 0x00U, 0x00U));
    assert(!pci_device_matches(&e1000, 0x8086U, 0x2723U,
                               0x02U, 0x00U, 0x00U));
    assert(pci_device_matches(&wifi, 0x8086U, 0x2723U,
                              0x02U, 0x80U, 0x00U));
    assert(pci_device_matches(&wifi, 0x8086U,
                              NEXUS_PCI_MATCH_ANY_ID,
                              0x02U, NEXUS_PCI_MATCH_ANY_CLASS,
                              NEXUS_PCI_MATCH_ANY_CLASS));
    assert(pci_device_matches(&wifi, NEXUS_PCI_MATCH_ANY_ID,
                              NEXUS_PCI_MATCH_ANY_ID,
                              NEXUS_PCI_MATCH_ANY_CLASS,
                              NEXUS_PCI_MATCH_ANY_CLASS,
                              NEXUS_PCI_MATCH_ANY_CLASS));
    assert(!pci_device_matches(NULL, NEXUS_PCI_MATCH_ANY_ID,
                               NEXUS_PCI_MATCH_ANY_ID,
                               NEXUS_PCI_MATCH_ANY_CLASS,
                               NEXUS_PCI_MATCH_ANY_CLASS,
                               NEXUS_PCI_MATCH_ANY_CLASS));
    const struct nexus_pci_device pci_wifi = {
        .vendor_id = 0x8086U, .device_id = 0x2723U,
        .class_code = 0x02U, .subclass = 0x80U
    };
    const struct nexus_pci_device pci_sig_wlan = {
        .vendor_id = 0x1234U, .device_id = 0x5678U,
        .class_code = 0x0DU, .subclass = 0x20U
    };
    const struct nexus_pci_device bluetooth = {
        .vendor_id = 0x8086U, .device_id = 0xA0F0U,
        .class_code = 0x0DU, .subclass = 0x11U
    };

    assert(pci_is_network_controller(&e1000));
    assert(!pci_is_wireless_controller(&e1000));
    assert(pci_is_network_controller(&pci_wifi));
    assert(pci_is_wireless_lan_candidate(&pci_wifi));
    assert(!pci_is_wireless_controller(&pci_wifi));
    assert(pci_is_network_controller(&pci_sig_wlan));
    assert(pci_is_wireless_controller(&pci_sig_wlan));
    assert(pci_is_wireless_lan_candidate(&pci_sig_wlan));
    assert(pci_is_wireless_controller(&bluetooth));
    assert(!pci_is_wireless_lan_candidate(&bluetooth));
    assert(!pci_is_network_controller(NULL));
    assert(!pci_is_wireless_controller(NULL));
    assert(!pci_is_wireless_lan_candidate(NULL));

    return 0;
}
