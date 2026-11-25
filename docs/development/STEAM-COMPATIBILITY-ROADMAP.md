# Nexus Game and Steam Compatibility Roadmap

**Status snapshot:** 2026-11-25  
**Architecture constraint:** Nexus remains its own x86-64 operating system and custom kernel. This roadmap does not replace it with a Linux distribution.

## What is implemented today

The current branch has a working foundation for small Nexus-native applications, but it is not yet a general-purpose desktop ABI.

- Ring-3 execution for a bounded, foreground static ELF64 application, with x87/SSE2 state management.
- Native ABI version 1 for basic file calls, console output, display information, timer sleep, keyboard/mouse polling, framebuffer presentation, gamepad polling, and optional AC'97 PCM playback.
- Firmware-framebuffer display and VirtIO-GPU 2D scanout/presentation. Rendering is still CPU-side; this is not a Vulkan/OpenGL/Direct3D implementation.
- PS/2 and xHCI boot-keyboard/mouse input. xHCI now has a bounded generic HID report-descriptor parser and an event path for up to four common USB gamepads. The parser supports one input report ID, up to eight axes, 32 buttons, and one hat field; composite controllers, arbitrary HID layouts, USB hubs, and Bluetooth pairing are not promised.
- Network foundations including Ethernet device drivers, TCP/HTTP, and an Mbed TLS-backed HTTPS client.
- NXFS v2 persistence, with a deliberate 1 MiB per-file and 64-entry limit. The installer boot-payload limit is separately 2 MiB and should not be confused with NXFS file capacity.
- A first .NUP application package format and signed-catalog parser. They are not a production app store: a production Ed25519 verifier/keyring and HTTPS catalog/package download-to-install path are still missing.

See [Native App SDK](NATIVE-APP-SDK.md) and [Software Ecosystem](SOFTWARE-ECOSYSTEM.md).

## Why Steam games do not run yet

Steam is not just a browser or a downloadable executable. The Linux Steam client and its games expect a broad Linux userspace and kernel interface. Proton adds Wine and graphics translation layers; it does not remove the need for the host OS to provide the system-call, memory, threading, file, device, and graphics contracts that those components use.

The current Nexus native ABI is intentionally much smaller. It does not provide Linux syscall compatibility, POSIX process semantics, a dynamic linker, shared libraries, mmap, clone, futex, epoll, Linux ioctl/device interfaces, or the usual /proc, /sys, and /dev contracts. The kernel's scheduler currently supports a small set of cooperative kernel threads; the native process runner executes one foreground app synchronously. A minimal AC'97 48 kHz stereo PCM playback path is now present, but there is no audio capture, multi-stream mixer, userspace audio server, or general HDA driver. There is still no hardware-accelerated 3D API.

The storage limits are also far below game requirements: current NXFS v2 files are at most 1 MiB each, packages accept one static ELF payload up to 1 MiB, and the volume has 64 entries. Most game installations are far larger and contain many files.

Therefore **the Steam client and commercial Steam games are not supported at this stage**. The new gamepad API is one prerequisite, not Steam compatibility.

## Required engineering milestones

### 1. Stabilize and grow the kernel execution model

- Complete repeatable BIOS and UEFI boot/install tests before increasing concurrency.
- Add safe preemptive scheduling from a validated interrupt/trap context, with per-thread kernel stacks, correct FPU/SSE context ownership, and synchronization around shared kernel state.
- Add independently addressable user processes, process lifecycle, blocking waits, exit/reap semantics, per-process handles, and a real foreground/windowed task model.
- Replace fixed, small process-image assumptions with bounded virtual-memory regions and page-backed allocation. Add page-fault handling, guard pages, mmap/unmap primitives, copy-on-write where justified, and robust out-of-memory behavior.
- Add SMP-safe allocator, scheduler, VFS, and driver synchronization before enabling multiple CPUs.

**Acceptance tests:** preemption stress tests, process isolation tests, address-space fault tests, repeated process creation/reaping, and QEMU BIOS/UEFI boot under load.

### 2. Build a real ELF and userspace runtime

- Support PIE/ET_DYN, PT_DYNAMIC, relocation processing, TLS, and a trusted dynamic loader.
- Implement a versioned shared-library ABI and libnexus/POSIX-compatible C runtime, with robust file descriptors, pipes, signals, clocks, polling, and sockets.
- Expand process memory and file mapping interfaces, environment/argument handling, and executable permissions.
- Keep the current Nexus-native ABI stable while adding a separate Linux compatibility ABI. Do not silently pretend a native syscall table is a Linux syscall table.
- Add 32-bit compatibility only if the scheduler, memory model, FPU state, libraries, and tested workloads justify its maintenance cost.

**Acceptance tests:** dynamic ELF load/unload, relocations and TLS fixtures, multi-process shell/utilities, libc conformance tests, and ABI tests for Linux syscalls used by the target runtimes.

### 3. Add audio and complete game input

- Extend the initial AC'97 single-buffer 48 kHz PCM playback path with multi-buffer rings, robust interrupt/poll completion, and codec controls; add Intel HD Audio support for modern PCs.
- Add capture streams, device hotplug, and a userspace audio service/mixer with latency management.
- Define stable audio APIs with buffer queues, latency handling, sample formats, and device hotplug.
- Expand controller support with per-interface composite-device handling, more HID report forms, reliable disconnect notifications, calibration/dead zones, rumble/force feedback, USB hubs, and Bluetooth HID.
- Add game-facing input APIs that can be consumed by SDL-like libraries without exposing hardware internals.

**Acceptance tests:** playback/capture loopback, underrun/overflow stress, multiple USB controllers, hotplug while an app is running, and supported SDL input tests.

### 4. Implement a real GPU and 3D graphics stack

- Keep framebuffer and VirtIO 2D presentation as safe fallbacks.
- Add a real GPU command-submission and memory-management model, fence/synchronization primitives, buffer objects, and display modesetting.
- Implement one hardware driver path at a time (for example, VirtIO-GPU 3D for QEMU first), then evaluate Intel, AMD, and NVIDIA device families separately.
- Expose tested OpenGL/Vulkan user APIs and a shader/compiler/runtime stack. A GPU name detected on PCI is not proof that the device is accelerated.
- Build a window/compositor model and predictable fullscreen/surface APIs.

**Acceptance tests:** renderer conformance suites, GPU reset/recovery, texture/shader tests, frame pacing, multi-window composition, and Vulkan/OpenGL test workloads.

### 5. Make storage and software delivery game-sized and trustworthy

- Design NXFS v3 as a new on-disk format rather than changing v2 limits in place. It needs directories, scalable allocation/extents, large files, crash recovery, quotas/free-space accounting, and migration/compatibility tests.
- Support package assets, multiple files, shared libraries, install manifests, dependency constraints, rollback, and atomic updates.
- Complete production signature verification with a vetted Ed25519 implementation, pinned release keys, rotation/revocation policy, and known-answer/negative test vectors. The test-only fake signature verifier must never be used in production.
- Wire the catalog to HTTPS fetching and verify catalog signature, package hash, package signature, publisher identity, and install destination before any filesystem mutation.
- Add a safe updater with staged updates and recovery.

**Acceptance tests:** multi-gigabyte sparse and non-sparse files, volume-full handling, power-loss injection, package rollback, invalid-signature rejection, key rotation, and end-to-end HTTPS package install.

### 6. Build the Steam/Proton compatibility environment

Only after the previous milestones:

- Implement the Linux syscall and device ABI surface actually used by Steam Runtime and target games, guided by traces and compatibility tests rather than guesswork.
- Port or provide the required Linux userspace libraries and service contracts, including the Steam Runtime dependencies.
- Port Wine/Proton and its process, registry, graphics, audio, controller, and filesystem expectations to Nexus's compatibility layer.
- Integrate DXVK/VKD3D and a supported Vulkan driver path for Direct3D titles. OpenGL-only rendering is not enough for modern Proton games.
- Provide Steam client login, updates, runtime deployment, content downloads, pressure-tested storage, controller mapping, audio, overlays, and game-specific compatibility profiles.
- Treat anti-cheat and DRM support as per-title work; some games will remain incompatible even after the basic platform runs.

**Acceptance tests:** first a Steam Runtime shell and client boot; then verified launch/download/update of a small native Linux title; then Proton titles selected from a public compatibility test matrix. Report each game as tested, partially working, or unsupported rather than making blanket compatibility claims.

## Immediate next priorities

1. Keep the original-kernel BIOS/UEFI and installed-disk test pipeline green.
2. Add QEMU/host tests for HID gamepad report parsing, gamepad queue handling, and controller disconnects.
3. Implement safe preemptive process scheduling and per-process address spaces.
4. Design NXFS v3 before attempting multi-gigabyte downloads or game installs.
5. Add audio and VirtIO-GPU 3D in QEMU as explicit, independently testable driver projects.
6. Complete the production software trust chain before enabling a public app catalog or game downloads.

The milestone order is intentional: a game launcher UI cannot compensate for missing memory management, Linux ABI, GPU, audio, and large-file storage.
