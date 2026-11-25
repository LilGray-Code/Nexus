#include "elf64.h"

#include "memory.h"
#include "paging.h"

#define ELF64_HEADER_SIZE 64U
#define ELF64_PROGRAM_HEADER_SIZE 56U
#define ELF64_MAX_PROGRAM_HEADERS 64U
#define ELF64_MAX_TOTAL_MEMORY (16ULL * 1024ULL * 1024ULL)
#define ELF64_PAGE_SIZE 4096ULL
#define ELF64_USER_MIN 0x0000000000010000ULL
#define ELF64_USER_TOP 0x0000800000000000ULL
#define ELF64_STACK_PAGE 0x00000000007FF000ULL
#define ELF64_STACK_TOP 0x0000000000800000ULL
#define ELF64_PIE_BASE 0x0000000040000000ULL
#define ELF64_MAX_DYNAMIC_BYTES 4096U
#define ELF64_RELA_ENTRY_SIZE 24U

#define ELF64_ET_EXEC 2U
#define ELF64_ET_DYN 3U
#define ELF64_EM_X86_64 62U
#define ELF64_PT_LOAD 1U
#define ELF64_PT_DYNAMIC 2U
#define ELF64_PT_INTERP 3U
#define ELF64_PT_TLS 7U
#define ELF64_PF_X 0x1U
#define ELF64_PF_W 0x2U
#define ELF64_PF_R 0x4U
#define ELF64_DT_NULL 0
#define ELF64_DT_NEEDED 1
#define ELF64_DT_HASH 4
#define ELF64_DT_STRTAB 5
#define ELF64_DT_SYMTAB 6
#define ELF64_DT_RELA 7
#define ELF64_DT_RELASZ 8
#define ELF64_DT_RELAENT 9
#define ELF64_DT_STRSZ 10
#define ELF64_DT_SYMENT 11
#define ELF64_DT_DEBUG 21
#define ELF64_DT_FLAGS 30
#define ELF64_DT_GNU_HASH 0x6FFFFEF5LL
#define ELF64_DT_RELACOUNT 0x6FFFFFF9LL
#define ELF64_DT_FLAGS_1 0x6FFFFFFBLL
#define ELF64_DT_REL 17
#define ELF64_DT_RELSZ 18
#define ELF64_DT_RELENT 19
#define ELF64_DT_PLTRELSZ 2
#define ELF64_DT_PLTREL 20
#define ELF64_DT_JMPREL 23
#define ELF64_DT_TEXTREL 22
#define ELF64_DT_INIT 12
#define ELF64_DT_FINI 13
#define ELF64_DT_INIT_ARRAY 25
#define ELF64_DT_FINI_ARRAY 26
#define ELF64_DT_PREINIT_ARRAY 32
#define ELF64_R_X86_64_RELATIVE 8U

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

struct elf64_dynamic_entry {
    int64_t tag;
    uint64_t value;
} __attribute__((packed));

struct elf64_rela_entry {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

_Static_assert(sizeof(struct elf64_header) == ELF64_HEADER_SIZE,
               "ELF64 header layout must be 64 bytes");
_Static_assert(sizeof(struct elf64_program_header) == ELF64_PROGRAM_HEADER_SIZE,
               "ELF64 program header layout must be 56 bytes");
_Static_assert(sizeof(struct elf64_dynamic_entry) == 16U,
               "ELF64 dynamic entry layout must be 16 bytes");
_Static_assert(sizeof(struct elf64_rela_entry) == ELF64_RELA_ENTRY_SIZE,
               "ELF64 RELA entry layout must be 24 bytes");

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

static bool elf64_file_offset_for_virtual(
    const struct nexus_elf64_image *image, uint64_t virtual_address,
    uint64_t length, uint64_t *file_offset) {
    if (image == NULL || file_offset == NULL || length == 0U) return false;
    for (uint16_t i = 0U; i < image->segment_count; ++i) {
        const struct nexus_elf64_segment *segment = &image->segments[i];
        if (virtual_address < segment->virtual_address) continue;
        const uint64_t offset = virtual_address - segment->virtual_address;
        if (offset > segment->file_size ||
            length > segment->file_size - offset ||
            segment->file_offset > UINT64_MAX - offset) continue;
        *file_offset = segment->file_offset + offset;
        return true;
    }
    return false;
}

static bool elf64_virtual_range_mapped(
    const struct nexus_elf64_image *image, uint64_t address,
    uint64_t length, bool require_writable, bool allow_one_past) {
    if (image == NULL || length > UINT64_MAX - address) return false;
    for (uint16_t i = 0U; i < image->segment_count; ++i) {
        const struct nexus_elf64_segment *segment = &image->segments[i];
        if (require_writable &&
            (segment->flags & NEXUS_ELF64_SEGMENT_WRITE) == 0U)
            continue;
        if (address < segment->virtual_address) continue;
        const uint64_t offset = address - segment->virtual_address;
        if (offset <= segment->memory_size &&
            length <= segment->memory_size - offset &&
            (length != 0U || allow_one_past || offset < segment->memory_size))
            return true;
    }
    return false;
}

static bool elf64_add_signed(uint64_t base, int64_t addend,
                             uint64_t *result) {
    if (result == NULL) return false;
    if (addend >= 0) {
        const uint64_t positive = (uint64_t)addend;
        if (base > UINT64_MAX - positive) return false;
        *result = base + positive;
        return true;
    }
    const uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1U;
    if (base < magnitude) return false;
    *result = base - magnitude;
    return true;
}

static bool elf64_supported_dynamic_tag(int64_t tag) {
    switch (tag) {
        case ELF64_DT_NULL:
        case ELF64_DT_HASH:
        case ELF64_DT_STRTAB:
        case ELF64_DT_SYMTAB:
        case ELF64_DT_RELA:
        case ELF64_DT_RELASZ:
        case ELF64_DT_RELAENT:
        case ELF64_DT_STRSZ:
        case ELF64_DT_SYMENT:
        case ELF64_DT_DEBUG:
        case ELF64_DT_FLAGS:
        case ELF64_DT_GNU_HASH:
        case ELF64_DT_RELACOUNT:
        case ELF64_DT_FLAGS_1:
            return true;
        default:
            return false;
    }
}

static bool elf64_validate_relative_relocations(
    const uint8_t *bytes, size_t file_size,
    struct nexus_elf64_image *image) {
    if (bytes == NULL || image == NULL ||
        image->relocation_count > NEXUS_ELF64_MAX_RELATIVE_RELOCATIONS)
        return false;
    for (uint32_t i = 0U; i < image->relocation_count; ++i) {
        const uint64_t offset = image->relocation_file_offset +
            (uint64_t)i * ELF64_RELA_ENTRY_SIZE;
        if (offset > (uint64_t)file_size ||
            ELF64_RELA_ENTRY_SIZE > (uint64_t)file_size - offset)
            return false;
        const struct elf64_rela_entry *rela =
            (const struct elf64_rela_entry *)(const void *)(bytes + offset);
        const uint32_t relocation_type = (uint32_t)rela->info;
        const uint32_t symbol_index = (uint32_t)(rela->info >> 32);
        if (relocation_type != ELF64_R_X86_64_RELATIVE ||
            symbol_index != 0U || (rela->offset & 7U) != 0U ||
            rela->offset > UINT64_MAX - image->load_bias)
            return false;

        const uint64_t target = image->load_bias + rela->offset;
        if (!elf64_virtual_range_mapped(image, target, sizeof(uint64_t),
                                        true, false))
            return false;

        uint64_t value = 0U;
        if (!elf64_add_signed(image->load_bias, rela->addend, &value) ||
            !elf64_virtual_range_mapped(image, value, 0U, false, true))
            return false;
    }
    return true;
}

static bool elf64_parse_dynamic(
    const uint8_t *bytes, size_t file_size,
    const struct elf64_program_header *dynamic,
    struct nexus_elf64_image *image) {
    if (bytes == NULL || dynamic == NULL || image == NULL ||
        dynamic->file_size < sizeof(struct elf64_dynamic_entry) ||
        dynamic->file_size > ELF64_MAX_DYNAMIC_BYTES ||
        (dynamic->file_size % sizeof(struct elf64_dynamic_entry)) != 0U ||
        dynamic->file_size > dynamic->memory_size ||
        dynamic->offset > (uint64_t)file_size ||
        dynamic->file_size > (uint64_t)file_size - dynamic->offset ||
        dynamic->virtual_address > UINT64_MAX - image->load_bias)
        return false;

    const uint64_t dynamic_virtual = dynamic->virtual_address +
                                     image->load_bias;
    uint64_t dynamic_file_offset = 0U;
    if (!elf64_file_offset_for_virtual(image, dynamic_virtual,
            dynamic->file_size, &dynamic_file_offset) ||
        dynamic_file_offset != dynamic->offset)
        return false;

    bool saw_null = false;
    bool have_rela = false;
    bool have_relasz = false;
    bool have_relaent = false;
    uint64_t rela_virtual = 0U;
    uint64_t rela_size = 0U;
    uint64_t rela_entry_size = 0U;
    uint64_t rela_count_hint = 0U;

    const uint64_t count = dynamic->file_size /
                           sizeof(struct elf64_dynamic_entry);
    for (uint64_t i = 0U; i < count; ++i) {
        const struct elf64_dynamic_entry *entry =
            (const struct elf64_dynamic_entry *)(const void *)(
                bytes + dynamic->offset +
                i * sizeof(struct elf64_dynamic_entry));
        if (!elf64_supported_dynamic_tag(entry->tag)) return false;
        if (entry->tag == ELF64_DT_NULL) {
            saw_null = true;
            break;
        }
        switch (entry->tag) {
            case ELF64_DT_NEEDED:
            case ELF64_DT_REL:
            case ELF64_DT_RELSZ:
            case ELF64_DT_RELENT:
            case ELF64_DT_PLTRELSZ:
            case ELF64_DT_PLTREL:
            case ELF64_DT_JMPREL:
            case ELF64_DT_TEXTREL:
            case ELF64_DT_INIT:
            case ELF64_DT_FINI:
            case ELF64_DT_INIT_ARRAY:
            case ELF64_DT_FINI_ARRAY:
            case ELF64_DT_PREINIT_ARRAY:
                return false;
            case ELF64_DT_RELA:
                if (have_rela) return false;
                have_rela = true;
                rela_virtual = entry->value;
                break;
            case ELF64_DT_RELASZ:
                if (have_relasz) return false;
                have_relasz = true;
                rela_size = entry->value;
                break;
            case ELF64_DT_RELAENT:
                if (have_relaent) return false;
                have_relaent = true;
                rela_entry_size = entry->value;
                break;
            case ELF64_DT_RELACOUNT:
                rela_count_hint = entry->value;
                break;
            case ELF64_DT_FLAGS:
                /* DF_TEXTREL is not accepted. */
                if ((entry->value & 0x4ULL) != 0U) return false;
                break;
            default:
                break;
        }
    }

    if (!saw_null) return false;
    if (!have_rela && !have_relasz && !have_relaent) return true;
    if (!have_rela || !have_relasz || !have_relaent ||
        rela_entry_size != ELF64_RELA_ENTRY_SIZE ||
        rela_size % ELF64_RELA_ENTRY_SIZE != 0U)
        return false;
    const uint64_t relocation_count =
        rela_size / ELF64_RELA_ENTRY_SIZE;
    if (relocation_count > NEXUS_ELF64_MAX_RELATIVE_RELOCATIONS ||
        rela_count_hint > relocation_count ||
        (rela_size == 0U && rela_virtual != 0U) ||
        (rela_size != 0U && rela_virtual > UINT64_MAX - image->load_bias))
        return false;
    if (relocation_count == 0U) return true;

    const uint64_t relocated_rela_virtual =
        image->load_bias + rela_virtual;
    uint64_t relocation_file_offset = 0U;
    if (!elf64_file_offset_for_virtual(image, relocated_rela_virtual,
            rela_size, &relocation_file_offset) ||
        relocation_file_offset > (uint64_t)file_size ||
        rela_size > (uint64_t)file_size - relocation_file_offset)
        return false;

    image->relocation_file_offset = relocation_file_offset;
    image->relocation_count = (uint32_t)relocation_count;
    return elf64_validate_relative_relocations(bytes, file_size, image);
}

bool nexus_elf64_parse(const void *file, size_t file_size,
                       struct nexus_elf64_image *image) {
    if (file == NULL || image == NULL || file_size < ELF64_HEADER_SIZE)
        return false;

    const uint8_t *bytes = (const uint8_t *)file;
    const struct elf64_header *header =
        (const struct elf64_header *)(const void *)bytes;
    const bool is_pie = header->type == ELF64_ET_DYN;
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2U || header->ident[5] != 1U ||
        header->ident[6] != 1U ||
        (header->type != ELF64_ET_EXEC && !is_pie) ||
        header->machine != ELF64_EM_X86_64 || header->version != 1U ||
        header->header_size != ELF64_HEADER_SIZE ||
        header->program_header_entry_size != ELF64_PROGRAM_HEADER_SIZE ||
        header->program_header_count == 0U ||
        header->program_header_count > ELF64_MAX_PROGRAM_HEADERS)
        return false;

    const uint64_t phoff = header->program_header_offset;
    const uint64_t phnum = header->program_header_count;
    if ((phoff & 7U) != 0U || phoff > (uint64_t)file_size ||
        phnum > ((uint64_t)file_size - phoff) /
                ELF64_PROGRAM_HEADER_SIZE)
        return false;

    struct nexus_elf64_image parsed = {0};
    parsed.position_independent = is_pie;
    parsed.load_bias = is_pie ? ELF64_PIE_BASE : 0U;
    if (header->entry > UINT64_MAX - parsed.load_bias) return false;
    parsed.entry_point = header->entry + parsed.load_bias;

    struct elf64_program_header dynamic_program = {0};
    bool have_dynamic = false;
    bool entry_is_executable = false;
    uint64_t total_memory_size = 0U;

    for (uint64_t i = 0U; i < phnum; ++i) {
        const uint64_t offset = phoff + i * ELF64_PROGRAM_HEADER_SIZE;
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(const void *)(bytes + offset);

        if (program->type == ELF64_PT_INTERP ||
            program->type == ELF64_PT_TLS)
            return false;
        if (program->type == ELF64_PT_DYNAMIC) {
            if (!is_pie || have_dynamic) return false;
            dynamic_program = *program;
            have_dynamic = true;
            continue;
        }
        if (program->type != ELF64_PT_LOAD) continue;

        if (program->memory_size > ELF64_MAX_TOTAL_MEMORY - total_memory_size ||
            parsed.segment_count >= NEXUS_ELF64_MAX_LOAD_SEGMENTS ||
            program->memory_size == 0U ||
            program->file_size > program->memory_size ||
            program->offset > (uint64_t)file_size ||
            program->file_size > (uint64_t)file_size - program->offset ||
            program->virtual_address >
                UINT64_MAX - parsed.load_bias ||
            program->memory_size > ELF64_USER_TOP ||
            program->virtual_address + parsed.load_bias < ELF64_USER_MIN ||
            program->virtual_address + parsed.load_bias >
                ELF64_USER_TOP - program->memory_size ||
            (program->flags & ~(ELF64_PF_R | ELF64_PF_W | ELF64_PF_X)) != 0U ||
            (program->flags & (ELF64_PF_W | ELF64_PF_X)) ==
                (ELF64_PF_W | ELF64_PF_X))
            return false;

        if (program->alignment > 1U) {
            if (!is_power_of_two(program->alignment) ||
                (program->virtual_address & (program->alignment - 1U)) !=
                (program->offset & (program->alignment - 1U)) ||
                (parsed.load_bias & (program->alignment - 1U)) != 0U)
                return false;
        }

        const uint64_t relocated_address =
            program->virtual_address + parsed.load_bias;
        uint64_t new_start, new_end;
        if (!page_range(relocated_address, program->memory_size,
                        &new_start, &new_end))
            return false;

        for (uint16_t j = 0U; j < parsed.segment_count; ++j) {
            uint64_t old_start, old_end;
            const struct nexus_elf64_segment *old = &parsed.segments[j];
            if (!page_range(old->virtual_address, old->memory_size,
                            &old_start, &old_end) ||
                (new_start < old_end && old_start < new_end))
                return false;
        }

        total_memory_size += program->memory_size;
        struct nexus_elf64_segment *segment =
            &parsed.segments[parsed.segment_count++];
        segment->file_offset = program->offset;
        segment->virtual_address = relocated_address;
        segment->file_size = program->file_size;
        segment->memory_size = program->memory_size;
        segment->alignment = program->alignment;
        segment->flags = program->flags;

        const uint64_t segment_end =
            relocated_address + program->memory_size;
        if ((program->flags & ELF64_PF_X) != 0U &&
            parsed.entry_point >= relocated_address &&
            parsed.entry_point < segment_end)
            entry_is_executable = true;
    }

    if (parsed.segment_count == 0U || !entry_is_executable ||
        (is_pie != have_dynamic))
        return false;
    if (is_pie &&
        !elf64_parse_dynamic(bytes, file_size, &dynamic_program, &parsed))
        return false;

    *image = parsed;
    return true;
}

static bool allocate_zeroed_user_page(uint64_t root_physical,
                                      uint64_t virtual_address,
                                      bool writable, bool executable,
                                      uint64_t *page_physical) {
    if (page_physical == NULL || !memory_alloc_page(page_physical)) {
        return false;
    }

    volatile uint8_t *page =
        (volatile uint8_t *)memory_phys_to_virt(*page_physical);
    if (page == NULL) {
        (void)memory_free_page(*page_physical);
        return false;
    }
    for (size_t i = 0; i < (size_t)ELF64_PAGE_SIZE; ++i) page[i] = 0;

    if (!paging_map_user_page(root_physical, virtual_address,
                              *page_physical, writable, executable)) {
        (void)memory_free_page(*page_physical);
        return false;
    }
    return true;
}

bool nexus_elf64_load(const void *file, size_t file_size,
                      uint64_t *root_physical, uint64_t *entry_point,
                      uint64_t *stack_pointer) {
    if (root_physical == NULL || entry_point == NULL ||
        stack_pointer == NULL) {
        return false;
    }
    *root_physical = 0U;
    *entry_point = 0U;
    *stack_pointer = 0U;

    struct nexus_elf64_image image;
    if (!nexus_elf64_parse(file, file_size, &image)) return false;

    for (uint16_t i = 0; i < image.segment_count; ++i) {
        uint64_t segment_start;
        uint64_t segment_end;
        if (!page_range(image.segments[i].virtual_address,
                        image.segments[i].memory_size,
                        &segment_start, &segment_end) ||
            (ELF64_STACK_PAGE < segment_end &&
             segment_start < ELF64_STACK_TOP)) {
            return false;
        }
    }

    uint64_t new_root = 0U;
    if (!paging_create_user_address_space(&new_root)) return false;

    const uint8_t *bytes = (const uint8_t *)file;
    for (uint16_t i = 0; i < image.segment_count; ++i) {
        const struct nexus_elf64_segment *segment = &image.segments[i];
        uint64_t first_page;
        uint64_t end_page;
        if (!page_range(segment->virtual_address, segment->memory_size,
                        &first_page, &end_page)) {
            goto load_failed;
        }

        const uint64_t file_virtual_end =
            segment->virtual_address + segment->file_size;
        for (uint64_t page_address = first_page;
             page_address < end_page;
             page_address += ELF64_PAGE_SIZE) {
            uint64_t page_physical;
            const bool writable =
                (segment->flags & NEXUS_ELF64_SEGMENT_WRITE) != 0U;
            const bool executable =
                (segment->flags & NEXUS_ELF64_SEGMENT_EXEC) != 0U;
            if (!allocate_zeroed_user_page(new_root, page_address,
                                           writable, executable,
                                           &page_physical)) {
                goto load_failed;
            }

            const uint64_t copy_start =
                page_address > segment->virtual_address
                    ? page_address : segment->virtual_address;
            const uint64_t page_end = page_address + ELF64_PAGE_SIZE;
            const uint64_t copy_end =
                page_end < file_virtual_end ? page_end : file_virtual_end;
            if (copy_start < copy_end) {
                const uint64_t source_offset =
                    segment->file_offset +
                    (copy_start - segment->virtual_address);
                const uint64_t destination_offset = copy_start - page_address;
                volatile uint8_t *destination =
                    (volatile uint8_t *)memory_phys_to_virt(page_physical);
                if (destination == NULL ||
                    source_offset > (uint64_t)file_size ||
                    copy_end - copy_start >
                        (uint64_t)file_size - source_offset) {
                    goto load_failed;
                }
                for (uint64_t byte = 0;
                     byte < copy_end - copy_start;
                     ++byte) {
                    destination[destination_offset + byte] =
                        bytes[source_offset + byte];
                }
            }
        }
    }

    /* Apply only validated, symbol-free R_X86_64_RELATIVE relocations. */
    for (uint32_t i = 0U; i < image.relocation_count; ++i) {
        const uint64_t relocation_offset = image.relocation_file_offset +
            (uint64_t)i * ELF64_RELA_ENTRY_SIZE;
        const struct elf64_rela_entry *rela =
            (const struct elf64_rela_entry *)(const void *)(
                bytes + relocation_offset);
        if (rela->offset > UINT64_MAX - image.load_bias) goto load_failed;
        const uint64_t target = image.load_bias + rela->offset;
        uint64_t relocated_value = 0U;
        if (!elf64_add_signed(image.load_bias, rela->addend,
                              &relocated_value) ||
            !paging_copy_to_address_space(new_root, target,
                &relocated_value, sizeof(relocated_value))) {
            goto load_failed;
        }
    }

    uint64_t stack_physical;
    if (!allocate_zeroed_user_page(new_root, ELF64_STACK_PAGE, true, false,
                                   &stack_physical)) {
        goto load_failed;
    }

    /*
     * A freestanding x86-64 entry point has function-entry alignment:
     * RSP % 16 == 8. Leave a zero sentinel return slot at the top of the
     * stack page so compiled _start functions see the standard alignment.
     * Nexus has no argc/argv/auxv startup block yet.
     */
    volatile uint8_t *stack_memory =
        (volatile uint8_t *)memory_phys_to_virt(stack_physical);
    if (stack_memory == NULL) goto load_failed;
    volatile uint64_t *return_slot = (volatile uint64_t *)(void *)(
        stack_memory + ELF64_PAGE_SIZE - sizeof(uint64_t));
    *return_slot = 0U;

    *root_physical = new_root;
    *entry_point = image.entry_point;
    *stack_pointer = ELF64_STACK_TOP - sizeof(uint64_t);
    return true;

load_failed:
    /* Reclaim every user page and intermediate table allocated so far. */
    (void)paging_destroy_user_address_space(new_root);
    return false;
}

static bool elf64_static_pie_selftest(void) {
    union {
        uint64_t alignment;
        uint8_t bytes[0x2100];
    } fixture;
    for (size_t i = 0U; i < sizeof(fixture.bytes); ++i)
        fixture.bytes[i] = 0U;

    struct elf64_header *header =
        (struct elf64_header *)(void *)fixture.bytes;
    header->ident[0] = 0x7FU;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = 2U;
    header->ident[5] = 1U;
    header->ident[6] = 1U;
    header->type = ELF64_ET_DYN;
    header->machine = ELF64_EM_X86_64;
    header->version = 1U;
    header->entry = 8U;
    header->program_header_offset = ELF64_HEADER_SIZE;
    header->header_size = ELF64_HEADER_SIZE;
    header->program_header_entry_size = ELF64_PROGRAM_HEADER_SIZE;
    header->program_header_count = 3U;

    struct elf64_program_header *program =
        (struct elf64_program_header *)(void *)(
            fixture.bytes + ELF64_HEADER_SIZE);
    program[0].type = ELF64_PT_LOAD;
    program[0].flags = ELF64_PF_R | ELF64_PF_X;
    program[0].offset = 0x1000U;
    program[0].virtual_address = 0U;
    program[0].file_size = 16U;
    program[0].memory_size = 16U;
    program[0].alignment = ELF64_PAGE_SIZE;

    program[1].type = ELF64_PT_LOAD;
    program[1].flags = ELF64_PF_R | ELF64_PF_W;
    program[1].offset = 0x2000U;
    program[1].virtual_address = 0x1000U;
    program[1].file_size = 0x58U;
    program[1].memory_size = 0x100U;
    program[1].alignment = ELF64_PAGE_SIZE;

    program[2].type = ELF64_PT_DYNAMIC;
    program[2].offset = 0x2000U;
    program[2].virtual_address = 0x1000U;
    program[2].file_size = 4U * sizeof(struct elf64_dynamic_entry);
    program[2].memory_size = program[2].file_size;
    program[2].alignment = 8U;

    struct elf64_dynamic_entry *dynamic =
        (struct elf64_dynamic_entry *)(void *)(fixture.bytes + 0x2000U);
    dynamic[0].tag = ELF64_DT_RELA;
    dynamic[0].value = 0x1040U;
    dynamic[1].tag = ELF64_DT_RELASZ;
    dynamic[1].value = sizeof(struct elf64_rela_entry);
    dynamic[2].tag = ELF64_DT_RELAENT;
    dynamic[2].value = sizeof(struct elf64_rela_entry);
    dynamic[3].tag = ELF64_DT_NULL;
    dynamic[3].value = 0U;

    struct elf64_rela_entry *rela =
        (struct elf64_rela_entry *)(void *)(fixture.bytes + 0x2040U);
    rela->offset = 0x1080U;
    rela->info = ELF64_R_X86_64_RELATIVE;
    rela->addend = 0x1010;

    struct nexus_elf64_image image;
    if (!nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image) ||
        !image.position_independent ||
        image.load_bias != ELF64_PIE_BASE ||
        image.entry_point != ELF64_PIE_BASE + 8U ||
        image.segment_count != 2U ||
        image.relocation_count != 1U ||
        image.relocation_file_offset != 0x2040U ||
        image.segments[0].virtual_address != ELF64_PIE_BASE ||
        image.segments[1].virtual_address != ELF64_PIE_BASE + 0x1000U)
        return false;

    /* Symbol-based relocations are not supported by this static PIE ABI. */
    rela->info = (1ULL << 32) | ELF64_R_X86_64_RELATIVE;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image))
        return false;
    rela->info = ELF64_R_X86_64_RELATIVE;

    /* Relocation destinations must be writable mapped memory. */
    rela->offset = 8U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image))
        return false;
    rela->offset = 0x1080U;

    /* Relocated pointer values must remain inside this image's mapped range. */
    rela->addend = -(int64_t)ELF64_PIE_BASE;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image))
        return false;
    rela->addend = 0x1010;

    /* External dependencies are rejected, not silently left unresolved. */
    dynamic[0].tag = ELF64_DT_NEEDED;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image))
        return false;
    dynamic[0].tag = ELF64_DT_RELA;

    /* A PIE needing an interpreter is outside the supported execution ABI. */
    program[2].type = ELF64_PT_INTERP;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image))
        return false;

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
    header->program_header_offset = ELF64_HEADER_SIZE;

    /* Segment alignment must be a power of two with matching offsets. */
    program->alignment = 3U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    program->alignment = ELF64_PAGE_SIZE;

    /* Two load segments may not share a page with different protections. */
    header->program_header_count = 2U;
    struct elf64_program_header *second =
        (struct elf64_program_header *)(void *)(
            fixture.bytes + ELF64_HEADER_SIZE + ELF64_PROGRAM_HEADER_SIZE);
    *second = *program;
    second->offset = 0x1004U;
    second->virtual_address = 0x401800ULL;
    second->alignment = 1U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }

    /* Reject tiny files that request excessive zero-filled process memory. */
    program->memory_size = ELF64_MAX_TOTAL_MEMORY + 1U;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    program->memory_size = 16U;

    /* Static executables must not request a dynamic linker or TLS runtime. */
    header->program_header_count = 2U;
    second->type = ELF64_PT_INTERP;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    second->type = ELF64_PT_DYNAMIC;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }
    second->type = ELF64_PT_TLS;
    if (nexus_elf64_parse(fixture.bytes, sizeof(fixture.bytes), &image)) {
        return false;
    }

    return elf64_static_pie_selftest();
}
