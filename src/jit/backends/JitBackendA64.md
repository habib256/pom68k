# AArch64 backend — implementation and porting note

## Current implementation (2026-08-04)

`JitBackendA64.cpp` now covers the same 68040 opcode families advertised by
the x86-64 backend and adds long branches, modifying bit operations,
immediate shifts/rotates and constant register bitfields. Plain memory uses
the inline read/write DTLBs with REV16/REV32;
refused mappings and unsupported forms re-enter Moira at an exact boundary.
The Q605 lockstep passes 5 million comparisons (and a coarse-budget run
exercises complete multi-instruction blocks).

Peripheral pacing keeps the guest clock and deadline in callee-saved host
registers and calls synchronization only when a configured batch is due.
External exits probe the engine's direct-mapped link table and branch straight
to an already-compiled target. On the Q605 (whose batch is one), the exact
direct synchronization path remains cheaper than adding a redundant deadline
test.

The per-access read thunk and brief indexed addressing raise native retirement
to 97.3 % on the fixed 1,000-frame Q605 benchmark. Hidden CPU/peripheral-state
instrumentation found the first differences behind the former late-boot
failure and localized four targeted emitter bugs: NEG register materialization,
EA-commit scratch clobbering, short immediate masking and BFINS mask lifetime.
Five-million-step fine and budget-50 locksteps now pass, as does the complete
Q605 Finder boot; AArch64 is therefore the automatic arm64 backend.

macOS `sample` then exposed the dominant host cost in `sys_icache_invalidate`:
each block publication flushed the entire 128 MiB code reservation. The shared
`CodeBuffer` now invalidates only its newly emitted tail, which also benefits
Linux AArch64/Raspberry Pi through `__clear_cache`. On Apple Silicon the fixed
Release/native/LTO workload measures 1.22 s A64 versus 4.55 s threaded (3.73x),
and the existing LLVM PGO build measures 1.01 s versus 3.41 s; all four runs
produce the same guest fingerprint.

A second `sample` during the complete boot found virtually every sample in
`Engine::markPages`: libc++'s `unordered_multimap` scanned an ever-growing
equal-key group when thousands of blocks shared a 256-byte code slice, and
the compile path indexed each block twice. The index is now
`slice -> vector<block key>` (one hash lookup plus O(1) append) and compilation
does not repeat recording's mark. The complete Release Finder gate fell from
more than 21 minutes before interruption to 9.19 s, against 21.14 s threaded
(2.30x), with identical 640x480, framebuffer and SCSI signatures. This engine
fix is host-neutral and therefore applies to Raspberry Pi AArch64 too.
The corresponding PGO Finder measurement is 7.86 s versus 15.28 s (1.94x).

The implementation also exposed and fixed a shared `CodeBuffer` bug: after
the first Apple-Silicon W→X transition, later transitions must only toggle
`pthread_jit_write_protect_np`; repeating `mprotect(...RWX)` on the same
`MAP_JIT` mapping fails and leaves already-published code non-executable.

## Porting rationale

Written at J0, before the code generator existed, for one reason: to check
that `jit::BlockIr` and `jit::Backend` hold up against a **second**
architecture. If a contract only works for x86-64, that is a design error we
want to find while the IR is still cheap to change — not after an x86-64
backend has been written against it.

Nothing here is speculative about POM68K's own structure: the engine, the
code window, the block tracer, the write guard and the gauges are all
architecture-blind already. What follows is the list of things an AArch64
backend must do **differently from x86-64**, and where each one is already
accounted for.

## 1. Condition codes — the reason CCR is explicit in the IR

x86-64 and AArch64 disagree about the flag both of them call "carry":

| Operation | x86 CF | AArch64 C |
|---|---|---|
| `a - b`, no borrow | 0 | **1** |
| `a - b`, borrow | 1 | **0** |

The 68k `X` bit exists on neither, and 68k `V` on a shift is not host `V`.
A backend that "keeps the host flags live across instructions" therefore
cannot be written once and reused. This is why `jit::Instr` carries
`FlagSetsCcr` / `FlagUsesCcr` / `FlagSetsX` instead of leaving condition
codes implicit: each backend materialises the 68k CCR its own way, and the IR
says exactly when it has to.

## 2. Instruction cache — mandatory, unlike x86

x86-64 snoops stores against the instruction cache; AArch64 does not. Code
written through a data mapping is invisible to the fetcher until the range is
flushed. `jit::CodeBuffer::makeExecutable()` already calls
`__builtin___clear_cache` / `sys_icache_invalidate` on every non-x86 host —
see `JitCodeBuffer.cpp`. Omitting it produces the worst class of bug: one
that only shows up when the allocator happens to reuse an address.

## 3. W^X on Apple Silicon

macOS on arm64 requires `MAP_JIT` at `mmap` time and a **per-thread**
`pthread_jit_write_protect_np()` toggle around every write to the buffer.
Already implemented in `JitCodeBuffer.cpp`, including the toggle right after
`mmap` (a fresh MAP_JIT mapping is not necessarily writable in the calling
thread). Note the per-thread part: POM68K runs each machine on its own
thread, so a buffer written by the machine thread must be toggled by that
same thread.

## 4. Big-endian guest on a little-endian host

Identical problem on both targets, and the reason `jit::Instr` records
operand sizes rather than leaving byte order to whatever the host load
instruction happens to do. AArch64 has `REV16`/`REV32`, x86-64 has
`BSWAP`/`MOVBE`; the IR says *that* a swap is needed, the backend picks how.

## 5. Register allocation and ABI

Entirely inside the backend, and deliberately so. AArch64 has more callee-
saved general registers than x86-64 (x19–x28 vs rbx/rbp/r12–r15), which
changes how many 68k registers are worth pinning — a backend-local decision
that the IR must not, and does not, encode.

## 6. Pacing and block linking

Like x86-64, AArch64 keeps the guest clock and peripheral deadline in host
registers, calls synchronization only when due, and links hot block exits
through the engine's direct-mapped table. Constant targets use a tag-checked
slot directly; dynamic RTS/JMP targets hash and validate the runtime PC.
Uncompiled targets and tag collisions return through the normal epilogue.
