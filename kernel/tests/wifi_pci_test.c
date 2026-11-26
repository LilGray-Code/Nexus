#include "wifi_pci.h"
#include "pci.h"
#include "win_driver_inf.h"
#include "win_driver_package.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>


static void pe16(uint8_t *p, size_t at, uint16_t v) {
    p[at] = (uint8_t)v; p[at + 1U] = (uint8_t)(v >> 8);
}
static void pe32(uint8_t *p, size_t at, uint32_t v) {
    for (size_t i = 0U; i < 4U; ++i) p[at + i] = (uint8_t)(v >> (8U * i));
}
static void pe64(uint8_t *p, size_t at, uint64_t v) {
    for (size_t i = 0U; i < 8U; ++i) p[at + i] = (uint8_t)(v >> (8U * i));
}
static void make_test_driver(uint8_t image[0x400]) {
    memset(image, 0, 0x400U);
    pe16(image, 0U, 0x5A4DU); pe32(image, 0x3CU, 0x80U);
    pe32(image, 0x80U, 0x00004550U); pe16(image, 0x84U, 0x8664U);
    pe16(image, 0x86U, 1U); pe16(image, 0x94U, 240U);
    pe16(image, 0x96U, 0x2000U);
    const size_t opt = 0x98U;
    pe16(image, opt, 0x020BU); pe32(image, opt + 16U, 0x1000U);
    pe64(image, opt + 24U, 0x180000000ULL);
    pe32(image, opt + 32U, 0x1000U); pe32(image, opt + 36U, 0x200U);
    pe32(image, opt + 56U, 0x2000U); pe32(image, opt + 60U, 0x200U);
    pe16(image, opt + 68U, 1U); pe16(image, opt + 70U, 0x0100U);
    pe32(image, opt + 108U, 16U);
    const size_t sec = opt + 240U;
    memcpy(image + sec, ".text", 5U);
    pe32(image, sec + 8U, 0x200U); pe32(image, sec + 12U, 0x1000U);
    pe32(image, sec + 16U, 0x200U); pe32(image, sec + 20U, 0x200U);
    pe32(image, sec + 36U, 0x60000020U);
}

static struct nexus_pci_device devices[4];
static uint32_t count;

uint32_t pci_stored_device_count(void) { return count; }
const struct nexus_pci_device *pci_device_at(uint32_t index) {
    return index < count ? &devices[index] : NULL;
}

int main(void) {
    devices[0] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x00U,
        .vendor_id = 0x8086U, .device_id = 0x100EU
    };
    devices[1] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x80U,
        .bus = 2U, .slot = 3U,
        .vendor_id = 0x8086U, .device_id = 0x2723U,
        .revision_id = 0x01U,
        .subsystem_vendor_id = 0x8086U,
        .subsystem_device_id = 0x0024U
    };
    devices[2] = (struct nexus_pci_device){
        .class_code = 0x02U, .subclass = 0x80U,
        .bus = 4U, .slot = 1U,
        .vendor_id = 0x10ECU, .device_id = 0xC821U
    };
    devices[3] = (struct nexus_pci_device){
        .class_code = 0x03U, .subclass = 0x00U,
        .vendor_id = 0x1234U, .device_id = 0x1111U
    };
    count = 4U;
    assert(wifi_pci_discover());
    assert(wifi_pci_candidate_count() == 2U);
    const struct nexus_wifi_pci_candidate *first = wifi_pci_candidate_at(0U);
    const struct nexus_wifi_pci_candidate *second = wifi_pci_candidate_at(1U);
    assert(first != NULL && first->vendor_id == 0x8086U);
    assert(first->bus == 2U && first->slot == 3U);
    assert(first->revision_id == 0x01U);
    assert(first->subsystem_vendor_id == 0x8086U);
    assert(first->subsystem_device_id == 0x0024U);
    assert(second != NULL && second->vendor_id == 0x10ECU);
    static const char intel_inf[] =
        "[Manufacturer]\r\n"
        "Intel=Models,NTamd64\r\n"
        "[Models.NTamd64]\r\n"
        "Intel Wi-Fi=Install, PCI\\VEN_8086&DEV_2723&SUBSYS_00248086&REV_01\r\n";
    struct nexus_win_inf_match match;
    assert(wifi_pci_match_windows_inf(0U, intel_inf,
                                      sizeof(intel_inf) - 1U, &match));
    assert(strcmp(match.install_section, "Install") == 0);
    assert(match.specificity == 3U);

    static const char package_inf[] =
        "[Manufacturer]\r\n"
        "Maker=Models,NTamd64\r\n"
        "[Models.NTamd64]\r\n"
        "Wireless=Install, PCI\\VEN_8086&DEV_2723&SUBSYS_00248086&REV_01\r\n";
    uint8_t driver_image[0x400];
    make_test_driver(driver_image);
    struct nexus_win_driver_package_assessment assessment;
    assert(wifi_pci_assess_windows_package(
        0U, package_inf, sizeof(package_inf) - 1U,
        driver_image, sizeof(driver_image), &assessment));
    assert(assessment.pe_valid);
    assert(assessment.profile == NEXUS_WIN_WIFI_PROFILE_NOT_NDIS);
    assert(strcmp(assessment.inf_match.install_section, "Install") == 0);

    assert(!wifi_pci_match_windows_inf(1U, intel_inf,
                                       sizeof(intel_inf) - 1U, &match));

    assert(second->vendor_name[0] == 'R');
    assert(wifi_pci_candidate_at(2U) == NULL);
    count = 0U;
    assert(!wifi_pci_discover());
    assert(wifi_pci_candidate_count() == 0U);
    puts("NEXUS PCI WIRELESS INVENTORY TESTS PASSED");
    return 0;
}
