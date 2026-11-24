#include "mbedtls_time.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

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
    assert(!nexus_unix_time_to_utc_tm(UINT64_MAX, &output));
    assert(!nexus_unix_time_to_utc_tm(0ULL, NULL));
    return 0;
}
