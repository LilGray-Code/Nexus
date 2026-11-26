#include "wifi_pci.h"
#include "pci.h"
#include "win_driver_inf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct nexus_pci_device devices[4];
static uint32_t count;

uint32_t pci_stored_device_count(void) { return count; }
const struct nexus_pci_device *pci_device_at(uint32_t index) {
    return index < count ? &devices[index] : NULL;
}

int main(void) {
    devices[0] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x00U,
        .vendor_id = 0x8086U, .device_id = 0x100EU
    };
    devices[1] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x80U,
        .bus = 2U, .slot = 3U,
        .vendor_id = 0x8086U, .device_id = 0x2723U,
        .revision_id = 0x01U,
        .subsystem_vendor_id = 0x8086U,
        .subsystem_device_id = 0x0024U
    };
    devices[2] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x80U,
        .bus = 4U, .slot = 1U,
        .vendor_id = 0x10ECU, .device_id = 0xC821U
    };
    devices[3] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .vendor_id = 0x1234U, .device_id = 0x1111U
    };
    count = 4U;
    assert(wifi_pci_discover());
    assert(wifi_pci_candidate_count() == 2U);
    const struct nexus_wifi_pci_candidate *first = wifi_pci_candidate_at(0U);
    const struct nexus_wifi_pci_candidate *second = wifi_pci_candidate_at(1U);
    assert(first != NULL && first->vendor_id == 0x8086U);
    assert(first->bus == 2U && first->slot == 3U);
    assert(first->revision_id == 0x01U);
    assert(first->subsystem_vendor_id == 0x8086U);
    assert(first->subsystem_device_id == 0x0024U);
    assert(second != NULL && second->vendor_id == 0x10ECU);
    static const char intel_inf[] =
        "[Models.NTamd64]\r\n"
        "Intel Wi-Fi=Install, PCI\\VEN_8086&DEV_2723&SUBSYS_00248086&REV_01\r\n";
    struct nexus_win_inf_match match;
    assert(wifi_pci_match_windows_inf(0U, intel_inf,
                                      sizeof(intel_inf) - 1U, &match));
    assert(strcmp(match.install_section, "Install") == 0);
    assert(match.specificity == 3U);
    assert(!wifi_pci_match_windows_inf(1U, intel_inf,
                                       sizeof(intel_inf) - 1U, &match));

    assert(second->vendor_name[0] == 'R');
    assert(wifi_pci_candidate_at(2U) == NULL);
    count = 0U;
    assert(!wifi_pci_discover());
    assert(wifi_pci_candidate_count() == 0U);
    puts("NEXUS PCI WIRELESS INVENTORY TESTS PASSED");
    return 0;
}
