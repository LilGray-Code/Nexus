#include "wifi_chipset.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const char *name = wifi_pci_chipset_name(0x8086U, 0x2723U);
    assert(name != NULL && strcmp(name, "Intel Wi-Fi 6 AX200") == 0);
    name = wifi_pci_chipset_name(0x14C3U, 0x0616U);
    assert(name != NULL && strcmp(name, "MediaTek MT7922") == 0);
    name = wifi_pci_chipset_name(0x10ECU, 0xC821U);
    assert(name != NULL && strcmp(name, "Realtek RTL8821CE") == 0);
    name = wifi_pci_chipset_name(0x168CU, 0x003EU);
    assert(name != NULL && strcmp(name, "Qualcomm Atheros QCA6174") == 0);
    assert(wifi_pci_chipset_name(0xFFFFU, 0xFFFFU) == NULL);

    name = wifi_usb_chipset_name(0x0E8DU, 0x7961U);
    assert(name != NULL && strcmp(name, "MediaTek MT7921U") == 0);
    name = wifi_usb_chipset_name(0x148FU, 0x7610U);
    assert(name != NULL && strcmp(name, "MediaTek MT7610U") == 0);
    assert(wifi_usb_chipset_name(0xFFFFU, 0xFFFFU) == NULL);
    return 0;
}
