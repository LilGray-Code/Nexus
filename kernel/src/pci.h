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

struct nexus_pci_msi_info {
    bool present;
    bool enabled;
    bool supports_64bit_address;
    bool per_vector_masking;
    uint8_t maximum_message_exponent;
    uint16_t maximum_message_count;
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
/* Number of entries addressable through pci_device_at(). */
uint32_t pci_device_count(void);
/* Total discovered PCI functions, including entries beyond storage capacity. */
uint32_t pci_discovered_device_count(void);
uint32_t pci_wireless_device_count(void);
uint32_t pci_wireless_lan_candidate_count(void);
const struct nexus_pci_device *pci_device_at(uint32_t index);

/* Hardware classification helpers. These identify adapter candidates; they do
 * not claim that a driver exists or that the radio can connect.
 */
bool pci_is_network_controller(const struct nexus_pci_device *device);
bool pci_is_wireless_controller(const struct nexus_pci_device *device);
bool pci_is_wireless_lan_candidate(const struct nexus_pci_device *device);

/* Wildcards for the driver-match helper. */
#define NEXUS_PCI_MATCH_ANY_ID 0xFFFFU
#define NEXUS_PCI_MATCH_ANY_CLASS 0xFFU

/* Match any supplied subset of PCI identity and class fields. */
bool pci_device_matches(const struct nexus_pci_device *device,
                        uint16_t vendor_id, uint16_t device_id,
                        uint8_t class_code, uint8_t subclass,
                        uint8_t programming_interface);
/* Return the first enumerated device matching the supplied fields. */
const struct nexus_pci_device *pci_find_matching_device(
    uint16_t vendor_id, uint16_t device_id, uint8_t class_code,
    uint8_t subclass, uint8_t programming_interface);

/* Find a conventional PCI capability (for example MSI or PCI Express).
 * Returns its config-space offset; traversal is bounded and read-only.
 */
bool pci_find_capability(const struct nexus_pci_device *device,
                         uint8_t capability_id, uint8_t *offset);
/* Read MSI capability properties without enabling or modifying interrupts. */
bool pci_get_msi_info(const struct nexus_pci_device *device,
                      struct nexus_pci_msi_info *info);

/* Decode the raw BAR value captured during enumeration; never writes config. */
bool pci_decode_bar(const struct nexus_pci_device *device, uint8_t index,
                    struct nexus_pci_bar *bar);

/* Enable memory decoding and bus mastering using a 16-bit command write. */
bool pci_enable_memory_bus_master(const struct nexus_pci_device *device);

#endif
