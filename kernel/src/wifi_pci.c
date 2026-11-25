#include "wifi_pci.h"

#include "pci.h"

static struct nexus_wifi_pci_candidate
    candidates[NEXUS_WIFI_PCI_MAX_CANDIDATES];
static uint32_t candidate_count;

static const char *wireless_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086U: return "INTEL";
        case 0x10ECU: return "REALTEK";
        case 0x168CU: return "QUALCOMM/ATHEROS";
        case 0x14C3U: return "MEDIATEK";
        case 0x14E4U: return "BROADCOM";
        case 0x17CBU: return "QUALCOMM";
        case 0x1814U: return "RALINK";
        default: return "OTHER";
    }
}

bool wifi_pci_discover(void) {
    candidate_count = 0U;
    for (uint32_t i = 0U; i < pci_stored_device_count(); ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL || device->class_code != 0x02U ||
            device->subclass != 0x80U) continue;
        if (candidate_count >= NEXUS_WIFI_PCI_MAX_CANDIDATES) break;
        candidates[candidate_count++] = (struct nexus_wifi_pci_candidate){
            .bus = device->bus,
            .slot = device->slot,
            .function = device->function,
            .vendor_id = device->vendor_id,
            .device_id = device->device_id,
            .vendor_name = wireless_vendor_name(device->vendor_id)
        };
    }
    return candidate_count != 0U;
}

uint32_t wifi_pci_candidate_count(void) {
    return candidate_count;
}

const struct nexus_wifi_pci_candidate *
wifi_pci_candidate_at(uint32_t index) {
    return index < candidate_count ? &candidates[index] : NULL;
}
