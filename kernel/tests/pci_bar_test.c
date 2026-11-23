#include "pci.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    struct nexus_pci_device device = {0};
    struct nexus_pci_bar bar;

    device.bars[0] = 0x0000C001U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.present && bar.assigned && bar.io_space);
    assert(!bar.is_64bit && !bar.prefetchable);
    assert(bar.base == 0x0000C000ULL);

    device.bars[0] = 0xFEBF0008U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.present && bar.assigned && !bar.io_space);
    assert(bar.prefetchable && !bar.is_64bit);
    assert(bar.base == 0xFEBF0000ULL);

    device.bars[0] = 0x0000F002U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.base == 0x0000F000ULL);

    device.bars[0] = 0x3456700CU;
    device.bars[1] = 0x00000012U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.present && bar.assigned && !bar.io_space);
    assert(bar.is_64bit && bar.prefetchable);
    assert(bar.base == 0x0000001234567000ULL);
    assert(!pci_decode_bar(&device, 1U, &bar));

    device.bars[0] = 0x00000008U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.present && !bar.assigned && bar.prefetchable);
    assert(bar.base == 0U);

    device.bars[0] = 0x00000001U;
    assert(pci_decode_bar(&device, 0U, &bar));
    assert(bar.present && !bar.assigned && bar.io_space);

    device.bars[0] = 0x00000006U;
    assert(!pci_decode_bar(&device, 0U, &bar));
    assert(!bar.present);

    device.bars[0] = 0U;
    assert(!pci_decode_bar(&device, 0U, &bar));
    device.bars[0] = UINT32_MAX;
    assert(!pci_decode_bar(&device, 0U, &bar));
    assert(!pci_decode_bar(&device, NEXUS_PCI_BAR_COUNT, &bar));
    assert(!pci_decode_bar(NULL, 0U, &bar));
    assert(!pci_decode_bar(&device, 0U, NULL));

    device.bars[5] = 0x00000004U;
    assert(!pci_decode_bar(&device, 5U, &bar));

    device.bars[0] = 0U;
    device.bars[1] = 0x00001004U;
    device.bars[2] = 0x00000001U;
    assert(!pci_decode_bar(&device, 2U, &bar));

    return 0;
}
