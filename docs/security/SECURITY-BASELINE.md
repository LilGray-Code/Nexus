# Nexus OS security baseline

This document distinguishes **enforced behavior** from security work that remains unavailable. A feature is not considered implemented because a setting, manifest field, test stub, or design note exists.

## Enforced in the current kernel branch

- **Ring separation and address spaces:** Native applications enter CPL3 through the Nexus user-mode transition. A process receives a private lower-half page-table tree and supervisor-only shared kernel mappings. User-copy helpers validate user, present, and writable page-table permissions. The runtime still executes one foreground native process at a time; this is not yet a concurrent, SMP-safe multi-process scheduler.
- **W^X and NX:** ELF loading rejects writable+executable segments and maps executable text separately from writable data. The kernel now requires hardware NX before starting the user-mode runtime; processors without NX may boot no further than the explicit security-policy stop.
- **Kernel-issued capabilities:** Process launch code assigns a capability mask. There is no syscall to add capabilities or change process identity. File writes, audio, gamepad, CPU/system information, display, input, file reads, and console output are checked at their syscall boundary.
- **Least privilege:** Ordinary apps do not receive file-write, audio, gamepad, CPU-information, networking, driver-management, or administrator capabilities by default. Packages without a signed capability manifest are restricted to the package-default profile; no package can self-approve a permission.
- **User-mode fault containment:** Recoverable CPU exceptions originating in CPL3 are converted into a failed process result and returned to the kernel. Kernel faults and non-recoverable machine exceptions remain fatal. This is not a substitute for a complete exception, signal, or process scheduler design.
- **Security audit ring:** A bounded 64-event kernel ring records context transitions, denied capabilities, policy rejection, package verification rejection, and isolated user exceptions. It is volatile and currently read only by kernel code; it is not yet a persistent or user-facing security journal.
- **Package verification contract:** The package parser checks the payload digest and requires a non-null signature-verifier callback. Unknown keys or invalid signatures must fail. Test fixtures use a deterministic mock verifier and are not production signing keys.
- **HTTPS trust baseline:** TLS-enabled builds embed a hash-verified Mozilla-derived CA bundle. Certificate, hostname, clock, and handshake failures must not downgrade to plaintext HTTP. TLS-disabled builds do not pretend to provide HTTPS.

## Deliberately unavailable until the full trust chain exists

- **Secure Boot:** Not claimed. A signed and verified chain from firmware through the bootloader and kernel, key enrollment/rotation, revocation, and real firmware validation are not yet integrated.
- **Production package signing/keyring:** The package API accepts a verifier but this branch does not yet provide a release-key-backed production Ed25519 keyring. The test verifier is not suitable for shipping.
- **Verified system updates:** No signed system-update metadata, anti-rollback counter, atomic A/B deployment, boot-success confirmation, or signed rollback path is implemented. Package installation is not an OS updater.
- **Authentication and user accounts:** There is no multi-user account database, login manager, password KDF/credential store, session authorization, or privilege elevation workflow. Do not add a weak hash-based password substitute.
- **Object/file permissions:** Process capability gates are a coarse syscall boundary, not per-user file ownership, ACLs, handles, or a complete object-security model.
- **Firewall:** No complete ingress/egress packet-filter policy or user-facing firewall control is available yet. TLS protects authenticated transport; it is not a firewall.
- **Encryption at rest:** WPA2-CCMP and TLS provide supported link/transport encryption. Full-disk or per-volume encryption, key escrow/recovery, and authenticated key management are not implemented.
- **Third-party drivers:** Drivers are currently kernel-built code selected through the in-kernel device manager. There is no safe signed external driver/module loader. Do not load arbitrary PE/ELF driver payloads or relax validation to make a device work.
- **Persistent security logs and system recovery:** The audit ring is volatile. The installer has a transaction journal, but a signed OS recovery image, secure rollback, and post-update health recovery are not implemented.

## Fail-closed rules

1. Missing verifiers, unknown signing keys, malformed manifests, digest mismatch, invalid certificate chains, and hostname mismatch are errors, never reasons to try unsigned or plaintext alternatives.
2. Unsupported hardware and drivers remain unsupported/failed and visible in diagnostics. Never force-bind an unvalidated driver or execute an untrusted compatibility payload in kernel mode.
3. A denied permission is returned as -EPERM and recorded; it is not retried with a broader capability.
4. If hardware NX is unavailable, user mode does not start.
5. Release claims require passing host tests and boot tests on both OVMF/UEFI and SeaBIOS/BIOS. Secure Boot, update, encryption, authentication, and firewall claims additionally require dedicated integration tests on the relevant real platform.

## Next required security milestones

1. Implement an audited production Ed25519 trust keyring and a signed capability manifest for packages.
2. Add a verified update service with signed metadata, monotonic rollback protection, inactive-slot staging, boot-health confirmation, and tested recovery.
3. Add authenticated user accounts and object-level filesystem permissions before exposing administrator actions or writable trust-store overrides.
4. Integrate a measured and signed UEFI Secure Boot chain with documented key enrollment and revocation.
5. Add packet filtering, encrypted-volume key management, signed external driver support (or keep external drivers disabled), and persistent tamper-evident logging.
