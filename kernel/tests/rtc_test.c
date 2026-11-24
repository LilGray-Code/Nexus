#include "rtc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static void expect_epoch(struct nexus_rtc_fields fields, uint64_t expected) {
    uint64_t actual = UINT64_MAX;
    assert(rtc_fields_to_unix(&fields, &actual));
    assert(actual == expected);
}

int main(void) {
    expect_epoch((struct nexus_rtc_fields){1970U,1U,1U,0U,0U,0U}, 0ULL);
    expect_epoch((struct nexus_rtc_fields){2000U,1U,1U,0U,0U,0U},
                 946684800ULL);
    expect_epoch((struct nexus_rtc_fields){2000U,2U,29U,0U,0U,0U},
                 951782400ULL);
    expect_epoch((struct nexus_rtc_fields){2000U,3U,1U,0U,0U,0U},
                 951868800ULL);
    expect_epoch((struct nexus_rtc_fields){2024U,2U,29U,23U,59U,59U},
                 1709251199ULL);

    uint64_t output = 123U;
    struct nexus_rtc_fields invalid = {2023U,2U,29U,0U,0U,0U};
    assert(!rtc_fields_to_unix(&invalid, &output));
    assert(output == 123U);
    invalid = (struct nexus_rtc_fields){2100U,2U,29U,0U,0U,0U};
    assert(!rtc_fields_to_unix(&invalid, &output));
    invalid = (struct nexus_rtc_fields){2026U,13U,1U,0U,0U,0U};
    assert(!rtc_fields_to_unix(&invalid, &output));
    invalid = (struct nexus_rtc_fields){2026U,1U,1U,24U,0U,0U};
    assert(!rtc_fields_to_unix(&invalid, &output));
    assert(!rtc_fields_to_unix(NULL, &output));
    assert(!rtc_fields_to_unix(&invalid, NULL));
    return 0;
}
