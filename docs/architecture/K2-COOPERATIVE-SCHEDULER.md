# K2: Kernel Thread Scheduler

This stage extends the original Nexus x86-64 kernel scheduler with bounded
kernel workers, timer-driven preemption tests, and deadline-based sleeping.

## Implementation

- Sixteen statically allocated worker slots, each with a 16 KiB aligned stack.
- A synthetic initial stack frame starts new C thread entries using the x86-64
  System V calling convention.
- An assembly context switch saves/restores callee-saved integer registers and
  RSP. Interrupt frames remain on the suspended worker's own stack, allowing
  the timer-IRQ preemption path to resume that worker at its interrupted point.
- Round-robin selection skips sleeping and finished workers. Finished slots
  can be reused after the worker has switched away from its stack.
- The PIT tick calls `scheduler_timer_tick()`, which changes expired workers
  from sleeping to runnable. It does not context-switch inside the wakeup scan.
- `scheduler_sleep_ticks()` blocks a current worker until the requested
  deadline and yields to another runnable worker or the BSP main/idle context.
  The main boot thread deliberately cannot sleep yet because a dedicated
  per-CPU idle-thread implementation does not exist.
- Timer-driven preemption remains explicitly opt-in. The boot test enables it
  for CPU-bound workers and disables it again after validating the timer IRQ
  switch path.

## Boot validation

The boot tests cover:

- A-B-A-B-A-B cooperative round-robin ordering.
- Full sixteen-worker capacity, overflow rejection, and finished-slot reuse.
- A worker sleeping for three PIT ticks while an observer worker receives CPU
  time; the sleeper must resume only after its deadline.
- Timer-driven switching between two CPU-bound workers without explicit yields.

## Deliberate limits

This is still a kernel-thread scheduler prototype, not a complete process
scheduler. It is single scheduling domain on the BSP; the APs run their SMP
work loop separately. There are no priorities, cancellation, wait queues,
per-CPU idle threads, process/address-space context switching, FPU/SIMD context
switching, scheduler-aware blocking I/O, or scheduler locks suitable for SMP.
The synchronous user-process runner and user-mode sleep syscall are not yet
integrated with worker blocking. General preemption must remain disabled around
kernel subsystems that are not proven re-entrant or interrupt-safe.
