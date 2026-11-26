#ifndef NEXUS_ACPI_H
#define NEXUS_ACPI_H
#include <stdbool.h>
#include <stdint.h>
#define NEXUS_ACPI_MAX_IO_APICS 8U
#define NEXUS_ACPI_MAX_ISO 16U
#define NEXUS_ACPI_MAX_MCFG_REGIONS 8U
#define NEXUS_ACPI_HPET_ADDRESS_SPACE_SYSTEM_MEMORY 0U
struct nexus_acpi_mcfg_region {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
};
struct nexus_acpi_ioapic { uint8_t id; uint64_t physical_address; uint32_t gsi_base; };
struct nexus_acpi_iso { uint8_t bus; uint8_t source_irq; uint32_t gsi; uint16_t flags; };
struct nexus_acpi_topology {
    bool hpet_present;
    uint64_t hpet_physical_address;
    uint16_t hpet_minimum_tick;
    uint8_t hpet_number;
    uint8_t hpet_page_protection;
    uint64_t local_apic_physical_address;
    uint32_t local_apic_flags, enabled_processor_count, ioapic_count, iso_count;
    struct nexus_acpi_ioapic ioapics[NEXUS_ACPI_MAX_IO_APICS];
    struct nexus_acpi_iso isos[NEXUS_ACPI_MAX_ISO];
    uint32_t mcfg_region_count;
    struct nexus_acpi_mcfg_region mcfg_regions[NEXUS_ACPI_MAX_MCFG_REGIONS];
};
bool acpi_init(const void *rsdp_address);
const struct nexus_acpi_topology *acpi_get_topology(void);
const struct nexus_acpi_mcfg_region *acpi_find_mcfg_region(
    uint16_t segment_group, uint8_t bus);
#endif
