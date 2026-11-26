#ifndef NEXUS_DRIVER_MANAGER_H
#define NEXUS_DRIVER_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct nexus_pci_device;
#define NEXUS_DRIVER_MAX_DRIVERS 32U
#define NEXUS_DRIVER_MAX_BINDINGS 128U
#define NEXUS_DRIVER_NAME_MAX 32U
#define NEXUS_DRIVER_WILDCARD_U16 0xFFFFU
#define NEXUS_DRIVER_WILDCARD_U8 0xFFU
enum nexus_driver_bus {
    NEXUS_DRIVER_BUS_PCI = 1,
    NEXUS_DRIVER_BUS_PLATFORM = 2,
    NEXUS_DRIVER_BUS_USB = 3
};
enum nexus_driver_state {
    NEXUS_DRIVER_STATE_MATCHED = 1,
    NEXUS_DRIVER_STATE_ACTIVE,
    NEXUS_DRIVER_STATE_FAILED,
    NEXUS_DRIVER_STATE_REMOVED
};
/* native_device is a borrowed bus-specific pointer, valid only inside probe. */
enum nexus_driver_probe_outcome {
    NEXUS_DRIVER_PROBE_INVALID = 0,
    NEXUS_DRIVER_PROBE_NO_MATCH,
    NEXUS_DRIVER_PROBE_ACTIVE,
    NEXUS_DRIVER_PROBE_ALL_FAILED,
    NEXUS_DRIVER_PROBE_CAPACITY_EXHAUSTED
};
struct nexus_driver_probe_report {
    enum nexus_driver_probe_outcome outcome;
    size_t matching_driver_count;
    size_t attempted_driver_count;
    size_t failed_driver_count;
    char active_driver_name[NEXUS_DRIVER_NAME_MAX];
};
struct nexus_driver_device {
    enum nexus_driver_bus bus;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, programming_interface;
    uint8_t bus_number, slot, function;
    uint16_t interface_number;
    uint16_t segment_group;
    const void *native_device;
};
struct nexus_driver_match {
    enum nexus_driver_bus bus;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, programming_interface;
};
typedef bool (*nexus_driver_probe_fn)(
    const struct nexus_driver_device *device, void *context);
typedef void (*nexus_driver_remove_fn)(
    const struct nexus_driver_device *device, void *context);
struct nexus_driver_descriptor {
    const char *name;
    uint32_t version;
    int16_t priority;
    struct nexus_driver_match match;
    nexus_driver_probe_fn probe;
    nexus_driver_remove_fn remove;
    void *context;
};
struct nexus_driver_binding_info {
    char driver_name[NEXUS_DRIVER_NAME_MAX];
    enum nexus_driver_bus bus;
    enum nexus_driver_state state;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, programming_interface;
    uint8_t bus_number, slot, function;
    uint16_t interface_number;
    uint16_t segment_group;
};
void nexus_driver_manager_init(void);
bool nexus_driver_manager_register(
    const struct nexus_driver_descriptor *descriptor);
/* Diagnostic probe API: distinguishes missing drivers from failed probes. */
enum nexus_driver_probe_outcome nexus_driver_manager_probe_device_report(
    const struct nexus_driver_device *device,
    struct nexus_driver_probe_report *report);
/* Compatibility wrapper; true only when a driver becomes active. */
bool nexus_driver_manager_probe_device(
    const struct nexus_driver_device *device);
enum nexus_driver_probe_outcome nexus_driver_manager_probe_pci_device_report(
    const struct nexus_pci_device *device,
    struct nexus_driver_probe_report *report);
bool nexus_driver_manager_probe_pci_device(
    const struct nexus_pci_device *device);
/* Unbind a removed device and invoke the active driver's remove callback. */
bool nexus_driver_manager_remove_device(
    const struct nexus_driver_device *device);
size_t nexus_driver_manager_driver_count(void);
size_t nexus_driver_manager_binding_count(void);
bool nexus_driver_manager_binding_at(
    size_t index, struct nexus_driver_binding_info *info);
#endif
