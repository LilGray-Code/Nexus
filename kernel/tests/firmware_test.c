#include "firmware.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void finish_checksum(uint8_t *bytes, size_t length,
                            size_t checksum_offset) {
    bytes[checksum_offset] = 0U;
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    bytes[checksum_offset] = (uint8_t)(0U - sum);
}

static void test_uefi_smbios3_inventory(void) {
    uint8_t rsdp[16] = {'R','S','D',' ','P','T','R',' '};
    rsdp[15] = 2U;
    uint8_t entry[24] = {0U};
    memcpy(entry, "_SM3_", 5U);
    entry[6] = sizeof(entry);
    entry[7] = 3U;
    entry[8] = 6U;
    finish_checksum(entry, sizeof(entry), 5U);

    assert(nexus_firmware_init(2U, NULL, entry, rsdp));
    const struct nexus_firmware_info *info = nexus_firmware_get_info();
    assert(info->acpi_present && info->acpi_revision == 2U);
    assert(info->smbios_present && info->smbios_64bit);
    assert(info->smbios_major == 3U && info->smbios_minor == 6U);
    assert(strcmp(nexus_firmware_type_label(), "UEFI 64-BIT") == 0);
}

static void test_legacy_smbios2_and_invalid_checksum(void) {
    uint8_t entry[31] = {0U};
    memcpy(entry, "_SM_", 4U);
    entry[5] = sizeof(entry);
    entry[6] = 2U;
    entry[7] = 8U;
    memcpy(entry + 16U, "_DMI_", 5U);
    finish_checksum(entry + 16U, 15U, 5U);
    finish_checksum(entry, sizeof(entry), 4U);

    assert(nexus_firmware_init(0U, entry, NULL, NULL));
    const struct nexus_firmware_info *info = nexus_firmware_get_info();
    assert(info->smbios_present && !info->smbios_64bit);
    assert(info->smbios_major == 2U && info->smbios_minor == 8U);
    assert(strcmp(nexus_firmware_type_label(), "LEGACY BIOS") == 0);

    entry[4] ^= 0x01U;
    assert(nexus_firmware_init(0U, entry, NULL, NULL));
    assert(!nexus_firmware_get_info()->smbios_present);
}

int main(void) {
    test_uefi_smbios3_inventory();
    test_legacy_smbios2_and_invalid_checksum();
    puts("NEXUS FIRMWARE INVENTORY TESTS PASSED");
    return 0;
}
