# Chrome compatibility on standalone Nexus OS

## Goal

Run Google Chrome directly on an installed Nexus system, without booting or depending on Debian, Ubuntu, Windows, or another host operating system. A launcher or icon that delegates to another OS does not satisfy this goal.

## Current engineering baseline

The current kernel is a freestanding x86-64 system with a framebuffer desktop, a small ring-3 execution path, an ELF64 loader, a limited syscall ABI, VFS-backed file access, and early wired networking.

The existing user application path is intentionally small:
- The process table has four slots.
- Executable images are limited to 128 KiB by the process manager.
- The ELF parser accepts static x86-64 ET_EXEC images only.
- PT_INTERP, PT_DYNAMIC, and PT_TLS are rejected.
- The loader caps aggregate segment memory at 16 MiB.
- Each process is run synchronously to completion; this is not yet a general preemptively scheduled user-process environment.
- The syscall ABI currently exposes exit, console write, open, read, close, and file write.
- The desktop browser is a small text-oriented HTTP client, not a Chromium-based browser.
- Networking is early-stage Ethernet/DHCP/TCP/HTTP; TLS and modern browser networking are not implemented.

These constraints mean Chrome cannot currently be installed or executed on Nexus. Raising the executable size limit alone would not solve the missing runtime and operating-system interfaces.

## Required platform work

The following are engineering gates, not claims that the features already exist.

### 1. General-purpose user process model
- Per-process virtual address spaces with safe user/kernel isolation.
- Preemptive scheduling of user processes, process lifecycle, signals or an equivalent event model, and robust process cleanup.
- Per-process kernel stacks, descriptor tables, credentials, and resource accounting.
- Demand paging, page faults, memory mapping, shared memory, and practical address-space limits.
- Large executable and data files streamed from storage rather than copied into a fixed global staging buffer.

### 2. Executable and runtime support
- ELF ET_DYN/PIE loading with ASLR-capable relocation handling.
- PT_INTERP dynamic linker loading and symbol resolution.
- TLS and thread-local storage, including correct per-thread runtime state.
- Shared libraries, versioned symbols, constructors/destructors, and loader error handling.
- A maintained C/C++ runtime and a supported compiler/ABI toolchain.

### 3. POSIX/Linux application interface
- A documented, versioned syscall ABI and libc port.
- Threads, futex-like synchronization, robust signals, timers, polling, pipes, sockets, and process spawning.
- File and directory semantics, permissions, metadata, memory mapping, and filesystem notification primitives.
- Enough Linux-compatible behavior for the chosen Chromium/Chrome build, or a native port of the required interfaces.

### 4. Graphics, input, and multimedia
- A real display-mode and graphics driver stack, not only a bootloader-provided framebuffer.
- Window/compositor services, accelerated or reliable software rendering, GPU driver strategy, and font/text shaping.
- Full keyboard, mouse, touch, clipboard, and accessibility interfaces.
- Audio output and media codec integration if full browser media support is part of the target.

### 5. Secure networking and browser security
- IPv4/IPv6, DNS, TCP/UDP sockets, TLS 1.2/1.3, certificate trust and update mechanisms.
- Modern HTTP/2 and HTTP/3 support as required by the selected browser build.
- Browser sandbox primitives: separate processes, restricted syscalls, memory isolation, IPC, and capability/permission boundaries.
- Secure update, code-signing, and vulnerability-response strategy.

### 6. Chrome distribution and validation
- Determine whether Google Chrome's official Linux build can be supported through a Linux ABI compatibility layer, or whether Chromium must first be ported natively. Do not assume the official Chrome binary runs on a generic POSIX system.
- Build and package all required browser components and dependencies for Nexus.
- Test installation, first launch, tabs, JavaScript, HTTPS, downloads, fonts, audio/video, GPU fallback, process isolation, and browser updates on QEMU and supported physical hardware.
- Publish a compatibility statement and a reproducible, signed package.

## Recommended implementation order

1. Replace the synchronous four-slot process execution path with a persistent, preemptively scheduled user-process model and per-process resources.
2. Extend the executable loader to support ET_DYN/PIE, then dynamic linking and TLS, with host-side parser/loader tests.
3. Define a stable syscall ABI and build a libc/runtime port against it.
4. Implement the missing filesystem, thread, IPC, networking, and graphics interfaces.
5. Evaluate the official Chrome Linux build against a measured Linux ABI compatibility matrix; if the required compatibility scope is impractical, target a native Chromium port.
6. Integrate and test the browser only after the platform gates above are met.

## Release rule

Nexus must not advertise Chrome support until a real browser build launches and passes the functional and security tests on a clean, standalone Nexus installation. A browser mockup, web shortcut, text browser, or Linux package copied onto the disk is not Chrome support.
