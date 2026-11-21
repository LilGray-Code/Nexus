#ifndef NEXUS_TIMER_H
#define NEXUS_TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
void timer_interrupt_tick(void);
void pic_irq_eoi(uint8_t irq);
void pic_irq_unmask(uint8_t irq);

#endif
