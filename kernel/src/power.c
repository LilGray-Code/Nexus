#include "power.h"

#include "acpi.h"
#include "blockdev.h"
#include "cpu.h"
#include "smp.h"

#include <stddef.h>
#include <stdint.h>

#define IA32_PM_ENABLE 0x770U
#define IA32_HWP_REQUEST 0x774U
#define IA32_THERM_STATUS 0x19CU
#define IA32_TEMPERATURE_TARGET 0x1A2U
#define HWP_ENABLE (1ULL << 0)
#define HWP_EPP_MASK (0xFFULL << 24)
#define THERMAL_VALID (1ULL << 31)

struct hwp_cpu_state {
    uint64_t pm_enable;
    uint64_t request;
    uint64_t previous_pm_enable;
    uint64_t previous_request;
    uint8_t epp;
    bool supported;
    bool inspected;
    bool apply_succeeded;
};

static struct hwp_cpu_state hwp_cpus[NEXUS_SMP_MAX_CPUS];
static bool power_initialized;
static bool hwp_profile_available;
static enum nexus_power_profile active_profile = NEXUS_POWER_PROFILE_UNKNOWN;

static uint64_t read_msr(uint32_t msr) {
    uint32_t low = 0U, high = 0U;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : :
        "c"(msr), "a"((uint32_t)value),
        "d"((uint32_t)(value >> 32)) : "memory");
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    uint32_t a = 0U, b = 0U, c = 0U, d = 0U;
    __asm__ volatile ("cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0U));
    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static bool local_hwp_epp_supported(void) {
    uint32_t max_leaf = 0U;
    uint32_t vendor_ebx = 0U, vendor_ecx = 0U, vendor_edx = 0U;
    uint32_t leaf1_edx = 0U;
    cpuid(0U, &max_leaf, &vendor_ebx, &vendor_ecx, &vendor_edx);
    if (max_leaf < 6U ||
        vendor_ebx != 0x756E6547U ||
        vendor_edx != 0x49656E69U ||
        vendor_ecx != 0x6C65746EU) return false;
    cpuid(1U, NULL, NULL, NULL, &leaf1_edx);
    if ((leaf1_edx & (1U << 5)) == 0U) return false; /* MSR */
    uint32_t leaf6_eax = 0U;
    cpuid(6U, &leaf6_eax, NULL, NULL, NULL);
    return (leaf6_eax & (1U << 7)) != 0U &&
           (leaf6_eax & (1U << 10)) != 0U;
}

uint8_t nexus_power_profile_epp(enum nexus_power_profile profile) {
    switch (profile) {
        case NEXUS_POWER_PROFILE_POWER_SAVER: return 0xFFU;
        case NEXUS_POWER_PROFILE_BALANCED: return 0x80U;
        case NEXUS_POWER_PROFILE_PERFORMANCE: return 0x00U;
        default: return 0xFFU;
    }
}

bool nexus_power_decode_intel_temperature(uint64_t thermal_status,
                                          uint64_t temperature_target,
                                          uint8_t *temperature_celsius) {
    if (temperature_celsius == NULL ||
        (thermal_status & THERMAL_VALID) == 0U) return false;
    const uint8_t tj_max = (uint8_t)((temperature_target >> 16) & 0xFFU);
    const uint8_t distance =
        (uint8_t)((thermal_status >> 16) & 0x7FU);
    if (tj_max < 50U || tj_max > 125U || distance > tj_max)
        return false;
    *temperature_celsius = (uint8_t)(tj_max - distance);
    return true;
}

static enum nexus_power_profile profile_from_epp(uint8_t epp) {
    if (epp == 0xFFU) return NEXUS_POWER_PROFILE_POWER_SAVER;
    if (epp == 0x80U) return NEXUS_POWER_PROFILE_BALANCED;
    if (epp == 0x00U) return NEXUS_POWER_PROFILE_PERFORMANCE;
    return NEXUS_POWER_PROFILE_UNKNOWN;
}

static void inspect_hwp_cpu(uint32_t cpu_slot, uint32_t item_index,
                            void *context) {
    (void)item_index;
    struct hwp_cpu_state *states = (struct hwp_cpu_state *)context;
    if (states == NULL || cpu_slot >= NEXUS_SMP_MAX_CPUS) return;
    struct hwp_cpu_state *state = &states[cpu_slot];
    state->inspected = true;
    state->supported = local_hwp_epp_supported();
    if (!state->supported) return;
    state->pm_enable = read_msr(IA32_PM_ENABLE);
    state->request = read_msr(IA32_HWP_REQUEST);
    state->epp = (uint8_t)((state->request >> 24) & 0xFFU);
}

struct hwp_apply_context {
    uint8_t epp;
    bool rollback;
    volatile uint32_t failed;
};

static void apply_hwp_cpu(uint32_t cpu_slot, uint32_t item_index,
                          void *context) {
    (void)item_index;
    struct hwp_apply_context *apply =
        (struct hwp_apply_context *)context;
    if (apply == NULL || cpu_slot >= NEXUS_SMP_MAX_CPUS ||
        !hwp_cpus[cpu_slot].supported) {
        if (apply != NULL)
            __atomic_store_n(&apply->failed, 1U, __ATOMIC_RELEASE);
        return;
    }

    struct hwp_cpu_state *state = &hwp_cpus[cpu_slot];
    if (apply->rollback) {
        if (!state->apply_succeeded) return;
        write_msr(IA32_HWP_REQUEST, state->previous_request);
        write_msr(IA32_PM_ENABLE, state->previous_pm_enable);
        if (read_msr(IA32_HWP_REQUEST) != state->previous_request ||
            read_msr(IA32_PM_ENABLE) != state->previous_pm_enable) {
            __atomic_store_n(&apply->failed, 1U, __ATOMIC_RELEASE);
        } else {
            state->apply_succeeded = false;
        }
        return;
    }

    const uint64_t old_pm_enable = read_msr(IA32_PM_ENABLE);
    const uint64_t old_request = read_msr(IA32_HWP_REQUEST);
    state->previous_pm_enable = old_pm_enable;
    state->previous_request = old_request;

    const uint64_t pm_enable = old_pm_enable | HWP_ENABLE;
    if (pm_enable != old_pm_enable) write_msr(IA32_PM_ENABLE, pm_enable);
    const uint64_t request = (old_request & ~HWP_EPP_MASK) |
                             ((uint64_t)apply->epp << 24);
    write_msr(IA32_HWP_REQUEST, request);

    const uint64_t verify_pm = read_msr(IA32_PM_ENABLE);
    const uint64_t verify_request = read_msr(IA32_HWP_REQUEST);
    if ((verify_pm & HWP_ENABLE) == 0U ||
        ((verify_request >> 24) & 0xFFU) != apply->epp) {
        write_msr(IA32_HWP_REQUEST, old_request);
        write_msr(IA32_PM_ENABLE, old_pm_enable);
        __atomic_store_n(&apply->failed, 1U, __ATOMIC_RELEASE);
        return;
    }
    state->apply_succeeded = true;
    state->pm_enable = verify_pm;
    state->request = verify_request;
    state->epp = apply->epp;
}

bool nexus_power_init(void) {
    if (power_initialized) return true;
    for (size_t i = 0U; i < NEXUS_SMP_MAX_CPUS; ++i)
        hwp_cpus[i] = (struct hwp_cpu_state){0};

    const uint32_t online = nexus_smp_online_cpu_count();
    if (online == 0U || online > NEXUS_SMP_MAX_CPUS) return false;
    if (!nexus_smp_call_each_cpu(inspect_hwp_cpu, hwp_cpus)) {
        power_initialized = true;
        hwp_profile_available = false;
        return true;
    }

    bool all_supported = true;
    bool all_hwp_enabled = true;
    bool same_epp = true;
    bool first_epp = true;
    uint8_t common_epp = 0U;
    uint32_t inspected_online = 0U;
    for (uint32_t i = 0U; i < NEXUS_SMP_MAX_CPUS; ++i) {
        struct nexus_smp_cpu_info topology;
        if (!nexus_smp_cpu_info_at(i, &topology)) break;
        if (!topology.online) continue;
        ++inspected_online;
        if (!hwp_cpus[i].inspected || !hwp_cpus[i].supported) {
            all_supported = false;
            continue;
        }
        if ((hwp_cpus[i].pm_enable & HWP_ENABLE) == 0U) {
            all_hwp_enabled = false;
            continue;
        }
        if (first_epp) {
            common_epp = hwp_cpus[i].epp;
            first_epp = false;
        } else if (common_epp != hwp_cpus[i].epp) {
            same_epp = false;
        }
    }
    if (inspected_online != online) all_supported = false;
    hwp_profile_available = all_supported;
    active_profile = (all_supported && all_hwp_enabled &&
                      same_epp && !first_epp)
        ? profile_from_epp(common_epp) : NEXUS_POWER_PROFILE_UNKNOWN;
    power_initialized = true;
    return true;
}

bool nexus_power_set_profile(enum nexus_power_profile profile) {
    if (profile != NEXUS_POWER_PROFILE_POWER_SAVER &&
        profile != NEXUS_POWER_PROFILE_BALANCED &&
        profile != NEXUS_POWER_PROFILE_PERFORMANCE) return false;
    if (!nexus_power_init() || !hwp_profile_available) return false;

    struct hwp_apply_context apply = {
        .epp = nexus_power_profile_epp(profile),
        .rollback = false,
        .failed = 0U
    };
    for (size_t i = 0U; i < NEXUS_SMP_MAX_CPUS; ++i)
        hwp_cpus[i].apply_succeeded = false;
    const bool applied = nexus_smp_call_each_cpu(apply_hwp_cpu, &apply);
    if (!applied ||
        __atomic_load_n(&apply.failed, __ATOMIC_ACQUIRE) != 0U) {
        struct hwp_apply_context rollback = {
            .epp = 0U, .rollback = true, .failed = 0U
        };
        (void)nexus_smp_call_each_cpu(apply_hwp_cpu, &rollback);
        return false;
    }
    active_profile = profile;
    return true;
}

struct thermal_scan_result {
    volatile uint32_t valid_core_count;
    volatile uint32_t hottest_temperature;
};

static bool read_local_intel_core_temperature(uint8_t *temperature) {
    uint32_t vendor_ebx = 0U, vendor_ecx = 0U, vendor_edx = 0U;
    uint32_t maximum_leaf = 0U;
    cpuid(0U, &maximum_leaf, &vendor_ebx, &vendor_ecx, &vendor_edx);
    if (maximum_leaf < 6U ||
        vendor_ebx != 0x756E6547U ||
        vendor_edx != 0x49656E69U ||
        vendor_ecx != 0x6C65746EU) return false;

    uint32_t feature_edx = 0U;
    cpuid(1U, NULL, NULL, NULL, &feature_edx);
    if ((feature_edx & (1U << 5)) == 0U) return false;
    uint32_t power_eax = 0U;
    cpuid(6U, &power_eax, NULL, NULL, NULL);
    if ((power_eax & 1U) == 0U) return false;

    return nexus_power_decode_intel_temperature(
        read_msr(IA32_THERM_STATUS),
        read_msr(IA32_TEMPERATURE_TARGET), temperature);
}

static void scan_cpu_temperature(uint32_t cpu_slot, uint32_t item_index,
                                 void *context) {
    (void)cpu_slot;
    (void)item_index;
    struct thermal_scan_result *result =
        (struct thermal_scan_result *)context;
    if (result == NULL) return;
    uint8_t temperature = 0U;
    if (!read_local_intel_core_temperature(&temperature)) return;
    __atomic_fetch_add(&result->valid_core_count, 1U, __ATOMIC_RELAXED);
    uint32_t previous = __atomic_load_n(
        &result->hottest_temperature, __ATOMIC_RELAXED);
    while (temperature > previous &&
           !__atomic_compare_exchange_n(&result->hottest_temperature,
               &previous, temperature, true, __ATOMIC_RELAXED,
               __ATOMIC_RELAXED)) {
        /* previous is refreshed by compare_exchange on failure. */
    }
}

bool nexus_power_get_snapshot(struct nexus_power_snapshot *snapshot) {
    if (snapshot == NULL) return false;
    if (!power_initialized && !nexus_power_init()) return false;
    *snapshot = (struct nexus_power_snapshot){0};

    const struct nexus_acpi_power_info *acpi = acpi_get_power_info();
    if (acpi != NULL) {
        snapshot->acpi_shutdown_available = acpi->shutdown_supported;
        snapshot->acpi_restart_available = acpi->reset_supported;
    }
    /* Sleep and hibernate remain false until a tested firmware return path
       restores every CPU, interrupt, DMA and device state. */
    snapshot->suspend_available = false;
    snapshot->hibernate_available = false;

    /* The AML namespace evaluator and EC transaction driver do not yet exist.
       Never infer battery, charging, AC adapter or lid state from absence. */
    snapshot->battery_available = false;
    snapshot->battery_percentage_available = false;
    snapshot->charging_status_available = false;
    snapshot->external_power_available = false;
    snapshot->fan_control_available = false;
    snapshot->lid_detection_available = false;
    snapshot->native_backlight_available = false;

    struct thermal_scan_result thermal = {0};
    if (nexus_smp_call_each_cpu(scan_cpu_temperature, &thermal)) {
        const uint32_t valid_cores = __atomic_load_n(
            &thermal.valid_core_count, __ATOMIC_ACQUIRE);
        if (valid_cores != 0U) {
            snapshot->cpu_temperature_available = true;
            snapshot->cpu_temperature_celsius = (uint8_t)(
                __atomic_load_n(&thermal.hottest_temperature,
                                __ATOMIC_ACQUIRE));
        }
    }
    snapshot->cpu_hwp_profile_available = hwp_profile_available;
    snapshot->current_profile = active_profile;
    return true;
}

static uint16_t io_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static void io_out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static bool flush_writable_devices(void) {
    for (size_t i = 0U; i < blockdev_count(); ++i) {
        const struct nexus_block_device *device = blockdev_at(i);
        if (device == NULL || device->flush == NULL) continue;
        if (!blockdev_flush(device)) return false;
    }
    return true;
}

bool nexus_power_shutdown(void) {
    const struct nexus_acpi_power_info *acpi = acpi_get_power_info();
    if (acpi == NULL || !acpi->shutdown_supported ||
        acpi->pm1a_control_port == 0U) return false;
    if (!flush_writable_devices()) return false;

    __asm__ volatile ("cli" : : : "memory");
    const uint16_t type_a = (uint16_t)((acpi->s5_type_a & 7U) << 10);
    if (acpi->pm1b_control_port != 0U) {
        uint16_t control_b = io_in16(acpi->pm1b_control_port);
        control_b = (uint16_t)((control_b & ~((7U << 10) | (1U << 13))) |
                               ((acpi->s5_type_b & 7U) << 10) | (1U << 13));
        io_out16(acpi->pm1b_control_port, control_b);
    }
    uint16_t control_a = io_in16(acpi->pm1a_control_port);
    control_a = (uint16_t)((control_a & ~((7U << 10) | (1U << 13))) |
                           type_a | (1U << 13));
    io_out16(acpi->pm1a_control_port, control_a);
    for (;;) __asm__ volatile ("hlt" : : : "memory");
}

bool nexus_power_restart(void) {
    const struct nexus_acpi_power_info *acpi = acpi_get_power_info();
    if (acpi == NULL || !acpi->reset_supported ||
        acpi->reset_port == 0U) return false;
    if (!flush_writable_devices()) return false;

    __asm__ volatile ("cli" : : : "memory");
    io_out8(acpi->reset_port, acpi->reset_value);
    for (uint32_t spin = 0U; spin < 1000000U; ++spin)
        __asm__ volatile ("pause" : : : "memory");
    /* Legacy 8042 reset is a fallback if the firmware reset register failed. */
    io_out8(0x64U, 0xFEU);
    for (;;) __asm__ volatile ("hlt" : : : "memory");
}
