#include "interrupts.h"
#include "ioapic.h"

#include "serial.h"
#include "timer.h"

#include <stdint.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
};

#define DECLARE_ISR(n) extern void isr##n(void)
DECLARE_ISR(0);  DECLARE_ISR(1);  DECLARE_ISR(2);  DECLARE_ISR(3);
DECLARE_ISR(4);  DECLARE_ISR(5);  DECLARE_ISR(6);  DECLARE_ISR(7);
DECLARE_ISR(8);  DECLARE_ISR(9);  DECLARE_ISR(10); DECLARE_ISR(11);
DECLARE_ISR(12); DECLARE_ISR(13); DECLARE_ISR(14); DECLARE_ISR(15);
DECLARE_ISR(16); DECLARE_ISR(17); DECLARE_ISR(18); DECLARE_ISR(19);
DECLARE_ISR(20); DECLARE_ISR(21); DECLARE_ISR(22); DECLARE_ISR(23);
DECLARE_ISR(24); DECLARE_ISR(25); DECLARE_ISR(26); DECLARE_ISR(27);
DECLARE_ISR(28); DECLARE_ISR(29); DECLARE_ISR(30); DECLARE_ISR(31);

#define DECLARE_IRQ(n) extern void irq##n(void)
DECLARE_IRQ(32); DECLARE_IRQ(33); DECLARE_IRQ(34); DECLARE_IRQ(35);
DECLARE_IRQ(36); DECLARE_IRQ(37); DECLARE_IRQ(38); DECLARE_IRQ(39);
DECLARE_IRQ(40); DECLARE_IRQ(41); DECLARE_IRQ(42); DECLARE_IRQ(43);
DECLARE_IRQ(44); DECLARE_IRQ(45); DECLARE_IRQ(46); DECLARE_IRQ(47);

static struct idt_gate idt[256] __attribute__((aligned(16)));

static void idt_set_gate(uint8_t vector, void (*handler)(void),
                         uint16_t selector) {
    const uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(address & 0xFFFF);
    idt[vector].selector = selector;
    idt[vector].ist = 0;
    idt[vector].type_attributes = 0x8E;
    idt[vector].offset_middle = (uint16_t)((address >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

void idt_init(void) {
    static void (*const exception_handlers[32])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    static void (*const irq_handlers[16])(void) = {
        irq32, irq33, irq34, irq35, irq36, irq37, irq38, irq39,
        irq40, irq41, irq42, irq43, irq44, irq45, irq46, irq47
    };
    uint16_t code_segment;

    __asm__ volatile ("mov %%cs, %0" : "=r"(code_segment));
    for (uint8_t vector = 0; vector < 32; ++vector) {
        idt_set_gate(vector, exception_handlers[vector], code_segment);
    }
    for (uint8_t irq = 0; irq < 16; ++irq) {
        idt_set_gate((uint8_t)(32 + irq), irq_handlers[irq], code_segment);
    }

    const struct idt_register idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

__attribute__((noreturn))
void exception_dispatch(const struct interrupt_frame *frame) {
    serial_write("\r\nNEXUS CPU EXCEPTION\r\n");
    serial_write("Vector: ");
    serial_write_hex(frame->vector);
    serial_write("\r\nError code: ");
    serial_write_hex(frame->error_code);
    serial_write("\r\nInstruction pointer: ");
    serial_write_hex(frame->instruction_pointer);
    serial_write("\r\nSystem halted.\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

void interrupt_dispatch(const struct interrupt_frame *frame) {
    if (frame->vector >= 32 && frame->vector <= 47) {
        const uint8_t irq = (uint8_t)(frame->vector - 32);
        if (irq == 0) {
            serial_write("IRQ: timer vector entered\\r\\n");
            timer_interrupt_tick();
        }
        if (irq == 0 && ioapic_timer_route_active()) {
            ioapic_send_eoi();
            serial_write("IRQ: Local APIC EOI sent\\r\\n");
        } else {
            pic_irq_eoi(irq);
        }
        return;
    }

    serial_write("\r\nNEXUS UNEXPECTED INTERRUPT: ");
    serial_write_hex(frame->vector);
    serial_write("\r\n");
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}
