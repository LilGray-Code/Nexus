# K2: I/O APIC Timer Routing

The kernel uses the MADT interrupt-source override for ISA IRQ0 when present,
finds the I/O APIC covering that GSI, and configures its redirection entry to
deliver vector 32 to the bootstrap Local APIC.

The route is installed while interrupts are disabled. The implementation
determines the Local APIC destination ID via xAPIC MMIO or x2APIC MSRs,
masks the chosen redirection entry, programs its destination and vector,
applies ACPI polarity/trigger flags, and then unmasks it. It masks Local APIC
LINT0 ExtINT and the legacy PIC inputs after direct routing is configured.
The timer interrupt handler sends EOI to the Local APIC for this route.

If direct routing cannot be configured, the earlier PIC/PIT virtual-wire
route remains available as a fallback. CI requires direct routing to work
under OVMF and SeaBIOS before accepting this revision.

This is one timer route only. Other ISA and PCI interrupts, multi-controller
routing, APIC timer calibration, interrupt-safe preemption, SMP, and physical
hardware validation remain outstanding.
