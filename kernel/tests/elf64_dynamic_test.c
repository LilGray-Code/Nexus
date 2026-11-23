#include "elf64_dynamic.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
 p[0]=(struct phdr){1,7,0,0,0,sizeof(image),sizeof(image),0x1000};
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
 /* Overlapping PT_LOAD virtual ranges make address-to-file mapping
  * ambiguous and must be rejected before parsing dynamic metadata.
  */
 h->phnum=3;
 p[2]=(struct phdr){1,4,0x800,0x800,0,0x100,0x100,0x1000};
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 h->phnum=2;
 /* Dynamic metadata must be backed by the PT_LOAD bytes at its virtual
  * address, not merely point at an arbitrary in-range file offset.
  */
 p[1].offset=0x0FF0;
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 p[1].offset=0x1000;
 assert(nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 struct { uint64_t offset, info; int64_t addend; } __attribute__((packed)) *rela =
     (void *)(image + 0x1090);
 rela->offset=0x10B0; rela->info=8U; rela->addend=0x1234;
 uint8_t loaded[sizeof(image)];
 memcpy(loaded,image,sizeof(image));
 assert(nexus_elf64_dynamic_apply_relative(image,sizeof(image),0U,
                                            loaded,sizeof(loaded),0x400000U));
 uint64_t relocated=0U;
 memcpy(&relocated,loaded+0x10B0,sizeof(relocated));
 assert(relocated==0x401234U);
 rela->info=7U; /* Unsupported relocation must be rejected. */
 memcpy(loaded,image,sizeof(image));
 assert(!nexus_elf64_dynamic_apply_relative(image,sizeof(image),0U,
                                             loaded,sizeof(loaded),0x400000U));
 rela->info=8U;
 rela->offset=0x1200; /* Outside mapped writable load segment. */
 assert(!nexus_elf64_dynamic_apply_relative(image,sizeof(image),0U,
                                             loaded,sizeof(loaded),0x400000U));
 rela->offset=0x10B0;
 rela->addend=INT64_MAX;
 assert(!nexus_elf64_dynamic_apply_relative(image,sizeof(image),0U,
                                             loaded,sizeof(loaded),UINT64_MAX));
 rela->addend=0x1234;
 p[0].flags=5U; /* Read/execute load segment is not a writable target. */
 assert(!nexus_elf64_dynamic_apply_relative(image,sizeof(image),0U,
                                             loaded,sizeof(loaded),0x400000U));
 p[0].flags=7U;
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
 d[5].value=24;
 d[4].tag=0; /* RELA address without a matching size/entry-size tuple. */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 d[4].tag=8;
 d[3].tag=7;
 d[3].value=0x1090;
 d[4].tag=7; /* Duplicate DT_RELA entries are ambiguous and rejected. */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 d[4].tag=8;
 h->type=2; /* Static ET_EXEC is not a PIE image. */
 assert(!nexus_elf64_dynamic_parse(image,sizeof(image),&info));
 return 0;
}
