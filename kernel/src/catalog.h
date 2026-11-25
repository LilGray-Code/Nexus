#ifndef NEXUS_CATALOG_H
#define NEXUS_CATALOG_H

#include "package.h"

#define NEXUS_CATALOG_MAX_ENTRIES 16U
#define NEXUS_CATALOG_URL_MAX 159U
#define NEXUS_CATALOG_BYTES_MAX 4096U

struct nexus_catalog_entry {
    char id[NEXUS_PACKAGE_ID_MAX + 1U];
    char version[NEXUS_PACKAGE_VERSION_MAX + 1U];
    char url[NEXUS_CATALOG_URL_MAX + 1U];
    uint8_t package_sha256[NEXUS_PACKAGE_DIGEST_BYTES];
};

struct nexus_catalog {
    uint32_t generation;
    uint32_t entry_count;
    char key_id[NEXUS_PACKAGE_KEY_ID_MAX + 1U];
    struct nexus_catalog_entry entries[NEXUS_CATALOG_MAX_ENTRIES];
};

/* Verify a canonical HTTPS-only catalog with the supplied pinned-key verifier. */
bool nexus_catalog_parse_verify(const void *catalog_bytes,
                                size_t catalog_length,
                                nexus_package_signature_verifier verifier,
                                void *verifier_context,
                                struct nexus_catalog *catalog);

#endif
