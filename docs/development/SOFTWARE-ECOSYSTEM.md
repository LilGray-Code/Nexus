# Nexus Native Software Ecosystem — Package and Catalog Preview

## Native package (`.NUP`)

The first native package format is intentionally one static x86-64 ELF64 app per package. The loader accepts ET_EXEC and a restricted ET_DYN static-PIE subset with only symbol-free `R_X86_64_RELATIVE` relocations; PIE images with an interpreter, TLS, dependencies, symbol relocations, or unsupported dynamic tags are rejected. Version 1 is canonical UTF-8/ASCII text followed by the exact payload bytes:

```text
NEXUS-PACKAGE/1
ID=DEMO
VERSION=1.2.3
ARCH=x86_64
ABI=1
ENTRY=APP.ELF
SIZE=1234
SHA256=<64 lowercase hexadecimal characters>
KEY=release-key-1
SIGNATURE=<128 lowercase hexadecimal characters>

<exactly SIZE payload bytes>
```

The Ed25519 signature authenticates the exact header bytes from the beginning of `NEXUS-PACKAGE/1` through the newline after `KEY=...`. `SIGNATURE=` and the payload are not part of that signed message; the SHA-256 payload digest is in the signed header. The verifier must reject an unknown key ID, an invalid signature, or any payload whose SHA-256 differs. The package parser rejects non-canonical field order, CRLF, extra fields, unsupported architecture/ABI/entry names, malformed semantic versions, bad sizes, and trailing bytes.

`kernel/src/package.c` verifies this structure, then requires the payload to pass the Nexus ELF64 loader's validation (static ET_EXEC or the constrained static-PIE subset). It installs the executable under `APPS/<ID>-<VERSION>.ELF` and writes `APPS/<ID>.NPK` last. The receipt is the installed-state marker. Updating an app stages a separate versioned executable and only switches the receipt after the new payload is present. The previous version is removed after the receipt commit. A failed receipt commit removes a newly staged executable and leaves the previous receipt intact.

Installation requires a writable NexusFS Data volume. It never writes to FAT32 or the volatile RAM overlay. NXFS currently provides at most 64 entries and 1 MiB per file, so the first format supports a single executable payload up to 1 MiB; it does not package assets, shared libraries, or runtime dependencies yet.

## Signed software catalog

The catalog format (`NEXUS-CATALOG/1`) is a separate canonical signed document:

```text
NEXUS-CATALOG/1
GENERATION=7
COUNT=2
KEY=release-key-1
APP=DEMO|1.2.3|https://packages.example/DEMO-1.2.3.NUP|<64-hex package SHA-256>
APP=PAINT|2.0.0|https://packages.example/PAINT-2.0.0.NUP|<64-hex package SHA-256>
SIGNATURE=<128 lowercase hexadecimal characters>
```

The Ed25519 signature covers every byte before the `SIGNATURE=` line, including the generation, count, key ID, and all app entries. Entries must be strictly sorted by app ID, IDs must be unique, package URLs must use HTTPS, and catalog count/size are bounded. The catalog package hash is an additional download-integrity check; the package's own signature is still required.

`kernel/src/catalog.c` parses and verifies this catalog using the same injected signature-verifier contract as native packages. It does not currently fetch the catalog or packages over the network; it is the trust/format layer for that later service.

## Critical trust-provider boundary

`nexus_package_install` and `nexus_catalog_parse_verify` require a non-null Ed25519 verifier callback. The callback must map the supplied key ID to an immutable, pinned public key and verify Ed25519 over the exact message bytes. Passing no verifier fails closed. The repository does **not yet ship a production release public key or a kernel Ed25519 provider**, and the host tests use a deliberately fake deterministic verifier only to test parser/transaction behavior. That test verifier is not cryptographic and must never be used to install software.

The Store is not yet connected to this package/catalog API. The old Steam/Discord/Spotify entries remain HTTPS website shortcuts. Before enabling Store installation, Nexus needs a release-key provisioning/revocation process, a real Ed25519 implementation and known-answer tests, a signed release catalog, and an HTTPS download/stream-to-staging service that checks the catalog hash before package signature verification.

## Next compatibility layers

A constrained static-PIE path and initial gamepad/AC'97 service foundations now exist. The next layers are a complete shared-library dynamic linker and runtime ABI, multiple protected user processes, a native window API, full audio services, and a 3D graphics driver/API. The current user app is a foreground synchronous static ELF with a 2D framebuffer syscall. This package format does not make Windows PE/Win32 applications, Steam, Discord, Spotify, or commercial PC games runnable.
