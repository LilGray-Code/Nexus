#include "fat32_format.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DISK_BLOCKS ((uint64_t)16U*1024U*1024U*1024U/512U)
#define BASE 2048U
struct mock_disk{uint8_t first[128U*512U];uint64_t writes,flushes,fail_after;};
static bool mock_write(void *ctx,uint64_t lba,uint32_t n,const void *data){
    struct mock_disk *d=ctx;
    if(d->fail_after!=0U&&d->writes>=d->fail_after)return false;
    ++d->writes;
    if(lba>=BASE&&lba+n<=BASE+128U)
        memcpy(d->first+(size_t)(lba-BASE)*512U,data,(size_t)n*512U);
    return true;
}
static bool mock_read(void *ctx,uint64_t lba,uint32_t n,void *data){
    struct mock_disk *d=ctx;
    if(lba>=BASE&&lba+n<=BASE+128U){
        memcpy(data,d->first+(size_t)(lba-BASE)*512U,(size_t)n*512U);return true;
    }
    return false;
}
static bool mock_flush(void *ctx){((struct mock_disk *)ctx)->flushes++;return true;}
int main(void){
    struct mock_disk disk={0};
    struct nexus_fat32_format_target t={.block_count=DISK_BLOCKS,
        .block_size=512U,.max_blocks_per_request=8U,.context=&disk,
        .write=mock_write,.read=mock_read,.flush=mock_flush};
    const uint64_t part_blocks=8U*1024U*1024U*1024U/512U;
    assert(nexus_fat32_format(&t,BASE,part_blocks,0x12345678U)==NEXUS_FAT32_FORMAT_OK);
    const uint8_t *boot=disk.first;
    assert(boot[510]==0x55U&&boot[511]==0xAAU);
    assert(boot[82]=='F'&&boot[83]=='A'&&boot[84]=='T'&&boot[85]=='3'&&boot[86]=='2');
    assert(boot[13]==8U);
    assert(disk.first[6U*512U+510U]==0x55U);
    assert(disk.first[512U]==0x52U);
    assert(disk.first[32U*512U]==0xF8U);
    assert(disk.flushes==1U);
    assert(nexus_fat32_format(&t,0U,1024U,1U)==NEXUS_FAT32_FORMAT_TOO_SMALL);
    assert(nexus_fat32_format(&t,DISK_BLOCKS-10U,20U,1U)==NEXUS_FAT32_FORMAT_INVALID);
    disk.fail_after=disk.writes+1U;
    assert(nexus_fat32_format(&t,BASE,part_blocks,2U)==NEXUS_FAT32_FORMAT_IO_ERROR);
    puts("NEXUS FAT32 FORMAT TESTS PASSED");return 0;
}
