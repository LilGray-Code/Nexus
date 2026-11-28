#ifndef NEXUS_EHCI_H
#define NEXUS_EHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_EHCI_PAGE_SIZE 4096U
#define NEXUS_EHCI_QTD_TERMINATE 0x00000001U
#define NEXUS_EHCI_LINK_QH       0x00000002U
#define NEXUS_EHCI_QTD_MAX_BYTES 0x7FFFU

enum nexus_ehci_pid {
    NEXUS_EHCI_PID_OUT = 0U,
    NEXUS_EHCI_PID_IN = 1U,
    NEXUS_EHCI_PID_SETUP = 2U
};

enum nexus_ehci_speed {
    NEXUS_EHCI_SPEED_FULL = 0U,
    NEXUS_EHCI_SPEED_LOW = 1U,
    NEXUS_EHCI_SPEED_HIGH = 2U
};

/* EHCI Queue Element Transfer Descriptor. All descriptors are 32-byte aligned
 * and DMA-visible; controller addresses are deliberately constrained to 32-bit
 * DMA until 64-bit schedule pointer support is implemented. */
struct nexus_ehci_qtd {
    uint32_t next_qtd;
    uint32_t alternate_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((packed, aligned(32)));

_Static_assert(sizeof(struct nexus_ehci_qtd) == 32U,
               "EHCI qTD must be 32 bytes");

/* EHCI Queue Head. The hardware consumes the first 48 bytes; the 64-byte
 * allocation/alignment leaves a cache-line-sized object for DMA ownership. */
struct nexus_ehci_qh {
    uint32_t horizontal_link;
    uint32_t endpoint_characteristics;
    uint32_t endpoint_capabilities;
    uint32_t current_qtd;
    uint32_t overlay_next_qtd;
    uint32_t overlay_alternate_next_qtd;
    uint32_t overlay_token;
    uint32_t overlay_buffer[5];
    uint32_t reserved[3];
} __attribute__((packed, aligned(32)));

_Static_assert(sizeof(struct nexus_ehci_qh) == 64U,
               "EHCI QH allocation must be 64 bytes");

struct nexus_ehci_capabilities {
    uint8_t capability_length;
    uint16_t interface_version;
    uint8_t port_count;
    bool port_power_control;
    bool programmable_frame_list;
    bool has_64bit_addressing;
    bool has_legacy_support;
    uint8_t legacy_support_offset;
    uint32_t operational_base_offset;
};

struct nexus_ehci_port_status {
    bool connected;
    bool enabled;
    bool suspended;
    bool over_current;
    bool resetting;
    bool powered;
    uint8_t line_status;
    uint8_t speed_hint;
    uint16_t change_bits;
};

bool nexus_ehci_decode_capabilities(
    uint8_t capability_length, uint16_t interface_version,
    uint32_t structural_parameters, uint32_t capability_parameters,
    struct nexus_ehci_capabilities *capabilities);

bool nexus_ehci_validate_legacy_capability_offset(
    uint8_t extended_capability_pointer, uint8_t *offset);

bool nexus_ehci_decode_port_status(
    uint32_t portsc, struct nexus_ehci_port_status *status);

/* Build a single qTD. page_addresses contains up to five DMA32 physical page
 * bases for the data buffer. Page zero may have a byte offset; later pages must
 * be aligned. All supplied pages must be below 4 GiB. */
bool nexus_ehci_build_qtd(
    struct nexus_ehci_qtd *qtd, uint32_t qtd_physical,
    uint32_t next_qtd_physical, uint32_t alternate_qtd_physical,
    enum nexus_ehci_pid pid, uint16_t byte_count, bool data_toggle,
    bool interrupt_on_complete, const uint32_t page_addresses[5],
    uint16_t first_page_offset);

/* Configure a QH for one USB endpoint and link its first qTD. */
bool nexus_ehci_build_qh(
    struct nexus_ehci_qh *qh, uint32_t qh_physical,
    uint32_t next_qh_physical, uint8_t device_address,
    uint8_t endpoint_number, enum nexus_ehci_speed speed,
    uint16_t maximum_packet, bool control_endpoint,
    bool data_toggle_control, uint8_t nak_reload,
    uint32_t first_qtd_physical);

#endif
