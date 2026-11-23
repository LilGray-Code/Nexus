#include "pci.h"

#include <stddef.h>
#include <stdint.h>

bool pci_decode_bar(const struct nexus_pci_device *device, uint8_t index,
                    struct nexus_pci_bar *bar) {
    if (device == NULL || bar == NULL || index >= NEXUS_PCI_BAR_COUNT)
        return false;

    *bar = (struct nexus_pci_bar){0};

    /* A 64-bit BAR consumes the following BAR slot as its upper dword. */
    for (uint8_t i = 0; i < index; ++i) {
        const uint32_t previous = device->bars[i];
        if (previous != 0 && (previous & 1U) == 0 &&
            ((previous >> 1) & 3U) == 2U) {
            if ((uint8_t)(i + 1U) == index) return false;
            ++i;
        }
    }

    const uint32_t raw = device->bars[index];
    if (raw == 0 || raw == UINT32_MAX) return false;

    bar->present = true;
    if ((raw & 1U) != 0) {
        bar->io_space = true;
        bar->base = (uint64_t)(raw & ~3U);
        bar->assigned = bar->base != 0;
        return true;
    }

    const uint32_t memory_type = (raw >> 1) & 3U;
    if (memory_type == 3U) {
        *bar = (struct nexus_pci_bar){0};
        return false;
    }

    bar->prefetchable = (raw & 8U) != 0;
    if (memory_type == 2U) {
        if (index + 1U >= NEXUS_PCI_BAR_COUNT) {
            *bar = (struct nexus_pci_bar){0};
            return false;
        }
        bar->is_64bit = true;
        bar->base = ((uint64_t)device->bars[index + 1U] << 32) |
                    (uint64_t)(raw & ~0xFU);
    } else if (memory_type == 1U) {
        bar->base = (uint64_t)(raw & 0x000FFFF0U);
    } else {
        bar->base = (uint64_t)(raw & ~0xFU);
    }

    bar->assigned = bar->base != 0;
    return true;
}
