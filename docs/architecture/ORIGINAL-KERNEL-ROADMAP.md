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

### Desktop and input foundation (in progress)

- A reusable 32-bit RGB framebuffer drawing library validates Limine
  framebuffer dimensions, pitch, pixel-format masks, and clips rectangle/text
  drawing to the supplied surface.
- The in-kernel Nexus shell renders a top system panel, welcome window,
  application tiles, and bottom dock. It now has separate Files, Browser, and
  Settings views, navigated with F1/F2/F3 and Escape. These views are early
  built-in screens, not full applications; Files has no filesystem browser and
  Browser has no network stack or rendering engine.
- The shell consumes queued keyboard and mouse events. Keyboard shortcuts
  change views; a mouse pointer and clickable home-screen tiles are implemented.
  The latest CI run compiles and boots this shell, but does not synthesize mouse
  clicks or prove pointer behavior on physical hardware. Redrawing the framebuffer
  for pointer updates is intentionally simple and will need optimization.
- The PS/2 controller driver configures keyboard and mouse ports with bounded
  controller waits, enables IRQ1/IRQ12 only for devices that acknowledge setup,
  parses three-byte mouse packets, and queues input events in a fixed-size
  interrupt-safe ring buffer. USB HID, modern touchpads, and wireless input
  remain unimplemented.
- CI checks the framebuffer shell and keyboard initialization in UEFI QEMU,
  and still boots the same kernel under legacy BIOS. The QEMU test proves
  device initialization and boot, not real physical keyboard/mouse coverage.

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
- **NVMe read-only block-device API:** The generic UEFI/Q35 profile identifies
  controller and NSID 1, reports namespace capacity/LBA size, and registers
  `nvme0n1` through a bounds-checked read-only registry. The CI-only test build
  reads two sequential LBAs from a disposable QEMU image and verifies distinct
  signatures; the distributable ISO is rebuilt without the test-read option.

### Latest verified original-kernel build

CI run
[44878420894](https://github.com/LilGray-Code/Nexus/actions/runs/44878420894)
passed the full workflow: compilation, ELF/ISO verification, UEFI boot,
legacy BIOS boot, read-only NVMe block-device reads, GPT header and partition
entry CRC validation on a disposable test image, a clean distributable build,
and a final UEFI boot of that distributable image.

The CI-only GPT self-test uses a generated disposable 64 MiB QEMU disk image.
It contains a protective MBR, primary and backup GPT metadata, and one known
partition spanning LBAs 2048 through 4095. The parser validates the primary
header CRC and entry-array CRC, checks usable-LBA bounds, and discovers the
expected partition. The normal distributable ISO is built without the
NEXUS_STORAGE_SELFTEST option and does not automatically scan or read disk
sectors on boot.

The current parser is deliberately bounded: it supports 512-byte and 4096-byte
logical sectors, at most 128 entries and 64 KiB of entry data, stores at most
16 non-empty partitions, and exposes metadata only. It validates primary and
backup GPT metadata and both partition-array CRCs, requires a protective MBR,
and rejects overlapping partitions and duplicate partition GUIDs. It does not
mount a filesystem, interpret partition names, or write to storage.

The current CI profiles are deliberately generic rather than laptop-specific:
UEFI/Q35 with a qemu64 CPU and AHCI plus NVMe, and legacy BIOS/PC with the max
CPU model and IDE. Both profiles verify DMA32 page allocation. More QEMU memory
sizes, chipsets, and controller combinations remain planned.

These are still bring-up prototypes, not a daily-use operating system. The
allocator is monotonic and has no page-free operation or general heap.
Exception handling is fatal-only. The NVMe driver supports one in-flight
request at a time with a one-page transfer limit; there is no write support,
IOMMU/VT-d isolation, general DMA allocator, multi-namespace support, or
recovery path. AHCI does not yet issue ATA IDENTIFY or data commands. No
physical hardware compatibility is claimed until tests are recorded across
multiple vendors and generations.

### Next implementation sequence

1. Harden the read-only block interface with request ownership, bounded
   timeouts, controller recovery, and safe DMA lifetime management.
2. Validate backup GPT headers, partition-entry overlap, and malformed-table
   handling; expose partition metadata to a read-only Files application.
3. Add AHCI command-list/FIS/PRDT setup and ATA IDENTIFY, followed by
   read-only access to disposable test media.
4. Add IOMMU/VT-d awareness and a general-purpose DMA allocator before
   enabling broader bus-mastering device drivers.
5. Add a read-only filesystem, VFS, initramfs, and the first userland process.
6. Expand QEMU coverage across memory sizes, CPU models, chipsets, and USB,
   graphics, and network controller families.
7. Begin multi-vendor physical boot and read-only discovery testing; record
   exact hardware IDs, firmware, boot mode, and results for each system.
8. Build the window manager/compositor, app runtime, system services, installer,
   recovery, and update path before calling the OS a complete desktop release.

The current implementation has not been merged into `main`. A successful
QEMU CI run is not proof of physical compatibility.


### Interactive desktop shell work (prototype)

The desktop shell has keyboard-navigable app views. F1 opens the Files screen,
F2 opens the Browser placeholder, F3 opens Settings, and Escape returns home.
A mouse pointer and click-to-open home tiles are implemented. CI run
44878054534 compiled and booted the shell under UEFI and legacy BIOS, but it
did not synthesize pointer movement or clicks. These features are an
interactive shell foundation, not a complete desktop environment. There is
no window manager, compositor, user-mode application runtime, editable text
widget, filesystem browser, browser engine, network stack, or installer yet.


### Read-only GPT and Files-view progress

The bounded GPT parser now checks the protective MBR signature/type, primary
header CRC, backup header CRC and reciprocal LBA references, disk GUID and
partition-array metadata agreement, and both primary and backup partition-array
CRCs. It rejects invalid usable-LBA ranges, duplicate unique partition GUIDs,
overlapping partitions, and more than 16 non-empty partitions. It stores
metadata only and issues no write requests.

The desktop Files view now initiates a GPT scan only when the user opens Files
with F1. It displays the discovered partition count and first partition LBA
range for the first disk with a valid GPT. This is intentionally not a mounted
filesystem or a general file manager yet. The normal boot path does not
automatically read user disk sectors; the read occurs only after an explicit
Files action. CI run 44879132430 passed compilation, UEFI and legacy-BIOS boot tests, the NVMe/GPT self-test, and the final distributable ISO boot check. The distributable ISO is built without CI-only automatic disk reads.


### Read-only filesystem and VFS milestone (in development)

The original-kernel branch now contains a bounded FAT32 reader and a small
virtual filesystem interface. The FAT32 layer validates the BPB geometry,
supports 512-byte and 4096-byte logical sectors, follows bounded cluster
chains, lists short-name root entries, and reads file data in bounded chunks.
The VFS exposes root directory entries, read-only file open/read/close, and
case-insensitive 8.3 name matching. No create, rename, delete, write, or
format operation exists.

The desktop Files view is being switched to consume the VFS rather than call
the FAT32 implementation directly. It displays up to six root names and a
short text preview. CI uses a disposable GPT/FAT32 NVMe image with a README
file and a cross-cluster read test. This is not yet a full file manager:
subdirectories, long file names, FAT consistency recovery, multiple mounts,
write support, permissions, and user-mode file APIs are not implemented.


### Desktop terminal application

The framebuffer desktop now includes a keyboard-driven terminal view (F4).
Its bounded command set is read-only: `help`, `about`, `mem`, `uptime`, `ls`, `cat FILE`,
and `clear`. Directory listing and file reads go through the VFS and do not
modify storage. The terminal is currently a kernel-integrated shell, not a
separate ring-3 process; command history, pipes, scripting, permissions, and
a general executable loader remain future work.


### Ring-3 syscall progress

The user-mode bring-up test now exercises a restricted write syscall (number 2)
before exit (number 1). The first implementation accepts only file descriptor
1, a maximum 128-byte buffer wholly inside the explicitly mapped user code page,
and writes those bytes to the serial console. The CI boot test asserts the
user-originated marker. This is a deliberately narrow proof of syscall entry,
argument validation, and return to user mode; it is not yet a general process
ABI, safe arbitrary user-memory copy layer, ELF loader, or terminal device.


### User-copy and syscall hardening (current milestone)

The syscall write path now copies bytes from the active address space through
the kernel page tables instead of dereferencing the userspace pointer. The
copy routine checks lower-canonical user bounds, verifies PRESENT and USER
permissions at every paging level, handles 4 KiB, 2 MiB, and 1 GiB leaves,
and copies into a bounded kernel-owned buffer before serial output. Invalid
pointers return an error to the caller rather than halting the kernel. The
ring-3 test first submits a high-half kernel pointer and verifies rejection,
then submits a valid user string and exits with status 42.

This remains a narrow, single-threaded proof: the syscall copies at most 128
bytes, writes only to the serial console, and has no process-specific address
space object, file-descriptor table, page-fault recovery, or concurrent
memory-unmapping support. It is not yet a general user-copy implementation
for a preemptive multi-process kernel.


### ELF64 executable validation foundation

The kernel now has a bounded ELF64 parser for little-endian x86-64 ET_EXEC
images. It validates the ELF identity and machine, exact header layouts,
program-header table bounds, load-segment file/memory ranges, user virtual
address limits, power-of-two alignment rules, non-overlapping page ranges,
and the W^X policy. The entry point must fall inside an executable load
segment. Dynamic linking and ET_DYN/PIE are deliberately not accepted yet.

A boot-time in-memory fixture exercises a valid image plus malformed magic,
out-of-bounds file data, writable-executable segments, an invalid entry point,
and a truncated program-header table. CI now requires this validation marker
in both UEFI and legacy-BIOS boots and in the final distributable-ISO boot.

This is the executable-format validation layer only. It does not yet allocate
and map segment pages, zero BSS, establish per-process page tables, apply
relocations, resolve dynamic dependencies, or transfer control to a loaded
program. The next userland step is a bounded loader built on this parser,
followed by process lifecycle and IPC.


### ELF64 static executable loading (new implementation milestone)

The kernel now has a bounded static ET_EXEC loader layered on the ELF64
validator. It creates a private lower-half page-table root while retaining the
kernel's high-half mappings, allocates and zeroes pages for each validated
PT_LOAD segment, copies file-backed bytes, leaves BSS bytes zeroed, maps a
separate initial user stack page, and transfers control to the loaded entry
point in ring 3. The boot-time user-mode self-test is now packaged as a real
ELF64 executable and launched through this loader rather than being copied
directly into a hand-built code mapping.

The loader currently handles one bootstrap process, fixed-address static
ET_EXEC binaries, at most 16 load segments, no dynamic linker/PIE/TLS, and a
single fixed stack page. The physical allocator is still monotonic, page-table
and user-page reclamation is absent, and the process has no general
file-descriptor table or lifecycle manager. This proves the image-to-address-space-to-ring-3 path, not a general
multi-process runtime. CI boot assertions exercise the loaded program's
rejected kernel-pointer write, valid stdout write, and exit status.


### Hardware NX page protection

The user address-space mapper now detects the CPUID NX feature and enables
IA32_EFER.NXE before using the page-table execute-disable bit. ELF executable
segments are mapped user-readable and read-only; non-executable segments and
the initial user stack receive NX when the processor advertises support.
Processors without NX retain the parser's software W^X restriction, but do
not receive hardware execute-disable enforcement. The boot log reports
whether hardware NX is enabled. The BIOS/UEFI and final-ISO CI profiles now
require the ELF loader self-test marker after the ring-3 program exits.

This remains an early single-process implementation. It does not yet reclaim
address-space pages, handle page faults as recoverable process faults, switch
between multiple user processes, or provide a mature per-process resource
table.


### User address-space teardown and page reuse

The bootstrap physical allocator now has a bounded free-page stack. It validates
page alignment and allocator ownership, rejects double frees, reuses reclaimed
pages for normal and DMA-constrained allocations, and includes a boot-time
allocation/reuse self-test. The private user address-space destroy path walks
only the lower-half user page-table tree, frees mapped user pages and
intermediate tables, and refuses to destroy the active CR3. The ring-3 ELF
self-test now tears down its address space after returning to the kernel.

This is an early single-core reclamation path with a fixed 8,192-page free-list
capacity. It is not yet SMP-safe, does not provide per-page reference counts,
and cannot safely reclaim shared pages. The loader still supports a single
bootstrap process; preemptive multi-process scheduling remains future work.


### Stable initial syscall error handling

The first int 0x80 ABI now has named syscall numbers for exit and stdout write.
Unknown syscall numbers return -ENOSYS, an invalid output descriptor returns
-EBADF, and a rejected userspace buffer returns -EFAULT. A non-user invocation
is rejected without transferring control into an idle loop. The ring-3 test
program checks both -ENOSYS and -EFAULT before continuing to a valid write and
process exit; BIOS, UEFI, and final-image CI logs require the syscall ABI marker.

Only exit and serial-backed stdout write exist at this stage. File descriptors,
stdin, filesystem reads, process creation, signal/fault delivery, and a
userspace C runtime remain future work.


### Read-only file syscalls for user-mode programs

The initial descriptor-based file ABI now has open, read, and close calls backed
by the existing read-only FAT32/VFS mount. The current bootstrap user process
has eight descriptor slots starting at 3. Paths are copied using checked
user-memory access, directory separators are rejected, and absent volumes,
missing files, invalid descriptors, invalid destinations, and oversized reads
return negative errno values. Reads use a kernel bounce buffer and a checked
writable-user-page copy path. The underlying disk remains read-only.

The boot-time ELF program validates path-traversal rejection and the invalid
descriptor results from read and close without relying on the contents or
presence of a physical disk. This is not yet a
general filesystem ABI: there are no writes, subdirectories, long filenames,
per-process tables beyond the single bootstrap process, stdin, or device files.


The terminal now performs read-only volume discovery when the user explicitly
runs `ls` or `cat`, instead of requiring the Files view to have been opened
first. It still does not auto-scan disks on boot.


The ELF validator also caps aggregate PT_LOAD memory at 16 MiB. This bound is
checked before allocating any process pages and is covered by the parser
self-test, independently of the 128 KiB executable-file limit used by Terminal.


### On-disk ELF execution test

The disposable GPT/FAT32 CI image now contains a small static \`HELLO.ELF\`
program in its root directory. During the UEFI storage-selftest build, Nexus
mounts that image through the read-only GPT/FAT32/VFS path, opens the ELF by
its 8.3 filename, loads it into a private user address space, executes it at
CPL3, checks its stdout marker and exit status 7, closes process descriptors,
and reclaims the process pages. This verifies the complete read-only disk to
user-process launch path without writing to the test image or touching a
user's real disk. The release build does not auto-scan or execute files.


The BIOS QEMU profile has no attached storage disk, so its storage-enabled
build reports the on-disk ELF execution test as skipped rather than failing
the boot. The UEFI profile attaches the disposable NVMe image and must emit
the HELLO.ELF stdout and exit-status assertions; a mounted volume with a bad
or unlaunchable test file remains a hard test failure.


### First Intel E1000 Ethernet driver

The kernel now detects a small Intel 82540/82545/82546/82574-compatible PCI
device set, enables PCI memory decoding and bus mastering, maps E1000 MMIO
register pages, reads the MAC address registers, allocates DMA32 transmit and
receive descriptor rings plus packet buffers, and configures polling-mode
receive/transmit. The packet API sends and receives bounded Ethernet frames;
the boot path transmits a broadcast ARP probe. UEFI and BIOS QEMU profiles
attach an E1000 model using the user-mode network backend and require the
transmit-completion marker.

This is only the first hardware/data-link stage. It does not yet implement
Ethernet demultiplexing, an ARP cache, IPv4, DHCP, DNS, TCP, TLS, sockets,
interrupt-driven packet delivery, or a browser network stack. The current
driver supports one polling queue and a limited Intel PCI ID set; it is not
yet a general NIC framework or hardened against arbitrary DMA devices.
