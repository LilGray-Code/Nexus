# Nexus PC Gaming Architecture and Implementation Plan

**Priority:** Critical product track  
**Current branch:** `nexus-pc-gaming-foundation`  
**Purpose:** Make gaming a first-class OS workload without overstating Steam, Proton, GPU, anti-cheat or game compatibility.

## 1. Current reality

Nexus already has several useful pieces for first-party games:
- A bounded native static ELF64 execution path.
- A framebuffer/display-info syscall for native apps.
- A keyboard/mouse input path and a USB HID gamepad report parser/queue.
- AC'97 stereo PCM output when a supported controller is initialized.
- A bundled native Breakout game and a gamepad test application.
- A package verifier/installer for small signed static ELF applications.
- A new persistent game-library metadata format and compatibility evaluator in this branch.

However, a game that runs in the native Nexus ELF ABI is **not** a Linux game, and a detected GPU is **not** an accelerated graphics stack.

Current hard limits include:
- User processes run through a small, bounded process table; launch is synchronous and does not provide a general background process scheduler.
- The current ELF loader accepts bounded static `ET_EXEC` and constrained static PIE `ET_DYN`; it is not a general dynamic linker or Linux ELF interpreter.
- The user ABI is a small Nexus-specific syscall interface, not the Linux syscall ABI.
- There is no general user-mode POSIX/libc runtime, glibc multilib environment, shared-library loader, Linux signal/futex/epoll/ioctl compatibility layer, or container runtime.
- USB HID gamepad input and direct AC'97 PCM output exist, but there is no complete SDL/GameInput-style userspace API, PipeWire/PulseAudio stack, headset/audio-class support, haptics or broad controller hotplug manager.
- The desktop uses a CPU framebuffer rasterizer. VirtIO-GPU currently provides a 2D host scanout path, not a 3D renderer.
- OpenGL, Vulkan, DXVK, VKD3D-Proton, Wine, Proton and the Steam client are not currently available as executable runtimes.

The current system capability query deliberately reports only the native static ELF path, the USB HID gamepad input implementation, an initialized 2D display surface, and AC'97 PCM when that controller is ready. It does not infer Steam/Proton support from PCI detection, the existence of a Steam web shortcut, or a virtual display device.

## 2. Implementation delivered in this increment

### Capability and compatibility model

`kernel/src/gaming.h` and `gaming.c` define explicit platform and graphics API requirements for:
- Native Nexus static ELF titles.
- Native Linux/Steam titles.
- Windows titles through Proton.
- Windows titles through Wine outside Steam.

Capabilities are individual flags for Linux syscall ABI, POSIX, 32/64-bit glibc, dynamic linking, multilib graphics, D-Bus, X11 WSI, audio-server protocols, Steam Runtime/client, Wine/Proton, OpenGL, Vulkan, DXVK, VKD3D-Proton, 3D rendering, GPU fences and anti-cheat providers.

The evaluator returns **ready to test**, **verified on this exact GPU/driver configuration**, **playable with reported limitations**, **missing prerequisites**, **blocked by anti-cheat**, **needs retest**, or **tested unsupported**. A game is never marked verified merely because its declared runtime requirements happen to exist.

Verified evidence is tied to the game's tested build, required capability mask, PCI vendor/device and GPU driver version. Changing the GPU or driver forces a retest.

### Persistent game library

`kernel/src/game_library.h` and `game_library.c` add a versioned `GAMES.NXL` metadata database in writable NXFS. It supports up to 24 titles, safe ID/path validation, per-game platform/API/anti-cheat declarations, tested build identifiers, test notes, exact hardware/driver evidence, launch counts and last-played ticks.

The metadata file uses fixed-width little-endian records and a SHA-256 payload checksum. It is metadata only; it does not contain full game depots or arbitrary game binaries.

The first launch backend is intentionally narrow: native Nexus static ELF files with a simple root filename. Steam, Proton and Wine records fail with an explicit not-implemented backend result instead of being passed to the native ELF loader. An untested native title requires an explicit caller opt-in before launch.

The desktop terminal exposes `games` to list titles and their current assessment, and `game ID` to launch a title. First use registers the bundled `ARCADE.ELF` Nexus Breakout entry only when the metadata file is absent; an invalid/corrupt metadata file is left untouched. The caller's explicit launch command is the opt-in to run an untested native title. The launch command still uses the synchronous bounded native ELF runner.

The library API is not yet wired into a dedicated graphical game-library page, game download/depot manager or Steam client. The current metadata database does not download or install commercial game content.

### Shader-cache foundation

`gaming.c` provides an NXFS-only per-blob shader cache:
- The SHA-256 key includes game ID/build, graphics API, translator version, shader compiler version, GPU vendor/device, GPU driver version and shader source hash.
- A cache blob contains a versioned header, the full 256-bit identity and the compiled payload digest.
- The filename uses a short truncated key for the bounded filesystem path, while the full key in the header prevents collisions from silently aliasing cache entries.
- Corrupt entries are removed rather than returned to a renderer.
- Each cache object is currently limited to 256 KiB; cache eviction, indexing, quota and multi-writer synchronization remain future work.

### Tests

New host tests exercise capability classification (including actual CPUID-backed Steam CPU prerequisite reporting), Direct3D mapping, anti-cheat blocking, hardware-specific verification, shader-cache identity/round-trip/corruption, game-library persistence/checksum handling, test evidence and safe launch restrictions. The full kernel workflow compiles the feature mask and game manager into the BIOS/UEFI kernel.

## 3. Why Steam and Proton are not a simple installer package

The Steam Linux client is a Linux userspace application. Valve's published requirements include a 64-bit Linux kernel environment, both 64-bit and 32-bit graphics/runtime libraries, and both 64-bit and 32-bit glibc. Current Steam client guidance also calls out session services and graphics/audio integration such as D-Bus, X11/Xwayland, PulseAudio/PipeWire-Pulse and OpenGL/Vulkan drivers. A web link to the Steam store is not the Steam client.

Proton is not a Windows kernel emulator. It combines a modified Wine build with graphics API implementations and the Linux userspace environment that they expect. Proton relies on Linux process, memory, file, threading, synchronization, device and graphics interfaces. Adding a PE loader or copying Wine DLLs alone cannot satisfy those contracts.

### Required Linux userspace/ABI milestones

1. General ELF dynamic loader: `PT_INTERP`, shared-object loading, symbol resolution, relocations, TLS, loader search paths, RPATH/RUNPATH and ABI versioning.
2. Stable process model: independent address spaces, preemptive process scheduling, process groups, child/wait semantics, exit signals, file descriptors and executable mapping.
3. User threading: thread creation/exit, robust futex operations, TLS, atomics, condition variables, priority inheritance where necessary, signals and safe thread cancellation.
4. Required Linux syscall families: `mmap`/shared mappings, `mprotect`, `munmap`, `futex`, `clone`/thread creation, `set_tid_address`, `clock_gettime`, `nanosleep`, `poll`/epoll, eventfd, timerfd, pipes, sockets, ioctl, getrandom and process identity/limits.
5. Linux/POSIX interfaces: procfs/sysfs/devfs semantics, mount namespaces and file locking, sockets and DNS, D-Bus, locale, shared-memory and IPC.
6. 32-bit x86 compatibility for required runtime libraries and graphics stack, not just the 64-bit kernel.
7. Audio session stack, device enumeration, mixer/volume, low-latency playback, microphone capture and headset hotplug.
8. Window-system interface for fullscreen/borderless games, focus, input capture, cursor, multiple monitors and display mode switches.
9. A maintained Steam Runtime compatibility environment. A container/pressure-vessel approach is a later milestone after the kernel namespace, mount, security and runtime dependencies are real.

These components must be implemented and tested incrementally. Do not shim hundreds of Linux calls with success-returning stubs; that can cause data loss, deadlocks, security failures and silent game corruption.

## 4. Wine, Proton and Direct3D translation

### Wine
Wine maps Windows userspace APIs onto host OS services. It therefore needs a sound POSIX process/thread, file, registry, synchronization, windowing, audio, network and dynamic linking foundation. A working PE parser is only one small component.

### DXVK
DXVK implements Direct3D 8/9/10/11 on top of Vulkan. It is a userspace compatibility layer and needs a genuinely functional Vulkan implementation, memory objects, synchronization, swapchains and presentation. Requirements vary by DXVK release; the current DXVK 3.x support guidance documents Vulkan 1.4 and newer Wine requirements. Nexus must evaluate the exact bundled DXVK release and driver feature set rather than assume one static minimum.

### VKD3D-Proton
VKD3D-Proton implements Direct3D 12 on top of Vulkan. Its published requirements include Vulkan 1.3 and advanced features such as descriptor indexing, robustness and push descriptors. Merely exposing a Vulkan loader without an actual GPU driver and these features is insufficient.

### OpenGL/Vulkan
OpenGL and Vulkan require mature user-mode drivers as well as a kernel-mode driver interface for GPU memory, command submission, synchronization, reset and display. Mesa RADV, for example, relies on the Linux AMDGPU kernel driver and its DRM userspace API; RADV alone cannot provide hardware acceleration on Nexus. The separate graphics roadmap covers VirtIO-GPU 3D/virgl first, followed by a native GPU ABI and physical Intel/AMD/NVIDIA driver work.

## 5. Anti-cheat, DRM and multiplayer policy

Compatibility is per game and per publisher configuration. Do not promise that Easy Anti-Cheat or BattlEye will work for every game simply because Proton can support some configurations.

Valve's Steamworks documentation says that Easy Anti-Cheat and BattlEye can be supported in Proton, but each title needs provider/developer configuration and enablement. Valve also states that kernel-space anti-cheat solutions are not currently supported in Proton and recommends user-space anti-cheat. Kernel-level anti-cheat drivers are therefore a hard compatibility blocker for this early Nexus gaming stack. A game that blocks a Linux/Proton environment remains unsupported until the publisher and anti-cheat provider explicitly enable a supported path.

Nexus must not bypass, disable, spoof or tamper with anti-cheat protections. The compatibility UI should say why a game is blocked and direct users to the publisher/provider's documented support path.

## 6. Game input, audio and peripherals

### Existing
- USB HID boot keyboard and mouse.
- USB HID gamepad report parsing with normalized axes, buttons and hat state.
- A bounded gamepad event queue.
- Direct AC'97 stereo signed-16 PCM playback when hardware is present.
- Keyboard/mouse input queue and desktop framebuffer blit/poll interfaces for native applications.

### Next
- Unified input device IDs, connect/disconnect events and remapping profiles.
- Generic HID controllers and standardized mappings, plus XInput/SDL-compatible userspace API.
- Rumble/haptics, trigger effects, gyro and battery state where hardware exposes them.
- USB Audio Class and HDA drivers, microphone capture, headset output/input switching, sample-rate negotiation, mixer and low-latency audio service.
- Gaming keyboard/mouse vendor features only through documented interfaces; basic HID operation remains the fallback.
- Controller focus/navigation APIs for launchers and couch-style UI.

## 7. Performance and game-mode scheduler

Gaming mode should be a policy applied to real process scheduling and memory-management controls, not a magic "performance" switch.

Required prerequisites:
- Preemptive user-process scheduling and a stable thread model.
- Per-process CPU usage, priorities, affinity and fair-share controls.
- Foreground-game latency budget, background download/update throttling and audio-thread priority.
- Memory accounting, working-set controls, reclaim/eviction, file cache, GPU memory accounting and out-of-memory policy.
- I/O queue priorities, asynchronous file I/O, shader compilation scheduling and asset streaming.
- Vsync/VRR-aware presentation scheduling and reduced compositor overhead in exclusive/borderless fullscreen.
- Thermal/power/frequency management exposed through safe firmware/ACPI/CPU interfaces.

Nexus currently has a small cooperative kernel-thread scheduler and synchronous native process execution; it does not yet have a general preemptive user-process/thread scheduler. Scheduler policy work must wait for the underlying isolation and scheduling invariants.

## 8. Fullscreen, multiple monitors and performance profiles

The display path needs:
- Exclusive/borderless fullscreen transitions with safe mode restore.
- Multiple active outputs, mode enumeration, EDID refresh rates, hotplug and per-output scale.
- High-refresh modes and vblank/page-flip completion.
- Frame pacing/latency measurement, VRR/adaptive-sync where the exact device/connector path supports it.
- HDR/10-bit color metadata only when all GPU, display-engine, connector and monitor requirements exist.
- Window focus, input capture and cursor confinement with predictable alt-tab behavior.

Do not treat high resolution reported by a framebuffer or multiple VirtIO scanout descriptors as proof that high refresh, native modesetting or a multi-monitor desktop works.

## 9. Compatibility database and test farm

A compatibility entry must record:
- Steam AppID or stable native game ID, title and publisher.
- Game depot/build ID, architecture and graphics API.
- Runtime package, Proton/Wine version, DXVK/VKD3D version and launch flags.
- GPU PCI vendor/device/subsystem IDs, driver version, firmware, Vulkan/OpenGL versions and relevant feature bits.
- Audio device/backend, controller class and display mode.
- Anti-cheat provider, exact publisher enablement status and multiplayer scope.
- Test environment/CI run ID, test date, launch/result, crash log, known defects and benchmark data.

Use these user-facing states:
- **Verified:** exact game build/runtime/driver/hardware passed the defined test suite.
- **Playable with limitations:** the game runs but specific features or stability/performance remain limited.
- **Untested:** there is insufficient evidence.
- **Unsupported:** a confirmed technical incompatibility exists.
- **Blocked by anti-cheat:** the provider/publisher configuration does not support this environment or requires an unsupported kernel component.
- **Retest required:** hardware, driver, runtime or game build changed since the passing test.

Testing must not claim a whole Steam catalog is compatible after one native demo or QEMU boot test.

### Test tiers

1. Unit tests for ELF loader, syscalls, threading primitives, file semantics, graphics, input and audio.
2. Runtime smoke tests for Steam client startup/login/library install/launch/update/shutdown.
3. Proton launch tests for a small licensed game set across D3D9/11 and D3D12, with exact DXVK/VKD3D version recorded.
4. Gamepad/audio/fullscreen/multi-monitor/sleep-wake and alt-tab regressions.
5. Long-running game session, shader compilation, memory pressure, background download and GPU reset tests.
6. Performance benchmarks using same game build, scene, resolution, driver and power profile against the CPU fallback and a reference OS.
7. Anti-cheat tests only with publisher-authorized configurations and the real provider-supported runtime.

## 10. Milestone gates

| Gate | Deliverable | Status |
|---|---|---|
| P0 | Truthful native/Linux/Proton capability model | Implemented in this branch |
| P1 | Persistent game library metadata and test evidence | Implemented in this branch; no GUI page yet |
| P2 | Bounded shader cache identity, integrity and NXFS storage | Implemented in this branch; per-blob size 256 KiB, no eviction |
| P3 | Native Nexus game install/launch/update and controller/audio profiles | Partial: one static ELF launch backend only |
| P4 | General preemptive user-process/thread scheduler and dynamic ELF loader | Not implemented |
| P5 | POSIX/Linux syscall and userspace compatibility layer | Not implemented |
| P6 | Linux multilib runtime, D-Bus, X11/Wayland WSI, sockets, audio server | Not implemented |
| P7 | Vulkan/OpenGL driver and GPU memory/sync/presentation UAPI | Not implemented |
| P8 | Wine runtime and Win32 userspace compatibility | Not implemented |
| P9 | Proton + DXVK + VKD3D-Proton integration | Not implemented |
| P10 | Steam client, library/depot manager, background updates | Not implemented |
| P11 | Fullscreen/high-refresh/multi-monitor/VRR, controller/audio polish | Not implemented |
| P12 | Per-game validation and public compatibility database | Not implemented |

**Recommended next engineering order:** complete the process/scheduler and dynamic-linking foundations; design the Linux ABI compatibility layer; bring up VirtIO-GPU 3D/virgl and a real Vulkan implementation; then build a Wine/Proton experiment in an isolated runtime environment. Steam client integration should begin only after the required runtime service and 32/64-bit graphics stacks are working.

## 11. Upstream technical references

- [Valve Steam for Linux client requirements](https://github.com/ValveSoftware/steam-for-linux#hardware-and-software-requirements)
- [Valve Steam Runtime / beta desktop requirements](https://github.com/ValveSoftware/steam-for-linux/blob/master/README.beta.md)
- [Valve Steamworks: Steam Deck and Proton](https://partner.steamgames.com/doc/steamdeck/proton)
- [Valve Proton source and runtime integration](https://github.com/ValveSoftware/Proton)
- [Wine project: API compatibility layer](https://www.winehq.org/about)
- [DXVK: Direct3D 8/9/10/11 over Vulkan](https://github.com/doitsujin/dxvk)
- [DXVK current driver support matrix](https://github.com/doitsujin/dxvk/wiki/Driver-support)
- [VKD3D-Proton: Direct3D 12 over Vulkan](https://github.com/HansKristian-Work/vkd3d-proton)
- [Mesa RADV architecture and AMDGPU kernel dependency](https://docs.mesa3d.org/drivers/radv.html)

Upstream compatibility versions change. Resolve each dependency to a pinned release, check its license and build requirements, and test against the Nexus kernel/userspace contract before publishing a package.
