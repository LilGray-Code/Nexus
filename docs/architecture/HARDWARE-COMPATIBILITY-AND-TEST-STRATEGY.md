# Nexus x86-64 Hardware Compatibility and Test Strategy

## Goal

Nexus is a general-purpose x86-64 operating system project, not a build targeted
at one laptop model. The Dell Latitude 5410 is one possible physical test
machine, not the compatibility baseline or the only supported target.

## Compatibility approach

The first target is broad x86-64 PC compatibility through standard interfaces:

- UEFI and legacy BIOS boot paths.
- ACPI tables for platform discovery, including MADT interrupt topology and
  MCFG PCIe configuration windows; FADT/HPET and power-management support remain
  separate follow-on work.
- PCI/PCIe enumeration across firmware-described segments, with ECAM extended
  configuration access and legacy CF8/CFC fallback.
- Standard x86-64 CPU baseline; optional features are detected before use.
- AHCI/SATA, NVMe, and legacy IDE storage controller families.
- Common USB host-controller families, graphics adapters, network adapters,
  and audio devices as their drivers are implemented.

A device being enumerated is not the same as being supported by a working
driver. The compatibility matrix must distinguish firmware boot, device
discovery, driver operation, and real data I/O.

## Continuous-integration profiles

The QEMU suite should exercise multiple independent hardware profiles rather
than imitate a single physical system:

| Profile | Firmware | Machine / CPU | Devices and purpose |
|---|---|---|---|
| Generic baseline | UEFI | Q35 / qemu64 | Baseline x86-64 CPU, PCIe, AHCI and NVMe discovery |
| Modern chipset | UEFI | Q35 / max | Expanded CPU features, ACPI and AHCI register/port discovery |
| Legacy PC | BIOS | PC / qemu64 or max | Legacy boot and IDE controller discovery |
| Memory variation | UEFI | Q35 / 256 MiB and 1 GiB | Catch assumptions about available RAM |
| Future expansion | BIOS and UEFI | Multiple machine profiles | USB, graphics, network, audio and additional storage controllers |

QEMU is a fast regression environment, not proof of physical compatibility.
CI must not report a driver as supported merely because a PCI ID or class code
was detected.

## Physical validation plan

When driver milestones are ready, test on a rotating set of real systems from
different vendors and generations. Include desktops, laptops, and virtual
machines; Intel and AMD platforms; integrated and discrete graphics; SATA and
NVMe storage; and different UEFI implementations. Record exact hardware IDs,
firmware versions, boot mode, test results, and known limitations. Do not make
a model-specific whitelist the definition of compatibility.

## Current status

The current CI includes two firmware paths with different virtual hardware:

- UEFI / Q35 / qemu64 / 512 MiB, with the chipset AHCI controller and an
  emulated NVMe controller backed by a disposable 64 MiB image. CI verifies PCI
  detection, AHCI register/port discovery, DMA32 allocation, and successful
  NVMe Identify Controller metadata (vendor, serial, model, and firmware).
- Legacy BIOS / PC / max CPU / 512 MiB, which exercises the legacy boot path,
  IDE discovery, and DMA32 allocation.

The NVMe test enables PCI memory decoding and bus mastering for the emulated
controller, configures a polling admin queue, and issues only Identify
Controller. It does not issue namespace or sector commands, transfer sector
data, mount filesystems, or write to the emulated disk. Additional QEMU
profiles for other memory sizes and controller types remain planned.

Additional QEMU profiles for alternate memory sizes, additional chipsets, and
USB/network/graphics/audio controllers remain planned. Physical compatibility
will be validated across multiple vendors and generations as drivers mature.


## Hardware inventory and driver boundaries

Nexus now has a runtime CPUID inventory that reports the processor vendor,
brand string, family/model/stepping, package core/thread counts, and selected
instruction capabilities. It also records physical/virtual address widths and
capabilities such as PCID, FSGSBASE, SMEP, SMAP, UMIP, INVPCID, 1 GiB pages,
RDTSCP, CLFLUSHOPT, and CLWB when advertised. AVX-family features are reported
only when the processor advertises them **and** the OS has enabled the required
extended register state in XCR0. Merely detecting a CPU feature never enables
that instruction or turns on a protection mode for kernel code. SIMD execution
remains gated until scheduler and user-process context switching preserve the
corresponding register state.

PCI display discovery retains a bounded list of display adapters and their
vendor/device, revision, subsystem vendor/device, and bus/device/function
identifiers. The same OEM-aware PCI identity is retained for wireless
candidates; this is useful for matching future driver and firmware packages to
the exact board variant instead of relying on a broad vendor name. The generic
layer does not program unknown GPU BARs or issue commands to Intel, AMD, or
NVIDIA hardware. The current VirtIO-GPU path provides host scanout using a
CPU-rendered backing surface; it is not a general 2D/3D shader driver. The
renderer tracks changed pixels and can submit bounded dirty rectangles to the
VirtIO display transport.

PCI wireless inventory records common PCI network-class wireless candidates.
This is deliberately separate from a working Wi-Fi radio driver: scan,
authentication, firmware loading, regulatory behavior, and packet I/O still
require a chipset-specific implementation registered with the Wi-Fi manager.
USB wireless devices need USB-class discovery and their own drivers. The
Settings hardware overview presents per-device PCI identity, CPU capability
details, firmware inventory, and the active graphics path while clearly
distinguishing a detected device from a registered driver or active engine.

Firmware reporting currently inventories the boot firmware mode plus ACPI and
SMBIOS entry-point availability/version. ACPI discovery validates MADT and
optionally parses bounded MCFG allocations, rejecting bad checksums, unaligned
ECAM bases, invalid bus ranges, and overlapping ranges within one segment.
PCI enumeration uses the MCFG-defined ECAM window for both segment zero and
nonzero segments, reads extended configuration space, and retains CF8/CFC as
a fallback for segment-zero buses not covered by MCFG. MMIO pages are mapped
through a bounded, reusable uncached kernel window; device and driver-binding
identity includes the PCI segment so matching BDFs in different segments do
not alias during removal. Host tests cover MCFG parsing and segment isolation,
and BIOS/UEFI QEMU runs exercise the live PCI discovery path.

Nexus does not rewrite or flash the motherboard's UEFI/BIOS; vendor firmware
updates remain a platform maintenance operation. Future platform work includes
ACPI FADT/HPET power management, PCIe hot-plug event delivery, MSI/MSI-X
interrupt routing, IOMMU/DMA isolation, CPU topology across more vendors,
secure-boot policy reporting, and more real-device validation.

These features improve discovery and the safe base-driver contracts. They do
not imply that every display adapter, CPU feature, or Wi-Fi chipset is already
fully operational.
