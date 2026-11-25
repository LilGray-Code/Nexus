# Nexus Store and application compatibility

## Current implementation

The desktop now has a dedicated Store view, reachable from the taskbar and Start
launcher. Its first-party catalog contains official HTTPS destinations for
Steam, Discord, Spotify, GitHub, VS Code for Web, and itch.io.

The current Install action creates a small Nexus-owned `.URL` shortcut under
the volume root. Its record includes a format version, a fixed catalog ID, and
the catalog's HTTPS URL. Reinstalling an identical shortcut is idempotent;
unrelated files with a reserved shortcut name are never overwritten. Removal
only deletes a matching Nexus shortcut from writable Nexus storage. Read-only
FAT32 boot media is never modified. If writable NXFS is not mounted, the VFS
uses its bounded RAM overlay, so those shortcuts are temporary and are lost
on power-off.

The Store can open the selected official site in Nexus Browser. This is a
website shortcut, not installation of the publisher's desktop application.
The current in-kernel browser is a bounded HTTPS/HTML-text browser, not a
Chromium-class browser engine; JavaScript-heavy sites, WebAssembly applications,
protected media, microphone/camera access, notifications, and modern web
application APIs are not yet available.

## Native application support status

Nexus does not yet install or run the Windows clients for Steam, Discord, or
Spotify. It also does not currently support arbitrary Windows PE executables.
A Store label saying that a website shortcut is installed must never be
interpreted as the native client being installed or runnable.

The early native userland can load a constrained static x86-64 ELF executable,
but the system does not yet provide a full POSIX runtime, dynamic linker,
shared-library ABI, GUI application SDK, background process/service model,
or a Windows PE loader and compatibility layer. Games additionally need
working graphics, audio, input, storage, controller, and anti-cheat-compatible
interfaces. Compatibility and anti-cheat support must be evaluated per
application; it cannot be promised by a generic installer.

## Native package platform roadmap

A safe native Store should be built in layers:

1. **Package format and catalog.** Define a versioned package manifest with
   stable application ID, publisher, architecture, minimum Nexus ABI, package
   size, payload digest, entry point, capabilities, and dependency declarations.
   Catalog metadata and package signing keys must be rooted in the OS trust
   store. A plain SHA-256 digest verifies integrity only when the digest itself
   is trusted; it is not a publisher signature.
2. **Trust and download pipeline.** Fetch metadata and payloads over verified
   HTTPS. Verify a publisher/catalog signature before trusting package
   metadata, check the payload digest and size while streaming, reject path
   traversal and unsupported architectures, and retain a clear provenance
   record. No package installer should execute downloaded code during install.
3. **Transactional package manager.** Install into per-application directories,
   stage files before commit, atomically switch the active version, preserve
   user data, support uninstall and rollback, and recover safely after power
   loss. Never overwrite the OS or another app's files. Keep application
   permissions and update channels explicit.
4. **Native runtime and GUI SDK.** Add process spawning and lifecycle
   management, virtual memory and heap APIs, a stable system-call ABI,
   dynamic linking and shared libraries, filesystem descriptors, window
   surfaces, input, clipboard, audio, networking, and permission boundaries.
5. **Windows compatibility.** First finish the native runtime and driver
   interfaces. Then evaluate a PE/COFF loader plus the Win32 API and runtime
   services required by target applications. Wine/Proton-class compatibility
   is a major subsystem, not a file-extension association; it needs process,
   memory, registry/prefix, graphics, audio, input, and library compatibility.
   Driver and anti-cheat requirements may still make individual games
   unsupported.
6. **Store operations.** Add publisher accounts and signing, app discovery,
   search, categories, version history, safe background downloads, update
   checks, changelogs, uninstall/repair, ratings/reviews moderation, and
   rollback. Keep first-party, publisher-verified, and community packages
   distinguishable.

## Security invariants

- Only fixed HTTPS catalog URLs are exposed by the current Store catalog.
- The Store shortcut record is validated byte-for-byte before it is treated
  as installed or removed.
- A shortcut-name collision with user data causes installation to fail closed.
- Root-scoped deletion only affects writable Nexus-owned storage.
- Web shortcuts, package integrity checks, trusted signatures, and executable
  compatibility are separate concepts and must be reported separately.
