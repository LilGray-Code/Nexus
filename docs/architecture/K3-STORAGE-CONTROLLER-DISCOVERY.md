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
scanner. The scanner captures PCI command/status and raw BAR registers; the
BAR decoder distinguishes I/O versus memory space, 32-bit versus 64-bit memory,
prefetchability, and whether a non-zero base address is assigned. It does not
enable bus mastering, change PCI command bits, size or assign BARs, map
controller registers, issue ATA/NVMe commands, or access sectors. It does not mount filesystems and is not a disk driver.

The next storage milestones are controller-specific MMIO/resource validation,
an AHCI read-only identify path, an NVMe identify path, and then block-device
and filesystem layers. The current AHCI register probe only reads CAP, GHC,
PI, and VS from the assigned ABAR after checking PCI Memory Space Enable. It
does not issue commands, start ports, enable bus mastering, or write AHCI
registers. Each later stage needs separate tests before disk access.
