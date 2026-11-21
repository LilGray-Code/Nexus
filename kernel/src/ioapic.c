#include "ioapic.h"
#include "acpi.h"
#include "paging.h"
#include "serial.h"
#include <stdint.h>

#define IOAPIC_SELECT 0x00U
#define IOAPIC_WINDOW 0x10U
#define IOAPIC_ID 0x00U
#define IOAPIC_VERSION 0x01U
#define IOAPIC_REDIR_BASE 0x10U
#define REDIR_MASKED (1U << 16)
#define REDIR_ACTIVE_LOW (1U << 13)
#define REDIR_LEVEL (1U << 15)
#define IA32_APIC_BASE 0x1B
#define IA32_X2APIC_ID 0x802
#define IA32_X2APIC_EOI 0x80B
#define IA32_X2APIC_LINT0 0x835
#define APIC_X2APIC (1ULL << 10)
#define LAPIC_ID_REG 0x20U
#define LAPIC_LINT0_REG 0x350U
#define LAPIC_EOI_REG 0x0B0U

static bool route_active;
static bool route_x2apic;
static volatile uint32_t *lapic_regs;

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)));
}
static uint32_t ioapic_read(volatile uint32_t *r, uint8_t index) {
    r[IOAPIC_SELECT / 4] = index;
    __asm__ volatile ("mfence" : : : "memory");
    return r[IOAPIC_WINDOW / 4];
}
static void ioapic_write(volatile uint32_t *r, uint8_t index, uint32_t value) {
    r[IOAPIC_SELECT / 4] = index;
    __asm__ volatile ("mfence" : : : "memory");
    r[IOAPIC_WINDOW / 4] = value;
    __asm__ volatile ("mfence" : : : "memory");
}
static bool map_ioapic(const struct nexus_acpi_ioapic *record,
                       volatile uint32_t **registers) {
    return record != NULL && registers != NULL &&
           record->physical_address != 0 &&
           paging_map_mmio_page(record->physical_address, registers) &&
           *registers != NULL;
}

bool ioapic_probe(void) {
    const struct nexus_acpi_topology *t = acpi_get_topology();
    if (t == NULL || t->ioapic_count == 0) {
        serial_write("IOAPIC: no I/O APIC records in MADT\r\n");
        return false;
    }
    for (uint32_t i = 0; i < t->ioapic_count; ++i) {
        volatile uint32_t *r = NULL;
        if (!map_ioapic(&t->ioapics[i], &r)) return false;
        const uint32_t id = ioapic_read(r, IOAPIC_ID);
        const uint32_t v = ioapic_read(r, IOAPIC_VERSION);
        const uint8_t version = (uint8_t)v;
        const uint16_t count = (uint16_t)(((v >> 16) & 0xFFU) + 1U);
        if (version == 0 || version == 0xFF) return false;
        serial_write("IOAPIC: controller ID: ");
        serial_write_hex((id >> 24) & 0x0FU);
        serial_write("\r\nIOAPIC: register version: ");
        serial_write_hex(version);
        serial_write("\r\nIOAPIC: redirection entries: ");
        serial_write_hex(count);
        serial_write("\r\nIOAPIC: GSI base: ");
        serial_write_hex(t->ioapics[i].gsi_base);
        serial_write("\r\n");
    }
    serial_write("IOAPIC: MMIO register probe completed without routing changes\r\n");
    return true;
}

static bool get_local_apic_id(const struct nexus_acpi_topology *t,
                              uint32_t *destination) {
    if (t == NULL || destination == NULL) return false;
    const uint64_t base = rdmsr(IA32_APIC_BASE);
    route_x2apic = (base & APIC_X2APIC) != 0;
    if (route_x2apic) {
        const uint64_t id = rdmsr(IA32_X2APIC_ID);
        if (id > 0xFFU) return false;
        *destination = (uint32_t)id;
        lapic_regs = NULL;
        return true;
    }
    if (!paging_map_mmio_page(t->local_apic_physical_address, &lapic_regs) ||
        lapic_regs == NULL) return false;
    *destination = (lapic_regs[LAPIC_ID_REG / 4] >> 24) & 0xFFU;
    return true;
}

bool ioapic_route_isa_irq0(uint8_t vector) {
    const struct nexus_acpi_topology *t = acpi_get_topology();
    if (t == NULL || t->ioapic_count == 0 || vector < 32) return false;
    route_active = false;

    uint32_t gsi = 0; /* ISA IRQ0 defaults to GSI 0 when no override exists. */
    uint16_t flags = 0;
    for (uint32_t i = 0; i < t->iso_count; ++i) {
        const struct nexus_acpi_iso *iso = &t->isos[i];
        if (iso->bus == 0 && iso->source_irq == 0) {
            gsi = iso->gsi;
            flags = iso->flags;
            break;
        }
    }
    const uint16_t polarity = flags & 3U;
    const uint16_t trigger = (flags >> 2) & 3U;
    if (polarity == 2U || trigger == 2U) return false;

    uint32_t destination;
    if (!get_local_apic_id(t, &destination)) return false;

    for (uint32_t i = 0; i < t->ioapic_count; ++i) {
        const struct nexus_acpi_ioapic *record = &t->ioapics[i];
        if (gsi < record->gsi_base) continue;
        volatile uint32_t *r = NULL;
        if (!map_ioapic(record, &r)) continue;

        const uint32_t version = ioapic_read(r, IOAPIC_VERSION);
        const uint32_t count = ((version >> 16) & 0xFFU) + 1U;
        const uint32_t pin = gsi - record->gsi_base;
        if (pin >= count) continue;

        const uint32_t low_reg = IOAPIC_REDIR_BASE + pin * 2U;
        const uint32_t high_reg = low_reg + 1U;
        if (high_reg > 0xFFU) return false;

        uint32_t low = vector; /* Fixed delivery, physical destination. */
        if (polarity == 3U) low |= REDIR_ACTIVE_LOW;
        if (trigger == 3U) low |= REDIR_LEVEL;

        /* Mask first, program destination, then expose the selected route. */
        ioapic_write(r, (uint8_t)low_reg, low | REDIR_MASKED);
        ioapic_write(r, (uint8_t)high_reg, (destination & 0xFFU) << 24);
        ioapic_write(r, (uint8_t)low_reg, low);

        /* Disable PIC virtual-wire after the direct I/O APIC route is live. */
        if (route_x2apic) {
            wrmsr(IA32_X2APIC_LINT0, REDIR_MASKED);
        } else {
            lapic_regs[LAPIC_LINT0_REG / 4] = REDIR_MASKED;
            __asm__ volatile ("mfence" : : : "memory");
        }
        const uint8_t all_masked = 0xFF;
        __asm__ volatile ("outb %0, %1" : : "a"(all_masked), "Nd"((uint16_t)0x21));
        __asm__ volatile ("outb %0, %1" : : "a"(all_masked), "Nd"((uint16_t)0xA1));

        route_active = true;
        serial_write("IOAPIC: routed ISA IRQ0 through GSI ");
        serial_write_hex(gsi);
        serial_write(" to vector ");
        serial_write_hex(vector);
        serial_write("\r\nIOAPIC: Local APIC destination ID: ");
        serial_write_hex(destination);
        serial_write("\r\n");
        return true;
    }
    serial_write("IOAPIC: no controller covers the ISA IRQ0 GSI\r\n");
    return false;
}

bool ioapic_timer_route_active(void) {
    return route_active;
}

void ioapic_send_eoi(void) {
    if (!route_active) return;
    if (route_x2apic) {
        wrmsr(IA32_X2APIC_EOI, 0);
    } else if (lapic_regs != NULL) {
        lapic_regs[LAPIC_EOI_REG / 4] = 0;
        __asm__ volatile ("mfence" : : : "memory");
    }
}
