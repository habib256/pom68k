# Group 3 — willAccessBus / didAccessBus — bus-cycle bounds (long access split SYNC(2)+hook+SYNC(2))

## NeoST change

Commit `ce1cc52` (2026-10-19). Moira files: `Moira.h`, `MoiraDataflow_cpp.h`.

```diff
# NeoST ce1cc52 — extern/moira/Moira/Moira.h
@@ -294,11 +294,18 @@ public:
     //

 protected:
+
+    // NeoST : faute courante, y compris celle qui interrompt une exception.
+    virtual void willProcessFault(const StackFrame &frame) { }

 #if MOIRA_VIRTUAL_API == true

     // Advances the internal clock by the specified number of cycles
     virtual void sync(int cycles) { clock += cycles; }
+    // NeoST : bornes des cycles bus, pour l'arbitrage blitter avant/après accès.
+    virtual void willAccessBus(u32 /*addr*/, bool /*write*/, unsigned /*size*/) { }
+    virtual void didAccessBus() { }
+    virtual void setBusLock(bool /*locked*/) { }

     // Reads a value from memory
     virtual u8 read8(u32 addr) const = 0;
@@ -408,6 +415,9 @@ protected:

     // Advances the internal clock by the specified number of cycles
     void sync(int cycles);
+    void willAccessBus(u32 /*addr*/, bool /*write*/, unsigned /*size*/) { }
+    void didAccessBus() { }
+    void setBusLock(bool) { }

     // Reads a value from memory
     u8 read8(u32 addr) const;
```

```diff
# NeoST ce1cc52 — extern/moira/Moira/MoiraDataflow_cpp.h
@@ -366,6 +366,7 @@ Moira::read(u32 addr)

     // Update function code pins
     setFC(AS == AddrSpace::DATA ? FC::USER_DATA : FC::USER_PROG);
+    willAccessBus(addr & addrMask<C>(), false, S == Byte ? 1 : 2);
     SYNC(2);

     // Check for address errors
@@ -386,6 +387,7 @@ Moira::read(u32 addr)
         if (F & POLL) POLL_IPL;
         result = read8(addr & addrMask<C>());
         SYNC(2);
+        didAccessBus();
     }

     if constexpr (S == Word) {
@@ -393,15 +395,17 @@ Moira::read(u32 addr)
         if (F & POLL) POLL_IPL;
         result = read16(addr & addrMask<C>());
         SYNC(2);
+        didAccessBus();
     }

     if constexpr (S == Long) {

         result = read16(addr & addrMask<C>()) << 16;
-        SYNC(4);
+        SYNC(2); didAccessBus(); willAccessBus((addr + 2) & addrMask<C>(), false, 2); SYNC(2);
         if (F & POLL) POLL_IPL;
         result |= read16((addr + 2) & addrMask<C>());
         SYNC(2);
+        didAccessBus();
     }

     return result;
@@ -422,6 +426,7 @@ Moira::write(u32 addr, u32 val)
 {
     // Update function code pins
     setFC(AS == AddrSpace::DATA ? FC::USER_DATA : FC::USER_PROG);
+    willAccessBus((addr + ((S == Long && (F & REVERSE)) ? 2 : 0)) & addrMask<C>(), true, S == Byte ? 1 : 2);
     SYNC(2);

     // Check for address errors
@@ -440,6 +445,7 @@ Moira::write(u32 addr, u32 val)
         if (F & POLL) POLL_IPL;
         write8(addr & addrMask<C>(), (u8)val);
         SYNC(2);
+        didAccessBus();
     }

     if constexpr (S == Word) {
@@ -447,6 +453,7 @@ Moira::write(u32 addr, u32 val)
         if (F & POLL) POLL_IPL;
         write16(addr & addrMask<C>(), (u16)val);
         SYNC(2);
+        didAccessBus();
     }

     if constexpr (S == Long) {
@@ -454,18 +461,20 @@ Moira::write(u32 addr, u32 val)
         if (F & REVERSE) {

             write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
-            SYNC(4);
+            SYNC(2); didAccessBus(); willAccessBus(addr & addrMask<C>(), true, 2); SYNC(2);
             if (F & POLL) POLL_IPL;
             write16(addr & addrMask<C>(), u16(val >> 16));
             SYNC(2);
+            didAccessBus();

         } else {

             write16(addr & addrMask<C>(), u16(val >> 16));
-            SYNC(4);
+            SYNC(2); didAccessBus(); willAccessBus((addr + 2) & addrMask<C>(), true, 2); SYNC(2);
             if (F & POLL) POLL_IPL;
             write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
             SYNC(2);
+            didAccessBus();
         }
     }
 }
```

**NeoST rationale (translated).** Hooks `willAccessBus(addr, write, size)` / `didAccessBus()` before
and after every bus cycle (two for a long). The `SYNC(4)` between the two words is split 2+2
without adding cycles. Default implementations are empty; NeoST uses them for the MegaSTE
blitter's PRE_START and Hatari's 64 non-hog CPU accesses. Direction and width describe each cycle
(long = two words, reversed order kept): MegaSTE cache hit/miss decision and RAM pre-alignment before
`SYNC(2)`. The hit is latched before the cycles so that DMA cannot change data the CPU already got
from the cache. **Evidence:** seventeen strict timing probes against **Hatari** (ten of them blitter:
sizes, ROM/RAM, HOG/shared, MULU and TAS), TAS memory/D0 tests, credit resumption after snapshot;
"BL5 pixel closed": the 100 TADR values of `blitter_timer` match Hatari, zero pixel difference.
NeoST compiles `MOIRA_VIRTUAL_API true`, so both hooks are **virtual calls on every access** there.

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `Moira.h` virtual branch, after `sync` | `Moira.h` l.1104-1107 (`#if MOIRA_VIRTUAL_API == true` / `virtual void sync`) | Identical context. |
| `Moira.h` non-virtual branch, after `void sync(int cycles);` | `Moira.h` l.1305-1306 | Identical context. |
| `read<>`: `willAccessBus` before the leading `SYNC(2)`; `didAccessBus` after each word; long split | `MoiraDataflow_cpp.h` l.509-597, `Moira::read(u32)` | **Heavily changed.** POM68K inserted (1) the 68030 `mmuRead` / 68040 `mmu040Read` early returns between `setFC` and `SYNC(2)` (rows 5, 18); (2) `pomDidReachWatchpoint(addr, S, false, AS == PROG)` instead of `didReachWatchpoint` (row 35); (3) the plain-68020 in-flight access recording `kPlain020` / `mmuAccAddr`/`mmuAccSsw`/`mmuAccFc` before each `read8/read16` (row 21). |
| `write<>`: same, with the REVERSE first-cycle address | `MoiraDataflow_cpp.h` l.609-695, `Moira::write(u32,u32)` | Same three POM68K insertions. |
| (no NeoST hunk) the JIT cycle-exact fetch window | `Moira.h` l.881-897, `pomJitFetch000<C,P>` — reproduces `read<C,PROG,Word,F>` step by step but **skips `read<>`** | POM68K-only (row 22). |

POM68K current `read<>` long path (l.582-594):
```cpp
    if constexpr (S == Long) {

        if constexpr (kPlain020) {
            mmuAccAddr = addr; mmuAccSsw = 0x00;
            mmuAccFc = readFC(); mmuAccWrite = false;
        }
        result = read16(addr & addrMask<C>()) << 16;
        SYNC(4);
        if (F & POLL) POLL_IPL;
        if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
        result |= read16((addr + 2) & addrMask<C>());
        SYNC(2);
    }
```
**Conflicts flagged:**
- **(c) JIT seam — real conflict.** NeoST's hooks live only in `read<>`/`write<>`. In POM68K, when a
  `jit::Engine` arms the code window on a 68000 (compacts, Portable/PB100 — `PortableCpu` drives
  `jit_.executeUntil` when enabled), `prefetch`/`fullPrefetch`/`readExt` take
  `pomJitFetch000` and **never enter `read<>`**: a verbatim port would silently lose every windowed
  opcode/extension fetch from the hook stream, i.e. the interpreter and the JIT would report
  different bus streams. Edit 3.4 adds the same bounds inside `pomJitFetch000` (after `setFC`,
  around the two `sync(2)`), in `read<>`'s own order. The 020/030/040 windows (`pomJitFetch020`,
  `pomJitFetch`, `pomJitData`) and the MMU paths are not covered — on `Core::C68020` SYNC is empty, so
  bus-cycle bounds have no timing meaning there; documented in the comment.
- **Watchpoint delegate:** NeoST's context line is `didReachWatchpoint(addr & addrMask<C>())`; POM68K's
  is `pomDidReachWatchpoint(...)`. The edits do not touch those lines (OLD blocks anchor elsewhere),
  so no conflict, but a verbatim NeoST hunk would not apply.
- **(a) SST address-error machinery:** `willAccessBus` fires before the misalignment check, so an
  odd word/long access calls `willAccessBus` and then throws `AddressError` **without**
  `didAccessBus` (NeoST has the same imbalance). A consumer must close the bracket from
  `willProcessFault` (group 2) or tolerate it. Frame contents are unaffected.
- **Plain 68020** (`kPlain020`): hooks fire there too (as written, like NeoST's generic code); SYNC
  is empty on that core, so the bounds carry no cycle information. The 68030/68040 return before the
  hook.
- **Hot-path cost.** POM68K compiles `MOIRA_VIRTUAL_API true`; NeoST's shape would add **two virtual
  calls per bus cycle** on every 68000/68010/plain-020 access, plus one extra `sync()` virtual call per
  long access (the 2+2 split). POM68K measured ~11 % from one virtual hook on the fetch path (row 14).
  Hence the compile-time switch `MOIRA_BUS_CYCLE_HOOKS` (MoiraConfig.h, **default false**):
  - OFF: `willAccessBus`/`didAccessBus` are empty **non-virtual** inline members → each call is
    removed by the inliner (arguments are pure); `if constexpr (MOIRA_BUS_CYCLE_HOOKS)` keeps the
    single `SYNC(4)` in long accesses, so the emitted code is the current code.
  - ON: virtual members, NeoST's exact SYNC(2)+hooks+SYNC(2) split.
  This keeps NeoST's call-site shape (same statements, same order) except the long-access split,
  which is wrapped in `if constexpr`.
- **Is 2+2 equal to 4 when ON?** On `Cpu68k` (compacts) `sync()` is `clock += n; catchUp()` and
  `catchUp` ticks `mem_.tick(d)` by the delta: two ticks of 2 instead of one of 4. Equivalent only if
  `MacMemory::tick` is additive and an IRQ raised at +2 rather than +4 is not observed before the
  next `POLL_IPL` — true while `iplDelay4 == 0` (NEOST_IPLFETCH is off on every Mac profile,
  `pomJitSimpleIpl`). **Unverified** for `PortableCpu`/`PortableMemory::tick`. Irrelevant when OFF.
- (b) `irqDelay`: not touched.

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**3.1 — `extern/moira/Moira/MoiraConfig.h` — MoiraConfig.h: compile-time switch, default OFF**

OLD:
```cpp
#define MOIRA_VIRTUAL_API true
```
NEW:
```cpp
#define MOIRA_VIRTUAL_API true

/* POM68K (ported from NeoST ce1cc52): bus-cycle bound delegates.
 *
 * When true, read<>/write<> (68000/68010 cores and the plain 68020 path) and
 * the cycle-exact JIT fetch window (Moira.h pomJitFetch000) call the virtual
 * delegates willAccessBus(addr, write, size) before and didAccessBus() after
 * every bus cycle (a long access is two word cycles, reversed order kept for
 * REVERSE writes), and the 4-cycle gap inside a long access is split
 * SYNC(2) + delegates + SYNC(2) without adding cycles.
 *
 * When false (POM68K default) the delegates are empty NON-virtual inline
 * members, every call site compiles away, and a long access keeps its single
 * SYNC(4) (no extra sync() call). Kept off because one virtual hook on the
 * fetch path cost ~11 % in this fork (POM68K_VENDOR.md § Status, row 14).
 * Requires MOIRA_VIRTUAL_API == true to be overridable.
 */
#define MOIRA_BUS_CYCLE_HOOKS false
```

**3.2 — `extern/moira/Moira/Moira.h` — Moira.h (virtual API branch): declare willAccessBus/didAccessBus**

OLD:
```cpp
    virtual void sync(int cycles) { clock += cycles; }
```
NEW:
```cpp
    virtual void sync(int cycles) { clock += cycles; }

    // POM68K (ported from NeoST ce1cc52): bounds of every bus cycle, behind
    // MOIRA_BUS_CYCLE_HOOKS (MoiraConfig.h). Off: empty non-virtual inlines
    // that compile away. NOT called by the 68030/68040 translated paths
    // (mmuRead/mmu040Read and their writes), the JIT data windows, the IACK
    // cycle or the reset vector fetch. An address error throws after
    // willAccessBus() and before didAccessBus() (same as NeoST).
#if MOIRA_BUS_CYCLE_HOOKS == true
    virtual void willAccessBus(u32 /*addr*/, bool /*write*/, unsigned /*size*/) { }
    virtual void didAccessBus() { }
#else
    void willAccessBus(u32 /*addr*/, bool /*write*/, unsigned /*size*/) { }
    void didAccessBus() { }
#endif
```

**3.3 — `extern/moira/Moira/Moira.h` — Moira.h (non-virtual API branch): no-op twins, as NeoST**

OLD:
```cpp
    void sync(int cycles);
```
NEW:
```cpp
    void sync(int cycles);
    void willAccessBus(u32 /*addr*/, bool /*write*/, unsigned /*size*/) { }  // POM68K (ported from NeoST ce1cc52)
    void didAccessBus() { }
```

**3.4 — `extern/moira/Moira/Moira.h` — Moira.h pomJitFetch000: mirror read<>'s bounds in the windowed fetch**

OLD:
```cpp
        setFC(FC::USER_PROG);
        sync(2);
        if constexpr (P) pollIpl();
        // The machine's bus model rides inside read16() on this core (the
        // Mac Plus video/RAM contention — Cpu68k::applyContention). No
        // read16, no contention, unless the wrapper asks for it here.
        if (pomJitBusStallFn) pomJitBusStallFn(pomJitBusStallCtx, addr & addrMask<C>());
        out = u16(u16(p[0]) << 8 | p[1]);
        sync(2);
        return true;
```
NEW:
```cpp
        setFC(FC::USER_PROG);
        willAccessBus(addr & addrMask<C>(), false, 2);  // POM68K (ported from NeoST ce1cc52): as read<>
        sync(2);
        if constexpr (P) pollIpl();
        // The machine's bus model rides inside read16() on this core (the
        // Mac Plus video/RAM contention — Cpu68k::applyContention). No
        // read16, no contention, unless the wrapper asks for it here.
        if (pomJitBusStallFn) pomJitBusStallFn(pomJitBusStallCtx, addr & addrMask<C>());
        out = u16(u16(p[0]) << 8 | p[1]);
        sync(2);
        didAccessBus();
        return true;
```

**3.5 — `extern/moira/Moira/MoiraDataflow_cpp.h` — MoiraDataflow_cpp.h read<>: willAccessBus before the leading SYNC(2)**

OLD:
```cpp
    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F>(addr));
    }
```
NEW:
```cpp
    // POM68K (ported from NeoST ce1cc52): bus-cycle bounds (MOIRA_BUS_CYCLE_HOOKS)
    willAccessBus(addr & addrMask<C>(), false, S == Byte ? 1 : 2);
    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F>(addr));
    }
```

**3.6 — `extern/moira/Moira/MoiraDataflow_cpp.h` — read<> Byte**

OLD:
```cpp
        result = read8(addr & addrMask<C>());
        SYNC(2);
    }
```
NEW:
```cpp
        result = read8(addr & addrMask<C>());
        SYNC(2);
        didAccessBus();
    }
```

**3.7 — `extern/moira/Moira/MoiraDataflow_cpp.h` — read<> Word**

OLD:
```cpp
        result = read16(addr & addrMask<C>());
        SYNC(2);
    }
```
NEW:
```cpp
        result = read16(addr & addrMask<C>());
        SYNC(2);
        didAccessBus();
    }
```

**3.8 — `extern/moira/Moira/MoiraDataflow_cpp.h` — read<> Long**

OLD:
```cpp
        result = read16(addr & addrMask<C>()) << 16;
        SYNC(4);
        if (F & POLL) POLL_IPL;
        if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
        result |= read16((addr + 2) & addrMask<C>());
        SYNC(2);
    }
```
NEW:
```cpp
        result = read16(addr & addrMask<C>()) << 16;
        // POM68K (ported from NeoST ce1cc52): with MOIRA_BUS_CYCLE_HOOKS the
        // 4-cycle gap is split around the two word cycles' bounds; without
        // it the single SYNC(4) stays (no extra sync() call).
        if constexpr (MOIRA_BUS_CYCLE_HOOKS) {
            SYNC(2); didAccessBus(); willAccessBus((addr + 2) & addrMask<C>(), false, 2); SYNC(2);
        } else {
            SYNC(4);
        }
        if (F & POLL) POLL_IPL;
        if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
        result |= read16((addr + 2) & addrMask<C>());
        SYNC(2);
        didAccessBus();
    }
```

**3.9 — `extern/moira/Moira/MoiraDataflow_cpp.h` — MoiraDataflow_cpp.h write<>: willAccessBus before the leading SYNC(2)**

OLD:
```cpp
    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F|AE_WRITE>(addr));
    }
```
NEW:
```cpp
    // POM68K (ported from NeoST ce1cc52): bus-cycle bounds (MOIRA_BUS_CYCLE_HOOKS);
    // a REVERSE long write announces its first cycle, the low word at addr+2
    willAccessBus((addr + ((S == Long && (F & REVERSE)) ? 2 : 0)) & addrMask<C>(), true, S == Byte ? 1 : 2);
    SYNC(2);

    // Check for address errors
    if (misaligned<C, S>(addr)) {
        throw AddressError(makeFrame<F|AE_WRITE>(addr));
    }
```

**3.10 — `extern/moira/Moira/MoiraDataflow_cpp.h` — write<> Byte**

OLD:
```cpp
        write8(addr & addrMask<C>(), (u8)val);
        SYNC(2);
    }
```
NEW:
```cpp
        write8(addr & addrMask<C>(), (u8)val);
        SYNC(2);
        didAccessBus();
    }
```

**3.11 — `extern/moira/Moira/MoiraDataflow_cpp.h` — write<> Word**

OLD:
```cpp
        write16(addr & addrMask<C>(), (u16)val);
        SYNC(2);
    }
```
NEW:
```cpp
        write16(addr & addrMask<C>(), (u16)val);
        SYNC(2);
        didAccessBus();
    }
```

**3.12 — `extern/moira/Moira/MoiraDataflow_cpp.h` — write<> Long (both orders)**

OLD:
```cpp
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(4);
            if (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr; mmuAccSsw = 0x20; }
            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(2);

        } else {

            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(4);
            if (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(2);
        }
```
NEW:
```cpp
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            // POM68K (ported from NeoST ce1cc52): split only with the hooks on
            if constexpr (MOIRA_BUS_CYCLE_HOOKS) {
                SYNC(2); didAccessBus(); willAccessBus(addr & addrMask<C>(), true, 2); SYNC(2);
            } else {
                SYNC(4);
            }
            if (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr; mmuAccSsw = 0x20; }
            write16(addr & addrMask<C>(), u16(val >> 16));
            SYNC(2);
            didAccessBus();

        } else {

            write16(addr & addrMask<C>(), u16(val >> 16));
            // POM68K (ported from NeoST ce1cc52): split only with the hooks on
            if constexpr (MOIRA_BUS_CYCLE_HOOKS) {
                SYNC(2); didAccessBus(); willAccessBus((addr + 2) & addrMask<C>(), true, 2); SYNC(2);
            } else {
                SYNC(4);
            }
            if (F & POLL) POLL_IPL;
            if constexpr (kPlain020) { mmuAccAddr = addr + 2; mmuAccSsw = 0x20; }
            write16((addr + 2) & addrMask<C>(), u16(val & 0xFFFF));
            SYNC(2);
            didAccessBus();
        }
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group3.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green** in both switch positions.
- OFF (default): no behavioural change at all — same SYNC sequence, the inline empty calls vanish.
- ON: `TestCpu` overrides neither hook nor `sync()`; the default `sync(n)` is `clock += n`, so
  `SYNC(2)+SYNC(2)` ≡ `SYNC(4)`: same `length`, same registers, prefetch and RAM.
The harness does not compare bus-transaction lists (it skips `transactions`), so the hook stream
itself is not checked by `sst68000`; a hook-ON consumer would need its own gate (e.g. a
TestCpu that logs `willAccessBus` and compares against the SST `transactions` arrays — that data
is in the local corpus).

## Other gates that exercise the area

- `sst68000` (every vector; with the switch OFF nothing to observe).
- `jit_lockstep_68000_test`, `jit_lockstep_68000_blocks_test` (clock-exact interpreter vs JIT on a
  Mac Plus; the place to prove edit 3.4 keeps both engines' hook streams equal once ON).
- `contention_test`, `compact_timing_etalon`, `compact_boot_etalon` (Mac Plus/SE/Classic contention
  and VIA timing live in `read16/write16` + `sync`, i.e. right where the split lands when ON).
- `savestate_68k_test`, `jit_bench_plus` (perf, dev tool) for the cost measurement if ever turned ON.

## Risk grade and recommendation

**Medium** (hot path; JIT window bypass). Recommendation: **apply behind the compile-time switch, default OFF** — and only if a POM68K consumer (bus-master/DMA arbitration) is planned; with OFF it is pure convergence at zero runtime cost. Turning it ON needs a same-process A/B on `jit_bench_plus` per `docs/MEASURING.md`, and a lockstep proof that windowed and non-windowed fetches report identical bounds.
