#ifndef NEXUS_CERT_STORE_H
#define NEXUS_CERT_STORE_H

#include <stdbool.h>
#include <stddef.h>

bool nexus_tls_default_ca_bundle(const unsigned char **pem,
                                 size_t *pem_length);

#endif
