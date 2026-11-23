# Nexus Gaming Support Plan

**Status:** Architecture and acceptance criteria defined; game execution is not yet supported.
**Scope:** Native Nexus games first, then a documented compatibility/runtime layer. This plan does not imply Linux, Windows, Steam, or console-game compatibility.

## Current baseline

The current Original Kernel build has a framebuffer desktop, keyboard/mouse input, PCI discovery, a basic USB xHCI path for directly attached boot keyboards and mice, a small cooperative process facility, and early networking. It does not yet provide a general game platform.

In particular, the current kernel has no general-purpose userland ABI or long-lived preemptively scheduled GUI processes, no gamepad HID report-descriptor pipeline, no audio playback/mixer/device driver, no accelerated GPU driver or graphics API, and no game runtime/package manager. The framebuffer is a software-rendered desktop surface, not a GPU acceleration interface. A game launcher alone would not solve these missing capabilities.

## Gaming platform layers

### G1 — Input devices and controllers

- Define a stable, timestamped input-event ABI for keyboard, mouse, gamepad, joystick, and hot-plug events.
- Parse USB HID report descriptors with strict bounds checking; support report IDs, signed and unsigned values, variable button fields, hat switches, and common Generic Desktop axes.
- Extend xHCI device enumeration and interrupt-IN endpoint scheduling beyond the current boot keyboard/mouse special cases.
- Add controller identity/capability reporting, dead-zone and calibration settings, button/axis mapping, disconnect handling, and multiple simultaneous controllers.
- Add tests using synthetic descriptors, malformed descriptors, short reports, report-ID changes, axis extremes, and hot-plug state resets.
- Later add Bluetooth HID only after Bluetooth transport and pairing/security exist.

**Acceptance:** Two different USB gamepads can be identified and deliver stable button/axis events to a test application in QEMU or on supported hardware, including unplug/replug recovery.

### G2 — Audio foundation

- Discover audio controllers/codecs through PCI and ACPI.
- Select and document an initial supported device class (for example, Intel HD Audio) rather than claiming universal PC audio.
- Implement DMA buffer allocation, descriptor rings, stream setup/teardown, interrupt/poll completion, sample-rate/channel/format negotiation, and safe device reset.
- Define a kernel audio service boundary and a user-facing PCM stream API with bounded queues and back-pressure.
- Add a mixer/resampler, per-stream volume, mute, output-device selection, and eventually capture/microphone support.
- Keep audio callbacks real-time safe: no blocking filesystem/network calls or unbounded allocation in interrupt context.
- Test silence, tone generation, format conversion, underruns/overruns, stop/restart, and device loss.

**Acceptance:** A user process can play a known PCM test tone through a documented supported physical or emulated audio device without underruns or kernel instability.

### G3 — Graphics and display

- Separate the desktop framebuffer from a versioned graphics device/API boundary.
- Establish display modes, pixel formats, vsync/present semantics, surfaces, clipping, damage regions, and frame pacing.
- Implement a software renderer as the correctness baseline, with accelerated drivers as optional backends.
- Add GPU discovery and safe driver binding; begin with a deliberately selected device family and publish exact tested PCI IDs.
- For accelerated support, implement memory management, command submission, synchronization/fences, reset/recovery, and a stable userspace graphics API. Do not expose arbitrary MMIO or unrestricted DMA to games.
- Define a 2D API first, then a small 3D API (shader/program model, buffers, textures, render targets, depth/stencil, blending, and presentation) only when a real driver can implement it.
- Add frame-time and GPU-reset diagnostics.

**Acceptance:** A native test app can render a moving, textured scene at a documented resolution and frame rate on both the software fallback and at least one explicitly supported accelerated GPU, with clean recovery after app exit.

### G4 — Execution, memory, and isolation

- Complete a versioned syscall ABI, virtual address spaces, user/kernel isolation, handle-based resource ownership, and safe IPC.
- Implement preemptive multitasking, per-process scheduling/accounting, timers, thread lifecycle, synchronization primitives, and robust process teardown.
- Provide memory mapping and shared graphics/audio buffers with explicit permissions and lifetime rules.
- Add crash isolation, resource quotas, input focus, window surfaces, and a compositor presentation protocol.
- Keep drivers in the kernel only where required; use a broker/service boundary for device access where practical.

**Acceptance:** Multiple long-running user applications can render, receive input, play audio, yield/preempt, and exit independently without blocking or corrupting the desktop.

### G5 — Game runtime and developer platform

- Publish a Nexus-native executable format/toolchain and a versioned game-facing SDK.
- Provide stable libraries for input, graphics, audio, files, clocks, threading, and window/presentation.
- Define asset paths, save-data directories, configuration, localization, controller remapping, and gamepad hot-plug APIs.
- Add a package format with signed manifests, dependency declarations, integrity verification, install/update/uninstall, rollback, and capability permissions.
- Build and ship a small open test game and a conformance suite before advertising the SDK.
- Consider compatibility layers only after the native ABI and security model stabilize. Windows/DirectX, Linux/ELF+POSIX, Steam, Vulkan, OpenGL, and console compatibility are separate major projects, not automatic consequences of a native game API.

**Acceptance:** A third-party developer can build a sample game using published SDK documentation, package it, install it, launch it from the desktop, save progress, and uninstall it cleanly.

## Release gates

A gaming-capable release must pass all of the following:

1. Controller descriptor fuzz/bounds tests and physical USB controller hot-plug tests.
2. Audio format, sustained playback, underrun, device reset, and output selection tests.
3. Graphics API conformance, sustained rendering, display-mode, and GPU reset tests.
4. Multi-process scheduling, memory isolation, input focus, and crash-recovery tests.
5. Install/update/rollback/save-data persistence tests.
6. At least one complete sample game tested end-to-end on every advertised hardware profile.
7. A published compatibility matrix listing exact GPU, audio, controller, firmware, and resolution combinations tested.

QEMU-only success is not evidence of physical GPU or audio support. Hardware support must be stated per tested device and driver; do not claim "supports everything" or universal PC game compatibility.

## Implementation order

1. Complete generic USB HID parsing and xHCI interrupt endpoint support; expose controller events.
2. Add an audio-device driver and PCM service for a narrowly defined first hardware target.
3. Establish userland scheduling, syscall, memory, and graphics-surface contracts.
4. Implement software graphics presentation and the first accelerated GPU backend.
5. Ship the native SDK, package manager, and a conformance-tested sample game.
6. Expand device coverage from real hardware test results.

Each stage must include host-side unit tests, QEMU integration tests where hardware emulation is available, and explicit physical-device validation before its compatibility claim is promoted.


## Steam and Epic compatibility is a separate platform project

A native Nexus game SDK does not automatically run Windows games or third-party launchers. Steam and Epic compatibility requires a Windows application compatibility environment (Win32/NT API coverage, loader, registry/configuration semantics, graphics translation, audio/input integration, fonts, networking, installers/updaters, and per-title testing). Steam additionally has its own client services, overlays, Steamworks integrations, DRM options, and anti-cheat arrangements; Epic titles have their own launcher, account, entitlement, update, and anti-cheat dependencies.

Nexus must not claim that Steam, Epic, Proton, Wine, or any particular game works until the actual supported build and game have been tested on Nexus hardware. Do not bypass DRM, anti-cheat, account controls, or publisher restrictions. Use official distribution and authentication flows.

### Recommended compatibility sequence

1. Ship a stable native Nexus game runtime and a few openly licensed sample games.
2. Complete the userland, graphics, audio, controller, storage, and security interfaces needed by applications.
3. Evaluate a legally compatible Windows API compatibility layer and graphics translation stack as a distinct project; document unsupported APIs and performance limitations.
4. Build and test a narrowly scoped Windows application suite, then investigate official Steam/Epic client compatibility and publisher requirements.
5. Publish a per-launcher and per-game compatibility matrix. A launcher installing successfully is not evidence that its games launch or play correctly.

Downloads must remain independent of execution: Nexus may safely download a file only after validated HTTPS and streaming storage exist; installation and execution require separate publisher verification, user consent, and application isolation.
