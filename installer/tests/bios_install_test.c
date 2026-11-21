#include "bios_install.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DISK_BLOCKS 32768U
#define BIOS_FIRST 2048U
#define BIOS_BLOCKS 4096U
struct disk { uint8_t *bytes; uint32_t flushes; uint32_t writes; };
static bool rd(void *ctx,uint64_t lba,uint32_t n,void *buf){
 struct disk*d=ctx;if(lba>DISK_BLOCKS||n>DISK_BLOCKS-lba)return false;
 memcpy(buf,d->bytes+(size_t)lba*512U,(size_t)n*512U);return true;
}
static bool wr(void *ctx,uint64_t lba,uint32_t n,const void *buf){
 struct disk*d=ctx;if(lba>DISK_BLOCKS||n>DISK_BLOCKS-lba)return false;
 memcpy(d->bytes+(size_t)lba*512U,buf,(size_t)n*512U);d->writes++;return true;
}
static bool fl(void *ctx){((struct disk*)ctx)->flushes++;return true;}
int main(void){
 uint8_t storage[DISK_BLOCKS*512U];memset(storage,0,sizeof(storage));
 struct disk d={.bytes=storage};
 storage[510]=0x55U;storage[511]=0xAAU;
 for(unsigned i=440U;i<510U;i++)storage[i]=(uint8_t)(i^0x5aU);
 for(unsigned i=218U;i<224U;i++)storage[i]=(uint8_t)(i+7U);
 uint8_t image[512U+7000U];memset(image,0xA5,sizeof(image));
 image[0]=0xEBU;image[510]=0x55U;image[511]=0xAAU;
 struct nexus_bios_install_target t={DISK_BLOCKS,512U,8U,&d,rd,wr,fl};
 assert(nexus_bios_install_apply(&t,BIOS_FIRST,BIOS_BLOCKS,image,sizeof(image)));
 assert(d.flushes==2U&&d.writes>=3U);
 assert(memcmp(storage+(size_t)BIOS_FIRST*512U,image+512U,7000U)==0);
 assert(memcmp(storage+218U,image+218U,0U)==0);
 for(unsigned i=218U;i<224U;i++)assert(storage[i]==(uint8_t)(i+7U));
 for(unsigned i=440U;i<510U;i++)assert(storage[i]==(uint8_t)(i^0x5aU));
 uint64_t encoded=0U;for(unsigned i=0;i<8U;i++)encoded|=(uint64_t)storage[0x1a4U+i]<<(i*8U);
 assert(encoded==(uint64_t)BIOS_FIRST*512U);
 assert(!nexus_bios_install_apply(&t,BIOS_FIRST,1U,image,sizeof(image)));
 assert(!nexus_bios_install_apply(&t,DISK_BLOCKS,1U,image,sizeof(image)));
 assert(!nexus_bios_install_apply(&t,BIOS_FIRST,BIOS_BLOCKS,image,512U));
 image[511]=0U;assert(!nexus_bios_install_apply(&t,BIOS_FIRST,BIOS_BLOCKS,image,sizeof(image)));
 puts("NEXUS LIMINE BIOS INSTALLER TESTS PASSED");return 0;
}
