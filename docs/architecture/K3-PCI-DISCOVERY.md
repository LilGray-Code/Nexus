# K3: Read-Only PCI Discovery

This first K3 increment enumerates PCI functions using PCI Configuration
Mechanism #1 through I/O ports CF8/CFC.

For each bus 0–255 and device 0–31, the scan reads function 0's vendor ID and
header type. It scans functions 1–7 only for multifunction devices. For each
present function it records:
- BDF address (bus/device/function)
- Vendor and device IDs
- Header type
- Base class, subclass, and programming interface

The kernel stores up to 128 discovered records and prints a bounded summary.
It counts storage, display, and network class functions as discovery hints;
these do not identify a complete driver or prove that attached hardware is
usable.

All access is read-only with respect to device configuration space. The only
write is the PCI configuration-address selector at port CF8; the selected
configuration dword is read from CFC. The scan does not write BARs, command
registers, bus-master enable bits, or device-specific registers. It does not
perform DMA or access PCIe extended configuration space via ACPI MCFG.

CI requires the PCI discovery marker under OVMF and SeaBIOS. This validates
the QEMU firmware path only. Physical Latitude 5410 PCI IDs must be collected
separately before NVMe, Wi-Fi, graphics, or USB drivers are selected.
