#ifndef NEXUS_PACKAGE_H
#define NEXUS_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nexus/app.h"

#define NEXUS_PACKAGE_ABI_VERSION 1U
#define NEXUS_PACKAGE_MAX_PAYLOAD (4U * 1024U * 1024U)
#define NEXUS_PACKAGE_HEADER_MAX 512U
#define NEXUS_PACKAGE_ID_MAX 8U
#define NEXUS_PACKAGE_VERSION_MAX 11U
#define NEXUS_PACKAGE_KEY_ID_MAX 16U
#define NEXUS_PACKAGE_SIGNATURE_BYTES 64U
#define NEXUS_PACKAGE_DIGEST_BYTES 32U

/*
 * Ed25519 verifies the canonical manifest prefix ending at KEY=...\n.
 * A verifier must map key_id to a compiled/pinned public key and return false
 * for unknown keys. The kernel never accepts an absent verifier.
 */
typedef bool (*nexus_package_signature_verifier)(
    const char *key_id, const uint8_t *message, size_t message_length,
    const uint8_t signature[NEXUS_PACKAGE_SIGNATURE_BYTES], void *context);

struct nexus_package_manifest {
    char id[NEXUS_PACKAGE_ID_MAX + 1U];
    char version[NEXUS_PACKAGE_VERSION_MAX + 1U];
    char key_id[NEXUS_PACKAGE_KEY_ID_MAX + 1U];
    uint32_t abi_version;
    uint32_t payload_size;
    uint32_t permissions;
    size_t signed_message_size;
    size_t header_size;
    size_t payload_offset;
    uint8_t payload_sha256[NEXUS_PACKAGE_DIGEST_BYTES];
    uint8_t signature[NEXUS_PACKAGE_SIGNATURE_BYTES];
};

enum nexus_package_error {
    NEXUS_PACKAGE_ERROR_NONE = 0,
    NEXUS_PACKAGE_ERROR_ARGUMENT,
    NEXUS_PACKAGE_ERROR_VERIFY,
    NEXUS_PACKAGE_ERROR_EXECUTABLE,
    NEXUS_PACKAGE_ERROR_STORAGE,
    NEXUS_PACKAGE_ERROR_RECEIPT,
    NEXUS_PACKAGE_ERROR_VERSION_CONFLICT,
    NEXUS_PACKAGE_ERROR_WRITE_EXECUTABLE,
    NEXUS_PACKAGE_ERROR_COMMIT_RECEIPT
};

struct nexus_package_install_result {
    char id[NEXUS_PACKAGE_ID_MAX + 1U];
    char version[NEXUS_PACKAGE_VERSION_MAX + 1U];
    char executable_path[32];
    uint32_t permissions;
};

/* Parse a complete package. This does not authenticate it. */
bool nexus_package_parse(const void *package_bytes, size_t package_length,
                         struct nexus_package_manifest *manifest);
/* Verify the detached Ed25519 signature and the payload SHA-256 digest. */
bool nexus_package_verify(const void *package_bytes, size_t package_length,
                          nexus_package_signature_verifier verifier,
                          void *verifier_context,
                          struct nexus_package_manifest *manifest);
/*
 * Install one static ELF64 app into the writable NXFS Data volume. Signed
 * PERMISSIONS= metadata controls the optional syscalls granted at launch;
 * legacy packages receive no optional capabilities.
 * The signed .NPK receipt is committed only after the executable is present.
 */
bool nexus_package_install(const void *package_bytes, size_t package_length,
                           nexus_package_signature_verifier verifier,
                           void *verifier_context,
                           struct nexus_package_install_result *result);
bool nexus_package_is_installed(const char *id,
                                nexus_package_signature_verifier verifier,
                                void *verifier_context,
                                struct nexus_package_install_result *result);
bool nexus_package_launch(const char *id, nexus_package_signature_verifier verifier, void *verifier_context, uint64_t *exit_status);
enum nexus_package_error nexus_package_last_error(void);
bool nexus_package_uninstall(const char *id,
                             nexus_package_signature_verifier verifier,
                             void *verifier_context);

#endif
