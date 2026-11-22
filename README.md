# Nexus OS — Original Kernel

Nexus is an independent x86-64 operating-system project. The active **Original
Kernel** edition boots a Nexus-written freestanding kernel and its integrated
framebuffer desktop; it is not a Linux, Debian, or FreeBSD distribution.

> **Status: QEMU-validated installable engineering prototype.** The dual-mode
> BIOS/UEFI ISO and graphical installer have been exercised in virtual
> machines. Nexus has not yet been certified on physical PCs and is not a
> production-ready general-purpose desktop OS.

## What is implemented

- A freestanding x86-64 kernel, booted by Limine in UEFI or legacy BIOS mode.
- A framebuffer desktop with Home, Files, Browser, Settings, and Terminal,
  plus keyboard and mouse input. The installed desktop displays an animated
  Nexus startup splash before showing the workspace and does not expose the
  installer as an application or launcher item.
- PS/2 input and xHCI USB HID boot-keyboard / boot-mouse support.
- NVMe and AHCI/SATA block-device support. The AHCI driver identifies ATA disks
  and supports guarded installer writes when the device provides flush support.
- GPT, FAT32, NXFS, and a virtual filesystem layer.
- A separate full-screen live-media installer. Booting the USB opens the
  installer directly; booting the installed disk opens the normal desktop.
  The installer builds a GPT layout with an EFI System Partition, Nexus
  System volume, Recovery journal partition, and separate Nexus Data volume;
  BIOS installs also include a BIOS Boot partition. After successful
  deployment, Enter requests a reboot so the firmware can start the installed
  OS after the USB is removed.
- A Files view with a flat root listing (up to 128 indexed entries, six
  visible at once), keyboard/mouse selection, create/open/edit/delete, names
  up to 31 characters, and a scrollable 4 KiB ASCII text editor. The desktop
  mounts the dedicated NXFS Data partition after the Recovery partition;
  it does not expose the NXFS System partition as the user's file store.
  FAT32 volumes are read-only; RAM-only fallback saves are volatile.
- An early Intel E1000 wired-Ethernet stack with DHCP, DNS, TCP, and a plain
  HTTP text viewer. This is limited network access, not a general-purpose
  internet/browser implementation.

The desktop applications and current system services are integrated into the
kernel. The presence of an ELF loader and ring-3 execution tests does **not**
mean that Nexus currently has a full separate userland, a mature init/service
manager, or ordinary preemptive multitasking.

## Installer safety

The installer requires the user to choose a target disk, confirm its stable
device identity, and separately type `ERASE` before destructive operations.
It revalidates the target and keeps a durable recovery journal. The Recovery
partition is separate from the System and Data volumes. An interrupted install
can be retried against the same target; the transaction tests verify resume
after simulated restarts without rewriting an already-committed GPT.

**Installing erases the selected disk.** Use a disposable disk or virtual
machine while evaluating Nexus. Back up important data and check the displayed
model, capacity, and stable identity before confirming.

## Validation evidence

The repository's GitHub Actions workflows build both the write-locked kernel
and the installer-enabled kernel and exercise:

- live UEFI boot with OVMF and live legacy-BIOS boot with SeaBIOS;
- installed UEFI and BIOS boot;
- graphical installation to disposable NVMe and AHCI/SATA targets;
- persistent NXFS user-file creation and fresh-boot retention on NVMe and
  AHCI/SATA targets;
- a Files-editor keyboard edit followed by save, reboot, and verification of
  the edited file on the NVMe Data volume;
- host-side installer fault injection across simulated power cycles, journal
  recovery, and NXFS unmount/remount/edit/remount persistence;
- dual-mode ISO construction and a boot check of the distributable ISO.

These are virtual-machine and host-test results. They do not demonstrate
physical power-loss behavior, compatibility with every controller or
firmware, or successful installation on arbitrary PCs. Check the
[GitHub Actions workflows](https://github.com/LilGray-Code/Nexus/actions) for
the latest run attached to the current branch.

## Important limitations

- No physical PC installation or hardware-compatibility matrix has been
  completed. Hardware testing remains required.
- Secure Boot is not supported.
- The built-in Browser is the current default web app, but it is only an early
  plain-HTTP text viewer. HTTPS/TLS, modern web rendering, Wi-Fi, and broad
  network-adapter support are not implemented. Google Chrome cannot run on
  the current freestanding kernel: Nexus still lacks the Linux/POSIX runtime,
  dynamic userspace, graphics and system interfaces Chrome depends on.
- The E1000/DHCP stack is exercised with emulated networking in QEMU; actual
  public-internet access on physical hardware has not yet been validated.
- The Files view is currently a flat root listing with bounded filename and
  editor sizes; it is not yet a full hierarchical file manager.
- Desktop applications and much of the system runtime remain kernel-integrated.
  A separate userland, a full application/process model, robust preemptive
  multitasking, updates, and production recovery/upgrade tooling remain work
  in progress.
- Broad GPU acceleration, audio, Wi-Fi, Bluetooth, power management, and
  support for arbitrary third-party hardware are not claimed.

## Build and test

See the [installer documentation](installer/README.md), the
[original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md),
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions).

A successful build or QEMU test is not a substitute for testing on the actual
PC and storage hardware where Nexus will be used.
