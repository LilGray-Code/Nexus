#include "ioapic.h"

#include "acpi.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define IOAPIC_REGISTER_SELECT 0x00U
#define IOAPIC_REGISTER_WINDOW 0x10U
#define IOAPIC_REG_ID 0x00U
#define IOAPIC_REG_VERSION 0x01U

static uint32_t ioapic_read(volatile uint32_t *registers, uint8_t index) {
    registers[IOAPIC_REGISTER_SELECT / sizeof(uint32_t)] = index;
    __asm__ volatile ("mfence" : : : "memory");
    return registers[IOAPIC_REGISTER_WINDOW / sizeof(uint32_t)];
}

bool ioapic_probe(void) {
    const struct nexus_acpi_topology *topology = acpi_get_topology();
    if (topology == NULL || topology->ioapic_count == 0) {
        serial_write("IOAPIC: no I/O APIC records in MADT\r\n");
        return false;
    }

    for (uint32_t i = 0; i < topology->ioapic_count; ++i) {
        const struct nexus_acpi_ioapic *record = &topology->ioapics[i];
        volatile uint32_t *registers = NULL;

        if (record->physical_address == 0U) {
            serial_write("IOAPIC: MADT supplied a zero physical address\r\n");
            return false;
        }
        serial_write("IOAPIC: mapping controller MMIO at ");
        serial_write_hex(record->physical_address);
        serial_write("\r\n");
        if (!paging_map_mmio_page(record->physical_address, &registers) ||
            registers == NULL) {
            serial_write("IOAPIC: paging_map_mmio_page rejected controller address\r\n");
            return false;
        }

        const uint32_t id_register = ioapic_read(registers, IOAPIC_REG_ID);
        const uint32_t version_register =
            ioapic_read(registers, IOAPIC_REG_VERSION);

        const uint8_t controller_id = (uint8_t)((id_register >> 24) & 0x0F);
        const uint8_t version = (uint8_t)(version_register & 0xFF);
        const uint16_t redirection_count =
            (uint16_t)(((version_register >> 16) & 0xFF) + 1U);

        if (version == 0 || version == 0xFF) {
            serial_write("IOAPIC: invalid version register value\r\n");
            return false;
        }

        serial_write("IOAPIC: controller ID: ");
        serial_write_hex(controller_id);
        serial_write("\r\nIOAPIC: register version: ");
        serial_write_hex(version);
        serial_write("\r\nIOAPIC: redirection entries: ");
        serial_write_hex(redirection_count);
        serial_write("\r\nIOAPIC: GSI base: ");
        serial_write_hex(record->gsi_base);
        serial_write("\r\n");
    }

    serial_write("IOAPIC: read-only MMIO probe completed\r\n");
    return true;
}
