#include "../src/libc.c"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_memory_and_strings(void) {
    char source[] = "Nexus-runtime";
    char destination[32] = {0};
    assert(strlen(source) == 13U);
    assert(strcmp(source, "Nexus-runtime") == 0);
    assert(strcmp(source, "Nexus") > 0);
    assert(strchr(source, '-') == source + 5);
    assert(strstr(source, "runtime") == source + 6);
    memcpy(destination, source, sizeof(source));
    assert(memcmp(destination, source, sizeof(source)) == 0);
    memmove(destination + 2, destination, 6U);
    assert(destination[2] == 'N' && destination[7] == '-');
    memset(destination, 'x', 3U);
    assert(destination[0] == 'x' && destination[2] == 'x');
    explicit_bzero(destination, sizeof(destination));
    for (size_t i = 0U; i < sizeof(destination); ++i)
        assert(destination[i] == 0U);
}

static void test_bounded_formatting(void) {
    char output[64];
    const int length = snprintf(output, sizeof(output),
                                "%s:%04x:%zu:%lld", "tls", 0x2AU,
                                (size_t)17U, (long long)-9000000001LL);
    assert(length > 0 && (size_t)length == strlen(output));
    assert(strcmp(output, "tls:002a:17:-9000000001") == 0);

    char short_output[5];
    assert(snprintf(short_output, sizeof(short_output), "abcdef") == 6);
    assert(strcmp(short_output, "abcd") == 0);
    assert(snprintf(NULL, 0U, "%u/%s", 42U, "Nexus") == 8);
}

static void test_inet_pton(void) {
    uint8_t ipv4[4] = {0};
    assert(inet_pton(2, "192.0.2.20", ipv4) == 1);
    assert(ipv4[0] == 192U && ipv4[1] == 0U &&
           ipv4[2] == 2U && ipv4[3] == 20U);
    assert(inet_pton(2, "192.0.2.999", ipv4) == 0);
    assert(inet_pton(2, "192.0.2", ipv4) == 0);

    uint8_t ipv6[16] = {0};
    static const uint8_t expected_loopback[16] = {
        0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,0U,1U
    };
    assert(inet_pton(10, "::1", ipv6) == 1);
    assert(memcmp(ipv6, expected_loopback, sizeof(ipv6)) == 0);
    assert(inet_pton(10, "2001:db8::1", ipv6) == 1);
    assert(ipv6[0] == 0x20U && ipv6[1] == 0x01U &&
           ipv6[2] == 0x0DU && ipv6[3] == 0xB8U && ipv6[15] == 1U);
    assert(inet_pton(10, "::ffff:192.0.2.1", ipv6) == 1);
    assert(ipv6[10] == 0xFFU && ipv6[11] == 0xFFU &&
           ipv6[12] == 192U && ipv6[15] == 1U);
    assert(inet_pton(10, "1::2::3", ipv6) == 0);
}

int main(void) {
    test_memory_and_strings();
    test_bounded_formatting();
    test_inet_pton();
    return 0;
}
