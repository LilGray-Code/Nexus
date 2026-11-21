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

This is still an early prototype. It does not yet provide a complete desktop
OS, durable installable filesystem, safe disk-writing stack, custom installer,
modern browser, broad USB/Wi-Fi/audio/GPU drivers, or full recovery/update
system.

## First-party graphical installer

Nexus will have its own graphical installer, running directly in the Nexus live
environment. It will not launch or depend on another distribution's installer
or package manager. The installer specification, proposed UEFI/BIOS disk
layouts, safety invariants, and implementation gates are in
[installer/README.md](installer/README.md).

The installer is deliberately non-destructive until the kernel has a tested
write-capable block layer, crash-consistent NXFS, GPT write/read-back tests, and
recovery handling. No installer completion or disk-write capability is claimed
yet.

## Build and test

See the [original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions) for BIOS/UEFI
build and QEMU test status. A buildable ISO is not a finished OS: release
requires verified boots, installer and first-boot tests, recovery tests, and a
multi-vendor x86-64 hardware matrix.
