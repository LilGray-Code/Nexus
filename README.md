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
- A framebuffer desktop with Home, Files, Browser, Settings, Terminal, and
  Installer views, plus keyboard and mouse input.
- PS/2 input and xHCI USB HID boot-keyboard / boot-mouse support.
- NVMe and AHCI/SATA block-device support. The AHCI driver identifies ATA disks
  and supports guarded installer writes when the device provides flush support.
- GPT, FAT32, NXFS, and a virtual filesystem layer.
- A Nexus graphical installer. The installer builds a GPT layout with an EFI
  System Partition, Nexus System volume, Recovery journal partition, and
  separate Nexus Data volume; BIOS installs also include a BIOS Boot
  partition.
- A Files view with a flat root listing (up to 128 indexed entries, six
  visible at once), keyboard/mouse selection, create/open/edit/delete, names
  up to 31 characters, and a scrollable 4 KiB ASCII text editor. The desktop
  mounts the dedicated NXFS Data partition after the Recovery partition;
  it does not expose the NXFS System partition as the user's file store.
  FAT32 volumes are read-only; RAM-only fallback saves are volatile.
- An early IPv4 stack with DHCP, DNS, TCP, and a plain HTTP text viewer.
  The network-device layer supports multiple registered adapters and tags
  wired Ethernet versus Wi-Fi media, but only the E1000 Ethernet driver is
  implemented; no Wi-Fi chipset, firmware, or scanning is currently supported.
  WPA2-Personal PMK derivation, pairwise-key expansion (PTK), EAPOL-Key MIC
  compute/verify, AES-128-CCM for CCMP, and RFC 3394 AES key unwrap have
  implemented known-answer tests. A complete EAPOL four-way state machine,
  replay/nonce policy, GTK installation, 802.11 header/nonce assembly,
  scanning/association, and a working wireless station/chipset driver remain
  unimplemented.
- The HTTP viewer validates bounded HTTP/1.x response framing (including
  content-length and chunked bodies) and provides keyboard page scrolling. Its
  bounded HTML-to-text stage formats common block elements, list items, and
  entities while omitting comments and script/style content. It does not yet
  provide CSS layout, JavaScript, HTTPS/TLS, cookies, or a modern browser engine.

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
- The browser is an early plain-HTTP text viewer; HTTPS/TLS and modern web
  rendering are not implemented.
- The Files view is currently a flat-file interface with 8.3-style filenames
  and a 4 KiB ASCII editor; it is not a full hierarchical file manager.
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
