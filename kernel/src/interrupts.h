#ifndef NEXUS_INTERRUPTS_H
#define NEXUS_INTERRUPTS_H

void idt_init(void);
bool idt_load_current_cpu(void);
/* Reload the BSP-prepared shared IDT after an AP installs its own GDT. */
bool idt_load_current_cpu(void);

#endif
