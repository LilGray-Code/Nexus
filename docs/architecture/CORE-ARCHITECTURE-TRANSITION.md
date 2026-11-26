# Nexus Core Architecture Transition

**Status:** Architecture decision and implementation contract  
**Target branch:** `nexus-original-kernel-roadmap`  
**Implementation branch:** `nexus-core-architecture-transition`  
**Scope:** x86-64 kernel, task/process model, virtual memory, syscall boundary, IPC, dynamic linking, SMP scheduling

This document records the next architecture step for Nexus. It is deliberately separate from the release checklist: documenting a design or passing a host test does not mean that the feature is integrated into the booted kernel.

## 1. Architecture decision: keep the Nexus-owned kernel

### Decision

Continue the original-kernel program. Do not replace the runtime kernel with Linux as part of this milestone.

The project README and original-kernel roadmap define Nexus as an independently developed operating system with a Nexus-owned kernel and userland. A Linux-kernel migration would provide a much larger existing driver, scheduler, virtual-memory, networking, and graphics ecosystem, but it would also change that product decision and turn the current kernel into a prototype or compatibility project rather than the product kernel.

Use Linux and other mature kernels as **technical references and comparative benchmarks**, not as runtime dependencies. Revisit this decision only through an explicit product-level change request that accepts the consequences for the independent-kernel objective.

### Consequences

- Continue using Limine as the reviewed BIOS/UEFI bootloader; Limine is not the OS kernel.
- Keep the Nexus kernel, syscall ABI, userland, native application SDK, desktop, and installer first-party.
- Prefer established, audited libraries and portable components where licensing and ABI boundaries permit; do not recreate cryptography, compression, fonts, or protocol implementations merely to claim they are custom.
- Do not claim Linux, POSIX, or Windows binary compatibility until the relevant ABI and conformance tests actually pass.
- Retain the existing development ISO and build output as development artifacts. This architecture work does not authorize destructive installation or a release-ready claim.

## 2. Source baseline and verified limitations

The active branch contains kernel sources and a freestanding build system. The old binary-only audit is no longer the complete view of the repository.

The following baseline is visible in the current source:

| Area | Present foundation | Important limitation |
|---|---|---|
| Boot | x86-64 Limine path for BIOS and UEFI | Firmware boot and physical hardware support still require separate validation |
| Memory | Physical page allocator, DMA zones, page-table management, user address-space helpers | No complete general-purpose VM subsystem, demand paging, or per-process memory lifecycle |
| Protection | CPL3 entry, user/supervisor page permissions, NX and ELF segment permission checks | Full process fault isolation and fault-to-process termination are not yet established |
| Kernel scheduling | Fixed-size kernel-thread table, stackful context switch, timer tick and wakeups | Preemption is explicitly gated and only enabled in an isolated self-test; the scheduler is not a general per-CPU process scheduler |
| SMP | Secondary CPU startup and IPI-based parallel work dispatch | APs do not yet own independent task run queues or schedule user/kernel tasks |
| Processes | ELF64 loading, address-space creation, CPL3 entry, syscall return, exit status | Process execution is synchronous; a foreground caller waits until the user program exits |
| Syscalls | Versioned initial `int 0x80` ABI | Syscall state and several service buffers are global bootstrap state, not isolated per process |
| IPC | No complete user-visible IPC contract | No general pipes, message queues, shared-memory handles, or waitable process events |
| Dynamic linking | ELF64 executable and static-PIE loading foundation | No complete dynamic interpreter, shared-object dependency loader, or relocation runtime |

Relevant current implementation files include:

- `kernel/src/scheduler.c`, `scheduler.h`, and `context.S`
- `kernel/src/process.c` and `process.h`
- `kernel/src/paging.c` and `paging.h`
- `kernel/src/usermode.c`, `usermode.S`, and `syscall.h`
- `kernel/src/interrupts.c`, `timer.c`, and `smp.c`
- `kernel/src/elf64.c` and `elf64.h`

Specific constraints that must shape the implementation:

1. The current kernel scheduler has a fixed table of 16 threads with 16 KiB stacks. Timer-driven preemption can be enabled for a dedicated CPU-bound thread test, but is disabled outside that test because allocator, VFS, driver, and process state are not yet generally safe to preempt.
2. The current process table is fixed at four records. `process_run()` changes one process to RUNNING, enters user mode, and does not return until that process exits. It then destroys the address space.
3. The user-mode bootstrap has shared file-descriptor slots and global staging/output buffers. Those cannot safely serve simultaneously scheduled processes.
4. The SMP implementation brings secondary CPUs online for bounded parallel work, but those CPUs do not run a general scheduler.
5. The existing context switch saves the callee-saved kernel register set. A general preemptive user-task switch must preserve the complete interrupt/trap frame, per-task kernel stack, address-space root, and extended processor state.

These are architectural boundaries, not cosmetic missing features. Increasing static table sizes alone would not remove them.

## 3. Target execution model

Separate **processes** from **threads**.

A process owns resources shared by its threads:

- A reference-counted address-space object and page-table root.
- A handle/file-descriptor table.
- Process identity, parent/child relationship, credentials and permissions.
- A working-directory and environment representation once those services exist.
- Exit state and waitable completion events.
- IPC endpoints and mappings owned by the process.

A schedulable thread/task owns execution state:

- A unique task ID and a pointer to its process.
- RUNNING, RUNNABLE, BLOCKED, SLEEPING, STOPPED, or ZOMBIE state.
- Saved general-purpose registers and a complete interrupt-return frame.
- Its own kernel stack with a guard page when the VM layout supports it.
- User stack pointer and instruction pointer through its saved frame.
- Scheduling priority, time-slice accounting, CPU affinity, and wait reason.
- Its own FPU/XSAVE state, with lazy/eager policy chosen only after correctness tests.

Multiple threads in one process share the process address space but have independent kernel stacks, user stacks, register contexts, and scheduling state. Separate processes must not share user mappings unless an explicit shared-memory object authorizes it.

The bootstrap desktop may initially remain a special kernel task. It must not be treated as proof that ordinary applications are already independent processes.

## 4. Scheduler v2

### 4.1 Preserve the existing scheduler as a bootstrap compatibility layer

Do not abruptly remove the tested K2 worker-thread behavior. Keep the existing scheduler test path until the new task scheduler has equivalent or stronger tests.

Introduce a versioned scheduler interface behind a small internal boundary. Migrate one caller at a time. The new interface must distinguish:

- Creating a kernel thread.
- Creating a user thread inside an existing process.
- Making a blocked task runnable.
- Blocking on a wait queue.
- Sleeping until a monotonic deadline.
- Exiting a task and reaping its resources.
- Requesting rescheduling from interrupt context.

### 4.2 Save and restore full task context

The new switch path must preserve all architecturally required state. A timer interrupt that interrupts a user task already provides an interrupt frame containing the return state; the scheduler must retain that frame with the task rather than merely saving the current C function's callee-saved registers.

The interrupt-exit path should select the next runnable task and return through that task's saved frame. Do not perform arbitrary scheduling while holding a driver, VFS, allocator, or run-queue lock. Use a deferred reschedule flag and a well-defined interrupt nesting/return policy.

On a task switch:

1. Verify that the destination task is runnable and its resources are alive.
2. Switch to its kernel stack and address-space root.
3. Apply required TLB invalidation or address-space-generation handling.
4. Restore its FPU/XSAVE state according to the selected policy.
5. Restore its saved register/trap frame and return at the correct privilege level.

Kernel threads use the kernel address space. User tasks use their process address space with only the intended supervisor kernel mappings shared.

### 4.3 Per-CPU scheduling

Introduce per-CPU scheduler state:

- Current task and idle task.
- Local runnable queue and queue lock.
- Reschedule-needed flag and interrupt nesting count.
- Local timer accounting and load statistics.
- CPU online/accepting-work state.

Start with a simple round-robin or priority round-robin policy with explicit fairness tests. Only add work stealing, affinity tuning, real-time classes, or load balancing after the single-CPU scheduler is stable.

The BSP and each AP must be able to run independent kernel tasks. AP startup must transition from the current IPI work loop into the same scheduler lifecycle only after per-CPU GDT/TSS, IDT, local APIC timer, interrupt stack, and scheduler state are initialized.

### 4.4 Preemption and synchronization

Preemption must not be enabled globally until the following rules are enforceable:

- Spinlocks have a documented acquire/release contract.
- A task cannot sleep while holding a spinlock.
- Locks shared with interrupt handlers use an interrupt-safe locking discipline.
- Allocator, VFS, process, descriptor, and driver state have explicit synchronization ownership.
- Per-CPU data is accessed only through the current CPU's stable slot.
- IRQ handlers do bounded work and defer slow work to worker tasks.
- Rescheduling from an interrupt is deferred to the common interrupt-exit path.

Use a monotonic timer and a per-CPU local APIC timer for general scheduling. Keep the current PIC/PIT route as a bring-up fallback until the local APIC timer and IOAPIC routing have dedicated tests on both QEMU firmware modes and representative physical hardware.

## 5. Process lifecycle and memory isolation

Replace synchronous process execution with a lifecycle that supports concurrent processes:

`NEW -> LOADING -> RUNNABLE -> RUNNING <-> BLOCKED -> EXITING -> ZOMBIE -> REAPED`

The exact state machine may be refined, but every transition must have one owner and defined failure cleanup.

### 5.1 Address spaces

- Keep the kernel half supervisor-only and consistently mapped.
- Give each process a distinct lower-half user address space.
- Validate canonical ranges, permissions, overflows, and page ownership for every mapping.
- Keep executable pages non-writable and writable data pages non-executable by default.
- Add reference-counted physical frames and page-table pages.
- Add safe user-copy helpers that validate the complete range before copying and never blindly dereference user pointers in ring 0.
- Implement per-CPU TLB invalidation and cross-CPU TLB shootdown before an address space or page can be reclaimed.
- Add guard pages for user and kernel stacks.
- Define a controlled page-fault path: recoverable kernel faults remain kernel bugs; user faults terminate or signal the faulting process without halting the machine.

Do not implement demand paging, copy-on-write, or fork-like cloning until mapping lifetime, reference counting, TLB invalidation, and failure unwind are proven.

### 5.2 Per-process resources

Move current global user-mode state into explicit process-owned objects:

- File descriptors and open-file state.
- Console/output routing.
- Input/event subscriptions.
- Audio and game-controller handles.
- Temporary syscall staging buffers.
- Working directory and process metadata.

Every syscall must validate the calling task's process, permissions, handle ownership, and user-memory range. A failure in one application must not corrupt the desktop or another application.

### 5.3 Process control

Add a minimal, versioned process API with spawn/exec, exit, wait, get-process-info, and signal/terminate semantics. A process that exits must wake its parent/waiters, release descriptors and IPC endpoints, and reclaim address-space resources only after no thread can still reference them.

Start with spawn-and-wait. Add fork-like semantics only if a concrete application requirement justifies the complexity.

## 6. System-call ABI evolution

Preserve syscall ABI version 1 for existing Nexus test applications while adding a documented ABI v2 for the new process model. Do not silently change existing syscall numbers, structure layouts, or error conventions.

ABI v2 should define:

- Stable integer widths, alignment, structure sizes, reserved fields, and version negotiation.
- Process and thread operations.
- Handle-based file and device I/O.
- Blocking and nonblocking calls with documented EINTR/EAGAIN-like behavior.
- IPC and waitable event operations.
- Virtual-memory mapping operations once the VM implementation is ready.
- Monotonic clock and deadline-based sleep.
- Per-process resource and permission checks.

Keep `int 0x80` as the compatibility entry while the new ABI is validated. A later fast-entry path may use the x86-64 `SYSCALL/SYSRET` mechanism, but only after STAR/LSTAR/FMASK setup, kernel-stack switching, register clobbers, user-canonical address checks, and return-to-user security tests are complete.

## 7. IPC and synchronization primitives

Build IPC on top of process-owned handles and scheduler wait queues.

Initial order:

1. Kernel wait queues and wake-one/wake-all semantics.
2. Bounded message queues with explicit ownership and backpressure.
3. Unidirectional pipes with independent read/write endpoints.
4. Event objects for process exit, input readiness, and service notifications.
5. Shared-memory objects with explicit mapping and teardown rules.

Blocking IPC must put the current task to sleep without busy-waiting. Closing an endpoint must wake blocked peers with a documented error/EOF condition. Queue capacity, message size, cancellation, and resource exhaustion must be bounded and tested. Do not expose raw kernel pointers as user handles.

## 8. Dynamic linking and native userland

Keep static ELF applications working while adding dynamic loading in a separate milestone.

Required stages:

1. Harden ELF64 parsing, segment bounds, alignment, overlap, and W^X checks.
2. Support position-independent `ET_DYN` main executables with documented relocations.
3. Implement a user-mode ELF interpreter/loader or a minimal trusted loader with a stable ABI.
4. Resolve `DT_NEEDED` dependencies from a controlled library search path.
5. Apply only supported x86-64 relocations; reject unknown relocation types safely.
6. Implement TLS module metadata, initial thread-local storage, and a documented `auxv`/startup stack.
7. Add shared-library lifetime rules, reference counts, and RELRO where supported.
8. Provide a versioned Nexus C library and native SDK conformance suite.

Keep user-space ELF loading policy out of the kernel where practical. The kernel should provide process creation, mapping, file handles, and protection primitives; a user-mode loader should resolve application dependencies. Do not claim generic Linux ELF or glibc compatibility merely because an ELF64 file can be parsed.

## 9. SMP and hardware architecture

Before general SMP scheduling:

- Finish per-CPU GDT/TSS and interrupt-stack provisioning.
- Initialize and validate a local APIC timer on every online CPU.
- Ensure IDT and exception state is valid on every CPU.
- Add atomic and memory-ordering rules to shared kernel objects.
- Implement TLB shootdown IPIs and wait for completion before freeing mapped pages.
- Add CPU hotplug policy only after static online/offline lifecycle is sound.
- Keep device interrupts routed to one known CPU initially; add affinity changes later.

Do not equate a successful parallel-for test with a multi-core operating-system scheduler.

## 10. Implementation sequence and exit gates

### A0 — ABI and invariant freeze

Deliver a written kernel ABI, lock rules, per-CPU layout, page ownership rules, task/process structures, and state-transition tables.

**Exit:** Code review finds no ambiguous ownership of global syscall state, scheduler state, or address-space lifetime.

### A1 — Scheduler safety substrate

Add per-CPU state, run-queue primitives, interrupt nesting, preempt-disable counters, deferred rescheduling, and a full saved-context representation.

**Exit:** Host model tests and QEMU tests prove round-robin fairness, no lost wakeups, safe timer preemption, task exit, and idle behavior.

### A2 — Concurrent kernel threads

Move kernel workers onto the new scheduler, add blocking wait queues, and prove that timer preemption works without explicit yields while allocator/VFS test workloads run.

**Exit:** Repeated multi-worker stress tests pass with lock assertions and no hangs, corruption, or missed timer ticks.

### A3 — Concurrent isolated user processes

Give each process a complete process object and each user thread a separate task context. Move file descriptors and syscall state out of globals.

**Exit:** Two user programs run concurrently; one can block while the other runs; a bad user pointer or page fault terminates only the offending process; one process cannot read or modify another process's private mappings.

### A4 — IPC and process supervision

Implement wait queues, pipes, event handles, parent/child wait, and process cleanup.

**Exit:** Separate user processes exchange data, block and wake correctly, close resources, and survive peer exit without leaks or deadlocks.

### A5 — Dynamic loader and shared runtime

Implement the user-mode loader, shared objects, relocations, TLS, and native library ABI.

**Exit:** A PIE application dynamically links against at least one Nexus shared library, starts with correct TLS, and passes relocation, permission, and unload tests.

### A6 — SMP scheduler

Enable independent per-CPU task scheduling, timer interrupts, safe queue migration, and TLB shootdown.

**Exit:** Multiple CPUs execute unrelated tasks concurrently; stress tests verify affinity, migration, inter-processor wakeups, and address-space teardown.

## 11. Required automated tests

Add tests to the original-kernel CI workflow and preserve serial evidence for QEMU BIOS and UEFI runs.

Minimum tests:

- Thread creation, context restore, round-robin fairness, sleep/wakeup, and finished-slot reuse.
- CPU-bound preemption without explicit yields.
- Preemption while stress-testing allocator and VFS operations.
- No scheduling while spinlocks or interrupt-sensitive critical sections are held.
- Per-process CR3 switching and user/kernel page permission checks.
- Cross-process read/write isolation and guard-page faults.
- User pointer overflow, unmapped range, read-only range, and canonical-address rejection.
- Per-process file-descriptor isolation, close-on-exit, and descriptor exhaustion.
- Two simultaneously runnable user processes with distinct kernel stacks and saved registers.
- Parent wait, process exit, fault termination, cleanup, and PID reuse safety.
- IPC queue full/empty, blocked reader/writer wakeups, endpoint close, and peer exit.
- ELF dynamic relocation correctness, unknown relocation rejection, W^X, TLS, and shared-object dependency failures.
- SMP TLB shootdown completion and task scheduling on more than one CPU.

Host tests prove pure logic only. QEMU tests prove behavior in the tested virtual hardware. Neither replaces physical hardware testing.

## 12. Release and safety rules

- Keep the original-kernel branch buildable throughout each stage.
- Do not enable global preemption by changing one Boolean; first eliminate unsafe shared state and add synchronization.
- Do not increase user process limits until process and thread objects are dynamically or safely bounded and resource cleanup is tested.
- Do not add a dynamic linker before reliable VM, mapping lifetime, and syscall isolation exist.
- Do not call the system production-ready because a single static ELF runs in ring 3.
- Do not publish a new ISO as installable until BIOS/UEFI boot, virtual-disk install, installed-target boot, recovery, and destructive-write safety gates pass.
- Every claimed feature must name its test, test environment, and known limitations.

**Next implementation priority:** A0 followed by A1. The current kernel already proves useful early foundations, but concurrent isolated user processes must be built on a safe per-CPU scheduler and complete interrupt context—not on the current synchronous foreground execution path.
