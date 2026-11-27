# Nexus ISO release engineering

The ISO workflow is a release gate, not just a kernel compilation job. A build
is not published as a downloadable artifact until the ISO filesystem and
payloads are inspected, the installer deployment path is exercised on a
disposable image, the installed target boots in UEFI and legacy BIOS QEMU, and
the installer ISO itself boots in both modes.

## Local build

On Ubuntu 24.04, install the host tools:

```sh
sudo apt-get update
sudo apt-get install -y build-essential binutils curl git xorriso \
  qemu-system-x86 ovmf file dosfstools mtools
```

From the repository root:

```sh
chmod +x build/build-iso.sh
EXTRA_CFLAGS="-DNEXUS_INSTALLER_ENABLE_DESTRUCTIVE_WRITES" \
  ./build/build-iso.sh
```

The script fetches pinned Limine, Mbed TLS, and CA-bundle inputs, builds the
actual freestanding x86-64 kernel with TLS support, builds the first-party
native application SDK, packages the read-only Nexus system pack, constructs
a hybrid BIOS/UEFI ISO, installs the Limine BIOS stage, then verifies the
completed image.

## Release contents

The distributable image is
`release/NexusOS-OriginalKernel-x86_64.iso`. It contains:

- Limine BIOS and UEFI boot components and the installer-only boot menu.
- The just-built `boot/nexus-kernel.elf`, including the kernel-integrated
  desktop shell, twelve-category Settings UI, Files/Terminal views, text
  browser, networking/TLS, firmware discovery, and compiled drivers.
- `NEXUS/SYSTEM.NXP`, a CRC-checked read-only system pack with its manifest,
  app index, and static Nexus-native ELF examples.
- A release manifest listing the kernel source/object groups and hashes.
- A build-info file and an installer/recovery note.

Settings, the desktop shell, and the text browser are currently integrated
into the kernel; they are not separately packaged GUI executables. Driver
sources are built into the kernel. The image does not bundle proprietary
vendor firmware blobs. The manifest describes what is actually built and does
not imply universal support for all listed hardware families.

## Required release checks

`build/release.py` performs the post-build validation:

1. Checks ISO9660 primary volume descriptor and sector alignment.
2. Uses xorriso to verify the El Torito catalog has bootable BIOS and UEFI
   entries and to extract every required file from the finished image.
3. Compares the extracted kernel and system pack byte-for-byte with the
   build outputs and verifies the installed boot menu is the installer menu.
4. Checks the linked x86-64 ELF and expected exported desktop, Settings,
   browser, firmware, filesystem, and installer/recovery symbols.
5. Checks the NXSPACK1 header, bounds, table and payload CRCs, app manifest,
   and bundled x86-64 ELF examples.
6. Writes exact byte size, MiB size, build commit, kernel/system-pack hashes,
   and ISO SHA-256 to `release/BUILD-INFO.txt`.

The GitHub Actions release workflow adds these runtime gates:

- BIOS and UEFI QEMU boots of the distributable installer ISO.
- Installer storage deployment to a disposable disk image using the built
  kernel, bootloaders, and system pack.
- FAT32 EFI System Partition and installed Limine configuration checks.
- UEFI and legacy-BIOS QEMU boots of the installed target.
- SHA-256 verification of the ISO and release metadata.

The build produces `release/SHA256SUMS.txt`, `release/BUILD-INFO.txt`, and
`release/RELEASE-MANIFEST.TXT` beside the ISO.

## Downloading the tested ISO

Open [Nexus Distributable Original-Kernel ISO Actions](https://github.com/LilGray-Code/Nexus/actions/workflows/nexus-distributable-iso.yml),
select a successful run for the desired commit, and download the artifact named
`NexusOS-OriginalKernel-x86_64-<commit>`. Extract the artifact ZIP to obtain
the ISO and its verification files. Artifacts are retained for 90 days; a
failed or cancelled workflow does not publish a boot-tested artifact.

Verify the ISO before writing it to a USB drive:

```sh
sha256sum --check SHA256SUMS.txt
```

The artifact is a downloadable ISO, not a source archive. The release workflow
also retains the kernel, system pack, and QEMU serial logs as supporting
evidence.

## Support boundary

Passing the release workflow establishes the recorded QEMU/host behavior; it
does not certify real PCs, every BIOS/UEFI implementation, every storage
controller, Secure Boot, or every third-party driver. Physical machine
validation remains a separate requirement. The installer erases and
repartitions its selected target disk: test on a disposable disk or VM and
back up user data before installing.
