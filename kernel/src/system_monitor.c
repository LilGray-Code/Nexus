#include "system_monitor.h"

#include "blockdev.h"
#include "gpu.h"
#include "memory.h"
#include "netdev.h"
#include "nxfs.h"
#include "scheduler.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

static struct nexus_system_monitor_snapshot current_snapshot;
static bool monitor_initialized;
static uint64_t previous_busy_ticks;
static uint64_t previous_idle_ticks;
static uint64_t previous_sample_tick;
static uint64_t previous_network_tx;
static uint64_t previous_network_rx;
static uint64_t previous_disk_read;
static uint64_t previous_disk_write;

static uint64_t saturating_rate(uint64_t delta, uint64_t ticks,
                                uint32_t ticks_per_second) {
    if (ticks == 0U || ticks_per_second == 0U) return 0U;
    if (delta > UINT64_MAX / ticks_per_second) return UINT64_MAX;
    return (delta * ticks_per_second) / ticks;
}

static uint8_t percent_u64(uint64_t part, uint64_t whole) {
    if (whole == 0U || part == 0U) return 0U;
    if (part >= whole) return 100U;
    const uint64_t value = (part * 100U) / whole;
    return (uint8_t)(value > 100U ? 100U : value);
}

static void copy_name(char *destination, size_t capacity,
                      const char *source) {
    if (destination == NULL || capacity == 0U) return;
    size_t i = 0U;
    if (source != NULL) {
        while (i + 1U < capacity && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    while (++i < capacity) destination[i] = '\0';
}

void nexus_system_monitor_init(void) {
    current_snapshot = (struct nexus_system_monitor_snapshot){0};
    monitor_initialized = false;
    previous_busy_ticks = 0U;
    previous_idle_ticks = 0U;
    previous_sample_tick = 0U;
    previous_network_tx = 0U;
    previous_network_rx = 0U;
    previous_disk_read = 0U;
    previous_disk_write = 0U;
}

bool nexus_system_monitor_sample(void) {
    struct nexus_system_monitor_snapshot next = {0};
    const uint64_t now = timer_ticks();
    next.tick = now;
    next.uptime_ticks = now;

    uint64_t busy = 0U, idle = 0U;
    scheduler_get_cpu_ticks(&busy, &idle);
    const uint64_t total_ticks = busy + idle;
    if (monitor_initialized && busy >= previous_busy_ticks &&
        idle >= previous_idle_ticks) {
        const uint64_t busy_delta = busy - previous_busy_ticks;
        const uint64_t idle_delta = idle - previous_idle_ticks;
        const uint64_t elapsed = busy_delta + idle_delta;
        if (elapsed != 0U) {
            next.cpu_usage_available = true;
            next.cpu_usage_percent = percent_u64(busy_delta, elapsed);
        }
    }
    previous_busy_ticks = busy;
    previous_idle_ticks = idle;
    (void)total_ticks;

    next.online_core_count = nexus_smp_online_cpu_count();
    if (next.online_core_count > NEXUS_SMP_MAX_CPUS)
        next.online_core_count = NEXUS_SMP_MAX_CPUS;
    for (uint32_t slot = 0U; slot < NEXUS_SMP_MAX_CPUS; ++slot) {
        struct nexus_smp_cpu_info topology;
        if (!nexus_smp_cpu_info_at(slot, &topology)) continue;
        next.cores[slot].apic_id = topology.apic_id;
        next.cores[slot].online = topology.online;
        /*
         * PIT accounting is currently BSP-only. APs are online for kernel
         * tasks but do not have per-CPU timer accounting or user scheduling.
         */
        if (slot == 0U && next.cpu_usage_available) {
            next.cores[slot].usage_available = true;
            next.cores[slot].usage_percent = next.cpu_usage_percent;
        }
    }

    struct memory_allocator_stats memory;
    if (memory_get_allocator_stats(&memory)) {
        next.ram_available = true;
        next.ram_total_bytes = memory.total_bytes;
        next.ram_free_bytes = memory.free_bytes;
        next.ram_used_bytes = memory.used_bytes;
    }

    const struct nexus_gpu_info *gpu = gpu_primary_info();
    if (gpu != NULL && gpu->present) {
        next.gpu_detected = true;
        copy_name(next.gpu_name, sizeof(next.gpu_name),
                  gpu->name != NULL ? gpu->name : gpu_primary_name());
    }
    /* Discovery does not expose live utilization counters yet. */
    next.gpu_utilization_available = false;

    next.storage_available = nxfs_get_storage_usage(
        &next.storage_capacity_bytes, &next.storage_file_data_bytes,
        &next.storage_file_count);

    struct nexus_netdev_activity network = {0};
    netdev_get_activity(&network);
    next.network_available = netdev_count() != 0U;
    next.network_transmitted_bytes = network.transmitted_bytes;
    next.network_received_bytes = network.received_bytes;

    struct nexus_blockdev_activity disk = {0};
    blockdev_get_activity(&disk);
    next.disk_io_available = blockdev_count() != 0U;
    next.disk_read_bytes = disk.read_bytes;
    next.disk_written_bytes = disk.written_bytes;
    next.disk_read_operations = disk.read_operations;
    next.disk_write_operations = disk.write_operations;

    if (monitor_initialized && now > previous_sample_tick &&
        network.transmitted_bytes >= previous_network_tx &&
        network.received_bytes >= previous_network_rx &&
        disk.read_bytes >= previous_disk_read &&
        disk.written_bytes >= previous_disk_write) {
        const uint64_t interval = now - previous_sample_tick;
        next.network_rate_available = next.network_available;
        next.network_transmit_bytes_per_second = saturating_rate(
            network.transmitted_bytes - previous_network_tx, interval, 100U);
        next.network_receive_bytes_per_second = saturating_rate(
            network.received_bytes - previous_network_rx, interval, 100U);
        next.disk_rate_available = next.disk_io_available;
        next.disk_read_bytes_per_second = saturating_rate(
            disk.read_bytes - previous_disk_read, interval, 100U);
        next.disk_write_bytes_per_second = saturating_rate(
            disk.written_bytes - previous_disk_write, interval, 100U);
    }
    previous_sample_tick = now;
    previous_network_tx = network.transmitted_bytes;
    previous_network_rx = network.received_bytes;
    previous_disk_read = disk.read_bytes;
    previous_disk_write = disk.written_bytes;

    for (size_t i = 0U; i < NEXUS_MONITOR_MAX_PROCESSES; ++i) {
        struct nexus_process_info info;
        if (!process_get_info(i, &info)) continue;
        next.processes[next.process_count++].process = info;
    }
    for (size_t i = 0U; i < NEXUS_MONITOR_MAX_SERVICES; ++i) {
        struct nexus_service_info info;
        if (!nexus_service_manager_get_info(i, &info)) continue;
        next.services[next.service_count++].service = info;
    }

    /*
     * The foreground user process runs to completion on the calling context.
     * The kernel cannot safely time out or kill it while it executes.
     */
    next.application_responsiveness_available = false;

    current_snapshot = next;
    monitor_initialized = true;
    return true;
}

bool nexus_system_monitor_get_snapshot(
    struct nexus_system_monitor_snapshot *snapshot) {
    if (snapshot == NULL || !monitor_initialized) return false;
    *snapshot = current_snapshot;
    return true;
}
