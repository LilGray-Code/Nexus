# Nexus K0 — Original Kernel Boot Prototype

This is the first executable implementation of the Nexus-owned x86-64 kernel.

## What this prototype proves

- A freestanding x86-64 ELF kernel can be compiled independently of Linux and FreeBSD runtime kernels.
- Limine loads the kernel through its boot protocol.
- The kernel initializes COM1 serial output and reports a boot marker.
- If a supported 32-bit RGB framebuffer is supplied, the kernel paints a simple Nexus splash.
- The same hybrid ISO is exercised under OVMF UEFI and SeaBIOS legacy BIOS in CI.

## What it does not prove

This is not a usable desktop OS yet. It has no memory allocator, process isolation, scheduler, storage driver, filesystem, networking, USB stack, Wi-Fi, audio, graphics acceleration, or installer. The bootloader is third-party software. The kernel currently relies on the bootloader's initial CPU and paging setup and halts after the startup test.

## Build locally on Ubuntu 24.04

\`\`\`sh
sudo apt-get update
sudo apt-get install -y build-essential binutils curl git xorriso file
chmod +x build/build-iso.sh
./build/build-iso.sh
\`\`\`

Output: \`release/NexusOS-OriginalKernel-x86_64.iso\`

## Test

GitHub Actions boots the image twice: OVMF (UEFI) and SeaBIOS (legacy BIOS). Both serial logs must contain \`NEXUS K0 BOOT COMPLETE\`. Passing CI proves the prototype boots in those QEMU firmware configurations; it does not prove physical Latitude 5410 compatibility.

## Dell Latitude 5410

The next hardware step is a non-destructive USB live boot. Do not use this prototype to install or repartition a disk. It has no installer or storage-writing code.
