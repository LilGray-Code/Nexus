#include "elf64.h"

#define ELF64_HEADER_SIZE 64U
#define ELF64_PROGRAM_HEADER_SIZE 56U
#define ELF64_MAX_PROGRAM_HEADERS 64U
#define ELF64_PAGE_SIZE 4096ULL
#define ELF64_USER_MIN 0x0000000000010000ULL
#define ELF64_USER_TOP 0x0000800000000000ULL

#define ELF64_ET_EXEC 2U
#define ELF64_EM_X86_64 62U
#define ELF64_PT_LOAD 1U
#define ELF64_PF_X 0x1U
#define ELF64_PF_W 0x2U
#define ELF64_PF_R 0x4U

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

_Static_assert(sizeof(struct elf64_header) == ELF64_HEADER_SIZE,
               "ELF64 header layout must be 64 bytes");
_Static_assert(sizeof(struct elf64_program_header) == ELF64_PROGRAM_HEADER_SIZE,
               "ELF64 program header layout must be 56 bytes");

static bool is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool page_range(uint64_t address, uint64_t size,
                       uint64_t *start, uint64_t *end) {
    if (start == NULL || end == NULL || size == 0U ||
        address > UINT64_MAX - size) {
        return false;
    }
    const uint64_t byte_end = address + size;
    if (byte_end > UINT64_MAX - (ELF64_PAGE_SIZE - 1U)) return false;
    *start = address & ~(ELF64_PAGE_SIZE - 1U);
    *end = (byte_end + ELF64_PAGE_SIZE - 1U) &
           ~(ELF64_PAGE_SIZE - 1U);
    return *start < *end;
}

bool nexus_elf64_parse(const void *file, size_t file_size,
                       struct nexus_elf64_image *image) {
    if (file == NULL || image == NULL || file_size < ELF64_HEADER_SIZE) {
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)file;
    const struct elf64_header *header =
        (const struct elf64_header *)(const void *)bytes;
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2U || header->ident[5] != 1U ||
        header->ident[6] != 1U || header->type != ELF64_ET_EXEC ||
        header->machine != ELF64_EM_X86_64 || header->version != 1U ||
        header->header_size != ELF64_HEADER_SIZE ||
        header->program_header_entry_size != ELF64_PROGRAM_HEADER_SIZE ||
        header->program_header_count == 0U ||
        header->program_header_count > ELF64_MAX_PROGRAM_HEADERS) {
        return false;
    }

    const uint64_t phoff = header->program_header_offset;
    const uint64_t phnum = header->program_header_count;
    if (phoff > (uint64_t)file_size ||
        phnum > ((uint64_t)file_size - phoff) /
                ELF64_PROGRAM_HEADER_SIZE) {
        return false;
    }

    struct nexus_elf64_image parsed = {0};
    parsed.entry_point = header->entry;
    bool entry_is_executable = false;

    for (uint64_t i = 0; i < phnum; ++i) {
        const uint64_t offset = phoff + i * ELF64_PROGRAM_HEADER_SIZE;
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(const void *)(bytes + offset);
        if (program->type != ELF64_PT_LOAD) continue;

        if (parsed.segment_count >= NEXUS_ELF64_MAX_LOAD_SEGMENTS ||
            program->memory_size == 0U ||
            program->file_size > program->memory_size ||
            program->offset > (uint64_t)file_size ||
            program->file_size > (uint64_t)file_size - program->offset ||
            program->virtual_address < ELF64_USER_MIN ||
            program->memory_size > ELF64_USER_TOP ||
            program->virtual_address >
                ELF64_USER_TOP - program->memory_size ||
            (program->flags & ~(ELF64_PF_R | ELF64_PF_W | ELF64_PF_X)) != 0U ||
            (program->flags & (ELF64_PF_W | ELF64_PF_X)) ==
                (ELF64_PF_W | ELF64_PF_X)) {
            return false;
        }

        if (program->alignment > 1U) {
            if (!is_power_of_two(program->alignment) ||
                (program->virtual_address & (program->alignment - 1U)) !=
                (program->offset & (program->alignment - 1U))) {
                return false;
            }
        }

        uint64_t new_start;
        uint64_t new_end;
        if (!page_range(program->virtual_address, program->memory_size,
                        &new_start, &new_end)) {
            return false;
        }

        for (uint16_t j = 0; j < parsed.segment_count; ++j) {
            uint64_t old_start;
            uint64_t old_end;
            const struct nexus_elf64_segment *old = &parsed.segments[j];
            if (!page_range(old->virtual_address, old->memory_size,
                            &old_start, &old_end) ||
                (new_start < old_end && old_start < new_end)) {
                return false;
            }
        }

        struct nexus_elf64_segment *segment =
            &parsed.segments[parsed.segment_count++];
        segment->file_offset = program->offset;
        segment->virtual_address = program->virtual_address;
        segment->file_size = program->file_size;
        segment->memory_size = program->memory_size;
        segment->alignment = program->alignment;
        segment->flags = program->flags;

        const uint64_t segment_end =
            program->virtual_address + program->memory_size;
        if ((program->flags & ELF64_PF_X) != 0U &&
            parsed.entry_point >= program->virtual_address &&
            parsed.entry_point < segment_end) {
            entry_is_executable = true;
        }
    }

    if (parsed.segment_count == 0U || !entry_is_executable) return false;
    *image = parsed;
    return true;
}

bool nexus_elf64_selftest(void) {
    union {
        uint64_t alignment;
        uint8_t bytes[0x1100];
    } fixture;
    for (size_t i = 0; i < sizeof(fixture.bytes); ++i) {
        fixture.bytes[i] = 0;
    }

    struct elf64_header *header =
        (struct elf64_header *)(void *)fixture.bytes;
    header->ident[0] = 0x7FU;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = 2U;
    header->ident[5] = 1U;
    header->ident[6] = 1U;
    header->type = ELF64_ET_EXEC;
    header->machine = ELF64_EM_X86_64;
    header->version = 1U;
    header->entry = 0x401000ULL;
    header->program_header_offset = ELF64_HEADER_SIZE;
    header->header_size = ELF64_HEADER_SIZE;
    header->program_header_entry_size = ELF64_PROGRAM_HEADER_SIZE;
    header->program_header_count = 1U;

    struct elf64_program_header *program =
        (struct elf64_program_header *)(void *)(
            fixture.bytes + ELF64_HEADER_SIZE);
    program->type = ELF64_PT_LOAD;
    program->flags = ELF64_PF_R | ELF64_PF_X;
    program->offset = 0x1000ULL;
    program->virtual_address = 0x401000ULL;
    program->file_size = 4U;
    program->memory_size = 16U;
    program->alignment = ELF64_PAGE_SIZE;
    fixture.bytes[0x1000] = 0x90U;
    fixture.bytes[0x1001] = 0x90U;
    fixture.bytes[0x1002] = 0x90U;
    fixture.bytes[0x1003] = 0xC3U;

    struct nexus_elf64_image image;
    if (!nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image) ||
        image.entry_point != 0x401000ULL || image.segment_count != 1U ||
        image.segments[0].file_size != 4U ||
        image.segments[0].memory_size != 16U ||
        (image.segments[0].flags & NEXUS_ELF64_SEGMENT_EXEC) == 0U) {
        return false;
    }

    /* Bad magic. */
    header->ident[0] = 0;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    header->ident[0] = 0x7FU;

    /* File range extending beyond the supplied image. */
    program->offset = sizeof(fixture.bytes) - 2U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    program->offset = 0x1000ULL;

    /* Writable + executable mappings violate the W^X policy. */
    program->flags = ELF64_PF_R | ELF64_PF_W | ELF64_PF_X;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    program->flags = ELF64_PF_R | ELF64_PF_X;

    /* The entry point must belong to an executable load segment. */
    header->entry = 0x402000ULL;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    header->entry = 0x401000ULL;

    /* Program-header tables must be wholly contained in the input file. */
    header->program_header_offset = sizeof(fixture.bytes) - 8U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }

    return true;
}
