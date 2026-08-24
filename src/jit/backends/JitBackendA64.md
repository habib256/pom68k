# AArch64 backend — implementation and porting note

## What it is today

`JitBackendA64.cpp` is a native 68040/68030 code generator, and `auto` selects
it for both families on AArch64. Plain memory goes
through the inline read/write DTLBs with REV16/REV32; refused mappings and
unsupported forms re-enter Moira at an exact instruction boundary. Peripheral
pacing keeps the guest clock and the deadline in callee-saved host registers
and calls a wrapper's due handler directly only after the inline deadline
comparison succeeds. External
exits probe the engine's direct-mapped link table and branch straight into an
already-compiled target — constant targets through a tag-checked slot,
dynamic `RTS`/`JMP` targets by computing the index and validating the runtime
PC.

**Its opcode set is close to the x86-64 backend's but is not assumed
identical.** `canEmitReg()` is the source of truth. The A64 backend includes
the immediate and guarded register-count line-$E shifts and rotates,
register/read-only-memory bitfields, indexed modes (including proved
full-format memory indirection for LEA/JMP/JSR/MOVE/read-only ALU sources),
`MOVE SR,Dn`, `Scc`, `PEA`, `EXG` and distinct-register `CMPM`. Its 040 MOVE
path can preflight a RAM destination before consuming an exact/MMIO source;
the token is deliberately absent on 030. Its 68030 paths carry split cost
validation, restartable writes and emitted i-cache accounting; the dedicated
long 030 lockstep and platform boots keep that automatic path gated.

**The peripheral-phase access-clock bias (JIT_BRINGUP § C.4nonies) is
carried by this backend since 2026-08-22.** `pom68kA64Read/Write` bias
the clock around an exact access thunk by the instruction's would-be
i-cache fetch penalty (`Moira::pomJitIcachePeekPenalty`), so a forced I/O
flush sees the same clock the interpreter's same access would; the
compile loop packs `fetchWords << 32 | pc` per instruction
(`gAccessPcWords`) and `memLoadGuest`/`memStoreGuest` hand it to the
thunk in x4. Before the port the class was NOT latent here, contrary to
what the x64 closure assumed: `guardIcacheHits` replayed every
thunk-capable instruction whose fetch the block shadow could not prove a
hit — correct, and paid in replays (17.0 M → 12.8 M on the 120k lockstep
once the bias took over; every other counter identical). The backend
declares `accessClockBias`, which resolves the § C.4nonies admission
defaults ON — and since the evening of the same day the emitter
CONSULTS them: the restartable-write family is admitted on the split
base cost (`restartBaseAdmission()`, the total-cost rule had been
hard-wired here) and BSR.W joins JSR d16(PC) in the armed-charge
exemption (`bsrWideAdmission()`). Under the total cost every push
traced on an i-cache miss was refused — 120 M of the idle Finder's
238 M in-block fallbacks; native share 49 → 71 % at 30 000 frames.
`jit_lockstep_030_a64_alignment_test` pins the explicit-knob road;
`jit_lockstep_030_a64_experimental_test` the default.

**Gates.** `jit_lockstep_a64_coarse_test` (5 M comparisons at 50 cycles,
registered only on AArch64 with `POM68K_JIT_BACKENDS=auto`),
`jit_lockstep_030_a64_experimental_test`, `jit_lockstep_030_a64_alignment_test`,
`jit_store_guard_a64_test`, `jit_restart_write_030_test`, and the arm64
native smoke inside `jit_backend_test` — which builds a `MOVEQ` /
`MOVE SR,D1` / `NOP` block by hand and runs it, so encodings, the Darwin
AAPCS frame, the MAP_JIT W^X transitions, I-cache invalidation and cycle
charging are covered without a user ROM.

### Measured (Apple Silicon, Release/native/LTO)

Fixed 1,000-frame Q605 workload: **1.22 s** against 4.55 s threaded
(3.73×, identical fingerprint); the same pair under LLVM PGO, 1.01 s against
3.41 s. The complete Finder gate: **9.19 s** against 21.14 s threaded
(2.30×), or 7.86 s against 15.28 s under PGO (1.94×), with identical
640×480, framebuffer and SCSI signatures.

**68030, Apple M1, 2026-08-22 (bias port + slice-index fix, relinked):**
`bench::compare` `threaded,a64` on the LC II at 6000 frames, ABBA,
3 repeats, quiet host (NULL floor measured 0.1 % the same hour):
27.37 s → **22.88 s, −16.4 %**, spreads 0.0/0.1 %, fingerprint
`cfb184b6faddabec` on both arms. The 2026-08-18/20 same-shape figures
("within 3 %", then −5.3 %) are earlier sessions and earlier binaries —
context, not a delta. Still open: the idle-Finder regime, where the soak
runs 3× slower than `threaded` (CHANGELOG 2026-08-22 (fourth)).

Current non-LTO Apple-M4 fixed budget (3,000 frames), three ABBA pairs:
**3.24 s median, 15.43× real time** against 29.24 s interpreter (**9.03×**),
**99.7 % native**, fingerprint `778dd7ad558108fd` in every arm. The
30,000-frame lifetime
budget sustains **92.0 % native and 5.51× real time** after generated-code
capacity recycling; before that correction the full buffer made native share
collapse to 51.7 % and throughput to 3.10×.

On the 68030 LC II fixed 6,000-frame budget, three same-process ABBA
repetitions measure a64 at **18.88 s** median against **19.93 s** for
`threaded` (−5.3 %, fingerprint `cfb184b6faddabec`, arm spreads 0.3/0.4 %).
That is the measured speed proof behind the AArch64 030 automatic path.

Two host-side costs dominated before those numbers, and both fixes are
host-neutral (so Raspberry Pi AArch64 gets them too):

* macOS `sample` put the time in `sys_icache_invalidate` — every block
  publication was flushing the entire 128 MiB code reservation. The shared
  `CodeBuffer` now invalidates only its newly emitted tail
  (`icacheSynced_`, `JitCodeBuffer.cpp:183-187`), which on Linux AArch64
  goes through `__builtin___clear_cache`.
* A second `sample`, during a full boot, put virtually every sample in
  `Engine::markPages`: libc++'s `unordered_multimap` scanned an
  ever-growing equal-key group when thousands of blocks shared one 256-byte
  slice, and the compile path indexed each block twice. The index is now
  `slice -> vector<block key>` (one hash lookup, O(1) append) and
  compilation no longer repeats recording's mark. The Finder gate fell from
  over 21 minutes before interruption to the 9.19 s above.

The bring-up also found a shared `CodeBuffer` bug worth not re-deriving:
after the first Apple-Silicon W→X transition, later transitions must only
toggle `pthread_jit_write_protect_np`. Repeating `mprotect(...RWX)` on the
same `MAP_JIT` mapping fails and leaves already-published code
non-executable (`execMapped_`).

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
same thread. `CodeBuffer::supported()` probes for real and then restores the
thread's W state, because that flag is thread-global and a "side-effect-free"
query that left the thread execute-only would break whatever buffer that
thread was writing.

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
