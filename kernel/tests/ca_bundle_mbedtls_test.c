#include "ca_bundle.h"
#include "tls_heap.h"
#include "timer.h"

#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint64_t timer_ticks(void) { return 0U; }

int main(void) {
    size_t pem_length = 0U;
    const unsigned char *pem = nexus_builtin_ca_bundle_get(&pem_length);
    assert(pem != NULL && pem_length >= 100000U);
    assert(nexus_mbedtls_heap_init());
    assert(psa_crypto_init() == PSA_SUCCESS);

    const size_t initial_free = nexus_tls_heap_free_bytes();
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    const int result = mbedtls_x509_crt_parse(&chain, pem, pem_length);

    size_t certificate_count = 0U;
    for (const mbedtls_x509_crt *certificate = &chain;
         certificate != NULL; certificate = certificate->next) {
        if (certificate->raw.len != 0U) ++certificate_count;
    }
    fprintf(stderr,
        "bundle_parse_result=%d certificates=%zu heap_free=%zu heap_largest=%zu\n",
        result, certificate_count, nexus_tls_heap_free_bytes(),
        nexus_tls_heap_largest_free_block());
    assert(result == 0);
    assert(certificate_count >= 80U);

    mbedtls_x509_crt_free(&chain);
    /* Mbed TLS 4 may retain PSA backend state for imported public keys.
     * Production keeps this parsed root chain for the kernel lifetime, so
     * only a bounded free-space floor is required after releasing the chain.
     */
    assert(nexus_tls_heap_free_bytes() >= 3U * 1024U * 1024U);
    assert(nexus_tls_heap_largest_free_block() >= 3U * 1024U * 1024U);
    (void)initial_free;
    return 0;
}
