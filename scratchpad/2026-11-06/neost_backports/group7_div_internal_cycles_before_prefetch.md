# Group 7 — DIVU/DIVS: internal cycles before the prefetch on the 68000

## NeoST change

Commit `262f63a` (2026-10-20, "Rétablit l'ordre DIVU/DIVS…"), port of upstream Moira `f90fc50`
("Fixed DIVU/DIVS prefetch timing", 2026-09-23). Moira file: `MoiraExec_cpp.h`.

```diff
# NeoST 262f63a — extern/moira/Moira/MoiraExec_cpp.h
@@ -4281,12 +4281,25 @@ Moira::execDivsMoira(u16 opcode, bool *divByZero)
         return;
     }

-    result = divsMoira<C>(dividend, divisor);
-    writeD(dst, result);
-    prefetch<C, POLL>();
-
     [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
-    SYNC(cycles);
+
+    // NeoST : port de l'amont f90fc50 (« Fixed DIVU/DIVS prefetch timing »). Sur
+    // 68000, les cycles internes précèdent le préfetch (Hatari CE, op_80c0_13 :
+    // do_cycles_ce000_internal puis get_word_ce000_prefetch).
+    if constexpr (C == Core::C68000) {
+
+        SYNC(cycles);
+        result = divsMoira<C>(dividend, divisor);
+        writeD(dst, result);
+        prefetch<C, POLL>();
+
+    } else {
+
+        result = divsMoira<C>(dividend, divisor);
+        writeD(dst, result);
+        prefetch<C, POLL>();
+        SYNC(cycles);
+    }
 }

 template <Core C, Instr I, Mode M, Size S> void
@@ -4409,12 +4422,25 @@ Moira::execDivuMoira(u16 opcode, bool *divByZero)
         return;
     }

-    result = divuMoira<C>(dividend, divisor);
-    writeD(dst, result);
-    prefetch<C, POLL>();
-
     [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
-    SYNC(cycles);
+
+    // NeoST : port de l'amont f90fc50 (« Fixed DIVU/DIVS prefetch timing »). Sur
+    // 68000, les cycles internes précèdent le préfetch (Hatari CE, op_80c0_13 :
+    // do_cycles_ce000_internal puis get_word_ce000_prefetch).
+    if constexpr (C == Core::C68000) {
+
+        SYNC(cycles);
+        result = divuMoira<C>(dividend, divisor);
+        writeD(dst, result);
+        prefetch<C, POLL>();
+
+    } else {
+
+        result = divuMoira<C>(dividend, divisor);
+        writeD(dst, result);
+        prefetch<C, POLL>();
+        SYNC(cycles);
+    }
 }

 template <Core C, Instr I, Mode M, Size S> void
```

**NeoST rationale (translated).** On the 68000 `SYNC(cycles)` now precedes `writeD` and the
prefetch; port of upstream `f90fc50`, restricted to the 68000 as upstream did. Hatari CE does the
same (`op_80c0_13_ff`: `do_cycles_ce000_internal` then `get_word_ce000_prefetch`). The fix had existed
before in NeoST's lost fork (`e4da365`, 2026-06-17) and had been dropped at vendoring. **Evidence:**
probe `cpu_div_ram` of `tools/run_timing_oracle.py` — 24 DIVU then DIVS pairs executed **from RAM**,
each iteration's duration compared to **Hatari** to the cycle: before, 14 DIVU and 9 DIVS iterations
were 4 cycles short; after, 0. (From cartridge/fast memory the order has no effect.)

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `execDivsMoira` tail | `MoiraExec_cpp.h` l.4963-5040 | **Yes — conflict (a).** POM68K row 2: the 68000/68010 **early overflow exit** after the prefetch (`if (reg.sr.v) { SYNC((i32(dividend) < 0) ? 14 : 12); return; }` = 16/18 cycles total), plus 68020/68040 div-by-zero CCR rules and 68010-only `pomSetRB` in the address-error rectification. 39 changed lines vs NeoST@`47d6c39`. |
| `execDivuMoira` tail | `MoiraExec_cpp.h` l.5115-5186 | Changed above the tail only (div-by-zero CCR rules, `pomSetRB`); the tail is identical. 32 changed lines, none in the edited block. |

POM68K current `execDivsMoira` tail:
```cpp
    result = divsMoira<C>(dividend, divisor);
    writeD(dst, result);
    prefetch<C, POLL>();

    // POM68K: every overflow exits early at 16/18 cycles total
    // (SingleStepTests/680x0, CLK rule)
    if (reg.sr.v) {
        SYNC((i32(dividend) < 0) ? 14 : 12);
        return;
    }

    [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
    SYNC(cycles);
}
```
**Conflict (a) and its resolution.** NeoST computes `cyclesDiv` first and SYNCs before `divsMoira`.
POM68K needs V (from `divsMoira`) to choose between the early-exit charge (12/14) and `cyclesDiv − 4`,
and row 38 relies on `cyclesDiv` being **unreached on overflow** (the `$80000000` dividend). Since
`divsMoira` has no bus side effect (flags + return value only — `MoiraALU_cpp.h` l.705-753), the
68000 branch computes it first, then `SYNC(cycles)`, `writeD`, `prefetch`. Same totals, same flags,
`cyclesDiv` still skipped on overflow. `SYNC(...)` is a macro: the ternary is staged in a `const int`
because `cyclesDiv<C, I>(...)` contains a top-level comma. The 68010/020+ path is the current code,
unchanged. DIVU takes NeoST's shape verbatim (POM68K has no DIVU early exit; `cyclesDiv` handles
DIVU overflow itself).
- **Independent evidence found during this audit:** in `tests/data/sst68000`, **all** 4 963
  sequential DIVU and 4 992 sequential DIVS vectors end with an idle block (`n`) followed by the
  prefetch read at final PC+2 — including the 2 469 DIVU / 3 753 DIVS overflow vectors (DIVS overflow
  tails are `n 12`/`n 14`, exactly row 2's early exit). The current code does the read first.
- (b) `irqDelay`: untouched. (c) JIT: `prefetch` keeps its `pomJitFetch000` window; both engines run
  this handler (threaded replays Moira handlers), so `jit_lockstep_68000_test` stays in lockstep.
- **In-vivo effect on Macs:** the prefetch (and its `POLL_IPL`) moves up to ~150 cycles later. On a
  Mac Plus/SE/Classic with code in RAM the read now lands at a different phase of the video/sound
  contention window (`Cpu68k::applyContention`), so whole-machine cycle totals can shift by a few wait
  states per DIV, and an IRQ edge during a DIV may now be recognised one instruction later — both the
  silicon behaviour per SST/Hatari. ROM-resident DIVs are not contended.

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**7.1 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execDivsMoira (68000: idle before prefetch, early exit kept)**

OLD:
```cpp
    result = divsMoira<C>(dividend, divisor);
    writeD(dst, result);
    prefetch<C, POLL>();

    // POM68K: every overflow exits early at 16/18 cycles total
    // (SingleStepTests/680x0, CLK rule)
    if (reg.sr.v) {
        SYNC((i32(dividend) < 0) ? 14 : 12);
        return;
    }

    [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
    SYNC(cycles);
}
```
NEW:
```cpp
    result = divsMoira<C>(dividend, divisor);

    // POM68K (ported from NeoST 262f63a = upstream Moira f90fc50, "Fixed
    // DIVU/DIVS prefetch timing"): on the 68000 the internal cycles precede
    // the prefetch (Hatari CE op_80c0_13: do_cycles_ce000_internal, then
    // get_word_ce000_prefetch). Every non-exception DIVS vector of
    // tests/data/sst68000 shows the idle cycles before the final read,
    // overflow exits included. divsMoira() touches no bus, so it runs first:
    // V decides the early overflow exit below, and cyclesDiv() stays
    // unreached on overflow (row 38's argument holds).
    if constexpr (C == Core::C68000) {

        // POM68K: every overflow exits early at 16/18 cycles total
        // (SingleStepTests/680x0, CLK rule)
        const int cycles = reg.sr.v ? ((i32(dividend) < 0) ? 14 : 12)
                                    : cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
        SYNC(cycles);
        writeD(dst, result);
        prefetch<C, POLL>();

    } else {

        writeD(dst, result);
        prefetch<C, POLL>();

        // POM68K: every overflow exits early at 16/18 cycles total
        // (SingleStepTests/680x0, CLK rule)
        if (reg.sr.v) {
            SYNC((i32(dividend) < 0) ? 14 : 12);
            return;
        }

        [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
        SYNC(cycles);
    }
}
```

**7.2 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execDivuMoira (verbatim NeoST shape)**

OLD:
```cpp
    result = divuMoira<C>(dividend, divisor);
    writeD(dst, result);
    prefetch<C, POLL>();

    [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;
    SYNC(cycles);
}
```
NEW:
```cpp
    [[maybe_unused]] auto cycles = cyclesDiv<C, I>(dividend, (u16)divisor) - 4;

    // POM68K (ported from NeoST 262f63a = upstream Moira f90fc50): on the
    // 68000 the internal cycles precede the prefetch (Hatari CE op_80c0_13;
    // every non-exception DIVU vector of tests/data/sst68000 agrees).
    if constexpr (C == Core::C68000) {

        SYNC(cycles);
        result = divuMoira<C>(dividend, divisor);
        writeD(dst, result);
        prefetch<C, POLL>();

    } else {

        result = divuMoira<C>(dividend, divisor);
        writeD(dst, result);
        prefetch<C, POLL>();
        SYNC(cycles);
    }
}
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group7.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green.** The total `length` is unchanged for every path (overflow 16/18 kept,
normal path `cyclesDiv`), registers/flags/IRD/IRC/RAM unchanged (no bus side effect in `divsMoira`/
`divuMoira`; `writeD` before or after the idle block is invisible). Div-by-zero and address-error
paths are untouched. Only the uncompared transaction order changes — to what the corpus records.
`moira_rules_test` (DIVS `$80000000` on six models, 18 cycles on the 68000) compares totals and flags:
unchanged.

## Other gates that exercise the area

- `sst68000` (DIVU.json, DIVS.json — 8 065 each).
- `moira_rules_test` (DIVS.W `$80000000`, rows 2/38).
- `jit_asset_free_lockstep_test` (DIVS/DIVU sequences, SimCity operands — 020+/JIT; guards the
  unchanged `else` branch), `jit_backend_test`.
- `sst68030`/`sst68040` (unchanged `else` branch).
- `jit_lockstep_68000_test` / `_blocks_test`, `contention_test`, compact/Portable boot etalons
  (in-vivo; wait-state totals may move, see above — **unverified** whether any 68000 gate pins an
  exact clock that a DIV executed from RAM would perturb; the grep found no pinned cycle goldens in
  the compact/Portable etalons).

## Risk grade and recommendation

**Low-medium** (gate-neutral by construction; whole-machine cycle drift possible on compacts). Recommendation: **apply** (the variant here, not NeoST's verbatim text, because of row 2); run the 68000 machine tier and `jit_lockstep_68000_test`.
