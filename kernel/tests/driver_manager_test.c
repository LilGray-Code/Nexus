#include "driver_manager.h"
#include "pci.h"
#include <assert.h>
#include <stdint.h>
static uint32_t failed_probe_count, successful_probe_count;
static bool fail_probe(const struct nexus_driver_device *device, void *context) {
    assert(device != NULL && context == (void *)(uintptr_t)0x11U);
    ++failed_probe_count;
    return false;
}
static bool succeed_probe(const struct nexus_driver_device *device, void *context) {
    assert(device != NULL);
    assert(context == (void *)(uintptr_t)0x1234U);
    assert(device->native_device != NULL);
    ++successful_probe_count;
    return true;
}
static struct nexus_driver_descriptor make_driver(
    const char *name, int16_t priority, nexus_driver_probe_fn probe,
    void *context) {
    return (struct nexus_driver_descriptor){
        .name = name, .version = 1U, .priority = priority,
        .match = {
            .bus = NEXUS_DRIVER_BUS_PCI,
            .vendor_id = 0x8086U, .device_id = 0x100EU,
            .class_code = 0x02U,
            .subclass = NEXUS_DRIVER_WILDCARD_U8,
            .programming_interface = NEXUS_DRIVER_WILDCARD_U8
        },
        .probe = probe, .context = context
    };
}
int main(void) {
    nexus_driver_manager_init();
    struct nexus_driver_descriptor low =
        make_driver("test-fallback", 10, succeed_probe, (void *)(uintptr_t)0x1234U);
    struct nexus_driver_descriptor high =
        make_driver("test-primary", 100, fail_probe, (void *)(uintptr_t)0x11U);
    assert(nexus_driver_manager_register(&low));
    assert(nexus_driver_manager_register(&high));
    assert(!nexus_driver_manager_register(&high));
    assert(nexus_driver_manager_driver_count() == 2U);
    struct nexus_pci_device pci = {
        .bus = 0U, .slot = 3U, .function = 0U,
        .class_code = 0x02U, .subclass = 0U, .programming_interface = 0U,
        .vendor_id = 0x8086U, .device_id = 0x100EU
    };
    assert(nexus_driver_manager_probe_pci_device(&pci));
    assert(failed_probe_count == 1U && successful_probe_count == 1U);
    assert(nexus_driver_manager_binding_count() == 2U);
    struct nexus_driver_binding_info info;
    assert(nexus_driver_manager_binding_at(0U, &info));
    assert(info.state == NEXUS_DRIVER_STATE_FAILED);
    assert(info.bus_number == 0U && info.slot == 3U);
    assert(info.vendor_id == 0x8086U && info.device_id == 0x100EU);
    assert(nexus_driver_manager_binding_at(1U, &info));
    assert(info.state == NEXUS_DRIVER_STATE_ACTIVE);
    assert(!nexus_driver_manager_binding_at(2U, &info));
    pci.vendor_id = 0x1234U;
    assert(!nexus_driver_manager_probe_pci_device(&pci));
    assert(nexus_driver_manager_binding_count() == 2U);
    return 0;
}
