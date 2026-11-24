#include "random.h"

#include "psa/crypto.h"
#include "psa/crypto_extra.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Mbed TLS 4.x calls this provider through the PSA Crypto external-RNG hook.
 * RDRAND is used only as a cryptographic-quality generator; failures are
 * returned to PSA and must abort key generation rather than falling back to
 * timestamps, counters, or an ordinary PRNG.
 */
psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length) {
    (void)context;
    if (output_length == NULL) return PSA_ERROR_INVALID_ARGUMENT;
    *output_length = 0U;

    if (output_size == 0U) return PSA_SUCCESS;
    if (output == NULL) return PSA_ERROR_INVALID_ARGUMENT;

    if (!nexus_rng_fill(output, output_size)) {
        /* nexus_rng_fill clears the entire output on every failure path. */
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    }

    *output_length = output_size;
    return PSA_SUCCESS;
}
