#ifndef NEXUS_GDT_H
#define NEXUS_GDT_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_GDT_MAX_CPUS 64U

/* Install the BSP GDT and TSS before loading the IDT. */
bool gdt_init(void);
/* Install an independent GDT/TSS for a Limine-started application processor. */
bool gdt_init_ap(uint32_t cpu_slot);

/* Update the ring-0 stack used when an interrupt enters from CPL3 on the BSP. */
bool gdt_set_kernel_stack(uint64_t stack_top);
uint64_t gdt_get_kernel_stack(void);

#endif
