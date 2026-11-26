#include "win_driver_package.h"

bool nexus_win_assess_driver_package(
    const struct nexus_win_device_id *device,
    const char *inf_data, size_t inf_size,
    const void *sys_data, size_t sys_size,
    struct nexus_win_driver_package_assessment *assessment) {
    if (device == NULL || inf_data == NULL || sys_data == NULL ||
        assessment == NULL) return false;
    *assessment = (struct nexus_win_driver_package_assessment){0};
    if (!nexus_win_inf_find_match(inf_data, inf_size, device,
                                  &assessment->inf_match))
        return false;
    assessment->pe_valid = nexus_win_pe_inspect(
        sys_data, sys_size, &assessment->pe_info);
    assessment->profile = assessment->pe_valid
        ? nexus_win_pe_classify_wifi_driver(&assessment->pe_info)
        : NEXUS_WIN_WIFI_PROFILE_INVALID;
    return true;
}
