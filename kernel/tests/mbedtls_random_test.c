#include "random.h"

#include "psa/crypto.h"
#include "psa/crypto_extra.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    mbedtls_psa_external_random_context_t context = {0};
    uint8_t output[48];
    for (size_t i = 0U; i < sizeof(output); ++i) output[i] = 0xA5U;

    size_t output_length = 99U;
    assert(mbedtls_psa_external_get_random(
        &context, output, 0U, &output_length) == PSA_SUCCESS);
    assert(output_length == 0U);

    output_length = 99U;
    assert(mbedtls_psa_external_get_random(
        &context, NULL, 1U, &output_length) == PSA_ERROR_INVALID_ARGUMENT);
    assert(output_length == 0U);

    const psa_status_t status = mbedtls_psa_external_get_random(
        &context, output, sizeof(output), &output_length);
    if (status == PSA_SUCCESS) {
        assert(output_length == sizeof(output));
        bool nonzero = false;
        for (size_t i = 0U; i < sizeof(output); ++i)
            nonzero = nonzero || output[i] != 0U;
        assert(nonzero);
    } else {
        assert(status == PSA_ERROR_INSUFFICIENT_ENTROPY);
        assert(output_length == 0U);
        for (size_t i = 0U; i < sizeof(output); ++i)
            assert(output[i] == 0U);
    }

    assert(mbedtls_psa_external_get_random(
        &context, output, sizeof(output), NULL) == PSA_ERROR_INVALID_ARGUMENT);
    return 0;
}
