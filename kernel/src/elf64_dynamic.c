#include "elf64_dynamic.h"

#include <stddef.h>
#include <stdint.h>

#define ELF_HEADER_SIZE 64U
#define ELF_PROGRAM_HEADER_SIZE 56U
#define ELF_DYNAMIC_ENTRY_SIZE 16U
#define ELF_MAX_PROGRAM_HEADERS 64U
#define ELF_ET_DYN 3U
#define ELF_EM_X86_64 62U
#define ELF_PT_LOAD 1U
#define ELF_PT_DYNAMIC 2U
#define ELF_PT_INTERP 3U
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_STRTAB 5
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_JMPREL 23
#define DT_PLTREL 20
#define ELF_RELA_SIZE 24U

struct elf_header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum;
    uint16_t shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf_program_header {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

struct elf_dynamic_entry {
    int64_t tag;
    uint64_t value;
} __attribute__((packed));

static bool range_valid(uint64_t offset, uint64_t length, size_t total) {
    return offset <= (uint64_t)total &&
           length <= (uint64_t)total - offset;
}

static bool vaddr_to_file(const struct elf_program_header *loads,
                          uint16_t count, uint64_t address, uint64_t length,
                          uint64_t *offset) {
    if (offset == NULL) return false;
    for (uint16_t i = 0; i < count; ++i) {
        const struct elf_program_header *p = &loads[i];
        if (address < p->vaddr) continue;
        const uint64_t delta = address - p->vaddr;
        if (delta > p->filesz || length > p->filesz - delta) continue;
        if (p->offset > UINT64_MAX - delta) return false;
        *offset = p->offset + delta;
        return true;
    }
    return false;
}

bool nexus_elf64_dynamic_parse(const void *file, size_t file_size,
                               struct nexus_elf64_dynamic_info *info) {
    if (file == NULL || info == NULL || file_size < ELF_HEADER_SIZE)
        return false;
    const uint8_t *bytes = file;
    const struct elf_header *h = (const void *)bytes;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' ||
        h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 2U || h->ident[5] != 1U || h->ident[6] != 1U ||
        h->type != ELF_ET_DYN || h->machine != ELF_EM_X86_64 ||
        h->version != 1U || h->ehsize != ELF_HEADER_SIZE ||
        h->phentsize != ELF_PROGRAM_HEADER_SIZE || h->phnum == 0U ||
        h->phnum > ELF_MAX_PROGRAM_HEADERS ||
        (h->phoff & 7U) != 0U ||
        !range_valid(h->phoff, (uint64_t)h->phnum * h->phentsize,
                     file_size)) return false;

    struct elf_program_header loads[ELF_MAX_PROGRAM_HEADERS];
    uint16_t load_count = 0U;
    const struct elf_program_header *dynamic = NULL;
    struct nexus_elf64_dynamic_info parsed = {0};
    parsed.position_independent = true;
    for (uint16_t i = 0; i < h->phnum; ++i) {
        const struct elf_program_header *p = (const void *)(
            bytes + h->phoff + (uint64_t)i * h->phentsize);
        if (p->type == ELF_PT_LOAD) {
            if (p->filesz > p->memsz ||
                !range_valid(p->offset, p->filesz, file_size) ||
                p->vaddr > UINT64_MAX - p->memsz ||
                load_count >= ELF_MAX_PROGRAM_HEADERS) return false;
            loads[load_count++] = *p;
        } else if (p->type == ELF_PT_DYNAMIC) {
            if (dynamic != NULL || p->filesz == 0U ||
                p->filesz % ELF_DYNAMIC_ENTRY_SIZE != 0U ||
                !range_valid(p->offset, p->filesz, file_size)) return false;
            dynamic = p;
        } else if (p->type == ELF_PT_INTERP) {
            parsed.has_interpreter = true;
            if (!range_valid(p->offset, p->filesz, file_size) ||
                p->filesz == 0U ||
                bytes[p->offset + p->filesz - 1U] != 0U) return false;
        }
    }
    if (load_count == 0U || dynamic == NULL) return false;

    bool terminated = false;
    bool have_strtab = false, have_strsz = false;
    uint64_t strtab = 0U, strsz = 0U;
    uint64_t needed_offsets[NEXUS_ELF64_DYNAMIC_MAX_NEEDED] = {0U};
    for (uint64_t off = 0U; off < dynamic->filesz;
         off += ELF_DYNAMIC_ENTRY_SIZE) {
        const struct elf_dynamic_entry *entry =
            (const void *)(bytes + dynamic->offset + off);
        if (entry->tag == DT_NULL) { terminated = true; break; }
        switch (entry->tag) {
            case DT_NEEDED:
                if (parsed.needed_count >= NEXUS_ELF64_DYNAMIC_MAX_NEEDED)
                    return false;
                needed_offsets[parsed.needed_count++] = entry->value;
                break;
            case DT_STRTAB: strtab = entry->value; have_strtab = true; break;
            case DT_STRSZ: strsz = entry->value; have_strsz = true; break;
            case DT_RELA:
                parsed.rela_address = entry->value;
                parsed.has_rela = true;
                break;
            case DT_RELASZ: parsed.rela_size = entry->value; break;
            case DT_RELAENT: parsed.rela_entry_size = entry->value; break;
            case DT_REL: case DT_RELSZ: case DT_RELENT:
            case DT_JMPREL: case DT_PLTRELSZ: case DT_PLTREL:
                /* The first loader milestone supports RELA metadata only. */
                return false;
            default: break;
        }
    }
    if (!terminated) return false;

    if (parsed.needed_count != 0U) {
        uint64_t strtab_offset;
        if (!have_strtab || !have_strsz || strsz == 0U ||
            !vaddr_to_file(loads, load_count, strtab, strsz,
                           &strtab_offset) ||
            !range_valid(strtab_offset, strsz, file_size)) return false;
        for (uint32_t i = 0U; i < parsed.needed_count; ++i) {
            const uint64_t name_offset = needed_offsets[i];
            if (name_offset >= strsz) return false;
            bool name_terminated = false;
            for (uint64_t j = name_offset; j < strsz; ++j) {
                if (bytes[strtab_offset + j] == 0U) {
                    name_terminated = true;
                    break;
                }
            }
            if (!name_terminated) return false;
        }
    }
    if (parsed.has_rela) {
        uint64_t rela_offset;
        if (parsed.rela_entry_size != ELF_RELA_SIZE ||
            parsed.rela_size % ELF_RELA_SIZE != 0U ||
            !vaddr_to_file(loads, load_count, parsed.rela_address,
                           parsed.rela_size, &rela_offset) ||
            !range_valid(rela_offset, parsed.rela_size, file_size)) return false;
    }
    *info = parsed;
    return true;
}

struct elf_rela_entry {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

static bool prepare_relative_relocation(
    const struct elf_rela_entry *relocation,
    const struct elf_program_header *const *loads, uint16_t load_count,
    uint64_t virtual_base, size_t image_size, uint64_t load_bias,
    uint64_t *target_offset, uint64_t *relocated_value) {
    if (relocation == NULL || loads == NULL || target_offset == NULL ||
        relocated_value == NULL || (uint32_t)relocation->info != 8U ||
        (uint32_t)(relocation->info >> 32) != 0U ||
        relocation->offset < virtual_base ||
        relocation->offset > UINT64_MAX - sizeof(uint64_t)) return false;

    bool writable_target = false;
    for (uint16_t i = 0U; i < load_count; ++i) {
        const struct elf_program_header *load = loads[i];
        if ((load->flags & 2U) == 0U ||
            relocation->offset < load->vaddr ||
            relocation->offset + sizeof(uint64_t) >
                load->vaddr + load->memsz) continue;
        writable_target = true;
        break;
    }
    if (!writable_target) return false;

    *target_offset = relocation->offset - virtual_base;
    if (*target_offset > (uint64_t)image_size - sizeof(uint64_t))
        return false;

    if (relocation->addend >= 0) {
        const uint64_t addend = (uint64_t)relocation->addend;
        if (load_bias > UINT64_MAX - addend) return false;
        *relocated_value = load_bias + addend;
    } else {
        const uint64_t magnitude =
            (uint64_t)(-(relocation->addend + 1)) + 1U;
        if (load_bias < magnitude) return false;
        *relocated_value = load_bias - magnitude;
    }
    return true;
}

bool nexus_elf64_dynamic_apply_relative(const void *file, size_t file_size,
                                        uint64_t virtual_base,
                                        uint8_t *image, size_t image_size,
                                        uint64_t load_bias) {
    if (file == NULL || image == NULL || image_size < sizeof(uint64_t) ||
        virtual_base > UINT64_MAX - (uint64_t)image_size) return false;

    struct nexus_elf64_dynamic_info info = {0};
    if (!nexus_elf64_dynamic_parse(file, file_size, &info) ||
        !info.has_rela) return false;

    const uint8_t *bytes = file;
    const struct elf_header *header = (const void *)bytes;
    const struct elf_program_header *loads[ELF_MAX_PROGRAM_HEADERS];
    uint16_t load_count = 0U;
    const struct elf_program_header *dynamic = NULL;

    for (uint16_t i = 0U; i < header->phnum; ++i) {
        const struct elf_program_header *program = (const void *)(
            bytes + header->phoff + (uint64_t)i * header->phentsize);
        if (program->type == ELF_PT_LOAD) loads[load_count++] = program;
        if (program->type == ELF_PT_DYNAMIC) dynamic = program;
    }
    if (dynamic == NULL) return false;

    uint64_t rela_address = 0U;
    bool rela_found = false;
    for (uint64_t offset = 0U; offset < dynamic->filesz;
         offset += ELF_DYNAMIC_ENTRY_SIZE) {
        const struct elf_dynamic_entry *entry =
            (const void *)(bytes + dynamic->offset + offset);
        if (entry->tag == DT_NULL) break;
        if (entry->tag == DT_RELA) {
            rela_address = entry->value;
            rela_found = true;
        }
    }
    if (!rela_found) return false;

    uint64_t rela_file_offset = 0U;
    bool rela_mapped = false;
    for (uint16_t i = 0U; i < load_count; ++i) {
        const struct elf_program_header *load = loads[i];
        if (rela_address < load->vaddr) continue;
        const uint64_t delta = rela_address - load->vaddr;
        if (delta > load->filesz || info.rela_size > load->filesz - delta)
            continue;
        if (load->offset > UINT64_MAX - delta) return false;
        rela_file_offset = load->offset + delta;
        rela_mapped = true;
        break;
    }
    if (!rela_mapped ||
        !range_valid(rela_file_offset, info.rela_size, file_size)) return false;

    const uint64_t relocation_count = info.rela_size / ELF_RELA_SIZE;
    /* Preflight every relocation before modifying the loaded image. */
    for (uint64_t i = 0U; i < relocation_count; ++i) {
        const struct elf_rela_entry *relocation = (const void *)(
            bytes + rela_file_offset + i * ELF_RELA_SIZE);
        uint64_t target_offset, relocated_value;
        if (!prepare_relative_relocation(relocation, loads, load_count,
                                         virtual_base, image_size, load_bias,
                                         &target_offset, &relocated_value))
            return false;
    }

    for (uint64_t i = 0U; i < relocation_count; ++i) {
        const struct elf_rela_entry *relocation = (const void *)(
            bytes + rela_file_offset + i * ELF_RELA_SIZE);
        uint64_t target_offset, relocated_value;
        if (!prepare_relative_relocation(relocation, loads, load_count,
                                         virtual_base, image_size, load_bias,
                                         &target_offset, &relocated_value))
            return false;
        for (uint8_t byte = 0U; byte < 8U; ++byte)
            image[target_offset + byte] =
                (uint8_t)(relocated_value >> (byte * 8U));
    }
    return true;
}
