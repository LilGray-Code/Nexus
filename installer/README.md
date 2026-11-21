# Nexus Installer

**Status:** architecture and safety contract defined; implementation is gated on
writable block I/O, a crash-consistent on-disk filesystem, and a Nexus live
userland. This is not yet an installable or disk-writing program.

## Product requirement

The Nexus installer is a first-party Nexus application running inside the Nexus
live boot environment. It is not a wrapper around another operating system's
installer. It must not invoke Debian Installer, Calamares, Subiquity, Anaconda,
Linux utilities, or a host operating-system package manager.

The installer, target system image, and installed boot configuration are
Nexus-owned. The first release targets x86-64 and supports both UEFI and legacy
BIOS on x86-64 CPUs.

## User experience

The live boot menu will offer **Try Nexus** and **Install Nexus**. Install Nexus
opens a full-screen graphical wizard rendered by the Nexus framebuffer/window
system. The wizard must be usable with keyboard and mouse and expose accessible
focus order, clear error messages, and a persistent progress log.

Planned screens:

1. Welcome, language, keyboard layout, and release/build identity.
2. Hardware summary and firmware mode (UEFI or legacy BIOS).
3. Disk discovery: exact model, capacity, transport, existing partitions,
   filesystem signatures, and mounted/in-use state.
4. Installation target and partition plan preview.
5. Explicit destructive-action review, including the exact disk model and
   capacity. A second, separate confirmation is required before any erase,
   format, or partition-table write.
6. Copy/install progress with phase, byte counts, and durable log.
7. Bootloader installation and firmware-entry setup.
8. Completion, reboot/eject guidance, and recovery instructions.

The default must be non-destructive. No disk is selected automatically, and
the installer must never silently erase, repartition, or format a disk.

## Disk and boot layout (proposed v1)

The layout is a proposal until the GPT writer, filesystem, and boot paths have
passed their independent tests.

### UEFI

- GPT partition table.
- EFI System Partition: FAT32, with the Nexus Limine UEFI loader and Nexus boot
  configuration.
- Nexus System partition: dedicated Nexus filesystem (NXFS) containing the
  kernel, system services, desktop, libraries, and signed/hashed system
  manifests.
- Optional Nexus Data partition: NXFS for user data and settings, separate
  from the system image to support upgrades and recovery.

### Legacy BIOS

- GPT with a BIOS Boot Partition for the Limine BIOS stage.
- Nexus System partition (NXFS), and optional separate Nexus Data partition.
- The installer writes and verifies the BIOS boot stage only after validating
  the target disk identity and partition geometry.

The installer must not claim Secure Boot support until every executable in the
boot chain is signed and verification/key enrollment is tested.

## Installer engine boundaries

Keep UI, policy, and storage operations separate:

- installer-ui: graphical wizard, keyboard/mouse navigation, progress, logs.
- installer-core: state machine, validation, cancellation rules, and
  transaction journal.
- disk-discovery: read-only device enumeration and geometry/capacity data.
- partition: GPT parser/writer, CRC validation, alignment, and backup GPT
  handling.
- filesystem: NXFS formatter, checker, and mount integration.
- deploy: verified system-image copy, manifest/hash verification, and
  configuration generation.
- boot-install: Limine BIOS/UEFI deployment and boot-entry setup.
- recovery: interrupted-install detection, repair, and reinstall/rollback
  workflow.

The engine must consume kernel block-device APIs. It must not shell out to
external distribution tools or depend on Linux device names such as /dev/sda
as its internal identity model. Device identity must include stable controller
and device information, capacity, and a unique runtime handle.

## Safety and correctness invariants

- Discovery and preview are read-only.
- The selected disk is revalidated immediately before every destructive phase.
- The UI names the exact disk model and capacity on the final confirmation.
- No writes occur before two-step explicit confirmation.
- Never target the live boot medium, a mounted/in-use volume, or an ambiguous
  device.
- Check integer overflow, LBA bounds, sector size, alignment, and partition
  overlap before writing.
- GPT primary and backup headers and entry arrays must be CRC-checked; write
  backup metadata before publishing the primary table, then read back and
  verify.
- Use bounded I/O, flush/barrier support where available, and a durable
  transaction journal. On interruption, boot into recovery rather than
  continuing destructive work blindly.
- Verify every deployed file against a signed or hash-pinned manifest.
- Never format an unknown or unsupported filesystem without explicit consent.
- Keep user data separate from system replacement where feasible; upgrades
  must preserve the data partition by default.
- Installer tests use disposable virtual disks. Never run destructive tests
  against a developer's primary disk.

## Implementation gates

| Gate | Required implementation | Exit evidence |
|---|---|---|
| I0: discovery | Stable block-device identity, geometry, read-only sector reads | QEMU disk matrix and physical read-only inventory |
| I1: partition planning | GPT parse, CRC checks, pure in-memory planner | Golden-image tests for valid/corrupt GPT, overflow and overlap rejection |
| I2: safe writes | Block writes, flush, read-back, backup GPT updates | Disposable-disk power-cut/fault-injection tests |
| I3: NXFS | Formatter, mount, checker, crash-consistency tests | Create/mount/read/write/remount and corruption-recovery suite |
| I4: deployment | Versioned system image and verified copy | End-to-end hash equality on disposable disk |
| I5: boot integration | BIOS and UEFI Limine install and firmware entries | Installed-disk boots under SeaBIOS and OVMF |
| I6: graphical wizard | Full wizard, progress, logs, error and cancel handling | Keyboard/mouse automated UI tests and accessibility review |
| I7: release readiness | Recovery, upgrade, rollback, hardware matrix | Independent install/reboot/recovery reports |

No gate may be marked complete from compilation alone. Each exit requires captured test evidence.

## Current implementation status

- A pure, host-testable partition planner now exists in
  `installer/partition_plan.c`. It plans GPT partition extents for UEFI
  (512 MiB ESP) and legacy BIOS (2 MiB BIOS Boot partition), followed by an
  8 GiB Nexus System partition and an aligned remainder data partition.
  It supports 512-byte and 4096-byte logical sectors, checks arithmetic and
  bounds, and performs no disk I/O.
- CI compiles and runs planner tests for UEFI, BIOS, 4Kn alignment, exact
  partition sizes, non-overlap, invalid sector sizes, undersized disks, and
  invalid firmware modes.
- The kernel has GPT primary/backup header and entry-array CRC validation,
  exercised against a disposable QEMU disk image.
- NXFS has an early on-disk formatter/mounter, two checksummed superblocks,
  journal replay, copy-on-write data banks, and file read/write/delete. Its
  current CI self-test formats and tests only a disposable virtual disk. It
  remains a small prototype (16 files, up to 16 KiB each), not a production
  system filesystem.
- NVMe block writes and flush are available to the CI-only self-test build,
  where a reserved sector is written, read back, and restored on a disposable
  QEMU image. The distributable ISO intentionally registers NVMe as read-only;
  normal boots do not write to user disks.
- No GPT writer, general-purpose installer write authorization flow, graphical
  installer wizard, installed-system deployment, or installed first boot is
  currently available.

The partition planner is an implemented and tested foundation, not a usable
installer. Disk-writing remains disabled in the distributable ISO until the
GPT writer, write authorization, crash recovery, and complete install/boot
path have passed their dedicated disposable-disk tests.
