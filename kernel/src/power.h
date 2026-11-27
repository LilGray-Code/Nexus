#ifndef NEXUS_POWER_H
#define NEXUS_POWER_H

#include <stdbool.h>
#include <stdint.h>

enum nexus_power_profile {
    NEXUS_POWER_PROFILE_UNKNOWN = 0,
    NEXUS_POWER_PROFILE_POWER_SAVER,
    NEXUS_POWER_PROFILE_BALANCED,
    NEXUS_POWER_PROFILE_PERFORMANCE
};

struct nexus_power_snapshot {
    bool acpi_shutdown_available;
    bool acpi_restart_available;
    bool suspend_available;
    bool hibernate_available;

    bool battery_available;
    bool battery_percentage_available;
    uint8_t battery_percentage;
    bool charging_status_available;
    bool battery_charging;
    bool external_power_available;
    bool external_power_connected;

    bool cpu_temperature_available;
    uint8_t cpu_temperature_celsius;
    bool cpu_hwp_profile_available;
    enum nexus_power_profile current_profile;

    bool fan_control_available;
    bool lid_detection_available;
    bool native_backlight_available;
};

bool nexus_power_init(void);
bool nexus_power_get_snapshot(struct nexus_power_snapshot *snapshot);
bool nexus_power_set_profile(enum nexus_power_profile profile);

/* The operations return false when the validated firmware path is absent.
 * On success, shutdown/restart transfer control to firmware and do not return.
 */
bool nexus_power_shutdown(void);
bool nexus_power_restart(void);

/* Pure helpers are exposed so their boundary conditions can be host-tested. */
uint8_t nexus_power_profile_epp(enum nexus_power_profile profile);
bool nexus_power_decode_intel_temperature(uint64_t thermal_status,
                                          uint64_t temperature_target,
                                          uint8_t *temperature_celsius);

#endif
