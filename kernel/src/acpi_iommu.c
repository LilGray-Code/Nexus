#include "acpi_iommu.h"

#include <stddef.h>
#include <stdint.h>

#define ACPI_IOMMU_MAX_TABLE_LENGTH (1024U * 1024U)
#define ACPI_DMAR_TYPE_DRHD 0U
#define ACPI_DMAR_TYPE_RMRR 1U
#define ACPI_DMAR_DRHD_FIXED_LENGTH 16U
#define ACPI_DMAR_RMRR_FIXED_LENGTH 24U
#define ACPI_DMAR_SCOPE_FIXED_LENGTH 6U
#define ACPI_IVRS_FIXED_LENGTH 4U
#define ACPI_IVRS_IVHD_LEGACY 0x10U
#define ACPI_IVRS_IVHD_HARDWARE 0x11U
#define ACPI_IVRS_IVHD_HARDWARE_EXT 0x40U
#define ACPI_IVRS_IVMD_ALL 0x20U
#define ACPI_IVRS_IVMD_ONE 0x21U
#define ACPI_IVRS_IVMD_RANGE 0x22U

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_dmar_header {
    struct acpi_sdt_header sdt;
    uint8_t host_address_width;
    uint8_t flags;
    uint8_t reserved[10];
} __attribute__((packed));

struct acpi_dmar_structure {
    uint16_t type;
    uint16_t length;
} __attribute__((packed));

struct acpi_dmar_drhd {
    struct acpi_dmar_structure header;
    uint8_t flags;
    uint8_t reserved;
    uint16_t segment_group;
    uint64_t register_base;
} __attribute__((packed));

struct acpi_dmar_rmrr {
    struct acpi_dmar_structure header;
    uint16_t reserved;
    uint16_t segment_group;
    uint64_t base_address;
    uint64_t limit_address;
} __attribute__((packed));

struct acpi_dmar_scope {
    uint8_t type;
    uint8_t length;
    uint16_t reserved;
    uint8_t enumeration_id;
    uint8_t start_bus;
} __attribute__((packed));

struct acpi_ivrs_header {
    struct acpi_sdt_header sdt;
    uint32_t information;
} __attribute__((packed));

struct acpi_ivrs_block {
    uint8_t type;
    uint8_t flags;
    uint16_t length;
} __attribute__((packed));

static bool signature_equals(const char *actual, const char *expected) {
    for (size_t i = 0U; i < 4U; ++i)
        if (actual[i] != expected[i]) return false;
    return true;
}

static bool checksum_valid(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum == 0U;
}

static bool dmar_validate_scopes(const uint8_t *cursor,
                                 const uint8_t *end) {
    while (cursor < end) {
        if ((size_t)(end - cursor) < ACPI_DMAR_SCOPE_FIXED_LENGTH)
            return false;
        const struct acpi_dmar_scope *scope =
            (const struct acpi_dmar_scope *)cursor;
        if (scope->length < ACPI_DMAR_SCOPE_FIXED_LENGTH ||
            (size_t)(end - cursor) < scope->length ||
            ((scope->length - ACPI_DMAR_SCOPE_FIXED_LENGTH) & 1U) != 0U)
            return false;
        cursor += scope->length;
    }
    return cursor == end;
}

static bool parse_dmar(const uint8_t *bytes, size_t length,
                       struct nexus_acpi_iommu_info *info) {
    if (length < sizeof(struct acpi_dmar_header)) return false;
    const struct acpi_dmar_header *dmar =
        (const struct acpi_dmar_header *)bytes;
    const uint8_t *cursor = bytes + sizeof(*dmar);
    const uint8_t *end = bytes + length;

    if (dmar->host_address_width > 63U) return false;
    for (size_t i = 0U; i < sizeof(dmar->reserved); ++i)
        if (dmar->reserved[i] != 0U) return false;

    info->dmar_host_address_width =
        (uint8_t)(dmar->host_address_width + 1U);
    info->interrupt_remapping_advertised =
        (dmar->flags & 0x01U) != 0U;
    info->x2apic_opt_out = (dmar->flags & 0x02U) != 0U;

    while (cursor < end) {
        if ((size_t)(end - cursor) <
            sizeof(struct acpi_dmar_structure)) return false;
        const struct acpi_dmar_structure *record =
            (const struct acpi_dmar_structure *)cursor;
        if (record->length < sizeof(*record) ||
            (size_t)(end - cursor) < record->length) return false;
        const uint8_t *record_end = cursor + record->length;

        if (record->type == ACPI_DMAR_TYPE_DRHD) {
            if (record->length < ACPI_DMAR_DRHD_FIXED_LENGTH) return false;
            const struct acpi_dmar_drhd *drhd =
                (const struct acpi_dmar_drhd *)cursor;
            if (drhd->reserved != 0U || drhd->register_base == 0U ||
                (drhd->register_base & 0xFFFU) != 0U) return false;
            if (!dmar_validate_scopes(
                    cursor + ACPI_DMAR_DRHD_FIXED_LENGTH, record_end))
                return false;
            if (info->dmar_unit_count != UINT16_MAX)
                ++info->dmar_unit_count;
            const uint16_t saved = (uint16_t)(info->dmar_unit_count - 1U);
            if (saved < NEXUS_ACPI_IOMMU_MAX_UNITS) {
                info->dmar_units[saved] =
                    (struct nexus_acpi_dmar_unit){
                        .segment_group = drhd->segment_group,
                        .flags = drhd->flags,
                        .register_base = drhd->register_base
                    };
            }
        } else if (record->type == ACPI_DMAR_TYPE_RMRR) {
            if (record->length < ACPI_DMAR_RMRR_FIXED_LENGTH) return false;
            const struct acpi_dmar_rmrr *rmrr =
                (const struct acpi_dmar_rmrr *)cursor;
            if (rmrr->reserved != 0U ||
                rmrr->base_address > rmrr->limit_address)
                return false;
            if (!dmar_validate_scopes(
                    cursor + ACPI_DMAR_RMRR_FIXED_LENGTH, record_end))
                return false;
            if (info->dmar_reserved_region_count != UINT16_MAX)
                ++info->dmar_reserved_region_count;
            const uint16_t saved =
                (uint16_t)(info->dmar_reserved_region_count - 1U);
            if (saved < NEXUS_ACPI_IOMMU_MAX_RESERVED_REGIONS) {
                info->dmar_reserved_regions[saved] =
                    (struct nexus_acpi_dmar_reserved_region){
                        .segment_group = rmrr->segment_group,
                        .base_address = rmrr->base_address,
                        .limit_address = rmrr->limit_address
                    };
            }
        } else if (info->unknown_record_count != UINT16_MAX) {
            ++info->unknown_record_count;
        }
        cursor = record_end;
    }

    info->dmar_table_valid = true;
    return true;
}

static bool parse_ivrs(const uint8_t *bytes, size_t length,
                       struct nexus_acpi_iommu_info *info) {
    if (length < sizeof(struct acpi_ivrs_header)) return false;
    const struct acpi_ivrs_header *ivrs =
        (const struct acpi_ivrs_header *)bytes;
    const uint8_t *cursor = bytes + sizeof(*ivrs);
    const uint8_t *end = bytes + length;

    info->ivrs_information = (uint8_t)(ivrs->information & 0xFFU);
    while (cursor < end) {
        if ((size_t)(end - cursor) < sizeof(struct acpi_ivrs_block))
            return false;
        const struct acpi_ivrs_block *block =
            (const struct acpi_ivrs_block *)cursor;
        if (block->length < ACPI_IVRS_FIXED_LENGTH ||
            (size_t)(end - cursor) < block->length) return false;

        switch (block->type) {
            case ACPI_IVRS_IVHD_LEGACY:
            case ACPI_IVRS_IVHD_HARDWARE:
            case ACPI_IVRS_IVHD_HARDWARE_EXT:
                /* Fixed IVHD fields occupy at least 24 bytes. */
                if (block->length < 24U) return false;
                if (info->ivrs_unit_count != UINT16_MAX)
                    ++info->ivrs_unit_count;
                break;
            case ACPI_IVRS_IVMD_ALL:
            case ACPI_IVRS_IVMD_ONE:
            case ACPI_IVRS_IVMD_RANGE:
                /* IVMD memory exclusion/definition records have a 32-byte
                 * fixed prefix; device-entry extensions follow that prefix.
                 */
                if (block->length < 32U) return false;
                if (info->ivrs_memory_definition_count != UINT16_MAX)
                    ++info->ivrs_memory_definition_count;
                break;
            default:
                if (info->unknown_record_count != UINT16_MAX)
                    ++info->unknown_record_count;
                break;
        }
        cursor += block->length;
    }

    info->ivrs_table_valid = true;
    return true;
}

bool nexus_acpi_iommu_parse_table(
    const void *table, size_t available_bytes,
    struct nexus_acpi_iommu_info *info) {
    if (table == NULL || info == NULL ||
        available_bytes < sizeof(struct acpi_sdt_header)) return false;
    const uint8_t *bytes = (const uint8_t *)table;
    const struct acpi_sdt_header *header =
        (const struct acpi_sdt_header *)table;
    if (header->length < sizeof(*header) ||
        header->length > ACPI_IOMMU_MAX_TABLE_LENGTH ||
        header->length > available_bytes ||
        !checksum_valid(bytes, header->length)) return false;

    if (signature_equals(header->signature, "DMAR"))
        return parse_dmar(bytes, header->length, info);
    if (signature_equals(header->signature, "IVRS"))
        return parse_ivrs(bytes, header->length, info);
    return false;
}
