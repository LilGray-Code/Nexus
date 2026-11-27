# Nexus update recovery runbook

**For the original-kernel Nexus branch.** This runbook describes the intended recovery behavior. It is not a claim that the production update service or A/B boot selector is already implemented.

## Non-negotiable rule

Do not format, repartition, repair destructively, or write to the Nexus Data partition while recovering an interrupted update. User files live on Data. If the state is ambiguous, preserve the disk and stop rather than guessing.

## Before starting

1. Record the exact machine, firmware mode (UEFI or legacy BIOS), disk model/capacity, and the displayed error.
2. Disconnect unrelated external disks so the recovery environment cannot select the wrong target.
3. Boot the Nexus Recovery entry or installer/recovery medium. Keep the machine on stable power.
4. Choose inspection/read-only mode first. Save the recovery report to external media if available.
5. Confirm that the report identifies the intended system disk and the separate Data volume.

## Recovery decisions

### Interrupted download or staging

The active system was never changed. Inspect the journal and inactive-slot staging directory. Resume only if the signed repository snapshot remains valid and every previously downloaded object passes signature and digest verification. Otherwise discard only that transaction's staging directory. Do not touch either active system files or Data.

### Pending reboot or failed trial boot

Inspect the active, pending, and fallback slot metadata and compare each slot manifest with the journal. If the pending generation failed its bounded trial boot, select the recorded fallback generation and verify the boot-selection record by reading it back. Keep the failed slot intact for diagnosis; do not delete it automatically.

### Journal copies disagree

Do not select a slot based only on a timestamp or a partially valid record. Preserve both journal copies, both system slots, boot configuration, and Data. Export the report and require a recovery implementation that can reconcile the record sequence and slot manifests. If it cannot prove a unique safe state, stop without writes.

### No bootable system generation

Use the offline recovery medium to inspect boot files and signed system manifests. Reinstalling the OS is not the first recovery action. If a reinstall becomes necessary, target only the system/boot partitions after verifying the disk identity and Data partition boundaries. Back up Data before any operation that could affect its partition table or filesystem.

## After recovery

1. Boot the selected known-good generation.
2. Verify the displayed OS version, kernel build ID, system manifest digest, and recovery outcome.
3. Verify that Data mounts and that known user files are present and readable.
4. Compare pre-recovery and post-recovery Data hashes when a pre-recovery inventory exists.
5. Preserve journal and recovery logs until the cause is understood.
6. Do not retry the same update until its signature, package, dependency, storage, or boot-health failure has been corrected.

## Current implementation boundary

The existing installer has a separate Recovery partition and tests for interrupted installation transactions on disposable QEMU disks. That installer recovery is not yet a general package-update recovery service. Production update recovery additionally requires durable NXFS writes, a redundant update journal, A/B slot selection, signature verification, trial-boot health checks, and physical power-loss/recovery qualification. See SYSTEM-UPDATES.md §12.
