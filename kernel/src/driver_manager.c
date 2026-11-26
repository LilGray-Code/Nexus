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

struct pnp_device_record {
    struct nexus_driver_device device;
    struct nexus_pnp_device_info info;
};

static struct registered_driver drivers[NEXUS_DRIVER_MAX_DRIVERS];
static struct nexus_driver_binding_info bindings[NEXUS_DRIVER_MAX_BINDINGS];
static struct nexus_driver_device binding_devices[NEXUS_DRIVER_MAX_BINDINGS];
static size_t binding_driver_indices[NEXUS_DRIVER_MAX_BINDINGS];
static struct pnp_device_record pnp_devices[NEXUS_PNP_MAX_DEVICES];
static size_t drivers_used, bindings_used, pnp_devices_used;

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

static bool matches(const struct nexus_driver_match *match,
                    const struct nexus_driver_device *device) {
    return match != NULL && device != NULL &&
        match->bus == device->bus &&
        match16(match->vendor_id, device->vendor_id) &&
        match16(match->device_id, device->device_id) &&
        match8(match->class_code, device->class_code) &&
        match8(match->subclass, device->subclass) &&
        match8(match->programming_interface,
               device->programming_interface);
}

static bool same_device(const struct nexus_driver_device *a,
                        const struct nexus_driver_device *b) {
    return a != NULL && b != NULL && a->bus == b->bus &&
        a->vendor_id == b->vendor_id && a->device_id == b->device_id &&
        a->bus_number == b->bus_number && a->slot == b->slot &&
        a->function == b->function &&
        a->interface_number == b->interface_number &&
        a->segment_group == b->segment_group;
}

static size_t find_pnp_device(const struct nexus_driver_device *device) {
    for (size_t i = 0U; i < pnp_devices_used; ++i)
        if (same_device(&pnp_devices[i].device, device)) return i;
    return pnp_devices_used;
}

static size_t remember_device(const struct nexus_driver_device *device) {
    size_t index = find_pnp_device(device);
    if (index == pnp_devices_used) {
        if (pnp_devices_used >= NEXUS_PNP_MAX_DEVICES)
            return pnp_devices_used;
        index = pnp_devices_used++;
        pnp_devices[index] = (struct pnp_device_record){0};
        pnp_devices[index].info.state = NEXUS_PNP_DEVICE_DISCOVERED;
    }
    pnp_devices[index].device = *device;
    struct nexus_pnp_device_info *info = &pnp_devices[index].info;
    info->bus = device->bus;
    info->vendor_id = device->vendor_id;
    info->device_id = device->device_id;
    info->class_code = device->class_code;
    info->subclass = device->subclass;
    info->programming_interface = device->programming_interface;
    info->bus_number = device->bus_number;
    info->slot = device->slot;
    info->function = device->function;
    info->interface_number = device->interface_number;
    info->segment_group = device->segment_group;
    if (info->state == NEXUS_PNP_DEVICE_REMOVED) {
        info->state = NEXUS_PNP_DEVICE_DISCOVERED;
        info->driver_name[0] = '\0';
        info->probe_attempts = 0U;
    }
    return index;
}

static bool append_binding(const struct registered_driver *driver,
                           const struct nexus_driver_device *device,
                           enum nexus_driver_state state) {
    if (driver == NULL || device == NULL ||
        bindings_used >= NEXUS_DRIVER_MAX_BINDINGS) return false;
    const size_t index = bindings_used++;
    struct nexus_driver_binding_info *binding = &bindings[index];
    *binding = (struct nexus_driver_binding_info){0};
    for (size_t i = 0U; i < NEXUS_DRIVER_NAME_MAX; ++i)
        binding->driver_name[i] = driver->name[i];
    binding->bus = device->bus;
    binding->state = state;
    binding->vendor_id = device->vendor_id;
    binding->device_id = device->device_id;
    binding->class_code = device->class_code;
    binding->subclass = device->subclass;
    binding->programming_interface = device->programming_interface;
    binding->bus_number = device->bus_number;
    binding->slot = device->slot;
    binding->function = device->function;
    binding->interface_number = device->interface_number;
    binding->segment_group = device->segment_group;
    binding_devices[index] = *device;
    binding_driver_indices[index] = (size_t)(driver - drivers);
    return true;
}

void nexus_driver_manager_init(void) {
    drivers_used = 0U;
    bindings_used = 0U;
    pnp_devices_used = 0U;
    for (size_t i = 0U; i < NEXUS_DRIVER_MAX_DRIVERS; ++i)
        drivers[i] = (struct registered_driver){0};
    for (size_t i = 0U; i < NEXUS_DRIVER_MAX_BINDINGS; ++i) {
        bindings[i] = (struct nexus_driver_binding_info){0};
        binding_devices[i] = (struct nexus_driver_device){0};
        binding_driver_indices[i] = 0U;
    }
    for (size_t i = 0U; i < NEXUS_PNP_MAX_DEVICES; ++i)
        pnp_devices[i] = (struct pnp_device_record){0};
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

static bool probe_record(size_t record_index) {
    if (record_index >= pnp_devices_used) return false;
    struct pnp_device_record *record = &pnp_devices[record_index];
    if (record->info.state == NEXUS_PNP_DEVICE_ACTIVE) return true;
    if (record->info.state == NEXUS_PNP_DEVICE_REMOVED) return false;
    if (bindings_used >= NEXUS_DRIVER_MAX_BINDINGS) {
        record->info.state = NEXUS_PNP_DEVICE_FAILED;
        return false;
    }

    bool matched_any = false;
    bool attempted[NEXUS_DRIVER_MAX_DRIVERS] = {false};
    for (size_t attempt = 0U; attempt < drivers_used; ++attempt) {
        size_t best = drivers_used;
        for (size_t i = 0U; i < drivers_used; ++i) {
            if (attempted[i] ||
                !matches(&drivers[i].match, &record->device)) continue;
            if (best == drivers_used ||
                drivers[i].priority > drivers[best].priority) best = i;
        }
        if (best == drivers_used) break;
        attempted[best] = true;
        matched_any = true;
        if (record->info.probe_attempts != UINT16_MAX)
            ++record->info.probe_attempts;

        const bool active =
            drivers[best].probe(&record->device, drivers[best].context);
        if (!append_binding(&drivers[best], &record->device,
                            active ? NEXUS_DRIVER_STATE_ACTIVE
                                   : NEXUS_DRIVER_STATE_FAILED)) {
            record->info.state = NEXUS_PNP_DEVICE_FAILED;
            return false;
        }
        if (active) {
            record->info.state = NEXUS_PNP_DEVICE_ACTIVE;
            (void)copy_name(record->info.driver_name, drivers[best].name);
            return true;
        }
        if (bindings_used >= NEXUS_DRIVER_MAX_BINDINGS) break;
    }

    record->info.state = matched_any
        ? NEXUS_PNP_DEVICE_FAILED : NEXUS_PNP_DEVICE_UNSUPPORTED;
    record->info.driver_name[0] = '\0';
    return false;
}

bool nexus_driver_manager_probe_device(
    const struct nexus_driver_device *device) {
    if (device == NULL || device->bus < NEXUS_DRIVER_BUS_PCI ||
        device->bus > NEXUS_DRIVER_BUS_USB) return false;
    const size_t prior = find_pnp_device(device);
    const enum nexus_pnp_device_state prior_state =
        prior == pnp_devices_used ? NEXUS_PNP_DEVICE_DISCOVERED
                                  : pnp_devices[prior].info.state;
    const size_t record = remember_device(device);
    if (record == pnp_devices_used) return false;
    if (prior != pnp_devices_used) {
        if (pnp_devices[record].info.state == NEXUS_PNP_DEVICE_ACTIVE)
            return true;
        if (prior_state != NEXUS_PNP_DEVICE_REMOVED)
            return false;
        /* A genuinely reattached device gets a fresh probe attempt. */
    }
    return probe_record(record);
}

bool nexus_driver_manager_discover_device(
    const struct nexus_driver_device *device) {
    return nexus_driver_manager_probe_device(device);
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

size_t nexus_driver_manager_discover_pci_devices(void) {
    size_t discovered = 0U;
    const uint32_t count = pci_stored_device_count();
    for (uint32_t i = 0U; i < count; ++i) {
        const struct nexus_pci_device *device = pci_device_at(i);
        if (device == NULL) continue;
        const struct nexus_driver_device generic = {
            .bus = NEXUS_DRIVER_BUS_PCI,
            .vendor_id = device->vendor_id,
            .device_id = device->device_id,
            .class_code = device->class_code,
            .subclass = device->subclass,
            .programming_interface = device->programming_interface,
            .bus_number = device->bus,
            .slot = device->slot,
            .function = device->function,
            .interface_number = 0U,
            .segment_group = device->segment_group,
            .native_device = device
        };
        const size_t prior = find_pnp_device(&generic);
        (void)nexus_driver_manager_discover_device(&generic);
        if (prior == pnp_devices_used &&
            find_pnp_device(&generic) != pnp_devices_used) ++discovered;
    }
    return discovered;
}

size_t nexus_driver_manager_rescan(void) {
    size_t active = 0U;
    for (size_t i = 0U; i < pnp_devices_used; ++i) {
        const enum nexus_pnp_device_state state = pnp_devices[i].info.state;
        if (state == NEXUS_PNP_DEVICE_ACTIVE ||
            state == NEXUS_PNP_DEVICE_REMOVED) continue;
        if (probe_record(i)) ++active;
    }
    return active;
}

bool nexus_driver_manager_remove_device(
    const struct nexus_driver_device *device) {
    if (device == NULL) return false;
    bool removed = false;
    for (size_t i = 0U; i < bindings_used; ++i) {
        struct nexus_driver_binding_info *binding = &bindings[i];
        const struct nexus_driver_device *bound = &binding_devices[i];
        if (binding->state != NEXUS_DRIVER_STATE_ACTIVE ||
            !same_device(bound, device)) continue;
        const size_t driver_index = binding_driver_indices[i];
        if (driver_index < drivers_used && drivers[driver_index].remove != NULL)
            drivers[driver_index].remove(bound, drivers[driver_index].context);
        binding->state = NEXUS_DRIVER_STATE_REMOVED;
        removed = true;
    }
    const size_t record = find_pnp_device(device);
    if (record != pnp_devices_used) {
        pnp_devices[record].info.state = NEXUS_PNP_DEVICE_REMOVED;
        pnp_devices[record].info.driver_name[0] = '\0';
        removed = true;
    }
    return removed;
}

size_t nexus_driver_manager_device_count(void) {
    return pnp_devices_used;
}

bool nexus_driver_manager_device_at(
    size_t index, struct nexus_pnp_device_info *info) {
    if (info == NULL || index >= pnp_devices_used) return false;
    *info = pnp_devices[index].info;
    return true;
}

size_t nexus_driver_manager_driver_count(void) { return drivers_used; }
size_t nexus_driver_manager_binding_count(void) { return bindings_used; }

bool nexus_driver_manager_binding_at(
    size_t index, struct nexus_driver_binding_info *info) {
    if (info == NULL || index >= bindings_used) return false;
    *info = bindings[index];
    return true;
}
