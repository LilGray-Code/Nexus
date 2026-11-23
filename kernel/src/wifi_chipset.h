#ifndef NEXUS_WIFI_CHIPSET_H
#define NEXUS_WIFI_CHIPSET_H

#include <stdint.h>

/*
 * Return a chipset family name only for exact, reviewed hardware IDs.
 * These lookups identify hardware; they do not mean a Nexus radio driver
 * or firmware is installed.
 */
const char *wifi_pci_chipset_name(uint16_t vendor_id, uint16_t device_id);
const char *wifi_usb_chipset_name(uint16_t vendor_id, uint16_t product_id);

#endif
