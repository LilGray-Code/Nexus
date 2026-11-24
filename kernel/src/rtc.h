#ifndef NEXUS_RTC_H
#define NEXUS_RTC_H

#include <stdbool.h>
#include <stdint.h>

struct nexus_rtc_fields {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

/* Read the motherboard CMOS RTC and require a plausible post-2020 date. */
bool rtc_get_unix_time(uint64_t *unix_seconds);

/* Pure calendar conversion, also used by host-side regression tests. */
bool rtc_fields_to_unix(const struct nexus_rtc_fields *fields,
                        uint64_t *unix_seconds);

#endif
