#ifndef NEXUS_CPU_H
#define NEXUS_CPU_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_cpu_vendor {
    NEXUS_CPU_VENDOR_UNKNOWN = 0,
    NEXUS_CPU_VENDOR_INTEL,
    NEXUS_CPU_VENDOR_AMD,
    NEXUS_CPU_VENDOR_HYGON,
    NEXUS_CPU_VENDOR_CENTAUR,
    NEXUS_CPU_VENDOR_ZHAOXIN,
    NEXUS_CPU_VENDOR_TRANSMETA
};

enum nexus_cpu_core_type {
    NEXUS_CPU_CORE_TYPE_UNKNOWN = 0,
    NEXUS_CPU_CORE_TYPE_EFFICIENCY = 0x20,
    NEXUS_CPU_CORE_TYPE_PERFORMANCE = 0x40
};

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
    NEXUS_CPU_FEATURE_FXSR      = 1ULL << 36,
    NEXUS_CPU_FEATURE_AVX512F   = 1ULL << 37,
    NEXUS_CPU_FEATURE_AVX512DQ  = 1ULL << 38,
    NEXUS_CPU_FEATURE_AVX512CD  = 1ULL << 39,
    NEXUS_CPU_FEATURE_AVX512BW  = 1ULL << 40,
    NEXUS_CPU_FEATURE_AVX512VL  = 1ULL << 41,
    NEXUS_CPU_FEATURE_AVX512VNNI= 1ULL << 42
};

struct nexus_cpu_local_topology {
    enum nexus_cpu_vendor vendor;
    enum nexus_cpu_core_type core_type;
    uint32_t apic_id;
    uint32_t core_id;
    uint32_t node_id;
    uint8_t logical_processors_per_core;
    uint8_t nodes_per_processor_raw;
    bool apic_id_valid;
    bool core_id_valid;
    bool node_id_valid;
    bool topology_extensions_supported;
};

struct nexus_cpu_info {
    bool cpuid_available;
    enum nexus_cpu_vendor vendor_kind;
    enum nexus_cpu_core_type core_type;
    char vendor[13];
    char brand[49];
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint16_t logical_processors_per_package;
    uint16_t physical_cores_per_package;
    uint8_t logical_processors_per_core;
    uint32_t local_apic_id;
    uint32_t core_id;
    uint32_t node_id;
    bool apic_id_valid;
    bool core_id_valid;
    bool node_id_valid;
    bool topology_extensions_supported;
    uint8_t amd_nodes_per_processor_raw;
    uint8_t physical_address_bits;
    uint8_t virtual_address_bits;
    uint64_t features;
    /* CPUID leaf 6: advertised thermal/power capabilities, not live readings. */
    uint32_t thermal_power_capabilities;
    bool digital_thermal_sensor_supported;
    bool turbo_boost_supported;
    bool hardware_p_states_supported;
    bool hwp_notifications_supported;
    bool hwp_energy_performance_supported;
    /* Intel CPUID leaf 0x16 values; zero/invalid when unavailable. */
    bool frequency_info_valid;
    uint16_t base_frequency_mhz;
    uint16_t maximum_frequency_mhz;
    uint16_t bus_frequency_mhz;
    /* AMD CPUID 0x80000007 EDX, retained as vendor-specific raw capability bits. */
    uint32_t amd_power_management_features;
};

/* Runtime detection only: optional instructions are never enabled implicitly. */
bool nexus_cpu_init(void);
enum nexus_cpu_vendor nexus_cpu_get_vendor(void);
/* Query CPUID topology for the executing logical CPU (BSP or AP). */
bool nexus_cpu_get_local_topology(struct nexus_cpu_local_topology *topology);
/* Decode a captured AMD/Hygon CPUID 0x8000001E topology record. */
bool nexus_cpu_decode_amd_topology(
    enum nexus_cpu_vendor vendor, uint32_t maximum_extended_leaf,
    uint32_t extended_feature_ecx, uint32_t topology_eax,
    uint32_t topology_ebx, uint32_t topology_ecx,
    struct nexus_cpu_local_topology *topology);
enum nexus_cpu_core_type nexus_cpu_get_local_core_type(void);
const char *nexus_cpu_vendor_name(void);
bool nexus_cpu_is_intel(void);
bool nexus_cpu_is_amd(void);
const struct nexus_cpu_info *nexus_cpu_get_info(void);
bool nexus_cpu_has_feature(uint64_t feature);
/* Publish AVX/AVX2 only after the kernel enables and verifies XCR0 state. */
void nexus_cpu_enable_avx_features(void);
/* Mark AVX unavailable when the OS deliberately disables XSAVE state. */
void nexus_cpu_disable_avx(void);
const char *nexus_cpu_feature_level_label(void);

#endif
