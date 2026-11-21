# K1: Physical Memory and CPU Exception Prototype

K1 extends the K0 boot proof with two early kernel services.

## Physical memory

The kernel requests the Limine memory map and higher-half direct-map (HHDM)
offset. It records usable memory-map regions and provides a page-aligned,
monotonic 4 KiB physical page allocator. Physical addresses returned by the
allocator can be translated through the bootloader-provided HHDM.

This is intentionally a bootstrap allocator, not a finished general-purpose
physical memory manager. It currently has no free operation, page ownership,
buddy allocator, virtual-memory mapper, heap, or reclamation of bootloader
memory. It caps the region list at 256 entries and keeps the first MiB reserved.

## CPU exceptions

The kernel installs IDT gates for x86-64 exception vectors 0 through 31.
Assembly entry stubs normalize CPU error-code and non-error-code exception
frames, save general-purpose registers, and hand the frame to a C diagnostic
handler. The current policy is to print vector, error code, and instruction
pointer to COM1 serial and halt. Hardware IRQ routing, APIC/PIC setup, timer
interrupts, user-mode transitions, and recoverable exception handling are not
implemented yet.

## Validation

The CI workflow must compile the kernel, create the BIOS/UEFI hybrid ISO,
verify its checksum, and boot it under both OVMF and SeaBIOS. A K1 pass is
accepted only when the serial log includes both
`NEXUS K1 MEMORY+EXCEPTIONS READY` and the existing K0 boot marker.

QEMU validation is not a substitute for testing on physical Dell Latitude 5410
hardware. The first physical test should remain a non-destructive USB boot.
