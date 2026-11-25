#include "usb_msc.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct mock_usb {
    uint8_t cbw[31];
    uint8_t csw_status;
    uint32_t csw_residue;
    bool bad_csw_tag;
    bool short_data;
    bool large_capacity;
    bool fail_recovery;
    unsigned recoveries;
    unsigned commands;
    uint8_t last_opcode;
    uint8_t last_cdb[16];
    uint8_t last_cdb_length;
    uint8_t last_data_out_first;
    size_t last_data_out_length;
};

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
    uint64_t v = 0U;
    for (size_t i = 0U; i < 8U; ++i) v = (v << 8) | p[i];
    return v;
}
static void put_be32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void put_be64(uint8_t *p, uint64_t v) {
    for (size_t i=0U;i<8U;++i) p[i]=(uint8_t)(v>>(56U-(uint32_t)i*8U));
}

static bool mock_bulk_out(void *context, uint8_t endpoint,
                          const void *buffer, size_t length, size_t *actual) {
    struct mock_usb *mock = context;
    const uint8_t *bytes = buffer;
    assert(endpoint == 0x02U);
    if (length == 31U && bytes[0] == 'U' && bytes[1] == 'S' &&
        bytes[2] == 'B' && bytes[3] == 'C') {
        for (size_t i=0U;i<31U;++i) mock->cbw[i]=bytes[i];
        mock->last_opcode = bytes[15];
        mock->last_cdb_length = bytes[14];
        for (size_t i=0U;i<16U;++i) mock->last_cdb[i]=bytes[15U+i];
        ++mock->commands;
        *actual = length;
        return true;
    }
    assert(mock->cbw[12] == 0U);
    mock->last_data_out_length = length;
    mock->last_data_out_first = length != 0U ? bytes[0] : 0U;
    *actual = length;
    return true;
}

static bool mock_bulk_in(void *context, uint8_t endpoint,
                         void *buffer, size_t capacity, size_t *actual) {
    struct mock_usb *mock = context;
    uint8_t *bytes = buffer;
    assert(endpoint == 0x81U);
    const uint32_t expected = le32(mock->cbw + 8U);
    if (capacity == 13U) {
        bytes[0]='U'; bytes[1]='S'; bytes[2]='B'; bytes[3]='S';
        uint32_t tag=le32(mock->cbw+4U);
        if (mock->bad_csw_tag) ++tag;
        bytes[4]=(uint8_t)tag; bytes[5]=(uint8_t)(tag>>8);
        bytes[6]=(uint8_t)(tag>>16); bytes[7]=(uint8_t)(tag>>24);
        bytes[8]=(uint8_t)mock->csw_residue;
        bytes[9]=(uint8_t)(mock->csw_residue>>8);
        bytes[10]=(uint8_t)(mock->csw_residue>>16);
        bytes[11]=(uint8_t)(mock->csw_residue>>24);
        bytes[12]=mock->csw_status;
        *actual=13U;
        mock->csw_status=0U;
        mock->csw_residue=0U;
        mock->bad_csw_tag=false;
        return true;
    }
    assert(mock->cbw[12] == 0x80U);
    assert(capacity == expected);
    for (size_t i=0U;i<capacity;++i) bytes[i]=0U;
    if (mock->last_opcode == 0x12U) {
        bytes[0]=0U;
        bytes[4]=31U;
        const char vendor[]="NEXUS   ";
        const char product[]="USB STORAGE     ";
        const char revision[]="2.10";
        for(size_t i=0U;i<8U;++i) bytes[8U+i]=(uint8_t)vendor[i];
        for(size_t i=0U;i<16U;++i) bytes[16U+i]=(uint8_t)product[i];
        for(size_t i=0U;i<4U;++i) bytes[32U+i]=(uint8_t)revision[i];
    } else if (mock->last_opcode == 0x25U) {
        put_be32(bytes, mock->large_capacity ? UINT32_MAX : 4095U);
        put_be32(bytes+4U, 512U);
    } else if (mock->last_opcode == 0x9EU) {
        put_be64(bytes, 0x0000000100000000ULL);
        put_be32(bytes+8U, 512U);
    } else if (mock->last_opcode == 0x03U) {
        bytes[0]=0x70U; bytes[2]=0x05U;
        bytes[7]=10U; bytes[12]=0x20U; bytes[13]=0x00U;
    } else {
        for(size_t i=0U;i<capacity;++i) bytes[i]=0xA5U;
    }
    *actual=mock->short_data && capacity != 0U ? capacity-1U : capacity;
    mock->short_data=false;
    return true;
}

static bool mock_recover(void *context, uint8_t interface_number,
                         uint8_t input_endpoint, uint8_t output_endpoint) {
    struct mock_usb *mock=context;
    assert(interface_number==4U);
    assert(input_endpoint==0x81U);
    assert(output_endpoint==0x02U);
    ++mock->recoveries;
    return !mock->fail_recovery;
}

static const struct nexus_usb_msc_transport ops = {
    mock_bulk_out, mock_bulk_in, mock_recover
};

static void init_device(struct mock_usb *mock,
                        struct nexus_usb_msc_device *device) {
    mock->large_capacity = true;
    assert(nexus_usb_msc_init(device,&ops,mock,4U,0x81U,0x02U,0U));
    assert(nexus_usb_msc_ready(device));
}

static void test_initialize_and_read_capacity_16(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    init_device(&mock,&device);
    assert(device.block_size==512U);
    assert(device.block_count==0x0000000100000001ULL);
    assert(device.max_blocks_per_request==8U);
    assert(mock.commands==4U);
    assert(mock.last_opcode==0x9EU);
    assert(device.vendor[0]=='N' && device.product[0]=='U');
    assert(device.revision[0]=='2');
    assert(mock.recoveries==0U);
}

static void test_read_write_16_and_flush(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    init_device(&mock,&device);
    uint8_t sector[512];
    assert(nexus_usb_msc_read_blocks(&device,0x100000000ULL,1U,sector));
    assert(mock.last_opcode==0x88U);
    assert(be64(mock.last_cdb+2U)==0x100000000ULL);
    assert(be32(mock.last_cdb+10U)==1U);
    assert(sector[0]==0xA5U);

    sector[0]=0x5AU;
    assert(nexus_usb_msc_write_blocks(&device,0x100000000ULL,1U,sector));
    assert(mock.last_opcode==0x8AU);
    assert(be64(mock.last_cdb+2U)==0x100000000ULL);
    assert(mock.last_data_out_first==0x5AU);
    assert(mock.last_data_out_length==sizeof(sector));
    assert(nexus_usb_msc_flush(&device));
    assert(mock.last_opcode==0x35U);
}

static void test_sense_and_command_failure(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    init_device(&mock,&device);
    mock.csw_status=1U;
    uint8_t sector[512];
    assert(!nexus_usb_msc_read_blocks(&device,0U,1U,sector));
    assert(nexus_usb_msc_ready(&device));
    struct nexus_usb_msc_sense sense;
    assert(nexus_usb_msc_request_sense(&device,&sense));
    assert(sense.key==5U);
    assert(sense.additional_code==0x20U);
    assert(sense.additional_code_qualifier==0U);
}

static void test_bad_csw_and_short_data_recover(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    init_device(&mock,&device);
    uint8_t sector[512];
    mock.bad_csw_tag=true;
    assert(!nexus_usb_msc_read_blocks(&device,0U,1U,sector));
    assert(mock.recoveries==1U);
    assert(nexus_usb_msc_ready(&device));
    mock.short_data=true;
    assert(!nexus_usb_msc_read_blocks(&device,0U,1U,sector));
    assert(mock.recoveries==2U);
}

static void test_bounds_and_endpoint_validation(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    assert(!nexus_usb_msc_init(&device,&ops,&mock,4U,0x01U,0x02U,0U));
    assert(!nexus_usb_msc_init(&device,&ops,&mock,4U,0x81U,0x82U,0U));
    assert(!nexus_usb_msc_init(&device,&ops,&mock,4U,0x81U,0x02U,16U));
    init_device(&mock,&device);
    uint8_t sector[512];
    assert(!nexus_usb_msc_read_blocks(&device,device.block_count,1U,sector));
    assert(!nexus_usb_msc_read_blocks(&device,0U,0U,sector));
    assert(!nexus_usb_msc_read_blocks(&device,0U,9U,sector));
    assert(!nexus_usb_msc_write_blocks(&device,device.block_count-1U,2U,sector));
}

static void test_transport_failure_poisoning(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    init_device(&mock,&device);
    mock.bad_csw_tag=true;
    /* A working reset recovery preserves the device; the caller may retry. */
    uint8_t sector[512];
    assert(!nexus_usb_msc_read_blocks(&device,0U,1U,sector));
    assert(nexus_usb_msc_ready(&device));
}

static void test_configuration_descriptor_parser(void) {
    uint8_t descriptor[] = {
        9U,2U,48U,0U,2U,1U,0U,0x80U,50U,
        9U,4U,0U,0U,1U,3U,1U,1U,0U,
        7U,5U,0x81U,3U,8U,0U,10U,
        9U,4U,3U,0U,2U,8U,6U,0x50U,0U,
        7U,5U,0x02U,2U,0x00U,0x02U,0U,
        7U,5U,0x81U,2U,0x00U,0x02U,0U
    };
    struct nexus_usb_msc_interface found;
    assert(nexus_usb_msc_find_interface(
        descriptor,sizeof(descriptor),&found));
    assert(found.configuration_value==1U);
    assert(found.interface_number==3U);
    assert(found.bulk_in_endpoint==0x81U);
    assert(found.bulk_out_endpoint==0x02U);
    assert(found.bulk_in_max_packet==512U);
    assert(found.bulk_out_max_packet==512U);

    descriptor[32] = 0x51U; /* UAS protocol, not Bulk-Only Transport. */
    assert(!nexus_usb_msc_find_interface(
        descriptor,sizeof(descriptor),&found));
    descriptor[32] = 0x50U;
    descriptor[37] = 3U; /* Interrupt endpoint is not bulk. */
    assert(!nexus_usb_msc_find_interface(
        descriptor,sizeof(descriptor),&found));
    descriptor[37] = 2U;
    assert(!nexus_usb_msc_find_interface(
        descriptor,sizeof(descriptor)-1U,&found));
    descriptor[9] = 8U; /* Truncated interface descriptor. */
    assert(!nexus_usb_msc_find_interface(
        descriptor,sizeof(descriptor),&found));
}

static void test_capacity_10_and_32bit_commands(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    mock.large_capacity=false;
    assert(nexus_usb_msc_init(&device,&ops,&mock,4U,0x81U,0x02U,0U));
    assert(device.block_count==4096U);
    uint8_t sector[512];
    assert(nexus_usb_msc_read_blocks(&device,0x1234U,1U,sector));
    assert(mock.last_opcode==0x28U);
    assert(be32(mock.last_cdb+2U)==0x1234U);
    assert(nexus_usb_msc_write_blocks(&device,0x1234U,1U,sector));
    assert(mock.last_opcode==0x2AU);
}

static void test_failed_recovery_disables_device(void) {
    struct mock_usb mock={0};
    struct nexus_usb_msc_device device;
    mock.large_capacity=true;
    init_device(&mock,&device);
    mock.bad_csw_tag=true;
    mock.fail_recovery=true;
    uint8_t sector[512];
    assert(!nexus_usb_msc_read_blocks(&device,0U,1U,sector));
    assert(!nexus_usb_msc_ready(&device));
}

int main(void) {
    test_configuration_descriptor_parser();
    test_capacity_10_and_32bit_commands();
    test_initialize_and_read_capacity_16();
    test_read_write_16_and_flush();
    test_sense_and_command_failure();
    test_bad_csw_and_short_data_recover();
    test_bounds_and_endpoint_validation();
    test_transport_failure_poisoning();
    test_failed_recovery_disables_device();
    return 0;
}
