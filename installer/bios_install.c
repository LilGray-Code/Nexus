#include "bios_install.h"

#include <stddef.h>
#include <string.h>

#define BIOS_SECTOR_SIZE 512U
#define BIOS_MBR_STAGE2_OFFSET 0x1A4U
#define BIOS_IO_CHUNK_BYTES 4096U
#define BIOS_IO_CHUNK_BLOCKS (BIOS_IO_CHUNK_BYTES / BIOS_SECTOR_SIZE)

static void put64le(uint8_t *p,uint64_t v){
    for(unsigned i=0;i<8U;i++)p[i]=(uint8_t)(v>>(i*8U));
}

static bool range_ok(uint64_t first,uint64_t count,uint64_t limit){
    return count!=0U&&first<limit&&count<=limit-first;
}

bool nexus_bios_install_apply(const struct nexus_bios_install_target *t,
                              uint64_t first,uint64_t partition_blocks,
                              const void *image,uint32_t image_size){
    if(!t||!image||!t->read||!t->write||!t->flush||
       t->block_size!=BIOS_SECTOR_SIZE||t->max_blocks_per_request==0U||
       image_size<=BIOS_SECTOR_SIZE||
       !range_ok(first,partition_blocks,t->block_count)||
       partition_blocks<64U)return false;
    const uint8_t *src=(const uint8_t*)image;
    if(src[510]!=0x55U||src[511]!=0xAAU)return false;
    const uint64_t stage_bytes=(uint64_t)image_size-BIOS_SECTOR_SIZE;
    const uint64_t stage_blocks=(stage_bytes+BIOS_SECTOR_SIZE-1U)/BIOS_SECTOR_SIZE;
    if(stage_blocks>partition_blocks||first>UINT64_MAX/BIOS_SECTOR_SIZE)return false;
    const uint64_t stage_offset=first*BIOS_SECTOR_SIZE;
    if(stage_offset>UINT64_MAX-stage_bytes)return false;

    uint8_t original_mbr[BIOS_SECTOR_SIZE],new_mbr[BIOS_SECTOR_SIZE];
    if(!t->read(t->context,0U,1U,original_mbr))return false;
    if(original_mbr[510]!=0x55U||original_mbr[511]!=0xAAU)return false;
    memcpy(new_mbr,src,BIOS_SECTOR_SIZE);
    put64le(new_mbr+BIOS_MBR_STAGE2_OFFSET,stage_offset);
    /* Preserve BIOS disk timestamp and all disk/partition metadata. */
    memcpy(new_mbr+218U,original_mbr+218U,6U);
    memcpy(new_mbr+440U,original_mbr+440U,70U);

    uint8_t chunk[BIOS_IO_CHUNK_BYTES];
    uint64_t done=0U;
    while(done<stage_blocks){
        uint32_t n=(uint32_t)(stage_blocks-done);
        if(n>t->max_blocks_per_request)n=t->max_blocks_per_request;
        if(n>BIOS_IO_CHUNK_BLOCKS)n=BIOS_IO_CHUNK_BLOCKS;
        const uint64_t byte_off=done*BIOS_SECTOR_SIZE;
        const uint64_t remain=stage_bytes-byte_off;
        const size_t useful=(size_t)(remain<(uint64_t)n*BIOS_SECTOR_SIZE?
                                      remain:(uint64_t)n*BIOS_SECTOR_SIZE);
        memset(chunk,0,(size_t)n*BIOS_SECTOR_SIZE);
        memcpy(chunk,src+BIOS_SECTOR_SIZE+(size_t)byte_off,useful);
        if(!t->write(t->context,first+done,n,chunk))return false;
        done+=n;
    }
    if(!t->flush(t->context))return false;

    /* Read back stage 2 before making the MBR point at it. */
    done=0U;
    while(done<stage_blocks){
        uint32_t n=(uint32_t)(stage_blocks-done);
        if(n>t->max_blocks_per_request)n=t->max_blocks_per_request;
        if(!t->read(t->context,first+done,n,chunk))return false;
        const uint64_t byte_off=done*BIOS_SECTOR_SIZE;
        const uint64_t remain=stage_bytes-byte_off;
        const size_t useful=(size_t)(remain<(uint64_t)n*BIOS_SECTOR_SIZE?
                                      remain:(uint64_t)n*BIOS_SECTOR_SIZE);
        if(memcmp(chunk,src+BIOS_SECTOR_SIZE+(size_t)byte_off,useful)!=0)
            return false;
        done+=n;
    }

    if(!t->write(t->context,0U,1U,new_mbr)||!t->flush(t->context))return false;
    uint8_t verify[BIOS_SECTOR_SIZE];
    if(!t->read(t->context,0U,1U,verify))return false;
    return memcmp(verify,new_mbr,sizeof(verify))==0;
}
