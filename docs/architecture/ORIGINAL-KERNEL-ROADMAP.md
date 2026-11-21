# Nexus OS: Original Kernel Architecture and Migration Roadmap

**Status:** K0–K2 boot, memory, exceptions, PIC/PIT timer, cooperative scheduler, ACPI and read-only I/O APIC probe passed BIOS/UEFI QEMU CI; K3 PCI discovery being added; direct I/O APIC timer routing remains disabled  
**Target:** Broad x86-64 PC compatibility across vendors and generations, with UEFI and legacy BIOS boot in the first public release  
**Physical test strategy:** A rotating multi-vendor hardware matrix; no single laptop model defines compatibility  
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
- Add AHCI and NVMe controller support, beginning with read-only identification and then read-only block I/O.
- Add GPT parsing and a simple initramfs.
- Add USB host-controller support only after PCI, DMA, and interrupt foundations are stable.

**Exit:** Discover and identify supported storage controllers across representative x86-64 systems and virtual hardware, then read explicitly selected test media without writes. No installer may format disks at this stage.

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

## 6. Multi-vendor physical hardware validation

No single laptop or desktop is the compatibility baseline. Build a rotating matrix that includes systems from multiple manufacturers and generations, Intel and AMD platforms, different UEFI implementations, and representative storage, graphics, network, USB, and audio controllers. The Dell Latitude 5410 may be one optional sample in that matrix, but it is not a required-only target or a compatibility definition. Record exact PCI IDs and firmware versions from each system before driver work.

Record these categories on each physical system as applicable:

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

### Validated kernel foundation

- **K0 — Boot contract:** A freestanding x86-64 ELF kernel builds into a
  hybrid BIOS/UEFI ISO. The same kernel boots under OVMF and SeaBIOS.
- **K1 — Early CPU and memory:** The kernel installs exception IDT gates,
  consumes the Limine memory map and HHDM response, and allocates physical
  pages. The bootstrap allocator now has a bounded one-page DMA allocator;
  CI confirms an entire 4 KiB page can be allocated below 4 GiB.
- **K2 — Timer and cooperative scheduling:** The 8259 PIC/PIT path receives
  timer interrupts. A cooperative A-B-A-B-A-B scheduler test passes. Direct
  I/O APIC timer routing remains disabled after an earlier #GP; the proven
  PIC/PIT virtual-wire path remains active.
- **ACPI and PCI discovery:** RSDP/XSDT/RSDT/MADT parsing, read-only I/O APIC
  register probing, and PCI Configuration Mechanism #1 enumeration pass
  QEMU firmware tests. PCI BARs are decoded without BAR sizing or reassignment.
- **AHCI discovery:** The generic UEFI/Q35 profile reads AHCI CAP, GHC, PI, VS,
  and implemented-port link/signature registers. The legacy BIOS/PC profile
  currently exposes IDE.
- **NVMe namespace and read probe:** The generic UEFI/Q35 profile identifies
  the controller and NSID 1, reports namespace capacity/LBA size, creates one
  I/O queue pair, and reads LBA 0 from a disposable QEMU image seeded with a
  known signature. The returned bytes must match; no write command is issued.

### Latest verified storage build

CI run
[44874813799](https://github.com/LilGray-Code/Nexus/actions/runs/44874813799)
passed build, ELF/ISO verification, UEFI boot, legacy BIOS boot, and artifact
upload. In UEFI QEMU, the kernel identified the emulated controller as vendor
`1B36`, model `QEMU NVMe Ctrl`, serial `NEXUSNVME0001`, firmware `8.2.2`.
The test uses a disposable 64 MiB QEMU disk image. It identifies the controller
and namespace, then reads exactly one 512-byte logical block at LBA 0 and
verifies a known signature. No disk write command is issued.

The current CI profiles are deliberately generic rather than laptop-specific:
UEFI/Q35 with a qemu64 CPU and AHCI plus NVMe, and legacy BIOS/PC with the max
CPU model and IDE. Both profiles verify DMA32 page allocation. More QEMU memory
sizes, chipsets, and controller combinations remain planned.

These are still bring-up prototypes, not a daily-use operating system. The
allocator is monotonic and has no page-free operation or general heap.
Exception handling is fatal-only. The NVMe work is an admin Identify prototype,
not a block driver: the NVMe read path is a one-block QEMU prototype, not a general block driver;
there is no multi-request queueing, write support, IOMMU/VT-d
isolation, general DMA allocator, or recovery path. AHCI does not yet issue
ATA IDENTIFY or data commands. No physical hardware compatibility is claimed
until tests are recorded across multiple vendors and generations.

### Next implementation sequence

1. Introduce a common read-only block-device interface around the NVMe read
   prototype, including bounds checking and queue ownership.
2. Expand NVMe read support to multiple blocks and requests with timeouts,
   recovery, and DMA lifetime management.
3. Implement AHCI command-list/FIS/PRDT setup and ATA IDENTIFY, then bounded
   reads from explicitly selected disposable test media.
4. Add IOMMU/VT-d awareness, DMA ownership/lifetime rules, controller recovery,
   and more general DMA allocation before broader device driver support.
5. Expand QEMU coverage across memory sizes, CPU models, chipsets, and USB,
   graphics, and network controller families.
6. Begin multi-vendor physical boot and read-only discovery testing; record
   exact hardware IDs, firmware, boot mode, and results for each system.
7. Only after reliable block reads, add GPT parsing, a read-only filesystem
   path, initramfs, userland, and eventually the Nexus installer.

The current implementation has not been merged into `main`. A successful
QEMU CI run is not proof of physical compatibility.
