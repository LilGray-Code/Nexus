# K2: Cooperative Kernel Thread Prototype

This step adds a minimal single-core cooperative scheduler on top of the timer
and memory bootstrap.

## Implementation

- Four statically allocated thread slots, each with a 16 KiB aligned stack.
- A synthetic initial stack frame starts new C thread entries using the x86-64
  System V calling convention.
- An assembly context switch saves/restores callee-saved integer registers and
  RSP.
- Threads explicitly call `scheduler_yield()`; there is no timer preemption.
- A returning thread is marked finished and switched out. Finished stacks are
  never selected again.
- The boot thread resumes after all worker threads complete.

The boot test creates two workers. Each worker records three turns and yields.
The expected trace is A-B-A-B-A-B, with both counters equal to three.

## Deliberate limits

This is not yet a production scheduler. It is single-core, cooperative, and
runs with interrupts disabled during the context-switch test. There is no
preemption, sleep/wakeup, priority, wait queue, process abstraction, user mode,
address-space switch, FPU/SIMD context, per-CPU state, SMP startup, or
scheduler lock. The PIT tick remains an independently validated timer IRQ;
timer-driven preemption is deferred until interrupt-safe scheduler invariants
and full context preservation have been designed and tested.
