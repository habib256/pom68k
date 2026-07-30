### high — AppleTalk hub timers run on the BOOSTED core clock, so every second-scale timeout fires `cacheBoost_`× early
- **Where:** `src/main.cpp:205`
- **Defect:** `runQuantumWithWire` ticks the in-process AppleTalk stack with `cpu.getClock()` (Moira core cycles, 4× machine rate on every boosted wrapper) while the hub was configured with the *real* machine Hz (`hubHz = byteCycles * 28800`, `src/main.cpp:119`), so all `N * cpuHz_` periods expire 4× too soon.
- **Trigger:** GUI, any 030/040 profile (AppleTalk is on by default). `AtalkStack::configure` stores `cpuHz_ = 24 998 400` for a 25 MHz machine, but `Cpu030/040::runCycles(n)` executes `n * cacheBoost_` core cycles (`src/Cpu030.cpp:73`, `cacheBoost_ = 4` in every wrapper header except `Cpu020.h`), so `now_` advances 4× the rate the periods assume: ATP retry (`nextRetry = now_ + cpuHz_`, `src/AtalkStack.cpp:200`) fires after 250 ms instead of 1 s, RTMP broadcast every 2.5 s instead of 10 s, AFP tickle every 7.5 s, AFP session death (`now - s.lastHeard > 120 * st_.cpuHz()`, `src/AfpServer.cpp:200`) after 30 s. During a Finder copy the 250 ms ATP retry outruns the guest's ISR drain rate and injects duplicate TReq bursts — exactly the retransmit stall the comment at `src/main.cpp:107-111` says was chased before. `lagMs` at `src/AtalkStack.cpp:455` is also reported 4× high. Same defect class as the documented `viaSync`/`stall`-on-boosted-clock bug (CHANGELOG 2026-07-25).
- **Fix:** every wrapper reaching this call site already exposes the accessor (`Cpu030.h:42`, `Cpu040.h:32`, `SonoraCpu.h:38`, `RbvCpu.h:31`, `VaspCpu.h:31`, `CentrisCpu.h:32`, `Q630Cpu.h:29`, `Q700Cpu.h:30`, identity in `Cpu020.h:30`):
```cpp
        if (hub) g_atalk.tick(cpu.machineClock());   // bus/wire time = machine cycles
```

### low — `Cpu030::runCycles` drops `cacheBoost_` on the `POM68K_FPU_LOG` single-step path, running the machine 4× slow
- **Where:** `src/Cpu030.cpp:50`
- **Defect:** `runCycles(n)` is contractually a budget of `n` *machine* cycles (the else-branch multiplies by `cacheBoost_`, `flushTicks()` divides by it), but the `fpuLog_` branch executes only `n` Moira cycles, so `flushTicks()` hands `mem_.tick(n / cacheBoost_)` to the peripherals.
- **Trigger:** `POM68K_FPU_LOG=/tmp/f.log` on the LC II (`src/main.cpp:1354`). The 60 Hz driver calls `runCycles(kFrame)` expecting kFrame machine cycles; `flushTicks()` (`src/Cpu030.cpp:187-197`: `int m = int(periphAccum_ / cacheBoost_); if (m) mem_.tick(m);`) delivers a quarter of that, so VBL rate, VIA1 T1/T2, RTC seconds and ASC sample production all run at 1/4 cadence for as long as the logger is armed. Since the logger exists to diagnose a timing-sensitive Line-F/A5 corruption (`src/Cpu030.h:77-100`), arming it perturbs the very timing under study.
- **Fix:** `src/Cpu030.cpp:50` → `moira::i64 target = getClock() + n * cacheBoost_;`

### low — disassembly runs through the LIVE, side-effecting bus; the freeze-probe call site has no exception guard
- **Where:** `src/Cpu030.cpp:148` and `src/main.cpp:2747`
- **Defect:** no wrapper overrides `read16Dasm` (`extern/moira/Moira/Moira.h:346` falls back to `read16`), so Moira's disassembler reads go through `mem_.read16()` — device registers with read side effects and, on unmapped I/O, `busError()` → `cpu_->extBusError()` (which mutates `reg.a[]` (An)± fixups and the mmuSsw/mmuFaultAddr state) followed by `throw moira::MmuBusError{}`.
- **Trigger:** (a) `POM68K_FPU_LOG`: `dumpFpuLog` is called from `willExecute()` *while a Line-F exception is being taken* and disassembles up to 4096 ring PCs; the ring holds **logical** PCs while `read16` takes physical addresses, so with the PMMU on the reads land anywhere — the `catch (...)` at `Cpu030.cpp:149` swallows the throw but not the register/MMU-state damage or the device-register side effects (SCC status reads clear latched ext/status; IWM state-line accesses toggle CA0-CA2/LSTRB on *any* access). (b) `POM68K_FREEZE_PROBE=1` on the Quadra: `src/main.cpp:2745-2751` calls `cpu.disassemble()` with **no** try/catch, and there is no `catch` anywhere in `main.cpp`, so a bus error escapes the machine thread → `std::terminate`. The project already knows the hazard: `tests/lcii_trace.cpp:246-255` wraps `disassemble()` in a `safeDasm` lambda with exactly this comment.
- **Fix:** override the non-intrusive path in the wrappers (all these memories expose `peek8`):
```cpp
    moira::u16 read16Dasm(moira::u32 addr) const override {
        return moira::u16(mem_.peek8(addr) << 8) | mem_.peek8(addr + 1);
    }
```
which removes the bus-error path from both call sites.

### low — Mac II word-sized I/O accesses skip the peripheral catch-up the byte path performs
- **Where:** `src/MacIIMemory.cpp:444` (read16) and `src/MacIIMemory.cpp:528` (write16)
- **Defect:** `read8Decoded`/`write8Decoded` call `cpu_->flushTicks()` before touching I/O (`src/MacIIMemory.cpp:397`), but `read16`/`write16` route straight into `viaAccess()` (`src/MacIIMemory.cpp:222`, whose first act is only `viaSync()`), so a word/long access samples the VIA timers and IFR at a timeline up to `Cpu020::kPeriphBatch` = 64 cycles stale (`src/Cpu020.cpp:66-68` only flushes past that threshold, and `viaSync`→`stall`→`catchUp` may again fall under it).
- **Trigger:** any `move.w`/`move.l` from VIA space on the Mac II / IIx / IIcx (Moira splits a long into two `read16` calls, so both halves take this path) — e.g. a T2 countdown polled with a word read is read up to 3 VIA φ2 ticks behind. The sibling machine hit this and fixed it in the shared entry point: `src/V8Memory.cpp:255` opens `viaAccess8` with `if (cpu_) cpu_->flushTicks();   // word path skips read8's flush`; `MacIIMemory::viaAccess` has no such line.
- **Fix:** add `if (cpu_) cpu_->flushTicks();` as the first statement of `MacIIMemory::viaAccess` (before `viaSync()` at `src/MacIIMemory.cpp:224`) and drop the now-redundant flush from `read8Decoded`/`write8Decoded`, so byte and word accesses see one timeline.
