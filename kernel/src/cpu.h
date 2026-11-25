#ifndef NEXUS_CPU_H
#define NEXUS_CPU_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_cpu_feature {
    NEXUS_CPU_FEATURE_FPU       = 1ULL << 0,
    NEXUS_CPU_FEATURE_TSC       = 1ULL << 1,
    NEXUS_CPU_FEATURE_APIC      = 1ULL << 2,
    NEXUS_CPU_FEATURE_SSE       = 1ULL << 3,
    NEXUS_CPU_FEATURE_SSE2      = 1ULL << 4,
    NEXUS_CPU_FEATURE_SSE3      = 1ULL << 5,
    NEXUS_CPU_FEATURE_SSSE3     = 1ULL << 6,
    NEXUS_CPU_FEATURE_SSE41     = 1ULL << 7,
    NEXUS_CPU_FEATURE_SSE42     = 1ULL << 8,
    NEXUS_CPU_FEATURE_AES       = 1ULL << 9,
    NEXUS_CPU_FEATURE_PCLMUL    = 1ULL << 10,
    NEXUS_CPU_FEATURE_XSAVE     = 1ULL << 11,
    NEXUS_CPU_FEATURE_OSXSAVE   = 1ULL << 12,
    NEXUS_CPU_FEATURE_AVX       = 1ULL << 13,
    NEXUS_CPU_FEATURE_AVX2      = 1ULL << 14,
    NEXUS_CPU_FEATURE_FMA       = 1ULL << 15,
    NEXUS_CPU_FEATURE_BMI1      = 1ULL << 16,
    NEXUS_CPU_FEATURE_BMI2      = 1ULL << 17,
    NEXUS_CPU_FEATURE_RDRAND    = 1ULL << 18,
    NEXUS_CPU_FEATURE_RDSEED    = 1ULL << 19,
    NEXUS_CPU_FEATURE_SHA       = 1ULL << 20,
    NEXUS_CPU_FEATURE_NX        = 1ULL << 21,
    NEXUS_CPU_FEATURE_LONG_MODE = 1ULL << 22,
    NEXUS_CPU_FEATURE_INVAR_TSC = 1ULL << 23,
    NEXUS_CPU_FEATURE_HYPERVISOR= 1ULL << 24,
    NEXUS_CPU_FEATURE_PCID      = 1ULL << 25,
    NEXUS_CPU_FEATURE_FSGSBASE  = 1ULL << 26,
    NEXUS_CPU_FEATURE_UMIP      = 1ULL << 27,
    NEXUS_CPU_FEATURE_SMEP      = 1ULL << 28,
    NEXUS_CPU_FEATURE_INVPCID   = 1ULL << 29,
    NEXUS_CPU_FEATURE_SMAP      = 1ULL << 30,
    NEXUS_CPU_FEATURE_CLFLUSHOPT= 1ULL << 31,
    NEXUS_CPU_FEATURE_CLWB      = 1ULL << 32,
    NEXUS_CPU_FEATURE_LA57      = 1ULL << 33,
    NEXUS_CPU_FEATURE_RDTSCP    = 1ULL << 34,
    NEXUS_CPU_FEATURE_PAGE_1GB  = 1ULL << 35,
    NEXUS_CPU_FEATURE_FXSR      = 1ULL << 36
};

struct nexus_cpu_info {
    bool cpuid_available;
    char vendor[13];
    char brand[49];
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint16_t logical_processors_per_package;
    uint16_t physical_cores_per_package;
    uint8_t physical_address_bits;
    uint8_t virtual_address_bits;
    uint64_t features;
};

/* Runtime detection only: optional instructions are never enabled implicitly. */
bool nexus_cpu_init(void);
const struct nexus_cpu_info *nexus_cpu_get_info(void);
bool nexus_cpu_has_feature(uint64_t feature);
const char *nexus_cpu_feature_level_label(void);

#endif
