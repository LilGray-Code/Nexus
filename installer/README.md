# Nexus Installer

**Status:** An installable x86-64 prototype ISO has been built and passed
the full GitHub Actions validation run
[44969951849](https://github.com/LilGray-Code/Nexus/actions/runs/44969951849).
CI drove the graphical installer with a keyboard, installed to a disposable
16 GiB NVMe target, booted that installed disk under OVMF (UEFI) and SeaBIOS
(legacy BIOS), and boot-checked the final install-enabled ISO. The ISO SHA-256
is `cf7c5f0af18ac43024b33db4b44cab50d55742ae04a9551a47fea6c94481e622`.

This is a QEMU-validated installable prototype, not a production hardware
certification or a complete general-purpose OS. The current installer writes
to NVMe targets only; AHCI/SATA and USB mass-storage target writes are not
implemented. Physical hardware installation and recovery still require
validation.

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

## Disk and boot layout (implemented v1)

The following GPT layout is implemented and exercised by the disposable-disk
installer and boot tests.

### UEFI

- GPT partition table.
- EFI System Partition: FAT32, with the Nexus Limine UEFI loader and Nexus boot
  configuration.
- Nexus System partition: dedicated Nexus filesystem (NXFS) for the kernel and
  installed system payload.
- Nexus Recovery partition: a dedicated reserved partition for redundant,
  checksummed installer transaction records. It is not mounted as a normal
  filesystem and must remain separate from system/data files.
- Nexus Data partition: NXFS for user data and settings, separate from system
  replacement to support upgrades and recovery.

### Legacy BIOS

- GPT with a BIOS Boot Partition for the Limine BIOS stage.
- Nexus System partition (NXFS), dedicated Nexus Recovery partition for the
  durable install journal, and separate Nexus Data partition.
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

- **Graphical wizard:** The live desktop opens the installer with F5. It
  enumerates eligible NVMe devices, displays stable identity/capacity/sector
  size, previews UEFI or dual UEFI+BIOS partition layouts, asks for the exact
  stable disk ID, and requires a separate typed `ERASE` confirmation. The
  keyboard-driven flow is exercised in QEMU. No disk is selected or erased
  automatically.
- **Kernel installer runtime:** `kernel/src/installer_runtime.c` orchestrates
  target revalidation, GPT creation/read-back, EFI FAT32 formatting, NXFS
  system and data formatting, system/boot payload deployment, optional BIOS
  boot-stage installation, and durable journal updates.
- **Recovery:** The journal stores target identity, geometry, firmware mode,
  disk GUID, sequence, phase, and completed-step count in alternating sectors.
  A confirmed retry validates the journal and partition layout, then resumes
  from the last committed phase without rewriting GPT. Corrupt or inconsistent
  records fail closed.
- **Storage:** GPT metadata, FAT32, NXFS, boot-payload deployment, and BIOS
  installation use the kernel block-device API. The system partition receives
  the kernel ELF, SYSTEM.MAN, and README.TXT. The separate NXFS data partition
  is formatted and mounted by the desktop file-browser scan.
- **Boot support:** The same dual-layout installation boots under OVMF/UEFI and
  SeaBIOS/legacy BIOS in CI. The final ISO itself also passes UEFI and BIOS live
  boot checks. Secure Boot is not supported.
- **CI evidence:** Run
  [44969951849](https://github.com/LilGray-Code/Nexus/actions/runs/44969951849)
  passed the host recovery and partition tests, storage-adapter tests, both
  write-locked and installer-enabled kernel builds, disposable-disk runtime
  install, graphical QEMU install, installed-target UEFI and BIOS boots, and
  final ISO boot check. The uploaded artifact is named
  `NexusOS-OriginalKernel-BIOS-UEFI`.
- **Release write gate:** The ordinary kernel Makefile remains write-locked.
  The CI distributable ISO explicitly defines
  `NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES` after the complete QEMU
  validation matrix passes. The installer still requires an explicit disk ID
  and typed `ERASE` confirmation.
- **Remaining limitations:** Only NVMe target writes are implemented. No
  physical hardware install has been completed. The installed system payload
  is not a complete userland/root filesystem, and ordinary persistent user
  file writes remain read-only (user-facing writes can use the volatile RAM
  overlay). The file-management UI, physical NVMe recovery tests, SATA/USB
  storage, broader device drivers, upgrades, and production hardening remain
  unfinished.

The artifact is suitable for testing the install flow in a virtual machine.
Before installing on physical hardware, back up the selected disk and treat
the ISO as an early prototype; QEMU success does not establish compatibility
with every NVMe controller, firmware, or motherboard.
