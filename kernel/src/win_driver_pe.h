#ifndef NEXUS_WIN_PE_H
#define NEXUS_WIN_PE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WIN_PE_MAX_SECTIONS 96U
#define NEXUS_WIN_PE_MAX_IMPORT_MODULES 16U
#define NEXUS_WIN_PE_MODULE_NAME_MAX 64U

struct nexus_win_pe_info {
    uint16_t machine;
    uint16_t section_count;
    uint16_t subsystem;
    uint32_t entry_point_rva;
    uint32_t image_size;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t import_module_count;
    uint32_t import_symbol_count;
    char import_modules[NEXUS_WIN_PE_MAX_IMPORT_MODULES]
                       [NEXUS_WIN_PE_MODULE_NAME_MAX];
    bool is_dll;
    bool nx_compatible;
};

/* Inspects, but never maps or executes, a PE32+ AMD64 Windows image.
 * Rejects malformed/out-of-range headers and import tables.
 */
bool nexus_win_pe_inspect(const void *file_data, size_t file_size,
                          struct nexus_win_pe_info *info);

#endif
