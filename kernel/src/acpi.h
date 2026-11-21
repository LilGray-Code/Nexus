#ifndef NEXUS_ACPI_H
#define NEXUS_ACPI_H

#include <stdbool.h>
#include <stdint.h>

#define NEXUS_ACPI_MAX_IO_APICS 8U
#define NEXUS_ACPI_MAX_ISO 16U

struct nexus_acpi_ioapic {
    uint8_t id;
    uint64_t physical_address;
    uint32_t gsi_base;
};

struct nexus_acpi_iso {
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
};

struct nexus_acpi_topology {
    uint64_t local_apic_physical_address;
    uint32_t local_apic_flags;
    uint32_t enabled_processor_count;
    uint32_t ioapic_count;
    uint32_t iso_count;
    struct nexus_acpi_ioapic ioapics[NEXUS_ACPI_MAX_IO_APICS];
    struct nexus_acpi_iso isos[NEXUS_ACPI_MAX_ISO];
};

bool acpi_init(const void *rsdp_address);
const struct nexus_acpi_topology *acpi_get_topology(void);

#endif
