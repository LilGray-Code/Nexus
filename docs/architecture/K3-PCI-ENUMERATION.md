# K3 — PCI Device Enumeration

## Status

The Nexus kernel has a read-only PCI configuration-space enumerator on the
`nexus-original-kernel-roadmap` branch. Its first implementation uses x86
PCI Configuration Mechanism #1 through ports `0xCF8` and `0xCFC`.

## What it does

- Scans all 256 PCI bus numbers, 32 device numbers, and applicable functions.
- Detects multifunction devices through the header-type bit.
- Records vendor/device IDs, class/subclass/programming interface, revision,
  header type, interrupt line/pin, and the BAR register values exposed by the
  device header type.
- Keeps up to 256 discovered functions in a kernel-owned read-only inventory.
- Prints the discovered BDF and class identity to the serial console.
- Exposes `pci_device_count()` and `pci_device_at()` for later driver matching.

## Safety and limitations

This stage does not enable bus mastering, alter command registers, size or
assign BARs, configure bridges, or initialize any device. The BAR values are
reported as raw register values; 64-bit BAR pairs are not yet decoded.
Configuration Mechanism #1 exposes the conventional 256-byte configuration
space. PCIe extended configuration-space access through ACPI MCFG/ECAM is a
later task. The inventory is capped at 256 functions in this prototype.

This is discovery, not a driver stack. NVMe/AHCI, xHCI, Intel graphics, Wi-Fi,
Ethernet, and audio drivers remain future work. QEMU boot tests do not prove
that every physical laptop controller is supported.
