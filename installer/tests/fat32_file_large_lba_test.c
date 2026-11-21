#include "fat32_file.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PARTITION_LBA (0x100000000ULL + 2048ULL)
#define TOTAL_SECTORS 530000U
#define RESERVED_SECTORS 32U
#define FAT_SECTORS 520U
#define SECTOR_BYTES 512U
#define MAP_SLOTS 32U

struct sector_slot { uint64_t lba; uint8_t bytes[SECTOR_BYTES]; int used; };
struct sparse_disk { struct sector_slot slots[MAP_SLOTS]; };

static struct sector_slot *slot_for(struct sparse_disk *d,uint64_t lba,bool create) {
    struct sector_slot *free_slot=NULL;
    for(unsigned i=0;i<MAP_SLOTS;i++) {
        if(d->slots[i].used && d->slots[i].lba==lba) return &d->slots[i];
        if(!d->slots[i].used && free_slot==NULL) free_slot=&d->slots[i];
    }
    if(!create || free_slot==NULL) return NULL;
    free_slot->used=1;free_slot->lba=lba;
    memset(free_slot->bytes,0,sizeof(free_slot->bytes));
    return free_slot;
}
static bool read_blocks(void *ctx,uint64_t lba,uint32_t count,void *buffer) {
    struct sparse_disk *d=ctx;
    if(!buffer || count!=1U || lba<PARTITION_LBA ||
       lba>=PARTITION_LBA+TOTAL_SECTORS) return false;
    struct sector_slot *s=slot_for(d,lba,false);
    if(s) memcpy(buffer,s->bytes,SECTOR_BYTES);
    else memset(buffer,0,SECTOR_BYTES);
    return true;
}
static bool write_blocks(void *ctx,uint64_t lba,uint32_t count,const void *buffer) {
    struct sparse_disk *d=ctx;
    if(!buffer || count!=1U || lba<PARTITION_LBA ||
       lba>=PARTITION_LBA+TOTAL_SECTORS) return false;
    struct sector_slot *s=slot_for(d,lba,true);
    if(!s) return false;
    memcpy(s->bytes,buffer,SECTOR_BYTES);
    return true;
}
static bool flush_disk(void *ctx) { return ctx!=NULL; }
static void put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
int main(void) {
    static struct sparse_disk disk;
    const struct nexus_fat32_format_target target={
        .block_count=PARTITION_LBA+TOTAL_SECTORS+16U,
        .block_size=SECTOR_BYTES,.max_blocks_per_request=8U,
        .context=&disk,.write=write_blocks,.read=read_blocks,.flush=flush_disk
    };
    uint8_t boot[SECTOR_BYTES]={0};
    put16(boot+11U,SECTOR_BYTES);boot[13]=8U;
    put16(boot+14U,RESERVED_SECTORS);boot[16]=2U;
    put32(boot+32U,TOTAL_SECTORS);put32(boot+36U,FAT_SECTORS);
    put32(boot+44U,2U);boot[510]=0x55U;boot[511]=0xAAU;
    assert(write_blocks(&disk,PARTITION_LBA,1U,boot));

    static uint8_t payload[5000];
    for (uint32_t i=0;i<sizeof(payload);++i) payload[i]=(uint8_t)(i*37U+11U);
    assert(nexus_fat32_write_root_file(&target,PARTITION_LBA,TOTAL_SECTORS,
        "LIMINE.CFG",payload,sizeof(payload))==NEXUS_FAT32_FILE_OK);

    const uint64_t data_start=PARTITION_LBA+RESERVED_SECTORS+2ULL*FAT_SECTORS;
    const uint64_t root_lba=data_start;
    struct sector_slot *root=slot_for(&disk,root_lba,false);
    assert(root!=NULL);
    assert(memcmp(root->bytes,"LIMINE  CFG",11U)==0);
    const uint32_t cluster=(uint32_t)root->bytes[26]|
        ((uint32_t)root->bytes[27]<<8)|((uint32_t)root->bytes[20]<<16)|
        ((uint32_t)root->bytes[21]<<24);
    assert(cluster==3U);
    assert(cluster==3U);
    const uint64_t fat1=PARTITION_LBA+RESERVED_SECTORS;
    const uint64_t fat2=fat1+FAT_SECTORS;
    struct sector_slot *f1=slot_for(&disk,fat1,false);
    struct sector_slot *f2=slot_for(&disk,fat2,false);
    assert(f1!=NULL && f2!=NULL);
    assert((uint32_t)f1->bytes[12]|((uint32_t)f1->bytes[13]<<8)|
        ((uint32_t)f1->bytes[14]<<16)|((uint32_t)f1->bytes[15]<<24) == 4U);
    assert((uint32_t)f2->bytes[12]|((uint32_t)f2->bytes[13]<<8)|
        ((uint32_t)f2->bytes[14]<<16)|((uint32_t)f2->bytes[15]<<24) == 4U);
    for(uint32_t i=0;i<sizeof(payload);++i) {
        uint32_t cluster_index=i/(SECTOR_BYTES*8U);
        uint32_t in_cluster=i%(SECTOR_BYTES*8U);
        uint64_t lba=data_start+(uint64_t)(cluster+cluster_index-2U)*8U+
            in_cluster/SECTOR_BYTES;
        struct sector_slot *file=slot_for(&disk,lba,false);
        assert(file!=NULL && file->bytes[in_cluster%SECTOR_BYTES]==payload[i]);
    }
    puts("NEXUS FAT32 LARGE-LBA FILE WRITER TEST PASSED");
    return 0;
}
