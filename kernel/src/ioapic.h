#ifndef NEXUS_IOAPIC_H
#define NEXUS_IOAPIC_H
#include <stdbool.h>
#include <stdint.h>
bool ioapic_probe(void);
bool ioapic_route_isa_irq0(uint8_t vector);
bool ioapic_timer_route_active(void);
void ioapic_send_eoi(void);
#endif
