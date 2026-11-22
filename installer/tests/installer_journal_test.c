#include "installer_journal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct mock_disk {
    uint8_t sectors[16][512];
    unsigned writes;
    bool fail_write;
    bool tear_write;
    bool fail_flush;
};
static bool rd(void *ctx,uint64_t lba,uint32_t count,void *buffer) {
    struct mock_disk *d=ctx;
    if(count!=1U||lba>=16U)return false;
    memcpy(buffer,d->sectors[lba],512U);return true;
}
static bool wr(void *ctx,uint64_t lba,uint32_t count,const void *buffer) {
    struct mock_disk *d=ctx;
    if(d->fail_write||count!=1U||lba>=16U)return false;
    ++d->writes;
    if(d->tear_write){memcpy(d->sectors[lba],buffer,127U);return true;}
    memcpy(d->sectors[lba],buffer,512U);return true;
}
static bool fl(void *ctx) {
    struct mock_disk *d=ctx;return !d->fail_flush;
}
static struct nexus_installer_journal_record rec(uint64_t seq,
 enum nexus_installer_journal_phase phase) {
    struct nexus_installer_journal_record r={0};
    r.sequence=seq;r.phase=phase;r.disk_block_count=16U;
    r.block_size=512U;r.firmware_mode=1U;r.completed_steps=(uint32_t)phase;
    strcpy(r.stable_id,"nvme:serial:NEXUS-MOCK-0001");
    strcpy(r.model,"Nexus Mock NVMe");
    for(size_t i=0U;i<16U;++i)r.disk_guid[i]=(uint8_t)(i+1U);
    return r;
}
static void test_empty_and_alternating_commit(void) {
    struct mock_disk disk={0};
    struct nexus_installer_journal_device dev={16U,512U,1U,&disk,rd,wr,fl};
    struct nexus_installer_journal_record out;
    assert(nexus_installer_journal_load(&dev,4U,&out)==NEXUS_JOURNAL_NO_RECORD);
    struct nexus_installer_journal_record a=rec(1U,NEXUS_JOURNAL_PREPARED);
    assert(nexus_installer_journal_commit(&dev,4U,&a)==NEXUS_JOURNAL_OK);
    assert(nexus_installer_journal_load(&dev,4U,&out)==NEXUS_JOURNAL_OK);
    assert(out.sequence==1U&&out.phase==NEXUS_JOURNAL_PREPARED);
    struct nexus_installer_journal_record b=rec(2U,NEXUS_JOURNAL_GPT_WRITTEN);
    assert(nexus_installer_journal_commit(&dev,4U,&b)==NEXUS_JOURNAL_OK);
    assert(nexus_installer_journal_load(&dev,4U,&out)==NEXUS_JOURNAL_OK);
    assert(out.sequence==2U&&out.phase==NEXUS_JOURNAL_GPT_WRITTEN);
    assert(disk.writes==2U);
}
static void test_torn_update_preserves_previous_record(void) {
    struct mock_disk disk={0};
    struct nexus_installer_journal_device dev={16U,512U,1U,&disk,rd,wr,fl};
    struct nexus_installer_journal_record a=rec(1U,NEXUS_JOURNAL_PREPARED),out;
    assert(nexus_installer_journal_commit(&dev,4U,&a)==NEXUS_JOURNAL_OK);
    disk.tear_write=true;
    struct nexus_installer_journal_record b=rec(2U,NEXUS_JOURNAL_GPT_WRITTEN);
    assert(nexus_installer_journal_commit(&dev,4U,&b)==NEXUS_JOURNAL_CORRUPT);
    disk.tear_write=false;
    assert(nexus_installer_journal_load(&dev,4U,&out)==NEXUS_JOURNAL_OK);
    assert(out.sequence==1U&&out.phase==NEXUS_JOURNAL_PREPARED);
}
static void test_corruption_and_io_fail_closed(void) {
    struct mock_disk disk={0};
    struct nexus_installer_journal_device dev={16U,512U,1U,&disk,rd,wr,fl};
    struct nexus_installer_journal_record out;
    memset(disk.sectors[4],0xA5U,512U);
    assert(nexus_installer_journal_load(&dev,4U,&out)==NEXUS_JOURNAL_CORRUPT);
    memset(&disk,0,sizeof(disk));disk.fail_write=true;
    struct nexus_installer_journal_record a=rec(1U,NEXUS_JOURNAL_PREPARED);
    assert(nexus_installer_journal_commit(&dev,4U,&a)==NEXUS_JOURNAL_IO_ERROR);
    disk.fail_write=false;disk.fail_flush=true;
    assert(nexus_installer_journal_commit(&dev,4U,&a)==NEXUS_JOURNAL_IO_ERROR);
}
static void test_identity_and_sequence_rejection(void) {
    struct mock_disk disk={0};
    struct nexus_installer_journal_device dev={16U,512U,1U,&disk,rd,wr,fl};
    struct nexus_installer_journal_record a=rec(1U,NEXUS_JOURNAL_PREPARED);
    assert(nexus_installer_journal_commit(&dev,4U,&a)==NEXUS_JOURNAL_OK);
    struct nexus_installer_journal_record b=rec(2U,NEXUS_JOURNAL_GPT_WRITTEN);
    strcpy(b.stable_id,"different-device");
    assert(nexus_installer_journal_commit(&dev,4U,&b)==NEXUS_JOURNAL_INVALID);
    b=rec(1U,NEXUS_JOURNAL_GPT_WRITTEN);
    assert(nexus_installer_journal_commit(&dev,4U,&b)==NEXUS_JOURNAL_INVALID);
    b=rec(3U,NEXUS_JOURNAL_GPT_WRITTEN);
    assert(nexus_installer_journal_commit(&dev,4U,&b)==NEXUS_JOURNAL_OK);
}
int main(void) {
    test_empty_and_alternating_commit();
    test_torn_update_preserves_previous_record();
    test_corruption_and_io_fail_closed();
    test_identity_and_sequence_rejection();
    puts("NEXUS INSTALLER DURABLE JOURNAL TESTS PASSED");
    return 0;
}
