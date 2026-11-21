#ifndef NEXUS_FAT32_FORMAT_H
#define NEXUS_FAT32_FORMAT_H
#include <stdbool.h>
#include <stdint.h>
typedef bool (*nexus_fat32_write_fn)(void *,uint64_t,uint32_t,const void *);
typedef bool (*nexus_fat32_read_fn)(void *,uint64_t,uint32_t,void *);
typedef bool (*nexus_fat32_flush_fn)(void *);
struct nexus_fat32_format_target {
    uint64_t block_count;
    uint32_t block_size;
    uint32_t max_blocks_per_request;
    void *context;
    nexus_fat32_write_fn write;
    nexus_fat32_read_fn read;
    nexus_fat32_flush_fn flush;
};
enum nexus_fat32_format_result {
    NEXUS_FAT32_FORMAT_OK=0,
    NEXUS_FAT32_FORMAT_INVALID=1,
    NEXUS_FAT32_FORMAT_TOO_SMALL=2,
    NEXUS_FAT32_FORMAT_IO_ERROR=3,
    NEXUS_FAT32_FORMAT_VERIFY_ERROR=4
};
enum nexus_fat32_format_result nexus_fat32_format(
    const struct nexus_fat32_format_target *,uint64_t,uint64_t,uint32_t);
#endif
