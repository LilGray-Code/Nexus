# K3 — Storage Controller Discovery

## Status

Nexus now classifies PCI mass-storage controllers using the kernel's
read-only PCI inventory. This stage is discovery only.

## Detected classes

The current classifier recognizes SCSI, IDE, RAID, ATA, SATA/AHCI, SAS,
NVM Express (NVMe), and UFS class codes. For each controller it logs its
PCI bus/device/function address, vendor and device IDs, and programming
interface.

## Safety and limitations

The probe only reads the PCI inventory populated by the PCI configuration
scanner. It does not enable bus mastering, change PCI command bits, size or
assign BARs, map controller registers, issue ATA/NVMe commands, or access
sectors. It does not mount filesystems and is not a disk driver.

The next storage milestones are safe BAR decoding/resource reservation,
controller-specific MMIO mapping, an AHCI read-only identify path, an NVMe
identify path, and then block-device and filesystem layers. Each stage will
need separate tests before it can access or modify a disk.
