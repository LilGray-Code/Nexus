#ifndef NEXUS_SMP_H
#define NEXUS_SMP_H

#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"

struct limine_mp_response;

#define NEXUS_SMP_IPI_VECTOR 240U
#define NEXUS_SMP_MAX_CPUS 64U

typedef void (*nexus_smp_task_fn)(uint32_t cpu_index,
                                  uint32_t item_index,
                                  void *context);

struct nexus_smp_cpu_info {
    /* APIC ID from the boot protocol's processor record. */
    uint32_t apic_id;
    /* Per-core CPUID topology, queried on that logical CPU. */
    uint32_t cpuid_apic_id;
    uint32_t core_id;
    uint32_t node_id;
    enum nexus_cpu_vendor vendor;
    enum nexus_cpu_core_type core_type;
    uint8_t logical_processors_per_core;
    uint8_t nodes_per_processor_raw;
    bool cpuid_apic_id_valid;
    bool core_id_valid;
    bool node_id_valid;
    bool topology_extensions_supported;
    bool online;
};

/* Start secondary CPUs supplied by Limine; unsupported bootloaders fall back to one CPU. */
bool nexus_smp_init(struct limine_mp_response *response);
uint32_t nexus_smp_online_cpu_count(void);
/* Read the published APIC ID and core class for one online/offline slot. */
bool nexus_smp_cpu_info_at(uint32_t cpu_slot, struct nexus_smp_cpu_info *info);
/*
 * Synchronously distribute independent items across every online CPU.
 * Callbacks run in interrupt context on APs and must be short, non-blocking,
 * and safe to run concurrently. Returns false on an incomplete dispatch.
 */
bool nexus_smp_parallel_for(uint32_t item_count,
                            nexus_smp_task_fn task,
                            void *context);
/* Invoke a short, non-blocking callback exactly once on each online CPU. */
bool nexus_smp_call_each_cpu(nexus_smp_task_fn task, void *context);
/* Local-APIC vector 240 entry point; called only by the IDT dispatcher. */
void nexus_smp_ipi_interrupt(void);

#endif
