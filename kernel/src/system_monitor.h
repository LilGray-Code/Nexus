#ifndef NEXUS_SYSTEM_MONITOR_H
#define NEXUS_SYSTEM_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "process.h"
#include "service_manager.h"
#include "smp.h"

#define NEXUS_MONITOR_MAX_PROCESSES NEXUS_PROCESS_MAX
#define NEXUS_MONITOR_MAX_SERVICES NEXUS_SERVICE_MAX_SERVICES

struct nexus_monitor_core {
    uint32_t apic_id;
    bool online;
    bool usage_available;
    uint8_t usage_percent;
};

struct nexus_monitor_process {
    struct nexus_process_info process;
};

struct nexus_monitor_service {
    struct nexus_service_info service;
};

struct nexus_system_monitor_snapshot {
    uint64_t tick;
    uint64_t uptime_ticks;

    bool cpu_usage_available;
    uint8_t cpu_usage_percent;
    uint32_t online_core_count;
    struct nexus_monitor_core cores[NEXUS_SMP_MAX_CPUS];

    uint64_t ram_total_bytes;
    uint64_t ram_free_bytes;
    uint64_t ram_used_bytes;
    bool ram_available;

    bool gpu_detected;
    bool gpu_utilization_available;
    char gpu_name[48];

    bool storage_available;
    uint64_t storage_capacity_bytes;
    uint64_t storage_file_data_bytes;
    uint64_t storage_file_count;

    bool network_available;
    uint64_t network_transmitted_bytes;
    uint64_t network_received_bytes;
    uint64_t network_transmit_bytes_per_second;
    uint64_t network_receive_bytes_per_second;
    bool network_rate_available;

    bool disk_io_available;
    uint64_t disk_read_bytes;
    uint64_t disk_written_bytes;
    uint64_t disk_read_bytes_per_second;
    uint64_t disk_write_bytes_per_second;
    uint64_t disk_read_operations;
    uint64_t disk_write_operations;
    bool disk_rate_available;

    size_t process_count;
    struct nexus_monitor_process processes[NEXUS_MONITOR_MAX_PROCESSES];
    size_t service_count;
    struct nexus_monitor_service services[NEXUS_MONITOR_MAX_SERVICES];

    /* The current CPL3 runtime is synchronous; no external app heartbeat exists. */
    bool application_responsiveness_available;
};

void nexus_system_monitor_init(void);
bool nexus_system_monitor_sample(void);
bool nexus_system_monitor_get_snapshot(
    struct nexus_system_monitor_snapshot *snapshot);

#endif
