#include "elf64_dynamic.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct ehdr {
 uint8_t ident[16]; uint16_t type,machine; uint32_t version;
 uint64_t entry,phoff,shoff; uint32_t flags;
 uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
} __attribute__((packed));
struct phdr {
 uint32_t type,flags; uint64_t offset,vaddr,paddr,filesz,memsz,align;
} __attribute__((packed));
struct dyn { int64_t tag; uint64_t value; } __attribute__((packed));

int main(void) {
 uint8_t image[0x1100] = {0};
 struct ehdr *h=(void *)image;
 h->ident[0]=0x7f;h->ident[1]='E';h->ident[2]='L';h->ident[3]='F';
 h->ident[4]=2;h->ident[5]=1;h->ident[6]=1;
 h->type=3;h->machine=62;h->version=1;h->ehsize=64;
 h->phoff=64;h->phentsize=56;h->phnum=2;
 struct phdr *p=(void *)(image+64);
 p[0]=(struct phdr){1,5,0,0,0,sizeof(image),sizeof(image),0x1000};
 p[1]=(struct phdr){2,6,0x1000,0x1000,0,7*16,7*16,8};
 struct dyn *d=(void *)(image+0x1000);
 d[0]=(struct dyn){1,1};       /* DT_NEEDED */
 d[1]=(struct dyn){5,0x1080};  /* DT_STRTAB */
 d[2]=(struct dyn){10,9};      /* DT_STRSZ */
 d[3]=(struct dyn){7,0x1090};  /* DT_RELA */
 d[4]=(struct dyn){8,24};      /* DT_RELASZ */
 d[5]=(struct dyn){9,24};      /* DT_RELAENT */
 d[6]=(struct dyn){0,0};
 image[0x1080]=0;image[0x1081]='l';image[0x1082]='i';
 image[0x1083]='b';image[0x1084]='c';image[0x1085]='.';image[0x1086]='s';
 image[0x1087]='o';image[0x1088]=0;
 struct nexus_elf64_dynamic_info info;
 assert(nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 assert(info.position_independent && !info.has_interpreter);
 assert(info.needed_count==1 && info.has_rela);
 assert(info.rela_address==0x1090 && info.rela_size==24 &&
        info.rela_entry_size==24);
 d[0].value=9;
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 d[0].value=1;
 image[0x1088]='x';
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 image[0x1088]=0;
 assert(!nexus_elf64_dynamic_parse(image,63,&info));
 image[0x1000+6*16]=1; /* remove DT_NULL */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 d[6]=(struct dyn){0,0};
 d[5].value=16; /* malformed RELA entry size */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 h->type=2; /* Static ET_EXEC is not a PIE image. */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 return 0;
}
