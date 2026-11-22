#include "ahci.h"

#include "blockdev.h"
#include "memory.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define AHCI_PORT_BASE             0x100U
#define AHCI_PORT_STRIDE           0x80U
#define AHCI_DMA_LIMIT             0x100000000ULL
#define AHCI_POLL_LIMIT            10000000U
#define AHCI_MAX_REGISTERED_PORTS  4U
#define AHCI_PAGE_SIZE             4096U

#define AHCI_GHC_AE                (1U << 31)
#define AHCI_GHC_IE                (1U << 1)
#define AHCI_PXCMD_ST              (1U << 0)
#define AHCI_PXCMD_FRE             (1U << 4)
#define AHCI_PXCMD_FR              (1U << 14)
#define AHCI_PXCMD_CR              (1U << 15)
#define AHCI_PXIS_TFES             (1U << 30)
#define AHCI_PXTFD_ERR             (1U << 0)
#define AHCI_PXTFD_DRQ             (1U << 3)
#define AHCI_PXTFD_BSY             (1U << 7)
#define AHCI_SIG_ATA               0x00000101U
#define ATA_CMD_IDENTIFY           0xECU
#define ATA_CMD_READ_DMA_EXT       0x25U

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred_bytes;
    uint32_t command_table_low;
    uint32_t command_table_high;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t data_base_low;
    uint32_t data_base_high;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
} __attribute__((packed));

struct ahci_command_table {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
} __attribute__((packed, aligned(128)));

_Static_assert(sizeof(struct ahci_command_header) == 32U,
               "AHCI command header must be 32 bytes");
_Static_assert(offsetof(struct ahci_command_table, prdt) == 128U,
               "AHCI PRDT must follow the 128-byte command-table prefix");

struct ahci_port_state {
    volatile uint32_t *registers;
    volatile struct ahci_command_header *command_list;
    volatile uint8_t *received_fis;
    volatile struct ahci_command_table *command_table;
    volatile uint8_t *dma_buffer;
    uint64_t command_list_physical;
    uint64_t received_fis_physical;
    uint64_t command_table_physical;
    uint64_t dma_buffer_physical;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t port_number;
    uint32_t pci_bus;
    uint32_t pci_slot;
    char stable_id[NEXUS_BLOCKDEV_ID_MAX];
    char model[NEXUS_BLOCKDEV_MODEL_MAX];
    bool initialized;
    bool failed;
};

static struct ahci_port_state registered_ports[AHCI_MAX_REGISTERED_PORTS];
static size_t registered_port_count;

static bool ahci_map_register(uint64_t physical_address,
                              volatile uint32_t **register_address) {
    if (register_address == NULL) return false;
    volatile uint32_t *mapped_page = NULL;
    if (!paging_map_mmio_page(physical_address, &mapped_page) ||
        mapped_page == NULL) return false;
    *register_address = (volatile uint32_t *)(
        (uintptr_t)mapped_page + (uintptr_t)(physical_address & 0xFFFU));
    return true;
}

static bool ahci_allocate_dma_page(uint64_t *physical,
                                   volatile uint8_t **virtual_address) {
    if (physical == NULL || virtual_address == NULL ||
        !memory_alloc_dma_page(AHCI_DMA_LIMIT, physical)) return false;
    volatile uint8_t *page =
        (volatile uint8_t *)memory_phys_to_virt(*physical);
    if (page == NULL || *physical > UINT32_MAX - (AHCI_PAGE_SIZE - 1U))
        return false;
    for (size_t i = 0U; i < AHCI_PAGE_SIZE; ++i) page[i] = 0U;
    *virtual_address = page;
    return true;
}

static bool ahci_wait_port_bit(volatile uint32_t *port,
                               uint32_t register_offset,
                               uint32_t mask, bool set) {
    for (uint32_t spin = 0U; spin < AHCI_POLL_LIMIT; ++spin) {
        const uint32_t value = port[register_offset / 4U];
        if (value == UINT32_MAX) return false;
        if (((value & mask) != 0U) == set) return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool ahci_stop_engine(volatile uint32_t *port) {
    uint32_t command = port[0x18U / 4U];
    if (command == UINT32_MAX) return false;
    command &= ~AHCI_PXCMD_ST;
    port[0x18U / 4U] = command;
    if (!ahci_wait_port_bit(port, 0x18U, AHCI_PXCMD_CR, false))
        return false;

    command = port[0x18U / 4U];
    command &= ~AHCI_PXCMD_FRE;
    port[0x18U / 4U] = command;
    return ahci_wait_port_bit(port, 0x18U, AHCI_PXCMD_FR, false);
}

static bool ahci_start_engine(volatile uint32_t *port) {
    uint32_t command = port[0x18U / 4U];
    if (command == UINT32_MAX) return false;
    port[0x18U / 4U] = command | AHCI_PXCMD_FRE;
    command |= AHCI_PXCMD_FRE | AHCI_PXCMD_ST;
    port[0x18U / 4U] = command;
    return ahci_wait_port_bit(port, 0x18U, AHCI_PXCMD_ST, true);
}

static bool ahci_wait_taskfile_ready(struct ahci_port_state *state) {
    volatile uint32_t *port = state->registers;
    for (uint32_t spin = 0U; spin < AHCI_POLL_LIMIT; ++spin) {
        const uint32_t task_file = port[0x20U / 4U];
        if (task_file == UINT32_MAX ||
            (port[0x10U / 4U] & AHCI_PXIS_TFES) != 0U) return false;
        if ((task_file & (AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ)) == 0U)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

/*
 * All transfers use one page-sized DMA buffer and one PRDT entry. A timed-out
 * command permanently poisons this port instance: reusing a possibly active
 * DMA page after an ambiguous completion would be unsafe.
 */
static bool ahci_issue_command(struct ahci_port_state *state,
                               const uint8_t fis[20], bool write,
                               uint32_t transfer_bytes) {
    if (state == NULL || !state->initialized || state->failed ||
        fis == NULL || transfer_bytes == 0U ||
        transfer_bytes > AHCI_PAGE_SIZE) return false;

    volatile uint32_t *port = state->registers;
    if (!ahci_wait_taskfile_ready(state)) return false;
    if ((port[0x38U / 4U] & 1U) != 0U) return false;

    volatile struct ahci_command_header *header = &state->command_list[0];
    volatile struct ahci_command_table *table = state->command_table;

    for (size_t i = 0U; i < 64U; ++i) table->command_fis[i] = 0U;
    for (size_t i = 0U; i < 20U; ++i) table->command_fis[i] = fis[i];
    table->prdt[0].data_base_low = (uint32_t)state->dma_buffer_physical;
    table->prdt[0].data_base_high = 0U;
    table->prdt[0].reserved = 0U;
    table->prdt[0].byte_count_and_interrupt =
        (transfer_bytes - 1U) | (1U << 31);

    header->flags = (uint16_t)(5U | (write ? (1U << 6) : 0U));
    header->prdt_length = 1U;
    header->transferred_bytes = 0U;
    header->command_table_low = (uint32_t)state->command_table_physical;
    header->command_table_high = 0U;

    port[0x10U / 4U] = UINT32_MAX;
    port[0x30U / 4U] = UINT32_MAX;
    __asm__ volatile ("sfence" : : : "memory");
    port[0x38U / 4U] = 1U;

    bool completed = false;
    for (uint32_t spin = 0U; spin < AHCI_POLL_LIMIT; ++spin) {
        const uint32_t active = port[0x38U / 4U];
        const uint32_t interrupt_status = port[0x10U / 4U];
        const uint32_t task_file = port[0x20U / 4U];
        if (active == UINT32_MAX || interrupt_status == UINT32_MAX ||
            task_file == UINT32_MAX) break;
        if ((interrupt_status & AHCI_PXIS_TFES) != 0U ||
            (task_file & AHCI_PXTFD_ERR) != 0U) break;
        if ((active & 1U) == 0U) {
            completed = true;
            break;
        }
        __asm__ volatile ("pause");
    }

    const uint32_t interrupt_status = port[0x10U / 4U];
    const uint32_t task_file = port[0x20U / 4U];
    if (!completed || (interrupt_status & AHCI_PXIS_TFES) != 0U ||
        (task_file & AHCI_PXTFD_ERR) != 0U ||
        header->transferred_bytes != transfer_bytes) {
        state->failed = true;
        serial_write("AHCI: command failed or timed out; port locked\r\n");
        return false;
    }

    __asm__ volatile ("sfence" : : : "memory");
    port[0x10U / 4U] = UINT32_MAX;
    port[0x30U / 4U] = UINT32_MAX;
    return true;
}

static uint16_t ahci_identify_word(const volatile uint8_t *identify,
                                  uint32_t word) {
    const uint32_t offset = word * 2U;
    return (uint16_t)((uint16_t)identify[offset] |
                      ((uint16_t)identify[offset + 1U] << 8));
}

static uint64_t ahci_identify_u64(const volatile uint8_t *identify,
                                  uint32_t first_word) {
    uint64_t value = 0U;
    for (uint32_t i = 0U; i < 4U; ++i)
        value |= (uint64_t)ahci_identify_word(identify, first_word + i)
                 << (i * 16U);
    return value;
}

static void ahci_identify_string(char *destination, size_t capacity,
                                 const volatile uint8_t *identify,
                                 uint32_t first_word, uint32_t word_count) {
    if (destination == NULL || capacity == 0U) return;
    size_t output = 0U;
    for (uint32_t i = 0U; i < word_count && output + 1U < capacity; ++i) {
        const uint32_t offset = (first_word + i) * 2U;
        const uint8_t pair[2] = {identify[offset + 1U], identify[offset]};
        for (size_t j = 0U; j < 2U && output + 1U < capacity; ++j) {
            const uint8_t ch = pair[j];
            destination[output++] =
                (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : '?';
        }
    }
    while (output > 0U && destination[output - 1U] == ' ')
        --output;
    destination[output] = '\0';
}

static bool ahci_identify(struct ahci_port_state *state) {
    uint8_t fis[20] = {0};
    fis[0] = 0x27U;       /* Register host-to-device FIS. */
    fis[1] = 0x80U;       /* Command bit. */
    fis[2] = ATA_CMD_IDENTIFY;
    fis[7] = 0x40U;
    if (!ahci_issue_command(state, fis, false, 512U)) return false;

    const volatile uint8_t *identify = state->dma_buffer;
    const uint16_t command_sets = ahci_identify_word(identify, 83U);
    if ((command_sets & (1U << 10)) == 0U) {
        serial_write("AHCI: ATA device lacks LBA48; not registered\r\n");
        return false;
    }

    const uint64_t blocks = ahci_identify_u64(identify, 100U);
    if (blocks == 0U) return false;

    uint32_t logical_sector_size = 512U;
    const uint16_t geometry = ahci_identify_word(identify, 106U);
    if ((geometry & 0xC000U) == 0x4000U &&
        (geometry & (1U << 12)) != 0U) {
        const uint32_t words =
            (uint32_t)ahci_identify_word(identify, 117U) |
            ((uint32_t)ahci_identify_word(identify, 118U) << 16);
        if (words > UINT32_MAX / 2U) return false;
        logical_sector_size = words * 2U;
    }
    if (logical_sector_size != 512U && logical_sector_size != 4096U) {
        serial_write("AHCI: unsupported logical sector size\r\n");
        return false;
    }

    state->block_count = blocks;
    state->block_size = logical_sector_size;
    ahci_identify_string(state->stable_id, sizeof(state->stable_id),
                         identify, 10U, 10U);
    ahci_identify_string(state->model, sizeof(state->model),
                         identify, 27U, 20U);
    return true;
}

static bool ahci_read_blocks(void *context, uint64_t lba,
                             uint32_t block_count, void *buffer) {
    struct ahci_port_state *state = (struct ahci_port_state *)context;
    if (state == NULL || !state->initialized || state->failed ||
        buffer == NULL || block_count == 0U ||
        state->block_size == 0U ||
        lba >= state->block_count ||
        (uint64_t)block_count > state->block_count - lba ||
        block_count > AHCI_PAGE_SIZE / state->block_size) return false;

    const uint32_t byte_count = block_count * state->block_size;
    for (uint32_t i = 0U; i < byte_count; ++i)
        state->dma_buffer[i] = 0U;

    uint8_t fis[20] = {0};
    fis[0] = 0x27U;
    fis[1] = 0x80U;
    fis[2] = ATA_CMD_READ_DMA_EXT;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40U;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)block_count;
    fis[13] = (uint8_t)(block_count >> 8);

    if (!ahci_issue_command(state, fis, false, byte_count)) return false;
    uint8_t *output = (uint8_t *)buffer;
    for (uint32_t i = 0U; i < byte_count; ++i)
        output[i] = state->dma_buffer[i];
    return true;
}

#ifdef NEXUS_STORAGE_SELFTEST
static bool ahci_read_selftest(const struct nexus_block_device *device) {
    uint8_t sector[4096];
    static const char mbr_signature[] = "NEXUS-READ-ONLY-TEST";
    static const char gpt_signature[] = "EFI PART";
    if (device == NULL || device->block_size != 512U ||
        (device->flags & NEXUS_BLOCKDEV_READ_ONLY) == 0U ||
        (device->flags & NEXUS_BLOCKDEV_WRITABLE) != 0U ||
        device->write != NULL || device->flush != NULL ||
        blockdev_install_write_supported(device) ||
        blockdev_write(device, 100U, 1U, sector) ||
        blockdev_flush(device) ||
        !blockdev_read(device, 0U, 1U, sector)) return false;
    for (size_t i = 0U; i < sizeof(mbr_signature) - 1U; ++i)
        if (sector[i] != (uint8_t)mbr_signature[i]) return false;
    if (!blockdev_read(device, 1U, 1U, sector)) return false;
    for (size_t i = 0U; i < sizeof(gpt_signature) - 1U; ++i)
        if (sector[i] != (uint8_t)gpt_signature[i]) return false;
    serial_write("AHCI: disposable SATA sectors 0 and 1 read and verified\r\n");
    serial_write("NEXUS AHCI READ SELFTEST READY\r\n");
    return true;
}
#endif

static bool ahci_prepare_port(const struct nexus_pci_device *pci_device,
                              uint64_t abar, uint32_t port_number,
                              size_t port_index) {
    if (port_index >= AHCI_MAX_REGISTERED_PORTS) return false;
    const uint64_t port_offset =
        AHCI_PORT_BASE + (uint64_t)port_number * AHCI_PORT_STRIDE;
    if (abar > UINT64_MAX - port_offset) return false;

    struct ahci_port_state *state = &registered_ports[port_index];
    *state = (struct ahci_port_state){0};
    if (!ahci_map_register(abar + port_offset, &state->registers)) {
        serial_write("AHCI: cannot map SATA port registers\r\n");
        return false;
    }

    volatile uint32_t *port = state->registers;
    const uint32_t ssts = port[0x28U / 4U];
    const uint32_t signature = port[0x24U / 4U];
    if (ssts == UINT32_MAX || (ssts & 0x0FU) != 3U ||
        signature != AHCI_SIG_ATA) return false;

    if (!ahci_stop_engine(port)) {
        serial_write("AHCI: port engine did not stop safely\r\n");
        return false;
    }
    port[0x10U / 4U] = UINT32_MAX;
    port[0x30U / 4U] = UINT32_MAX;
    port[0x14U / 4U] = 0U;

    volatile uint8_t *dma_page = NULL;
    if (!ahci_allocate_dma_page(&state->command_list_physical, &dma_page))
        return false;
    state->command_list = (volatile struct ahci_command_header *)dma_page;
    if (!ahci_allocate_dma_page(&state->received_fis_physical,
                                &state->received_fis)) return false;
    if (!ahci_allocate_dma_page(&state->command_table_physical, &dma_page))
        return false;
    state->command_table = (volatile struct ahci_command_table *)dma_page;
    if (!ahci_allocate_dma_page(&state->dma_buffer_physical,
                                &state->dma_buffer)) return false;

    port[0x00U / 4U] = (uint32_t)state->command_list_physical;
    port[0x04U / 4U] = 0U;
    port[0x08U / 4U] = (uint32_t)state->received_fis_physical;
    port[0x0CU / 4U] = 0U;
    state->port_number = port_number;
    state->pci_bus = pci_device->bus;
    state->pci_slot = pci_device->slot;
    state->initialized = true;

    if (!ahci_start_engine(port) || !ahci_identify(state)) {
        state->initialized = false;
        serial_write("AHCI: SATA IDENTIFY failed; port not registered\r\n");
        return false;
    }

    size_t serial_length = 0U;
    while (serial_length < 20U &&
           state->stable_id[serial_length] != '\0') ++serial_length;
    if (serial_length > sizeof(state->stable_id) - 7U)
        serial_length = sizeof(state->stable_id) - 7U;
    for (size_t i = serial_length; i > 0U; --i)
        state->stable_id[i + 4U] = state->stable_id[i - 1U];
    const char prefix[] = "SATA-";
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
        state->stable_id[i] = prefix[i];
    if (serial_length == 0U) {
        static const char unknown[] = "UNKNOWN";
        for (size_t i = 0U; i < sizeof(unknown); ++i)
            state->stable_id[5U + i] = unknown[i];
    } else {
        state->stable_id[5U + serial_length] = '\0';
    }

    if (state->model[0] == '\0') {
        static const char fallback[] = "ATA disk";
        size_t i = 0U;
        while (fallback[i] != '\0' && i < sizeof(state->model) - 1U) {
            state->model[i] = fallback[i];
            ++i;
        }
        state->model[i] = '\0';
    }

    struct nexus_block_device device = {
        .transport = NEXUS_BLOCKDEV_TRANSPORT_AHCI,
        .block_count = state->block_count,
        .block_size = state->block_size,
        .max_blocks_per_request = AHCI_PAGE_SIZE / state->block_size,
        .flags = NEXUS_BLOCKDEV_READ_ONLY,
        .context = state,
        .read = ahci_read_blocks,
        .write = NULL,
        .flush = NULL
    };
    const char name_prefix[] = "sata";
    for (size_t i = 0U; i < sizeof(name_prefix) - 1U; ++i)
        device.name[i] = name_prefix[i];
    device.name[sizeof(name_prefix) - 1U] =
        (char)('0' + (port_index % 10U));
    device.name[sizeof(name_prefix)] = '\0';
    for (size_t i = 0U; i < sizeof(state->stable_id) &&
         i < sizeof(device.stable_id); ++i)
        device.stable_id[i] = state->stable_id[i];
    for (size_t i = 0U; i < sizeof(state->model) &&
         i < sizeof(device.model); ++i)
        device.model[i] = state->model[i];

    if (!blockdev_register(&device)) {
        state->initialized = false;
        serial_write("AHCI: unable to register read-only SATA block device\r\n");
        return false;
    }

    const struct nexus_block_device *registered =
        blockdev_at(blockdev_count() - 1U);
    if (registered == NULL) return false;
    ++registered_port_count;

    serial_write("AHCI: registered read-only SATA device ");
    serial_write(registered->name);
    serial_write(" model=");
    serial_write(registered->model);
    serial_write(" blocks=");
    serial_write_hex(registered->block_count);
    serial_write("\r\n");

#ifdef NEXUS_STORAGE_SELFTEST
    if (!ahci_read_selftest(registered)) {
        state->failed = true;
        serial_write("AHCI: disposable SATA read self-test failed\r\n");
        return false;
    }
#endif
    return true;
}

bool ahci_init_controller(const struct nexus_pci_device *device) {
    if (device == NULL || device->class_code != 0x01 ||
        device->subclass != 0x06 || device->programming_interface != 0x01)
        return false;
    if (registered_port_count >= AHCI_MAX_REGISTERED_PORTS) return false;

    struct nexus_pci_bar abar;
    if (!pci_decode_bar(device, 5U, &abar) || abar.io_space ||
        !abar.assigned || (abar.base & 0xFFFU) != 0U) {
        serial_write("AHCI: invalid or unassigned ABAR\r\n");
        return false;
    }
    if (!pci_enable_memory_bus_master(device)) {
        serial_write("AHCI: unable to enable PCI memory decoding/bus mastering\r\n");
        return false;
    }

    volatile uint32_t *hba = NULL;
    if (!ahci_map_register(abar.base, &hba)) {
        serial_write("AHCI: unable to map HBA registers\r\n");
        return false;
    }

    const uint32_t cap = hba[0x00U / 4U];
    uint32_t ghc = hba[0x04U / 4U];
    const uint32_t pi = hba[0x0CU / 4U];
    const uint32_t version = hba[0x10U / 4U];
    if (cap == 0U || cap == UINT32_MAX || ghc == UINT32_MAX ||
        pi == UINT32_MAX || version == 0U || version == UINT32_MAX)
        return false;

    /* Enable AHCI mode and keep controller-level interrupts disabled; this
       driver polls command completion and does not install an AHCI IRQ path. */
    ghc = (ghc | AHCI_GHC_AE) & ~AHCI_GHC_IE;
    hba[0x04U / 4U] = ghc;
    if ((hba[0x04U / 4U] & AHCI_GHC_AE) == 0U) return false;

    uint32_t detected = 0U;
    for (uint32_t port = 0U; port < 32U &&
         registered_port_count < AHCI_MAX_REGISTERED_PORTS; ++port) {
        if ((pi & (1U << port)) == 0U) continue;
        if (ahci_prepare_port(device, abar.base, port,
                             registered_port_count)) ++detected;
    }

    serial_write("AHCI: ATA ports registered read-only: ");
    serial_write_hex(detected);
    serial_write("\r\n");
    if (detected != 0U) serial_write("NEXUS AHCI BLOCK DEVICE READY\r\n");
    return detected != 0U;
}
