# Nexus OS

Nexus is an independent x86-64 desktop operating system. The target is a Nexus-owned kernel, userland, desktop, application platform, and graphical installer—not a Debian, Linux, or FreeBSD distribution.

## Active development

The custom operating-system implementation is developed on the
[nexus-original-kernel-roadmap branch](https://github.com/LilGray-Code/Nexus/tree/nexus-original-kernel-roadmap).
That branch uses a freestanding Nexus kernel and Limine for BIOS/UEFI boot.
The old Debian-based prototype and its source bundle are retained only as
historical material; they are not the target OS, installer, or runtime.

## Current status

The original-kernel branch is an early, bootable development prototype. It
already contains kernel memory/interrupt foundations, PCI and ACPI discovery,
read-only storage discovery, a framebuffer shell, PS/2 input, a minimal
user-mode/ELF foundation, RAM filesystem, and early networking. It is not yet
a complete desktop OS and does not yet have a safe disk installer.

## Custom installer requirement

Nexus will ship its own graphical installer, running directly in the Nexus
live environment. It will not launch Debian Installer, Calamares, Subiquity,
Anaconda, or another distribution installer. The installer will own disk
discovery, partition planning, formatting, file deployment, bootloader setup,
first-boot configuration, and recovery. Destructive disk operations remain
disabled until the Nexus block-write, filesystem, partition-table, and
power-loss safety tests pass.

See [the original-kernel roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [the custom installer specification](https://github.com/LilGray-Code/Nexus/blob/nexus-original-kernel-roadmap/installer/README.md) for the active
architecture and acceptance gates.

## Compatibility target

The first kernel generation targets x86-64 PCs and laptops across vendors,
with both UEFI and legacy BIOS boot. A rotating multi-vendor hardware matrix
is required; no single laptop model defines support.
