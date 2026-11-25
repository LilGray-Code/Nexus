#include "ca_bundle.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    size_t length = 0U;
    const unsigned char *bundle = nexus_builtin_ca_bundle_get(&length);
    assert(bundle != NULL);
    assert(length >= 100000U && length <= 524288U);
    assert(bundle[length - 1U] == 0U);
    assert(strstr((const char *)bundle, "-----BEGIN CERTIFICATE-----") != NULL);
    assert(strstr((const char *)bundle, "-----END CERTIFICATE-----") != NULL);
    return 0;
}
