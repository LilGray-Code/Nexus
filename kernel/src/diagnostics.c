#include "diagnostics.h"

#include "blockdev.h"
#include "cpu.h"
#include "driver_manager.h"
#include "memory.h"
#include "netdev.h"
#include "pci.h"
#include "serial.h"
#include "storage.h"
#include "timer.h"
#include "trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool text_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
    size_t i = 0U;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static void append_text(char *output, size_t capacity, size_t *length,
                        const char *text) {
    if (output == NULL || length == NULL || text == NULL || capacity == 0U)
        return;
    while (*text != '\0' && *length + 1U < capacity)
        output[(*length)++] = *text++;
    output[*length] = '\0';
}

static void append_u64(char *output, size_t capacity, size_t *length,
                       uint64_t value) {
    char digits[21];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U && *length + 1U < capacity)
        output[(*length)++] = digits[--count];
    output[*length] = '\0';
}

static void append_hex(char *output, size_t capacity, size_t *length,
                       uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    append_text(output, capacity, length, "0x");
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t nibble = (uint8_t)((value >> (unsigned int)shift) & 0x0FU);
        if (nibble != 0U || started || shift == 0) {
            char one[2] = { digits[nibble], '\0' };
            append_text(output, capacity, length, one);
            started = true;
        }
    }
}

static const char *bus_name(enum nexus_driver_bus bus) {
    switch (bus) {
        case NEXUS_DRIVER_BUS_PCI: return "PCI";
        case NEXUS_DRIVER_BUS_PLATFORM: return "platform";
        case NEXUS_DRIVER_BUS_USB: return "USB";
        default: return "unknown";
    }
}

static bool command_cpu(char *out, size_t cap, size_t *len) {
    const struct nexus_cpu_info *cpu = nexus_cpu_get_info();
    if (cpu == NULL || !cpu->cpuid_available) {
        append_text(out, cap, len, "CPU inventory unavailable");
        return true;
    }

    append_text(out, cap, len, cpu->vendor);
    append_text(out, cap, len, " F");
    append_u64(out, cap, len, cpu->family);
    append_text(out, cap, len, " M");
    append_u64(out, cap, len, cpu->model);
    append_text(out, cap, len, " S");
    append_u64(out, cap, len, cpu->stepping);
    append_text(out, cap, len, " cores/logical ");
    append_u64(out, cap, len, cpu->physical_cores_per_package);
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, cpu->logical_processors_per_package);
    append_text(out, cap, len, " AVX2-hw ");
    append_text(out, cap, len,
                (cpu->features & NEXUS_CPU_FEATURE_AVX2) != 0U ? "yes" : "no");
    return true;
}

static bool command_pci(char *out, size_t cap, size_t *len) {
    append_text(out, cap, len, "PCI functions ");
    append_u64(out, cap, len, pci_device_count());
    append_text(out, cap, len, " stored ");
    append_u64(out, cap, len, pci_stored_device_count());
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, NEXUS_PCI_MAX_STORED_DEVICES);
    return true;
}

static bool command_drivers(char *out, size_t cap, size_t *len) {
    size_t active = 0U, failed = 0U, unsupported = 0U, discovered = 0U;
    const size_t count = nexus_driver_manager_device_count();
    for (size_t i = 0U; i < count; ++i) {
        struct nexus_pnp_device_info info;
        if (!nexus_driver_manager_device_at(i, &info)) continue;
        switch (info.state) {
            case NEXUS_PNP_DEVICE_ACTIVE: ++active; break;
            case NEXUS_PNP_DEVICE_FAILED: ++failed; break;
            case NEXUS_PNP_DEVICE_UNSUPPORTED: ++unsupported; break;
            case NEXUS_PNP_DEVICE_DISCOVERED: ++discovered; break;
            default: break;
        }
    }

    append_text(out, cap, len, "drivers ");
    append_u64(out, cap, len, nexus_driver_manager_driver_count());
    append_text(out, cap, len, " devices ");
    append_u64(out, cap, len, count);
    append_text(out, cap, len, " active/failed/unsupported ");
    append_u64(out, cap, len, active);
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, failed);
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, unsupported);
    (void)discovered;
    return true;
}

static bool command_net(char *out, size_t cap, size_t *len) {
    const size_t count = netdev_count();
    append_text(out, cap, len, "network interfaces ");
    append_u64(out, cap, len, count);
    if (count == 0U) {
        append_text(out, cap, len, "; no registered adapter");
        return true;
    }

    struct nexus_netdev_info info;
    if (!netdev_get_info_at(0U, &info)) {
        append_text(out, cap, len, "; interface details unavailable");
        return true;
    }
    append_text(out, cap, len, "; ");
    append_text(out, cap, len, info.name);
    append_text(out, cap, len, " ");
    append_text(out, cap, len, info.link_up ? "link-up" : "link-down/unknown");
    append_text(out, cap, len,
        info.medium == NEXUS_NETDEV_MEDIUM_WIFI ? " Wi-Fi" : " Ethernet");
    return true;
}

static bool command_storage(char *out, size_t cap, size_t *len) {
    const size_t devices = blockdev_count();
    append_text(out, cap, len, "storage controllers/devices ");
    append_u64(out, cap, len, storage_controller_count());
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, devices);
    if (devices != 0U) {
        const struct nexus_block_device *device = blockdev_at(0U);
        if (device != NULL) {
            append_text(out, cap, len, "; ");
            append_text(out, cap, len, device->name);
            append_text(out, cap, len,
                (device->flags & NEXUS_BLOCKDEV_READ_ONLY) != 0U
                    ? " read-only" : " writable-capable");
            if (blockdev_is_mounted(device))
                append_text(out, cap, len, " mounted");
        }
    }
    return true;
}

static bool command_sysinfo(char *out, size_t cap, size_t *len) {
    struct memory_allocator_stats stats;
    append_text(out, cap, len, "uptime ");
    append_u64(out, cap, len, timer_ticks() / 100U);
    append_text(out, cap, len, "s; RAM ");
    if (memory_get_allocator_stats(&stats)) {
        append_u64(out, cap, len, stats.free_bytes / (1024U * 1024U));
        append_text(out, cap, len, "/");
        append_u64(out, cap, len, stats.total_bytes / (1024U * 1024U));
        append_text(out, cap, len, " MiB free/total; PCI ");
        append_u64(out, cap, len, pci_device_count());
    } else {
        append_text(out, cap, len, "stats unavailable");
    }
    return true;
}

static bool command_logs(char *out, size_t cap, size_t *len) {
    char recent[112];
    const size_t count = serial_log_read_recent(recent, sizeof(recent));
    if (count == 0U) {
        append_text(out, cap, len, "no captured serial logs");
        return true;
    }

    append_text(out, cap, len, "logs> ");
    for (size_t i = 0U; i < count; ++i) {
        const unsigned char ch = (unsigned char)recent[i];
        char printable[2];
        printable[0] = (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : ' ';
        printable[1] = '\0';
        append_text(out, cap, len, printable);
    }
    if (serial_log_dropped() != 0U) {
        append_text(out, cap, len, " (some log writes dropped)");
    }
    return true;
}

static bool command_trace(char *out, size_t cap, size_t *len) {
    const size_t count = nexus_trace_count();
    append_text(out, cap, len, "trace events ");
    append_u64(out, cap, len, count);
    append_text(out, cap, len, "/");
    append_u64(out, cap, len, NEXUS_TRACE_CAPACITY);

    struct nexus_trace_event event;
    if (nexus_trace_get_recent(0U, &event)) {
        append_text(out, cap, len, "; last type/tick ");
        append_u64(out, cap, len, event.type);
        append_text(out, cap, len, "/");
        append_u64(out, cap, len, event.timestamp_ticks);
        append_text(out, cap, len, " args ");
        append_hex(out, cap, len, event.argument0);
        append_text(out, cap, len, "/");
        append_hex(out, cap, len, event.argument1);
    }
    const uint64_t dropped = nexus_trace_dropped();
    if (dropped != 0U) {
        append_text(out, cap, len, "; dropped ");
        append_u64(out, cap, len, dropped);
    }
    return true;
}

bool nexus_diagnostics_command(const char *command,
                               char *output, size_t capacity) {
    if (command == NULL || output == NULL || capacity == 0U) return false;
    output[0] = '\0';
    size_t length = 0U;

    if (text_equal(command, "cpu")) return command_cpu(output, capacity, &length);
    if (text_equal(command, "pci") || text_equal(command, "hardware"))
        return command_pci(output, capacity, &length);
    if (text_equal(command, "drivers")) return command_drivers(output, capacity, &length);
    if (text_equal(command, "netdiag")) return command_net(output, capacity, &length);
    if (text_equal(command, "storage")) return command_storage(output, capacity, &length);
    if (text_equal(command, "sysinfo") || text_equal(command, "diag") ||
        text_equal(command, "kernel"))
        return command_sysinfo(output, capacity, &length);
    if (text_equal(command, "logs") || text_equal(command, "dmesg"))
        return command_logs(output, capacity, &length);
    if (text_equal(command, "trace")) return command_trace(output, capacity, &length);
    if (text_equal(command, "debug")) {
        append_text(output, capacity, &length,
                    "debug: serial log ring + scheduler trace; GDB stub is QEMU-only");
        return true;
    }
    if (text_equal(command, "sdk") || text_equal(command, "build")) {
        append_text(output, capacity, &length,
                    "SDK: make -C sdk; inspect with readelf/objdump; see DEVELOPER-TOOLS.md");
        return true;
    }
    return false;
}
