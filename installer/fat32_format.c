#include "fat32_format.h"
#include <stddef.h>
#include <stdint.h>
#define RESERVED 32U
#define BACKUP_BOOT 6U
#define BACKUP_FSINFO 7U
#define ROOT_CLUSTER 2U
#define EOC 0x0FFFFFFFU
#define MIN_CLUSTERS 65525U
#define MAX_BLOCK_SIZE 4096U
#define MAX_TRANSFER 8U
static uint8_t workspace[MAX_BLOCK_SIZE*MAX_TRANSFER];
static uint8_t verify_workspace[MAX_BLOCK_SIZE];
static uint8_t readback_workspace[MAX_BLOCK_SIZE];
static bool busy;
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static bool add64(uint64_t a,uint64_t b,uint64_t *o){
    if(o==NULL||a>UINT64_MAX-b)return false;
    *o=a+b;return true;
}
static bool mul64(uint64_t a,uint64_t b,uint64_t *o){
    if(o==NULL||(b!=0U&&a>UINT64_MAX/b))return false;
    *o=a*b;return true;
}
static uint64_t ceil_div(uint64_t n,uint64_t d){return n/d+(n%d!=0U?1U:0U);}
static bool write_blocks(const struct nexus_fat32_format_target *t,
                         uint64_t lba,uint64_t count,const uint8_t *data){
    uint32_t limit=t->max_blocks_per_request<MAX_TRANSFER
        ?t->max_blocks_per_request:MAX_TRANSFER;
    while(count!=0U){
        uint32_t n=count>limit?limit:(uint32_t)count;
        uint64_t bytes;
        if(!t->write(t->context,lba,n,data)||
           !mul64(n,t->block_size,&bytes)||bytes>SIZE_MAX)return false;
        data+=(size_t)bytes;lba+=n;count-=n;
    }
    return true;
}
static bool verify_sector(const struct nexus_fat32_format_target *t,
                          uint64_t lba,const uint8_t *expected){
    if(!t->read(t->context,lba,1U,readback_workspace))return false;
    for(uint32_t i=0U;i<t->block_size;++i)
        if(readback_workspace[i]!=expected[i])return false;
    return true;
}
static void make_fsinfo(uint8_t *s,uint32_t bytes){
    for(uint32_t i=0U;i<bytes;++i)s[i]=0U;
    put32(s,0x41615252U);put32(s+484U,0x61417272U);
    put32(s+488U,0xFFFFFFFFU);put32(s+492U,3U);
    put32(s+508U,0xAA550000U);
}
static void make_boot(uint8_t *s,uint32_t bytes,uint32_t spc,
                      uint32_t fat_size,uint32_t total,uint32_t hidden,uint32_t volume_id){
    for(uint32_t i=0U;i<bytes;++i)s[i]=0U;
    s[0]=0xEBU;s[1]=0x58U;s[2]=0x90U;
    s[3]='N';s[4]='E';s[5]='X';s[6]='U';s[7]='S';s[8]='F';s[9]='A';s[10]='T';
    put16(s+11U,(uint16_t)bytes);s[13]=(uint8_t)spc;
    put16(s+14U,RESERVED);s[16]=2U;put16(s+17U,0U);
    put16(s+19U,0U);s[21]=0xF8U;put16(s+22U,0U);
    put16(s+24U,63U);put16(s+26U,255U);put32(s+28U,hidden);
    put32(s+32U,total);put32(s+36U,fat_size);
    put16(s+40U,0U);put16(s+42U,0U);put32(s+44U,ROOT_CLUSTER);
    put16(s+48U,1U);put16(s+50U,BACKUP_BOOT);
    s[64]=0x80U;s[66]=0x29U;put32(s+67U,volume_id);
    const char label[11]={'N','E','X','U','S',' ','O','S',' ',' ',' '};
    for(uint32_t i=0U;i<11U;++i)s[71U+i]=(uint8_t)label[i];
    const char type[8]={'F','A','T','3','2',' ',' ',' '};
    for(uint32_t i=0U;i<8U;++i)s[82U+i]=(uint8_t)type[i];
    s[510]=0x55U;s[511]=0xAAU;
}
enum nexus_fat32_format_result nexus_fat32_format(
    const struct nexus_fat32_format_target *t,uint64_t start,
    uint64_t blocks,uint32_t volume_id){
    if(busy)return NEXUS_FAT32_FORMAT_INVALID;
    if(t==NULL||t->write==NULL||t->read==NULL||t->flush==NULL||
       t->block_size<512U||t->block_size>MAX_BLOCK_SIZE||
       (t->block_size&(t->block_size-1U))!=0U||
       t->max_blocks_per_request==0U||blocks==0U)
        return NEXUS_FAT32_FORMAT_INVALID;
    uint64_t end;
    if(!add64(start,blocks,&end)||end>t->block_count)
        return NEXUS_FAT32_FORMAT_INVALID;
    if(blocks>UINT32_MAX)return NEXUS_FAT32_FORMAT_TOO_SMALL;
    const uint32_t spc=t->block_size<=512U?8U:
                       (t->block_size<=2048U?2U:1U);
    uint64_t fat_size=1U,clusters=0U;
    for(unsigned int i=0U;i<32U;++i){
        uint64_t overhead;
        if(!mul64(fat_size,2U,&overhead)||
           !add64(overhead,RESERVED,&overhead)||overhead>=blocks)
            return NEXUS_FAT32_FORMAT_TOO_SMALL;
        clusters=(blocks-overhead)/spc;
        const uint64_t required=ceil_div((clusters+2U)*4U,t->block_size);
        if(required==fat_size)break;
        fat_size=required;
    }
    uint64_t overhead;
    if(!mul64(fat_size,2U,&overhead)||
       !add64(overhead,RESERVED,&overhead)||overhead>=blocks)
        return NEXUS_FAT32_FORMAT_TOO_SMALL;
    clusters=(blocks-overhead)/spc;
    if(clusters<MIN_CLUSTERS||fat_size>UINT32_MAX)
        return NEXUS_FAT32_FORMAT_TOO_SMALL;
    const uint64_t fat1=start+RESERVED;
    const uint64_t fat2=fat1+fat_size;
    const uint64_t data_start=fat2+fat_size;
    busy=true;
    for(size_t i=0U;i<sizeof(workspace);++i)workspace[i]=0U;
    /* Invalidate both boot-sector copies before overwriting filesystem data. */
    if(!write_blocks(t,start,1U,workspace)||
       !write_blocks(t,start+BACKUP_BOOT,1U,workspace)||
       !t->flush(t->context))goto io_error;
    uint64_t remain=fat_size,cursor=fat1;
    while(remain!=0U){
        uint32_t n=remain>MAX_TRANSFER?MAX_TRANSFER:(uint32_t)remain;
        if(!write_blocks(t,cursor,n,workspace))goto io_error;
        cursor+=n;remain-=n;
    }
    remain=fat_size;cursor=fat2;
    while(remain!=0U){
        uint32_t n=remain>MAX_TRANSFER?MAX_TRANSFER:(uint32_t)remain;
        if(!write_blocks(t,cursor,n,workspace))goto io_error;
        cursor+=n;remain-=n;
    }
    put32(workspace,0x0FFFFFF8U);put32(workspace+4U,0xFFFFFFFFU);
    put32(workspace+8U,EOC);
    if(!write_blocks(t,fat1,1U,workspace)||
       !write_blocks(t,fat2,1U,workspace)||
       !write_blocks(t,data_start,spc,workspace))goto io_error;
    make_fsinfo(workspace,t->block_size);
    if(!write_blocks(t,start+1U,1U,workspace)||
       !write_blocks(t,start+BACKUP_FSINFO,1U,workspace))goto io_error;
    make_boot(workspace,t->block_size,spc,(uint32_t)fat_size,
              (uint32_t)blocks,start<=UINT32_MAX?(uint32_t)start:0U,volume_id);
    if(!write_blocks(t,start,1U,workspace)||
       !write_blocks(t,start+BACKUP_BOOT,1U,workspace)||
       !t->flush(t->context))goto io_error;
    make_boot(verify_workspace,t->block_size,spc,(uint32_t)fat_size,
              (uint32_t)blocks,start<=UINT32_MAX?(uint32_t)start:0U,volume_id);
    if(!verify_sector(t,start,verify_workspace)||
       !verify_sector(t,start+BACKUP_BOOT,verify_workspace))goto verify_error;
    make_fsinfo(verify_workspace,t->block_size);
    if(!verify_sector(t,start+1U,verify_workspace)||
       !verify_sector(t,start+BACKUP_FSINFO,verify_workspace))goto verify_error;
    for(uint32_t i=0U;i<t->block_size;++i)verify_workspace[i]=0U;
    put32(verify_workspace,0x0FFFFFF8U);
    put32(verify_workspace+4U,0xFFFFFFFFU);
    put32(verify_workspace+8U,EOC);
    if(!verify_sector(t,fat1,verify_workspace)||
       !verify_sector(t,fat2,verify_workspace))goto verify_error;
    for(uint32_t i=0U;i<t->block_size;++i)verify_workspace[i]=0U;
    if(!verify_sector(t,data_start,verify_workspace))goto verify_error;
    busy=false;return NEXUS_FAT32_FORMAT_OK;
io_error:
    busy=false;return NEXUS_FAT32_FORMAT_IO_ERROR;
verify_error:
    busy=false;return NEXUS_FAT32_FORMAT_VERIFY_ERROR;
}
