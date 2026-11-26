# K1: Physical Memory and CPU Exception Prototype

K1 extends the K0 boot proof with two early kernel services.

## Physical memory

The kernel requests the Limine memory map and higher-half direct-map (HHDM)
offset. It validates and normalizes usable regions into an ordered list,
coalescing overlapping and adjacent ranges and keeping the first MiB reserved.
The early allocator supplies aligned 4 KiB pages, DMA-constrained pages, and
physically contiguous runs for devices such as paravirtual GPUs.

Individual pages and contiguous runs can be returned to a bounded reclaimed
page list. The allocator rejects unaligned, foreign, and duplicate frees and
validates region ordering, page ownership, and free-byte accounting. The region
list is capped at 256 entries and the reclaimed-page list at 8192 pages. This
remains a single-CPU bootstrap allocator: it is not yet a buddy allocator,
does not compact fragmented memory, and is not synchronized for concurrent
SMP allocation.

The allocator exposes a diagnostic snapshot containing total/free/used bytes,
region count, reclaimed-page count, largest remaining bump-allocation extent,
allocation/free request and failure counters, and cumulative page allocation
and reclamation counts. Boot diagnostics print the region count, largest
unallocated extent, reclaimed-page count, and allocation failures. The largest
extent deliberately reports only unallocated contiguous tails; it does not
pretend that individually reclaimed pages can satisfy a contiguous DMA request.

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
