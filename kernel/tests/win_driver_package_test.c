#include "win_driver_package.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, size_t at, uint16_t v) {
    p[at] = (uint8_t)v; p[at + 1U] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, size_t at, uint32_t v) {
    for (size_t i = 0U; i < 4U; ++i) p[at + i] = (uint8_t)(v >> (8U * i));
}
static void put64(uint8_t *p, size_t at, uint64_t v) {
    for (size_t i = 0U; i < 8U; ++i) p[at + i] = (uint8_t)(v >> (8U * i));
}

static void make_valid_driver(uint8_t image[0x400]) {
    memset(image, 0, 0x400U);
    put16(image, 0U, 0x5A4DU);
    put32(image, 0x3CU, 0x80U);
    put32(image, 0x80U, 0x00004550U);
    put16(image, 0x84U, 0x8664U);
    put16(image, 0x86U, 1U);
    put16(image, 0x94U, 240U);
    put16(image, 0x96U, 0x2000U);
    const size_t opt = 0x98U;
    put16(image, opt, 0x020BU);
    put32(image, opt + 16U, 0x1000U);
    put64(image, opt + 24U, 0x180000000ULL);
    put32(image, opt + 32U, 0x1000U);
    put32(image, opt + 36U, 0x200U);
    put32(image, opt + 56U, 0x2000U);
    put32(image, opt + 60U, 0x200U);
    put16(image, opt + 68U, 1U);
    put16(image, opt + 70U, 0x0100U);
    put32(image, opt + 108U, 16U);
    const size_t section = opt + 240U;
    memcpy(image + section, ".text", 5U);
    put32(image, section + 8U, 0x200U);
    put32(image, section + 12U, 0x1000U);
    put32(image, section + 16U, 0x200U);
    put32(image, section + 20U, 0x200U);
    put32(image, section + 36U, 0x60000020U);
}

static void test_combined_assessment(void) {
    static const char inf[] =
        "[Manufacturer]\r\n"
        "Maker=Models,NTamd64\r\n"
        "[Models.NTamd64]\r\n"
        "Wireless=Install, PCI\\VEN_8086&DEV_2723\r\n";
    const struct nexus_win_device_id device = {
        .bus = NEXUS_WIN_DEVICE_PCI,
        .vendor_id = 0x8086U,
        .device_id = 0x2723U
    };
    uint8_t driver[0x400];
    make_valid_driver(driver);
    struct nexus_win_driver_package_assessment assessment;
    assert(nexus_win_assess_driver_package(
        &device, inf, sizeof(inf) - 1U, driver, sizeof(driver),
        &assessment));
    assert(assessment.pe_valid);
    assert(assessment.profile == NEXUS_WIN_WIFI_PROFILE_NOT_NDIS);
    assert(strcmp(assessment.inf_match.install_section, "Install") == 0);

    driver[0] = 0U;
    assert(nexus_win_assess_driver_package(
        &device, inf, sizeof(inf) - 1U, driver, sizeof(driver),
        &assessment));
    assert(!assessment.pe_valid);
    assert(assessment.profile == NEXUS_WIN_WIFI_PROFILE_INVALID);

    const struct nexus_win_device_id other = {
        .bus = NEXUS_WIN_DEVICE_PCI, .vendor_id = 0x1234U,
        .device_id = 0x5678U
    };
    assert(!nexus_win_assess_driver_package(
        &other, inf, sizeof(inf) - 1U, driver, sizeof(driver),
        &assessment));
    assert(!nexus_win_assess_driver_package(
        NULL, inf, sizeof(inf) - 1U, driver, sizeof(driver),
        &assessment));
}

int main(void) {
    test_combined_assessment();
    puts("NEXUS WINDOWS DRIVER PACKAGE ASSESSMENT TESTS PASSED");
    return 0;
}
