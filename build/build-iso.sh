#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

LIMINE_VERSION="12.10.0"
LIMINE_SHA256="365ba8785d4e4b55c3368738a642681b31f606afec9f77179e07fc26ebe841e2"
PROTOCOL_COMMIT="877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157"

mkdir -p deps build release
# The exact Limine protocol header is vendored in kernel/include/limine.h.
# Keep the source commit recorded here for reproducibility.
curl --fail --location --retry 3 \
  "https://github.com/Limine-Bootloader/Limine/releases/download/v${LIMINE_VERSION}/limine-binary.tar.gz" \
  -o build/limine-binary.tar.gz
echo "${LIMINE_SHA256}  build/limine-binary.tar.gz" | sha256sum --check -

rm -rf build/limine-binary
tar -xzf build/limine-binary.tar.gz -C build
make -C build/limine-binary

make -C kernel clean all

ISO_ROOT="$ROOT/build/iso-root"
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot/limine" "$ISO_ROOT/EFI/BOOT"
cp build/nexus-kernel.elf "$ISO_ROOT/boot/nexus-kernel.elf"
cp boot/limine.conf "$ISO_ROOT/boot/limine/limine.conf"
cp build/limine-binary/limine-bios.sys "$ISO_ROOT/boot/limine/"
cp build/limine-binary/limine-bios-cd.bin "$ISO_ROOT/boot/limine/"
cp build/limine-binary/limine-uefi-cd.bin "$ISO_ROOT/boot/limine/"
cp build/limine-binary/BOOTX64.EFI "$ISO_ROOT/EFI/BOOT/"
# Keep installer payload paths identical to the Limine module request order.\ntest -s "$ISO_ROOT/EFI/BOOT/BOOTX64.EFI"\ntest -s "$ISO_ROOT/boot/limine/limine-bios.sys"\ntest -s "$ISO_ROOT/boot/nexus-kernel.elf"

xorriso -as mkisofs \
  -R -r -J \
  -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  "$ISO_ROOT" \
  -o release/NexusOS-OriginalKernel-x86_64.iso

build/limine-binary/limine bios-install release/NexusOS-OriginalKernel-x86_64.iso

(
  cd release
  sha256sum NexusOS-OriginalKernel-x86_64.iso > SHA256SUMS.txt
  sha256sum --check SHA256SUMS.txt
)
file release/NexusOS-OriginalKernel-x86_64.iso
