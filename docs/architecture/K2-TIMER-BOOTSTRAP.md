# K2: Legacy PIC/PIT Timer Bring-up

K2 adds the first hardware interrupt path on top of K1.

- The IDT has exception gates for vectors 0–31 and hardware IRQ gates for
  vectors 32–47.
- Assembly IRQ stubs normalize the hardware frame, preserve general-purpose
  registers, call the C interrupt dispatcher, restore the interrupted context,
  and return with `iretq`.
- The 8259 PIC is remapped to vectors 32–47. Only IRQ0 is unmasked.
- PIT channel 0 is configured for a nominal 100 Hz clock. The kernel enables
  interrupts briefly, waits for at least five timer ticks, then reports the
  count over COM1.
- On CPUs advertising x2APIC, the kernel enables x2APIC mode and configures
  Local APIC LINT0 for unmasked ExtINT delivery from the legacy PIC. It uses
  the x2APIC MSR interface; it does not assume the bootloader's HHDM maps
  Local APIC MMIO.

The initial implementation exposed an important distinction: adding an
HHDM offset to the Local APIC physical address does not establish a page-table
mapping. The kernel therefore avoids xAPIC MMIO access until it has its own
verified MMIO mapping facility. CPUs without x2APIC support are reported as
unsupported by this early timer route rather than being subjected to an
unmapped register write.

This is an early single-processor compatibility path. It is not the final
interrupt architecture: ACPI MADT parsing, IOAPIC routing, APIC timer
calibration, PCI interrupt routing, SMP startup, per-CPU state, run queues,
preemption, and a scheduler remain outstanding. The PIC/PIT path is retained
as a legacy fallback while those facilities are developed.

The CI workflow uses QEMU's maximum supported CPU model and must build the
hybrid ISO, verify its checksum, and receive timer ticks under OVMF and
SeaBIOS. QEMU validation is not a substitute for physical Dell Latitude 5410
testing.
