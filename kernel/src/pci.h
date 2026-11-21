#ifndef NEXUS_PCI_H
#define NEXUS_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_PCI_MAX_DEVICES 256U
#define NEXUS_PCI_BAR_COUNT 6U

struct nexus_pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bars[NEXUS_PCI_BAR_COUNT];
};

/* Performs read-only PCI configuration-space enumeration. */
bool pci_init(void);
size_t pci_device_count(void);
const struct nexus_pci_device *pci_device_at(size_t index);

#endif
