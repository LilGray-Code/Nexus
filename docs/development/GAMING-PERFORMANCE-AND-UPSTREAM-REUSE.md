# Nexus Gaming Performance and Upstream Reuse Plan

**Status:** Frame-pacing and benchmark-telemetry primitives implemented; not yet connected to a real game presentation/runtime path.
**Scope:** Measurement contracts, test methodology and compatibility dependency reuse. This document does not claim Vulkan, OpenGL, Steam, Wine or Proton support.

## 1. Frame pacing and benchmark collector

The kernel now has `gaming_perf.h` / `gaming_perf.c`, a bounded, allocation-free metrics collector and a monotonic frame-deadline helper.

Implemented primitives:

- Absolute monotonic frame deadlines at a requested period. Late frames skip expired deadlines rather than accumulating timer drift. The helper returns a deadline; the caller must sleep/block through the scheduler and must not busy-wait.
- Per-session frame count, minimum/maximum/average frame time, average FPS in milli-FPS, dropped presentation intervals, and approximate p50/p95/p99 frame-time upper bounds.
- CPU busy time aggregated over the game's threads and normalized by online logical processors to produce a total-system CPU utilization estimate.
- GPU busy-union time for a named GPU timeline, measured only when a real driver supplies it.
- Input-event-to-present latency average, maximum and approximate p95 upper bound.
- Current and peak resident memory and GPU memory values when a memory accounting source supplies them.
- Separately measured loading interval and an explicit crashed/normal completion outcome.

Metric source validity is explicit. Missing CPU, GPU, input or memory measurements are reported as unavailable, never as a fabricated zero. Frame and input histograms use 0.5 ms buckets; percentiles are conservative bucket upper bounds, not exact quantiles.

The benchmark collector does not itself read hardware counters, access GPU timestamps, inspect process accounting or intercept input/presentation. It consumes samples from those future subsystems. A run's CPU/GPU/input/memory numbers are only meaningful when the respective source is integrated and has been validated.

## 2. Current limitations and integration gates

The new code is an algorithmic foundation only. It is not yet connected to:

1. A real vblank/page-flip or swapchain present event.
2. A blocking scheduler timer or compositor frame deadline.
3. Per-process/per-thread CPU runtime accounting.
4. GPU fence/timestamp queries and engine-busy counters from a working GPU driver.
5. Input-device event timestamps propagated through the application and compositor to the matching present.
6. A page-backed process working-set and GPU allocation accounting service.
7. A repeatable in-game benchmark harness, game loading lifecycle, or persistent per-run result database.

Until those sources are connected, only unit-test results for the collector and pacer may be reported. Synthetic test data is not real hardware benchmark evidence.

A benchmark should record the exact game build, scene/save, launch/runtime configuration, game platform, resolution, refresh rate, graphics preset, VSync/VRR mode, CPU, GPU PCI IDs, firmware and driver/runtime versions, storage device, test run identifier, and whether the measurement is native, virtualized or emulated.

The first real measurement suite should use a deterministic first-party Nexus game and a repeatable scene, then QEMU/VirtIO 3D only when that path exists, followed by a narrow set of licensed games on specifically supported physical GPU/driver combinations. Compare repeated runs under the same workload and thermal/power state. Capture frame-time percentiles and loading/stability data, not just average FPS.

## 3. Upstream technology reuse strategy

Nexus should build the host OS interfaces and package maintained upstream runtimes instead of reimplementing whole graphics/compatibility stacks.

### Windows compatibility

Use a pinned, verified Proton release as the preferred Steam compatibility tool only after a compatible Linux userspace, process/thread model, ELF dynamic loader, filesystem semantics, sockets, synchronization, 32/64-bit runtime, display/window system and audio stack are available. Proton is a Steam compatibility tool built around Wine and additional components; its source tree includes DXVK and VKD3D-Proton subprojects. Treat the release as a coordinated toolchain with a documented manifest rather than independently mixing arbitrary DLL versions.

Wine is the Windows API implementation and PE execution/runtime layer. A small PE parser or Win32 DLL collection is not an equivalent replacement. Do not start with a home-grown Win32 clone when the goal is Proton application compatibility.

### Graphics translation

- DXVK: reuse its Direct3D 8/9/10/11-to-Vulkan translation libraries for games targeting those APIs.
- VKD3D-Proton: reuse its Direct3D 12-to-Vulkan translation stack. Its feature requirements must be checked against the exact Vulkan driver.
- Vulkan Loader: evaluate the Khronos loader and its ICD/layer manifest design. Keep loader, validation layers and actual hardware ICDs distinct. The loader discovers drivers; it is not a GPU driver.
- Mesa: evaluate a supported Mesa UMD/backend for a selected hardware path. RADV is a user-mode AMD Vulkan driver that relies on the AMDGPU kernel driver and its DRM userspace API; it cannot be dropped into Nexus until those required interfaces are ported or adapted.
- Gamescope: evaluate Valve's micro-compositor after Nexus has the required Wayland/Xwayland clients, Vulkan composition, display modesetting/page-flip and input integration. Its low-copy direct scanout and virtual-resolution concepts are useful, but the Linux DRM/KMS implementation is not directly usable by this custom kernel.

### Steam and Steamworks

Steam client support must follow a working Linux compatibility/runtime environment, not a custom web shortcut. First validate Steam Runtime setup and client startup, then login, downloads, updates, native Linux titles, and only then Proton games.

Steamworks-related libraries are a separate publisher/developer integration. Use official redistributable binaries only under their applicable Steamworks terms and for the documented feature/integration purpose. Do not bundle the entire SDK or distribute proprietary developer tools as general OS libraries. Individual Steamworks features may be optional for game operation; the exact title's requirements must be verified.

### Upstream governance and licensing

Pin exact upstream commits/releases, preserve each component's license and notices, track CVEs/security updates, and generate a per-build software bill of materials. Keep upstream source unmodified where possible and isolate Nexus-specific integration in small platform adapters. Review every subproject's contribution and AI-use policies; Valve's current Proton repository explicitly disallows LLM-generated contributions to Proton and its Wine submodule, so Nexus must not submit generated patches to those projects or paste generated code into them.

## 4. Gaming compatibility test suite

A title must pass the actual execution path it claims. The release gate is not a successful compile or a file-format parse.

Required per-title evidence:

- Native Linux: install and run the actual x86-64 and, where claimed, i386 binary; exercise shared libraries, runtime services, graphics, audio, input, fullscreen, and exit/relaunch.
- Windows through Proton/Wine: install and launch the real PE application through the actual compatibility runtime; exercise dependent DLLs, registry/prefix, process/thread behavior, input, audio and graphics.
- Graphics: run API smoke tests and conformance subsets on the real ICD/driver; exercise shaders, pipelines, resource lifetime, synchronization, swapchain/fullscreen and device reset.
- Steam: client bootstrap/login, content install, verify/update, launch, shutdown, library repair and restart. Use a test account and legally available test title.
- Stability: repeated launch/exit, long session, alt-tab/focus changes, resolution changes, suspend/resume, audio/controller hotplug, memory pressure and crash handling.
- Performance: frame-time p50/p95/p99, average FPS, CPU/GPU utilization, peak process/GPU memory, loading times, input latency, dropped frames and crash/hang rate.

Test anti-cheat only using publisher/provider-authorized configurations. Do not bypass or spoof anti-cheat/DRM. A title requiring an unsupported kernel anti-cheat component remains blocked even if its menu renders.

## 5. Release wording

Until a real game passes the corresponding tests, report:

- **Native Nexus game support:** limited to the tested native Nexus application ABI and games.
- **Linux/Steam games:** not supported.
- **Windows games through Wine/Proton:** not supported.
- **Vulkan/OpenGL hardware acceleration:** not available until a named driver passes conformance/workload tests.
- **Direct3D translation:** not available until DXVK/VKD3D-Proton execute under the supported Vulkan driver.
- **Steam client:** not supported until the client/runtime install-login-update-launch lifecycle is tested.

Never convert a detected GPU, an installed package, an upstream compatibility layer, a Vulkan loader, or a Steam store link into a compatibility claim.
