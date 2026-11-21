#include "fat32_file.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_SECTOR 4096U
#define MAX_FILE (1024U*1024U)
static uint8_t sector[MAX_SECTOR];
static uint8_t verify[MAX_SECTOR];
static bool busy;
static uint16_t le16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static bool read1(const struct nexus_fat32_format_target*t,uint64_t l,void*b){return t->read(t->context,l,1U,b);}
static bool write1(const struct nexus_fat32_format_target*t,uint64_t l,const void*b){return t->write(t->context,l,1U,b);}
static bool valid_name(const char*n,uint8_t out[11]){
 if(!n||!*n)return false;for(unsigned i=0;i<11;i++)out[i]=' ';
 unsigned base=0,ext=0;bool dot=false;
 for(unsigned i=0;n[i];i++){unsigned char c=(unsigned char)n[i];if(c=='.'){if(dot||base==0)return false;dot=true;continue;}
 if(c<33||c>126||c=='/'||c=='\\'||c==' ')return false;
 if(c>='a'&&c<='z')c=(unsigned char)(c-'a'+'A');
 if(!dot){if(base>=8)return false;out[base++]=c;}else{if(ext>=3)return false;out[8+ext++]=c;}}
 return base!=0;
}
enum nexus_fat32_file_result nexus_fat32_write_root_file(
 const struct nexus_fat32_format_target*t,uint64_t part,uint64_t count,
 const char*name,const void*data,uint32_t length){
 if(busy||!t||!t->read||!t->write||!t->flush||!name||
    (length&& !data)||length>MAX_FILE||t->block_size<512U||
    t->block_size>MAX_SECTOR||count<100U||part>t->block_count||
    count>t->block_count-part)return NEXUS_FAT32_FILE_INVALID;
 uint8_t shortname[11];if(!valid_name(name,shortname))return NEXUS_FAT32_FILE_INVALID;
 busy=true;enum nexus_fat32_file_result result=NEXUS_FAT32_FILE_IO;
 if(!read1(t,part,sector))goto done;
 uint32_t bps=le16(sector+11),spc=sector[13],reserved=le16(sector+14);
 uint32_t fats=sector[16],fat_size=le32(sector+36),root=le32(sector+44);
 uint32_t total=le32(sector+32);
 if(le16(sector+510)!=0xAA55U||bps!=t->block_size||!spc||
    (spc&(spc-1U))||!reserved||fats!=2U||!fat_size||root<2U||
    total>count||total==0U) {result=NEXUS_FAT32_FILE_INVALID;goto done;}
 uint64_t data_rel=(uint64_t)reserved+(uint64_t)fats*fat_size;
 if(data_rel>=total){result=NEXUS_FAT32_FILE_INVALID;goto done;}
 uint32_t clusters=(uint32_t)((total-data_rel)/spc);
 if(root-2U>=clusters){result=NEXUS_FAT32_FILE_INVALID;goto done;}
 uint64_t fat1=part+reserved,fat2=fat1+fat_size,data_start=part+data_rel;
 uint32_t root_lba=data_start+(root-2U)*spc;
 uint32_t root_slot=UINT32_MAX;
 /* Search the root directory's first cluster for a free entry or terminator. */
 for(uint32_t s=0;s<spc&&root_slot==UINT32_MAX;s++){
  if(!read1(t,(uint64_t)root_lba+s,sector))goto done;
  for(uint32_t off=0;off<bps;off+=32U){
   if(sector[off]==0U||sector[off]==0xE5U){root_slot=s*bps/32U+off/32U;break;}
   if(sector[off+11U]!=0x0FU&&sector[off+11U]!=0x08U&&
      __builtin_memcmp(sector+off,shortname,11U)==0){result=NEXUS_FAT32_FILE_EXISTS;goto done;}
  }
 }
 if(root_slot==UINT32_MAX){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 uint32_t cluster_bytes=spc*bps;
 uint32_t needed=length? (length+cluster_bytes-1U)/cluster_bytes:0U;
 if(needed>clusters-1U){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 /* Locate free clusters; this implementation uses sequential free clusters. */
 uint32_t found=0,first=0,previous=0;
 for(uint32_t c=3U;c<clusters+2U&&found<needed;c++){
  uint64_t fat_lba=fat1+((uint64_t)c*4U)/bps;uint32_t off=(c*4U)%bps;
  if(!read1(t,fat_lba,sector))goto done;
  if((le32(sector+off)&0x0FFFFFFFU)!=0U)continue;
  if(!first)first=c;
  if(previous){
   uint32_t poff=(previous*4U)%bps;uint64_t plba=fat1+((uint64_t)previous*4U)/bps;
   if(!read1(t,plba,sector))goto done;put32(sector+poff,c);
   if(!write1(t,plba,sector))goto done;
   uint64_t p2=fat2+((uint64_t)previous*4U)/bps;
   if(!read1(t,p2,sector))goto done;put32(sector+poff,c);
   if(!write1(t,p2,sector))goto done;
  }
  previous=c;found++;
 }
 if(found!=needed){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 if(needed){
  uint32_t off=(previous*4U)%bps;uint64_t lba=fat1+((uint64_t)previous*4U)/bps;
  if(!read1(t,lba,sector))goto done;put32(sector+off,0x0FFFFFFFU);
  if(!write1(t,lba,sector))goto done;
  lba=fat2+((uint64_t)previous*4U)/bps;
  if(!read1(t,lba,sector))goto done;put32(sector+off,0x0FFFFFFFU);
  if(!write1(t,lba,sector))goto done;
 }
 /* Mirror the first-cluster FAT link in both FAT copies. */
 if(needed){uint32_t off=(first*4U)%bps;uint64_t lba=fat1+((uint64_t)first*4U)/bps;
  if(!read1(t,lba,sector))goto done; /* preserve value already set by next-link update */
  if(!write1(t,lba,sector))goto done;
  lba=fat2+((uint64_t)first*4U)/bps;
  if(!read1(t,lba,sector)||!write1(t,lba,sector))goto done;(void)off;
 }
 /* Rewalk the allocation in ascending order and write file data. */
 uint32_t cluster=first,remaining=length;const uint8_t*src=data;
 for(uint32_t k=0;k<needed;k++){
  uint64_t base=data_start+(uint64_t)(cluster-2U)*spc;
  for(uint32_t sec=0;sec<spc;sec++){
   for(uint32_t j=0;j<bps;j++)sector[j]=0;
   uint32_t n=remaining>bps?bps:remaining;
   for(uint32_t j=0;j<n;j++)sector[j]=src[j];
   if(!write1(t,base+sec,sector))goto done;
   src+=n;remaining-=n;
  }
  /* Next cluster is found by scanning FAT for the first allocated successor. */
  uint32_t off=(cluster*4U)%bps;uint64_t lba=fat1+((uint64_t)cluster*4U)/bps;
  if(!read1(t,lba,sector))goto done;uint32_t next=le32(sector+off)&0x0FFFFFFFU;
  if(k+1U<needed){if(next<2U||next>=clusters+2U){result=NEXUS_FAT32_FILE_IO;goto done;}cluster=next;}
 }
 /* Commit one root entry after data and FAT are durable. */
 uint32_t dir_sector=root_slot/(bps/32U),dir_off=(root_slot%(bps/32U))*32U;
 if(!read1(t,(uint64_t)root_lba+dir_sector,sector))goto done;
 for(unsigned i=0;i<11;i++)sector[dir_off+i]=shortname[i];
 sector[dir_off+11U]=0x20U;put16(sector+dir_off+20U,(uint16_t)(first>>16));
 put16(sector+dir_off+26U,(uint16_t)first);put32(sector+dir_off+28U,length);
 if(!write1(t,(uint64_t)root_lba+dir_sector,sector)||!t->flush(t->context))goto done;
 if(!read1(t,(uint64_t)root_lba+dir_sector,verify))goto done;
 if(__builtin_memcmp(sector,verify,bps)!=0){result=NEXUS_FAT32_FILE_VERIFY;goto done;}
 result=NEXUS_FAT32_FILE_OK;
done:busy=false;return result;
}
