# Nexus system update architecture

**Status: architecture and on-disk contract added; runtime updater is not yet enabled.** This is the implementation contract for the original-kernel Nexus branch. It does not claim package installation, signature verification, or rollback are already available in the booted OS.

## 1. Safety boundary

The current disk layout separates the EFI System Partition, Nexus System (NXFS), Nexus Recovery, and Nexus Data (NXFS). Updates MUST leave Data untouched. The updater must never format, replace, recursively clean, or use Data as scratch space. User files and settings remain on Data; system state and package metadata belong on System/Recovery.

The current system is an engineering prototype. The block layer and NXFS must first pass durable-write, flush, remount, corruption, and power-loss tests. Existing installer transaction-recovery tests do not prove general package updates are safe. Until the gates in §12 pass, the release updater must report installation unavailable and must not expose an install action.

## 2. Release and package model

A signed repository snapshot contains:
- repository ID and monotonically increasing snapshot sequence;
- OS release channel (stable, beta, development) and target (x86_64-bios, x86_64-uefi, or x86_64-dual);
- minimum updater version and supported system ABI range;
- package records with package ID, version, architecture, component (kernel, driver, system, application), dependencies, conflicts, provides, replaces, file inventory, compressed and unpacked sizes, archive digest, and signing-key ID;
- expiry time and snapshot digest.

Package versions use epoch:major.minor.patch-release; comparison is numeric by epoch/major/minor/patch, then prerelease ordering. Package identity is (repository, package ID, architecture). Dependencies are version predicates over package IDs or virtual capabilities. Conflicts are explicit. The resolver must produce one deterministic, complete transaction or fail without changing the installed system.

Kernel and boot-critical drivers form one compatibility group. A snapshot declares kernel ABI and driver ABI. A transaction changing either must select a mutually compatible set. Applications may update independently only when their declared system ABI constraints remain satisfied.

## 3. Trust and transport

HTTPS protects transport; it does not establish package authenticity. Every repository snapshot and package is authenticated using detached Ed25519 signatures rooted in public keys compiled into a reviewed updater trust store. Repository metadata uses RFC 8785 JSON Canonicalization Scheme (JCS). The snapshot signature is Ed25519 over the canonical UTF-8 bytes of the snapshot with signing.signature omitted; signing.algorithm and signing.key_id remain covered. Each package signature is over a domain-separated canonical record containing repository ID, snapshot sequence, package ID, version, architecture, archive size, and archive SHA-256. The archive digest covers the exact downloaded archive bytes. Key IDs, validity windows, revocation sequence, and key-rotation metadata are part of the signed snapshot. Implementations must reject duplicate JSON keys and non-canonical encodings before verification.

Verification order:
1. Validate repository snapshot signature and expiry using the trusted root.
2. Reject a lower snapshot sequence than the highest committed sequence (anti-rollback).
3. Validate target architecture, channel, OS ABI, and minimum updater version.
4. Resolve dependencies and conflicts completely.
5. Download into bounded staging; enforce declared size limits while streaming.
6. Hash the exact downloaded bytes and compare with the signed digest.
7. Verify package signature and archive inventory before extraction.
8. Reject absolute paths, parent traversal, duplicate normalized paths, unsupported special files, escaping symlinks, hard links, device nodes, and file-count/size-limit violations.
9. Extract only into the inactive system slot or transaction staging directory. Never extract directly over the active system.

Fail closed if secure randomness, signature verification, a trusted key, or durable storage flush is unavailable. No unsigned, HTTP, checksum-only, or untrusted-mirror fallback is permitted.

## 4. Atomic system layout

Preferred deployment is two system generations (A/B) within Nexus System, with the bootloader and Recovery partition outside either generation:

- slots/A/ — one complete bootable system generation;
- slots/B/ — the other bootable generation;
- state/installed-manifest — committed package and OS version inventory;
- state/boot-selection — checksummed active, pending, and fallback slot metadata;
- staging/<transaction-id>/ — incomplete downloads and extraction, never bootable;
- Recovery/updates/ — redundant transaction journal records on the separate Recovery partition.

Only the inactive slot may be modified. The active slot remains byte-for-byte unchanged until the new slot boots and passes health checks. If NXFS cannot guarantee the required durability and atomic metadata replacement, do not emulate this with ordinary writes: updates stay disabled until NXFS snapshots or an equivalent tested copy-on-write/atomic-rename primitive exists.

The current boot configuration expects a single installed kernel path. A/B therefore requires a reviewed Limine configuration change that selects the chosen slot and a boot-selection record updated atomically. Preserve a known-good recovery boot entry. Never overwrite the only bootable kernel or bootloader before the new path is verified.

## 5. Transaction state machine

Every transaction has a cryptographically random unique ID and monotonically increasing journal sequence. Allowed states:

DISCOVERED -> RESOLVED -> DOWNLOADING -> VERIFIED -> STAGING -> STAGED -> PENDING_REBOOT -> TRIAL_BOOT -> COMMITTED

Failure before PENDING_REBOOT transitions to ABORTED; a failed trial transitions to ROLLBACK_REQUIRED -> ROLLED_BACK. A transaction may not skip states. Each transition is journaled and flushed before the next destructive step.

The journal record includes format version, transaction ID, sequence, previous and next state, old/new slot, old/new OS versions, old/new manifest digests, repository snapshot sequence, package digest list, progress counters, boot-attempt count, and record CRC. Store records redundantly with generation numbers; recovery chooses the newest valid record and rejects equal-generation conflicting records.

Commit protocol:
1. Acquire an exclusive update lock; reject concurrent installs and insufficient free space.
2. Record transaction intent and flush it.
3. Download, authenticate, resolve, and stage all packages without touching the active slot.
4. Verify every staged file against the signed inventory; run package validation and kernel/driver ABI checks.
5. Flush staged data and metadata; remount/read back and re-verify the staged generation.
6. Write a pending-slot record with the old slot as fallback; flush and read it back.
7. Reboot into the pending slot with a bounded trial counter.
8. Run boot health checks: kernel reaches the health marker, required storage is mounted, core services start, system manifest matches, and no critical self-test fails.
9. Only after explicit health confirmation, atomically mark the new slot active and record the previous slot as rollback generation.
10. Retain the previous generation until at least one later successful update or explicit user-approved cleanup.

A boot attempt is not a commit. If the trial counter expires, select the previous slot and record rollback. The fallback path must work offline.

## 6. Dependency resolution and package installation

Resolve against the installed manifest plus signed repository metadata. The resolver must compute complete dependency closure; detect cycles, missing providers, version conflicts, architecture mismatch, and explicit conflicts; choose a deterministic solution independent of download order; print a human-readable plan; reject partial solutions and unsupported scripts; and enforce disk-space and file-count budgets before download.

Packages are declarative file payloads plus bounded metadata. Arbitrary pre/post-install shell scripts are prohibited. Required migrations are versioned, idempotent, and have a tested reverse operation or write only to a new versioned namespace. User-data migrations never run implicitly as part of a system-slot switch. Data remains readable by the prior application version during rollback.

Application package generations use the same stage/verify/commit rules. If applications are not isolated from system files, deploy them as part of the complete system slot rather than modifying them in place.

## 7. Kernel and driver updates

Kernel, boot protocol, and boot-critical driver updates are staged together. Reject combinations that do not match declared kernel and driver ABIs. Preserve the prior kernel and boot configuration. Do not replace the active Limine binary during an ordinary package transaction.

Match device-specific drivers against exact supported hardware IDs and signed driver metadata. A driver package may not install executable code outside its package inventory. Driver load failure during trial boot is a health-check failure and triggers fallback.

## 8. Progress and user interface contract

Expose distinct phases: checking repository, resolving dependencies, downloading, verifying signatures, verifying digests, staging, validating, ready to restart, trial boot, complete, or rolled back. Show package name, bytes downloaded/total, overall transaction progress, and the reason for any blocked/failed operation. Progress comes from durable transaction state, not a cosmetic timer.

Allow cancellation only before the final metadata-switch phase. Cancellation removes only the transaction staging directory and records ABORTED; it must not touch the active slot or Data. Once a pending slot is armed, cancellation requires controlled rollback.

## 9. Interrupted update recovery

At every boot, before exposing update controls:
1. Read both redundant journal copies and validate bounds, version, CRC, and sequence.
2. Reconcile journal with slot-selection metadata and actual slot manifests.
3. If download/staging was interrupted, leave the active slot unchanged and offer safe resume or discard.
4. If a pending slot was selected but has not completed health confirmation, count the boot attempt and continue trial only within the configured limit.
5. If the trial limit is exceeded or the pending slot is invalid, boot the last known-good slot and record ROLLED_BACK.
6. If the journal is damaged or ambiguous, do not guess which slot is safe: enter Recovery, preserve both slots and Data, and require an explicit recovery decision.
7. Never automatically format, repair destructively, or recreate Data during update recovery.

Recovery must be bootable offline and able to inspect journal copies, slot manifests, package digests, boot-selection metadata, and logs. Recovery operations must be idempotent so repeated power cycles converge on the same safe state.

## 10. Version history and rollback

Record running OS version, kernel build ID, boot protocol version, system ABI, driver ABI, package versions, repository snapshot sequence, and slot generation. Settings should show current version, available update, release notes, update history, and rollback generation.

Rollback switches boot-selection metadata to the retained previous slot; it does not reinstall older packages over the current slot. Block downgrades if a required persistent-data migration is not backward-compatible. A user-visible warning and verified backup are mandatory before any migration that cannot be reversed.

## 11. Repository publication

Release tooling must build packages reproducibly, generate exact file inventories, compute cryptographic digests, sign package metadata and repository snapshots offline with a protected release key, and publish immutable versioned objects. The online repository must not hold the root signing key. Publish signed release notes, supported ABI ranges, and a revocation/key-rotation procedure. CI rejects a release if package inventory differs from the archive or a signature fails against the test trust root.

## 12. Enablement gates

The runtime updater remains unavailable until all of the following pass:
- durable NXFS writes, flush/barrier semantics, atomic metadata replacement, remount consistency, and corruption handling;
- reliable writable block support with bounded timeouts, flush support, request ownership, and DMA lifetime guarantees;
- redundant Recovery journal writes and readback under injected failures at every transition;
- cryptographic signature verification with production key handling and anti-rollback state;
- bounded archive parser and path traversal/fuzz tests;
- deterministic dependency resolver tests;
- A/B slot boot selection in both BIOS and UEFI;
- automated trial-boot failure and rollback tests;
- interrupted download, interrupted extraction, and simulated power-loss tests;
- proof that Data contents and hashes remain unchanged across install, update, cancellation, rollback, and recovery;
- physical hardware recovery tests across multiple vendors before general availability.

Until then, supported behavior is read-only update discovery only when a verified repository client is available; installation must be disabled. Do not advertise a complete updater in release notes or UI.
