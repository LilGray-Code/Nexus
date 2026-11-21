# Nexus

Nexus is an independent x86-64 desktop operating-system project. The original
kernel is developed on the
[nexus-original-kernel-roadmap branch](https://github.com/LilGray-Code/Nexus/tree/nexus-original-kernel-roadmap);
the Debian prototype on `main` is retained separately and is not the target
runtime kernel.

## Current prototype

The branch contains a freestanding x86-64 kernel, Limine BIOS/UEFI boot media,
a framebuffer desktop shell, PS/2 keyboard/mouse input, kernel terminal,
cooperative scheduler foundation, ring-3 ELF execution, read-only GPT/FAT32
storage and VFS, volatile RAM filesystem, Intel E1000 networking, DHCP, DNS,
and an early single-connection TCP/plain-HTTP client.

The Files view is being connected to the unified VFS and volatile RAM overlay.
The Browser view is still an early placeholder; plain HTTP retrieval through
the terminal is not a modern browser. HTTPS/TLS, certificate validation,
HTML/CSS/JavaScript rendering, preemptive user-process scheduling, persistent
writable storage, broad USB/Wi-Fi/audio/GPU drivers, power management, installer,
recovery, and release hardening are not yet complete.

## Build and test

See [the original-kernel architecture roadmap](docs/architecture/ORIGINAL-KERNEL-ROADMAP.md)
and [GitHub Actions](https://github.com/LilGray-Code/Nexus/actions) for the
current BIOS/UEFI build and QEMU test state. A buildable ISO is not considered a
finished OS until both firmware boot paths and the distributable image pass
their tests and the release requirements in the roadmap are satisfied.
