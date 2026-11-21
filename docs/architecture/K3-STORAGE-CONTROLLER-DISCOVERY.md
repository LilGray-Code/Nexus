# K3 — Storage Discovery and Read-Only Block Devices

## Status

Nexus discovers PCI mass-storage controllers and has a first read-only NVMe
block-device path. The generic UEFI/Q35 QEMU profile exercises NVMe; the
legacy BIOS/PC profile currently discovers IDE. AHCI register and port
discovery is implemented, but AHCI data commands are not.

## Common block-device interface

The kernel now has a small registry with a fixed maximum of eight devices.
A registered device describes its name, logical block count, block size,
maximum blocks per request, flags, driver context, and read callback.

The public read function rejects unregistered handles, null buffers, zero
lengths, out-of-range LBAs, requests extending beyond the namespace, and
requests above the device's per-request limit. Only devices explicitly marked
read-only can be registered or read. There is no write callback or write API.

The current implementation is an early kernel service, not yet a complete
storage subsystem: the registry is single-core, has no locking, no device
removal, no partition abstraction, and no filesystem integration.

## NVMe implementation

The NVMe path handles PCI class 01/subclass 08/programming-interface 02.
It validates an assigned aligned BAR0, enables PCI memory decoding and bus
mastering with a 16-bit PCI Command-register write, and maps controller and
doorbell registers.

It validates supported NVM command set, 4 KiB page size, queue depth, and
doorbell stride. It creates admin queues and issues Identify Controller and
Identify Namespace for NSID 1. It reports controller identity, namespace
capacity, and active logical block size.

It then creates one I/O completion queue and one I/O submission queue and
registers NSID 1 as `nvme0n1`. The driver accepts read requests of up to one
4 KiB page per request, copies data through a DMA32 bounce page, and serializes
requests using a single in-flight command. The queue uses polling, not
interrupts.

## Verification and disk safety

The QEMU CI self-test build is compiled with
`NEXUS_STORAGE_SELFTEST`. Its disposable 64 MiB NVMe image has distinct
signatures in LBA 0 and LBA 1; CI calls the common `blockdev_read` API twice
and verifies both signatures. This verifies sequential requests and completion
queue head advancement, without issuing any write opcode.

The distributable ISO is rebuilt separately without
`NEXUS_STORAGE_SELFTEST`. CI boots that image with an NVMe test device attached
and confirms the block device is registered but the automatic sector-read
self-test marker is absent. A normal build therefore does not automatically
read user disk sectors during boot.

No write operation, format, sanitize, or filesystem mount exists. The ISO is
still a kernel prototype and is not an installer or daily-use operating
system.

## Limitations and next work

The DMA allocator currently guarantees individual 4 KiB pages below a
caller-supplied physical-address limit. The NVMe prototype uses separate
pages; there is no general contiguous DMA allocator or IOMMU/VT-d isolation.
The driver currently supports one NVMe controller, NSID 1, polling mode, one
in-flight request, and a maximum transfer of one 4 KiB page. Error recovery,
controller reset, device removal, multi-namespace support, queue locking,
interrupt delivery, and concurrent I/O remain unfinished.

AHCI currently performs read-only register and port discovery only. Next
storage work includes broader request queueing and recovery, AHCI command
list/FIS/PRDT handling and ATA IDENTIFY, an explicit partition layer, GPT
parsing, and finally a read-only filesystem. Disk writes remain out of scope
until a separately designed and reviewed write path and destructive-test plan
exist.
