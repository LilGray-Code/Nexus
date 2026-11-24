#include "mbedtls_time.h"

#include "rtc.h"
#include "timer.h"

#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define UNIX_DAYS_OFFSET 719468LL
#define SECONDS_PER_DAY 86400ULL

static bool leap_year(int64_t year) {
    return (year % 4LL == 0LL) &&
           ((year % 100LL != 0LL) || (year % 400LL == 0LL));
}

static unsigned days_before_month(int64_t year, unsigned month) {
    static const uint16_t cumulative[12] = {
        0U,31U,59U,90U,120U,151U,181U,212U,243U,273U,304U,334U
    };
    if (month < 1U || month > 12U) return UINT_MAX;
    unsigned result = cumulative[month - 1U];
    if (month > 2U && leap_year(year)) ++result;
    return result;
}

bool nexus_unix_time_to_utc_tm(uint64_t unix_seconds, struct tm *output) {
    if (output == NULL) return false;
    const uint64_t days_u = unix_seconds / SECONDS_PER_DAY;
    if (days_u > (uint64_t)INT64_MAX - (uint64_t)UNIX_DAYS_OFFSET)
        return false;

    const uint64_t seconds_of_day = unix_seconds % SECONDS_PER_DAY;
    const int64_t days = (int64_t)days_u;
    const int64_t shifted = days + UNIX_DAYS_OFFSET;
    const int64_t era = shifted / 146097LL;
    const int64_t day_of_era = shifted - era * 146097LL;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460LL +
         day_of_era / 36524LL - day_of_era / 146096LL) / 365LL;
    int64_t year = year_of_era + era * 400LL;
    const int64_t day_of_year =
        day_of_era - (365LL * year_of_era + year_of_era / 4LL -
                      year_of_era / 100LL);
    const int64_t month_prime = (5LL * day_of_year + 2LL) / 153LL;
    const unsigned day =
        (unsigned)(day_of_year - (153LL * month_prime + 2LL) / 5LL + 1LL);
    const unsigned month = (unsigned)(month_prime +
                                      (month_prime < 10LL ? 3LL : -9LL));
    year += month <= 2U ? 1LL : 0LL;

    const int64_t tm_year = year - 1900LL;
    if (tm_year < INT_MIN || tm_year > INT_MAX || month < 1U ||
        month > 12U || day < 1U || day > 31U) return false;

    const unsigned year_day = days_before_month(year, month) + day - 1U;
    const unsigned weekday = (unsigned)((days + 4LL) % 7LL);
    *output = (struct tm){
        .tm_sec = (int)(seconds_of_day % 60ULL),
        .tm_min = (int)((seconds_of_day / 60ULL) % 60ULL),
        .tm_hour = (int)(seconds_of_day / 3600ULL),
        .tm_mday = (int)day,
        .tm_mon = (int)month - 1,
        .tm_year = (int)tm_year,
        .tm_wday = (int)weekday,
        .tm_yday = (int)year_day,
        .tm_isdst = 0
    };
    return true;
}

static mbedtls_time_t nexus_mbedtls_time(mbedtls_time_t *output) {
    uint64_t seconds = 0U;
    if (!rtc_get_unix_time(&seconds) ||
        seconds > (uint64_t)INT64_MAX) {
        if (output != NULL) *output = (mbedtls_time_t)-1;
        return (mbedtls_time_t)-1;
    }
    const mbedtls_time_t result = (mbedtls_time_t)seconds;
    if (output != NULL) *output = result;
    return result;
}

struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *seconds,
                                     struct tm *output) {
    if (seconds == NULL || output == NULL || *seconds < (mbedtls_time_t)0)
        return NULL;
    if (!nexus_unix_time_to_utc_tm((uint64_t)*seconds, output)) return NULL;
    return output;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    const uint64_t ticks = timer_ticks();
    if (ticks > (uint64_t)INT64_MAX / 10ULL) return INT64_MAX;
    return (mbedtls_ms_time_t)(ticks * 10ULL);
}

bool nexus_mbedtls_time_init(void) {
    uint64_t verified_epoch = 0U;
    if (!rtc_get_unix_time(&verified_epoch)) return false;
    if (verified_epoch > (uint64_t)INT64_MAX) return false;
    return mbedtls_platform_set_time(nexus_mbedtls_time) == 0;
}
