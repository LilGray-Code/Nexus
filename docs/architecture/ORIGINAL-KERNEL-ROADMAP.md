# Nexus OS: Original Kernel Architecture and Migration Roadmap

**Status:** K0 and K1 passed BIOS/UEFI QEMU CI; K2 PIC/PIT timer interrupt path passed; cooperative scheduler prototype under validation  
**Target:** x86-64 PCs, with UEFI and legacy BIOS boot in the first public release  
**Reference hardware:** Dell Latitude 5410  
**Long-term requirement:** Nexus-owned kernel and userland; no FreeBSD or Linux kernel at runtime.

## 1. Product decision

Nexus is to become an independent operating system, not a FreeBSD distribution and not a Linux distribution.

The first milestone is a small, auditable Nexus kernel that boots, reports hardware, manages memory, and can launch a minimal Nexus user process. The existing Debian-based ISO is retained as a historical prototype and is not the base for the new system.

FreeBSD may be used as a *temporary development/reference environment* for toolchains, emulation, and hardware research. No FreeBSD kernel, FreeBSD userland, or Linux kernel is part of the final Nexus runtime. This distinction must remain explicit in release notes and dependency audits.

## 2. Initial technical choices

| Area | Initial decision |
|---|---|
| CPU / ABI | x86-64 only for the first kernel generation |
| Kernel implementation | C for core and drivers, x86-64 assembly for entry/interrupt/context-switch code; evaluate Rust for isolated new subsystems after the ABI stabilizes |
| Bootloader | Limine, pinned to a reviewed release and commit |
| Boot protocol | Limine protocol; kernel is a freestanding ELF64 image |
| Firmware modes | UEFI x86-64 and legacy BIOS on x86-64 CPUs |
| Disk layout | GPT for UEFI; BIOS boot partition for BIOS-on-GPT, with an MBR/hybrid ISO path for removable media |
| Kernel format | ELF64, with explicit linker script and symbols |
| Early console | Serial COM1 plus framebuffer/text fallback |
| Build | Reproducible cross-build using a pinned x86_64-elf toolchain |
| Test | QEMU/OVMF for UEFI, QEMU SeaBIOS for legacy BIOS, then physical hardware |
| Root filesystem | Start with a read-only initramfs / ramdisk; add a documented on-disk format and installer only after block I/O is reliable |
| Userland | Nexus-owned minimal userland and syscall ABI; do not assume FreeBSD or Linux binaries will run |
| Desktop | Nexus compositor/shell after graphics, input, process, and IPC foundations exist |
| Browser | Preserve the existing browser UX and AI HTML, but port the application only after a supported userland, window system, networking, TLS, and browser-engine strategy exist |

### Bootloader rationale

Limine is a third-party bootloader, not the Nexus kernel. Its published documentation describes x86-64 support and both UEFI and BIOS installation paths, including hybrid ISO creation. Pin the exact version and verify its license, source, and checksums in the build. Do not copy an unreviewed binary into a release.

The kernel must not depend on firmware services after its boot handoff. It should validate the bootloader-provided memory map and framebuffer information before using them.

## 3. Repository structure to introduce

Keep the existing Debian prototype and build workflow untouched on `main`. Develop the new OS on a separate branch and introduce these directories incrementally:

```text
docs/
  architecture/
    ORIGINAL-KERNEL-ROADMAP.md
    KERNEL-ABI.md
    BOOT-PROTOCOL.md
    DRIVER-MATRIX.md
  hardware/
    latitude-5410.md
kernel/
  arch/x86_64/
    boot/
    cpu/
    interrupts/
    paging/
  core/
    memory/
    scheduler/
    ipc/
    syscall/
  drivers/
    acpi/
    pci/
    serial/
    framebuffer/
    storage/
  include/
  linker/
boot/
  limine/
  iso/
userland/
  init/
  libc/
  shell/
  services/
desktop/
  compositor/
  shell/
apps/
  nexus-browser/
  nexus-ai/
installer/
tests/
  qemu/
  boot/
  kernel/
toolchain/
  versions.lock
  scripts/
.github/workflows/
  nexus-kernel-ci.yml
```

Do not move or delete the current archive, installer, or workflow during the initial kernel work. The new workflow must have a distinct artifact name, for example `NexusOS-OriginalKernel-UEFI-BIOS`, so users cannot confuse it with the old Debian image.

## 4. Kernel milestones and acceptance criteria

### K0 — Build and boot contract

- Pin compiler, assembler, linker, Limine release, QEMU, and image tools.
- Add a linker script and kernel ELF target.
- Define the boot information structure and version checks.
- Build a hybrid ISO that contains UEFI and BIOS boot paths.
- CI boots the image under both OVMF and SeaBIOS.

**Exit:** CI proves the same kernel image reaches its entry point in both firmware modes and prints a versioned boot banner to serial.

### K1 — CPU and early kernel

- Establish a known-good stack and calling convention.
- Install GDT, IDT, exception handlers, and a double-fault path.
- Configure x86-64 page tables and enforce W^X where practical.
- Parse and validate the memory map.
- Add a physical page allocator and kernel heap.
- Add panic, assertion, and serial logging facilities.

**Exit:** Kernel reports CPU features and memory map, handles expected exceptions, and passes allocator tests without firmware calls.

### K2 — Interrupts, time, and multiprocessor support

- Initialize APIC/IOAPIC where available.
- Add PIT/HPET or another validated timer source.
- Add a monotonic clock.
- Bring up additional CPU cores using a staged SMP implementation.
- Add preemptive scheduling only after interrupt and context-switch tests pass.

**Exit:** Timer interrupts and scheduler tests pass in QEMU; single-core fallback remains reliable.

### K3 — Hardware discovery and storage

- Enumerate PCI/PCIe devices safely.
- Parse ACPI tables and expose power/firmware information.
- Add AHCI and NVMe block-device support, initially read-only.
- Add GPT parsing and a simple initramfs.
- Add USB host-controller support only after PCI, DMA, and interrupt foundations are stable.

**Exit:** Detect the Latitude 5410 SSD controller and identify its storage device without writing to it. No installer may format disks at this stage.

### K4 — Userland and system ABI

- Define a versioned syscall ABI and user/kernel boundary.
- Add ELF64 user-process loading, address-space isolation, and basic process lifecycle.
- Implement handles/file descriptors, pipes, IPC, and a minimal VFS.
- Build a small Nexus init process, shell, and service manager.
- Create a minimal C library only for the Nexus ABI; document what is and is not POSIX-compatible.

**Exit:** A user process can start, communicate with another process, read a ramdisk file, and exit without kernel corruption.

### K5 — Graphics, input, and networking

- Start with a framebuffer console and basic 2D drawing.
- Add keyboard and mouse input (USB HID after xHCI support).
- Add a basic window/compositor protocol.
- Add Ethernet before Wi-Fi; implement or port network components only after license and ABI review.
- Add DNS, TCP/IP, TLS, certificate updates, and secure time before browser networking.

**Exit:** Desktop prototype accepts keyboard/mouse input and can establish a verified HTTPS connection using a maintained TLS implementation.

### K6 — Nexus desktop and applications

- Implement Nexus login, desktop shell, launcher, settings, notifications, file manager, and power UI.
- Port the browser interface as a native Nexus application or a deliberately supported app runtime.
- Integrate the supplied `ai.html` as the AI UI, but do not treat an HTML file alone as a functioning AI backend. Document required network/API configuration and protect secrets.
- Add sandboxing, permissions, download isolation, and an update mechanism.

**Exit:** Browser can browse HTTPS sites, manage tabs/bookmarks/downloads, and invoke the configured AI service under the Nexus security model.

### K7 — Installer and first release

- Build a live environment with an explicit “Try Nexus” and “Install Nexus” flow.
- Detect disks and show exact model/capacity before destructive actions.
- Require a second confirmation before repartitioning; never silently erase.
- Support UEFI GPT and legacy BIOS boot installation.
- Add recovery media, install logs, rollback/recovery guidance, and SHA-256 manifests.
- Test installation and updates in virtual machines and on physical systems.

**Exit:** Independent testers can install, reboot, recover, and update without a terminal. UEFI and BIOS test reports are published separately.

## 5. BIOS + UEFI media design

Use one x86-64 kernel ELF and a bootloader configuration that supports both firmware paths.

- **UEFI:** FAT EFI System Partition / removable-media fallback path containing the x86-64 EFI boot application.
- **Legacy BIOS:** Limine BIOS stages, with a valid MBR/GPT boot arrangement.
- **ISO:** Build with a hybrid ISO toolchain and validate the ISO in both OVMF and SeaBIOS.
- **USB:** Publish a raw hybrid image where supported, plus clear instructions for writing it with Rufus or an equivalent imaging tool.
- **Secure Boot:** Not claimed in the first release unless the complete boot chain is signed, key enrollment is documented, and signature verification is tested.

Legacy BIOS support here means legacy firmware boot on an x86-64 processor; it does not mean support for 32-bit-only CPUs.

## 6. Dell Latitude 5410 hardware validation

The Latitude 5410 is the reference laptop, not a promise of full compatibility. Record exact PCI IDs and firmware versions from the actual machine before driver work.

Required hardware checklist:

- CPU model, CPUID features, and core topology.
- Intel integrated GPU PCI ID; framebuffer first, accelerated graphics later.
- NVMe/SATA controller and SSD model; read-only discovery before writes.
- Exact Wi-Fi and Bluetooth chipset; no generic assumption based on laptop model.
- Audio codec/controller, microphone, speakers, and headphone jack.
- USB-A, USB-C, xHCI, keyboard, touchpad, webcam, and card reader if present.
- ACPI battery, lid switch, brightness, thermal sensors, suspend/resume.
- UEFI boot, BIOS boot, reboot, shutdown, and recovery behavior.

No destructive install testing on the user's primary disk. Use a spare SSD or virtual disk until backups and recovery are verified.

## 7. CI and release gates

Create a separate GitHub Actions workflow for the original kernel. It should:

1. Build with pinned toolchain versions.
2. Treat warnings as errors for kernel-owned code where practical.
3. Run static analysis and unit tests for pure components.
4. Build the ELF kernel and hybrid ISO.
5. Boot UEFI using OVMF and legacy BIOS using SeaBIOS.
6. Capture serial logs and fail if the expected boot marker is absent.
7. Run memory allocator, exception, and boot-info tests.
8. Generate SHA-256 checksums and attach build metadata.
9. Upload clearly labeled artifacts with short retention for development builds.

A successful ISO build is not proof of hardware compatibility. Physical-hardware tests must be recorded separately.

## 8. Migration rules for the existing project

- Keep the current Debian 13 workflow available on `main` until the original-kernel path has independent, repeatable boot tests.
- Do not overwrite or relabel the existing Debian artifact as an original-kernel build.
- Preserve the existing Nexus Browser source and supplied AI HTML as application assets; audit and port them later.
- Separate reusable artwork, UX specifications, icons, and browser behavior from Linux/Electron-specific integration.
- Inventory every dependency in the existing source archive before porting. Classify each as reusable asset, portable source, OS-specific code, or unsupported binary.
- Do not claim “Linux-free” until the final kernel, boot image, userland, drivers, and runtime dependency inventory have been reviewed. A temporary Linux or FreeBSD build host does not make the installed OS a Linux/FreeBSD OS, but must be disclosed as a build dependency.

## 9. Main engineering risks

1. **Driver scope:** Intel graphics acceleration, Wi-Fi, audio, ACPI, USB, and power management are substantial work. Start with framebuffer and serial output.
2. **Browser scope:** A modern browser engine is itself a large platform. Treat it as a late milestone, not a kernel milestone.
3. **Security:** Process isolation, memory safety, filesystem permissions, TLS, updates, and recovery must be designed before public daily use.
4. **Boot portability:** BIOS and UEFI are separate paths and must have separate automated test jobs.
5. **Project scale:** A production-quality general-purpose OS is a multi-year effort. The first realistic deliverable is a small bootable kernel prototype, not a replacement for Windows or a mature desktop OS.

## 10. Current implementation status and next sequence

The original-kernel implementation lives on the separate
`nexus-original-kernel-roadmap` branch. The Debian-based prototype and its
workflow remain on `main`.

### Validated prototypes

- **K0 — Boot contract:** A freestanding x86-64 ELF kernel builds into a
  hybrid BIOS/UEFI ISO. CI run
  [44870153869](https://github.com/LilGray-Code/Nexus/actions/runs/44870153869)
  passed OVMF and SeaBIOS boot checks.
- **K1 — Early CPU and memory:** The kernel installs exception IDT gates,
  consumes the Limine memory map and HHDM response, and allocates a 4 KiB
  physical page. CI run
  [44870391280](https://github.com/LilGray-Code/Nexus/actions/runs/44870391280)
  passed both firmware tests.
- **K2 — Timer interrupt:** The 8259 PIC is remapped, PIT channel 0 is set to
  100 Hz, and Local APIC LINT0 routes PIC ExtINT to the CPU. x2APIC-capable
  CPUs use the MSR interface; xAPIC uses an explicit uncached MMIO mapping.
  CI run
  [44871433999](https://github.com/LilGray-Code/Nexus/actions/runs/44871433999)
  passed OVMF and SeaBIOS, with five PIT ticks observed in each serial log.

These are bring-up prototypes, not a daily-use operating system. The physical
Latitude 5410 has not yet been tested. The current allocator is monotonic and
has no page-free operation or heap. Exception handling is fatal-only. K2's
timer path is a single-core legacy PIC/PIT compatibility path; ACPI MADT,
IOAPIC routing, APIC timer calibration, SMP, and preemption remain unfinished.

### Current development increment

A cooperative scheduler prototype has been added on the branch. It uses
statically allocated stacks and an x86-64 assembly context switch. The QEMU
acceptance test creates two workers and requires the alternating
`A-B-A-B-A-B` trace, with both workers completing three turns. This test is
non-preemptive and is not evidence of SMP or interrupt-driven scheduling.
Its build and firmware tests must pass before treating the increment as
validated.

### Next implementation sequence

1. Finish and validate the cooperative scheduler and stack-switching test.
2. Add ACPI RSDP/RSDT/XSDT parsing and MADT processor/APIC topology discovery.
3. Implement IOAPIC routing and a calibrated Local APIC timer, while retaining
   a validated single-core fallback.
4. Add interrupt-safe preemption, sleep/wakeup queues, and scheduler tests.
5. Bring up additional CPU cores and add per-CPU state only after single-core
   context-switching and interrupt invariants are stable.
6. Continue into PCI discovery and read-only storage identification before
   attempting userland or destructive installation.

A successful CI ISO build is not proof of hardware compatibility. Physical
hardware testing must be recorded separately, starting with non-destructive
USB boot and read-only device discovery.
