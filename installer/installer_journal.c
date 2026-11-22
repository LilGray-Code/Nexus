#include "installer_journal.h"

#include <stddef.h>
#include <stdint.h>

#define JOURNAL_SECTOR_MAX 4096U
#define JOURNAL_VERSION 1U
#define JOURNAL_CRC_OFFSET(block_size) ((block_size) - 4U)

static const uint8_t journal_magic[8] = {
    'N','X','J','N','L','0','1',0
};

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4U) << 32);
}
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p,(uint32_t)v); put32(p+4U,(uint32_t)(v>>32));
}
static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc=0xFFFFFFFFU;
    for(size_t i=0U;i<length;++i) {
        crc^=data[i];
        for(uint32_t b=0U;b<8U;++b) {
            uint32_t mask=(uint32_t)-(int32_t)(crc&1U);
            crc=(crc>>1)^(0xEDB88320U&mask);
        }
    }
    return ~crc;
}
static size_t bounded_length(const char *s,size_t cap) {
    if(s==NULL)return cap;
    size_t n=0U; while(n<cap&&s[n]!='\0')++n;
    return n;
}
static bool device_valid(const struct nexus_installer_journal_device *d,
                         uint64_t lba) {
    return d!=NULL && d->block_count>=2U && lba<d->block_count &&
        d->block_count-lba>=2U &&
        (d->block_size==512U||d->block_size==4096U) &&
        d->max_blocks_per_request>=1U && d->read!=NULL &&
        d->write!=NULL && d->flush!=NULL;
}
static bool record_valid(const struct nexus_installer_journal_record *r) {
    return r!=NULL && r->sequence!=0U &&
        r->phase>=NEXUS_JOURNAL_EMPTY &&
        r->phase<=NEXUS_JOURNAL_RECOVERY_REQUIRED &&
        r->disk_block_count>0U &&
        (r->block_size==512U||r->block_size==4096U) &&
        bounded_length(r->stable_id,sizeof(r->stable_id))>0U &&
        bounded_length(r->stable_id,sizeof(r->stable_id))<sizeof(r->stable_id) &&
        bounded_length(r->model,sizeof(r->model))>0U &&
        bounded_length(r->model,sizeof(r->model))<sizeof(r->model);
}
static void encode(const struct nexus_installer_journal_record *r,
                   uint32_t sector_size,uint8_t *out) {
    for(uint32_t i=0U;i<sector_size;++i)out[i]=0U;
    for(size_t i=0U;i<8U;++i)out[i]=journal_magic[i];
    put32(out+8U,JOURNAL_VERSION);
    put32(out+12U,(uint32_t)r->phase);
    put64(out+16U,r->sequence);
    put64(out+24U,r->disk_block_count);
    put32(out+32U,r->block_size);
    put32(out+36U,r->firmware_mode);
    put32(out+40U,r->completed_steps);
    for(size_t i=0U;i<sizeof(r->stable_id);++i)out[48U+i]=(uint8_t)r->stable_id[i];
    for(size_t i=0U;i<sizeof(r->model);++i)out[112U+i]=(uint8_t)r->model[i];
    for(size_t i=0U;i<sizeof(r->disk_guid);++i)out[176U+i]=r->disk_guid[i];
    put32(out+JOURNAL_CRC_OFFSET(sector_size),0U);
    put32(out+JOURNAL_CRC_OFFSET(sector_size),
          crc32(out,sector_size));
}
static bool bytes_zero(const uint8_t *p,size_t n) {
    uint8_t v=0U; for(size_t i=0U;i<n;++i)v|=p[i]; return v==0U;
}
static bool bytes_ff(const uint8_t *p,size_t n) {
    uint8_t v=0xFFU; for(size_t i=0U;i<n;++i)v&=p[i]; return v==0xFFU;
}
static bool decode(const uint8_t *in,uint32_t sector_size,
                   struct nexus_installer_journal_record *r) {
    if(sector_size!=512U&&sector_size!=4096U)return false;
    for(size_t i=0U;i<8U;++i)if(in[i]!=journal_magic[i])return false;
    if(get32(in+8U)!=JOURNAL_VERSION)return false;
    uint32_t stored=get32(in+JOURNAL_CRC_OFFSET(sector_size));
    uint8_t copy[JOURNAL_SECTOR_MAX];
    for(uint32_t i=0U;i<sector_size;++i)copy[i]=in[i];
    put32(copy+JOURNAL_CRC_OFFSET(sector_size),0U);
    if(crc32(copy,sector_size)!=stored)return false;
    *r=(struct nexus_installer_journal_record){0};
    r->phase=(enum nexus_installer_journal_phase)get32(in+12U);
    r->sequence=get64(in+16U);
    r->disk_block_count=get64(in+24U);
    r->block_size=get32(in+32U);
    r->firmware_mode=get32(in+36U);
    r->completed_steps=get32(in+40U);
    for(size_t i=0U;i<sizeof(r->stable_id);++i)r->stable_id[i]=(char)in[48U+i];
    for(size_t i=0U;i<sizeof(r->model);++i)r->model[i]=(char)in[112U+i];
    for(size_t i=0U;i<sizeof(r->disk_guid);++i)r->disk_guid[i]=in[176U+i];
    return record_valid(r);
}
static bool same_target(const struct nexus_installer_journal_record *a,
                        const struct nexus_installer_journal_record *b) {
    if(a->disk_block_count!=b->disk_block_count||a->block_size!=b->block_size)
        return false;
    for(size_t i=0U;i<sizeof(a->stable_id);++i)
        if(a->stable_id[i]!=b->stable_id[i])return false;
    for(size_t i=0U;i<sizeof(a->model);++i)
        if(a->model[i]!=b->model[i])return false;
    for(size_t i=0U;i<sizeof(a->disk_guid);++i)
        if(a->disk_guid[i]!=b->disk_guid[i])return false;
    return true;
}
static bool same_record(const struct nexus_installer_journal_record *a,
                        const struct nexus_installer_journal_record *b) {
    return same_target(a,b) && a->sequence==b->sequence &&
        a->phase==b->phase && a->firmware_mode==b->firmware_mode &&
        a->completed_steps==b->completed_steps;
}

enum nexus_installer_journal_result nexus_installer_journal_load(
    const struct nexus_installer_journal_device *d,uint64_t lba,
    struct nexus_installer_journal_record *record) {
    if(!device_valid(d,lba)||record==NULL)return NEXUS_JOURNAL_INVALID;
    uint8_t a[JOURNAL_SECTOR_MAX],b[JOURNAL_SECTOR_MAX];
    if(!d->read(d->context,lba,1U,a)||
       !d->read(d->context,lba+1U,1U,b))return NEXUS_JOURNAL_IO_ERROR;
    struct nexus_installer_journal_record ra,rb;
    bool va=decode(a,d->block_size,&ra),vb=decode(b,d->block_size,&rb);
    if(va&&vb) {
        if(!same_target(&ra,&rb) ||
           (ra.sequence==rb.sequence&&!same_record(&ra,&rb)))
            return NEXUS_JOURNAL_CORRUPT;
        *record=ra.sequence>=rb.sequence?ra:rb;
        return NEXUS_JOURNAL_OK;
    }
    if(va){*record=ra;return NEXUS_JOURNAL_OK;}
    if(vb){*record=rb;return NEXUS_JOURNAL_OK;}
    if((bytes_zero(a,d->block_size)||bytes_ff(a,d->block_size))&&
       (bytes_zero(b,d->block_size)||bytes_ff(b,d->block_size)))
        return NEXUS_JOURNAL_NO_RECORD;
    return NEXUS_JOURNAL_CORRUPT;
}

enum nexus_installer_journal_result nexus_installer_journal_commit(
    const struct nexus_installer_journal_device *d,uint64_t lba,
    const struct nexus_installer_journal_record *record) {
    if(!device_valid(d,lba)||!record_valid(record)||
       record->disk_block_count!=d->block_count||
       record->block_size!=d->block_size)return NEXUS_JOURNAL_INVALID;
    struct nexus_installer_journal_record current;
    enum nexus_installer_journal_result loaded=
        nexus_installer_journal_load(d,lba,&current);
    if(loaded==NEXUS_JOURNAL_CORRUPT||loaded==NEXUS_JOURNAL_IO_ERROR||
       loaded==NEXUS_JOURNAL_INVALID)return loaded;
    if(loaded==NEXUS_JOURNAL_NO_RECORD) {
        if(record->sequence!=1U)return NEXUS_JOURNAL_INVALID;
    } else {
        if(!same_target(&current,record)||record->sequence<=current.sequence)
            return NEXUS_JOURNAL_INVALID;
    }
    uint32_t slot=(uint32_t)(record->sequence&1U);
    uint8_t buffer[JOURNAL_SECTOR_MAX],verify[JOURNAL_SECTOR_MAX];
    struct nexus_installer_journal_record decoded;
    encode(record,d->block_size,buffer);
    if(!d->write(d->context,lba+slot,1U,buffer)||!d->flush(d->context))
        return NEXUS_JOURNAL_IO_ERROR;
    if(!d->read(d->context,lba+slot,1U,verify))
        return NEXUS_JOURNAL_IO_ERROR;
    if(!decode(verify,d->block_size,&decoded)||
       decoded.sequence!=record->sequence||decoded.phase!=record->phase||
       !same_target(&decoded,record))
        return NEXUS_JOURNAL_CORRUPT;
    return NEXUS_JOURNAL_OK;
}
