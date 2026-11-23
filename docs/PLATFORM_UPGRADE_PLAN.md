# Nexus next-generation platform upgrade

## Goal

Move Nexus from a QEMU-validated kernel desktop prototype toward a general-purpose
desktop operating system with the breadth, reliability, security, application
compatibility and usability expected by Windows users. A larger ISO is not the
goal: the installed system must use the capacity of the selected drive and the
kernel must expose real services that applications can safely use.

This is an engineering roadmap, not a claim that the capabilities below already
exist. Work is complete only when its tests pass and the feature is usable from
the installed system.

## Baseline constraints found in the current source

- NXFS is a fixed-slot filesystem: 64 directory entries, a 1 MiB maximum file
  size, and two fixed data banks per entry. The format reserves a fixed data
  area instead of allocating extents across the selected partition. A large
  SSD therefore does not currently become a large, general-purpose user volume.
- VFS exposes a shallow file-oriented interface. Directory navigation is
  bounded, RAMFS is limited to 16 files of 4 KiB each, and the editor is a
  bounded text editor. This is not yet a complete filesystem namespace.
- The desktop applications are built into the kernel event loop. There is a
  process/ELF/user-mode foundation, but not yet a stable third-party GUI
  application ABI, dynamic linker, shared libraries, service manager or package
  manager.
- Nexus Browser is a bounded text-oriented HTTP client. It has no TLS,
  certificate validation, CSS layout, JavaScript engine, tabs, cookies,
  downloads manager, browser sandbox or modern web compatibility.
- The Wi-Fi work detects candidate adapters and provides a driver bridge, but
  there is no production chipset radio driver or verified WPA2/WPA3 connection
  path. Ethernet support is limited to the early E1000 stack.
- QEMU validation is not physical-PC compatibility certification.

## Upgrade sequence

### 1. Storage that scales with the selected drive

Design and implement NXFS v3 around allocation metadata and variable extents,
not a larger hard-coded array of fixed-size file slots.

Requirements:
- Use checked 64-bit LBA arithmetic and derive usable capacity from the
  partition's actual block count and block size.
- Add allocation bitmap/free-space accounting and multi-extent files so a file
  can grow beyond 1 MiB and unused space is not permanently reserved for empty
  slots.
- Add hierarchical directories, stable file identifiers, timestamps, file
  attributes, and bounded path resolution.
- Keep metadata checksummed and journaled. Recovery must be idempotent after
  interruption at every metadata/data write boundary.
- Preserve read-only access to existing NXFS v2 volumes or provide a tested,
  explicit migration path. Never silently reformat an existing volume.
- Add free/used/total capacity reporting and large-file tests.
- Keep the installer partition plan capacity-aware. Never size a data volume
  using a small fixed constant when more target-disk space is available.
- Test 512-byte and 4096-byte logical sectors, tiny disks, multi-terabyte
  capacity arithmetic, full volumes, fragmentation, bad metadata, and
  power-loss recovery in QEMU.

### 2. A real application platform

- Move user-facing applications out of the kernel desktop loop.
- Stabilize the syscall ABI and process lifecycle; add signals/notifications,
  robust process termination, memory limits, file descriptors, pipes and IPC.
- Add a dynamic linker, shared-library format and versioned application ABI.
- Add a service manager, startup dependencies, per-user sessions, permissions
  and a system configuration service.
- Ship an SDK, reproducible app build tools, signed packages, dependency
  resolution, install/update/uninstall and a first-party app catalog.
- Keep the kernel small: files, network, audio, graphics and device access
  should be mediated by explicit kernel interfaces and user-space services.

### 3. Modern desktop shell

- Build a compositor with hardware-accelerated graphics where supported and a
  reliable software-rendering fallback.
- Add scalable display modes, multi-monitor layout, DPI scaling, Unicode text
  shaping, font fallback, clipboard, drag-and-drop, accessibility APIs,
  notifications, sound, power/session controls and robust input handling.
- Deliver a coherent file manager with nested folders, multi-select,
  properties, previews, search/indexing, trash/restore, removable-media
  handling and safe copy/move progress.
- Add complete settings, task management, system update, backup and recovery
  applications. Each control must invoke a real service and report failure.

### 4. Networking and browser

- Complete a generic network-device/driver model and support a documented set
  of Ethernet and Wi-Fi chipsets with firmware loading, DHCP, DNS, IPv6,
  WPA2/WPA3, roaming and reconnect tests.
- Add a maintained TLS implementation with hostname verification, trusted
  root updates, certificate error UI, secure random generation and safe
  protocol defaults before enabling HTTPS.
- For modern web compatibility, integrate a maintained browser engine such as
  WebKit or Chromium rather than extending the current text parser into a
  home-grown HTML/CSS/JavaScript implementation.
- Isolate browser renderer processes, enforce origin boundaries, sandbox
  downloads and extensions, and provide tabs, history, bookmarks, profiles,
  cookies controls and a real download manager.
- Do not claim modern browser compatibility until the Web Platform Tests and
  security/update process are established.

### 5. Security, updates and release quality

- Add user accounts, authentication, privilege separation, capability-based
  device access, secure boot/signing policy, secret storage and audit logs.
- Add atomic system updates, rollback slots, verified recovery media and
  offline recovery procedures.
- Test installer failure injection, disk-full behavior, interrupted updates,
  filesystem corruption, memory pressure, device removal and repeated cold
  boots.
- Maintain a hardware compatibility list backed by tests on physical PCs.
- Publish signed release artifacts, SBOM, reproducible-build evidence,
  checksums, release notes and known limitations.

## Release gates

A feature is not marked complete because it compiles or appears in a mock UI.
It must have:
1. a documented user-visible behavior;
2. unit and integration tests for success and failure paths;
3. an installed-system test, not only a host-side test;
4. persistence/reboot coverage where data is involved;
5. security and destructive-operation review where relevant; and
6. physical hardware evidence for hardware compatibility claims.

## Immediate next implementation milestone

The first code milestone is now a host-tested, read-only NXFS v3 layout planner
in `kernel/src/nxfs_v3_layout.c`. It computes a partition-relative journal,
allocation bitmap, inode table, and data region with checked 64-bit arithmetic,
and has tests for 512-byte and 4096-byte sectors and 4 TiB volumes. It is not
yet a formatter, allocator, mount driver, or migration path. Next: specify the
v3 on-disk structures, build a v2 compatibility fixture, implement metadata
transactions and crash recovery, and only then connect v3 to VFS and the
installer. The existing installer already assigns all remaining aligned usable
space to the data partition; its storage filesystem, not the partition planner,
is the immediate capacity limitation.
