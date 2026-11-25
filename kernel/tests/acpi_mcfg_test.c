#include "acpi.h"
#include "memory.h"
#include "serial.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_RSDP 0x100U
#define TEST_XSDT 0x200U
#define TEST_MADT 0x300U
#define TEST_MCFG 0x400U
#define TEST_ARENA_SIZE 0x1000U

static uint8_t arena[TEST_ARENA_SIZE];

void *memory_phys_to_virt(uint64_t physical_address) {
    if (physical_address >= TEST_ARENA_SIZE) return NULL;
    return &arena[physical_address];
}
void serial_write(const char *text) { (void)text; }
void serial_write_hex(uint64_t value) { (void)value; }

static void finish_checksum(uint8_t *bytes, size_t length, size_t offset) {
    bytes[offset] = 0U;
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i) sum = (uint8_t)(sum + bytes[i]);
    bytes[offset] = (uint8_t)(0U - sum);
}

static void make_sdt(uint8_t *table, const char signature[4], uint32_t length) {
    memset(table, 0, length);
    memcpy(table, signature, 4U);
    memcpy(table + 4U, &length, sizeof(length));
    table[8] = 1U;
}

static void write_mcfg_entry(uint8_t *entry, uint64_t base,
                             uint16_t segment, uint8_t first_bus,
                             uint8_t last_bus) {
    memset(entry, 0, 16U);
    memcpy(entry, &base, sizeof(base));
    memcpy(entry + 8U, &segment, sizeof(segment));
    entry[10] = first_bus;
    entry[11] = last_bus;
}

static void build_tables(const uint16_t *segments, const uint8_t *starts,
                         const uint8_t *ends, const uint64_t *bases,
                         size_t region_count, bool corrupt_mcfg_checksum) {
    memset(arena, 0, sizeof(arena));

    uint8_t *rsdp = &arena[TEST_RSDP];
    memcpy(rsdp, "RSD PTR ", 8U);
    rsdp[15] = 2U;
    const uint32_t rsdp_length = 36U;
    memcpy(rsdp + 20U, &rsdp_length, sizeof(rsdp_length));
    const uint64_t xsdt_address = TEST_XSDT;
    memcpy(rsdp + 24U, &xsdt_address, sizeof(xsdt_address));
    finish_checksum(rsdp, 20U, 8U);
    finish_checksum(rsdp, 36U, 32U);

    const uint32_t xsdt_length = 36U + 2U * 8U;
    uint8_t *xsdt = &arena[TEST_XSDT];
    make_sdt(xsdt, "XSDT", xsdt_length);
    const uint64_t table_addresses[2] = {TEST_MADT, TEST_MCFG};
    memcpy(xsdt + 36U, table_addresses, sizeof(table_addresses));
    finish_checksum(xsdt, xsdt_length, 9U);

    uint8_t *madt = &arena[TEST_MADT];
    make_sdt(madt, "APIC", 44U);
    const uint32_t lapic_address = 0xFEE00000U;
    const uint32_t madt_flags = 1U;
    memcpy(madt + 36U, &lapic_address, sizeof(lapic_address));
    memcpy(madt + 40U, &madt_flags, sizeof(madt_flags));
    finish_checksum(madt, 44U, 9U);

    const uint32_t mcfg_length =
        (uint32_t)(44U + region_count * 16U);
    uint8_t *mcfg = &arena[TEST_MCFG];
    make_sdt(mcfg, "MCFG", mcfg_length);
    for (size_t i = 0U; i < region_count; ++i)
        write_mcfg_entry(mcfg + 44U + i * 16U, bases[i], segments[i],
                         starts[i], ends[i]);
    finish_checksum(mcfg, mcfg_length, 9U);
    if (corrupt_mcfg_checksum) mcfg[9] ^= 0x80U;
}

static void test_multiple_segments_and_bus_bounds(void) {
    const uint16_t segments[] = {0U, 2U};
    const uint8_t starts[] = {0U, 0x20U};
    const uint8_t ends[] = {0xFFU, 0x2FU};
    const uint64_t bases[] = {0xB0000000ULL, 0xC0000000ULL};
    build_tables(segments, starts, ends, bases, 2U, false);

    assert(acpi_init(&arena[TEST_RSDP]));
    const struct nexus_acpi_topology *topology = acpi_get_topology();
    assert(topology != NULL && topology->mcfg_region_count == 2U);
    const struct nexus_acpi_mcfg_region *segment0 =
        acpi_find_mcfg_region(0U, 0x80U);
    const struct nexus_acpi_mcfg_region *segment2 =
        acpi_find_mcfg_region(2U, 0x20U);
    assert(segment0 != NULL && segment0->base_address == bases[0]);
    assert(segment2 != NULL && segment2->base_address == bases[1]);
    assert(segment2->start_bus == 0x20U && segment2->end_bus == 0x2FU);
    assert(acpi_find_mcfg_region(2U, 0x30U) == NULL);
    assert(acpi_find_mcfg_region(1U, 0x20U) == NULL);
}

static void test_overlapping_segment_windows_rejected(void) {
    const uint16_t segments[] = {3U, 3U};
    const uint8_t starts[] = {0x20U, 0x2FU};
    const uint8_t ends[] = {0x2FU, 0x30U};
    const uint64_t bases[] = {0xD0000000ULL, 0xD1000000ULL};
    build_tables(segments, starts, ends, bases, 2U, false);

    assert(acpi_init(&arena[TEST_RSDP]));
    const struct nexus_acpi_topology *topology = acpi_get_topology();
    assert(topology != NULL && topology->mcfg_region_count == 0U);
    assert(acpi_find_mcfg_region(3U, 0x20U) == NULL);
}

static void test_bad_checksum_and_unaligned_base_are_ignored(void) {
    const uint16_t segments[] = {4U};
    const uint8_t starts[] = {0U};
    const uint8_t ends[] = {0x3FU};
    const uint64_t bases[] = {0xE0000000ULL};
    build_tables(segments, starts, ends, bases, 1U, true);
    assert(acpi_init(&arena[TEST_RSDP]));
    assert(acpi_get_topology()->mcfg_region_count == 0U);

    const uint64_t unaligned[] = {0xE0001000ULL};
    build_tables(segments, starts, ends, unaligned, 1U, false);
    assert(acpi_init(&arena[TEST_RSDP]));
    assert(acpi_get_topology()->mcfg_region_count == 0U);
}

int main(void) {
    test_multiple_segments_and_bus_bounds();
    test_overlapping_segment_windows_rejected();
    test_bad_checksum_and_unaligned_base_are_ignored();
    puts("NEXUS ACPI MCFG TESTS PASSED");
    return 0;
}
