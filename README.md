# Nexus OS

Nexus OS is a Debian 13 (Trixie)-based amd64 desktop operating-system project. The current `main` branch builds a standalone installer ISO from the versioned source bundle in this repository.

> **Project status:** development build, not a production-ready operating system. A successful ISO build and firmware smoke check do not by themselves establish that every machine boots successfully or that the installed desktop is complete.

## Current build

- **Base:** Debian 13 (Trixie)
- **Architecture:** x86-64 / amd64
- **Image:** standalone installer ISO
- **Build source:** `NexusOS-Standalone-GitHub-Build-v5.tar.xz`
- **Automated build:** [Nexus OS GitHub Actions workflow](.github/workflows/nexus-build.yml)

The build workflow verifies the source archive checksum, extracts the build tree, builds in a Debian 13 container, verifies the ISO checksum, inspects the El Torito boot catalog, and runs bounded QEMU firmware-execution smoke checks for legacy BIOS and UEFI. Those QEMU checks currently establish only that the emulator/firmware session remains running for the observation window; they are **not** proof of a successful kernel boot, installer completion, or desktop startup.

## Getting a build

1. Open the [Actions page](../../actions/workflows/nexus-build.yml).
2. Select **Build Nexus OS ISO** and choose **Run workflow** on `main`, or use the artifact from a completed push-triggered run.
3. Open the completed run and download the `NexusOS-Standalone-amd64-ISO` artifact.
4. Extract the artifact ZIP. It contains:
   - `NexusOS-Standalone-amd64.iso`
   - `SHA256SUMS.txt`
   - `BUILD-INFO.txt`
5. From the extracted directory, verify the download with:

   ```sh
   sha256sum -c SHA256SUMS.txt
   ```

Artifacts are retained by GitHub Actions for 30 days. A build is downloadable only after the workflow completes successfully and uploads the artifact.

## Build environment

The CI build runs on Ubuntu 24.04 and invokes the project build scripts inside a `debian:trixie` container. The source bundle is checked against the SHA-256 value pinned in the workflow before extraction. The workflow is the canonical reproducible build entry point at this stage.

## Hardware compatibility goals

The target is general-purpose x86-64 PCs and laptops, not a single laptop model. Compatibility still depends on the hardware and firmware exposed to the Debian kernel and included drivers. Before treating a machine as supported, validate at minimum:

- UEFI boot and, where intended, legacy BIOS boot
- SATA/AHCI and NVMe storage detection
- Intel and AMD integrated graphics, plus common discrete GPUs
- USB keyboards, mice, hubs, and removable storage
- Wired Ethernet and common Wi-Fi adapters
- Audio, suspend/resume, battery reporting, and thermal management
- Installation, reboot, upgrades, and recovery on real hardware

No broad hardware-compatibility certification is claimed yet.

## Development priorities

1. Prove complete BIOS and UEFI guest boots with observable boot milestones, not merely a running QEMU process.
2. Verify installer operation, disk partitioning, installation, and first boot in clean virtual machines.
3. Exercise the installed desktop: login/session startup, window management, settings, networking, audio, and core applications.
4. Expand hardware coverage across representative Intel and AMD systems and common peripherals.
5. Add robust update, rollback, recovery, and release-signing procedures.
6. Publish a release only with the ISO, build manifest, checksums, and documented test results.

## Architecture note

The current `main` branch is the Debian 13-based distribution build described above. It is distinct from the earlier experimental original-kernel work on the `nexus-original-kernel-roadmap` branch. The current Debian-based ISO must not be described as the original Nexus kernel.
