#include "catalog.h"
#include "sha256.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void to_hex(const uint8_t *bytes, size_t length, char *output) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < length; ++i) {
        output[i * 2U] = digits[bytes[i] >> 4];
        output[i * 2U + 1U] = digits[bytes[i] & 15U];
    }
    output[length * 2U] = '\0';
}

/* Test-only deterministic verifier, not an Ed25519 implementation. */
static bool test_verify(const char *key, const uint8_t *message,
                        size_t length, const uint8_t signature[64],
                        void *context) {
    (void)context;
    if (key == NULL || strcmp(key, "catalog-test") != 0 ||
        message == NULL || length == 0U || signature == NULL) return false;
    uint8_t digest[32];
    nexus_sha256(message, length, digest);
    return memcmp(signature, digest, 32U) == 0 &&
           memcmp(signature + 32U, digest, 32U) == 0;
}

static size_t build_catalog(char *output, size_t capacity,
                            const char *app_lines, uint32_t count) {
    char prefix[3072];
    const int prefix_length = snprintf(prefix, sizeof(prefix),
        "NEXUS-CATALOG/1\nGENERATION=7\nCOUNT=%u\nKEY=catalog-test\n%s",
        count, app_lines);
    assert(prefix_length > 0 && (size_t)prefix_length < sizeof(prefix));
    uint8_t signature[64];
    nexus_sha256(prefix, (size_t)prefix_length, signature);
    memcpy(signature + 32U, signature, 32U);
    char signature_hex[129];
    to_hex(signature, sizeof(signature), signature_hex);
    const int total = snprintf(output, capacity, "%sSIGNATURE=%s\n",
                               prefix, signature_hex);
    assert(total > prefix_length && (size_t)total < capacity);
    return (size_t)total;
}

int main(void) {
    static const char entries[] =
        "APP=DEMO|1.2.3|https://packages.nexus.example/apps/demo.nup|"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        "APP=PAINT|2.0.0|https://packages.nexus.example/apps/paint.nup|"
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\n";
    char catalog_bytes[4096];
    size_t length = build_catalog(catalog_bytes, sizeof(catalog_bytes),
                                  entries, 2U);
    struct nexus_catalog catalog;
    assert(nexus_catalog_parse_verify(catalog_bytes, length, test_verify,
                                      NULL, &catalog));
    assert(catalog.generation == 7U && catalog.entry_count == 2U);
    assert(strcmp(catalog.entries[0].id, "DEMO") == 0);
    assert(strcmp(catalog.entries[1].id, "PAINT") == 0);
    assert(strcmp(catalog.entries[0].version, "1.2.3") == 0);
    assert(strcmp(catalog.entries[0].url,
                  "https://packages.nexus.example/apps/demo.nup") == 0);

    char invalid[4096];
    memcpy(invalid, catalog_bytes, length);
    char *signature = strstr(invalid, "SIGNATURE=");
    assert(signature != NULL);
    signature[10] = signature[10] == '0' ? '1' : '0';
    assert(!nexus_catalog_parse_verify(invalid, length, test_verify,
                                      NULL, &catalog));

    static const char http_entries[] =
        "APP=DEMO|1.2.3|http://packages.nexus.example/apps/demo.nup|"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    length = build_catalog(catalog_bytes, sizeof(catalog_bytes),
                           http_entries, 1U);
    assert(!nexus_catalog_parse_verify(catalog_bytes, length, test_verify,
                                       NULL, &catalog));

    static const char unsorted_entries[] =
        "APP=PAINT|2.0.0|https://packages.nexus.example/apps/paint.nup|"
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\n"
        "APP=DEMO|1.2.3|https://packages.nexus.example/apps/demo.nup|"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    length = build_catalog(catalog_bytes, sizeof(catalog_bytes),
                           unsorted_entries, 2U);
    assert(!nexus_catalog_parse_verify(catalog_bytes, length, test_verify,
                                       NULL, &catalog));
    assert(!nexus_catalog_parse_verify(catalog_bytes, length, NULL,
                                       NULL, &catalog));

    puts("NEXUS SIGNED CATALOG TESTS PASSED");
    return 0;
}
