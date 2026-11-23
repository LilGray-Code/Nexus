#include "wifi_chipset.h"

#include <stddef.h>
#include <stdint.h>

struct wifi_chipset_id {
    uint16_t vendor;
    uint16_t device;
    const char *name;
};

static const struct wifi_chipset_id pci_chipsets[] = {
    {0x8086U, 0x2723U, "Intel Wi-Fi 6 AX200"},
    {0x8086U, 0x2725U, "Intel Wi-Fi 6E AX210"},
    {0x8086U, 0x272BU, "Intel Wi-Fi 7 BE200"},
    {0x14C3U, 0x7602U, "MediaTek MT7602E"},
    {0x14C3U, 0x7603U, "MediaTek MT7603E"},
    {0x14C3U, 0x7610U, "MediaTek MT7610E"},
    {0x14C3U, 0x7611U, "MediaTek MT7611E"},
    {0x14C3U, 0x7612U, "MediaTek MT7612E"},
    {0x14C3U, 0x7662U, "MediaTek MT7662E"},
    {0x14C3U, 0x0608U, "MediaTek MT7921K / RZ608"},
    {0x14C3U, 0x0616U, "MediaTek MT7922"},
    {0x14C3U, 0x7922U, "MediaTek MT7922"},
    {0x14C3U, 0x7961U, "MediaTek MT7921"},
    {0x14C3U, 0x7920U, "MediaTek MT7920"},
    {0x14C3U, 0x7902U, "MediaTek MT7902"},
    {0x14C3U, 0x7925U, "MediaTek MT7925"},
    {0x10ECU, 0xB821U, "Realtek RTL8821CE"},
    {0x10ECU, 0xB822U, "Realtek RTL8822BE"},
    {0x10ECU, 0xC821U, "Realtek RTL8821CE"},
    {0x10ECU, 0xC822U, "Realtek RTL8822CE"},
    {0x10ECU, 0xC82FU, "Realtek RTL8822CE"},
    {0x10ECU, 0xB851U, "Realtek RTL8851BE"},
    {0x10ECU, 0xB852U, "Realtek RTL8852BE"},
    {0x10ECU, 0xB85BU, "Realtek RTL8852BE"},
    {0x10ECU, 0xC852U, "Realtek RTL8852CE"},
    {0x10ECU, 0xD723U, "Realtek RTL8723DE"},
    {0x168CU, 0x003CU, "Qualcomm Atheros QCA988x"},
    {0x168CU, 0x003EU, "Qualcomm Atheros QCA6174"},
    {0x168CU, 0x0040U, "Qualcomm Atheros QCA9980 / QCA9990"},
    {0x168CU, 0x0041U, "Qualcomm Atheros QCA6164"},
    {0x168CU, 0x0042U, "Qualcomm Atheros QCA9377"}
};

static const struct wifi_chipset_id usb_chipsets[] = {
    {0x0E8DU, 0x7612U, "MediaTek MT7612U"},
    {0x0E8DU, 0x7663U, "MediaTek MT7663U"},
    {0x0E8DU, 0x7961U, "MediaTek MT7921U"},
    {0x0E8DU, 0x7925U, "MediaTek MT7925U"},
    {0x148FU, 0x7610U, "MediaTek MT7610U"}
};

static const char *lookup(const struct wifi_chipset_id *ids, size_t count,
                          uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0U; i < count; ++i) {
        if (ids[i].vendor == vendor_id && ids[i].device == device_id)
            return ids[i].name;
    }
    return NULL;
}

const char *wifi_pci_chipset_name(uint16_t vendor_id, uint16_t device_id) {
    return lookup(pci_chipsets,
                  sizeof(pci_chipsets) / sizeof(pci_chipsets[0]),
                  vendor_id, device_id);
}

const char *wifi_usb_chipset_name(uint16_t vendor_id, uint16_t product_id) {
    return lookup(usb_chipsets,
                  sizeof(usb_chipsets) / sizeof(usb_chipsets[0]),
                  vendor_id, product_id);
}
