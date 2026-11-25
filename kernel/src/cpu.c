#include "cpu.h"

#include <stddef.h>
#include <stdint.h>

static struct nexus_cpu_info cpu_info;

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf));
    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static void append_register_bytes(char *destination, size_t offset,
                                  uint32_t value) {
    for (size_t i = 0U; i < 4U; ++i)
        destination[offset + i] = (char)(value >> (i * 8U));
}

static uint64_t read_xcr0(void) {
    uint32_t low, high;
    __asm__ volatile ("xgetbv"
        : "=a"(low), "=d"(high)
        : "c"(0U));
    return ((uint64_t)high << 32) | low;
}

static void set_feature(uint64_t feature, bool present) {
    if (present) cpu_info.features |= feature;
}

static bool discover_cpu_topology(uint32_t maximum_basic_leaf) {
    const uint32_t topology_leaf = maximum_basic_leaf >= 0x1FU ? 0x1FU
        : (maximum_basic_leaf >= 0x0BU ? 0x0BU : 0U);
    if (topology_leaf == 0U) return false;

    uint32_t threads_per_core = 0U;
    uint32_t logical_per_package = 0U;
    for (uint32_t subleaf = 0U; subleaf < 4U; ++subleaf) {
        uint32_t eax = 0U, ebx = 0U, ecx = 0U, edx = 0U;
        cpuid(topology_leaf, subleaf, &eax, &ebx, &ecx, &edx);
        (void)eax;
        (void)edx;
        const uint32_t level_type = (ecx >> 8) & 0xFFU;
        const uint32_t level_count = ebx & 0xFFFFU;
        if (level_count == 0U) break;
        if (level_type == 1U) threads_per_core = level_count;
        if (level_type == 2U) {
            logical_per_package = level_count;
            break;
        }
    }

    if (threads_per_core == 0U || logical_per_package == 0U ||
        logical_per_package < threads_per_core) return false;
    const uint32_t physical_cores = logical_per_package / threads_per_core;
    if (physical_cores == 0U || physical_cores > UINT16_MAX ||
        logical_per_package > UINT16_MAX) return false;

    cpu_info.logical_processors_per_package =
        (uint16_t)logical_per_package;
    cpu_info.physical_cores_per_package = (uint16_t)physical_cores;
    return true;
}

bool nexus_cpu_init(void) {
    cpu_info = (struct nexus_cpu_info){0};

    uint32_t maximum = 0U, ebx = 0U, ecx = 0U, edx = 0U;
    cpuid(0U, 0U, &maximum, &ebx, &ecx, &edx);
    if (maximum == 0U) return false;

    cpu_info.cpuid_available = true;
    cpu_info.max_basic_leaf = maximum;
    append_register_bytes(cpu_info.vendor, 0U, ebx);
    append_register_bytes(cpu_info.vendor, 4U, edx);
    append_register_bytes(cpu_info.vendor, 8U, ecx);
    cpu_info.vendor[12] = '\0';

    uint32_t eax = 0U;
    cpuid(1U, 0U, &eax, &ebx, &ecx, &edx);
    const uint32_t base_family = (eax >> 8) & 0x0FU;
    const uint32_t base_model = (eax >> 4) & 0x0FU;
    const uint32_t extended_family = (eax >> 20) & 0xFFU;
    const uint32_t extended_model = (eax >> 16) & 0x0FU;
    cpu_info.family = base_family == 0x0FU
        ? base_family + extended_family : base_family;
    cpu_info.model = (base_family == 0x06U || base_family == 0x0FU)
        ? base_model | (extended_model << 4) : base_model;
    cpu_info.stepping = eax & 0x0FU;
    cpu_info.logical_processors_per_package =
        (uint16_t)((ebx >> 16) & 0xFFU);
    if (cpu_info.logical_processors_per_package == 0U)
        cpu_info.logical_processors_per_package = 1U;

    set_feature(NEXUS_CPU_FEATURE_FPU, (edx & (1U << 0)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_TSC, (edx & (1U << 4)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_APIC, (edx & (1U << 9)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSE, (edx & (1U << 25)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSE2, (edx & (1U << 26)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSE3, (ecx & (1U << 0)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_PCLMUL, (ecx & (1U << 1)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSSE3, (ecx & (1U << 9)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSE41, (ecx & (1U << 19)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_SSE42, (ecx & (1U << 20)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_AES, (ecx & (1U << 25)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_XSAVE, (ecx & (1U << 26)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_OSXSAVE, (ecx & (1U << 27)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_RDRAND, (ecx & (1U << 30)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_HYPERVISOR, (ecx & (1U << 31)) != 0U);

    uint64_t xcr0 = 0U;
    const uint64_t xsave_state_features =
        NEXUS_CPU_FEATURE_XSAVE | NEXUS_CPU_FEATURE_OSXSAVE;
    const bool os_owns_extended_state =
        (cpu_info.features & xsave_state_features) == xsave_state_features;
    if (os_owns_extended_state) xcr0 = read_xcr0();
    const bool avx_state_enabled =
        os_owns_extended_state && (xcr0 & 0x6U) == 0x6U;
    set_feature(NEXUS_CPU_FEATURE_AVX,
        avx_state_enabled && (ecx & (1U << 28)) != 0U);
    set_feature(NEXUS_CPU_FEATURE_FMA,
        (cpu_info.features & NEXUS_CPU_FEATURE_AVX) != 0U &&
        (ecx & (1U << 12)) != 0U);

    if (maximum >= 7U) {
        uint32_t max7_eax = 0U, max7_ebx = 0U, max7_ecx = 0U, max7_edx = 0U;
        cpuid(7U, 0U, &max7_eax, &max7_ebx, &max7_ecx, &max7_edx);
        (void)max7_eax;
        (void)max7_ecx;
        (void)max7_edx;
        set_feature(NEXUS_CPU_FEATURE_AVX2,
            (cpu_info.features & NEXUS_CPU_FEATURE_AVX) != 0U &&
            (max7_ebx & (1U << 5)) != 0U);
        set_feature(NEXUS_CPU_FEATURE_BMI1, (max7_ebx & (1U << 3)) != 0U);
        set_feature(NEXUS_CPU_FEATURE_BMI2, (max7_ebx & (1U << 8)) != 0U);
        set_feature(NEXUS_CPU_FEATURE_RDSEED, (max7_ebx & (1U << 18)) != 0U);
        set_feature(NEXUS_CPU_FEATURE_SHA, (max7_ebx & (1U << 29)) != 0U);
    }

    uint32_t max_extended = 0U;
    cpuid(0x80000000U, 0U, &max_extended, NULL, NULL, NULL);
    cpu_info.max_extended_leaf = max_extended;
    if (max_extended >= 0x80000001U) {
        uint32_t ext_edx = 0U;
        cpuid(0x80000001U, 0U, NULL, NULL, NULL, &ext_edx);
        set_feature(NEXUS_CPU_FEATURE_NX, (ext_edx & (1U << 20)) != 0U);
        set_feature(NEXUS_CPU_FEATURE_LONG_MODE,
                    (ext_edx & (1U << 29)) != 0U);
    }
    if (max_extended >= 0x80000007U) {
        uint32_t invariant_edx = 0U;
        cpuid(0x80000007U, 0U, NULL, NULL, NULL, &invariant_edx);
        set_feature(NEXUS_CPU_FEATURE_INVAR_TSC,
                    (invariant_edx & (1U << 8)) != 0U);
    }
    if (max_extended >= 0x80000008U) {
        uint32_t address_eax = 0U, address_ebx = 0U, address_ecx = 0U;
        cpuid(0x80000008U, 0U, &address_eax, &address_ebx, &address_ecx, NULL);
        (void)address_eax;
        (void)address_ebx;
        const uint16_t cores = (uint16_t)((address_ecx & 0xFFU) + 1U);
        if (cores != 0U) cpu_info.physical_cores_per_package = cores;
    }
    if (cpu_info.physical_cores_per_package == 0U)
        cpu_info.physical_cores_per_package = 1U;
    (void)discover_cpu_topology(maximum);

    if (max_extended >= 0x80000004U) {
        uint32_t words[12];
        for (uint32_t leaf = 0U; leaf < 3U; ++leaf) {
            uint32_t a, b, c, d;
            cpuid(0x80000002U + leaf, 0U, &a, &b, &c, &d);
            words[leaf * 4U] = a;
            words[leaf * 4U + 1U] = b;
            words[leaf * 4U + 2U] = c;
            words[leaf * 4U + 3U] = d;
        }
        for (size_t i = 0U; i < 12U; ++i)
            append_register_bytes(cpu_info.brand, i * 4U, words[i]);
        cpu_info.brand[48] = '\0';
    }
    if (cpu_info.brand[0] == '\0') {
        static const char fallback[] = "Unknown x86-64 processor";
        for (size_t i = 0U; i < sizeof(fallback); ++i)
            cpu_info.brand[i] = fallback[i];
    } else {
        size_t first = 0U;
        while (cpu_info.brand[first] == ' ' && first < 48U) ++first;
        if (first != 0U) {
            size_t out = 0U;
            while (first < 48U && cpu_info.brand[first] != '\0')
                cpu_info.brand[out++] = cpu_info.brand[first++];
            cpu_info.brand[out] = '\0';
        }
    }
    return true;
}

const struct nexus_cpu_info *nexus_cpu_get_info(void) {
    return &cpu_info;
}

bool nexus_cpu_has_feature(uint64_t feature) {
    return feature != 0U && (cpu_info.features & feature) == feature;
}

const char *nexus_cpu_feature_level_label(void) {
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2)) return "AVX2 AVAILABLE";
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX)) return "AVX AVAILABLE";
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE42)) return "SSE4.2 AVAILABLE";
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE2)) return "SSE2 BASELINE";
    return "SCALAR BASELINE";
}
