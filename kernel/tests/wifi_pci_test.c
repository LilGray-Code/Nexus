#include "wifi_pci.h"
#include "pci.h"

#include <assert.h>
#include <stdio.h>

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
        .vendor_id = 0x8086U, .device_id = 0x2723U
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
    assert(second != NULL && second->vendor_id == 0x10ECU);
    assert(second->vendor_name[0] == 'R');
    assert(wifi_pci_candidate_at(2U) == NULL);
    count = 0U;
    assert(!wifi_pci_discover());
    assert(wifi_pci_candidate_count() == 0U);
    puts("NEXUS PCI WIRELESS INVENTORY TESTS PASSED");
    return 0;
}
