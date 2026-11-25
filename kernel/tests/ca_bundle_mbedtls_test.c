#include "ca_bundle.h"
#include "tls_heap.h"
#include "timer.h"

#include "mbedtls/x509_crt.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const unsigned char begin_marker[] = "-----BEGIN CERTIFICATE-----";
static const unsigned char end_marker[] = "-----END CERTIFICATE-----";

static const unsigned char *find_marker(const unsigned char *data,
                                        size_t length, size_t start,
                                        const unsigned char *marker,
                                        size_t marker_length) {
    if (start > length || marker_length > length - start) return NULL;
    for (size_t i = start; i <= length - marker_length; ++i) {
        if (memcmp(data + i, marker, marker_length) == 0)
            return data + i;
    }
    return NULL;
}

/* The kernel's monotonic clock adapter is linked but PEM parsing does not
 * consume time in this host-only test.
 */
uint64_t timer_ticks(void) { return 0U; }

int main(void) {
    size_t pem_length = 0U;
    const unsigned char *pem = nexus_builtin_ca_bundle_get(&pem_length);
    assert(pem != NULL && pem_length >= 100000U);
    assert(nexus_mbedtls_heap_init());

    const size_t begin_length = sizeof(begin_marker) - 1U;
    const size_t end_length = sizeof(end_marker) - 1U;
    size_t cursor = 0U;
    size_t certificate_count = 0U;
    size_t unsupported_count = 0U;
    size_t certificate_index = 0U;
    unsigned char certificate_pem[16384];

    while (cursor < pem_length) {
        const unsigned char *begin = find_marker(
            pem, pem_length, cursor, begin_marker, begin_length);
        if (begin == NULL) break;
        const size_t begin_offset = (size_t)(begin - pem);
        const unsigned char *end = find_marker(
            pem, pem_length, begin_offset + begin_length,
            end_marker, end_length);
        assert(end != NULL);

        const size_t block_length =
            (size_t)(end - begin) + end_length;
        assert(block_length + 2U < sizeof(certificate_pem));
        memcpy(certificate_pem, begin, block_length);
        certificate_pem[block_length] = '\n';
        certificate_pem[block_length + 1U] = '\0';

        const size_t heap_before = nexus_tls_heap_free_bytes();
        mbedtls_x509_crt certificate;
        mbedtls_x509_crt_init(&certificate);
        const int result = mbedtls_x509_crt_parse(
            &certificate, certificate_pem, block_length + 2U);
        if (result == 0 && certificate.raw.len != 0U) {
            ++certificate_count;
        } else {
            ++unsupported_count;
            fprintf(stderr, "unsupported_root_index=%zu result=%d\\n",
                    certificate_index, result);
        }
        mbedtls_x509_crt_free(&certificate);
        assert(nexus_tls_heap_free_bytes() == heap_before);

        ++certificate_index;
        cursor = (size_t)(end - pem) + end_length;
        while (cursor < pem_length &&
               (pem[cursor] == '\r' || pem[cursor] == '\n' ||
                pem[cursor] == ' ' || pem[cursor] == '\t')) ++cursor;
    }

    fprintf(stderr, "supported_roots=%zu unsupported_roots=%zu\\n",
            certificate_count, unsupported_count);
    assert(certificate_count >= 80U);
    return 0;
}
