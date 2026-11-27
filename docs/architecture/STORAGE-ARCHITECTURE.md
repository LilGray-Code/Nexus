# Nexus Storage and Filesystem Architecture

**Current increment:** block-device telemetry, MBR/EBR discovery, MBR FAT32 mount entry point, and a graphical storage inspection page.

This document separates functional paths from planned work. A discovered PCI controller is not proof of a working disk interface, and a partition-type identifier is not proof that a filesystem driver can read or write it.

## 1. Block-device architecture

Nexus has a central block-device registry with bounded device count, stable identity/model strings, block geometry, transfer limits, transport, and read/write/flush callbacks.

Current controller/transport paths in the repository:

- **NVMe:** PCI discovery, controller/namespace setup, admin and I/O queues, namespace block-device registration, bounded DMA transfer path. Production release mode registers the disk read-only plus install-write capability; ordinary writes remain subject to block-device policy.
- **SATA/AHCI:** PCI/ABAR setup, port engine and DMA structures, ATA IDENTIFY, bounded DMA reads/writes/flush, and read-only/install-capable registration. The ordinary data filesystem policy is separate from installer-write authorization.
- **USB mass storage:** USB Mass Storage Class Bulk-Only Transport, SCSI INQUIRY/TEST UNIT READY/READ CAPACITY, READ(10/16), REQUEST SENSE, BOT reset recovery and an install-capable block-device adapter. The xHCI host supplies endpoint transfers and reset/clear-halt.
- **GPT:** CRC-validated primary and backup header/entry-table parsing with partition bounds validation.

Block I/O telemetry in this increment counts successful read/write blocks, requests, flush requests and operation failures per registered disk. The desktop storage panel displays these counters. They are software I/O counters, not SMART/NVMe health logs.

## 2. MBR and EBR support

The new `mbr.c` parser validates:
- The 0x55AA MBR signature.
- Four primary partition entries and valid boot indicators.
- Non-zero partition types and ranges within the disk.
- Extended partition containers (0x05, 0x0F and 0x85).
- Bounded EBR chains (maximum 32 hops), range validation and cycle rejection.
- Protective GPT MBR identification, so the caller can use the CRC-validated GPT parser instead of misinterpreting a protective record as a legacy layout.

The VFS now exposes a FAT32 mount entry point for a validated MBR partition. This is a compatibility path, not an MBR formatter. The installer continues to create GPT layouts; MBR boot installation and extended-partition creation/editing are not implemented here.

## 3. Filesystem status

### FAT32

- Existing FAT32 mount/read implementation validates the BPB, cluster geometry, FAT bounds, long-file-name records and bounded directory chains.
- Existing VFS mounts it as a read-only source. Persistent user writes go to NXFS; they are not silently written to FAT32.
- The new MBR mount bridge can present an MBR FAT32 partition to the existing reader.
- The formatter under `installer/fat32_format.c` is used by the installer path. This does not mean arbitrary live FAT32 volumes can be safely reformatted by the desktop.

### NXFS (native Nexus filesystem)

The current on-disk NXFS implementation is a small, fixed-table filesystem intended for the system/data partition:
- Two independently checksummed superblocks.
- A checksummed metadata record for each table slot.
- A one-block redo journal for individual metadata updates/deletes.
- Two data banks per file slot: write new data to the inactive bank, flush and verify its CRC, then journal and publish the metadata record.
- Journal replay after an unclean shutdown, including read-only overlay replay when writes are not authorized.
- Streaming writes with bounded RAM use, nested directories, copy/move/delete and data CRC validation.
- Per-file content checksum and monotonically increasing entry generation.

Current structural limits matter:
- 64 metadata entries total (files and directories).
- Names are limited to 31 printable ASCII characters.
- The highest current per-file limit is 3 GiB for newly formatted volumes; the supported format tiers step down through 2 GiB, 1 GiB, 512 MiB, 256 MiB and smaller legacy sizes according to partition capacity.
- The format reserves two fixed-size data banks per metadata slot. A 3 GiB maximum tier reserves two banks for each of 64 slots and therefore needs roughly 384 GiB of file-bank space, plus metadata/journal overhead. Existing 256 MiB and earlier superblocks remain supported; the larger tiers are selected when formatting a volume with sufficient capacity. Larger partitions still do not automatically gain additional metadata entries or arbitrarily large files.
- The journal currently serializes one metadata-record transaction at a time; it is not a full write-ahead journal for multi-file transactions or concurrent writers.

The current implementation is crash-recoverable at its file-metadata commit boundary, but it should not be described as a general-purpose, high-scale filesystem until allocator/index/extent design and power-loss testing have advanced.

### Not yet implemented

- **exFAT:** no Nexus exFAT mount/metadata/write driver.
- **NTFS:** no Nexus NTFS mount/metadata/write driver.
- **Filesystem permissions/ACLs:** no persistent owner/group/mode/ACL model for NXFS.
- **Encryption:** no full-volume or per-file encryption/key management.
- **Snapshots and backup/restore utility:** no integrated backup catalog, snapshot or restore engine.
- **General-purpose filesystem cache:** no unified clean/dirty page cache with writeback and eviction; individual drivers currently use bounded I/O buffers.
- **SMART/NVMe health monitoring:** no ATA SMART, SCSI LOG SENSE health view, or NVMe SMART/Health log integration.

## 4. Graphical Disk Management

Settings now includes a **Storage** page showing:
- Registered block devices, model, transport, logical block size and approximate capacity.
- Validated GPT partition information, or MBR/EBR information when GPT is not present.
- Per-device read/write/flush operation telemetry and error counters.
- An explicit statement when neither partition map validates or when SMART telemetry is unavailable.
- A path to the existing installer for destructive partition/format operations.

The current Storage page is an inspection and navigation tool. It does not make an ordinary running desktop session capable of arbitrary disk erasure. The installer remains responsible for destructive disk operations and its existing target-identity, boot-media, mounted-volume, typed-ID and erase-phrase gates.

## 5. Safety boundaries

- Do not infer device writability from controller discovery alone.
- Do not allow a mounted data volume, system partition, ESP, boot medium or ambiguous disk identity to become an installer target.
- Keep GPT and filesystem writes behind the existing block-device policy.
- Do not repair or reformat unknown/corrupt filesystems automatically.
- A corrupt journal, failed flush, invalid GPT backup, out-of-bounds EBR, or ambiguous identity must be surfaced as an error, not "fixed" by guessing.

## 6. Roadmap

1. Add integration tests for MBR FAT32 mounting and real boot media selection when the bootloader reports an MBR signature.
2. Add a transactional extent allocator and larger NXFS versioned metadata format; retain a read-only v2 compatibility mount and an explicit offline migration tool.
3. Expand journaling to ordered multi-block transactions with sequence numbers, checksums, recovery markers and power-cut tests at every commit boundary.
4. Add a general buffer/page cache with explicit flush, ordering, eviction and device barriers.
5. Design NXFS ownership/mode/ACL metadata and enforce permissions at the VFS/system-call boundary.
6. Add supported exFAT and NTFS read-only drivers first; only add writes after conformance and crash-recovery suites exist.
7. Add real NVMe SMART/Health log and ATA SMART/SCSI health collection with vendor/model decoding.
8. Add authenticated backup/restore and key management before enabling encryption.
9. Add partition-table edit/format operations to the desktop only after a separately tested transaction/rollback plan and explicit destructive confirmation UI exist.
10. Validate NVMe, SATA and USB devices across physical machines and virtual devices, including hot-unplug, timeout, flush errors, corrupted media, large-capacity disks and unexpected power loss.
