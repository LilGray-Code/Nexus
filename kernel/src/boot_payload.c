#include "boot_payload.h"
#include <limine.h>

#define NEXUS_PAYLOAD_LIMIT (1024U * 1024U)
static struct nexus_boot_payloads payloads;
static bool payloads_ready;

static bool prefix_is(const char *value, const char *prefix) {
    if (value == NULL || prefix == NULL) return false;
    while (*prefix != '\\0') {
        char a = *value;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        ++value;
        ++prefix;
    }
    return true;
}

static bool path_is(const char *actual, const char *expected) {
    if (actual == NULL || expected == NULL) return false;
    if (prefix_is(actual, "boot():")) actual += 7U;
    if (prefix_is(expected, "boot():")) expected += 7U;
    while (*actual && *expected) {
        char a = *actual++, e = *expected++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (e >= 'A' && e <= 'Z') e = (char)(e - 'A' + 'a');
        if (a != e) return false;
    }
    return *actual == '\\0' && *expected == '\\0';
}

static bool blob_from_file(struct limine_file *file,
                           struct nexus_boot_blob *out,
                           uint32_t minimum) {
    if (file == NULL || out == NULL || file->address == NULL ||
        file->size < minimum || file->size > NEXUS_PAYLOAD_LIMIT ||
        file->size > UINT32_MAX) return false;
    out->data = (const uint8_t *)file->address;
    out->size = (uint32_t)file->size;
    return true;
}

bool nexus_boot_payloads_init(void *module_response, void *executable_response) {
    struct limine_module_response *modules = module_response;
    struct limine_executable_file_response *executable = executable_response;
    payloads_ready = false;
    if (modules == NULL || executable == NULL ||
        modules->module_count < 3U || modules->module_count > 16U ||
        modules->modules == NULL || executable->executable_file == NULL)
        return false;

    struct limine_file *uefi = NULL;
    struct limine_file *bios_hdd = NULL;
    struct limine_file *bios_sys = NULL;
    for (uint64_t i = 0U; i < modules->module_count; ++i) {
        struct limine_file *file = modules->modules[i];
        if (file == NULL || file->path == NULL) continue;
        if (path_is(file->path, "boot():/EFI/BOOT/BOOTX64.EFI")) {
            if (uefi != NULL) return false;
            uefi = file;
        } else if (path_is(file->path, "boot():/boot/limine/limine-bios-hdd.bin")) {
            if (bios_hdd != NULL) return false;
            bios_hdd = file;
        } else if (path_is(file->path, "boot():/boot/limine/limine-bios.sys")) {
            if (bios_sys != NULL) return false;
            bios_sys = file;
        }
    }
    struct limine_file *kernel = executable->executable_file;
    if (uefi == NULL || bios_hdd == NULL || bios_sys == NULL ||
        !blob_from_file(kernel, &payloads.kernel_elf, 64U) ||
        !blob_from_file(uefi, &payloads.uefi_loader, 512U) ||
        !blob_from_file(bios_hdd, &payloads.bios_hdd_image, 512U) ||
        !blob_from_file(bios_sys, &payloads.bios_sys, 512U)) return false;

    const uint8_t *k = payloads.kernel_elf.data;
    if (k[0] != 0x7fU || k[1] != 'E' || k[2] != 'L' || k[3] != 'F' ||
        k[4] != 2U || k[5] != 1U) return false;
    const uint8_t *e = payloads.uefi_loader.data;
    if (e[0] != 'M' || e[1] != 'Z') return false;
    uint32_t pe = (uint32_t)e[0x3c] | ((uint32_t)e[0x3d] << 8) |
                  ((uint32_t)e[0x3e] << 16) | ((uint32_t)e[0x3f] << 24);
    if (pe > payloads.uefi_loader.size - 4U ||
        e[pe] != 'P' || e[pe + 1U] != 'E' ||
        e[pe + 2U] != 0U || e[pe + 3U] != 0U) return false;
    if (payloads.bios_hdd_image.data[510U] != 0x55U ||
        payloads.bios_hdd_image.data[511U] != 0xAAU) return false;
    payloads_ready = true;
    return true;
}

const struct nexus_boot_payloads *nexus_boot_payloads_get(void) {
    return payloads_ready ? &payloads : NULL;
}
