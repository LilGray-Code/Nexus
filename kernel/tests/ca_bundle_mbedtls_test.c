#include "ca_bundle.h"
#include "tls_heap.h"
#include "timer.h"


#include "mbedtls/x509_crt.h"

#include <assert.h>
#include <stddef.h>

/* Time is irrelevant to PEM parsing, but the compiled Mbed TLS platform
 * adapter still references the kernel's monotonic tick source.
 */
uint64_t timer_ticks(void) { return 0U; }

int main(void) {
    size_t pem_length = 0U;
    const unsigned char *pem = nexus_builtin_ca_bundle_get(&pem_length);
    assert(pem != NULL && pem_length >= 100000U);
    assert(nexus_mbedtls_heap_init());
    const size_t initial_free = nexus_tls_heap_free_bytes();

    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    const int result = mbedtls_x509_crt_parse(&chain, pem, pem_length);
    assert(result == 0);

    size_t certificate_count = 0U;
    for (const mbedtls_x509_crt *certificate = &chain;
         certificate != NULL; certificate = certificate->next) {
        if (certificate->raw.len != 0U) ++certificate_count;
    }
    assert(certificate_count >= 80U);

    mbedtls_x509_crt_free(&chain);
    assert(nexus_tls_heap_free_bytes() == initial_free);
    return 0;
}
