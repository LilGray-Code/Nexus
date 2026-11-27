# Nexus platform services and driver architecture

## Current implementation

The kernel separates bounded device matching from boot-service ordering. The
driver manager is a kernel-resident PnP/control registry that records every
stored PCI function after the storage service initializes it. It retains
unsupported hardware instead of silently dropping devices with no driver.

### Driver manager

The API in `kernel/src/driver_manager.h` provides:

- **Hardware matching:** bus, vendor/device IDs, subsystem vendor/device IDs,
  revision inventory, class, subclass and programming interface. A zero
  subsystem ID in a driver match is a wildcard; nonzero IDs are exact matches.
- **Priority and fallback:** candidates are probed from highest priority down.
  A failed candidate is recorded and the next matching driver may try.
- **Dependency gates:** descriptors declare up to eight named dependencies.
  Dependencies must already be registered, which makes the registration graph
  acyclic. A dependent driver cannot load until all dependencies are loaded.
- **Lifecycle controls:** `register`, `install`, `load`, `unload` and
  `update` operate on bounded, in-kernel descriptors. Install registers an
  in-tree driver and retries retained device records. Update requires a newer
  version and no active bindings.
- **Safe refusal:** unload refuses while a driver has active bindings or a
  loaded driver depends on it. Device removal first checks that every active
  binding has a teardown callback; otherwise it refuses without changing the
  active PnP record. With callbacks present, it invokes teardown once and marks
  binding history removed.
- **Failure visibility:** device and binding records retain probe failures,
  dependency blocks, disabled-driver state and table-capacity errors. The
  `drivers`, `driverlist` and `drivererrors` diagnostics expose status.

Capacity is fixed at 32 drivers, 128 binding-history records and 256 PnP device
records. PCI itself retains at most 128 functions. Capacity is intentionally
bounded for the current kernel prototype.

AHCI and NVMe are registered as real storage-driver descriptors and invoked
through this manager. The storage stage now passes the entire retained PCI
inventory through PnP discovery, so unsupported display, network, USB and
other PCI functions remain visible. Descriptors for E1000, RTL8139, VirtIO network, xHCI, AC'97 and VirtIO
GPU are also registered. Their existing service-specific initialization
routines still perform hardware setup; the manager's probe callbacks verify
the active PCI controller identity and readiness rather than initializing the
same DMA engine a second time. Controllers that become ready later in boot
are retried by a post-service recovery rescan. Intel/AMD/NVIDIA GPU inventory,
Wi-Fi candidate inventory and unintegrated USB interfaces remain inventory
or partial-support entries, not active driver bindings.

The manager's `remove` callback contract is available, but current PCI
surprise-removal notifications and all xHCI USB hot-unplug events are not yet
connected to it. Removal of an active binding without a teardown callback is
refused without changing its PnP state. Existing hardware drivers without a
quiesce/remove callback are not safe to unload while their DMA engines or
interrupts may still be active. The manager rejects unload of active bindings;
it does not invent a driver teardown path.

Driver descriptors currently point to statically linked kernel functions.
`install()` is not an arbitrary executable/module loader. There is no
signed on-disk driver package installer, kernel module relocation ABI, driver
sandbox, automatic persistent update or recovery rollback. The manager is
also not currently SMP-locked or reentrant; lifecycle calls are expected from
serialized kernel/PnP control flow.

The hardware compatibility database is maintained in
[`docs/hardware/DRIVER-COMPATIBILITY.md`](../hardware/DRIVER-COMPATIBILITY.md)
with a companion
[`driver-compatibility.csv`](../hardware/driver-compatibility.csv). It
distinguishes an operational I/O path from partial support and mere inventory.
Compatibility claims remain scoped to the listed IDs and operations.

### Service supervisor

Services register dependencies and start/stop callbacks through the
`nexus_service_manager` API. The supervisor starts prerequisites first,
rejects missing dependencies and cycles, records failed or blocked starts,
and prevents a running prerequisite from stopping while one of its direct
dependents remains active.

The boot path uses this supervisor for PCI discovery, graphics discovery,
network devices and stack, storage, timer, input, audio, desktop, and native
process-runtime initialization. Callbacks are still kernel functions; the
supervisor is a lifecycle/control plane, not yet a collection of isolated
ring-3 daemons. Moving services into user mode requires robust IPC, handles,
process scheduling, permissions and restart/recovery semantics first.

### Installed system pack

The installer medium and installed EFI System Partition carry
`NEXUS/SYSTEM.NXP`, a bounded `NXSPACK1` archive of immutable system files.
The kernel rejects malformed headers, invalid names, duplicate entries,
overlapping extents, unexpected flags, and mismatched table, payload, or
per-file CRC-32 values before publishing any entry. The VFS exposes verified
entries as read-only root files; they can be opened and run through the
existing static ELF process launcher.

The ISO build generates the pack from the native SDK examples plus
`NEXUS-README.TXT` and `NEXUS-APPS.TXT`. The installer writes this exact pack to
the EFI System Partition and records it as a Limine module in the installed
boot configuration. The 2 MiB bound keeps boot-time validation and installer
deployment bounded. This makes the demo apps available after installation,
but it is not yet a package manager, writable system volume, dynamic loader,
or complete userland.

## Remaining work across the platform goals

| Area | Current state | Required next engineering |
|---|---|---|
| Driver system | Bounded PCI PnP registry; AHCI/NVMe, E1000, RTL8139, VirtIO network, xHCI, AC'97 and VirtIO GPU descriptors; ID matching, dependency gates, probe fallback, history and lifecycle APIs | Add teardown callbacks and bus notifications; complete USB interface PnP; implement signed loadable modules, firmware delivery, persistent package updates and rollback |
| System services | Dependency supervisor used by boot | Move service hosts to isolated processes after IPC and preemptive scheduling; add service permissions, logs, health checks, watchdogs and restart policy |
| Native app runtime | Static ELF64, versioned syscalls, foreground launch | Multiple concurrent processes, full process lifecycle, dynamic linker, shared libraries, libc/POSIX layer, IPC and application sandboxing |
| Graphics | Framebuffer software renderer and VirtIO 2D display, optional Virgl 3D submission | Native Intel/AMD/NVIDIA drivers, GPU memory management, synchronization, shader compiler and a documented 3D API |
| Desktop | Built-in shell and app views | Independent overlapping windows, compositor, window protocol, input focus, accessibility, display settings and isolated user applications |
| Browser, network, Store | IPv4/DHCP/DNS/TCP and TLS text browser; signed package format exists | Full HTML/CSS/JavaScript engine, downloads/media, Wi-Fi chipset drivers, production Ed25519 provider/key and Store-to-package installation flow |
| Gaming | Gamepad polling and bounded AC'97 output; native 2D app ABI | Concurrent app runtime, low-latency audio/mixer, complete 3D graphics API, controller mappings, Linux ABI/dynamic loader and/or Win32/PE compatibility |
| Distributable ISO | Hybrid BIOS/UEFI installer includes the kernel, boot payloads and a checksummed read-only system pack | Package the finished userland, services, drivers, broader applications, trust roots and driver recovery/update metadata, then verify deployment on installed systems |

## Steam compatibility gate

The Store's Steam entry is currently a website shortcut, not a Steam client.
A real Steam implementation is blocked by the absent Linux userspace ABI,
dynamic loader, shared libraries, complete 3D graphics stack, full audio
subsystem, and Proton/Win32 compatibility runtime. A bootable ISO or a larger
kernel cannot substitute for those subsystems. Do not advertise Windows
executables, Linux Steam games, DirectX, Vulkan, or general OpenGL
compatibility until their respective conformance suites pass.

## Validation

The platform-foundations workflow host-tests the driver registry and service
supervisor with warnings treated as errors. Host tests validate matching,
dependency and lifecycle bookkeeping; they do not prove that a physical device
can be driven. The existing BIOS/UEFI QEMU workflow remains the integration
gate for the full kernel. Physical-hardware compatibility must be recorded
separately in the hardware compatibility database.
