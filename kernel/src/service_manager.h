#ifndef NEXUS_SERVICE_MANAGER_H
#define NEXUS_SERVICE_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NEXUS_SERVICE_MAX_SERVICES 24U
#define NEXUS_SERVICE_MAX_DEPENDENCIES 8U
#define NEXUS_SERVICE_NAME_MAX 32U
enum nexus_service_state {
    NEXUS_SERVICE_REGISTERED = 0,
    NEXUS_SERVICE_STARTING,
    NEXUS_SERVICE_RUNNING,
    NEXUS_SERVICE_FAILED,
    NEXUS_SERVICE_BLOCKED,
    NEXUS_SERVICE_STOPPED
};
typedef bool (*nexus_service_start_fn)(void *context);
typedef void (*nexus_service_stop_fn)(void *context);
struct nexus_service_descriptor {
    const char *name;
    const char *const *dependencies;
    size_t dependency_count;
    nexus_service_start_fn start;
    nexus_service_stop_fn stop;
    void *context;
};
struct nexus_service_info {
    char name[NEXUS_SERVICE_NAME_MAX];
    enum nexus_service_state state;
    uint8_t dependency_count;
};
void nexus_service_manager_init(void);
bool nexus_service_manager_register(
    const struct nexus_service_descriptor *descriptor);
bool nexus_service_manager_start(const char *name);
bool nexus_service_manager_stop(const char *name);
size_t nexus_service_manager_count(void);
bool nexus_service_manager_get_info(size_t index,
                                    struct nexus_service_info *info);
const char *nexus_service_state_name(enum nexus_service_state state);
#endif
