# Nexus Application Ecosystem

**Branch:** `nexus-application-ecosystem`  
**Purpose:** Turn the existing shortcut catalog and single-package installer into a repository-aware, dependency-aware native application foundation.

## Delivered in this increment

### Signed repository metadata

`kernel/src/app_repository.c` and `app_repository.h` define a canonical, signed Nexus application repository format. Each entry contains:

- Stable package ID and semantic version.
- Application name, publisher, category and description.
- Package URL, declared size and SHA-256 digest.
- Optional HTTPS screenshot URL.
- Optional rating (tenths of a star) and rating count.
- Up to four dependencies, each with a minimum version.

The repository parser verifies the complete canonical byte sequence through the existing pinned-key verifier callback before publishing any entries. It rejects malformed fields, unsigned data, non-HTTPS package/screenshot URLs, unsorted/duplicate package IDs, unavailable dependencies, dependency version mismatches and dependency cycles. Ratings may be omitted using zero values.

The metadata API supports category/search filtering and stable ID lookup. The dependency planner builds an install/update order with dependencies before the requested application. Already-installed dependencies at or above the required version are not scheduled; older dependencies are upgraded from the repository catalogue.

This is a signed catalogue parser and planner, not a hosted public repository or a ratings service. The caller must supply a real Ed25519 verifier connected to pinned repository keys. Host tests use a deterministic test-only verifier; it is not suitable for production.

### Authenticated HTTPS package source

`kernel/src/download_https.c` adapts the download manager to Nexus's verified HTTPS/TLS 1.3 client and built-in Mozilla CA roots. It accepts only HTTPS host/path URLs on port 443, rejects redirects and chunked/ambiguous responses, requires HTTP 200 with an exact Content-Length matching the signed repository size, and streams body bytes without holding the complete object in memory. It preserves pause/resume on the existing connection; retry creates a fresh TLS connection and starts from byte zero. URL parsing, partial header reads, request construction, TLS-verification rejection and HTTP framing are covered by host tests with a mock TLS transport.

The adapter depends on the TLS-enabled kernel build and usable network/CA initialization at runtime. In a TLS-disabled build, the HTTPS stubs fail closed. It does not implement redirects, range requests, concurrent downloads or chunked package responses.

### Large streaming downloads

`kernel/src/download_manager.c` adds a single-transfer NXFS streaming manager. It:

- Writes 4 KiB chunks directly to the transactional NXFS writer.
- Supports downloads up to the mounted NXFS per-file limit (currently up to 256 MiB), subject to available disk space.
- Requires a declared byte count and expected SHA-256 digest.
- Reports byte counts, percentage, state and failure reason.
- Supports pause and resume while the source connection remains open.
- Supports retry from byte zero after a failed transport.
- Discards partial data on cancellation, transfer errors, size mismatch or digest failure.
- Publishes the final file only after the complete payload digest matches.

The manager deliberately permits one active download because the current NXFS transactional writer and TCP/TLS transport are single-session resources. The HTTPS adapter uses the built-in CA roots and verified TLS 1.3 transport. CI uses mock TLS and synthetic payloads; no real public repository service or production package-signing key has been provisioned.

### Native install, update, launch and removal orchestration

`kernel/src/app_manager.c` connects the authenticated repository, the real HTTPS source and the existing signed `.NPK` installer. Its caller-owned manager:
- Searches the authenticated repository and resolves the root app plus missing or outdated dependencies in dependency-first order.
- Downloads each package through verified HTTPS and NXFS streaming, checks the complete package SHA-256 against the signed repository record, then verifies the package signature, ID and version before installation.
- Stages the package outside the kernel stack in bounded contiguous physical pages, reads it in 4 KiB VFS chunks, scrubs the staging buffer and releases the pages after installation.
- Supports pause/resume on a live connection, retry from byte zero after a network failure, cancellation, app launch, and uninstall refusal when a currently catalogued installed app depends on the target.
- Rolls back newly installed dependencies if a later install step fails; it does not remove software that was installed before this operation.

**Important limits:** this manager only installs packages the current native installer can install: a maximum 4 MiB payload plus the header, and the ELF payload must fit the current 1 MiB process-image limit. Large downloads can be staged by the general downloader, but this manager intentionally refuses to treat large blobs as runnable native apps. Package updates are individually atomic; a multi-app dependency transaction does not yet retain prior-version backups, so an earlier dependency update can remain applied if a later package fails. Removal dependency checks use the current signed repository catalogue; there is not yet a durable reverse-dependency database covering packages removed from that catalogue.

The app-manager host tests use mock package/storage/memory/download services and a deterministic test-only catalog/package verifier. They validate dependency installation order, existing app updates, launch/remove routing, dependency-protected removal, pause/resume, retry and rollback of newly installed dependencies. These tests verify orchestration contracts, not execution of third-party binaries on a booted Nexus system.

### Existing native package lifecycle

Nexus's signed `.NPK` lifecycle remains the low-level install engine: it validates the signature and payload digest, checks static ELF structure, commits the executable before the signed receipt, supports version replacement, and validates the installed payload before launch/removal. The app manager now calls those APIs after repository and payload checks. The signature-verifier callback must still be backed by a real production Ed25519 implementation and pinned key policy before any public repository can be trusted.

## Explicit unsupported formats and runtimes

- **Flatpak:** Not supported yet. It needs OCI/OSTree content, sandboxing, portals, namespaces, cgroups/resource controls, mount integration and a compatible Linux userspace/runtime.
- **AppImage:** Not supported yet. It needs a compatible FUSE or extraction/runtime path and a broader Linux userspace ABI. Nexus cannot execute an AppImage as if it were a native Nexus ELF.
- **Windows applications:** Not supported yet. There is no Wine/Proton, Windows syscall/NT runtime, PE loader for general applications, Windows graphics translation or Direct3D runtime.
- **Linux packages:** The current `.NPK` format installs only Nexus static x86-64 ELF executables. It is not a Debian, RPM, Arch, APK or generic Linux package manager.
- **Dynamic/shared applications:** Not supported by the current application loader. General dynamic linking, shared libraries, runtime service dependencies and multi-process lifecycle are still missing.

Do not label any of these formats installable until their actual runtimes, security boundaries and lifecycle tests exist.

## Required next integration milestones

1. Add a production Ed25519 verifier/keyring, pinned repository keys and a safe key-rotation policy. Until then the manager fails closed unless the caller supplies a trusted verifier.
2. Publish and operate a signed repository endpoint with metadata, package signing, screenshot assets and a release/rollback process.
3. Add a durable installed-app/dependency database, reverse-dependency records independent of the current catalogue, and power-loss recovery for multi-package operations.
4. Wire the repository metadata, installed/update state, download progress, pause/resume/retry, errors, screenshots and ratings into the graphical desktop Store.
5. Generate and remove real desktop shortcuts/launcher entries from verified package receipts.
6. Expand the static ELF loader beyond its current 1 MiB image bound only after implementing memory-accounted process images, robust reclamation and larger-file validation.
7. Add AppImage, Flatpak and Windows compatibility only after their actual execution and isolation runtimes exist.

## Security rules

- Catalog/package signatures and payload hashes are separate checks and both are required.
- HTTPS authenticates the transport; it does not replace package signatures.
- Downloads are staged transactionally and never exposed as completed files before the expected digest matches.
- A dependency failure must stop the plan. The transaction layer must not remove an application that was installed before the current transaction.
- Uninstall must verify ownership receipts before deleting files and must refuse removal while another installed package depends on the target.
