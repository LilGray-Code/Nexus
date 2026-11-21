# K2: Legacy PIC/PIT Timer Bring-up

K2 adds the first hardware interrupt path on top of K1.

- The IDT now has exception gates for vectors 0–31 and hardware IRQ gates
  for vectors 32–47.
- Assembly IRQ stubs normalize the hardware frame, preserve general-purpose
  registers, call the C interrupt dispatcher, restore the interrupted context,
  and return with `iretq`.
- The 8259 PIC is remapped to vectors 32–47. Only IRQ0 is unmasked in this
  milestone; the other IRQ lines remain masked.
- PIT channel 0 is configured for a nominal 100 Hz clock. The kernel enables
  interrupts briefly, waits for at least five timer ticks, then disables
  interrupts and reports the count over COM1.

This is an early single-processor compatibility path. It is not the final
interrupt architecture: Local APIC and IOAPIC discovery, APIC timer support,
interrupt routing for PCI devices, SMP startup, per-CPU state, run queues,
preemption, and a scheduler are still outstanding. The legacy PIC/PIT path
will be retained as a fallback while ACPI/APIC support is developed.

The automated validation still needs to build the hybrid ISO and boot both
UEFI/OVMF and BIOS/SeaBIOS. A successful QEMU timer tick test demonstrates
that the kernel received and returned from its PIT interrupt; it does not
validate physical Dell hardware.
