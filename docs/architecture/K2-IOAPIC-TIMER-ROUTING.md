# K2: I/O APIC Discovery and Experimental Routing Status

The kernel validates the ACPI MADT and maps I/O APIC MMIO pages as uncached.
It reads the controller ID/version and redirection-entry count without
changing any redirection entries. This read-only probe passed OVMF and
SeaBIOS.

A direct PIT IRQ0 route through the I/O APIC was attempted using the MADT
source override (QEMU maps ISA IRQ0 to GSI 2). The route was programmed, but
the first interrupt caused a General Protection Fault in QEMU. The direct
route is therefore **disabled in the active boot path**. The verified
single-core PIC/PIT virtual-wire path remains enabled and continues to pass
both firmware tests.

Do not treat the direct I/O APIC route as supported until interrupt entry,
vector selection, EOI, and return-frame tests prove it safe. No I/O APIC
redirection table is modified during the default boot.
