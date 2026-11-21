# K2: Timer Interrupt and Cooperative Scheduling

K2 now has two validated early-kernel capabilities:

## PIC/PIT timer interrupt

- The IDT has exception gates for vectors 0–31 and hardware IRQ gates for
  vectors 32–47.
- Assembly IRQ stubs normalize the hardware frame, preserve general-purpose
  registers, call the C interrupt dispatcher, restore the interrupted context,
  and return with `iretq`.
- The 8259 PIC is remapped to vectors 32–47. Only IRQ0 is unmasked.
- PIT channel 0 is configured for a nominal 100 Hz clock.
- Local APIC LINT0 is configured for ExtINT delivery from the legacy PIC.
  CPUs advertising x2APIC use the MSR interface; xAPIC uses an explicit,
  uncached MMIO page mapping. The kernel does not assume that adding the HHDM
  offset automatically maps device memory.

CI run [44871433999](https://github.com/LilGray-Code/Nexus/actions/runs/44871433999)
passed under both OVMF and SeaBIOS. Both serial logs reported five PIT ticks,
the K2 timer marker, and the K0 boot marker.

## Cooperative kernel threads

The kernel has a single-core cooperative scheduler prototype with four
statically allocated thread slots and 16 KiB stacks. An x86-64 assembly
context switch saves/restores callee-saved integer registers and RSP.
Two test threads yield explicitly; CI requires the trace A-B-A-B-A-B and
checks both counters reach three.

The cooperative scheduler is deliberately non-preemptive. Its boot test runs
with interrupts disabled. The PIT timer is independently verified but does
not yet drive the scheduler.

## Remaining K2 work

This is not a production scheduler or a complete interrupt architecture.
Outstanding work includes ACPI MADT parsing, IOAPIC support, APIC timer
calibration, interrupt-safe preemption, sleep/wakeup queues, scheduler
priorities, FPU/SIMD context policy, per-CPU state, SMP startup, run queues,
and stress tests on physical hardware.

The OVMF/SeaBIOS tests are emulator validation only. They do not establish
compatibility with a physical Dell Latitude 5410.
