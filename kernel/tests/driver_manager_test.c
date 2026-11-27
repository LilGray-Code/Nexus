#include "driver_manager.h"
#include "pci.h"
#include "security.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static struct nexus_pci_device pci_test_devices[4];
static uint32_t pci_test_device_count;
uint32_t pci_stored_device_count(void) { return pci_test_device_count; }
const struct nexus_pci_device *pci_device_at(uint32_t index) {
    return index < pci_test_device_count ? &pci_test_devices[index] : NULL;
}
static uint32_t failed_probe_count, successful_probe_count, remove_count;
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
static void remove_device(const struct nexus_driver_device *device, void *context) { assert(device != NULL && context == (void *)(uintptr_t)0x1234U); ++remove_count; }
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
        .probe = probe, .remove = probe == succeed_probe ? remove_device : NULL, .context = context
    };
}
static bool succeed_unsupported(const struct nexus_driver_device *device,
                                void *context) {
    assert(device != NULL && context == (void *)(uintptr_t)0x5678U);
    ++successful_probe_count;
    return true;
}

int main(void) {
    nexus_security_init();
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

    uint64_t audit_cursor = 1U;
    struct nexus_security_event audit_events[8];
    const size_t audit_count = nexus_security_audit_read(
        &audit_cursor, audit_events, 8U);
    bool duplicate_rejection_logged = false;
    for (size_t i = 0U; i < audit_count; ++i) {
        if (audit_events[i].type == NEXUS_SECURITY_EVENT_DRIVER_REJECTED &&
            audit_events[i].code == NEXUS_SECURITY_REASON_DRIVER_UNTRUSTED) {
            duplicate_rejection_logged = true;
            break;
        }
    }
    assert(duplicate_rejection_logged);
    assert(nexus_driver_manager_binding_count() == 2U);
    assert(nexus_driver_manager_device_count() == 1U);
    struct nexus_driver_binding_info binding;
    assert(nexus_driver_manager_binding_at(0U, &binding));
    assert(binding.state == NEXUS_DRIVER_STATE_FAILED);
    assert(binding.bus_number == 0U && binding.slot == 3U);
    assert(binding.vendor_id == 0x8086U && binding.device_id == 0x100EU);
    assert(nexus_driver_manager_binding_at(1U, &binding));
    assert(binding.state == NEXUS_DRIVER_STATE_ACTIVE);
    assert(!nexus_driver_manager_binding_at(2U, &binding));

    struct nexus_pnp_device_info pnp;
    assert(nexus_driver_manager_device_at(0U, &pnp));
    assert(pnp.state == NEXUS_PNP_DEVICE_ACTIVE);
    assert(pnp.probe_attempts == 2U);
    assert(pnp.bus == NEXUS_DRIVER_BUS_PCI);
    assert(pnp.segment_group == 0U);
    assert(strcmp(pnp.driver_name, "test-fallback") == 0);

    /* Repeated discovery is idempotent; it must not rebind or duplicate entries. */
    assert(nexus_driver_manager_probe_pci_device(&pci));
    assert(nexus_driver_manager_binding_count() == 2U);
    assert(failed_probe_count == 1U && successful_probe_count == 1U);

    const struct nexus_driver_device removed = {
        .bus=NEXUS_DRIVER_BUS_PCI, .vendor_id=0x8086U, .device_id=0x100EU,
        .class_code=0x02U, .bus_number=0U, .slot=3U, .function=0U
    };
    assert(nexus_driver_manager_remove_device(&removed));
    assert(remove_count == 1U);
    assert(nexus_driver_manager_binding_at(1U, &binding));
    assert(binding.state == NEXUS_DRIVER_STATE_REMOVED);
    assert(nexus_driver_manager_device_at(0U, &pnp));
    assert(pnp.state == NEXUS_PNP_DEVICE_REMOVED);
    assert(!nexus_driver_manager_remove_device(&removed));

    /* The same BDF can exist in another PCI segment. */
    struct nexus_pci_device other_segment = pci;
    other_segment.segment_group = 1U;
    assert(nexus_driver_manager_probe_pci_device(&other_segment));
    assert(nexus_driver_manager_binding_count() == 4U);
    assert(nexus_driver_manager_device_count() == 2U);
    assert(nexus_driver_manager_binding_at(3U, &binding));
    assert(binding.state == NEXUS_DRIVER_STATE_ACTIVE);
    assert(binding.segment_group == 1U);
    struct nexus_driver_device other_removed = removed;
    other_removed.segment_group = 1U;
    assert(nexus_driver_manager_remove_device(&other_removed));
    assert(nexus_driver_manager_binding_at(3U, &binding));
    assert(binding.state == NEXUS_DRIVER_STATE_REMOVED);
    assert(remove_count == 2U);

    /* Unsupported hardware remains visible and can be rebound after a driver
     * is registered, without rescanning and duplicating old bindings.
     */
    struct nexus_driver_device unsupported = {
        .bus = NEXUS_DRIVER_BUS_PCI, .vendor_id = 0x1234U,
        .device_id = 0x5678U, .class_code = 0x04U,
        .bus_number = 0U, .slot = 6U, .function = 0U
    };
    assert(!nexus_driver_manager_discover_device(&unsupported));
    assert(nexus_driver_manager_device_count() == 3U);
    assert(nexus_driver_manager_device_at(2U, &pnp));
    assert(pnp.state == NEXUS_PNP_DEVICE_UNSUPPORTED);
    assert(pnp.probe_attempts == 0U);

    struct nexus_driver_descriptor late = {
        .name = "late-audio",
        .version = 1U,
        .priority = 50,
        .match = {
            .bus = NEXUS_DRIVER_BUS_PCI,
            .vendor_id = 0x1234U,
            .device_id = 0x5678U,
            .class_code = 0x04U,
            .subclass = NEXUS_DRIVER_WILDCARD_U8,
            .programming_interface = NEXUS_DRIVER_WILDCARD_U8
        },
        .probe = succeed_unsupported,
        .context = (void *)(uintptr_t)0x5678U
    };
    assert(nexus_driver_manager_register(&late));
    assert(nexus_driver_manager_rescan() == 1U);
    assert(nexus_driver_manager_device_at(2U, &pnp));
    assert(pnp.state == NEXUS_PNP_DEVICE_ACTIVE);
    assert(strcmp(pnp.driver_name, "late-audio") == 0);
    const size_t bindings_after_rescan = nexus_driver_manager_binding_count();
    assert(nexus_driver_manager_rescan() == 0U);
    assert(nexus_driver_manager_binding_count() == bindings_after_rescan);

    /* PCI enumeration records unsupported devices instead of silently
     * discarding them; subsequent full scans do not create duplicates.
     */
    pci_test_devices[0] = (struct nexus_pci_device){
        .bus = 0U, .slot = 10U, .function = 0U,
        .class_code = 0x01U, .subclass = 0x06U,
        .vendor_id = 0xCAFEU, .device_id = 0x1000U
    };
    pci_test_devices[1] = (struct nexus_pci_device){
        .bus = 0U, .slot = 8U, .function = 0U,
        .class_code = 0x03U, .subclass = 0U,
        .vendor_id = 0x8086U, .device_id = 0x1234U
    };
    pci_test_device_count = 2U;
    assert(nexus_driver_manager_discover_pci_devices() == 2U);
    assert(nexus_driver_manager_device_count() == 4U);
    const size_t final_devices = nexus_driver_manager_device_count();
    assert(nexus_driver_manager_discover_pci_devices() == 0U);
    assert(nexus_driver_manager_device_count() == final_devices);
    return 0;
}
