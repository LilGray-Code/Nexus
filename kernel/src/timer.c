#include "timer.h"

#include "paging.h"
#include "serial.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_INPUT_HZ 1193182U

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_X2APIC_TPR    0x808
#define IA32_X2APIC_SVR    0x80F
#define IA32_X2APIC_LVT_LINT0 0x835
#define APIC_BASE_ENABLE   (1ULL << 11)
#define APIC_BASE_X2APIC   (1ULL << 10)
#define APIC_PHYSICAL_MASK 0x0000000FFFFFF000ULL

static volatile uint64_t tick_count;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)value;
    const uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static bool local_apic_enable_pic_virtual_wire(void) {
    uint32_t eax = 1, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;

    if ((edx & (1U << 9)) == 0) return false;

    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    apic_base |= APIC_BASE_ENABLE;

    if ((ecx & (1U << 21)) != 0) {
        /* x2APIC-capable processors use the architectural MSR interface. */
        apic_base |= APIC_BASE_X2APIC;
        write_msr(IA32_APIC_BASE_MSR, apic_base);
        write_msr(IA32_X2APIC_TPR, 0);

        uint64_t svr = read_msr(IA32_X2APIC_SVR);
        svr = (svr & ~0xFFULL) | 0x1FFULL;
        write_msr(IA32_X2APIC_SVR, svr);
        write_msr(IA32_X2APIC_LVT_LINT0, 0x700);
        serial_write("K2: x2APIC LINT0 configured for PIC ExtINT\r\n");
        return true;
    }

    /* xAPIC mode: map the APIC page explicitly; never assume HHDM covers MMIO. */
    apic_base &= ~APIC_BASE_X2APIC;
    write_msr(IA32_APIC_BASE_MSR, apic_base);

    const uint64_t apic_physical = apic_base & APIC_PHYSICAL_MASK;
    volatile uint32_t *lapic = NULL;
    if (!paging_map_mmio_page(apic_physical, &lapic) || lapic == NULL) {
        serial_write("K2: unable to map xAPIC MMIO page\r\n");
        return false;
    }

    lapic[0x80 / 4] = 0;
    lapic[0x350 / 4] = 0x700;
    uint32_t svr = lapic[0xF0 / 4];
    lapic[0xF0 / 4] = (svr & ~0xFFU) | 0x1FFU;
    __asm__ volatile ("mfence" : : : "memory");
    serial_write("K2: xAPIC MMIO mapped; LINT0 configured for PIC ExtINT\r\n");
    return true;
}

static void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Enable only IRQ0 (the PIT) during this bring-up stage. */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535U) divisor = 65535U;

    tick_count = 0;
    pic_remap();

    if (!local_apic_enable_pic_virtual_wire()) {
        serial_write("K2: Local APIC virtual-wire setup failed\r\n");
    }

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t timer_ticks(void) {
    return tick_count;
}

void timer_interrupt_tick(void) {
    ++tick_count;
}

void pic_irq_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}

void pic_irq_unmask(uint8_t irq) {
    if (irq >= 16U) return;

    if (irq < 8U) {
        const uint8_t mask = inb(PIC1_DATA);
        outb(PIC1_DATA, (uint8_t)(mask & (uint8_t)~(1U << irq)));
        return;
    }

    const uint8_t slave_mask = inb(PIC2_DATA);
    outb(PIC2_DATA,
         (uint8_t)(slave_mask & (uint8_t)~(1U << (irq - 8U))));
    const uint8_t master_mask = inb(PIC1_DATA);
    outb(PIC1_DATA, (uint8_t)(master_mask & (uint8_t)~(1U << 2U)));
}
