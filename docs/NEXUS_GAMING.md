# Nexus Gaming and Game Downloads

## Product goal

Nexus should let users discover games, download installers and game files from
trusted websites, manage a local game library, and launch supported games from
the desktop. The long-term goal includes compatibility paths for major PC game
stores such as Steam and Epic Games Store.

This is a product goal, **not a claim that game downloads or those launchers
work today**. The current OS does not have the secure transport, storage
streaming, application runtime, graphics/audio stack, or compatibility layers
needed to deliver that experience safely.

## Current status

| Capability | Status |
|---|---|
| Open a basic plain-HTTP text page | Prototype only |
| Secure HTTPS downloads | Not implemented; TLS and certificate validation are absent |
| Large-file streaming to persistent storage | Not implemented; current file APIs are bounded and not a streaming download target |
| Download queue, pause/resume, retry, progress, integrity verification | Not implemented |
| Native Nexus game package format and installer | Not implemented |
| Game library and launch management | Not implemented |
| Steam client / SteamOS compatibility | Not implemented |
| Epic Games Launcher compatibility | Not implemented |
| Windows game compatibility (Wine/Proton-like layer) | Not implemented |
| GPU acceleration, audio, controller coverage, anti-cheat compatibility | Incomplete / not claimed |

## Safe implementation sequence

### 1. Secure network transport — blocking prerequisite

- Implement a maintained and reviewed TLS 1.2/1.3 implementation or port a
  suitable permissively licensed TLS library with its dependencies and
  provenance recorded.
- Provide cryptographically secure randomness, certificate-chain validation,
  hostname verification, validity-period checks, and a maintained trust store.
- Implement robust HTTP response parsing, redirects, content-length and
  transfer-encoding handling, timeouts, and connection failure recovery.
- Fail closed on invalid certificates, unsupported TLS, malformed responses,
  and HTTPS downgrade attempts. Never send account credentials or game-store
  tokens over plain HTTP.

No game download from an internet URL should be enabled before these gates pass.

### 2. Persistent streaming storage

- Add a bounded streaming write API that writes sequential chunks to a
  temporary file without buffering the entire game in RAM.
- Extend the persistent filesystem to support files much larger than the
  current 1 MiB NXFS file limit, with explicit volume capacity accounting.
- Flush data and metadata in a recoverable order; preserve the previous final
  file until the replacement is fully verified.
- Support safe cancellation, interrupted-transfer recovery, disk-full handling,
  and atomic promotion from a temporary partial file to its final name.
- Test large transfers, short writes, device removal, simulated power loss,
  corrupted metadata, and reboot recovery on disposable virtual disks.

### 3. Native download manager

- Provide a desktop download queue with source URL, destination, progress,
  transfer rate, state, retry, pause/resume, and cancel.
- Restrict destinations to a user-selected managed directory; canonicalize
  filenames and reject path traversal, device names, control characters, and
  collisions.
- Verify a publisher-provided cryptographic digest or signed package manifest
  when available. A transport checksum alone does not prove publisher identity.
- Clearly identify publisher, source domain, package type, and signature status.
- Never execute a downloaded file automatically. Require an explicit install
  action and user confirmation.
- Persist queue metadata safely and recover incomplete downloads after reboot.

### 4. Native Nexus game packages

Define a signed package format with:
- publisher identity and signing-key rotation/revocation policy;
- package ID, version, architecture, dependencies, and minimum Nexus ABI;
- file manifest, sizes, hashes, install paths, and declared permissions;
- install, update, uninstall, and rollback transactions;
- a sandbox/capability policy and an auditable install receipt.

The package manager must reject unsigned or malformed packages by default.
A game package must not be able to overwrite OS files or access arbitrary user
data merely because it was downloaded.

### 5. Game runtime and controller support

- Establish a stable user-mode process, graphics, audio, input, storage, and
  device-driver ABI before third-party game execution is promised.
- Add a game launcher/library that tracks installed versions, launch options,
  save-data locations, and uninstall state.
- Expand GPU drivers, audio, gamepad support, power management, and performance
  tooling with hardware-specific tests.
- Build a documented native SDK and compatibility test suite for Nexus games.

## Steam and Epic Games Store

Steam and Epic Games Store are not just download websites. Their desktop
launchers depend on proprietary services, authentication, update systems,
platform APIs, and sometimes DRM or anti-cheat components. Many games also
depend on Windows APIs, DirectX, Vulkan/OpenGL drivers, audio, and specific
anti-cheat integrations.

A realistic compatibility plan is:

1. **Native Nexus games first:** publish an SDK and package format, then ship
   small test games that exercise graphics, audio, input, storage, and updates.
2. **Store integration research:** evaluate official developer/distribution
   interfaces and licensing requirements. Do not impersonate store clients,
   bypass DRM, or redistribute proprietary launcher binaries.
3. **Compatibility runtime:** only after the user-mode ABI and graphics stack
   mature, evaluate a maintained Windows compatibility layer and its licensing,
   security, performance, and anti-cheat limitations.
4. **Official launcher validation:** test supported official launcher builds
   and real game titles individually. Compatibility with one launcher version
   or one game must not be generalized to the whole catalog.

The project must not claim Steam, Epic, Windows-game, DRM, or anti-cheat
compatibility until real end-to-end installations, updates, launches, and
gameplay tests pass on supported hardware.

## Release acceptance tests

- Download a multi-gigabyte test payload over validated HTTPS to a user-chosen
  persistent volume without loading it all into RAM.
- Verify a known publisher signature and reject tampered, expired, or untrusted
  packages.
- Exercise pause/resume, redirects, retries, cancellation, disk-full, network
  loss, and interrupted reboot recovery.
- Prove that path traversal and malicious package manifests cannot escape the
  selected install directory or modify protected OS files.
- Install, launch, update, roll back, and uninstall a native sample game.
- Test controller, audio, GPU, save persistence, and performance on each
  supported hardware profile.
- For any claimed Steam or Epic support, install and use the official launcher
  and named games end to end, documenting versions and known limitations.

Until these tests pass, the UI must clearly label game-store support as
unavailable rather than presenting a nonfunctional launcher button.
