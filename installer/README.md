# Nexus Installer

**Status:** The x86-64 kernel boots from a dual-mode ISO and from a
separately installed disposable GPT disk under both OVMF (UEFI) and SeaBIOS
(legacy BIOS), as verified in CI. The installer has guarded GPT, FAT32, NXFS,
Limine payload, and BIOS boot-stage storage adapters. The desktop drive chooser
is still a non-destructive preview; it does not yet execute the install flow.
The root filesystem/userland image, persistent journal placement and recovery
workflow, physical-drive installation matrix, and production hardware support
remain incomplete.

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

- A pure, host-testable partition planner exists in
  `installer/partition_plan.c`. It plans GPT partition extents for UEFI
  (512 MiB ESP) and legacy BIOS (2 MiB BIOS Boot partition), followed by an
  8 GiB Nexus System partition and an aligned remainder data partition.
  It supports 512-byte and 4096-byte logical sectors, checks arithmetic and
  bounds, and performs no disk I/O.
- `installer/gpt_image.c` serializes a protective MBR, primary and backup GPT
  headers, mirrored entry arrays, partition type GUIDs, and CRC32 checksums.
  It performs no disk I/O. CI tests both 512-byte and 4Kn metadata and rejects
  duplicate GUIDs and invalid layouts.
- `installer/gpt_install.c` provides a guarded metadata-install transaction.
  It requires two separate confirmations and flags proving target identity was
  revalidated, the target is not live media, and it is not mounted. It writes
  backup entries/header first, then primary entries/header, and the protective
  MBR last; every phase is flushed and read back. Mock-disk CI tests verify
  write order, read-back, rejection without confirmation, unsafe-target
  rejection, and injected I/O failures.
- `installer/installer_core.c` implements the installer authorization state
  machine: target selection, pure partition planning, exact stable-ID
  confirmation, a second typed `ERASE` confirmation, immediate target
  revalidation, apply eligibility, cancellation, and a recovery-required state
  after a changed target or failed apply. Host CI tests all state transitions.
- The kernel desktop now has an **Install F5** screen that displays the selected
  block device, capacity, sector size, and a UEFI or BIOS partition preview.
  It is explicitly read-only and does not invoke the GPT install transaction,
  FAT32 formatter, or the installer state machine.
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
- `installer/fat32_file.c` writes bounded nested FAT32 paths, allocates and
  initializes one-cluster directories (including `.` and `..`), mirrors FAT
  chains, verifies file sectors and directory entries, and emits the VFAT long
  filename entry for Limine's required `limine.conf` name. Directory expansion
  and general Unicode/long-name support are not implemented; full directories
  fail closed. Host tests cover nested `EFI/BOOT` deployment and a >2 TiB LBA.
- `installer/fat32_format.c` formats an existing partition as FAT32 with
  primary/backup boot sectors and FSInfo, two allocation tables, initialized
  reserved entries, and an empty root cluster. It uses bounded writes, flushes,
  and read-back verification. Host tests cover a sparse 8 GiB volume, invalid
  geometry, and injected I/O failure.
- `kernel/src/installer_storage.c` adapts GPT writing and FAT32 formatting to
  the registered kernel block-device API. Both paths revalidate stable device
  identity and obtain a kernel-owned destructive-write session. FAT32 formatting
  additionally requires an exact GPT EFI System Partition extent; arbitrary
  LBAs and non-ESP partitions are rejected. The adapter is linked into the
  kernel but is not called by the desktop.
- The kernel requests Limine's standard Executable File response. It resolves
  boot provenance by matching a unique GPT partition GUID, GPT disk GUID, or
  MBR disk ID against registered devices after validating on-disk metadata.
  Optical and TFTP boots are accepted as non-disk sources only when no device
  of the corresponding transport is registered. Missing or ambiguous identity
  remains fail-closed. The low-level setters are private to `blockdev.c`.
- CI now verifies that the executable-file handoff identifies the live optical
  source in both OVMF UEFI and SeaBIOS boots, including the distributable ISO.
  The GPT partition resolver is host-tested against validated mock GPT metadata
  and rejects duplicate matches. Physical GPT/MBR boot-media matching still
  needs testing with actual USB/SATA/NVMe boot media. Never infer identity from
  device order, model, or capacity.
- The distributable NVMe driver blocks ordinary filesystem writes but exposes
  its write/flush callbacks only through the kernel installer authorization
  session. The physical NVMe write path has CI-only disposable-disk coverage;
  physical-drive installation has not been tested. The desktop does not yet
  invoke the guarded GPT or FAT32 adapters.
- The graphical preview is not yet a complete installer. It supports
  keyboard-based target selection and firmware-mode planning, and displays
  boot-source, mounted-state, stable-identity, and write-path eligibility.
  It deliberately does not start destructive operations.
- `installer/installer_journal.c` provides a host-tested, dual-sector
  recovery-record primitive. It alternates journal sectors, stores a sequence,
  phase, target identity, geometry, firmware mode, progress count, and disk
  GUID, and verifies each flushed update by reading it back. A torn update
  leaves the previous valid record recoverable. Tests cover 512-byte and
  4096-byte sectors, corrupt records, write/flush failures, identity changes,
  and monotonic sequence enforcement. This journal is not yet assigned a
  dedicated on-disk partition or wired into the installer transaction.
- Installed-disk UEFI and legacy BIOS boot, plus live ISO UEFI and legacy BIOS
  boot, pass the current QEMU CI workflow. These tests use disposable virtual
  media and do not establish physical hardware compatibility.
- A complete root filesystem/userland image, durable journal partition layout,
  graphical confirmation/progress/recovery flow, and physical hardware install
  validation are still outstanding.

The installer now has a kernel-side transaction orchestrator in
`kernel/src/installer_runtime.c`. It sequences GPT creation and verification,
EFI FAT32 formatting, NXFS system formatting, kernel/manifest deployment,
UEFI boot-file deployment, optional BIOS boot-stage installation, and durable
journal phase updates. Legacy-BIOS installs use the dual partition layout so
Limine's configuration remains available on FAT32. The graphical wizard now
collects the exact selected disk ID and a separate typed `ERASE` confirmation.

The runtime call is deliberately compile-time locked in the normal kernel
build. `NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES` is not defined by the
release Makefile. A host-side runtime test and a dedicated GitHub Actions build
gate have been added, but their results must be checked before the lock is
removed. The runtime does not yet format the separate data partition, and the
system payload is still only the kernel ELF, SYSTEM.MAN, and README.TXT—not a
complete general-purpose userland or root filesystem.

The installer is not release-ready. Do not enable the compile-time write gate
until the new host transaction tests, the full freestanding kernel build, and
the QEMU install/reboot/recovery matrix all pass. Physical NVMe installation
and recovery still require validation; AHCI/SATA and USB mass-storage writes
are not implemented. Do not treat the tested individual storage adapters or
journal primitive as proof of a finished end-to-end installer.
