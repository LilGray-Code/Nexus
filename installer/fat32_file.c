#include "fat32_file.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_SECTOR 4096U
#define MAX_FILE (2U*1024U*1024U)
static uint8_t sector[MAX_SECTOR];
static uint8_t verify[MAX_SECTOR];
static bool busy;
#define MAX_FILE_CLUSTERS (MAX_FILE/512U)
static uint32_t allocated[MAX_FILE_CLUSTERS];
static uint16_t le16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int bytes_compare(const uint8_t*a,const uint8_t*b,size_t n){for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 1;return 0;}
static bool read1(const struct nexus_fat32_format_target*t,uint64_t l,void*b){return t->read(t->context,l,1U,b);}
static bool write1(const struct nexus_fat32_format_target*t,uint64_t l,const void*b){return t->write(t->context,l,1U,b);}

static bool fat_set(const struct nexus_fat32_format_target*t,uint64_t fat1,
 uint64_t fat2,uint32_t bps,uint32_t cluster,uint32_t value){
 uint64_t lba=fat1+((uint64_t)cluster*4U)/bps;
 uint32_t off=(uint32_t)(((uint64_t)cluster*4U)%bps);
 if(!read1(t,lba,sector))return false;
 put32(sector+off,(le32(sector+off)&0xF0000000U)|(value&0x0FFFFFFFU));
 if(!write1(t,lba,sector))return false;
 lba=fat2+((uint64_t)cluster*4U)/bps;
 if(!read1(t,lba,sector))return false;
 put32(sector+off,(le32(sector+off)&0xF0000000U)|(value&0x0FFFFFFFU));
 return write1(t,lba,sector);
}
static bool directory_child(const struct nexus_fat32_format_target*t,
 uint64_t fat1,uint64_t fat2,uint64_t data_start,uint32_t bps,
 uint32_t spc,uint32_t clusters,uint32_t root,uint32_t parent,const uint8_t name[11],
 uint32_t*out){
 if(parent<2U||parent>=clusters+2U||!out)return false;
 uint64_t dir_lba=data_start+(uint64_t)(parent-2U)*spc;
 uint32_t free_sector=UINT32_MAX,free_off=0;
 for(uint32_t s=0;s<spc;s++){
  if(!read1(t,dir_lba+s,sector))return false;
  for(uint32_t off=0;off<bps;off+=32U){
   if(sector[off]==0U){
    if(free_sector==UINT32_MAX){free_sector=s;free_off=off;}
    s=spc;break;
   }
   if(sector[off]==0xE5U){
    if(free_sector==UINT32_MAX){free_sector=s;free_off=off;}
    continue;
   }
   if(sector[off+11U]==0x0FU)continue;
   if(bytes_compare(sector+off,name,11U)==0){
    if((sector[off+11U]&0x10U)==0U)return false;
    *out=(uint32_t)le16(sector+off+26U)|
         ((uint32_t)le16(sector+off+20U)<<16);
    return *out>=2U&&*out<clusters+2U;
   }
  }
 }
 if(free_sector==UINT32_MAX)return false;
 uint32_t child=0;
 for(uint32_t candidate=3U;candidate<clusters+2U;candidate++){
  uint64_t lba=fat1+((uint64_t)candidate*4U)/bps;
  uint32_t off=(uint32_t)(((uint64_t)candidate*4U)%bps);
  if(!read1(t,lba,sector))return false;
  if((le32(sector+off)&0x0FFFFFFFU)==0U){child=candidate;break;}
 }
 if(!child||!fat_set(t,fat1,fat2,bps,child,0x0FFFFFFFU))return false;
 /* Initialize the complete directory cluster before publishing its parent entry. */
 uint64_t child_lba=data_start+(uint64_t)(child-2U)*spc;
 for(uint32_t s=0;s<spc;s++){
  for(uint32_t i=0;i<bps;i++)sector[i]=0;
  if(s==0U){
   sector[0]='.';for(unsigned i=1;i<11;i++)sector[i]=' ';
   sector[11]=0x10U;put16(sector+20U,(uint16_t)(child>>16));
   put16(sector+26U,(uint16_t)child);
   sector[32]='.';sector[33]='.';
   for(unsigned i=34;i<43;i++)sector[i]=' ';
   sector[43]=0x10U;
   uint32_t parent_cluster=parent==root?0U:parent;
   put16(sector+52U,(uint16_t)(parent_cluster>>16));
   put16(sector+58U,(uint16_t)parent_cluster);
  }
  if(!write1(t,child_lba+s,sector))return false;
 }
 if(!t->flush(t->context))return false;
 if(!read1(t,dir_lba+free_sector,sector))return false;
 for(unsigned i=0;i<11;i++)sector[free_off+i]=name[i];
 sector[free_off+11U]=0x10U;
 put16(sector+free_off+20U,(uint16_t)(child>>16));
 put16(sector+free_off+26U,(uint16_t)child);
 put32(sector+free_off+28U,0U);
 if(!write1(t,dir_lba+free_sector,sector)||!t->flush(t->context))return false;
 if(!read1(t,dir_lba+free_sector,verify)||
    bytes_compare(sector,verify,bps)!=0)return false;
 *out=child;return true;
}
static bool valid_name(const char*n,uint8_t out[11]);
static uint8_t lfn_checksum(const uint8_t name[11]){
 uint8_t sum=0;
 for(unsigned i=0;i<11;i++)sum=(uint8_t)(((sum&1U)?0x80U:0U)+(sum>>1)+name[i]);
 return sum;
}
static bool long_name_info(const char*n,uint8_t alias[11],
                            const char **long_name,uint32_t *lfn_count){
 static const char *const names[]={
  "limine.conf","limine-bios.sys","limine-bios-hdd.bin"
 };
 static const char *const aliases[]={
  "LIMINE~1.CNF","LIMINE~1.SYS","LIMINE~1.BIN"
 };
 if(!n||!long_name||!lfn_count)return false;
 for(size_t k=0U;k<sizeof(names)/sizeof(names[0]);k++){
  size_t i=0U;
  while(n[i]&&names[k][i]){
   char ch=n[i];if(ch>='A'&&ch<='Z')ch=(char)(ch-'A'+'a');
   if(ch!=names[k][i])break;
   i++;
  }
  if(n[i]!='\0'||names[k][i]!='\0')continue;
  if(!valid_name(aliases[k],alias))return false;
  *long_name=names[k];
  size_t len=0U;while(names[k][len])len++;
  *lfn_count=(uint32_t)((len+1U+12U)/13U);
  return *lfn_count>0U&&*lfn_count<=2U;
 }
 return false;
}
static bool valid_name(const char*n,uint8_t out[11]){
 if(!n||!*n)return false;
 for(unsigned i=0;i<11;i++)out[i]=' ';
 unsigned base=0,ext=0;bool dot=false;
 for(unsigned i=0;n[i];i++){unsigned char c=(unsigned char)n[i];if(c=='.'){if(dot||base==0)return false;dot=true;continue;}
 if(c<33||c>126||c=='/'||c=='\\'||c==' ')return false;
 if(c>='a'&&c<='z')c=(unsigned char)(c-'a'+'A');
 if(!dot){if(base>=8)return false;out[base++]=c;}else{if(ext>=3)return false;out[8+ext++]=c;}}
 return base!=0;
}
enum nexus_fat32_file_result nexus_fat32_write_file(
 const struct nexus_fat32_format_target*t,uint64_t part,uint64_t count,
 const char*name,const void*data,uint32_t length){
 if(busy||!t||!t->read||!t->write||!t->flush||!name||
    (length&& !data)||length>MAX_FILE||t->block_size<512U||
    t->block_size>MAX_SECTOR||count<100U||part>t->block_count||
    count>t->block_count-part)return NEXUS_FAT32_FILE_INVALID;
 uint8_t shortname[11];
 if(!name||!*name||name[0]=='/')return NEXUS_FAT32_FILE_INVALID;
 busy=true;enum nexus_fat32_file_result result=NEXUS_FAT32_FILE_IO;
 if(!read1(t,part,sector))goto done;
 uint32_t bps=le16(sector+11),spc=sector[13],reserved=le16(sector+14);
 uint32_t fats=sector[16],fat_size=le32(sector+36),root=le32(sector+44);
 uint32_t total=le32(sector+32);
 if(le16(sector+510)!=0xAA55U||bps!=t->block_size||
    (bps!=512U&&bps!=4096U)||bps%32U!=0U||!spc||spc>128U||
    (spc&(spc-1U))||!reserved||fats!=2U||!fat_size||root<2U||
    total>count||total==0U||(uint64_t)spc*bps>65536U) {
  result=NEXUS_FAT32_FILE_INVALID;goto done;
 }
 uint64_t data_rel=(uint64_t)reserved+(uint64_t)fats*fat_size;
 if(data_rel>=total){result=NEXUS_FAT32_FILE_INVALID;goto done;}
 uint32_t clusters=(uint32_t)((total-data_rel)/spc);
 if((uint64_t)fat_size*bps/4U<(uint64_t)clusters+2U) {
  result=NEXUS_FAT32_FILE_INVALID;goto done;
 }
 if(root-2U>=clusters){result=NEXUS_FAT32_FILE_INVALID;goto done;}
 uint64_t fat1=part+reserved,fat2=fat1+fat_size,data_start=part+data_rel;
 /* Partition LBAs are 64-bit: large GPT disks can place the ESP above 2 TiB. */
 uint32_t parent=root;bool has_lfn=false;
 const char *lfn_name=NULL;uint32_t lfn_entries=0U;
 const char*component=name;
 for(;;){
  const char*slash=component;
  while(*slash&&*slash!='/')slash++;
  if(*slash=='/'){
   size_t n=(size_t)(slash-component);
   char partname[13];uint8_t shortpart[11];
   if(n==0U||n>12U){result=NEXUS_FAT32_FILE_INVALID;goto done;}
   for(size_t i=0;i<n;i++)partname[i]=component[i];
   partname[n]='\0';
   if(!valid_name(partname,shortpart)){result=NEXUS_FAT32_FILE_INVALID;goto done;}
   uint32_t child=0;
   if(!directory_child(t,fat1,fat2,data_start,bps,spc,clusters,
                       root,parent,shortpart,&child)){
    result=NEXUS_FAT32_FILE_NO_SPACE;goto done;
   }
   parent=child;component=slash+1;
   continue;
  }
  if(!valid_name(component,shortname)){
   if(!long_name_info(component,shortname,&lfn_name,&lfn_entries)){
    result=NEXUS_FAT32_FILE_INVALID;goto done;
   }
   has_lfn=true;
  }
  break;
 }
 uint64_t parent_lba=data_start+(uint64_t)(parent-2U)*spc;
 uint32_t root_slot=UINT32_MAX,lfn_slot=UINT32_MAX;
 for(uint32_t s=0;s<spc&&root_slot==UINT32_MAX;s++){
  if(!read1(t,parent_lba+s,sector))goto done;
  for(uint32_t off=0;off<bps;off+=32U){
   bool free_entry=sector[off]==0U||sector[off]==0xE5U;
   if(free_entry){
    if(!has_lfn){root_slot=s*bps/32U+off/32U;break;}
    if(sector[off]==0U&&off+(lfn_entries+1U)*32U<=bps){
     lfn_slot=s*bps/32U+off/32U;
     root_slot=lfn_slot+lfn_entries;
     break;
    }
    continue;
   }
   if(sector[off+11U]!=0x0FU&&sector[off+11U]!=0x08U&&
      bytes_compare(sector+off,shortname,11U)==0){result=NEXUS_FAT32_FILE_EXISTS;goto done;}
  }
 }
 if(root_slot==UINT32_MAX){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 uint32_t cluster_bytes=spc*bps;
 uint32_t needed=length? (length+cluster_bytes-1U)/cluster_bytes:0U;
 if(needed>clusters-1U){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 /* Preflight the complete allocation before changing either FAT copy.
  * This avoids leaking a partial cluster chain when the volume is full. */
 if(needed>MAX_FILE_CLUSTERS){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 uint32_t found=0,first=0;
 for(uint32_t c=3U;c<clusters+2U&&found<needed;c++){
  uint64_t lba=fat1+((uint64_t)c*4U)/bps;
  uint32_t off=(uint32_t)(((uint64_t)c*4U)%bps);
  if(!read1(t,lba,sector))goto done;
  if((le32(sector+off)&0x0FFFFFFFU)==0U)allocated[found++]=c;
 }
 if(found!=needed){result=NEXUS_FAT32_FILE_NO_SPACE;goto done;}
 if(needed)first=allocated[0];
 /* Commit the FAT chain to both copies only after the full allocation fits. */
 for(uint32_t k=0;k<needed;k++){
  uint32_t c=allocated[k],next=(k+1U<needed)?allocated[k+1U]:0x0FFFFFFFU;
  if(!fat_set(t,fat1,fat2,bps,c,next))goto done;
 }
 if(!t->flush(t->context))goto done;
 /* Rewalk the allocation in ascending order and write file data. */
 uint32_t remaining=length;const uint8_t*src=data;
 for(uint32_t k=0;k<needed;k++){
  uint32_t cluster=allocated[k];
  uint64_t base=data_start+(uint64_t)(cluster-2U)*spc;
  for(uint32_t sec=0;sec<spc;sec++){
   for(uint32_t j=0;j<bps;j++)sector[j]=0;
   uint32_t n=remaining>bps?bps:remaining;
   for(uint32_t j=0;j<n;j++)sector[j]=src[j];
   if(!write1(t,base+sec,sector)||
      !read1(t,base+sec,verify)||
      bytes_compare(sector,verify,bps)!=0) {
    result=NEXUS_FAT32_FILE_VERIFY;goto done;
   }
   src+=n;remaining-=n;
  }
 }
 /* Commit one root entry after data and FAT are durable. */
 uint32_t dir_sector=root_slot/(bps/32U),dir_off=(root_slot%(bps/32U))*32U;
 if(!read1(t,parent_lba+dir_sector,sector))goto done;
 if(has_lfn){
  static const uint8_t offsets[13]={1U,3U,5U,7U,9U,14U,16U,18U,20U,22U,24U,28U,30U};
  uint32_t lfn_sector=lfn_slot/(bps/32U);
  uint32_t lfn_off=(lfn_slot%(bps/32U))*32U;
  if(lfn_sector!=dir_sector){result=NEXUS_FAT32_FILE_INVALID;goto done;}
  size_t long_length=0U;while(lfn_name[long_length])long_length++;
  for(uint32_t entry_index=0U;entry_index<lfn_entries;entry_index++){
   uint32_t order=lfn_entries-entry_index;
   uint8_t entry[32];for(unsigned i=0;i<32U;i++)entry[i]=0xFFU;
   entry[0]=(uint8_t)order|(entry_index==0U?0x40U:0U);
   entry[11]=0x0FU;entry[12]=0U;
   entry[13]=lfn_checksum(shortname);put16(entry+26U,0U);
   for(unsigned i=0;i<13U;i++){
    size_t char_index=(size_t)(order-1U)*13U+i;
    uint16_t ch=char_index<long_length?(uint8_t)lfn_name[char_index]:
      (char_index==long_length?0U:0xFFFFU);
    put16(entry+offsets[i],ch);
   }
   uint32_t entry_off=lfn_off+entry_index*32U;
   for(unsigned i=0;i<32U;i++)sector[entry_off+i]=entry[i];
  }
 }
 for(unsigned i=0;i<11;i++)sector[dir_off+i]=shortname[i];
 sector[dir_off+11U]=0x20U;put16(sector+dir_off+20U,(uint16_t)(first>>16));
 put16(sector+dir_off+26U,(uint16_t)first);put32(sector+dir_off+28U,length);
 if(!write1(t,parent_lba+dir_sector,sector)||!t->flush(t->context))goto done;
 if(!read1(t,parent_lba+dir_sector,verify))goto done;
 if(bytes_compare(sector,verify,bps)!=0){result=NEXUS_FAT32_FILE_VERIFY;goto done;}
 result=NEXUS_FAT32_FILE_OK;
done:busy=false;return result;
}

enum nexus_fat32_file_result nexus_fat32_write_root_file(
 const struct nexus_fat32_format_target*t,uint64_t part,uint64_t count,
 const char*name,const void*data,uint32_t length){
 return nexus_fat32_write_file(t,part,count,name,data,length);
}
