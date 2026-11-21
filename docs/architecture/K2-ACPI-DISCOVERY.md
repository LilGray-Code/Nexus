# K2: ACPI RSDP and MADT Discovery

The kernel requests the firmware RSDP through the Limine RSDP request and
performs read-only ACPI discovery after memory initialization.

It validates the RSDP signature and checksums, validates the XSDT (preferred)
or RSDT fallback, locates the APIC/MADT table, checks its checksum and length,
and walks variable-length MADT records with bounds checks.

The current topology snapshot records:
- Local APIC physical address, including a MADT address override
- Enabled or online-capable processor entries (legacy Local APIC and x2APIC)
- I/O APIC IDs, MMIO physical addresses, and GSI bases
- Interrupt source override records

Unknown MADT record types are skipped using their declared length. I/O APIC
registers are not accessed yet; this milestone only discovers and validates
firmware data. The fixed topology arrays are intentionally capped.

CI requires a valid MADT discovery marker in both OVMF and SeaBIOS logs. This
is emulator validation, not proof that every firmware implementation is
handled. Physical Dell Latitude 5410 ACPI tables still need separate review.
