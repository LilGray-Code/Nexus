#include "mbedtls_time.h"

#include "rtc.h"
#include "timer.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static mbedtls_time_t (*registered_time)(mbedtls_time_t *);
static uint64_t fake_epoch = 1709251199ULL;
static uint64_t fake_ticks;

bool rtc_get_unix_time(uint64_t *seconds) {
    if (seconds == NULL || fake_epoch == 0U) return false;
    *seconds = fake_epoch;
    return true;
}

uint64_t timer_ticks(void) {
    return ++fake_ticks;
}

int mbedtls_platform_set_time(mbedtls_time_t (*callback)(mbedtls_time_t *)) {
    registered_time = callback;
    return 0;
}

static void expect_tm(uint64_t epoch, int year, int month, int day,
                      int hour, int minute, int second,
                      int weekday, int yearday) {
    struct tm actual;
    memset(&actual, 0xA5, sizeof(actual));
    assert(nexus_unix_time_to_utc_tm(epoch, &actual));
    assert(actual.tm_year == year - 1900);
    assert(actual.tm_mon == month - 1);
    assert(actual.tm_mday == day);
    assert(actual.tm_hour == hour);
    assert(actual.tm_min == minute);
    assert(actual.tm_sec == second);
    assert(actual.tm_wday == weekday);
    assert(actual.tm_yday == yearday);
    assert(actual.tm_isdst == 0);
}

int main(void) {
    expect_tm(0ULL, 1970,1,1,0,0,0,4,0);
    expect_tm(946684800ULL, 2000,1,1,0,0,0,6,0);
    expect_tm(951782400ULL, 2000,2,29,0,0,0,2,59);
    expect_tm(1709251199ULL, 2024,2,29,23,59,59,4,59);
    expect_tm(2147483647ULL, 2038,1,19,3,14,7,2,18);

    struct tm output;
    mbedtls_time_t time_value = (mbedtls_time_t)1709251199ULL;
    assert(mbedtls_platform_gmtime_r(&time_value, &output) == &output);
    assert(output.tm_year == 124 && output.tm_mon == 1 &&
           output.tm_mday == 29 && output.tm_hour == 23);

    assert(nexus_mbedtls_time_init());
    assert(registered_time != NULL);
    mbedtls_time_t now = 0;
    assert(registered_time(&now) == (mbedtls_time_t)fake_epoch);
    assert(now == (mbedtls_time_t)fake_epoch);

    fake_epoch = 0U;
    assert(!nexus_mbedtls_time_init());
    assert(registered_time != NULL); /* The previous callback remains intact. */
    fake_ticks = 0U;
    assert(mbedtls_ms_time() == 10);
    assert(mbedtls_ms_time() == 20);

    assert(!nexus_unix_time_to_utc_tm(UINT64_MAX, &output));
    assert(!nexus_unix_time_to_utc_tm(0ULL, NULL));
    time_value = (mbedtls_time_t)-1;
    assert(mbedtls_platform_gmtime_r(&time_value, &output) == NULL);
    return 0;
}
