# Nexus Installer

**Status: QEMU-validated installable engineering prototype.** The live installer
and installed OS are separate bootable kernel images. The dual-mode BIOS/UEFI
build has passed the repository's disposable-disk QEMU install and boot
workflows. Physical-PC installation and recovery remain unverified.

## Product boundary

This is a first-party installer for the Nexus freestanding x86-64 kernel. It
does not launch Debian Installer, Calamares, Subiquity, Anaconda, Linux
utilities, or another distribution's package manager.

The installed runtime currently consists of the Nexus kernel and its integrated
desktop/system features, plus a separate NXFS user Data volume. The System
partition receives `KERNEL.ELF`, `SYSTEM.MAN`, and `README.TXT`. The
desktop, terminal, file browser, settings, and early HTTP text viewer are
currently kernel-integrated; this is not yet a separate full userland or a
production desktop application environment.

## Live installer and installed desktop are separate environments

The release ISO contains two distinct kernel executables. `nexus-installer.elf`
is the Limine boot entry for the USB/live environment. `nexus-os.elf` is loaded
as a separately validated install payload and is deployed to the target's
Nexus System volume and EFI boot files. The build compiles the installed OS
with `NEXUS_INSTALLED_OS_BUILD`; this runtime never enables installer mode or
the destructive installer-write build flag.

Booting the USB/live medium starts directly in the dedicated Nexus Installer.
It is a full-screen interface with no normal desktop taskbar, Start menu, or
application shortcuts. It is not an installer window inside the installed OS.

Booting from an installed Nexus disk starts the normal Nexus desktop
(Home, Files, Browser, Settings, and Terminal). The installer is not listed in
the installed desktop's launcher or taskbar and cannot be opened with a
function-key shortcut. Installer mode is selected only when kernel boot-source
identification positively establishes that the machine was started from an
external live medium; ambiguous boot-source identity must fail closed.

The installer discovers eligible block devices and provides a target
selection and layout preview. A destructive install is blocked until the user
confirms the selected device's stable identity and separately types `ERASE`.
The kernel revalidates the device immediately before each destructive stage.

After successful deployment, the installer displays a **Restart into Nexus OS**
action and a reminder to remove the live USB. Selecting it requests a hardware
reset. Remove the installer USB before triggering the reset; firmware should
then boot the installed target into the ordinary Nexus desktop. The software
does not assume it can control every machine's firmware boot order or
physically eject removable media. The live boot medium, mounted devices, and
devices with ambiguous identity are rejected as installation targets.

**The selected target disk is erased and repartitioned.** Only proceed after
checking the exact model, capacity, and stable device identity, and after
backing up all important data.

## Disk layout

The installer creates a GPT partition table. UEFI installs include:

1. **EFI System Partition** — FAT32, containing the Limine UEFI loader,
   configuration, and required BIOS boot support files.
2. **Nexus System** — NXFS, containing the installed kernel and system
   manifest/readme.
3. **Nexus Recovery** — a separate reserved partition for redundant,
   checksummed installer transaction-journal records.
4. **Nexus Data** — NXFS, used by the installed Files view for persistent
   user files.

Legacy-BIOS and dual-firmware layouts also contain a BIOS Boot partition for
the Limine BIOS stage. The same installed kernel and Data volume are used by
both firmware boot paths. Secure Boot is not supported.

## Storage and deployment

The installer uses the kernel's block-device interfaces. The current virtual
test targets include NVMe and AHCI/SATA. The AHCI path identifies ATA devices
and registers guarded writes when flush support is available. These storage
paths are exercised using disposable QEMU disks; this does not certify every
physical NVMe/SATA controller or firmware implementation.

The installer formats the System and Data NXFS volumes separately, copies the
installed OS kernel and boot payloads, and records each completed transaction
phase in the Recovery partition. The installed OS is a separate ELF built
without `NEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES`; only the live installer
image can receive that build flag. The ordinary kernel build remains
write-locked.

The installed Files view mounts the NXFS Data partition following the
Recovery partition. It does not present the NXFS System volume as the user's
writable file store. FAT32 volumes remain read-only. If no writable Data volume
is mounted, RAM-only file saves are volatile and do not survive a reboot.

## Recovery and test coverage

Host-side tests use disposable mock block devices and inject failures at
filesystem formatting, system-payload deployment, and boot-payload deployment.
They simulate multiple restarts while preserving the disk's GPT and Recovery
journal, then verify that a retry resumes without rewriting the committed GPT.
NXFS tests unmount and remount the Data volume, verify a file, replace it with
edited contents, remount again, and verify the new generation.

The QEMU workflow additionally exercises:

- UEFI and legacy-BIOS live boot;
- installed UEFI and BIOS boot from disposable target disks;
- graphical installation to disposable NVMe and AHCI/SATA disks;
- creation and persistence of user files across a fresh boot on both target
  types;
- a GUI text-editor insertion and save on NVMe, followed by reboot and
  verification that the larger edited file is still present;
- construction and boot-check of the install-enabled dual-mode ISO;
- validation that the ISO carries two separately built kernel executables;
- installed UEFI and BIOS boots that confirm the installed runtime disables
  installer mode.

The simulated power cycles and QEMU reboots are not physical power-cut tests.
No physical-PC installation, controller matrix, or real-world recovery
certification has been completed.

## Current user-file manager

The installed Files view currently exposes a flat root directory with up to
128 indexed entries and six visible entries at a time. Keyboard arrow keys,
page navigation, and mouse selection navigate the view. Users can create,
open, edit, save, and delete supported files. The built-in editor supports
files up to 1 MiB and reads persistent files in bounded 4 KiB VFS chunks.
Current filenames are limited to 31 characters using letters, digits,
underscores, hyphens, and at most one period. The editor is a basic ASCII-text
editor; this is not yet a complete hierarchical desktop file manager.

## Known limits

- No physical hardware compatibility matrix or physical recovery tests.
- No Secure Boot support.
- No full separate userland or mature init/service manager; desktop apps and
  core services remain integrated into the kernel.
- No production-grade updates, rollback workflow, or filesystem repair
  environment beyond the implemented installer transaction recovery path.
- No claim of arbitrary GPU, audio, Wi-Fi, Bluetooth, or motherboard support.
- The browser provides only early plain-HTTP text retrieval; TLS/HTTPS is not
  implemented.

## Installed desktop appearance

The installed desktop is styled around a Windows 10-inspired layout: a bottom
taskbar with Start button and pinned app buttons, a Start menu with app rows and
tiles, a blue accent and title bars, and a blue-toned wallpaper. This is a
Nexus-native design; it is not Windows and does not include Windows binaries.

Treat the ISO as an engineering prototype. Prefer QEMU or another disposable
test machine until physical testing has been completed.
