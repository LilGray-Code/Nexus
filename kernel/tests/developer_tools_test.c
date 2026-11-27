#include "diagnostics.h"

#include "blockdev.h"
#include "cpu.h"
#include "driver_manager.h"
#include "memory.h"
#include "netdev.h"
#include "pci.h"
#include "trace.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct nexus_cpu_info mock_cpu = {
    .cpuid_available = true,
    .vendor = "GenuineIntel",
    .brand = "Nexus Test CPU",
    .family = 6U,
    .model = 42U,
    .stepping = 7U,
    .physical_cores_per_package = 8U,
    .logical_processors_per_package = 16U,
    .features = NEXUS_CPU_FEATURE_AVX2
};

const struct nexus_cpu_info *nexus_cpu_get_info(void) {
    return &mock_cpu;
}

uint32_t pci_device_count(void) { return 12U; }
uint32_t pci_stored_device_count(void) { return 12U; }

size_t nexus_driver_manager_device_count(void) { return 5U; }
size_t nexus_driver_manager_driver_count(void) { return 3U; }
size_t nexus_driver_manager_binding_count(void) { return 4U; }
size_t nexus_driver_manager_failure_count(void) { return 1U; }

/* Keep the diagnostics test hermetic: mock the manager's display helper,
 * while the production implementation remains in driver_manager.c. */
const char *nexus_driver_error_name(enum nexus_driver_error error) {
    switch (error) {
        case NEXUS_DRIVER_ERROR_NONE: return "none";
        case NEXUS_DRIVER_ERROR_NO_MATCH: return "no-match";
        case NEXUS_DRIVER_ERROR_PROBE_FAILED: return "probe-failed";
        case NEXUS_DRIVER_ERROR_DEPENDENCY_MISSING: return "dependency-missing";
        case NEXUS_DRIVER_ERROR_DISABLED: return "driver-disabled";
        case NEXUS_DRIVER_ERROR_DEVICE_TABLE_FULL: return "device-table-full";
        case NEXUS_DRIVER_ERROR_BINDING_TABLE_FULL: return "binding-table-full";
        case NEXUS_DRIVER_ERROR_DRIVER_BUSY: return "driver-busy";
        case NEXUS_DRIVER_ERROR_DEPENDENT_DRIVER: return "dependent-driver";
        case NEXUS_DRIVER_ERROR_VERSION_NOT_NEWER: return "version-not-newer";
        case NEXUS_DRIVER_ERROR_INVALID_DESCRIPTOR: return "invalid-descriptor";
        default: return "unknown";
    }
}
bool nexus_driver_manager_driver_at(size_t index,
                                    struct nexus_driver_info *info) {
    static const char *const names[] = { "nvme", "ahci", "e1000" };
    if (info == NULL || index >= 3U) return false;
    memset(info, 0, sizeof(*info));
    memcpy(info->name, names[index], strlen(names[index]) + 1U);
    info->version = 1U;
    info->loaded = true;
    info->active_bindings = index == 0U ? 1U : 0U;
    info->failure_count = index == 1U ? 1U : 0U;
    return true;
}
bool nexus_driver_manager_binding_at(
    size_t index, struct nexus_driver_binding_info *info) {
    if (info == NULL || index >= 4U) return false;
    memset(info, 0, sizeof(*info));
    memcpy(info->driver_name, "ahci", 5U);
    info->state = index == 1U ? NEXUS_DRIVER_STATE_FAILED
                              : NEXUS_DRIVER_STATE_ACTIVE;
    info->error = index == 1U ? NEXUS_DRIVER_ERROR_PROBE_FAILED
                              : NEXUS_DRIVER_ERROR_NONE;
    return true;
}
bool nexus_driver_manager_device_at(size_t index,
                                    struct nexus_pnp_device_info *info) {
    if (info == NULL || index >= 5U) return false;
    memset(info, 0, sizeof(*info));
    if (index < 2U) info->state = NEXUS_PNP_DEVICE_ACTIVE;
    else if (index == 2U) info->state = NEXUS_PNP_DEVICE_FAILED;
    else info->state = NEXUS_PNP_DEVICE_UNSUPPORTED;
    return true;
}

size_t netdev_count(void) { return 1U; }
bool netdev_get_info_at(size_t index, struct nexus_netdev_info *info) {
    if (index != 0U || info == NULL) return false;
    memset(info, 0, sizeof(*info));
    memcpy(info->name, "eth0", 5U);
    info->medium = NEXUS_NETDEV_MEDIUM_ETHERNET;
    info->link_up = true;
    return true;
}

static struct nexus_block_device mock_block = {
    .name = "NVME0",
    .flags = NEXUS_BLOCKDEV_READ_ONLY
};
size_t blockdev_count(void) { return 1U; }
const struct nexus_block_device *blockdev_at(size_t index) {
    return index == 0U ? &mock_block : NULL;
}
bool blockdev_is_mounted(const struct nexus_block_device *device) {
    return device == &mock_block;
}
size_t storage_controller_count(void) { return 1U; }

bool memory_get_allocator_stats(struct memory_allocator_stats *stats) {
    if (stats == NULL) return false;
    memset(stats, 0, sizeof(*stats));
    /* Keep the byte-size arithmetic 64-bit before assignment. */
    stats->total_bytes = 8192ULL * 1024ULL * 1024ULL;
    stats->free_bytes = 2048ULL * 1024ULL * 1024ULL;
    return true;
}
uint64_t timer_ticks(void) { return 2300U; }

size_t serial_log_read_recent(char *output, size_t capacity) {
    static const char sample[] = "BOOT OK\r\n";
    if (output == NULL || capacity == 0U) return 0U;
    const size_t length = sizeof(sample) - 1U;
    const size_t copy = length < capacity - 1U ? length : capacity - 1U;
    memcpy(output, sample, copy);
    output[copy] = '\0';
    return copy;
}
uint64_t serial_log_dropped(void) { return 0U; }

size_t nexus_trace_count(void) { return 2U; }
bool nexus_trace_get_recent(size_t reverse_index,
                            struct nexus_trace_event *event) {
    if (reverse_index != 0U || event == NULL) return false;
    memset(event, 0, sizeof(*event));
    event->type = NEXUS_TRACE_EVENT_SCHEDULER_SWITCH;
    event->timestamp_ticks = 99U;
    event->argument0 = 1U;
    event->argument1 = 2U;
    return true;
}
uint64_t nexus_trace_dropped(void) { return 0U; }

static bool has(const char *text, const char *part) {
    return strstr(text, part) != NULL;
}

int main(void) {
    _Static_assert(NEXUS_CPU_FEATURE_CLWB == (UINT64_C(1) << 32),
                   "64-bit CPU feature mask must preserve bit 32");
    _Static_assert(NEXUS_CPU_FEATURE_AVX512VNNI == (UINT64_C(1) << 42),
                   "64-bit CPU feature mask must preserve bit 42");
    _Static_assert((NEXUS_CPU_FEATURE_AVX512VNNI &
                    NEXUS_CPU_FEATURE_AVX512F) == 0U,
                   "CPU feature masks must remain distinct");
    char output[160];

    assert(nexus_diagnostics_command("cpu", output, sizeof(output)));
    assert(has(output, "GenuineIntel") && has(output, "AVX2-hw yes"));

    assert(nexus_diagnostics_command("hardware", output, sizeof(output)));
    assert(has(output, "GenuineIntel") && has(output, "PCI functions 12"));
    assert(has(output, "driver bindings 4"));

    assert(nexus_diagnostics_command("pci", output, sizeof(output)));
    assert(has(output, "PCI functions 12") && has(output, "stored 12/128"));

    assert(nexus_diagnostics_command("drivers", output, sizeof(output)));
    assert(has(output, "drivers 3") && has(output, "2/1/2"));

    assert(nexus_diagnostics_command("driverlist", output, sizeof(output)));
    assert(has(output, "registered drivers 3") && has(output, "nvme v1 loaded"));

    assert(nexus_diagnostics_command("drivererrors", output, sizeof(output)));
    assert(has(output, "failure records 1") && has(output, "ahci probe-failed"));

    assert(nexus_diagnostics_command("netdiag", output, sizeof(output)));
    assert(has(output, "eth0") && has(output, "link-up"));

    assert(nexus_diagnostics_command("storage", output, sizeof(output)));
    assert(has(output, "controllers/devices 1/1"));
    assert(has(output, "NVME0 read-only mounted"));

    assert(nexus_diagnostics_command("sysinfo", output, sizeof(output)));
    assert(has(output, "uptime 23s") && has(output, "2048/8192 MiB"));

    assert(nexus_diagnostics_command("logs", output, sizeof(output)));
    assert(has(output, "BOOT OK"));

    assert(nexus_diagnostics_command("trace", output, sizeof(output)));
    assert(has(output, "trace events 2/128") && has(output, "2/99"));

    assert(nexus_diagnostics_command("sdk", output, sizeof(output)));
    assert(has(output, "make -C sdk"));

    assert(!nexus_diagnostics_command("rm -rf /", output, sizeof(output)));
    assert(!nexus_diagnostics_command("unknown", output, sizeof(output)));

    char short_output[5];
    assert(nexus_diagnostics_command("cpu", short_output,
                                     sizeof(short_output)));
    assert(short_output[sizeof(short_output) - 1U] == '\0');
    return 0;
}
