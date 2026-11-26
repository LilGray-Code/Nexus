#ifndef NEXUS_ACPI_IOMMU_H
#define NEXUS_ACPI_IOMMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_ACPI_IOMMU_MAX_UNITS 8U
#define NEXUS_ACPI_IOMMU_MAX_RESERVED_REGIONS 8U

struct nexus_acpi_dmar_unit {
    uint16_t segment_group;
    uint8_t flags;
    uint64_t register_base;
};

struct nexus_acpi_dmar_reserved_region {
    uint16_t segment_group;
    uint64_t base_address;
    uint64_t limit_address;
};

struct nexus_acpi_iommu_info {
    bool dmar_table_valid;
    bool ivrs_table_valid;
    bool interrupt_remapping_advertised;
    bool x2apic_opt_out;
    bool hardware_enable_supported;
    bool translation_enabled;
    uint8_t dmar_host_address_width;
    uint16_t dmar_unit_count;
    uint16_t dmar_reserved_region_count;
    uint16_t ivrs_unit_count;
    uint16_t ivrs_memory_definition_count;
    uint16_t unknown_record_count;
    uint8_t ivrs_information;
    struct nexus_acpi_dmar_unit dmar_units[NEXUS_ACPI_IOMMU_MAX_UNITS];
    struct nexus_acpi_dmar_reserved_region
        dmar_reserved_regions[NEXUS_ACPI_IOMMU_MAX_RESERVED_REGIONS];
};

/* Validates and inventories ACPI DMAR or IVRS. It never enables an IOMMU. */
bool nexus_acpi_iommu_parse_table(
    const void *table, size_t available_bytes,
    struct nexus_acpi_iommu_info *info);

#endif
