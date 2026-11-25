#include "ca_bundle.h"
#include <stdint.h>
extern const unsigned char nexus_builtin_ca_bundle_start[];
extern const unsigned char nexus_builtin_ca_bundle_end[];
const unsigned char *nexus_builtin_ca_bundle_get(size_t *length) {
    if (length == NULL) return NULL;
    const uintptr_t start = (uintptr_t)nexus_builtin_ca_bundle_start;
    const uintptr_t end = (uintptr_t)nexus_builtin_ca_bundle_end;
    if (end <= start) { *length = 0U; return NULL; }
    *length = (size_t)(end - start);
    return nexus_builtin_ca_bundle_start;
}
