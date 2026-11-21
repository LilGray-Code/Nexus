#ifndef NEXUS_FAT32_FILE_H
#define NEXUS_FAT32_FILE_H
#include "fat32_format.h"
enum nexus_fat32_file_result { NEXUS_FAT32_FILE_OK=0,NEXUS_FAT32_FILE_INVALID=1,NEXUS_FAT32_FILE_IO=2,NEXUS_FAT32_FILE_NO_SPACE=3,NEXUS_FAT32_FILE_EXISTS=4,NEXUS_FAT32_FILE_VERIFY=5 };
/* Creates bounded 8.3 EFI/BOOT directories and the limine.conf VFAT LFN.
 * Directories are single-cluster and fail closed when their entry area is full. */
enum nexus_fat32_file_result nexus_fat32_write_file(const struct nexus_fat32_format_target*,uint64_t,uint64_t,const char*,const void*,uint32_t);
enum nexus_fat32_file_result nexus_fat32_write_root_file(const struct nexus_fat32_format_target*,uint64_t,uint64_t,const char*,const void*,uint32_t);
#endif
