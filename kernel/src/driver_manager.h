#ifndef NEXUS_DRIVER_MANAGER_H
#define NEXUS_DRIVER_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nexus_pci_device;

#define NEXUS_DRIVER_MAX_DRIVERS 32U
#define NEXUS_DRIVER_MAX_BINDINGS 128U
#define NEXUS_PNP_MAX_DEVICES 256U
#define NEXUS_DRIVER_NAME_MAX 32U
#define NEXUS_DRIVER_MAX_DEPENDENCIES 8U
#define NEXUS_DRIVER_WILDCARD_U16 0xFFFFU
#define NEXUS_DRIVER_WILDCARD_U8 0xFFU

/*
 * A zero subsystem ID in a match is a wildcard. A nonzero value requests an
 * exact subsystem match; vendor/device and class fields use the explicit
 * wildcard constants below.
 */
enum nexus_driver_bus {
    NEXUS_DRIVER_BUS_PCI = 1,
    NEXUS_DRIVER_BUS_PLATFORM = 2,
    NEXUS_DRIVER_BUS_USB = 3
};

enum nexus_pnp_device_state {
    NEXUS_PNP_DEVICE_DISCOVERED = 1,
    NEXUS_PNP_DEVICE_ACTIVE,
    NEXUS_PNP_DEVICE_FAILED,
    NEXUS_PNP_DEVICE_UNSUPPORTED,
    NEXUS_PNP_DEVICE_REMOVED
};

enum nexus_driver_state {
    NEXUS_DRIVER_STATE_MATCHED = 1,
    NEXUS_DRIVER_STATE_ACTIVE,
    NEXUS_DRIVER_STATE_FAILED,
    NEXUS_DRIVER_STATE_REMOVED,
    NEXUS_DRIVER_STATE_BLOCKED
};

enum nexus_driver_error {
    NEXUS_DRIVER_ERROR_NONE = 0,
    NEXUS_DRIVER_ERROR_NO_MATCH,
    NEXUS_DRIVER_ERROR_PROBE_FAILED,
    NEXUS_DRIVER_ERROR_DEPENDENCY_MISSING,
    NEXUS_DRIVER_ERROR_DISABLED,
    NEXUS_DRIVER_ERROR_DEVICE_TABLE_FULL,
    NEXUS_DRIVER_ERROR_BINDING_TABLE_FULL,
    NEXUS_DRIVER_ERROR_DRIVER_BUSY,
    NEXUS_DRIVER_ERROR_DEPENDENT_DRIVER,
    NEXUS_DRIVER_ERROR_VERSION_NOT_NEWER,
    NEXUS_DRIVER_ERROR_INVALID_DESCRIPTOR
};

/* native_device is borrowed; keep its backing bus record valid until remove. */
struct nexus_driver_device {
    enum nexus_driver_bus bus;
    uint16_t vendor_id, device_id;
    uint16_t subsystem_vendor_id, subsystem_device_id;
    uint8_t revision_id;
    uint8_t class_code, subclass, programming_interface;
    uint8_t bus_number, slot, function;
    uint16_t interface_number;
    uint16_t segment_group;
    const void *native_device;
};

struct nexus_driver_match {
    enum nexus_driver_bus bus;
    uint16_t vendor_id, device_id;
    uint16_t subsystem_vendor_id, subsystem_device_id;
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
    const char *const *dependencies;
    size_t dependency_count;
};

struct nexus_pnp_device_info {
    enum nexus_driver_bus bus;
    enum nexus_pnp_device_state state;
    enum nexus_driver_error last_error;
    uint16_t vendor_id, device_id;
    uint16_t subsystem_vendor_id, subsystem_device_id;
    uint8_t revision_id;
    uint8_t class_code, subclass, programming_interface;
    uint8_t bus_number, slot, function;
    uint16_t interface_number;
    uint16_t segment_group;
    uint16_t probe_attempts;
    char driver_name[NEXUS_DRIVER_NAME_MAX];
    char error_driver_name[NEXUS_DRIVER_NAME_MAX];
};

struct nexus_driver_binding_info {
    char driver_name[NEXUS_DRIVER_NAME_MAX];
    enum nexus_driver_bus bus;
    enum nexus_driver_state state;
    enum nexus_driver_error error;
    uint32_t driver_version;
    uint16_t vendor_id, device_id;
    uint16_t subsystem_vendor_id, subsystem_device_id;
    uint8_t revision_id;
    uint8_t class_code, subclass, programming_interface;
    uint8_t bus_number, slot, function;
    uint16_t interface_number;
    uint16_t segment_group;
};

struct nexus_driver_info {
    char name[NEXUS_DRIVER_NAME_MAX];
    uint32_t version;
    uint32_t probe_count;
    uint32_t failure_count;
    uint16_t active_bindings;
    uint8_t dependency_count;
    bool loaded;
};

void nexus_driver_manager_init(void);
/* Register a statically linked/in-tree driver descriptor. */
bool nexus_driver_manager_register(
    const struct nexus_driver_descriptor *descriptor);
/* Install an in-tree descriptor and retry devices already in the inventory. */
bool nexus_driver_manager_install(
    const struct nexus_driver_descriptor *descriptor);
/* Load/unload registered drivers. Unload refuses live bindings/dependents. */
bool nexus_driver_manager_load(const char *name);
bool nexus_driver_manager_unload(const char *name);
/* Replace an unloaded/inactive driver with a strictly newer implementation. */
bool nexus_driver_manager_update(
    const struct nexus_driver_descriptor *descriptor);
bool nexus_driver_manager_probe_device(
    const struct nexus_driver_device *device);
bool nexus_driver_manager_probe_pci_device(
    const struct nexus_pci_device *device);
/* Discover/bind a bus device once; repeated scans do not duplicate devices. */
bool nexus_driver_manager_discover_device(
    const struct nexus_driver_device *device);
/* Inventory every retained PCI function and attempt all registered matches. */
size_t nexus_driver_manager_discover_pci_devices(void);
/* Retry failed or unsupported records after install/load/update. */
size_t nexus_driver_manager_rescan(void);
/* Remove a device and invoke its active driver's teardown callback once. */
bool nexus_driver_manager_remove_device(
    const struct nexus_driver_device *device);
size_t nexus_driver_manager_device_count(void);
bool nexus_driver_manager_device_at(
    size_t index, struct nexus_pnp_device_info *info);
size_t nexus_driver_manager_driver_count(void);
bool nexus_driver_manager_driver_at(size_t index,
                                    struct nexus_driver_info *info);
size_t nexus_driver_manager_binding_count(void);
size_t nexus_driver_manager_failure_count(void);
bool nexus_driver_manager_binding_at(
    size_t index, struct nexus_driver_binding_info *info);
const char *nexus_driver_error_name(enum nexus_driver_error error);

#endif
