#include "cert_store.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The ISO build embeds a checksum-verified Mozilla CA PEM bundle as a
 * read-only ELF binary object. The linker-provided range includes one
 * terminating NUL byte required by mbedtls_x509_crt_parse().
 */
extern const unsigned char _binary_build_nexus_ca_bundle_pem_start[];
extern const unsigned char _binary_build_nexus_ca_bundle_pem_end[];

bool nexus_tls_default_ca_bundle(const unsigned char **pem,
                                 size_t *pem_length) {
    if (pem == NULL || pem_length == NULL) return false;
    const uintptr_t begin =
        (uintptr_t)_binary_build_nexus_ca_bundle_pem_start;
    const uintptr_t end =
        (uintptr_t)_binary_build_nexus_ca_bundle_pem_end;
    if (end <= begin || end - begin < 2U) return false;
    const size_t length = (size_t)(end - begin);
    if (_binary_build_nexus_ca_bundle_pem_end[-1] != 0U ||
        _binary_build_nexus_ca_bundle_pem_start[0] == 0U) return false;
    *pem = _binary_build_nexus_ca_bundle_pem_start;
    *pem_length = length;
    return true;
}
