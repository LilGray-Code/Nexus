#include "win_driver_pe.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t image[0x800];

static void put16(size_t at, uint16_t value) {
    image[at] = (uint8_t)value;
    image[at + 1U] = (uint8_t)(value >> 8);
}
static void put32(size_t at, uint32_t value) {
    for (size_t i = 0U; i < 4U; ++i)
        image[at + i] = (uint8_t)(value >> (i * 8U));
}
static void put64(size_t at, uint64_t value) {
    for (size_t i = 0U; i < 8U; ++i)
        image[at + i] = (uint8_t)(value >> (i * 8U));
}

static void make_valid_driver(void) {
    memset(image, 0, sizeof(image));
    put16(0U, 0x5A4DU);
    put32(0x3CU, 0x80U);
    put32(0x80U, 0x00004550U);
    put16(0x84U, 0x8664U);
    put16(0x86U, 1U);
    put16(0x94U, 240U);
    put16(0x96U, 0x2000U);
    const size_t opt = 0x98U;
    put16(opt, 0x020BU);
    put32(opt + 16U, 0x1000U);
    put64(opt + 24U, 0x180000000ULL);
    put32(opt + 32U, 0x1000U);
    put32(opt + 36U, 0x200U);
    put32(opt + 56U, 0x2000U);
    put32(opt + 60U, 0x200U);
    put16(opt + 68U, 1U);
    put16(opt + 70U, 0x0100U);
    put32(opt + 108U, 16U);
    put32(opt + 120U, 0x1100U);
    put32(opt + 124U, 40U);

    const size_t section = opt + 240U;
    memcpy(image + section, ".rdata", 6U);
    put32(section + 8U, 0x600U);
    put32(section + 12U, 0x1000U);
    put32(section + 16U, 0x600U);
    put32(section + 20U, 0x200U);

    put32(0x300U, 0x1140U);
    put32(0x30CU, 0x1130U);
    put32(0x310U, 0x1160U);
    memcpy(image + 0x330U, "NDIS.sys", 9U);
    put64(0x340U, 0x1180U);
    put64(0x348U, 0U);
    put64(0x360U, 0x1180U);
    put64(0x368U, 0U);
    put16(0x380U, 0U);
    memcpy(image + 0x382U, "NdisMRegisterMiniportDriver", 28U);
    image[0x39EU] = 0U;
}

static void test_valid_x64_driver_and_imports(void) {
    make_valid_driver();
    struct nexus_win_pe_info info;
    assert(nexus_win_pe_inspect(image, sizeof(image), &info));
    assert(info.machine == 0x8664U);
    assert(info.section_count == 1U);
    assert(info.entry_point_rva == 0x1000U);
    assert(info.image_size == 0x2000U);
    assert(info.is_dll && info.nx_compatible);
    assert(info.import_module_count == 1U);
    assert(info.import_symbol_count == 1U);
    assert(strcmp(info.import_modules[0], "NDIS.sys") == 0);
}

static void test_rejects_bad_headers(void) {
    make_valid_driver();
    struct nexus_win_pe_info info;
    put32(0x3CU, UINT32_MAX);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
    make_valid_driver();
    put16(0x84U, 0x014CU);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
    make_valid_driver();
    put16(0x94U, 96U);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
    assert(!nexus_win_pe_inspect(image, 32U, &info));
}

static void test_rejects_bad_imports(void) {
    make_valid_driver();
    struct nexus_win_pe_info info;
    put32(0x30CU, 0xFFFFFFF0U);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
    make_valid_driver();
    put32(0x300U, 0x15F8U);
    put64(0x7F8U, 0x1180U);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
    make_valid_driver();
    put32(0x110U, 0U);
    assert(!nexus_win_pe_inspect(image, sizeof(image), &info));
}

static void test_valid_image_without_imports(void) {
    make_valid_driver();
    put32(0x110U, 0U);
    put32(0x114U, 0U);
    struct nexus_win_pe_info info;
    assert(nexus_win_pe_inspect(image, sizeof(image), &info));
    assert(info.import_module_count == 0U);
}

int main(void) {
    test_valid_x64_driver_and_imports();
    test_rejects_bad_headers();
    test_rejects_bad_imports();
    test_valid_image_without_imports();
    puts("NEXUS WINDOWS PE DRIVER INSPECTION TESTS PASSED");
    return 0;
}
