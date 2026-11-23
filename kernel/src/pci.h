#ifndef NEXUS_PCI_H
#define NEXUS_PCI_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_PCI_MAX_STORED_DEVICES 128U
#define NEXUS_PCI_BAR_COUNT 6U

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
    uint16_t command;
    uint16_t status;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint32_t bars[NEXUS_PCI_BAR_COUNT];
};

struct nexus_pci_bar {
    bool present;
    bool assigned;
    bool io_space;
    bool is_64bit;
    bool prefetchable;
    uint64_t base;
};

bool pci_init(void);
uint32_t pci_device_count(void);
/* Number of entries retained in the bounded discovery table. */
uint32_t pci_stored_device_count(void);
const struct nexus_pci_device *pci_device_at(uint32_t index);
/* Find the stored device at or after start_index with matching class codes. */
const struct nexus_pci_device *pci_find_class(uint8_t class_code,
                                               uint8_t subclass,
                                               uint32_t start_index,
                                               uint32_t *found_index);

/* Decode the raw BAR value captured during enumeration; never writes config. */
bool pci_decode_bar(const struct nexus_pci_device *device, uint8_t index,
                    struct nexus_pci_bar *bar);

/* Enable memory decoding and bus mastering using a 16-bit command write. */
bool pci_enable_memory_bus_master(const struct nexus_pci_device *device);

#endif
