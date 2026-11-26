#ifndef NEXUS_WIN_PE_H
#define NEXUS_WIN_PE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WIN_PE_MAX_SECTIONS 96U
#define NEXUS_WIN_PE_MAX_IMPORT_MODULES 16U
#define NEXUS_WIN_PE_MODULE_NAME_MAX 64U
#define NEXUS_WIN_PE_MAX_NDIS_IMPORTS 64U
#define NEXUS_WIN_PE_SYMBOL_NAME_MAX 64U

enum nexus_win_wifi_driver_profile {
    NEXUS_WIN_WIFI_PROFILE_INVALID = 0,
    NEXUS_WIN_WIFI_PROFILE_NOT_NDIS,
    NEXUS_WIN_WIFI_PROFILE_LEGACY_OR_UNKNOWN_NDIS,
    NEXUS_WIN_WIFI_PROFILE_REQUIRES_WDF,
    NEXUS_WIN_WIFI_PROFILE_REQUIRES_NETADAPTERCX,
    NEXUS_WIN_WIFI_PROFILE_INCOMPLETE_IMPORTS,
    NEXUS_WIN_WIFI_PROFILE_UNSUPPORTED_RELOCATIONS,
    NEXUS_WIN_WIFI_PROFILE_NDIS6_CANDIDATE
};

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
    uint32_t relocation_count;
    char import_modules[NEXUS_WIN_PE_MAX_IMPORT_MODULES]
                       [NEXUS_WIN_PE_MODULE_NAME_MAX];
    char ndis_import_names[NEXUS_WIN_PE_MAX_NDIS_IMPORTS]
                          [NEXUS_WIN_PE_SYMBOL_NAME_MAX];
    uint32_t ndis_import_count;
    bool ndis_imports_truncated;
    bool ndis_v6_miniport;
    bool is_dll;
    bool nx_compatible;
    bool imports_ndis;
    bool imports_wdf;
    bool imports_net_adapter_cx;
    bool imports_windows_kernel;
    bool imports_hal;
    bool has_relocations;
    bool relocations_supported;
};

/* Inspects, but never maps or executes, a PE32+ AMD64 Windows image.
 * Rejects malformed/out-of-range headers and import tables.
 */
bool nexus_win_pe_inspect(const void *file_data, size_t file_size,
                          struct nexus_win_pe_info *info);

/* Classification only: NDIS6_CANDIDATE does not mean loadable yet. */
enum nexus_win_wifi_driver_profile
nexus_win_pe_classify_wifi_driver(const struct nexus_win_pe_info *info);

#endif
