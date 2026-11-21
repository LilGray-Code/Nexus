#include "nvme.h"

#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define NVME_REG_CAP   0x000U
#define NVME_REG_INTMS 0x00CU
#define NVME_REG_CC    0x014U
#define NVME_REG_CSTS  0x01CU
#define NVME_REG_AQA   0x024U
#define NVME_REG_ASQ   0x028U
#define NVME_REG_ACQ   0x030U
#define NVME_DOORBELL_BASE 0x1000U

#define NVME_CC_ENABLE       (1U << 0)
#define NVME_CSTS_READY      (1U << 0)
#define NVME_CSTS_FATAL      (1U << 1)
#define NVME_ADMIN_QUEUE_DEPTH 16U
#define NVME_POLL_LIMIT 100000000U
#define NVME_DMA_LIMIT 0x100000000ULL

struct nvme_command {
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;
    uint32_t namespace_id;
    uint64_t reserved;
    uint64_t metadata_pointer;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct nvme_completion {
    uint32_t result_low;
    uint32_t result_high;
    uint16_t submission_head;
    uint16_t submission_queue_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed));

_Static_assert(sizeof(struct nvme_command) == 64,
               "NVMe command entry must be 64 bytes");
_Static_assert(sizeof(struct nvme_completion) == 16,
               "NVMe completion entry must be 16 bytes");

static bool nvme_map_page(uint64_t physical,
                          volatile uint32_t **mapped_page) {
    return paging_map_mmio_page(physical, mapped_page) &&
           mapped_page != NULL && *mapped_page != NULL;
}

static bool nvme_allocate_dma_page(uint64_t *physical,
                                   volatile uint8_t **virtual_address) {
    if (physical == NULL || virtual_address == NULL ||
        !memory_alloc_dma_page(NVME_DMA_LIMIT, physical)) {
        return false;
    }

    volatile uint64_t *page =
        (volatile uint64_t *)memory_phys_to_virt(*physical);
    if (page == NULL) return false;

    for (unsigned int i = 0; i < 512; ++i) page[i] = 0;
    *virtual_address = (volatile uint8_t *)page;
    return true;
}

static bool nvme_wait_ready(volatile uint32_t *registers, bool ready) {
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        const uint32_t status = registers[NVME_REG_CSTS / 4U];
        if (status == UINT32_MAX || (status & NVME_CSTS_FATAL) != 0) {
            return false;
        }
        if (((status & NVME_CSTS_READY) != 0) == ready) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static void nvme_log_identify_field(const char *label,
                                    const volatile uint8_t *field,
                                    size_t field_size) {
    char value[41];
    size_t length = field_size < sizeof(value) - 1U
        ? field_size : sizeof(value) - 1U;

    for (size_t i = 0; i < length; ++i) {
        const uint8_t ch = field[i];
        if (ch == 0) {
            length = i;
            break;
        }
        value[i] = (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : '?';
    }

    while (length > 0 && value[length - 1U] == ' ') --length;
    value[length] = '\0';
    serial_write(label);
    serial_write(value);
    serial_write("\r\n");
}

bool nvme_identify_probe(const struct nexus_pci_device *device) {
    if (device == NULL || device->class_code != 0x01 ||
        device->subclass != 0x08 ||
        device->programming_interface != 0x02) {
        return false;
    }

    struct nexus_pci_bar bar0;
    if (!pci_decode_bar(device, 0, &bar0) || bar0.io_space ||
        !bar0.assigned || (bar0.base & 0xFFFU) != 0) {
        serial_write("NVMe: BAR0 is not an assigned, aligned MMIO BAR\r\n");
        return false;
    }
    if (bar0.base > UINT64_MAX - NVME_DOORBELL_BASE) {
        serial_write("NVMe: BAR0 address overflows register range\r\n");
        return false;
    }

    if (!pci_enable_memory_bus_master(device)) {
        serial_write("NVMe: unable to enable PCI memory and bus mastering\r\n");
        return false;
    }

    volatile uint32_t *register_page = NULL;
    if (!nvme_map_page(bar0.base, &register_page)) {
        serial_write("NVMe: unable to map controller registers\r\n");
        return false;
    }
    volatile uint32_t *registers = register_page;
    const uint64_t capability =
        *(volatile uint64_t *)(uintptr_t)&registers[NVME_REG_CAP / 4U];

    if (capability == 0 || capability == UINT64_MAX) {
        serial_write("NVMe: invalid CAP register\r\n");
        return false;
    }

    const uint32_t maximum_queue_entries =
        (uint32_t)(capability & 0xFFFFU) + 1U;
    const uint8_t doorbell_stride = (uint8_t)((capability >> 32) & 0x0FU);
    const uint8_t minimum_page_size = (uint8_t)((capability >> 48) & 0x0FU);
    const bool nvm_command_set_supported =
        (capability & (1ULL << 37)) != 0;

    if (!nvm_command_set_supported || minimum_page_size != 0 ||
        maximum_queue_entries < NVME_ADMIN_QUEUE_DEPTH ||
        doorbell_stride > 8U) {
        serial_write("NVMe: controller capabilities outside current driver limits CAP=");
        serial_write_hex((uint32_t)capability);
        serial_write("\r\n");
        return false;
    }

    serial_write("NVMe: CAP low/high: ");
    serial_write_hex((uint32_t)capability);
    serial_write("/");
    serial_write_hex((uint32_t)(capability >> 32));
    serial_write("\r\n");

    const uint32_t doorbell_stride_bytes = 4U << doorbell_stride;
    volatile uint32_t *doorbell_page = NULL;
    if (!nvme_map_page(bar0.base + NVME_DOORBELL_BASE, &doorbell_page)) {
        serial_write("NVMe: unable to map admin doorbell page\r\n");
        return false;
    }

    uint64_t submission_physical = 0;
    uint64_t completion_physical = 0;
    uint64_t identify_physical = 0;
    volatile uint8_t *submission_bytes = NULL;
    volatile uint8_t *completion_bytes = NULL;
    volatile uint8_t *identify_bytes = NULL;

    if (!nvme_allocate_dma_page(&submission_physical, &submission_bytes) ||
        !nvme_allocate_dma_page(&completion_physical, &completion_bytes) ||
        !nvme_allocate_dma_page(&identify_physical, &identify_bytes)) {
        serial_write("NVMe: unable to allocate three DMA32 pages\r\n");
        return false;
    }

    volatile struct nvme_command *submission_queue =
        (volatile struct nvme_command *)submission_bytes;
    volatile struct nvme_completion *completion_queue =
        (volatile struct nvme_completion *)completion_bytes;

    uint32_t controller_configuration = registers[NVME_REG_CC / 4U];
    uint32_t controller_status = registers[NVME_REG_CSTS / 4U];
    if (controller_configuration == UINT32_MAX ||
        controller_status == UINT32_MAX ||
        (controller_status & NVME_CSTS_FATAL) != 0) {
        serial_write("NVMe: controller state register is invalid or fatal\r\n");
        return false;
    }

    if ((controller_configuration & NVME_CC_ENABLE) != 0 ||
        (controller_status & NVME_CSTS_READY) != 0) {
        registers[NVME_REG_CC / 4U] =
            controller_configuration & ~NVME_CC_ENABLE;
        if (!nvme_wait_ready(registers, false)) {
            serial_write("NVMe: controller did not become ready for reconfiguration\r\n");
            return false;
        }
    }

    const uint32_t queue_attributes =
        (NVME_ADMIN_QUEUE_DEPTH - 1U) |
        ((NVME_ADMIN_QUEUE_DEPTH - 1U) << 16);
    registers[NVME_REG_AQA / 4U] = queue_attributes;
    *(volatile uint64_t *)(uintptr_t)&registers[NVME_REG_ASQ / 4U] =
        submission_physical;
    *(volatile uint64_t *)(uintptr_t)&registers[NVME_REG_ACQ / 4U] =
        completion_physical;

    /* Polling mode: mask controller interrupts before enabling admin queues. */
    registers[NVME_REG_INTMS / 4U] = UINT32_MAX;

    /* 4 KiB memory page, 64-byte SQ entries, 16-byte CQ entries. */
    registers[NVME_REG_CC / 4U] =
        NVME_CC_ENABLE | (6U << 16) | (4U << 20);
    if (!nvme_wait_ready(registers, true)) {
        serial_write("NVMe: controller failed to become ready\r\n");
        return false;
    }

    submission_queue[0].opcode = 0x06U; /* Identify */
    submission_queue[0].command_id = 1U;
    submission_queue[0].namespace_id = 0U; /* Controller identify */
    submission_queue[0].prp1 = identify_physical;
    submission_queue[0].cdw10 = 1U; /* CNS=1: Identify Controller */

    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[0] = 1U; /* Admin Submission Queue tail doorbell. */

    bool completed = false;
    uint16_t completion_status = 0;
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        completion_status = completion_queue[0].status;
        if ((completion_status & 1U) != 0) {
            completed = true;
            break;
        }
        controller_status = registers[NVME_REG_CSTS / 4U];
        if (controller_status == UINT32_MAX ||
            (controller_status & NVME_CSTS_FATAL) != 0) {
            break;
        }
        __asm__ volatile ("pause");
    }

    if (!completed || completion_queue[0].command_id != 1U ||
        ((completion_status >> 1) & 0xFFU) != 0 ||
        ((completion_status >> 9) & 0x07U) != 0) {
        serial_write("NVMe: Identify Controller command did not complete successfully, status=");
        serial_write_hex(completion_status);
        serial_write("\r\n");
        return false;
    }

    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[doorbell_stride_bytes / 4U] = 1U;

    const uint16_t vendor_id =
        (uint16_t)identify_bytes[0] |
        ((uint16_t)identify_bytes[1] << 8);
    if (vendor_id == 0 || vendor_id == UINT16_MAX) {
        serial_write("NVMe: Identify data has an invalid vendor ID\r\n");
        return false;
    }

    serial_write("NVMe: Identify Controller vendor ID: ");
    serial_write_hex(vendor_id);
    serial_write("\r\n");
    nvme_log_identify_field("NVMe: serial: ", identify_bytes + 4U, 20U);
    nvme_log_identify_field("NVMe: model: ", identify_bytes + 24U, 40U);
    nvme_log_identify_field("NVMe: firmware: ", identify_bytes + 64U, 8U);
    serial_write("NVMe: Identify Controller completed\r\n");

    /* Identify Namespace 1: metadata only; no namespace data I/O. */
    const uint32_t namespace_count =
        (uint32_t)identify_bytes[516] |
        ((uint32_t)identify_bytes[517] << 8) |
        ((uint32_t)identify_bytes[518] << 16) |
        ((uint32_t)identify_bytes[519] << 24);
    serial_write("NVMe: namespace count: ");
    serial_write_hex(namespace_count);
    serial_write("\r\n");

    if (namespace_count == 0) {
        serial_write("NVMe: controller reports no namespaces\r\n");
        return false;
    }

    uint64_t namespace_physical = 0;
    volatile uint8_t *namespace_bytes = NULL;
    if (!nvme_allocate_dma_page(&namespace_physical, &namespace_bytes)) {
        serial_write("NVMe: unable to allocate namespace Identify DMA page\r\n");
        return false;
    }

    submission_queue[1].opcode = 0x06U;
    submission_queue[1].command_id = 2U;
    submission_queue[1].namespace_id = 1U;
    submission_queue[1].prp1 = namespace_physical;
    submission_queue[1].cdw10 = 0U; /* CNS=0: Identify Namespace */

    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[0] = 2U;

    completed = false;
    completion_status = 0;
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        completion_status = completion_queue[1].status;
        if ((completion_status & 1U) != 0) {
            completed = true;
            break;
        }
        controller_status = registers[NVME_REG_CSTS / 4U];
        if (controller_status == UINT32_MAX ||
            (controller_status & NVME_CSTS_FATAL) != 0) {
            break;
        }
        __asm__ volatile ("pause");
    }

    if (!completed || completion_queue[1].command_id != 2U ||
        ((completion_status >> 1) & 0xFFU) != 0 ||
        ((completion_status >> 9) & 0x07U) != 0) {
        serial_write("NVMe: Identify Namespace command failed, status=");
        serial_write_hex(completion_status);
        serial_write("\r\n");
        return false;
    }

    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[doorbell_stride_bytes / 4U] = 2U;

    const uint64_t namespace_size =
        (uint64_t)namespace_bytes[0] |
        ((uint64_t)namespace_bytes[1] << 8) |
        ((uint64_t)namespace_bytes[2] << 16) |
        ((uint64_t)namespace_bytes[3] << 24) |
        ((uint64_t)namespace_bytes[4] << 32) |
        ((uint64_t)namespace_bytes[5] << 40) |
        ((uint64_t)namespace_bytes[6] << 48) |
        ((uint64_t)namespace_bytes[7] << 56);
    const uint8_t formatted_lba = namespace_bytes[26] & 0x0FU;
    const uint8_t lba_format_count = namespace_bytes[25] + 1U;
    if (namespace_size == 0 || formatted_lba >= lba_format_count) {
        serial_write("NVMe: namespace Identify returned invalid capacity or LBA format\r\n");
        return false;
    }

    const uint32_t lba_format_offset = 128U + (uint32_t)formatted_lba * 4U;
    const uint8_t lba_data_size_exponent =
        namespace_bytes[lba_format_offset + 2U];
    if (lba_data_size_exponent < 9U || lba_data_size_exponent > 16U) {
        serial_write("NVMe: namespace logical block size is outside current support\r\n");
        return false;
    }
    const uint32_t logical_block_size = 1U << lba_data_size_exponent;

    serial_write("NVMe: namespace 1 capacity blocks low/high: ");
    serial_write_hex((uint32_t)namespace_size);
    serial_write("/");
    serial_write_hex((uint32_t)(namespace_size >> 32));
    serial_write("\r\nNVMe: namespace logical block bytes: ");
    serial_write_hex(logical_block_size);
    serial_write("\r\n");
    serial_write("NVMe: Identify Namespace completed; no sector I/O issued\r\n");
    serial_write("NEXUS NVME NAMESPACE READY\r\n");

    /*
     * Create one polled I/O completion queue and submission queue. These
     * queues are used only for a single read of LBA 0 from the disposable
     * QEMU test namespace. No write opcode is ever submitted.
     */
    uint64_t io_submission_physical = 0;
    uint64_t io_completion_physical = 0;
    uint64_t read_buffer_physical = 0;
    volatile uint8_t *io_submission_bytes = NULL;
    volatile uint8_t *io_completion_bytes = NULL;
    volatile uint8_t *read_buffer_bytes = NULL;
    if (!nvme_allocate_dma_page(&io_submission_physical,
                                &io_submission_bytes) ||
        !nvme_allocate_dma_page(&io_completion_physical,
                                &io_completion_bytes) ||
        !nvme_allocate_dma_page(&read_buffer_physical,
                                &read_buffer_bytes)) {
        serial_write("NVMe: unable to allocate I/O queue and read DMA pages\r\n");
        return false;
    }

    volatile struct nvme_command *io_submission_queue =
        (volatile struct nvme_command *)io_submission_bytes;
    volatile struct nvme_completion *io_completion_queue =
        (volatile struct nvme_completion *)io_completion_bytes;

    /* Admin command 3: Create I/O Completion Queue, QID=1, 16 entries. */
    submission_queue[2].opcode = 0x05U;
    submission_queue[2].command_id = 3U;
    submission_queue[2].prp1 = io_completion_physical;
    submission_queue[2].cdw10 = (15U << 16) | 1U;
    submission_queue[2].cdw11 = 1U; /* Physically contiguous, polling. */
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[0] = 3U;

    completed = false;
    completion_status = 0;
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        completion_status = completion_queue[2].status;
        if ((completion_status & 1U) != 0) {
            completed = true;
            break;
        }
        controller_status = registers[NVME_REG_CSTS / 4U];
        if (controller_status == UINT32_MAX ||
            (controller_status & NVME_CSTS_FATAL) != 0) break;
        __asm__ volatile ("pause");
    }
    if (!completed || completion_queue[2].command_id != 3U ||
        ((completion_status >> 1) & 0xFFU) != 0 ||
        ((completion_status >> 9) & 0x07U) != 0) {
        serial_write("NVMe: Create I/O Completion Queue failed, status=");
        serial_write_hex(completion_status);
        serial_write("\r\n");
        return false;
    }
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[doorbell_stride_bytes / 4U] = 3U;

    /* Admin command 4: Create I/O Submission Queue, QID=1, 16 entries. */
    submission_queue[3].opcode = 0x01U;
    submission_queue[3].command_id = 4U;
    submission_queue[3].prp1 = io_submission_physical;
    submission_queue[3].cdw10 = (15U << 16) | 1U;
    submission_queue[3].cdw11 = (1U << 16) | 1U; /* CQID=1, PC=1. */
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[0] = 4U;

    completed = false;
    completion_status = 0;
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        completion_status = completion_queue[3].status;
        if ((completion_status & 1U) != 0) {
            completed = true;
            break;
        }
        controller_status = registers[NVME_REG_CSTS / 4U];
        if (controller_status == UINT32_MAX ||
            (controller_status & NVME_CSTS_FATAL) != 0) break;
        __asm__ volatile ("pause");
    }
    if (!completed || completion_queue[3].command_id != 4U ||
        ((completion_status >> 1) & 0xFFU) != 0 ||
        ((completion_status >> 9) & 0x07U) != 0) {
        serial_write("NVMe: Create I/O Submission Queue failed, status=");
        serial_write_hex(completion_status);
        serial_write("\r\n");
        return false;
    }
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[doorbell_stride_bytes / 4U] = 4U;

    /* One-block Read (opcode 0x02), NSID=1, SLBA=0, NLB=0. */
    io_submission_queue[0].opcode = 0x02U;
    io_submission_queue[0].command_id = 5U;
    io_submission_queue[0].namespace_id = 1U;
    io_submission_queue[0].prp1 = read_buffer_physical;
    io_submission_queue[0].cdw10 = 0U;
    io_submission_queue[0].cdw11 = 0U;
    io_submission_queue[0].cdw12 = 0U;
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[(2U * doorbell_stride_bytes) / 4U] = 1U;

    completed = false;
    completion_status = 0;
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        completion_status = io_completion_queue[0].status;
        if ((completion_status & 1U) != 0) {
            completed = true;
            break;
        }
        controller_status = registers[NVME_REG_CSTS / 4U];
        if (controller_status == UINT32_MAX ||
            (controller_status & NVME_CSTS_FATAL) != 0) break;
        __asm__ volatile ("pause");
    }
    if (!completed || io_completion_queue[0].command_id != 5U ||
        ((completion_status >> 1) & 0xFFU) != 0 ||
        ((completion_status >> 9) & 0x07U) != 0) {
        serial_write("NVMe: one-block read failed, status=");
        serial_write_hex(completion_status);
        serial_write("\r\n");
        return false;
    }
    __asm__ volatile ("sfence" : : : "memory");
    doorbell_page[(3U * doorbell_stride_bytes) / 4U] = 1U;

    static const char expected_test_data[] = "NEXUS-READ-ONLY-TEST";
    bool test_data_matches = true;
    for (size_t i = 0; i < sizeof(expected_test_data) - 1U; ++i) {
        if (read_buffer_bytes[i] != (uint8_t)expected_test_data[i]) {
            test_data_matches = false;
            break;
        }
    }
    if (!test_data_matches) {
        serial_write("NVMe: read data did not match the disposable test image\r\n");
        return false;
    }

    serial_write("NVMe: read-only LBA 0 verified against test image signature\r\n");
    serial_write("NEXUS NVME READ ONLY READY\r\n");
    serial_write("NEXUS NVME IDENTIFY READY\r\n");
    return true;
}
