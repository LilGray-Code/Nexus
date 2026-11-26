# Nexus Application Ecosystem

**Branch:** \`nexus-application-ecosystem\`  
**Purpose:** Turn the existing shortcut catalog and single-package installer into a repository-aware, dependency-aware native application foundation.

## Delivered in this increment

### Signed repository metadata

\`kernel/src/app_repository.c\` and \`app_repository.h\` define a canonical, signed Nexus application repository format. Each entry contains:

- Stable package ID and semantic version.
- Application name, publisher, category and description.
- Package URL, declared size and SHA-256 digest.
- Optional HTTPS screenshot URL.
- Optional rating (tenths of a star) and rating count.
- Up to four dependencies, each with a minimum version.

The repository parser verifies the complete canonical byte sequence through the existing pinned-key verifier callback before publishing any entries. It rejects malformed fields, unsigned data, non-HTTPS package/screenshot URLs, unsorted/duplicate package IDs, unavailable dependencies, dependency version mismatches and dependency cycles. Ratings may be omitted using zero values.

The metadata API supports category/search filtering and stable ID lookup. The dependency planner builds an install/update order with dependencies before the requested application. Already-installed dependencies at or above the required version are not scheduled; older dependencies are upgraded from the repository catalogue.

This is a signed catalogue parser and planner, not a hosted public repository or a ratings service. The caller must supply a real Ed25519 verifier connected to pinned repository keys. Host tests use a deterministic test-only verifier; it is not suitable for production.

### Large streaming downloads

\`kernel/src/download_manager.c\` adds a single-transfer NXFS streaming manager. It:

- Writes 4 KiB chunks directly to the transactional NXFS writer.
- Supports downloads up to the mounted NXFS per-file limit (currently up to 256 MiB), subject to available disk space.
- Requires a declared byte count and expected SHA-256 digest.
- Reports byte counts, percentage, state and failure reason.
- Supports pause and resume while the source connection remains open.
- Supports retry from byte zero after a failed transport.
- Discards partial data on cancellation, transfer errors, size mismatch or digest failure.
- Publishes the final file only after the complete payload digest matches.

The manager deliberately permits one active download because the current NXFS transactional writer and TCP/TLS transport are single-session resources. The transport is injected through a small source interface. The transport implementation must use authenticated HTTPS, validate the certificate chain and return the exact response body; the test source is synthetic and does not provide network access.

### Existing native package lifecycle

Nexus already has a separate signed \`.NPK\` package lifecycle in \`package.c\`: signature/hash verification, static ELF validation, install, version replacement, launch and uninstall. It is not yet connected to the new repository planner and download manager. Its package parser still caps payloads at 4 MiB; the current process loader is more restrictive (1 MiB maximum image). The download manager can stage larger files but that does not make them installable or launchable as native apps.

## Explicit unsupported formats and runtimes

- **Flatpak:** Not supported yet. It needs OCI/OSTree content, sandboxing, portals, namespaces, cgroups/resource controls, mount integration and a compatible Linux userspace/runtime.
- **AppImage:** Not supported yet. It needs a compatible FUSE or extraction/runtime path and a broader Linux userspace ABI. Nexus cannot execute an AppImage as if it were a native Nexus ELF.
- **Windows applications:** Not supported yet. There is no Wine/Proton, Windows syscall/NT runtime, PE loader for general applications, Windows graphics translation or Direct3D runtime.
- **Linux packages:** The current \`.NPK\` format installs only Nexus static x86-64 ELF executables. It is not a Debian, RPM, Arch, APK or generic Linux package manager.
- **Dynamic/shared applications:** Not supported by the current application loader. General dynamic linking, shared libraries, runtime service dependencies and multi-process lifecycle are still missing.

Do not label any of these formats installable until their actual runtimes, security boundaries and lifecycle tests exist.

## Required next integration milestones

1. Add a production Ed25519 implementation and pinned key rotation policy; reject all unknown signing keys.
2. Implement an authenticated HTTPS source adapter over \`net_https\`, including bounded HTTP headers, Content-Length validation, redirect policy, and exact body framing. Do not weaken TLS verification.
3. Connect the signed repository parser, downloader and package verifier into a transaction: download to a staging file, verify repository digest, verify package signature and package digest, install dependencies first, commit the app receipt last, and roll back only packages newly installed by that transaction.
4. Add a durable installed-package database with dependency references, launch metadata, safe uninstall protection and recovery after interrupted updates.
5. Raise the native process image limit only after replacing the fixed 1 MiB loader buffer with a bounded, memory-accounted allocation model. Add streaming ELF validation and loader tests before increasing the limit.
6. Wire repository search, categories, screenshots, versions, ratings, installed/update states, download controls and error details into the desktop Store UI.
7. Add desktop launchers and shortcuts tied to installed package receipts; remove them atomically on uninstall.
8. Add AppImage, Flatpak and Windows compatibility only after their required execution and isolation runtimes are present.

## Security rules

- Catalog/package signatures and payload hashes are separate checks and both are required.
- HTTPS authenticates the transport; it does not replace package signatures.
- Downloads are staged transactionally and never exposed as completed files before the expected digest matches.
- A dependency failure must stop the plan. The transaction layer must not remove an application that was installed before the current transaction.
- Uninstall must verify ownership receipts before deleting files and must refuse removal while another installed package depends on the target.
