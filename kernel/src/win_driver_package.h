#ifndef NEXUS_WIN_DRIVER_PACKAGE_H
#define NEXUS_WIN_DRIVER_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "win_driver_inf.h"
#include "win_driver_pe.h"

struct nexus_win_driver_package_assessment {
    struct nexus_win_inf_match inf_match;
    struct nexus_win_pe_info pe_info;
    enum nexus_win_wifi_driver_profile profile;
    bool pe_valid;
};

/* Returns true when the INF matches the supplied hardware. A matching INF
 * with an invalid .sys is still reported with pe_valid=false and INVALID.
 * This assesses metadata only; it never installs or executes a driver.
 */
bool nexus_win_assess_driver_package(
    const struct nexus_win_device_id *device,
    const char *inf_data, size_t inf_size,
    const void *sys_data, size_t sys_size,
    struct nexus_win_driver_package_assessment *assessment);

#endif
