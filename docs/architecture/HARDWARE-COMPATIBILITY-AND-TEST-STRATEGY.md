# Nexus x86-64 Hardware Compatibility and Test Strategy

## Goal

Nexus is a general-purpose x86-64 operating system project, not a build targeted
at one laptop model. The Dell Latitude 5410 is one possible physical test
machine, not the compatibility baseline or the only supported target.

## Compatibility approach

The first target is broad x86-64 PC compatibility through standard interfaces:

- UEFI and legacy BIOS boot paths.
- ACPI tables for platform discovery, including MADT and later power-management
  tables.
- PCI/PCIe enumeration and class-based device discovery.
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
