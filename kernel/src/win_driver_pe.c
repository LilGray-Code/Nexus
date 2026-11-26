#include "win_driver_pe.h"

#define PE_DOS_HEADER_MIN 64U
#define PE_COFF_HEADER_SIZE 20U
#define PE_SECTION_HEADER_SIZE 40U
#define PE_IMPORT_DESCRIPTOR_SIZE 20U
#define PE_THUNK_SIZE 8U
#define PE_IMAGE_FILE_DLL 0x2000U
#define PE_DLLCHARACTERISTICS_NX_COMPAT 0x0100U
#define PE_DIRECTORY_IMPORT 1U
#define PE_DIRECTORY_ENTRY_SIZE 8U
#define PE_MAX_IMPORT_DESCRIPTORS 256U
#define PE_MAX_IMPORT_THUNKS 65536U

struct pe_view {
    const uint8_t *bytes;
    size_t size;
    uint32_t headers_size;
    uint32_t section_count;
    size_t section_offset;
};

static bool range_ok(size_t size, uint64_t offset, uint64_t length) {
    return offset <= (uint64_t)size && length <= (uint64_t)size - offset;
}

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4U) << 32);
}

static bool bounded_string(const struct pe_view *view, uint32_t offset,
                           char *output, size_t capacity) {
    if (view == NULL || output == NULL || capacity < 2U ||
        offset >= view->size) return false;
    size_t n = 0U;
    while ((size_t)offset + n < view->size && n + 1U < capacity &&
           view->bytes[(size_t)offset + n] != 0U) {
        const uint8_t c = view->bytes[(size_t)offset + n];
        if (c < 0x20U || c > 0x7EU) return false;
        output[n++] = (char)c;
    }
    if ((size_t)offset + n >= view->size ||
        view->bytes[(size_t)offset + n] != 0U || n == 0U) return false;
    output[n] = '\0';
    return true;
}

static bool rva_to_offset(const struct pe_view *view, uint32_t rva,
                          size_t length, size_t *offset) {
    if (view == NULL || offset == NULL) return false;
    if (rva < view->headers_size &&
        range_ok(view->size, rva, length)) {
        *offset = rva;
        return true;
    }
    for (uint32_t i = 0U; i < view->section_count; ++i) {
        const size_t at = view->section_offset +
                          (size_t)i * PE_SECTION_HEADER_SIZE;
        const uint32_t virtual_size = read32(view->bytes + at + 8U);
        const uint32_t virtual_address = read32(view->bytes + at + 12U);
        const uint32_t raw_size = read32(view->bytes + at + 16U);
        const uint32_t raw_offset = read32(view->bytes + at + 20U);
        const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        if (rva < virtual_address || (uint64_t)rva + length >
            (uint64_t)virtual_address + span) continue;
        const uint64_t delta = (uint64_t)rva - virtual_address;
        if (delta + length > raw_size ||
            !range_ok(view->size, (uint64_t)raw_offset + delta, length))
            return false;
        *offset = (size_t)((uint64_t)raw_offset + delta);
        return true;
    }
    return false;
}

static bool module_is(const char *module, const char *expected) {
    size_t i = 0U;
    while (module[i] != '\0' && expected[i] != '\0') {
        char a = module[i], b = expected[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        ++i;
    }
    return module[i] == '\0' && expected[i] == '\0';
}
static bool parse_imports(const struct pe_view *view, uint32_t import_rva,
                          uint32_t import_size,
                          struct nexus_win_pe_info *info) {
    if (import_rva == 0U && import_size == 0U) return true;
    if (import_rva == 0U || import_size < PE_IMPORT_DESCRIPTOR_SIZE ||
        import_size > 1024U * 1024U) return false;

    uint32_t consumed = 0U;
    bool terminated = false;
    while (consumed + PE_IMPORT_DESCRIPTOR_SIZE <= import_size &&
           info->import_module_count < PE_MAX_IMPORT_DESCRIPTORS) {
        size_t descriptor_offset;
        const uint64_t descriptor_rva = (uint64_t)import_rva + consumed;
        if (descriptor_rva > UINT32_MAX ||
            !rva_to_offset(view, (uint32_t)descriptor_rva,
                           PE_IMPORT_DESCRIPTOR_SIZE, &descriptor_offset))
            return false;
        const uint8_t *d = view->bytes + descriptor_offset;
        const uint32_t original_thunk = read32(d);
        const uint32_t name_rva = read32(d + 12U);
        const uint32_t first_thunk = read32(d + 16U);
        if (original_thunk == 0U && read32(d + 4U) == 0U &&
            read32(d + 8U) == 0U && name_rva == 0U && first_thunk == 0U) {
            terminated = true;
            break;
        }
        if (name_rva == 0U || first_thunk == 0U) return false;
        if (info->import_module_count >= NEXUS_WIN_PE_MAX_IMPORT_MODULES)
            return false;
        char *module = info->import_modules[info->import_module_count];
        size_t name_offset;
        if (!rva_to_offset(view, name_rva, 1U, &name_offset) ||
            !bounded_string(view, (uint32_t)name_offset, module,
                            NEXUS_WIN_PE_MODULE_NAME_MAX)) return false;

        const bool ndis_module = module_is(module, "ndis.sys");
        const uint32_t thunk_rva = original_thunk != 0U
            ? original_thunk : first_thunk;
        bool thunk_terminated = false;
        for (uint32_t i = 0U; i < PE_MAX_IMPORT_THUNKS; ++i) {
            const uint64_t current = (uint64_t)thunk_rva +
                                     (uint64_t)i * PE_THUNK_SIZE;
            if (current > UINT32_MAX) return false;
            size_t thunk_offset;
            if (!rva_to_offset(view, (uint32_t)current, PE_THUNK_SIZE,
                               &thunk_offset)) return false;
            const uint64_t value = read64(view->bytes + thunk_offset);
            if (value == 0U) { thunk_terminated = true; break; }
            char symbol[NEXUS_WIN_PE_SYMBOL_NAME_MAX] = {0};
            bool symbol_fits = false;
            if ((value & (UINT64_C(1) << 63)) == 0U) {
                if (value > UINT32_MAX) return false;
                size_t hint_offset;
                if (!rva_to_offset(view, (uint32_t)value, 3U,
                                   &hint_offset)) return false;
                const size_t name_at = hint_offset + 2U;
                size_t name_length = 0U;
                bool found_null = false;
                for (size_t k = name_at; k < view->size; ++k) {
                    if (view->bytes[k] == 0U) { found_null = true; break; }
                    if (view->bytes[k] < 0x20U || view->bytes[k] > 0x7EU)
                        return false;
                    ++name_length;
                }
                if (!found_null) return false;
                if (name_length < sizeof(symbol)) {
                    for (size_t k = 0U; k < name_length; ++k)
                        symbol[k] = (char)view->bytes[name_at + k];
                    symbol[name_length] = '\0';
                    symbol_fits = true;
                }
            }
            if (ndis_module && symbol_fits) {
                if (info->ndis_import_count < NEXUS_WIN_PE_MAX_NDIS_IMPORTS) {
                    char *destination = info->ndis_import_names[info->ndis_import_count++];
                    size_t k = 0U;
                    while (symbol[k] != '\0') { destination[k] = symbol[k]; ++k; }
                    destination[k] = '\0';
                    if (module_is(symbol, "NdisMRegisterMiniportDriver"))
                        info->ndis_v6_miniport = true;
                } else {
                    info->ndis_imports_truncated = true;
                }
            } else if (ndis_module && !symbol_fits &&
                       (value & (UINT64_C(1) << 63)) == 0U) {
                info->ndis_imports_truncated = true;
            }
            if (info->import_symbol_count == UINT32_MAX) return false;
            ++info->import_symbol_count;
        }
        if (!thunk_terminated) return false;
        if (module_is(module, "ndis.sys")) info->imports_ndis = true;
        if (module_is(module, "wdf01000.sys")) info->imports_wdf = true;
        if (module_is(module, "netadaptercx.sys"))
            info->imports_net_adapter_cx = true;
        if (module_is(module, "ntoskrnl.exe"))
            info->imports_windows_kernel = true;
        if (module_is(module, "hal.dll")) info->imports_hal = true;
        ++info->import_module_count;
        consumed += PE_IMPORT_DESCRIPTOR_SIZE;
    }
    return terminated;
}

static bool parse_relocations(const struct pe_view *view, uint32_t rva,
                              uint32_t size,
                              struct nexus_win_pe_info *info) {
    if (rva == 0U && size == 0U) {
        info->relocations_supported = true;
        return true;
    }
    if (rva == 0U || size < 8U || size > 4U * 1024U * 1024U) return false;
    uint32_t consumed = 0U;
    info->has_relocations = true;
    info->relocations_supported = true;
    while (consumed < size) {
        if (size - consumed < 8U) return false;
        const uint64_t block_rva = (uint64_t)rva + consumed;
        if (block_rva > UINT32_MAX) return false;
        size_t block_offset;
        if (!rva_to_offset(view, (uint32_t)block_rva, 8U, &block_offset))
            return false;
        const uint32_t page_rva = read32(view->bytes + block_offset);
        const uint32_t block_size = read32(view->bytes + block_offset + 4U);
        if ((page_rva & 0xFFFU) != 0U || block_size < 8U ||
            (block_size & 1U) != 0U || block_size > size - consumed)
            return false;
        const uint32_t entries = (block_size - 8U) / 2U;
        for (uint32_t i = 0U; i < entries; ++i) {
            size_t entry_offset;
            const uint64_t entry_rva = block_rva + 8U + (uint64_t)i * 2U;
            if (entry_rva > UINT32_MAX ||
                !rva_to_offset(view, (uint32_t)entry_rva, 2U, &entry_offset))
                return false;
            const uint16_t entry = read16(view->bytes + entry_offset);
            const uint16_t type = (uint16_t)(entry >> 12);
            const uint16_t displacement = (uint16_t)(entry & 0x0FFFU);
            if (type == 0U) continue;
            if (type == 10U) {
                const uint64_t target = (uint64_t)page_rva + displacement;
                size_t target_offset;
                if (target > UINT32_MAX ||
                    !rva_to_offset(view, (uint32_t)target, 8U, &target_offset))
                    return false;
                (void)target_offset;
                if (info->relocation_count == UINT32_MAX) return false;
                ++info->relocation_count;
            } else {
                info->relocations_supported = false;
            }
        }
        consumed += block_size;
    }
    return consumed == size;
}
bool nexus_win_pe_inspect(const void *file_data, size_t file_size,
                          struct nexus_win_pe_info *info) {
    if (file_data == NULL || info == NULL || file_size < PE_DOS_HEADER_MIN ||
        file_size > UINT32_MAX)
        return false;
    *info = (struct nexus_win_pe_info){0};
    const uint8_t *bytes = (const uint8_t *)file_data;
    if (read16(bytes) != 0x5A4DU) return false;
    const uint32_t pe_offset = read32(bytes + 0x3CU);
    if (!range_ok(file_size, pe_offset, 4U + PE_COFF_HEADER_SIZE) ||
        read32(bytes + pe_offset) != 0x00004550U) return false;

    const size_t coff = (size_t)pe_offset + 4U;
    const uint16_t machine = read16(bytes + coff);
    const uint16_t sections = read16(bytes + coff + 2U);
    const uint16_t optional_size = read16(bytes + coff + 16U);
    const uint16_t characteristics = read16(bytes + coff + 18U);
    if (machine != 0x8664U || sections == 0U ||
        sections > NEXUS_WIN_PE_MAX_SECTIONS ||
        (characteristics & PE_IMAGE_FILE_DLL) == 0U) return false;
    const size_t optional = coff + PE_COFF_HEADER_SIZE;
    if (optional_size < 128U ||
        !range_ok(file_size, optional, optional_size) ||
        read16(bytes + optional) != 0x020BU) return false;

    const uint32_t entry = read32(bytes + optional + 16U);
    const uint64_t image_base = read64(bytes + optional + 24U);
    const uint32_t section_alignment = read32(bytes + optional + 32U);
    const uint32_t file_alignment = read32(bytes + optional + 36U);
    const uint32_t image_size = read32(bytes + optional + 56U);
    const uint32_t headers_size = read32(bytes + optional + 60U);
    const uint16_t subsystem = read16(bytes + optional + 68U);
    const uint16_t dll_characteristics = read16(bytes + optional + 70U);
    const uint32_t directory_count = read32(bytes + optional + 108U);
    if (image_base == 0U || entry == 0U || section_alignment == 0U ||
        file_alignment == 0U || image_size == 0U ||
        headers_size == 0U || headers_size > file_size ||
        section_alignment < file_alignment ||
        (section_alignment & (section_alignment - 1U)) != 0U ||
        (file_alignment & (file_alignment - 1U)) != 0U ||
        directory_count < 2U ||
        optional_size < 112U + 2U * PE_DIRECTORY_ENTRY_SIZE)
        return false;

    const size_t section_offset = optional + optional_size;
    const size_t section_bytes = (size_t)sections * PE_SECTION_HEADER_SIZE;
    if (!range_ok(file_size, section_offset, section_bytes) ||
        section_offset + section_bytes > headers_size) return false;

    for (uint32_t i = 0U; i < sections; ++i) {
        const size_t at = section_offset + (size_t)i * PE_SECTION_HEADER_SIZE;
        const uint32_t virtual_size = read32(bytes + at + 8U);
        const uint32_t virtual_address = read32(bytes + at + 12U);
        const uint32_t raw_size = read32(bytes + at + 16U);
        const uint32_t raw_offset = read32(bytes + at + 20U);
        const uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;
        if ((raw_size != 0U &&
             (!range_ok(file_size, raw_offset, raw_size) ||
              raw_offset < headers_size || raw_offset % file_alignment != 0U)) ||
            virtual_address >= image_size ||
            (uint64_t)virtual_address + span > image_size ||
            virtual_address % section_alignment != 0U) return false;
    }

    struct pe_view view = {
        .bytes = bytes, .size = file_size, .headers_size = headers_size,
        .section_count = sections, .section_offset = section_offset
    };
    uint32_t relocation_rva = 0U, relocation_size = 0U;
    if (directory_count > 5U && optional_size >= 112U + 6U * PE_DIRECTORY_ENTRY_SIZE) {
        relocation_rva = read32(bytes + optional + 112U + 5U * PE_DIRECTORY_ENTRY_SIZE);
        relocation_size = read32(bytes + optional + 112U + 5U * PE_DIRECTORY_ENTRY_SIZE + 4U);
    }
    uint32_t import_rva = read32(bytes + optional + 112U +
                                 PE_DIRECTORY_IMPORT * PE_DIRECTORY_ENTRY_SIZE);
    uint32_t import_size = read32(bytes + optional + 112U +
                                  PE_DIRECTORY_IMPORT * PE_DIRECTORY_ENTRY_SIZE + 4U);
    info->machine = machine;
    info->section_count = sections;
    info->subsystem = subsystem;
    info->entry_point_rva = entry;
    info->image_size = image_size;
    info->section_alignment = section_alignment;
    info->file_alignment = file_alignment;
    info->is_dll = true;
    info->nx_compatible =
        (dll_characteristics & PE_DLLCHARACTERISTICS_NX_COMPAT) != 0U;
    if (entry >= image_size ||
        !parse_imports(&view, import_rva, import_size, info) ||
        !parse_relocations(&view, relocation_rva, relocation_size, info)) {
        *info = (struct nexus_win_pe_info){0};
        return false;
    }
    return true;
}
