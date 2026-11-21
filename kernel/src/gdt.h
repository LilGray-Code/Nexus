#ifndef NEXUS_GDT_H
#define NEXUS_GDT_H

#include <stdbool.h>
#include <stdint.h>

/* Install the Nexus x86-64 GDT and 64-bit TSS before the IDT. */
bool gdt_init(void);

/* Update the ring-0 stack used when an interrupt enters from CPL3. */
bool gdt_set_kernel_stack(uint64_t stack_top);
uint64_t gdt_get_kernel_stack(void);

#endif
