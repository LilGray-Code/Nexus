# NXFS v3 on-disk format proposal

**Status:** design proposal only. The kernel currently mounts/formats NXFS v2. The v3 layout calculator is a read-only planner; it does not write, mount, or allocate a v3 volume. Do not use this document as evidence that v3 is ready for installation.

## Goals

- Use the full eligible capacity of the GPT data partition.
- Allocate blocks dynamically instead of reserving two maximum-size banks for every fixed directory entry.
- Support large files, fragmented files, hierarchical directories, and more than 64 filesystem objects.
- Recover committed metadata transactions after a reset or interrupted write.
- Detect damaged metadata and never silently reformat or overwrite an unknown filesystem.
- Keep all partition-relative offsets and capacities 64-bit.

## Version 3 layout

All offsets are relative to the start of the NXFS GPT partition. All integers are little-endian. The supported logical block sizes for the first implementation are 512 and 4096 bytes.

| Region | Relative blocks | Purpose |
|---|---:|---|
| Primary superblock | 0 | Version, geometry, UUID, sequence, CRC |
| Backup superblock | 1 | Independent copy for recovery |
| Journal | 2–257 | Bounded redo log and commit records |
| Allocation bitmap | 258 onward | One bit per data block; 1 means allocated |
| Inode table | After bitmap | 16,384 fixed 128-byte inode records |
| Data region | After inode table | Dynamically allocated file and directory extents |

The layout planner calculates the bitmap size from the data region and uses checked 64-bit arithmetic. The exact bitmap size and data start are stored in both superblocks. The final data block must be strictly within the partition. A formatter must verify all arithmetic and write/read-back metadata before it reports success.

The current prototype uses a 256-block journal and 16,384 inodes. These are initial format parameters, not a promise that every future volume must retain those limits. Changing them requires a new format version or compatible feature flags.

## Superblock fields

The v3 superblock is one logical block and includes:
- magic NEXUSFS3, format version, header length and logical block size;
- filesystem UUID and monotonically increasing superblock sequence;
- partition block count and all region start/count pairs;
- inode record size and inode count;
- clean/dirty mount state and last committed journal sequence;
- feature flags and required-feature flags; and
- CRC32 over the defined header, with reserved bytes required to be zero.

A mount validates each copy independently. If one is valid, the volume can be mounted read-only while recovery is assessed. If both are invalid, or valid copies disagree on immutable geometry, the driver must refuse the mount and must not attempt an automatic format.

## Inodes and extents

Each 128-byte inode record has a generation-qualified inode number, object type, mode/attributes, owner identifiers, timestamps, logical size, allocated-block count, link count, extent-root reference, directory metadata, and record CRC. Unused records have a defined zero state.

Files use an extent tree. Each extent maps a contiguous run of logical file blocks to a contiguous run of physical data-region blocks. Extent nodes carry level, count, generation and CRC. All extent arithmetic must be checked against both file size and data-region boundaries. A file can therefore grow beyond the old 1 MiB NXFS v2 limit without reserving a maximum-sized region in advance.

Directories are files containing bounded, checksummed directory records that map UTF-8 names to generation-qualified inode references. Path lookup must reject malformed UTF-8, dot/dot-dot traversal above the root, duplicate names, cycles, stale inode generations and paths exceeding configured limits.

## Allocation

The allocation bitmap covers only the data region. Bit zero describes the first data block. Allocation scans may use in-memory hints, but the bitmap is the persistent source of truth. Free-space counts are advisory and must be recomputed or verified against the bitmap at mount/recovery.

An allocation transaction must reserve bitmap bits before publishing an inode or extent that references those blocks. On rollback, uncommitted reservations must be released. The allocator must reject requests that exceed free space, the representable extent count, or the maximum supported transaction size.

## Journal and crash recovery

The journal is a bounded redo log. Each transaction has a sequence number, record count, payload lengths, per-record checksums and a commit record whose checksum covers the transaction. The commit record is persisted and flushed before any transaction is acknowledged to the caller.

On mount:
1. Validate superblocks and immutable geometry.
2. Scan the journal for complete, checksummed transactions newer than the superblock's last committed sequence.
3. Replay complete committed transactions in sequence order.
4. Ignore incomplete/uncommitted tails without exposing partially updated metadata.
5. Flush replayed metadata, advance the superblock sequence, and clear/reclaim journal records only after the new superblock is durable.

The first implementation must bound each transaction to the journal capacity. Large writes are split into data writes plus smaller metadata transactions; file data must be flushed before metadata publishes references to it. The journal must never contain arbitrary unbounded user data.

## NXFS v2 compatibility and migration

- Detect v2 by its existing magic/version and mount it through the v2 driver.
- Keep v2 read/write behavior unchanged until v3 has an independently tested driver and migration path.
- Migration is explicit, user-confirmed, and requires a verified backup.
- Never upgrade a v2 volume in place as a side effect of booting or installing.
- A migration tool copies files and directory structure into a separately formatted v3 volume, verifies content hashes, and only then offers a cutover.

## Implementation gates

1. Layout calculator tests for small, 64 GiB, multi-terabyte and 4Kn volumes.
2. Host-side codec tests for superblocks, inode records, extent nodes, directory records and journal transactions, including malformed and overflow inputs.
3. Mock-block-device tests for allocation, fragmentation, full-volume behavior, and data integrity.
4. Fault injection after every write and flush boundary, proving committed operations replay and uncommitted operations do not appear.
5. QEMU tests on disposable NVMe and SATA images, including repeated reboot and simulated power-loss recovery.
6. Read-only v2 compatibility fixtures and explicit migration tests.
7. Only after all gates pass may the v3 formatter be enabled in an installer build. Never enable it in a release build based only on host tests.
