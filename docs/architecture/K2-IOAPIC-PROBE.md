# K2: I/O APIC Read-Only Probe

The kernel now uses the ACPI MADT topology snapshot to map each discovered
I/O APIC MMIO page and inspect its ID and version registers.

The probe:
- Uses the explicit uncached MMIO mapper.
- Selects the I/O APIC ID register and reads it through the I/O window.
- Reads the version register and derives the number of redirection entries.
- Records/logs the firmware GSI base associated with the controller.
- Does not write any redirection-table entry and does not change IRQ routing.

CI requires the probe to succeed under OVMF and SeaBIOS. This validates
controller discovery and basic register access in emulators; it does not yet
route devices through the I/O APIC. Interrupt source overrides are parsed but
not applied. The existing PIC/PIT ExtINT path remains the active timer route.
