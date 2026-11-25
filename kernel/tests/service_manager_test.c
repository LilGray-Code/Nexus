#include "service_manager.h"
#include <assert.h>
#include <stdint.h>
static unsigned int sequence[8];
static size_t sequence_count;
static unsigned int stop_count;
static bool start_record(void *context) {
    assert(sequence_count < sizeof(sequence) / sizeof(sequence[0]));
    sequence[sequence_count++] = (unsigned int)(uintptr_t)context;
    return true;
}
static void stop_record(void *context) { (void)context; ++stop_count; }
static bool start_fail(void *context) { (void)context; return false; }
static struct nexus_service_descriptor make_service(
    const char *name, const char *const *dependencies, size_t dependency_count,
    nexus_service_start_fn start, void *context) {
    return (struct nexus_service_descriptor){
        .name = name, .dependencies = dependencies,
        .dependency_count = dependency_count, .start = start,
        .stop = stop_record, .context = context
    };
}
int main(void) {
    nexus_service_manager_init();
    static const char *const graphics_deps[] = {"platform.timer"};
    static const char *const desktop_deps[] = {"platform.graphics"};
    struct nexus_service_descriptor timer = make_service(
        "platform.timer", NULL, 0U, start_record, (void *)(uintptr_t)1U);
    struct nexus_service_descriptor graphics = make_service(
        "platform.graphics", graphics_deps, 1U, start_record, (void *)(uintptr_t)2U);
    struct nexus_service_descriptor desktop = make_service(
        "desktop.shell", desktop_deps, 1U, start_record, (void *)(uintptr_t)3U);
    assert(nexus_service_manager_register(&desktop));
    assert(nexus_service_manager_register(&graphics));
    assert(nexus_service_manager_register(&timer));
    assert(!nexus_service_manager_register(&timer));
    assert(nexus_service_manager_count() == 3U);
    assert(nexus_service_manager_start("desktop.shell"));
    assert(sequence_count == 3U);
    assert(sequence[0] == 1U && sequence[1] == 2U && sequence[2] == 3U);
    assert(!nexus_service_manager_stop("platform.timer"));
    assert(nexus_service_manager_stop("desktop.shell"));
    assert(nexus_service_manager_stop("platform.graphics"));
    assert(nexus_service_manager_stop("platform.timer"));
    assert(stop_count == 3U);

    nexus_service_manager_init();
    static const char *const missing_dep[] = {"missing.service"};
    struct nexus_service_descriptor blocked = make_service(
        "consumer", missing_dep, 1U, start_record, (void *)(uintptr_t)4U);
    assert(nexus_service_manager_register(&blocked));
    assert(!nexus_service_manager_start("consumer"));
    struct nexus_service_info info;
    assert(nexus_service_manager_get_info(0U, &info));
    assert(info.state == NEXUS_SERVICE_BLOCKED);

    nexus_service_manager_init();
    static const char *const a_dep[] = {"cycle.b"};
    static const char *const b_dep[] = {"cycle.a"};
    struct nexus_service_descriptor a = make_service(
        "cycle.a", a_dep, 1U, start_record, (void *)(uintptr_t)5U);
    struct nexus_service_descriptor b = make_service(
        "cycle.b", b_dep, 1U, start_record, (void *)(uintptr_t)6U);
    assert(nexus_service_manager_register(&a));
    assert(nexus_service_manager_register(&b));
    assert(!nexus_service_manager_start("cycle.a"));

    nexus_service_manager_init();
    struct nexus_service_descriptor failed = make_service(
        "bad.start", NULL, 0U, start_fail, NULL);
    assert(nexus_service_manager_register(&failed));
    assert(!nexus_service_manager_start("bad.start"));
    assert(nexus_service_manager_get_info(0U, &info));
    assert(info.state == NEXUS_SERVICE_FAILED);
    return 0;
}
