#include "driver_manager.h"
#include "pci.h"
#include <stddef.h>
struct registered_driver {
    char name[NEXUS_DRIVER_NAME_MAX];
    uint32_t version;
    int16_t priority;
    struct nexus_driver_match match;
    nexus_driver_probe_fn probe;
    nexus_driver_remove_fn remove;
    void *context;
};
static struct registered_driver drivers[NEXUS_DRIVER_MAX_DRIVERS];
static struct nexus_driver_binding_info bindings[NEXUS_DRIVER_MAX_BINDINGS];
static size_t drivers_used, bindings_used;
static struct nexus_driver_device binding_devices[NEXUS_DRIVER_MAX_BINDINGS];
static size_t binding_driver_indices[NEXUS_DRIVER_MAX_BINDINGS];
static bool copy_name(char destination[NEXUS_DRIVER_NAME_MAX],
                      const char *source) {
    if (destination == NULL || source == NULL) return false;
    size_t length = 0U;
    while (length + 1U < NEXUS_DRIVER_NAME_MAX && source[length] != '\0') {
        const char c = source[length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return false;
        destination[length++] = c;
    }
    if (length == 0U || source[length] != '\0') return false;
    destination[length] = '\0';
    return true;
}
static bool names_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    size_t i = 0U;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == b[i];
}
static bool match16(uint16_t expected, uint16_t actual) {
    return expected == NEXUS_DRIVER_WILDCARD_U16 || expected == actual;
}
static bool match8(uint8_t expected, uint8_t actual) {
    return expected == NEXUS_DRIVER_WILDCARD_U8 || expected == actual;
}
static bool matches(const struct nexus_driver_match *m,
                    const struct nexus_driver_device *d) {
    return m != NULL && d != NULL && m->bus == d->bus &&
        match16(m->vendor_id, d->vendor_id) &&
        match16(m->device_id, d->device_id) &&
        match8(m->class_code, d->class_code) &&
        match8(m->subclass, d->subclass) &&
        match8(m->programming_interface, d->programming_interface);
}
static bool append_binding(const struct registered_driver *driver,
                           const struct nexus_driver_device *device,
                           enum nexus_driver_state state) {
    if (driver == NULL || device == NULL ||
        bindings_used >= NEXUS_DRIVER_MAX_BINDINGS) return false;
    struct nexus_driver_binding_info *b = &bindings[bindings_used++];
    *b = (struct nexus_driver_binding_info){0};
    for (size_t i = 0U; i < NEXUS_DRIVER_NAME_MAX; ++i)
        b->driver_name[i] = driver->name[i];
    b->bus = device->bus;
    b->state = state;
    b->vendor_id = device->vendor_id;
    b->device_id = device->device_id;
    b->class_code = device->class_code;
    b->subclass = device->subclass;
    b->programming_interface = device->programming_interface;
    b->bus_number = device->bus_number;
    b->slot = device->slot;
    b->function = device->function;
    b->interface_number = device->interface_number;
    b->segment_group = device->segment_group;
    const size_t index = bindings_used - 1U;
    binding_devices[index] = *device;
    binding_driver_indices[index] = (size_t)(driver - drivers);
    return true;
}
void nexus_driver_manager_init(void) {
    drivers_used = 0U;
    bindings_used = 0U;
    for (size_t i = 0U; i < NEXUS_DRIVER_MAX_DRIVERS; ++i)
        drivers[i] = (struct registered_driver){0};
    for (size_t i = 0U; i < NEXUS_DRIVER_MAX_BINDINGS; ++i)
        bindings[i] = (struct nexus_driver_binding_info){0};
    for (size_t i = 0U; i < NEXUS_DRIVER_MAX_BINDINGS; ++i) {
        binding_devices[i] = (struct nexus_driver_device){0};
        binding_driver_indices[i] = 0U;
    }
}
bool nexus_driver_manager_register(
    const struct nexus_driver_descriptor *descriptor) {
    if (descriptor == NULL || descriptor->probe == NULL ||
        drivers_used >= NEXUS_DRIVER_MAX_DRIVERS ||
        descriptor->match.bus < NEXUS_DRIVER_BUS_PCI ||
        descriptor->match.bus > NEXUS_DRIVER_BUS_USB) return false;
    struct registered_driver candidate = {0};
    if (!copy_name(candidate.name, descriptor->name)) return false;
    for (size_t i = 0U; i < drivers_used; ++i)
        if (names_equal(drivers[i].name, candidate.name)) return false;
    candidate.version = descriptor->version;
    candidate.priority = descriptor->priority;
    candidate.match = descriptor->match;
    candidate.probe = descriptor->probe;
    candidate.remove = descriptor->remove;
    candidate.context = descriptor->context;
    drivers[drivers_used++] = candidate;
    return true;
}
bool nexus_driver_manager_probe_device(
    const struct nexus_driver_device *device) {
    if (device == NULL || device->bus < NEXUS_DRIVER_BUS_PCI ||
        device->bus > NEXUS_DRIVER_BUS_USB) return false;
    bool attempted[NEXUS_DRIVER_MAX_DRIVERS] = {false};
    for (size_t attempt = 0U; attempt < drivers_used; ++attempt) {
        size_t best = drivers_used;
        for (size_t i = 0U; i < drivers_used; ++i) {
            if (attempted[i] || !matches(&drivers[i].match, device)) continue;
            if (best == drivers_used ||
                drivers[i].priority > drivers[best].priority) best = i;
        }
        if (best == drivers_used) break;
        attempted[best] = true;
        if (bindings_used >= NEXUS_DRIVER_MAX_BINDINGS) return false;
        const bool active = drivers[best].probe(device, drivers[best].context);
        if (!append_binding(&drivers[best], device,
                            active ? NEXUS_DRIVER_STATE_ACTIVE
                                   : NEXUS_DRIVER_STATE_FAILED)) return false;
        if (active) return true;
    }
    return false;
}
bool nexus_driver_manager_probe_pci_device(
    const struct nexus_pci_device *device) {
    if (device == NULL) return false;
    const struct nexus_driver_device generic = {
        .bus = NEXUS_DRIVER_BUS_PCI,
        .vendor_id = device->vendor_id, .device_id = device->device_id,
        .class_code = device->class_code, .subclass = device->subclass,
        .programming_interface = device->programming_interface,
        .bus_number = device->bus, .slot = device->slot,
        .function = device->function, .interface_number = 0U,
        .segment_group = device->segment_group,
        .native_device = device
    };
    return nexus_driver_manager_probe_device(&generic);
}

bool nexus_driver_manager_remove_device(
    const struct nexus_driver_device *device) {
    if (device == NULL) return false;
    bool removed = false;
    for (size_t i = 0U; i < bindings_used; ++i) {
        struct nexus_driver_binding_info *binding = &bindings[i];
        const struct nexus_driver_device *bound = &binding_devices[i];
        if (binding->state != NEXUS_DRIVER_STATE_ACTIVE ||
            bound->bus != device->bus ||
            bound->vendor_id != device->vendor_id ||
            bound->device_id != device->device_id ||
            bound->bus_number != device->bus_number ||
            bound->slot != device->slot ||
            bound->function != device->function ||
            bound->interface_number != device->interface_number ||
            bound->segment_group != device->segment_group)
            continue;
        const size_t driver_index = binding_driver_indices[i];
        if (driver_index < drivers_used && drivers[driver_index].remove != NULL)
            drivers[driver_index].remove(bound, drivers[driver_index].context);
        binding->state = NEXUS_DRIVER_STATE_REMOVED;
        removed = true;
    }
    return removed;
}

size_t nexus_driver_manager_driver_count(void) { return drivers_used; }
size_t nexus_driver_manager_binding_count(void) { return bindings_used; }
bool nexus_driver_manager_binding_at(
    size_t index, struct nexus_driver_binding_info *info) {
    if (info == NULL || index >= bindings_used) return false;
    *info = bindings[index];
    return true;
}
