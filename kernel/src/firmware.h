#ifndef NEXUS_FIRMWARE_H
#define NEXUS_FIRMWARE_H

#include <stdbool.h>
#include <stdint.h>

struct nexus_firmware_info {
    uint64_t firmware_type;
    bool acpi_present;
    uint8_t acpi_revision;
    bool smbios_present;
    bool smbios_64bit;
    uint8_t smbios_major;
    uint8_t smbios_minor;
};

bool nexus_firmware_init(uint64_t firmware_type,
                         const void *smbios_entry_32,
                         const void *smbios_entry_64,
                         const void *rsdp_address);
const struct nexus_firmware_info *nexus_firmware_get_info(void);
const char *nexus_firmware_type_label(void);

#endif
