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

struct acpi_root_info {
    const struct acpi_sdt_header *header;
    bool xsdt;
};

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

bool acpi_init(const void *rsdp_address) {
    topology_valid = false;
    topology.local_apic_physical_address = 0;
    topology.local_apic_flags = 0;
    topology.enabled_processor_count = 0;
    topology.ioapic_count = 0;
    topology.iso_count = 0;

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

const struct nexus_acpi_topology *acpi_get_topology(void) {
    return topology_valid ? &topology : NULL;
}
