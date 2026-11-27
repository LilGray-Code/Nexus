#include "acpi.h"

#include "memory.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define ACPI_MAX_TABLE_LENGTH (1024U * 1024U)
#define ACPI_MAX_RSDP_LENGTH 4096U

struct acpi_rsdp_v1 {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct acpi_rsdp_v2 {
    struct acpi_rsdp_v1 first;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

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

struct acpi_madt_header {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_lapic {
    struct acpi_madt_entry header;
    uint8_t processor_uid;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_iso {
    struct acpi_madt_entry header;
    uint8_t bus;
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_lapic_override {
    struct acpi_madt_entry header;
    uint16_t reserved;
    uint64_t address;
} __attribute__((packed));

struct acpi_madt_x2apic {
    struct acpi_madt_entry header;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t processor_uid;
} __attribute__((packed));

struct acpi_hpet_table {
    struct acpi_sdt_header header;
    uint32_t event_timer_block_id;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

struct acpi_root_info {
    const struct acpi_sdt_header *header;
    bool xsdt;
};

struct acpi_mcfg_header {
    struct acpi_sdt_header header;
    uint64_t reserved;
} __attribute__((packed));

struct acpi_mcfg_allocation {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t reserved;
} __attribute__((packed));

struct acpi_power_gas {
    uint8_t space_id;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

static struct nexus_acpi_power_info power_info;

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes) {
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(bytes + 4U) << 32);
}

/* Decode only AML integer encodings needed by the fixed _S5 package. */
static bool aml_read_small_integer(const uint8_t *aml, size_t end,
                                   size_t *cursor, uint64_t *value) {
    if (aml == NULL || cursor == NULL || value == NULL ||
        *cursor >= end) return false;
    const uint8_t opcode = aml[(*cursor)++];
    if (opcode == 0x00U) { *value = 0U; return true; } /* ZeroOp */
    if (opcode == 0x01U) { *value = 1U; return true; } /* OneOp */
    size_t bytes = 0U;
    if (opcode == 0x0AU) bytes = 1U;       /* BytePrefix */
    else if (opcode == 0x0BU) bytes = 2U;  /* WordPrefix */
    else if (opcode == 0x0CU) bytes = 4U;  /* DWordPrefix */
    else if (opcode == 0x0EU) bytes = 8U;  /* QWordPrefix */
    else return false;
    if (bytes > end - *cursor) return false;
    uint64_t result = 0U;
    for (size_t i = 0U; i < bytes; ++i)
        result |= (uint64_t)aml[(*cursor)++] << (i * 8U);
    *value = result;
    return true;
}

/* Parse the AML Package length encoding with strict enclosing-table bounds. */
static bool aml_read_package_length(const uint8_t *aml, size_t end,
                                    size_t *cursor, size_t *package_end) {
    if (aml == NULL || cursor == NULL || package_end == NULL ||
        *cursor >= end) return false;
    const size_t package_start = *cursor;
    const uint8_t lead = aml[(*cursor)++];
    const size_t follow = (size_t)(lead >> 6);
    uint64_t length = follow == 0U ? (uint64_t)(lead & 0x3FU)
                                   : (uint64_t)(lead & 0x0FU);
    if (follow > end - *cursor) return false;
    for (size_t i = 0U; i < follow; ++i)
        length |= (uint64_t)aml[(*cursor)++] << (4U + i * 8U);
    if (length < (*cursor - package_start) ||
        length > (uint64_t)(end - package_start)) return false;
    *package_end = package_start + (size_t)length;
    return true;
}

static bool parse_dsdt_s5(const struct acpi_sdt_header *dsdt,
                          uint8_t *type_a, uint8_t *type_b) {
    if (!validate_sdt(dsdt, "DSDT") ||
        dsdt->length <= sizeof(struct acpi_sdt_header) ||
        type_a == NULL || type_b == NULL) return false;
    const uint8_t *aml = (const uint8_t *)dsdt + sizeof(*dsdt);
    const size_t aml_length =
        (size_t)dsdt->length - sizeof(struct acpi_sdt_header);

    for (size_t i = 0U; i + 6U < aml_length; ++i) {
        /* NameOp, "_S5_", PackageOp. Other namespace paths are not guessed. */
        if (aml[i] != 0x08U || aml[i + 1U] != '_' ||
            aml[i + 2U] != 'S' || aml[i + 3U] != '5' ||
            aml[i + 4U] != '_' || aml[i + 5U] != 0x12U) continue;
        size_t cursor = i + 6U;
        size_t package_end = 0U;
        if (!aml_read_package_length(aml, aml_length, &cursor,
                                     &package_end) ||
            cursor >= package_end) return false;
        const uint8_t element_count = aml[cursor++];
        if (element_count < 2U) return false;
        uint64_t a = 0U, b = 0U;
        if (!aml_read_small_integer(aml, package_end, &cursor, &a) ||
            !aml_read_small_integer(aml, package_end, &cursor, &b) ||
            a > 7U || b > 7U) return false;
        *type_a = (uint8_t)a;
        *type_b = (uint8_t)b;
        return true;
    }
    return false;
}

static bool gas_io_port(const uint8_t *bytes, uint8_t required_width,
                        uint16_t *port) {
    if (bytes == NULL || port == NULL) return false;
    const struct acpi_power_gas *gas =
        (const struct acpi_power_gas *)bytes;
    if (gas->space_id != 1U || gas->bit_offset != 0U ||
        gas->bit_width < required_width ||
        gas->address == 0U || gas->address > UINT16_MAX ||
        (required_width == 16U && gas->address == UINT16_MAX)) return false;
    if (required_width == 16U &&
        gas->access_size != 0U && gas->access_size != 2U) return false;
    if (required_width == 8U &&
        gas->access_size != 0U && gas->access_size != 1U) return false;
    *port = (uint16_t)gas->address;
    return true;
}

static bool parse_fadt(const struct acpi_sdt_header *header) {
    if (!validate_sdt(header, "FACP") || header->length < 90U) return false;
    const uint8_t *bytes = (const uint8_t *)header;
    power_info = (struct nexus_acpi_power_info){0};
    power_info.fadt_present = true;
    power_info.preferred_profile = bytes[45U];
    power_info.pm1_control_length = bytes[89U];

    if (power_info.pm1_control_length >= 2U) {
        const uint32_t legacy_a = read_le32(bytes + 64U);
        const uint32_t legacy_b = read_le32(bytes + 68U);
        if (legacy_a >= 0x100U && legacy_a <= UINT16_MAX - 1U)
            power_info.pm1a_control_port = (uint16_t)legacy_a;
        if (legacy_b >= 0x100U && legacy_b <= UINT16_MAX - 1U)
            power_info.pm1b_control_port = (uint16_t)legacy_b;
        /* ACPI 2+ extended GAS entries take precedence when usable. */
        if (header->length >= 184U) {
            uint16_t extended = 0U;
            if (gas_io_port(bytes + 172U, 16U, &extended) && extended >= 0x100U)
                power_info.pm1a_control_port = extended;
        }
        if (header->length >= 196U) {
            uint16_t extended = 0U;
            if (gas_io_port(bytes + 184U, 16U, &extended) && extended >= 0x100U)
                power_info.pm1b_control_port = extended;
        }
    }

    /* The reset GAS is optional (introduced with FADT revision 2). */
    const uint32_t fadt_flags =
        header->length >= 116U ? read_le32(bytes + 112U) : 0U;
    if (header->length >= 129U && (fadt_flags & (1U << 10)) != 0U) {
        uint16_t reset_port = 0U;
        if (gas_io_port(bytes + 116U, 8U, &reset_port)) {
            power_info.reset_port = reset_port;
            power_info.reset_value = bytes[128U];
            power_info.reset_supported = true;
        }
    }

    uint64_t dsdt_physical = 0U;
    if (header->length >= 148U)
        dsdt_physical = read_le64(bytes + 140U);
    if (dsdt_physical == 0U)
        dsdt_physical = read_le32(bytes + 40U);
    if (dsdt_physical != 0U) {
        const struct acpi_sdt_header *dsdt =
            physical_table(dsdt_physical);
        power_info.shutdown_supported =
            power_info.pm1a_control_port != 0U &&
            parse_dsdt_s5(dsdt, &power_info.s5_type_a,
                          &power_info.s5_type_b);
    }
    return true;
}

static bool find_and_parse_fadt(const struct acpi_root_info *root) {
    if (root == NULL || root->header == NULL) return false;
    const uint8_t *cursor =
        (const uint8_t *)root->header + sizeof(struct acpi_sdt_header);
    const uint8_t *end =
        (const uint8_t *)root->header + root->header->length;
    const size_t pointer_size = root->xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
    while (cursor < end) {
        if ((size_t)(end - cursor) < pointer_size) return false;
        const uint64_t physical = root->xsdt
            ? read_le64(cursor) : (uint64_t)read_le32(cursor);
        const struct acpi_sdt_header *table = physical_table(physical);
        if (table != NULL && signature_equals(table->signature, "FACP", 4U))
            return parse_fadt(table);
        cursor += pointer_size;
    }
    return false;
}

static struct nexus_acpi_topology topology;
static bool topology_valid;

static bool signature_equals(const char *actual, const char *expected,
                             size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

static bool checksum_valid(const void *memory, uint32_t length) {
    if (memory == NULL || length == 0 || length > ACPI_MAX_TABLE_LENGTH) {
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)memory;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

static const struct acpi_sdt_header *physical_table(uint64_t physical) {
    return (const struct acpi_sdt_header *)memory_phys_to_virt(physical);
}

static bool validate_sdt(const struct acpi_sdt_header *header,
                         const char *expected_signature) {
    if (header == NULL ||
        !signature_equals(header->signature, expected_signature, 4) ||
        header->length < sizeof(*header) ||
        header->length > ACPI_MAX_TABLE_LENGTH) {
        return false;
    }
    return checksum_valid(header, header->length);
}

static bool validate_rsdp(const void *address,
                          const struct acpi_rsdp_v1 **v1_out,
                          const struct acpi_rsdp_v2 **v2_out) {
    if (address == NULL || v1_out == NULL || v2_out == NULL) return false;

    const struct acpi_rsdp_v1 *v1 =
        (const struct acpi_rsdp_v1 *)address;
    if (!signature_equals(v1->signature, "RSD PTR ", 8) ||
        !checksum_valid(v1, sizeof(*v1))) {
        return false;
    }

    *v1_out = v1;
    *v2_out = NULL;

    if (v1->revision >= 2) {
        const struct acpi_rsdp_v2 *v2 =
            (const struct acpi_rsdp_v2 *)address;
        if (v2->length < sizeof(*v2) ||
            v2->length > ACPI_MAX_RSDP_LENGTH ||
            !checksum_valid(v2, v2->length)) {
            return false;
        }
        *v2_out = v2;
    }

    return true;
}

static bool find_root_table(const struct acpi_rsdp_v1 *rsdp,
                            const struct acpi_rsdp_v2 *rsdp2,
                            struct acpi_root_info *root) {
    if (rsdp == NULL || root == NULL) return false;

    if (rsdp2 != NULL && rsdp2->xsdt_address != 0) {
        const struct acpi_sdt_header *xsdt =
            physical_table(rsdp2->xsdt_address);
        if (validate_sdt(xsdt, "XSDT") &&
            ((xsdt->length - sizeof(*xsdt)) % sizeof(uint64_t)) == 0) {
            root->header = xsdt;
            root->xsdt = true;
            return true;
        }
    }

    if (rsdp->rsdt_address != 0) {
        const struct acpi_sdt_header *rsdt =
            physical_table(rsdp->rsdt_address);
        if (validate_sdt(rsdt, "RSDT") &&
            ((rsdt->length - sizeof(*rsdt)) % sizeof(uint32_t)) == 0) {
            root->header = rsdt;
            root->xsdt = false;
            return true;
        }
    }

    return false;
}

static bool parse_madt(const struct acpi_sdt_header *header) {
    if (!validate_sdt(header, "APIC") ||
        header->length < sizeof(struct acpi_madt_header)) {
        return false;
    }

    const struct acpi_madt_header *madt =
        (const struct acpi_madt_header *)header;

    topology.local_apic_physical_address = madt->local_apic_address;
    topology.local_apic_flags = madt->flags;

    const uint8_t *cursor =
        (const uint8_t *)header + sizeof(struct acpi_madt_header);
    const uint8_t *end = (const uint8_t *)header + header->length;

    while (cursor < end) {
        if ((size_t)(end - cursor) < sizeof(struct acpi_madt_entry)) {
            return false;
        }

        const struct acpi_madt_entry *entry =
            (const struct acpi_madt_entry *)cursor;
        if (entry->length < sizeof(*entry) ||
            (size_t)(end - cursor) < entry->length) {
            return false;
        }

        switch (entry->type) {
            case 0: /* Processor Local APIC */
                if (entry->length >= sizeof(struct acpi_madt_lapic)) {
                    const struct acpi_madt_lapic *lapic =
                        (const struct acpi_madt_lapic *)entry;
                    if ((lapic->flags & 0x3U) != 0)
                        ++topology.enabled_processor_count;
                }
                break;

            case 1: /* I/O APIC */
                if (entry->length >= sizeof(struct acpi_madt_ioapic) &&
                    topology.ioapic_count < NEXUS_ACPI_MAX_IO_APICS) {
                    const struct acpi_madt_ioapic *ioapic =
                        (const struct acpi_madt_ioapic *)entry;
                    struct nexus_acpi_ioapic *saved =
                        &topology.ioapics[topology.ioapic_count++];
                    saved->id = ioapic->ioapic_id;
                    saved->physical_address = ioapic->address;
                    saved->gsi_base = ioapic->gsi_base;
                }
                break;

            case 2: /* Interrupt Source Override */
                if (entry->length >= sizeof(struct acpi_madt_iso) &&
                    topology.iso_count < NEXUS_ACPI_MAX_ISO) {
                    const struct acpi_madt_iso *iso =
                        (const struct acpi_madt_iso *)entry;
                    struct nexus_acpi_iso *saved =
                        &topology.isos[topology.iso_count++];
                    saved->bus = iso->bus;
                    saved->source_irq = iso->source_irq;
                    saved->gsi = iso->gsi;
                    saved->flags = iso->flags;
                }
                break;

            case 5: /* Local APIC Address Override */
                if (entry->length >=
                    sizeof(struct acpi_madt_lapic_override)) {
                    const struct acpi_madt_lapic_override *override =
                        (const struct acpi_madt_lapic_override *)entry;
                    topology.local_apic_physical_address =
                        override->address;
                }
                break;

            case 9: /* Processor Local x2APIC */
                if (entry->length >= sizeof(struct acpi_madt_x2apic)) {
                    const struct acpi_madt_x2apic *x2apic =
                        (const struct acpi_madt_x2apic *)entry;
                    if ((x2apic->flags & 0x3U) != 0)
                        ++topology.enabled_processor_count;
                }
                break;

            default:
                /* Unknown MADT record types are skipped by their length. */
                break;
        }

        cursor += entry->length;
    }

    return true;
}

static bool find_and_parse_madt(const struct acpi_root_info *root) {
    if (root == NULL || root->header == NULL) return false;

    const uint8_t *cursor =
        (const uint8_t *)root->header + sizeof(struct acpi_sdt_header);
    const uint8_t *end =
        (const uint8_t *)root->header + root->header->length;
    const size_t pointer_size = root->xsdt ? sizeof(uint64_t) : sizeof(uint32_t);

    while (cursor < end) {
        uint64_t physical = 0;
        if ((size_t)(end - cursor) < pointer_size) return false;

        if (root->xsdt) {
            const uint64_t *item = (const uint64_t *)cursor;
            physical = *item;
        } else {
            const uint32_t *item = (const uint32_t *)cursor;
            physical = *item;
        }

        const struct acpi_sdt_header *table = physical_table(physical);
        if (table != NULL && signature_equals(table->signature, "APIC", 4)) {
            return parse_madt(table);
        }

        cursor += pointer_size;
    }

    return false;
}


static bool parse_mcfg(const struct acpi_sdt_header *header) {
    if (!validate_sdt(header, "MCFG") ||
        header->length < sizeof(struct acpi_mcfg_header)) return false;

    const struct acpi_mcfg_header *mcfg =
        (const struct acpi_mcfg_header *)header;
    if (mcfg->reserved != 0U) return false;

    const uint32_t payload_length =
        header->length - (uint32_t)sizeof(struct acpi_mcfg_header);
    if ((payload_length % sizeof(struct acpi_mcfg_allocation)) != 0U)
        return false;

    const uint32_t count =
        payload_length / (uint32_t)sizeof(struct acpi_mcfg_allocation);
    if (count > NEXUS_ACPI_MAX_MCFG_REGIONS) return false;

    const struct acpi_mcfg_allocation *entries =
        (const struct acpi_mcfg_allocation *)(
            (const uint8_t *)header + sizeof(struct acpi_mcfg_header));
    for (uint32_t i = 0U; i < count; ++i) {
        const struct acpi_mcfg_allocation *entry = &entries[i];
        if (entry->reserved != 0U || entry->base_address == 0U ||
            (entry->base_address & 0xFFFFFU) != 0U ||
            entry->start_bus > entry->end_bus) return false;

        const uint64_t bus_count =
            (uint64_t)entry->end_bus - entry->start_bus + 1U;
        const uint64_t window_bytes = bus_count << 20;
        if (entry->base_address > UINT64_MAX - window_bytes) return false;

        for (uint32_t previous = 0U; previous < i; ++previous) {
            const struct acpi_mcfg_allocation *other = &entries[previous];
            if (other->segment_group == entry->segment_group &&
                entry->start_bus <= other->end_bus &&
                other->start_bus <= entry->end_bus) return false;
        }

        topology.mcfg_regions[i] = (struct nexus_acpi_mcfg_region){
            .base_address = entry->base_address,
            .segment_group = entry->segment_group,
            .start_bus = entry->start_bus,
            .end_bus = entry->end_bus
        };
    }
    topology.mcfg_region_count = count;
    return true;
}

static bool find_and_parse_mcfg(const struct acpi_root_info *root) {
    if (root == NULL || root->header == NULL) return false;

    const uint8_t *cursor =
        (const uint8_t *)root->header + sizeof(struct acpi_sdt_header);
    const uint8_t *end =
        (const uint8_t *)root->header + root->header->length;
    const size_t pointer_size = root->xsdt ? sizeof(uint64_t) : sizeof(uint32_t);

    while (cursor < end) {
        uint64_t physical = 0U;
        if ((size_t)(end - cursor) < pointer_size) return false;
        if (root->xsdt) {
            const uint64_t *item = (const uint64_t *)cursor;
            physical = *item;
        } else {
            const uint32_t *item = (const uint32_t *)cursor;
            physical = *item;
        }
        const struct acpi_sdt_header *table = physical_table(physical);
        if (table != NULL && signature_equals(table->signature, "MCFG", 4U))
            return parse_mcfg(table);
        cursor += pointer_size;
    }
    return false;
}

static bool parse_hpet(const struct acpi_sdt_header *header) {
    if (!validate_sdt(header, "HPET") ||
        header->length < sizeof(struct acpi_hpet_table)) return false;

    const struct acpi_hpet_table *hpet =
        (const struct acpi_hpet_table *)header;
    /* This first implementation exposes only the architecturally common
     * system-memory, 64-bit HPET register block. It does not map or program
     * the timer until the dedicated HPET driver is initialized.
     */
    if (hpet->address_space_id != NEXUS_ACPI_HPET_ADDRESS_SPACE_SYSTEM_MEMORY ||
        hpet->register_bit_width != 64U ||
        hpet->register_bit_offset != 0U ||
        (hpet->access_size != 0U && hpet->access_size != 4U) ||
        hpet->address == 0U || (hpet->address & 7U) != 0U ||
        hpet->minimum_tick == 0U || hpet->page_protection > 3U) {
        return false;
    }

    topology.hpet_present = true;
    topology.hpet_physical_address = hpet->address;
    topology.hpet_minimum_tick = hpet->minimum_tick;
    topology.hpet_number = hpet->hpet_number;
    topology.hpet_page_protection = hpet->page_protection;
    return true;
}

static bool find_and_parse_hpet(const struct acpi_root_info *root) {
    if (root == NULL || root->header == NULL) return false;

    const uint8_t *cursor =
        (const uint8_t *)root->header + sizeof(struct acpi_sdt_header);
    const uint8_t *end =
        (const uint8_t *)root->header + root->header->length;
    const size_t pointer_size = root->xsdt ? sizeof(uint64_t) : sizeof(uint32_t);

    while (cursor < end) {
        uint64_t physical = 0U;
        if ((size_t)(end - cursor) < pointer_size) return false;
        if (root->xsdt) {
            const uint64_t *item = (const uint64_t *)cursor;
            physical = *item;
        } else {
            const uint32_t *item = (const uint32_t *)cursor;
            physical = *item;
        }
        const struct acpi_sdt_header *table = physical_table(physical);
        if (table != NULL && signature_equals(table->signature, "HPET", 4U))
            return parse_hpet(table);
        cursor += pointer_size;
    }
    return false;
}

bool acpi_init(const void *rsdp_address) {
    topology_valid = false;
    power_info = (struct nexus_acpi_power_info){0};
    topology.local_apic_physical_address = 0;
    topology.local_apic_flags = 0;
    topology.enabled_processor_count = 0;
    topology.ioapic_count = 0;
    topology.iso_count = 0;
    topology.mcfg_region_count = 0U;
    topology.hpet_present = false;
    topology.hpet_physical_address = 0U;
    topology.hpet_minimum_tick = 0U;
    topology.hpet_number = 0U;
    topology.hpet_page_protection = 0U;
    for (size_t i = 0U; i < NEXUS_ACPI_MAX_MCFG_REGIONS; ++i)
        topology.mcfg_regions[i] = (struct nexus_acpi_mcfg_region){0};

    const struct acpi_rsdp_v1 *rsdp = NULL;
    const struct acpi_rsdp_v2 *rsdp2 = NULL;
    if (!validate_rsdp(rsdp_address, &rsdp, &rsdp2)) {
        serial_write("ACPI: invalid or unavailable RSDP\r\n");
        return false;
    }

    struct acpi_root_info root = {0};
    if (!find_root_table(rsdp, rsdp2, &root)) {
        serial_write("ACPI: no valid XSDT/RSDT found\r\n");
        return false;
    }

    if (!find_and_parse_madt(&root)) {
        serial_write("ACPI: no valid MADT/APIC table found\r\n");
        return false;
    }

    /* HPET is optional. Discovery is read-only; the PIT remains the active
       clock source until a separately tested HPET driver takes ownership.
     */
    if (find_and_parse_hpet(&root)) {
        serial_write("ACPI: validated HPET system-memory block at ");
        serial_write_hex(topology.hpet_physical_address);
        serial_write("\r\n");
    }

    if (find_and_parse_fadt(&root)) {
        serial_write("ACPI: validated FADT; ");
        serial_write(power_info.shutdown_supported
            ? "S5 power-off available" : "S5 power-off unavailable");
        serial_write(power_info.reset_supported
            ? "; reset register available\r\n"
            : "; reset register unavailable\r\n");
    }

    /* MCFG is optional: legacy PCI configuration remains available when the
       firmware does not expose PCIe enhanced configuration windows. */
    if (find_and_parse_mcfg(&root) && topology.mcfg_region_count != 0U) {
        serial_write("ACPI: validated PCIe MCFG regions: ");
        serial_write_hex(topology.mcfg_region_count);
        serial_write("\r\n");
    }

    topology_valid = topology.local_apic_physical_address != 0;
    if (!topology_valid) {
        serial_write("ACPI: MADT has no usable Local APIC address\r\n");
        return false;
    }

    serial_write("ACPI: validated RSDP, root table, and MADT\r\n");
    serial_write("ACPI: Local APIC physical address: ");
    serial_write_hex(topology.local_apic_physical_address);
    serial_write("\r\nACPI: enabled processor entries: ");
    serial_write_hex(topology.enabled_processor_count);
    serial_write("\r\nACPI: I/O APIC entries: ");
    serial_write_hex(topology.ioapic_count);
    serial_write("\r\nACPI: interrupt source overrides: ");
    serial_write_hex(topology.iso_count);
    serial_write("\r\n");

    return true;
}

const struct nexus_acpi_power_info *acpi_get_power_info(void) {
    return power_info.fadt_present ? &power_info : NULL;
}

const struct nexus_acpi_topology *acpi_get_topology(void) {
    return topology_valid ? &topology : NULL;
}

const struct nexus_acpi_mcfg_region *acpi_find_mcfg_region(
    uint16_t segment_group, uint8_t bus) {
    if (!topology_valid) return NULL;
    for (uint32_t i = 0U; i < topology.mcfg_region_count; ++i) {
        const struct nexus_acpi_mcfg_region *region =
            &topology.mcfg_regions[i];
        if (region->segment_group == segment_group &&
            bus >= region->start_bus && bus <= region->end_bus)
            return region;
    }
    return NULL;
}
