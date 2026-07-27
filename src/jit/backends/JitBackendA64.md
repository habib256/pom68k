# AArch64 backend — porting note (not implemented)

Written at J0, before any code generator exists, for one reason: to check
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

## 6. What is genuinely missing to write it

Nothing structural. The backend needs: an AArch64 emitter, a mapping from
`jit::Kind` to emitted sequences, and the same "anything not covered exits to
the interpreter at an instruction boundary" rule the x86-64 backend will use.
Registration is two lines in `src/jit/JitBackend.cpp` behind
`POM68K_JIT_BACKEND_A64`, and one branch in `CMakeLists.txt`.
