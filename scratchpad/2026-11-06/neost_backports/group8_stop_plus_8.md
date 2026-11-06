# Group 8 — STOP+8: stopGraceUntil, quantised 4-cycle wake, STOP = 4(0/0) operand from IRC

## NeoST change

Commits `993ce5e` (2026-10-22, "STOP+8 : les quatre cycles manquants…") and `2fc53a8`
(2026-10-22, "STOP 4(0/0) et réveil quantifié (fx68k)…"). Moira files: `Moira.h`, `Moira.cpp`,
`MoiraExec_cpp.h`.

```diff
# NeoST 993ce5e — extern/moira/Moira/Moira.h
@@ -85,6 +85,12 @@ protected:
     // la valeur POLLÉE à la frontière SUIVANTE (rotation ipl[0]=ipl[1] de la run
     // loop, newcpu.c:5693). −1 = rien en attente.
     int iplDeferred {-1};
+    // NEOST : STOP avec une IRQ déjà pendante. Le 68000 teste l'interruption AVANT
+    // d'écrire le nouveau masque du SR (fx68k : rInterrupt verrouillé au même T3 que
+    // pswI ; Hatari gencpu : checkint() avant regs.sr = sr), et STOP se ré-exécute
+    // toutes les 4 cycles : l'exception part à STOP+8, pas STOP+4. Horloge jusqu'à
+    // laquelle checkForIrq est retenu après un STOP ; −1 = aucune retenue.
+    i64 stopGraceUntil {-1};

     // Value on the lower two function code pins (FC1|FC0)
     u8 fcl {2};
```

```diff
# NeoST 993ce5e — extern/moira/Moira/Moira.cpp
@@ -308,7 +308,8 @@ Moira::execute()
         }

         // Process pending interrupt (if any)
-        if (flags & CHECK_IRQ) {
+        // NEOST : retenue de 4 cycles après un STOP (cf. stopGraceUntil, Moira.h).
+        if ((flags & CHECK_IRQ) && !((flags & STOPPED) && clock < stopGraceUntil)) {

             try {
                 if (checkForIrq()) goto done;
```

```diff
# NeoST 993ce5e — extern/moira/Moira/MoiraExec_cpp.h
@@ -5426,6 +5426,11 @@ Moira::execStop(u16 opcode)
     //           .b  .b  .b        .w  .w  .w        .l  .l  .l
     CYCLES_IP   ( 0,  0,  0,        4,  4,  8,        0,  0,  0)

+    // NEOST : la première itération de STOP a testé l'IRQ avec l'ANCIEN masque (cf.
+    // stopGraceUntil) — une IRQ démasquée par l'opérande n'est prise qu'à l'itération
+    // suivante, 4 cycles plus tard (fx68k 0x3A2/0x327, Hatari do_cycles_stop(4)).
+    if constexpr (C == Core::C68000) stopGraceUntil = clock + 4;
+
     FINALIZE
 }

```

```diff
# NeoST 2fc53a8 — extern/moira/Moira/Moira.cpp
@@ -308,9 +308,19 @@ Moira::execute()
         }

         // Process pending interrupt (if any)
-        // NEOST : retenue de 4 cycles après un STOP (cf. stopGraceUntil, Moira.h).
+        // NEOST : pendant un STOP, l'interruption n'est prise qu'en fin d'ITÉRATION de
+        // STOP, toutes les 4 cycles depuis son début (fx68k ré-exécute 0x3A2/0x327 ;
+        // Hatari ré-exécute l'opcode avec do_cycles_stop(4)) — la première itération
+        // ayant testé avec l'ancien masque (cf. stopGraceUntil, Moira.h). La scrutation
+        // de la broche reste à chaque cycle ; seul l'instant de prise est quantifié : on
+        // AVANCE jusqu'à la frontière (l'horloge peut sauter par créneaux bus, attendre
+        // de la croiser exactement bloquait le CPU en STOP).
         if ((flags & CHECK_IRQ) && !((flags & STOPPED) && clock < stopGraceUntil)) {

+            if (flags & STOPPED) {
+                const i64 mis = (clock - stopGraceUntil) & 3;
+                if (mis) sync(int(4 - mis));
+            }
             try {
                 if (checkForIrq()) goto done;
             } catch (const std::exception &exc) {
```

```diff
# NeoST 2fc53a8 — extern/moira/Moira/MoiraExec_cpp.h
@@ -5416,7 +5416,14 @@ Moira::execStop(u16 opcode)

     SUPERVISOR_MODE_ONLY

-    u16 src = (u16)readI<C, Word>();
+    // NEOST : STOP vaut 4(0/0) — aucun cycle bus. L'opérande est déjà dans IRC ;
+    // le silicium ne préfetche pas pendant STOP (fx68k : nanomots 0x3A2/0x327 sans
+    // accès ; Hatari : « STOP does not prefetch anything », m68k_incpci(4) au réveil).
+    // readI<Word> aurait lu IRC puis préfetché le mot suivant sur le bus. Les
+    // sorties de STOP (exception, puis RTE) rechargent la file : IRC périmé sans effet.
+    u16 src;
+    if constexpr (C == Core::C68000) { src = queue.irc; reg.pc += 2; }
+    else                              { src = (u16)readI<C, Word>(); }

     setSR(src);
     flags |= State::STOPPED;
```

**NeoST rationale (translated).** When an IRQ is already pending and the `stop` operand unmasks it,
the 68000 takes it not at the end of the `stop` (+4) but at the next iteration (+8): the microcode
tests the interrupt BEFORE writing the new mask (fx68k.sv: `rInterrupt <= intPend` latched on the
same T3 as `pswI`, nanowords 0x3A2/0x327; STOP re-executes every 4 cycles because `Ir2Ird` blocks
IR←IRC; Hatari gencpu.c: `checkint()` before `regs.sr = sr`, then `do_cycles_stop(4)`). Upstream does
`setSR` then an immediate `checkForIrq` → exception at STOP+4. Completed the same day: (a) quantised
wake — an IRQ arriving AFTER STOP entry is taken only at the end of the current 4-cycle iteration
(`(clock − stopGraceUntil) & 3 == 0`), the pin still being polled every cycle; (b) no bus cycle during
STOP: operand taken from IRC without prefetch (`reg.pc += 2`), STOP = 4(0/0); the ways out of STOP
(exception then RTE) reload the queue. Not ported: fx68k's two-stage IPL synchroniser (~2 cycles).
NeoST serialises the field (`Cpu68k`, save-state v55). **Evidence:** **fx68k** netlist/microcode,
**Hatari** gencpu; Super Hang-On chain probe (Timer B → handler → `stop #$2100` → pending HBL,
cartridge probe, 3 997 chains): with `993ce5e` stop→exception 4 → 8, chain 136 → 140 = Hatari, which
exposed the old `g_iackMfp = 16` as a compensation (back to 12). After `2fc53a8`: "exception MFP 60 =
Hatari, stop→HBL {60,64,68} vs Hatari {64,68,72}", declared a deliberate divergence ("Hatari takes 12,
silicon 8"); Super Hang-On pixel-identical on 7 captures.

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `Moira.h` member after `iplDeferred` | `Moira.h` l.100-104: POM68K added `int irqDelay {0};` (row 13) right after `iplDeferred`; the edit anchors after `irqDelay`. | Context moved; no semantic conflict. |
| `Moira.cpp` `execute()` CHECK_IRQ block | `Moira.cpp` l.558-573 | **Yes — conflict (b).** POM68K wrapped `checkForIrq` in the `irqDelay` one-instruction delay (row 13, SimCity 2000). |
| `execStop` | `MoiraExec_cpp.h` l.6487 | No (identical to NeoST@`47d6c39`). |
| (new) `reset<C>()` | `Moira.cpp` l.244 | POM68K-only addition proposed (NeoST does not reset the field). |

POM68K current CHECK_IRQ block:
```cpp
        // Process pending interrupt (if any)
        if (flags & CHECK_IRQ) {

            // POM68K: honour the one-instruction delay armed by a mask-
            // lowering SR-write — run the next instruction before taking
            // the IRQ (breaks the SimCity-2000 redraw livelock).
            if (irqDelay) {
                irqDelay--;
            } else {
                try {
                    if (checkForIrq()) goto done;
                } catch (const std::exception &exc) {
                    processException(exc);
                }
            }
        }
```
**Conflict (b) and its resolution.** The grace test goes on the outer `if` (NeoST's text); the
quantisation goes inside POM68K's `else` (only when an IRQ will actually be tested). `irqDelay` is
armed only for `cpuModel >= M68020` and `stopGraceUntil` only by `execStop<C68000>`, so on any given
machine at most one of the two mechanisms is live.
**Gap in NeoST's code for a multi-core fork:** NeoST quantises whenever `STOPPED`, with
`stopGraceUntil` possibly −1. In POM68K a 68020/030/040 STOP (used by `berr030_test`,
`v8_ramsize`, and potentially by Mac OS/A/UX idle code on 020+ machines — unverified) would then
`sync(1..3)` at every wake with a phase relative to −1. The port adds `stopGraceUntil >= 0` to the
quantisation guard and resets the field in `reset<C>()`.
**Surprising — NeoST's final arithmetic does not give "+8".** Under `MOIRA_PRECISE_TIMING` (POM68K and
NeoST both true) `CYCLES_IP(...)` is **empty** (`MoiraMacros.h`: `CYCLES_68000(c) { }`), so
`execStop<C68000>` charges cycles only through its bus accesses. Trace for an IRQ already pending,
`T0` = clock when `execStop` starts:
- POM68K today: `readI` → one prefetch read = 4 cycles → `T0+4`; next `execute()` takes the IRQ at **T0+4**.
- NeoST `993ce5e`: readI 4 cycles, grace = T0+8 → STOPPED loop `sync(2)` ×2 → IRQ at **T0+8**.
- NeoST `2fc53a8` (final): no readI → 0 cycles, grace = `clock + 4` = T0+4 → IRQ at **T0+4**.
So the final NeoST state takes a pending IRQ at the **same instant as current POM68K**, differing only
by the absent bus read and the quantised wake for later IRQs. This matches NeoST's own probe deltas
(993ce5e matched Hatari's {64,68,72}; 2fc53a8 moved 4 cycles earlier to {60,64,68}), which NeoST
attributed to Hatari being late. Unless NeoST's `Cpu68k::run` STOP fast-forward adds cycles (not
read), the "STOP+8" claim describes `993ce5e`, not the shipped code. The port keeps NeoST's text
(option A) and documents **option B** (`SYNC(4)` in the 68000 operand line → IRQ at T0+8, STOP 4(0/0)
actually charged). **Arbitration needed** — see risk.
- **Save state:** `stopGraceUntil` survives instruction boundaries while STOPPED; it must join
  `MoiraSnapshot::visitCpuCommon` (`src/MoiraSnapshot.h`, next to `irqDelay`), the field map
  `tests/CpuChunkMap.h` (`{"stopGraceUntil", 8}`), the save-state version bump, and the JIT lockstep
  comparison (`tests/jit_lockstep_test.cpp` compares `irqDelay`; `src/Cpu040.cpp`/`Cpu040.h`
  mirror it). These are outside `extern/moira` and are **not** in the edit list.
- (a) SST patches: not touched. (c) JIT seams: none in `execStop`/STOPPED handling; blocks exit on
  any flag and the interpreter's slow path runs STOP (unverified for `PortableCpu`'s
  `jit_.executeUntil`, which is assumed to fall back to `execute()` while `flags != 0`).

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**8.1 — `extern/moira/Moira/Moira.h` — Moira.h: stopGraceUntil member (next to irqDelay)**

OLD:
```cpp
    // POM68K: one-instruction interrupt-recognition delay after a mask-
    // lowering SR-write (see setSR / the run loop). 68020+ only.
    int irqDelay {0};
```
NEW:
```cpp
    // POM68K: one-instruction interrupt-recognition delay after a mask-
    // lowering SR-write (see setSR / the run loop). 68020+ only.
    int irqDelay {0};

    // POM68K (ported from NeoST 993ce5e/2fc53a8): STOP with an IRQ already
    // pending. The 68000 tests the interrupt BEFORE writing the new SR mask
    // (fx68k: rInterrupt latched on the same T3 as pswI; Hatari gencpu:
    // checkint() before regs.sr = sr) and re-executes STOP every 4 cycles,
    // so an IRQ unmasked by the operand is taken one iteration later.
    // Clock until which checkForIrq is held off after a 68000 STOP, and the
    // phase origin of the 4-cycle wake quantum; -1 = none. 68000 only.
    // Survives an instruction boundary: belongs in the save state.
    i64 stopGraceUntil {-1};
```

**8.2 — `extern/moira/Moira/Moira.cpp` — Moira.cpp reset<C>(): clear it (POM68K-only addition)**

OLD:
```cpp
    irqDelay = 0;                  // POM68K: clear the SR-write IRQ delay too
```
NEW:
```cpp
    irqDelay = 0;                  // POM68K: clear the SR-write IRQ delay too
    stopGraceUntil = -1;           // POM68K: no STOP grace across a reset (NeoST leaves it)
```

**8.3 — `extern/moira/Moira/Moira.cpp` — Moira.cpp execute(): grace + quantised wake, nested inside POM68K's irqDelay**

OLD:
```cpp
        // Process pending interrupt (if any)
        if (flags & CHECK_IRQ) {

            // POM68K: honour the one-instruction delay armed by a mask-
            // lowering SR-write — run the next instruction before taking
            // the IRQ (breaks the SimCity-2000 redraw livelock).
            if (irqDelay) {
                irqDelay--;
            } else {
                try {
```
NEW:
```cpp
        // Process pending interrupt (if any)
        // POM68K (ported from NeoST 993ce5e/2fc53a8): during a 68000 STOP the
        // interrupt is only taken at the end of a STOP ITERATION, every 4
        // cycles from stopGraceUntil (fx68k re-executes 0x3A2/0x327; Hatari
        // re-executes the opcode with do_cycles_stop(4)); the first
        // iteration tested with the old mask. The pin is still polled every
        // call; only the instant of acceptance is quantised, by ADVANCING to
        // the boundary (the clock can jump in bus-cycle steps; waiting to
        // cross it exactly kept NeoST's CPU stopped).
        if ((flags & CHECK_IRQ) && !((flags & STOPPED) && clock < stopGraceUntil)) {

            // POM68K: honour the one-instruction delay armed by a mask-
            // lowering SR-write — run the next instruction before taking
            // the IRQ (breaks the SimCity-2000 redraw livelock).
            if (irqDelay) {
                irqDelay--;
            } else {
                // POM68K: stopGraceUntil >= 0 confines the quantum to the
                // 68000 (NeoST has no other core; on the 68020+ it stays -1)
                if ((flags & STOPPED) && stopGraceUntil >= 0) {
                    const i64 mis = (clock - stopGraceUntil) & 3;
                    if (mis) sync(int(4 - mis));
                }
                try {
```

**8.4 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execStop: IRC operand, no bus cycle, arm the grace (NeoST verbatim, option A)**

OLD:
```cpp
    SUPERVISOR_MODE_ONLY

    u16 src = (u16)readI<C, Word>();

    setSR(src);
    flags |= State::STOPPED;
    reg.pc0 = reg.pc;

    //           00  10  20        00  10  20        00  10  20
    //           .b  .b  .b        .w  .w  .w        .l  .l  .l
    CYCLES_IP   ( 0,  0,  0,        4,  4,  8,        0,  0,  0)

    FINALIZE
```
NEW:
```cpp
    SUPERVISOR_MODE_ONLY

    // POM68K (ported from NeoST 2fc53a8): STOP is 4(0/0) — no bus cycle. The
    // operand is already in IRC; the silicon does not prefetch during STOP
    // (fx68k nanowords 0x3A2/0x327 have no access; Hatari: "STOP does not
    // prefetch anything", m68k_incpci(4) on wake). readI<Word> would have
    // consumed IRC and then prefetched the next word on the bus. The ways
    // out of STOP (exception, then RTE) reload the queue: a stale IRC is
    // harmless.
    u16 src;
    if constexpr (C == Core::C68000) { src = queue.irc; reg.pc += 2; }
    else                              { src = (u16)readI<C, Word>(); }

    setSR(src);
    flags |= State::STOPPED;
    reg.pc0 = reg.pc;

    //           00  10  20        00  10  20        00  10  20
    //           .b  .b  .b        .w  .w  .w        .l  .l  .l
    CYCLES_IP   ( 0,  0,  0,        4,  4,  8,        0,  0,  0)

    // POM68K (ported from NeoST 993ce5e): the first STOP iteration tested the
    // IRQ with the OLD mask (see stopGraceUntil) — an IRQ unmasked by the
    // operand is taken at the next iteration, 4 cycles later (fx68k
    // 0x3A2/0x327, Hatari do_cycles_stop(4)).
    if constexpr (C == Core::C68000) stopGraceUntil = clock + 4;

    FINALIZE
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group8.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green — by absence, not by construction.** The local corpus has **no STOP.json**
(124 files), so no vector executes STOP; the `execute()` change is inert unless `STOPPED` is set; the
`reset()` addition runs once per vector and is harmless (no vector leaves the CPU stopped). The gate
therefore says nothing about this group either way.

## Other gates that exercise the area

- No 68000 STOP gate exists (grep of `tests/` for `0x4E72`/STOP: only 020/030 uses).
- `berr030_test` (four `STOP #$2700` stubs on the 68030) and `v8_ramsize` (STOP on a V8-board 68020/68030)
  guard that the 020+ path is unchanged (`readI` kept, no quantum since `stopGraceUntil` stays −1).
- `savestate_68k_test`, `savestate_v8_test`, `compact_savestate_relaunch_etalon` once the field is
  serialised.
- `jit_lockstep_68000_test`, `portable_boot_etalon` / `PortableSleepProbe` (whether the Portable ROM
  idles in STOP is unverified).
- Missing and needed before applying: a `moira_rules_test` section — 68000, IRQ level 4 pending,
  `STOP #$2300` → exception start clock and the absence of a bus read during STOP; plus an IRQ raised
  1, 2, 3 cycles after entry → wake on the 4-cycle grid.

## Risk grade and recommendation

**High** (no gate; NeoST's own arithmetic inconsistent; save-state format change; touches POM68K's irqDelay block). Recommendation: **hold for arbitration** — evidence: MAME's microcode-derived `m68000` core (its generated microcode sources are not in the local clone `refs/mame`, which holds only `m68000.cpp`/`.h`) or fx68k simulation of `STOP #$2300` with a pending level-4 IRQ, deciding T0+4 vs T0+8; then apply option A or B with the new `moira_rules_test` section and the snapshot field.

### Option B (alternative to edit 8.4's operand line — not applied by default)

Replace, after applying 8.4:
```cpp
    if constexpr (C == Core::C68000) { src = queue.irc; reg.pc += 2; }
```
with:
```cpp
    if constexpr (C == Core::C68000) { src = queue.irc; reg.pc += 2; SYNC(4); }  // option B: 4(0/0) charged
```
This charges STOP's 4 internal cycles (no bus cycle) so a pending, operand-unmasked IRQ is taken at
T0+8, the behaviour NeoST's documentation describes; with option A it is taken at T0+4 (today's
timing). The quantum phase is unchanged modulo 4 either way.
