# Nexus application platform

**Status:** Native application foundation in active development. This document
records what is implemented in the current source tree and what must not yet
be advertised as compatible.

## Current native execution model

Nexus applications are x86-64 user-mode programs loaded as static ELF64
images. The loader creates a private lower-half user address space, enforces
read/write/execute page permissions and rejects writable-executable segments.
Each process has a private kernel stack and a capability mask. The foreground
execution path is synchronous: the shell resumes after the application exits.
The process table is small lifecycle bookkeeping; it is not yet a general
multi-process scheduler.

The ELF parser accepts:

- `ET_EXEC` x86-64 static executables.
- A constrained static `ET_DYN`/PIE form with a single dynamic table and
  bounded `R_X86_64_RELATIVE` relocation processing.

It rejects `PT_INTERP`, ELF TLS, external `DT_NEEDED` dependencies, unsupported
dynamic tags, malformed or overlapping segments, invalid relocation ranges,
and writable-executable pages. The executable image input limit is 4 MiB.
Static PIE support is not a general runtime linker and does not enable
third-party shared objects.

## SDK and ABI

The public ABI is in `kernel/include/nexus/app.h`; matching syscall constants
and freestanding wrappers are in `userland/include/nexus/`. ABI major version
1 uses `int 0x80`, with RAX for the call number/result and RDI/RSI/RDX for
arguments. The SDK includes small bounded helpers in `libnexus.a`, file
read/write wrappers, console output, input polling, CPU capability queries,
sleep/tick calls, framebuffer blits, audio and gamepad interfaces.

The additive syscall 16 returns the optional permission mask granted to the
running app. Existing ABI structure layouts remain fixed; CI checks syscall
constant parity and public structure sizes/offsets. ABI major version changes
must be explicit and must not silently reinterpret existing structures.

The SDK is intentionally freestanding. It does not provide full ISO C libc,
POSIX threads, signals, process spawning, sockets, filesystem directory
descriptors, a C++ runtime, or an ELF shared-library environment. The runtime
has no general heap allocator today.

## Package installation, updates and signatures

The native package format is `.NPK`, currently version 1. Its canonical
manifest includes a package ID, version, architecture, Nexus ABI version,
entry-point filename, payload size, SHA-256 digest, optional signed
`PERMISSIONS=` request, publisher key ID and signature. Installation checks
the payload hash, delegates signature verification to a required callback,
validates the ELF payload and commits a signed receipt after the executable
has been stored. Updates stage the new executable and receipt before cleaning
up the old version. Uninstallation removes the receipt, local grant record
and installed payload.

The current package limit is 4 MiB per payload. The receipt and executable
storage use the writable NXFS data volume.

**Signature verification is not yet a production-complete trust service.** The
package layer provides a callback contract; it does not itself embed a
production Ed25519 implementation, pinned publisher keyring, authenticated
key updates, rotation/revocation or release-signing pipeline. Host package
tests use a deterministic mock verifier and prove parser/install/tamper
handling only. They do not prove publisher authenticity for a released ISO.

## Permissions and sandbox boundary

Signed package metadata describes the capabilities an application requests.
It does not grant those capabilities. A separate local `.NPA` approval record
stores the subset approved by the system package manager. Missing approval
metadata means no optional capabilities. A package update revokes the old
approval so new requested permissions must be reviewed again.

The current kernel checks the following permissions at syscall entry:

| Capability | Protected operation |
|---|---|
| File read | Open a VFS file for reading |
| File write | Write a file through the app file API |
| Input | Read keyboard/mouse events |
| Display | Query display geometry and blit framebuffer content |
| Audio | Submit PCM audio frames |
| Gamepad | Poll USB gamepad events |

Core exit, bounded console output, sleep/tick and CPU capability queries are
not part of this optional mask. User pointers are range-checked before data is
copied. Permission denial returns `-EACCES`.

The approval backend is present, but a complete graphical permission prompt,
per-app permissions page, protected settings store, policy migration and
user-visible audit history remain incomplete. The current local approval
record is a mutable data-volume file; it is not backed by TPM/sealed key
material and should not be described as tamper-proof against an administrator
or someone with offline disk access.

The loader isolates app address spaces and applies W^X segment policy.
Ordinary ring-3 CPU exceptions now return through the user-return trampoline,
mark the process failed and retain the exception vector, error code and fault
instruction address. The boot self-test includes a deliberately invalid
instruction and checks that the kernel continues. Double fault, NMI and
machine-check remain system-fatal. There is no core dump format, crash UI,
automatic restart policy, or production crash-report uploader yet.

## Compatibility status

**Only the Nexus-native static ELF64 application ABI is supported.** An ELF
file is not inherently a Linux application: Linux binaries expect the Linux
syscall ABI, process/thread/signal semantics, filesystem conventions, device
interfaces and usually glibc or musl plus the dynamic loader. Nexus does not
implement that environment.

Windows PE applications are not supported. Windows PE/INF code elsewhere in
the repository inspects *driver packages*; it does not implement the Win32
API, NT process model, PE application loader, registry, COM, Windows DLLs or
the required DirectX/graphics/audio/input compatibility services. Do not
describe that driver-package inspection as Windows application support.

POSIX applications require the standard system interfaces and C runtime
environment, not simply an executable-file extension. Nexus is not
POSIX-conformant and does not currently expose that interface set.

Cross-platform runtimes are also future work: there is no JVM, .NET runtime,
WASI/Wasm runtime, Electron/Chromium runtime, or general graphics/window
toolkit. Nexus Browser web shortcuts are not native apps and do not imply
browser-engine support.

### File-format and compatibility research

- Linux's `ld.so` dynamic linker loads shared objects and resolves program
  dependencies. Nexus does not implement an equivalent runtime linker:
  [Linux ld.so manual](https://man7.org/linux/man-pages/man8/ld.so.8.html).
- POSIX specifies a broad operating-system API and execution environment,
  not simply an executable-file extension:
  [The Open Group POSIX.1-2024](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/).
- AppImage is a packaging/runtime model with an embedded filesystem payload;
  it still expects a compatible Linux environment:
  [AppImage concepts](https://docs.appimage.org/introduction/concepts.html).
- Flatpak combines application packaging, runtimes, sandboxing and portal
  services. Its design can inform Nexus permissions, but Flatpak packages
  cannot run without the corresponding Linux runtime:
  [Flatpak basic concepts](https://docs.flatpak.org/en/latest/basic-concepts.html)
  and [sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html).
- OCI defines a container runtime/bundle contract, not a desktop application
  ABI or substitute for a compatible Linux userspace:
  [Open Container Initiative Runtime Specification](https://github.com/opencontainers/runtime-spec).
- Wine's architecture illustrates the breadth of a PE compatibility layer:
  Windows API implementation, loader and PE/Unix transitions are required;
  accepting the PE file format alone is not enough:
  [Wine 8.0 release notes](https://www.winehq.org/announce/8.0).

## Compatibility roadmap and release gates

1. **Native platform hardening:** permission UI and protected grant storage,
   production signature/key management, ABI compatibility tests, app lifecycle
   and crash reporting.
2. **System libraries:** stable libc subset, kernel-backed allocator, thread and process
   APIs, file/directory APIs, dynamic loader design, ELF symbol/version and
   relocation support, shared-library cache, dependency solver and ABI testing.
3. **Runtime packaging:** signed library/runtime bundles, version pinning,
   dependency closure, rollback-safe updates and garbage collection.
4. **Linux compatibility:** define a Linux syscall personality and validate
   one actual static Linux program first, then dynamic musl/glibc programs,
   syscall behavior, file descriptors, processes, threads, signals, futexes,
   networking and device services. Do not advertise general Linux ELF support
   before this matrix passes.
5. **Windows compatibility:** only after the native and Linux runtime layers
   are stable, evaluate a Wine-like compatibility host with PE loader, NT
   object/process model, Win32 API layers, Windows DLL loading, registry,
   synchronization, DirectX/Vulkan translation and crash isolation. Test named
   Windows apps on physical hardware; driver INF matching is not a shortcut.
6. **Cross-platform runtimes:** evaluate WASI, JVM, .NET and Electron
   independently, including security updates, sandboxing and media/graphics
   dependencies. Do not treat WebAssembly parsing as full WASI or browser
   compatibility.

A compatibility claim requires a documented test matrix with named application
versions, installation and update behavior, launch, core features, graphics,
audio, input, networking, failure recovery and physical-hardware coverage.
Successful ELF parsing, a bootable ISO, or a launcher icon is not evidence of
application compatibility.
