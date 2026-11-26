#ifndef NEXUS_WIN_DRIVER_INF_H
#define NEXUS_WIN_DRIVER_INF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEXUS_WIN_INF_LINE_MAX 1024U
#define NEXUS_WIN_INF_FIELD_MAX 128U

enum nexus_win_device_bus {
    NEXUS_WIN_DEVICE_PCI = 1,
    NEXUS_WIN_DEVICE_USB = 2
};

struct nexus_win_device_id {
    enum nexus_win_device_bus bus;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t revision_id;
};

struct nexus_win_inf_match {
    char model_description[NEXUS_WIN_INF_FIELD_MAX];
    char install_section[NEXUS_WIN_INF_FIELD_MAX];
    char hardware_id[NEXUS_WIN_INF_FIELD_MAX];
    uint16_t specificity;
    size_t line_number;
};

/* Safely identifies matching PCI/USB devices in Windows INF model sections.
 * It does not execute Windows kernel drivers or interpret install directives.
 */
bool nexus_win_inf_find_match(const char *inf_text, size_t text_length,
                              const struct nexus_win_device_id *device,
                              struct nexus_win_inf_match *match);

#endif
