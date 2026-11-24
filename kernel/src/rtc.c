#include "rtc.h"

#include <stddef.h>
#include <stdint.h>

#define CMOS_INDEX_PORT 0x70U
#define CMOS_DATA_PORT 0x71U
#define CMOS_SECONDS 0x00U
#define CMOS_MINUTES 0x02U
#define CMOS_HOURS 0x04U
#define CMOS_DAY 0x07U
#define CMOS_MONTH 0x08U
#define CMOS_YEAR 0x09U
#define CMOS_STATUS_A 0x0AU
#define CMOS_STATUS_B 0x0BU
#define CMOS_CENTURY 0x32U
#define CMOS_UPDATE_IN_PROGRESS 0x80U
#define CMOS_BINARY_MODE 0x04U
#define CMOS_24_HOUR_MODE 0x02U
#define CMOS_PM 0x80U
#define CMOS_POLL_LIMIT 100000U
#define UNIX_EPOCH_DAYS_OFFSET 719468LL
#define UNIX_EPOCH_MIN_2020 1577836800ULL

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t reg) {
    const uint8_t previous_index = inb(CMOS_INDEX_PORT);
    outb(CMOS_INDEX_PORT, (uint8_t)((previous_index & 0x80U) | reg));
    const uint8_t value = inb(CMOS_DATA_PORT);
    outb(CMOS_INDEX_PORT, previous_index);
    return value;
}

static bool wait_for_update_complete(void) {
    for (unsigned spin = 0U; spin < CMOS_POLL_LIMIT; ++spin) {
        if ((cmos_read(CMOS_STATUS_A) & CMOS_UPDATE_IN_PROGRESS) == 0U)
            return true;
        __asm__ volatile ("pause");
    }
    return false;
}

static bool bcd_to_binary(uint8_t value, uint8_t *decoded) {
    if (decoded == NULL) return false;
    const uint8_t low = value & 0x0FU;
    const uint8_t high = (value >> 4U) & 0x0FU;
    if (low > 9U || high > 9U) return false;
    *decoded = (uint8_t)(high * 10U + low);
    return true;
}

static bool decode_register(uint8_t value, bool binary_mode,
                            uint8_t *decoded) {
    if (decoded == NULL) return false;
    if (binary_mode) {
        *decoded = value;
        return true;
    }
    return bcd_to_binary(value, decoded);
}

static bool rtc_read_fields(struct nexus_rtc_fields *fields) {
    if (fields == NULL) return false;

    for (unsigned sample = 0U; sample < 8U; ++sample) {
        if (!wait_for_update_complete()) return false;

        uint8_t second_raw = cmos_read(CMOS_SECONDS);
        uint8_t minute_raw = cmos_read(CMOS_MINUTES);
        uint8_t hour_raw = cmos_read(CMOS_HOURS);
        uint8_t day_raw = cmos_read(CMOS_DAY);
        uint8_t month_raw = cmos_read(CMOS_MONTH);
        uint8_t year_raw = cmos_read(CMOS_YEAR);
        uint8_t century_raw = cmos_read(CMOS_CENTURY);
        const uint8_t status_b = cmos_read(CMOS_STATUS_B);

        if (!wait_for_update_complete()) return false;
        if (cmos_read(CMOS_SECONDS) != second_raw) continue;

        const bool binary_mode = (status_b & CMOS_BINARY_MODE) != 0U;
        const bool mode_24_hour = (status_b & CMOS_24_HOUR_MODE) != 0U;
        const bool pm = !mode_24_hour && (hour_raw & CMOS_PM) != 0U;
        hour_raw &= (uint8_t)~CMOS_PM;

        uint8_t second = 0U, minute = 0U, hour = 0U;
        uint8_t day = 0U, month = 0U, year = 0U, century = 0U;
        if (!decode_register(second_raw, binary_mode, &second) ||
            !decode_register(minute_raw, binary_mode, &minute) ||
            !decode_register(hour_raw, binary_mode, &hour) ||
            !decode_register(day_raw, binary_mode, &day) ||
            !decode_register(month_raw, binary_mode, &month) ||
            !decode_register(year_raw, binary_mode, &year)) return false;

        bool has_century = century_raw != 0U && century_raw != 0xFFU;
        if (has_century) {
            if (!decode_register(century_raw, binary_mode, &century))
                has_century = false;
        }

        if (mode_24_hour) {
            /* The 24-hour register has no PM flag. */
        } else {
            if (hour == 0U || hour > 12U) return false;
            if (hour == 12U) hour = 0U;
            if (pm) hour = (uint8_t)(hour + 12U);
        }

        uint16_t full_year;
        if (has_century && century >= 19U && century <= 99U) {
            full_year = (uint16_t)(century * 100U + year);
        } else {
            full_year = (uint16_t)(year >= 70U ? 1900U + year
                                               : 2000U + year);
        }

        *fields = (struct nexus_rtc_fields){
            .year = full_year,
            .month = month,
            .day = day,
            .hour = hour,
            .minute = minute,
            .second = second
        };
        return true;
    }
    return false;
}

static bool is_leap_year(uint16_t year) {
    return (year % 4U == 0U) &&
           ((year % 100U != 0U) || (year % 400U == 0U));
}

static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t month_days[12] = {
        31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U
    };
    if (month == 0U || month > 12U) return 0U;
    if (month == 2U && is_leap_year(year)) return 29U;
    return month_days[month - 1U];
}

bool rtc_fields_to_unix(const struct nexus_rtc_fields *fields,
                        uint64_t *unix_seconds) {
    if (fields == NULL || unix_seconds == NULL ||
        fields->year < 1970U || fields->month < 1U || fields->month > 12U ||
        fields->day < 1U ||
        fields->day > days_in_month(fields->year, fields->month) ||
        fields->hour > 23U || fields->minute > 59U ||
        fields->second > 59U) return false;

    int64_t year = (int64_t)fields->year;
    const int64_t month = (int64_t)fields->month;
    const int64_t day = (int64_t)fields->day;
    year -= month <= 2LL ? 1LL : 0LL;

    const int64_t era = year / 400LL;
    const int64_t year_of_era = year - era * 400LL;
    const int64_t adjusted_month = month + (month > 2LL ? -3LL : 9LL);
    const int64_t day_of_year =
        (153LL * adjusted_month + 2LL) / 5LL + day - 1LL;
    const int64_t day_of_era =
        year_of_era * 365LL + year_of_era / 4LL -
        year_of_era / 100LL + day_of_year;
    const int64_t days = era * 146097LL + day_of_era -
                         UNIX_EPOCH_DAYS_OFFSET;
    if (days < 0LL) return false;

    const uint64_t result = (uint64_t)days * 86400ULL +
                            (uint64_t)fields->hour * 3600ULL +
                            (uint64_t)fields->minute * 60ULL +
                            fields->second;
    *unix_seconds = result;
    return true;
}

bool rtc_get_unix_time(uint64_t *unix_seconds) {
    if (unix_seconds == NULL) return false;
    *unix_seconds = 0U;

    struct nexus_rtc_fields fields;
    uint64_t value = 0U;
    if (!rtc_read_fields(&fields) || !rtc_fields_to_unix(&fields, &value) ||
        value < UNIX_EPOCH_MIN_2020) return false;

    *unix_seconds = value;
    return true;
}
