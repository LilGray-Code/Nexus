# Nexus platform services and driver architecture

## Current implementation

The kernel now has two bounded registries intended to separate device matching
and boot-service ordering from the monolithic boot routine.

### Driver registry

The driver API provides nexus_driver_manager_register(),
nexus_driver_manager_probe_device(), and
nexus_driver_manager_probe_pci_device(). A driver descriptor supplies a bus,
vendor/device and class match, a priority, and a probe callback. Matching
drivers are attempted from highest to lowest priority. A failed probe is
recorded and the next matching driver is tried. Successful and failed probe
records retain the matched identity and state. The generic device descriptor
models PCI, platform, and USB buses, although only the PCI adapter is currently
enumerated.

AHCI and NVMe are the first migrated drivers. Their real controller-probe
functions are invoked through registered descriptors; this is not merely a
hardware inventory table. E1000, RTL8139, VirtIO network/display, xHCI, and
other drivers still use their existing initialization paths and should be
migrated one at a time.

This registry does not yet load external driver binaries, relocate modules,
provide a driver sandbox, or implement complete hot-unplug cleanup. The kernel
remains statically linked.

### Service supervisor

Services register dependencies and start/stop callbacks through the
nexus_service_manager API. The supervisor starts prerequisites first, rejects
missing dependencies and cycles, records failed or blocked starts, and
prevents a running prerequisite from stopping while one of its direct
dependents remains active.

The boot path now uses this supervisor for PCI discovery, graphics discovery,
network devices and stack, storage, timer, input, audio, desktop, and native
process-runtime initialization. Callbacks are still kernel functions; the
supervisor is a lifecycle/control plane, not yet a collection of isolated
ring-3 daemons. Moving services into user mode requires robust IPC, handles,
process scheduling, and restart/recovery semantics first.

## Remaining work across the eight platform goals

| Area | Current state | Required next engineering |
|---|---|---|
| Driver system | Registry and AHCI/NVMe dispatch implemented | Migrate remaining PCI drivers; add USB enumeration, firmware loading, device teardown, signed loadable modules, and power/hotplug lifecycle |
| System services | Dependency supervisor used by boot | Move service hosts to isolated processes after IPC and preemptive scheduling; add service permissions, logs, health checks, and restart policy |
| Native app runtime | Static ELF64, versioned syscalls, foreground launch | Multiple concurrent processes, full process lifecycle, dynamic linker, shared libraries, libc/POSIX layer, IPC and application sandboxing |
| Graphics | Framebuffer software renderer and VirtIO 2D scanout | Real GPU command submission, memory management, synchronization, shader compiler, and a documented 3D API; current VirtIO path still uses CPU rasterization |
| Desktop | Built-in shell and app views | Independent overlapping windows, compositor, window protocol, input focus, accessibility, display settings, and isolated user applications |
| Browser, network, Store | IPv4/DHCP/DNS/TCP and TLS text browser; signed package format exists | Full HTML/CSS/JavaScript engine, downloads/media, Wi-Fi chipset drivers, production Ed25519 provider/key, and Store-to-package installation flow |
| Gaming | Gamepad polling and bounded AC'97 output; native 2D app ABI | Concurrent app runtime, low-latency audio/mixer, 3D graphics API, controller mappings, Linux ABI/dynamic loader and/or Win32/PE compatibility |
| Distributable ISO | Hybrid BIOS/UEFI installer image includes the kernel and boot payloads | Package the finished userland, services, drivers, assets, applications, trust roots and recovery/update metadata, then verify deployment on installed systems |

## Steam compatibility gate

The Store's Steam entry is currently a website shortcut, not a Steam client.
A real Steam implementation is blocked by the absent Linux userspace ABI,
dynamic loader, shared libraries, 3D graphics stack, full audio subsystem,
and Proton/Win32 compatibility runtime. A bootable ISO or a larger kernel
cannot substitute for those subsystems. Do not advertise Windows executables,
Linux Steam games, DirectX, Vulkan, or OpenGL compatibility until their
respective conformance suites pass.

## Validation

The new host workflow compiles and runs the driver-registry and service
supervisor tests with warnings treated as errors. The existing BIOS/UEFI QEMU
workflow remains the integration gate for the full kernel. Passing host tests
alone does not prove physical-hardware compatibility.
