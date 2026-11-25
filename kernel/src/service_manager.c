#include "service_manager.h"
#include <stddef.h>
struct managed_service {
    char name[NEXUS_SERVICE_NAME_MAX];
    char dependencies[NEXUS_SERVICE_MAX_DEPENDENCIES][NEXUS_SERVICE_NAME_MAX];
    size_t dependency_count;
    nexus_service_start_fn start;
    nexus_service_stop_fn stop;
    void *context;
    enum nexus_service_state state;
};
static struct managed_service services[NEXUS_SERVICE_MAX_SERVICES];
static size_t services_used;
static bool copy_name(char destination[NEXUS_SERVICE_NAME_MAX],
                      const char *source) {
    if (destination == NULL || source == NULL) return false;
    size_t length = 0U;
    while (length + 1U < NEXUS_SERVICE_NAME_MAX && source[length] != '\0') {
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
static size_t find_service(const char *name) {
    for (size_t i = 0U; i < services_used; ++i)
        if (names_equal(services[i].name, name)) return i;
    return services_used;
}
void nexus_service_manager_init(void) {
    services_used = 0U;
    for (size_t i = 0U; i < NEXUS_SERVICE_MAX_SERVICES; ++i)
        services[i] = (struct managed_service){0};
}
bool nexus_service_manager_register(
    const struct nexus_service_descriptor *descriptor) {
    if (descriptor == NULL || services_used >= NEXUS_SERVICE_MAX_SERVICES ||
        descriptor->dependency_count > NEXUS_SERVICE_MAX_DEPENDENCIES ||
        (descriptor->dependency_count != 0U &&
         descriptor->dependencies == NULL) || descriptor->start == NULL)
        return false;
    struct managed_service candidate = {0};
    if (!copy_name(candidate.name, descriptor->name) ||
        find_service(candidate.name) != services_used) return false;
    candidate.dependency_count = descriptor->dependency_count;
    for (size_t i = 0U; i < candidate.dependency_count; ++i) {
        if (!copy_name(candidate.dependencies[i], descriptor->dependencies[i]) ||
            names_equal(candidate.name, candidate.dependencies[i])) return false;
        for (size_t j = 0U; j < i; ++j)
            if (names_equal(candidate.dependencies[i],
                            candidate.dependencies[j])) return false;
    }
    candidate.start = descriptor->start;
    candidate.stop = descriptor->stop;
    candidate.context = descriptor->context;
    candidate.state = NEXUS_SERVICE_REGISTERED;
    services[services_used++] = candidate;
    return true;
}
static bool start_index(size_t index) {
    if (index >= services_used) return false;
    struct managed_service *service = &services[index];
    if (service->state == NEXUS_SERVICE_RUNNING) return true;
    if (service->state == NEXUS_SERVICE_STARTING) {
        service->state = NEXUS_SERVICE_FAILED;
        return false;
    }
    service->state = NEXUS_SERVICE_STARTING;
    for (size_t i = 0U; i < service->dependency_count; ++i) {
        const size_t dep = find_service(service->dependencies[i]);
        if (dep == services_used || !start_index(dep)) {
            service->state = NEXUS_SERVICE_BLOCKED;
            return false;
        }
    }
    if (!service->start(service->context)) {
        service->state = NEXUS_SERVICE_FAILED;
        return false;
    }
    service->state = NEXUS_SERVICE_RUNNING;
    return true;
}
bool nexus_service_manager_start(const char *name) {
    const size_t index = find_service(name);
    return index != services_used && start_index(index);
}
static bool has_running_dependent(size_t target) {
    for (size_t i = 0U; i < services_used; ++i) {
        if (i == target || services[i].state != NEXUS_SERVICE_RUNNING) continue;
        for (size_t j = 0U; j < services[i].dependency_count; ++j)
            if (names_equal(services[i].dependencies[j],
                            services[target].name)) return true;
    }
    return false;
}
bool nexus_service_manager_stop(const char *name) {
    const size_t index = find_service(name);
    if (index == services_used) return false;
    struct managed_service *service = &services[index];
    if (service->state != NEXUS_SERVICE_RUNNING) return true;
    if (has_running_dependent(index)) return false;
    if (service->stop != NULL) service->stop(service->context);
    service->state = NEXUS_SERVICE_STOPPED;
    return true;
}
size_t nexus_service_manager_count(void) { return services_used; }
bool nexus_service_manager_get_info(size_t index,
                                    struct nexus_service_info *info) {
    if (info == NULL || index >= services_used) return false;
    size_t i = 0U;
    while (i + 1U < NEXUS_SERVICE_NAME_MAX && services[index].name[i] != '\0') {
        info->name[i] = services[index].name[i];
        ++i;
    }
    info->name[i] = '\0';
    info->state = services[index].state;
    info->dependency_count = (uint8_t)services[index].dependency_count;
    return true;
}
const char *nexus_service_state_name(enum nexus_service_state state) {
    switch (state) {
        case NEXUS_SERVICE_REGISTERED: return "registered";
        case NEXUS_SERVICE_STARTING: return "starting";
        case NEXUS_SERVICE_RUNNING: return "running";
        case NEXUS_SERVICE_FAILED: return "failed";
        case NEXUS_SERVICE_BLOCKED: return "blocked";
        case NEXUS_SERVICE_STOPPED: return "stopped";
        default: return "unknown";
    }
}
