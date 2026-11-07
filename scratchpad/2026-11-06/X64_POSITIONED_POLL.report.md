# x64 positioned-IPL-poll audit (cacheless 68040), read-only, 2026-11-06

Scope: whether `JitBackendX64.cpp` has the hole A64 closed on 2026-10-02
(`POM68K_JIT.md` § 8, "Cacheless forms are not exempt"). This was a code
reading only. Nothing was built or run.

## 1. How the interpreter polls (Moira, 68040 path)

* `POLL_IPL` is `pollIpl()` (`MoiraMacros.h:67`). With `iplDelay4 == 0`
  (the default) it does `reg.ipl = ipl; return;` (`Moira.cpp:1293`). When
  the tracer is on, it also packs `pomJitTiming.dataAccesses` into one
  nibble per poll (`Moira.cpp:1281-1289`).
* `setIPL(val)` changes only the **pin** (`ipl = val; flags |= CHECK_IRQ;`,
  `Moira.cpp:1257-1268`). It does not touch `reg.ipl`.
* The interrupt decision is `checkForIrq()`: `if (reg.ipl > reg.sr.ipl ||
  reg.ipl == 7) execInterrupt(...)`. Otherwise it clears CHECK_IRQ only when
  `reg.ipl == ipl` (`Moira.cpp:794-817`). It runs at the top of the slow
  path of `execute()`, before the next instruction's `mmu040InstrStart`
  (`Moira.cpp:559-628`).
* `mmu040InstrStart()` begins with `POLL_IPL;` (`MoiraExecMMU_cpp.h:2031`).
  This is the head poll.
* `mmu040Read`/`mmu040Write` contain **no** poll. The comment says so
  ("no poll fires inside this call, so counting on entry equals counting on
  return", `MoiraExecMMU_cpp.h:3127-3131`). A device access does move time
  inside it: board `stall()`/`catchUp()` → `flushTicks` → device → `setIPL`.
* On the 040 core (`Core::C68020`), `SYNC(x)` expands to nothing in both
  macro branches (`MoiraMacros.h:19,29`). The only charge is
  `CYCLES_68020` at the end of the handler, after the handler's polls.
* Memory-source ALU, `execAndEaRg` (`MoiraExec_cpp.h:688-711`):
  `readOp<…>(src,&ea,&data); … writeD(dst,result); … else { POLL_IPL; …
  prefetch<C>(); }`. So `AND.B d16(A1),D0` (`$C029`, which matches the doc)
  polls once at the head and once **after its only data read**. That is
  `iplPolls=2`, `iplPollMask=0b1`. The same shape appears in
  `execAddEaRg:343`, `execCmp:2153`, `execCmpa:2197` and `execAdda:422`.
  Other poll sites are `execMove2/3` (`3065`/`3186`, before the write for
  non-memory sources, so position 0), `execLink`, `execPea`, `execCmpm`,
  `execAddx/Abcd` and `execMovep`.
* Result for "AND.B d16(A1),D0 whose read raises the pin":
  1. head `reg.ipl = old`;
  2. the read raises `ipl`;
  3. the post-read poll sets `reg.ipl = new`;
  4. at the next `execute()`, `checkForIrq` takes the interrupt **before
     N+1**.

  Without step 3, `checkForIrq` sees the stale `reg.ipl` and keeps
  CHECK_IRQ (`reg.ipl != ipl`). N+1's head poll then latches the level, and
  the interrupt lands after N+1. That is one instruction late.
* RMW and memory-destination forms (`AND Dn,<ea>` etc.) have no extra poll
  in the list above (inference from the poll-site map). MOVE with a register
  source polls *before* the write (position 0), so the A64 rule refuses it
  (mask 0 ≠ 1<<(n-1)).

## 2. What A64 emits now (`JitBackendA64.cpp`, block emit loop ~4776-4853)

```cpp
bool lateIplPoll = false;
if (!L.is030 && in.memory.count != 0 && in.iplPolls > 1) {
    const uint8_t n = in.memory.count;
    lateIplPoll = (!L.cache040Live || cache040LatePollEnabled()) &&
        in.iplPollPosValid && n >= 1 && n <= MemoryContract::MaxAccesses &&
        in.iplPollMask == uint8_t(1u << (n - 1));
    if (!lateIplPoll) { watchRefusal(..."positioned-ipl-poll"); a.b(slowStatic[i]); continue; }
}
...
if (in.kind == Kind::Branch) continue;
if (lateIplPoll) { a.ldrB(11, 0, L.iplPin); a.strB(11, 0, L.regIpl); }
if (icache) chargeIcache(...); commitQueue(...); chargeAndRetire(...);
```

* Admission: the instruction is 040, has memory and has more than one poll.
  It is native only if every non-head poll comes after the **final** access.
  With the cache off this always applies. With the cache on it needs
  `POM68K_JIT_040_LATE_POLL=1`. Any other placement takes the static stub
  (the interpreter). `VariableCount` (0xFF) fails `n <= MaxAccesses` and is
  refused.
* Emission: the body is emitted as usual. After the body and before the
  i-cache charge, queue commit and cycle charge, it adds one
  `reg.ipl = ipl` byte copy. A64 emits no per-instruction head poll; it
  relies on "flags==0 ⇒ reg.ipl==ipl" (comment at ~4811).
* A64 *reached* the VIA case because its `admitSoleReadTiming` has a 040
  split: `const bool exact040 = !L.is030 && in.cycles >= fixedCycles; if
  (exact040) read.exactRequired = true;` (`JitBackendA64.cpp:1303-1318`).
  This admits a sole read whose trace includes a device wait.

## 3. What x64 emits (`JitBackendX64.cpp`)

* Refusal, in `Emitter::emit()` (~5014). It is guarded only by cache-live:
  ```cpp
  if (!L_.is030 && L_.cache040Live &&
      ir_.instrs[i].memory.count != 0 && ir_.instrs[i].iplPolls > 1) {
      a_.jmp(staticStub(i)); ...
  ```
  With the cache off (the product default), a multi-poll memory instruction
  goes on to `pollIpl(); … emitInstr(i)` (5030-5034).
* Head poll, `Emitter::pollIpl()` (1010-1013): `mov al,[iplPin]; mov
  [regIpl],al`. This is emitted at the head of **every** instruction. No
  other write to `regIpl` exists in x64: grep finds only these two lines.
* Data access, `Emitter::memLoad` (2027-2085). If the inline DTLB probe
  misses (MMIO), it goes cold to `pom68kJitRead` (130-139). That calls
  `cpu->pomJitReadData` (`MoiraExecMMU_cpp.h:2447-2469`), which calls
  `mmu040Read<C68020,…>(addr,true)` then `pomJitCache040Publish`. This is
  Moira's own accessor. It can raise the pin, but it **does not poll**
  (§1). So (b), "a correct reg.ipl as a side effect of the exact thunk", is
  **false**: the poll belongs to the handler, not to the accessor.
* After the body: `chargeIcache` (030 only), `commitQueue`,
  `chargeCycles`, `retire()` (5058-5081). There is no re-sample.
* Next boundary: `guards(i+1)` (1406-1411) tests `[flags] != 0`, so it
  takes `flags_[i+1]`, which is `Exit::CpuFlags` with pc = N+1 (5116-5120).
  If N ended the block, `leaveTo` goes to the linked block, whose
  `guards(0)` does the same.
* Engine (`JitEngine.cpp:1295-1303`): `if (!cpu_.pomJitIdle()) {
  cpu_.execute(); … }`. `execute()` then runs `checkForIrq()` with the
  **stale** `reg.ipl` (no interrupt) and executes N+1 (head poll). The
  interrupt is taken after N+1. This is exactly A64's pre-fix trace.
* `exactRequired` (`JitIr.h:807, 904, 1009, 1589`) is set only under
  `is030` (`0x1029/0x1429`, `0x0829`, `0x4A11`, and `0x137C` with ext
  `$1A00`). It plays no part on the 040.
* **Why `q605_afp_live_etalon` is green on x64.** The VIA read is never
  native there, because the cost admission refuses it:
  * Q605 `viaAccess8` calls `flushTicks(); viaSync();`
    (`Q605Memory.cpp:236-238`). `viaSync` stalls to
    `via_eclock::syncTarget(c)`, defined as "the middle of the NEXT E-clock
    cycle, plus one" (`ViaEClock.h:38-44`). That target is always > c, so
    every VIA access adds roughly 17-48 machine cycles. `Instr::cycles` is
    the traced clock delta (`JitEngine.cpp:919-926`), so it includes that
    stall.
  * `emitAluEaRg` requires `unsigned(cost) == traced030(i)`, and
    `traced030` returns `in.cycles` on the 040 (577-582, 2677).
  * x64's `admitSoleReadTiming` returns false unless `L_.is030 &&
    read.exactRequired` (589-598). x64 has no counterpart to A64's
    `exact040` split.
  * So `$C029` on a VIA address is refused at emission and runs through
    `staticStub` → Moira, with the correct post-read poll.
  * The same holds for the other 040 boards: Centris, Q700 and Q630 all use
    the same `viaSync`, and SCSI (3) and SWIM (5) stalls are also nonzero.
  * A64 differs only because `exact040` admitted the stalled read.
* Unresolved puzzle (it does not change the verdict):
  `runExactSourceMove` (`jit_asset_free_lockstep_test.cpp:1327-1363`)
  asserts `slowInstrs == 0` for `MOVE.B (A0),(A1)+` from a device that adds
  3..6 cycles. By reading alone I could not see which x64 path admits a
  delayed 040 MOVE source: `tracedMove` = `traced030` = `in.cycles`, and x64
  does not set `preflightedExactSource`. Whatever it is, it only affects
  which delayed reads x64 takes native. It does not add a re-sample: there
  is no other `regIpl` store.

## 4. Verdict

**x64 has the same hole.** It is latent and not reached by the current
Q605 gates. It becomes reachable when a cacheless 68040 block natively emits
a memory instruction with `iplPolls > 1` and post-final-access poll(s), and
that instruction's exact-thunk access raises the IPL pin above the SR mask.

On today's 040 boards the main trigger (VIA/SCC/SCSI/SWIM reads) always
stalls, so the cost check keeps it off the native path. Paths that could
still reach it (inference, not verified):
* zero-wait reads whose handler re-evaluates an IRQ line, such as the Q605
  plain pseudo-DMA window `scsiDmaRead_()` (`Q605Memory.cpp:412-417`) or ASC
  reads (`:419-420`);
* any future x64 port of A64's `exact040` timing split, which would open the
  VIA case immediately.

Confidence: high (~90 %) on the mechanism, since the code path is fully
traced: no `regIpl` write after the body, the thunk has no poll, and the
guard exits with CpuFlags to `execute()`. The remaining uncertainty is
whether some untraced emitter admission already makes a stalling MMIO read
native (see the puzzle above). The asset-free case below settles it.

Key lines:
* `JitBackendX64.cpp` `Emitter::emit()`:
  `if (!L_.is030 && L_.cache040Live && …iplPolls > 1)`. The refusal is
  cache-only.
* `JitBackendX64.cpp` `Emitter::pollIpl()`: the only `L_.regIpl` store,
  emitted at the instruction head.
* `MoiraExecMMU_cpp.h` `mmu040Read`: "no poll fires inside this call".
* `MoiraExec_cpp.h` `execAndEaRg`: `readOp…; … POLL_IPL;`.
* `Moira.cpp` `checkForIrq`: `if (reg.ipl > reg.sr.ipl || reg.ipl == 7)`.

## 5. Asset-free oracle: a new case in `tests/jit_asset_free_lockstep_test.cpp`

There is no separate `PortableEnv` class for this. The test's machine is
`SyntheticCpu` (lines 174-284), a `moira::Moira` subclass with 1 MiB RAM.
Device reads are virtual `read8/read16` overrides on `inDevice(a)`
(`kDevice=$500000..+$FFF`). The `hooks()` `dataSpan`/`codeSpan` return
nullptr for device addresses, so the JIT always takes the exact thunk there.
`setIPL` is public (`Moira.h:2350`). The interrupt mode defaults to
`IrqMode::AUTO` (vector 24+level, `Moira.cpp:1316`). No new hook is needed,
only two new branches in the existing overrides.

**Rule: the side-effecting read must add NO clock.** If it does, x64's cost
check refuses AND and the case proves nothing. The existing device branch
adds 3..6 cycles.

Extension (SyntheticCpu):
```cpp
static constexpr uint32_t kIrqDev = kDevice + 0x800;   // inside inDevice() → no host span
bool irqArmed = false; uint8_t irqLevel = 2; uint32_t irqReads = 0, irqAcks = 0;
// read8, BEFORE the generic device branch:
if (a == kIrqDev) { auto& c = *const_cast<SyntheticCpu*>(this);
    c.irqReads++; if (c.irqArmed) c.setIPL(c.irqLevel); return 0xFF; }  // zero wait
// write8, BEFORE the generic device branch:
if (a == kIrqDev) { c.irqAcks++; c.setIPL(0); return; }                  // ack, zero wait
```

Program (`installLateIplLoop`):
```
installVectors(c);
put32(c, 0x68, kIrqHandler);           // vector 26 = level-2 autovector; kIrqHandler = 0x1A00
kCode+0x00: C029 0800                  AND.B $0800(A1),D0   (A1 = kDevice → kIrqDev)
kCode+0x04: 5281                       ADDQ.L #1,D1
kCode+0x06: 5282                       ADDQ.L #1,D2
kCode+0x08: 60F6                       BRA.S kCode
kIrqHandler+0x00: 26EF 0002            MOVE.L 2(A7),(A3)+   ; log stacked PC
kIrqHandler+0x04: 13FC 0000 0050 0800  MOVE.B #0,$00500800  ; ack → setIPL(0)
kIrqHandler+0x0C: 4E73                 RTE
```

Setup, applied to both `ref` and `native` (`resetCpu` gives TC=0 and
CACR=0, so the cache model is off, which is the configuration under test):
* `setA(1,kDevice)`, `setA(3,kData)`, `setD(0,0xFF)`, `setD(1,0)`,
  `setD(2,0)`, `setSR(0x2000)` (supervisor, mask 0);
* `native.jit.setEnabled(true)`.

Run:
1. Warm up, unarmed: about 16 checkpoints of `executeUntil(clock+37)`. The
   trace then records the full 4-instruction looped block (if it were armed,
   the trace would end at AND with `EndReason::ControlFlow`).
2. Set `irqArmed = true` on both machines.
3. Run 64 checkpoints, comparing after each:
   * `sameCpu(ref,native,true)` (D/A, PC, PC0, SR, clock, IRD/IRC, USP,
     ISP, MSP);
   * `sameMemory(ref,native,kData,256,true)` (the stacked-PC log);
   * `sameMemory(ref,native,kStack-64,64,true)`;
   * `irqReads` and `irqAcks` equal.

Expected:
* Interpreter: every logged PC is `kCode+4`.
* x64 today (predicted): the first armed interrupt logs `kCode+6`, `D1` is
  one higher, and the clock differs, so it fails at the first armed
  checkpoint.
* A64 (predicted): passes with the late re-sample.

Proving it ran native:
* `s = native.jit.stats().snapshot()`, and require `s.blocksCompiled &&
  s.blocksRun && s.slowInstrs == 0`. A static or runtime stub for AND would
  increment `slowInstrs` (x64 frame `+52`, `JitBackendX64.cpp:174, 5583`).
* Also require `nativeInstrs = s.instrs - s.interpInstrs - s.traceInstrs -
  s.windowInstrs - s.slowInstrs > 0`. Handler and RTE are Unsafe, so they
  count as interp, not slow.
* Also require `native.irqReads > ref-trace-count`.
* For the diagnosis run only, `POM68K_JIT_VERBOSE=1` prints `[jit] block
  $00001100  4 instr, 4 native`.

Selecting native: `main()` already forces `POM68K_JIT_HOT=1`, `BLOCKS=1`,
`ACCESS_THUNK=2` and `LINKS=1`, and unsets `POM68K_JIT_BACKEND`, so `auto`
picks x64 on non-Windows x86-64 and a64 on AArch64. `POM68K_JIT_REQUIRE_NATIVE=1`
turns "native unavailable" into FAIL (lines 294-298). Register it with
`check(runLateIplLockstep(), "cacheless 040 post-read IPL re-sample takes
the IRQ at the reading boundary")`. A cache-armed twin
(`setCACR(0x80008000)` plus `POM68K_040_DCACHE=1`) is optional. With
LATE_POLL off it must instead show `slowInstrs > 0` (exact fallback), which
mirrors the copyback gate's `ADD.W (A0),D0` pin
(`jit_copyback_write_040_test.cpp:205, 445-488`).

## 6. Fix sketch for x64 (mirror A64)

In `Emitter::emit()`, replace the block at ~5014:
```cpp
bool lateIplPoll = false;
const Instr& in = ir_.instrs[i];
if (!L_.is030 && in.memory.count != 0 && in.iplPolls > 1) {
    const uint8_t n = in.memory.count;
    lateIplPoll = (!L_.cache040Live || cache040LatePollEnabled()) &&
        in.iplPollPosValid && n >= 1 && n <= MemoryContract::MaxAccesses &&
        in.iplPollMask == uint8_t(1u << (n - 1));
    if (!lateIplPoll) { a_.jmp(staticStub(i)); emitted = i + 1; continue; }
}
```
Then, after `native_++; if (kind == Branch) break;` and before
`chargeIcache(i)` (~5050-5058), add `if (lateIplPoll) pollIpl();`. RAX is
scratch at that point, just as at the head.

The change is not purely additive. Cacheless x64 forms whose second poll is
**not** after the final access (MOVE reg→mem with a pre-write poll, CMPM,
ADDX/ABCD -(An), LINK and PEA if traced as memory) become refusals. They are
native today. A64 measured +1.35 % on `jit_bench` for the same rule. Price
it with an ABBA run, and expect `pom68k_perf_budget` native-ratio cells to
move.

Gates that must stay green (`cmake/Pom68kJitGates.cmake`):
* `jit_asset_free_lockstep_test` (plus the new case);
* `jit_copyback_write_040_test` and its `_control`, `jit_copyback_bsr_040_test`,
  `jit_copyback_pair_040_test` and `_pair_040_control_test`;
* `jit_lockstep_x64_test` (cache on, 1M, LINE_STATS);
* `jit_lockstep_x64_fine_test` (cacheless, 200k);
* `jit_lockstep_noaccess_test`;
* the 030 x64 locksteps (`jit_lockstep_030_x64_{experimental,packed_ccr,alignment}_test`).
  They are guarded by `!L_.is030` but share the emit loop.

Then `q605_afp_live_etalon`, `q605_afp_rename_etalon`
(`Pom68kAfpGates.cmake`) and the `-L m040` tier, checking that required
assets actually executed rather than soft-skipping.
