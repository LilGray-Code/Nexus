# K3 — Storage Controller Discovery and NVMe Identify

## Status

Nexus classifies PCI mass-storage controllers through PCI configuration-space
enumeration. AHCI/SATA register and port discovery is read-only. The NVMe admin queue implements Identify Controller and Identify Namespace.
It also creates one I/O queue pair and performs a single read-only block probe
against a disposable QEMU test image.

## Controller discovery

The classifier recognizes SCSI, IDE, RAID, ATA, SATA/AHCI, SAS, NVMe, and UFS
PCI class codes. For each discovered controller it logs the PCI
bus/device/function address, vendor/device IDs, programming interface, and
decoded BAR resources.

The AHCI probe reads CAP, GHC, PI, and VS from the assigned ABAR, then reads
SSTS and signature values for implemented ports that fit within the mapped
register page. It does not start an AHCI port or issue ATA/ATAPI commands.
The QEMU UEFI profile exposes AHCI; the legacy BIOS profile currently exposes
an IDE controller.

## NVMe Identify Controller

The NVMe path is limited to PCI class 01/subclass 08/programming-interface 02.
It validates an assigned aligned BAR0, enables PCI memory decoding and bus
mastering with a 16-bit PCI Command-register write (avoiding writes to the
adjacent write-one-to-clear Status register), and maps the controller and
admin doorbell pages.

It checks that the controller supports the NVM command set, 4 KiB memory pages,
at least 16 admin queue entries, and a doorbell stride supported by this first
implementation. It allocates and clears three separate 4 KiB DMA pages below
the 4 GiB boundary for the admin submission queue, completion queue, and
Identify data. It configures a 16-entry admin queue, masks interrupts for
polling, and sends NVMe Identify Controller (opcode 0x06, CNS=1). It then reads the
controller namespace count and, when nonzero, issues Identify Namespace for
NSID 1 (opcode 0x06, CNS=0). It validates NSZE and the active LBA format and
reports namespace capacity in logical blocks and logical-block byte size. It
then creates one I/O completion/submission queue pair and reads exactly LBA 0
from NSID 1 into a DMA page. CI seeds that LBA with a fixed signature and
requires the returned bytes to match.

The controller identity returned by the QEMU UEFI test is vendor 1B36,
model "QEMU NVMe Ctrl", serial "NEXUSNVME0001", firmware "8.2.2". The current
CI requires the Identify completion marker and model field.

## Safety and limitations

Identify Controller and Identify Namespace read controller/namespace
metadata only. The I/O path issues exactly one NVMe Read command for one
logical block at LBA 0 on the disposable QEMU namespace. No write opcode,
format/sanitize command, or other sector access is issued. It does not mount filesystems or expose an OS block device. The
QEMU test image is disposable and is not accessed at the namespace/sector
level.

The DMA allocator currently guarantees one 4 KiB page below a caller-provided
physical-address ceiling. NVMe currently uses three separate pages, not a
general contiguous DMA allocator. IOMMU/VT-d isolation, interrupt delivery,
controller reset/recovery, additional namespace-ID discovery beyond NSID 1,
request scheduling, timeouts
based on CAP.TO, and broader doorbell-stride support remain future work.
Bus mastering is enabled for the detected NVMe controller; no IOMMU mapping
or device-specific DMA isolation is implemented yet. Do not treat this
prototype as safe for untrusted PCI devices or as a production disk driver.

Next storage milestones are a common read-only block-device interface, AHCI command/IDENTIFY support, sector reads from explicitly
selected test media, and filesystem layers. Sector writes remain out of scope
until a separate, explicit storage-write design and test plan exists.
