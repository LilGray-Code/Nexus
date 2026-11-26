#include "win_driver_inf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char inf[] =
    "; Windows wireless driver package sample\r\n"
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "[Manufacturer]\r\n"
    "%Maker%=Models,NTamd64\r\n"
    "[Models.NTamd64]\r\n"
    "%Wireless%=Install, PCI\\VEN_8086&DEV_2723, PCI\\VEN_8086&DEV_2723&SUBSYS_00248086&REV_01\r\n"
    "%USBWifi%=UsbInstall, USB\\VID_0BDA&PID_C820\r\n"
    "[Install]\r\n"
    "CopyFiles=DriverCopy\r\n"
    "[UsbInstall]\r\n";

static void test_pci_best_exact_match(void) {
    const struct nexus_win_device_id device = {
        .bus = NEXUS_WIN_DEVICE_PCI,
        .vendor_id = 0x8086U, .device_id = 0x2723U,
        .subsystem_vendor_id = 0x8086U,
        .subsystem_device_id = 0x0024U, .revision_id = 0x01U
    };
    struct nexus_win_inf_match match;
    assert(nexus_win_inf_find_match(inf, sizeof(inf) - 1U, &device, &match));
    assert(strcmp(match.model_description, "%Wireless%") == 0);
    assert(strcmp(match.install_section, "Install") == 0);
    assert(strstr(match.hardware_id, "SUBSYS_00248086") != NULL);
    assert(match.specificity == 3U);
    assert(match.line_number == 7U);
}

static void test_pci_wrong_subsystem_rejected(void) {
    const struct nexus_win_device_id device = {
        .bus = NEXUS_WIN_DEVICE_PCI,
        .vendor_id = 0x8086U, .device_id = 0x2723U,
        .subsystem_vendor_id = 0x1234U,
        .subsystem_device_id = 0x5678U, .revision_id = 0x01U
    };
    struct nexus_win_inf_match match;
    assert(nexus_win_inf_find_match(inf, sizeof(inf) - 1U, &device, &match));
    assert(match.specificity == 1U);
    assert(strcmp(match.hardware_id, "PCI\\VEN_8086&DEV_2723") == 0);
}

static void test_usb_match(void) {
    const struct nexus_win_device_id device = {
        .bus = NEXUS_WIN_DEVICE_USB,
        .vendor_id = 0x0BDAU, .device_id = 0xC820U
    };
    struct nexus_win_inf_match match;
    assert(nexus_win_inf_find_match(inf, sizeof(inf) - 1U, &device, &match));
    assert(strcmp(match.install_section, "UsbInstall") == 0);
    assert(match.specificity == 1U);
}

static void test_nonmatching_bus_and_bad_input(void) {
    const struct nexus_win_device_id device = {
        .bus = NEXUS_WIN_DEVICE_PCI, .vendor_id = 0xFFFFU,
        .device_id = 0xFFFFU
    };
    struct nexus_win_inf_match match;
    assert(!nexus_win_inf_find_match(inf, sizeof(inf) - 1U, &device, &match));
    assert(!nexus_win_inf_find_match(NULL, 1U, &device, &match));
    assert(!nexus_win_inf_find_match(inf, sizeof(inf) - 1U, NULL, &match));
}

int main(void) {
    test_pci_best_exact_match();
    test_pci_wrong_subsystem_rejected();
    test_usb_match();
    test_nonmatching_bus_and_bad_input();
    puts("NEXUS WINDOWS INF HARDWARE-ID MATCH TESTS PASSED");
    return 0;
}
