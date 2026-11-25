#include "firmware.h"

#include <stddef.h>
#include <stdint.h>

#define SMBIOS3_ENTRY_MAX_LENGTH 64U
#define SMBIOS2_ENTRY_MAX_LENGTH 64U
#define ACPI_RSDP_V2_MIN_LENGTH 36U
#define ACPI_RSDP_MAX_LENGTH 4096U

static struct nexus_firmware_info firmware_info;

static bool signature_equals(const uint8_t *actual,
                             const char *expected, size_t length) {
    if (actual == NULL || expected == NULL) return false;
    for (size_t i = 0U; i < length; ++i)
        if (actual[i] != (uint8_t)expected[i]) return false;
    return true;
}

static bool checksum_valid(const uint8_t *bytes, size_t length) {
    if (bytes == NULL || length == 0U) return false;
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0U;
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool parse_rsdp(const void *address, uint8_t *revision) {
    if (address == NULL || revision == NULL) return false;
    const uint8_t *bytes = (const uint8_t *)address;
    if (!signature_equals(bytes, "RSD PTR ", 8U) ||
        !checksum_valid(bytes, 20U)) return false;

    const uint8_t acpi_revision = bytes[15];
    if (acpi_revision >= 2U) {
        const uint32_t length = read_le32(bytes + 20U);
        if (length < ACPI_RSDP_V2_MIN_LENGTH ||
            length > ACPI_RSDP_MAX_LENGTH ||
            !checksum_valid(bytes, length)) return false;
    }
    *revision = acpi_revision;
    return true;
}

static bool parse_smbios3(const void *entry_point) {
    if (entry_point == NULL) return false;
    const uint8_t *bytes = (const uint8_t *)entry_point;
    if (!signature_equals(bytes, "_SM3_", 5U)) return false;
    const uint8_t length = bytes[6];
    if (length < 24U || length > SMBIOS3_ENTRY_MAX_LENGTH ||
        !checksum_valid(bytes, length)) return false;
    firmware_info.smbios_major = bytes[7];
    firmware_info.smbios_minor = bytes[8];
    firmware_info.smbios_64bit = true;
    firmware_info.smbios_present = true;
    return true;
}

static bool parse_smbios2(const void *entry_point) {
    if (entry_point == NULL) return false;
    const uint8_t *bytes = (const uint8_t *)entry_point;
    if (!signature_equals(bytes, "_SM_", 4U)) return false;
    const uint8_t length = bytes[5];
    if (length < 31U || length > SMBIOS2_ENTRY_MAX_LENGTH ||
        !checksum_valid(bytes, length) ||
        !signature_equals(bytes + 16U, "_DMI_", 5U) ||
        !checksum_valid(bytes + 16U, 15U)) return false;
    firmware_info.smbios_major = bytes[6];
    firmware_info.smbios_minor = bytes[7];
    firmware_info.smbios_64bit = false;
    firmware_info.smbios_present = true;
    return true;
}

bool nexus_firmware_init(uint64_t firmware_type,
                         const void *smbios_entry_32,
                         const void *smbios_entry_64,
                         const void *rsdp_address) {
    firmware_info = (struct nexus_firmware_info){0};
    firmware_info.firmware_type = firmware_type;
    firmware_info.acpi_present =
        parse_rsdp(rsdp_address, &firmware_info.acpi_revision);

    if (!parse_smbios3(smbios_entry_64)) {
        if (!parse_smbios2(smbios_entry_32))
            (void)parse_smbios3(smbios_entry_32);
    }
    return true;
}

const struct nexus_firmware_info *nexus_firmware_get_info(void) {
    return &firmware_info;
}

const char *nexus_firmware_type_label(void) {
    switch (firmware_info.firmware_type) {
        case 0U: return "LEGACY BIOS";
        case 1U: return "UEFI 32-BIT";
        case 2U: return "UEFI 64-BIT";
        case 3U: return "SBI FIRMWARE";
        default: return "FIRMWARE UNKNOWN";
    }
}
