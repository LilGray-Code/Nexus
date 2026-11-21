#ifndef NEXUS_PCI_H
#define NEXUS_PCI_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_PCI_MAX_STORED_DEVICES 128U

struct nexus_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t header_type;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint16_t vendor_id;
    uint16_t device_id;
};

bool pci_init(void);
uint32_t pci_device_count(void);
const struct nexus_pci_device *pci_device_at(uint32_t index);

#endif
