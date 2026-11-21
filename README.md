# Nexus OS

Nexus is an independent x86-64 desktop operating system. Its target runtime is
fully Nexus-owned: kernel, userland, system services, desktop, applications,
filesystem, and graphical installer. It is not a Linux, Debian, or FreeBSD
distribution.

## Active implementation

The original-kernel implementation is developed on the
[nexus-original-kernel-roadmap branch](https://github.com/LilGray-Code/Nexus/tree/nexus-original-kernel-roadmap).
It uses a freestanding x86-64 kernel and Limine for UEFI and legacy BIOS boot.
The Debian-based prototype is historical only and is not part of the target
runtime or installer.

## Current prototype

The active branch contains a freestanding x86-64 kernel, Limine BIOS/UEFI boot
media, a framebuffer desktop shell, PS/2 keyboard/mouse input, kernel terminal,
cooperative scheduler foundation, ring-3 ELF execution, read-only GPT/FAT32
storage and VFS, volatile RAM filesystem, Intel E1000 networking, DHCP, DNS,
and an early single-connection TCP/plain-HTTP client.

The desktop now includes an **Install F5** graphical preview with target-device
capacity, logical sector size, and UEFI/legacy-BIOS partition planning. It is
read-only and cannot install or modify a disk. The GPT transaction is linked
through a kernel block-device adapter and has host-side adapter tests for boot
media rejection, unknown boot provenance, mounted targets, confirmation, and
successful disposable-disk metadata writes. Firmware boot-source identification
and UI integration are still missing, so release writes remain disabled.

This is still an early prototype, not a complete desktop OS. Durable installable
filesystem hardening, a full installer wizard, system-image deployment,
target-disk bootloader installation, first boot, recovery/update flows, modern
browser/TLS, broad USB/Wi-Fi/audio/GPU drivers, power management, and release
hardening remain incomplete.

## First-party graphical installer

Nexus will have its own graphical installer, running directly in the Nexus live
environment. It will not launch or depend on another distribution's installer
or package manager. The current UI is only a read-only preview; the actual
installer engine specification, GPT planner, metadata serializer, guarded
transaction, safety invariants, and implementation gates are in
[installer/README.md](installer/README.md).

The GPT transaction and its kernel storage adapter are host-tested against a
disposable in-memory block device in CI. Tests exercise unknown boot provenance,
boot-medium rejection, mounted-target rejection, bad confirmation, identity
change, and a successful GPT write/read-back. The adapter cannot open a session
until the boot chain positively identifies the live boot medium; that firmware
provenance integration is not implemented. The UI is not connected, and no
completed installation or release-ISO disk-writing capability is claimed.

## Build and test

See the [original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions) for BIOS/UEFI
build and QEMU test status. A buildable ISO is not a finished OS: release
requires verified boots, actual installer and first-boot tests, recovery tests,
and a multi-vendor x86-64 hardware matrix.
