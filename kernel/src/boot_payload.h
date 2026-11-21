#ifndef NEXUS_BOOT_PAYLOAD_H
#define NEXUS_BOOT_PAYLOAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct nexus_boot_blob { const uint8_t *data; uint32_t size; };
struct nexus_boot_payloads {
    struct nexus_boot_blob kernel_elf;
    struct nexus_boot_blob uefi_loader;
    struct nexus_boot_blob bios_stage2;
};
bool nexus_boot_payloads_init(void *module_response, void *executable_response);
const struct nexus_boot_payloads *nexus_boot_payloads_get(void);
#endif
