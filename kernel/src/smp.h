#ifndef NEXUS_SMP_H
#define NEXUS_SMP_H

#include <stdbool.h>
#include <stdint.h>

struct limine_mp_response;

#define NEXUS_SMP_IPI_VECTOR 240U
#define NEXUS_SMP_MAX_CPUS 64U

typedef void (*nexus_smp_task_fn)(uint32_t cpu_index,
                                  uint32_t item_index,
                                  void *context);

/* Start secondary CPUs supplied by Limine; unsupported bootloaders fall back to one CPU. */
bool nexus_smp_init(struct limine_mp_response *response);
uint32_t nexus_smp_online_cpu_count(void);
/*
 * Synchronously distribute independent items across every online CPU.
 * Callbacks run in interrupt context on APs and must be short, non-blocking,
 * and safe to run concurrently. Returns false on an incomplete dispatch.
 */
bool nexus_smp_parallel_for(uint32_t item_count,
                            nexus_smp_task_fn task,
                            void *context);
/* Local-APIC vector 240 entry point; called only by the IDT dispatcher. */
void nexus_smp_ipi_interrupt(void);

#endif
