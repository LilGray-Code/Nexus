#include "cpu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_amd_extended_topology_decoder(void) {
    struct nexus_cpu_local_topology topology = {
        .vendor = NEXUS_CPU_VENDOR_UNKNOWN,
        .core_type = NEXUS_CPU_CORE_TYPE_UNKNOWN,
        .logical_processors_per_core = 1U
    };
    const uint32_t topology_feature = 1U << 22;
    const uint32_t extended_apic_id = 0xFEDCBA98U;
    const uint32_t core_id = 0x35U;
    const uint32_t threads_per_core_minus_one = 1U;
    const uint32_t node_id = 7U;
    const uint32_t nodes_per_processor = 2U;
    const uint32_t topology_ebx =
        core_id | (threads_per_core_minus_one << 8);
    const uint32_t topology_ecx =
        node_id | (nodes_per_processor << 8);

    assert(nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_AMD, 0x8000001EU, topology_feature,
        extended_apic_id, topology_ebx, topology_ecx, &topology));
    assert(topology.vendor == NEXUS_CPU_VENDOR_AMD);
    assert(topology.topology_extensions_supported);
    assert(topology.apic_id_valid && topology.apic_id == extended_apic_id);
    assert(topology.core_id_valid && topology.core_id == core_id);
    assert(topology.node_id_valid && topology.node_id == node_id);
    assert(topology.nodes_per_processor_raw == nodes_per_processor);
    assert(topology.logical_processors_per_core == 2U);

    topology = (struct nexus_cpu_local_topology){
        .vendor = NEXUS_CPU_VENDOR_UNKNOWN,
        .logical_processors_per_core = 1U
    };
    assert(nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_HYGON, 0x8000001EU, topology_feature,
        extended_apic_id, topology_ebx, topology_ecx, &topology));
    assert(topology.vendor == NEXUS_CPU_VENDOR_HYGON);
    assert(topology.core_id == core_id && topology.node_id == node_id);

    topology = (struct nexus_cpu_local_topology){
        .vendor = NEXUS_CPU_VENDOR_UNKNOWN,
        .logical_processors_per_core = 1U
    };
    assert(!nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_INTEL, 0x8000001EU, topology_feature,
        extended_apic_id, topology_ebx, topology_ecx, &topology));
    assert(!nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_AMD, 0x8000001DU, topology_feature,
        extended_apic_id, topology_ebx, topology_ecx, &topology));
    assert(!nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_AMD, 0x8000001EU, 0U,
        extended_apic_id, topology_ebx, topology_ecx, &topology));

    /* A raw ThreadsPerCore value of 255 encodes 256 logical threads and
     * cannot be represented by this kernel's uint8_t inventory field.
     */
    assert(!nexus_cpu_decode_amd_topology(
        NEXUS_CPU_VENDOR_AMD, 0x8000001EU, topology_feature,
        extended_apic_id, core_id | (0xFFU << 8), topology_ecx,
        &topology));
}

int main(void) {
    test_amd_extended_topology_decoder();
    assert(nexus_cpu_init());
    const struct nexus_cpu_info *info = nexus_cpu_get_info();
    assert(info != NULL);
    assert(info->cpuid_available);
    assert(info->vendor[0] != '\0');
    struct nexus_cpu_local_topology local_topology;
    assert(nexus_cpu_get_local_topology(&local_topology));
    assert(local_topology.vendor == info->vendor_kind);
    assert(local_topology.logical_processors_per_core >= 1U);
    assert(local_topology.apic_id_valid);
    if (local_topology.core_id_valid || local_topology.node_id_valid) {
        assert(local_topology.vendor == NEXUS_CPU_VENDOR_AMD ||
               local_topology.vendor == NEXUS_CPU_VENDOR_HYGON);
        assert(local_topology.topology_extensions_supported);
        assert(local_topology.core_id_valid && local_topology.node_id_valid);
    }
    assert(nexus_cpu_get_vendor() == info->vendor_kind);
    const enum nexus_cpu_core_type local_core_type =
        nexus_cpu_get_local_core_type();
    assert(local_core_type == NEXUS_CPU_CORE_TYPE_UNKNOWN ||
           local_core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE ||
           local_core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY);
    assert(nexus_cpu_vendor_name()[0] != '\0');
    if (info->vendor_kind == NEXUS_CPU_VENDOR_INTEL)
        assert(nexus_cpu_is_intel() && !nexus_cpu_is_amd());
    if (info->vendor_kind == NEXUS_CPU_VENDOR_AMD ||
        info->vendor_kind == NEXUS_CPU_VENDOR_HYGON)
        assert(nexus_cpu_is_amd() && !nexus_cpu_is_intel());
    assert(info->brand[0] != '\0');
    assert(info->logical_processors_per_package >= 1U);
    assert(info->physical_cores_per_package >= 1U);
    if (info->physical_address_bits != 0U)
        assert(info->physical_address_bits >= 32U &&
               info->physical_address_bits <= 64U);
    if (info->virtual_address_bits != 0U)
        assert(info->virtual_address_bits >= 32U &&
               info->virtual_address_bits <= 64U);
    assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_LONG_MODE));
    assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_SSE2));
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2))
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
    if (nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX512F)) {
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_XSAVE));
        assert(nexus_cpu_has_feature(NEXUS_CPU_FEATURE_OSXSAVE));
    }
    nexus_cpu_disable_avx();
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX));
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX2));
    assert(!nexus_cpu_has_feature(NEXUS_CPU_FEATURE_AVX512F));
    assert(nexus_cpu_feature_level_label()[0] != '\0');
    if (info->core_type != NEXUS_CPU_CORE_TYPE_UNKNOWN) {
        assert(info->vendor_kind == NEXUS_CPU_VENDOR_INTEL);
        assert(info->core_type == NEXUS_CPU_CORE_TYPE_PERFORMANCE ||
               info->core_type == NEXUS_CPU_CORE_TYPE_EFFICIENCY);
    }
    if (info->frequency_info_valid) {
        assert(info->vendor_kind == NEXUS_CPU_VENDOR_INTEL);
        assert(info->base_frequency_mhz != 0U);
        assert(info->maximum_frequency_mhz == 0U ||
               info->maximum_frequency_mhz >= info->base_frequency_mhz);
    }
    printf("NEXUS CPU CAPABILITY TESTS PASSED (%s / %s)\n",
           info->vendor, info->brand);
    return 0;
}
