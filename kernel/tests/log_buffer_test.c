#include "log_buffer.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    char output[NEXUS_LOG_BUFFER_CAPACITY + 1U];
    nexus_log_buffer_init();
    assert(nexus_log_buffer_read_recent(output, sizeof(output)) == 0U);

    nexus_log_buffer_append("boot: ", 6U);
    nexus_log_buffer_append("ready\r\n", 7U);
    assert(nexus_log_buffer_read_recent(output, sizeof(output)) == 13U);
    assert(strcmp(output, "boot: ready\r\n") == 0);

    static char oversized[NEXUS_LOG_BUFFER_CAPACITY + 73U];
    for (size_t i = 0U; i < sizeof(oversized); ++i)
        oversized[i] = (char)('A' + (i % 26U));
    nexus_log_buffer_append(oversized, sizeof(oversized));

    const size_t count =
        nexus_log_buffer_read_recent(output, sizeof(output));
    assert(count == NEXUS_LOG_BUFFER_CAPACITY);
    assert(output[count] == '\0');
    for (size_t i = 0U; i < count; ++i) {
        const size_t source = sizeof(oversized) -
                              NEXUS_LOG_BUFFER_CAPACITY + i;
        assert(output[i] == oversized[source]);
    }

    char small[5];
    assert(nexus_log_buffer_read_recent(small, sizeof(small)) == 4U);
    assert(small[4] == '\0');
    assert(nexus_log_buffer_dropped() == 0U);
    return 0;
}
