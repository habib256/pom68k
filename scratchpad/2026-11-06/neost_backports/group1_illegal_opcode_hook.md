# Group 1 — illegalOpcodeHook — delegable illegal instruction

## NeoST change

Commit `fdbce2e` (2026-10-01, "GEMDOS HD : un opcode magique n'est plus rejoué au retour
d'interruption"). Moira files: `Moira.h`, `MoiraExec_cpp.h`.

```diff
# NeoST fdbce2e — extern/moira/Moira/Moira.h
@@ -362,6 +362,14 @@ protected:
     virtual int iackSyncBefore(u8 level) { return 4; }
     virtual int iackSyncAfter(u8 level) { return 4; }

+    // NEOST : instruction illégale DÉLÉGABLE. Appelé par execIllegal<C>() AVANT la
+    // prise de l'exception ILLEGAL, donc quand l'instruction s'exécute VRAIMENT
+    // (interruptions et trace déjà arbitrées par execute()). Renvoie true si le
+    // sous-classeur a traité l'opcode : il est alors consommé comme un NOP
+    // (prefetch, 4 cycles). Sert aux opcodes magiques GEMDOS HD de NeoST
+    // ($0008-$000A), comme les gestionnaires OpCode_GemDos/Pexec/SysInit d'Hatari.
+    virtual bool illegalOpcodeHook(u16 opcode) { return false; }
+
     // Called when the CPU jumps to an exception vector
     virtual void didJumpToVector(int nr, u32 addr) { }

```

```diff
# NeoST fdbce2e — extern/moira/Moira/MoiraExec_cpp.h
@@ -77,6 +77,15 @@ Moira::execIllegal(u16 opcode)
 {
     AVAILABILITY(Core::C68000)

+    // NEOST : opcode traité par le sous-classeur → consommé comme un NOP
+    // (cf. Moira.h illegalOpcodeHook).
+    if (illegalOpcodeHook(opcode)) {
+
+        prefetch<C, POLL>();
+        FINALIZE
+        return;
+    }
+
     execException<C>(M68kException::ILLEGAL);

     CYCLES_68000(34)
```

**NeoST rationale (NEOST_VENDOR.md, translated).** NeoST's host GEMDOS disk relies on magic
"illegal" opcodes ($0008 GEMDOS, $0009 PEXEC, $000A SYSINIT). They used to be intercepted in
`Cpu68k::run` *before* `execute()`, which replaced IRD with a NOP; but `execute()` arbitrates
interrupts before running IRD, so when an IRQ was takeable at that boundary the host call was
served, the IRQ taken, and the stacked PC still pointed at the magic opcode, replayed at `rte`
(a `Fread` served twice, a block silently skipped). Hatari handles these opcodes inside their
instruction handler (`OpCode_GemDos`), once per real execution; the patch does the same.
**Evidence:** NeoST serial self-test `gemdos_irq` (`tools/make_gemdos_irq_test.py`), verdict
`gdforced` makes the IRQ takeable exactly at the $0008 boundary. Hatari as behavioural model.

## POM68K target

| NeoST hunk | POM68K location now | POM68K changed those lines since the copy? |
|---|---|---|
| `Moira.h` after `iackSyncAfter` | `Moira.h` ~l.1168-1172, protected section under `#if MOIRA_VIRTUAL_API == true` (block "Exception delegates") | No. The two `iackSync*` lines and `didJumpToVector` are byte-identical to NeoST's pre-change context. |
| `MoiraExec_cpp.h` `execIllegal` | `MoiraExec_cpp.h` l.110, `Moira::execIllegal` | No — function identical to NeoST@`47d6c39` (checked with `tools/fncmp.py`). |

Current POM68K code (unchanged upstream text):
```cpp
Moira::execIllegal(u16 opcode)
{
    AVAILABILITY(Core::C68000)

    execException<C>(M68kException::ILLEGAL);

    CYCLES_68000(34)
    CYCLES_68010(38)
    CYCLES_68020(20)

    FINALIZE
}
```
**Conflicts:** none textual. Semantic notes for POM68K:
- `execIllegal` is instantiated for all three cores; on `Core::C68020` (020/030/040) a consumed
  opcode charges **0 cycles** (`prefetch` SYNCs are empty there and `CYCLES_68020(20)` is skipped
  by the early `return`), and on the 030/040 `prefetch<C,POLL>` only sets `pc0` (mode-5 fetch
  model, row 5/18) — the next opcode is fetched by `mmuExecuteStart`/`mmu040InstrStart`. Harmless,
  but a consumer on a 68020+ machine should charge its own cycles.
- No JIT conflict: `src/jit` has no native ILLEGAL path (grep), the threaded backend replays
  Moira's handlers, so the hook is reached on every engine.
- (a) SST convergence, (b) `irqDelay`, (c) JIT fetch/watchpoint seams: not touched.
- There is **no consumer** in POM68K today (nothing like NeoST's GEMDOS host calls); the value is
  textual convergence plus a ready seam.

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**1.1 — `extern/moira/Moira/Moira.h` — Moira.h: declare illegalOpcodeHook (virtual API section, after iackSyncAfter)**

OLD:
```cpp
    virtual int iackSyncBefore(u8 level) { return 4; }
    virtual int iackSyncAfter(u8 level) { return 4; }

    // Called when the CPU jumps to an exception vector
```
NEW:
```cpp
    virtual int iackSyncBefore(u8 level) { return 4; }
    virtual int iackSyncAfter(u8 level) { return 4; }

    // POM68K (ported from NeoST fdbce2e): delegable illegal instruction.
    // Called by execIllegal<C>() BEFORE the ILLEGAL exception is taken, so
    // only when the instruction really executes (interrupts and trace have
    // already been arbitrated by execute()). Returning true means the
    // subclass handled the opcode: it is then consumed like a NOP
    // (prefetch<C, POLL>: 4 cycles on the 68000/68010; nothing is charged on
    // Core::C68020, where SYNC is empty). Default false = upstream behaviour.
    // NeoST serves its GEMDOS host-call opcodes ($0008-$000A) through it.
    virtual bool illegalOpcodeHook(u16 /*opcode*/) { return false; }

    // Called when the CPU jumps to an exception vector
```

**1.2 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execIllegal: consult the hook before the exception**

OLD:
```cpp
Moira::execIllegal(u16 opcode)
{
    AVAILABILITY(Core::C68000)

    execException<C>(M68kException::ILLEGAL);
```
NEW:
```cpp
Moira::execIllegal(u16 opcode)
{
    AVAILABILITY(Core::C68000)

    // POM68K (ported from NeoST fdbce2e): opcode handled by the subclass ->
    // consumed like a NOP (see Moira.h illegalOpcodeHook).
    if (illegalOpcodeHook(opcode)) {

        prefetch<C, POLL>();
        FINALIZE
        return;
    }

    execException<C>(M68kException::ILLEGAL);
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group1.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green, by construction.** The default returns `false`, so `execIllegal` runs
exactly the current code; `TestCpu` in `tests/sst68000.cpp` does not override the hook. Cost: one
virtual call on the illegal-instruction path only (never on a hot path).
The harness compares D0-D7, A0-A6, USP, SSP, SR, PC (`getPC0`), IRD, IRC, the final RAM cells and
the total `length`; it **skips the `transactions` array** (`parseVector`: "else skipValue(p); //
transactions") — no bus-transaction comparison anywhere in `sst68000`.

## Other gates that exercise the area

- `sst68000` (illegal opcodes appear only as exception paths of other files; there is no
  ILLEGAL.json in the local corpus — 124 files, none named ILLEGAL/STOP).
- No gate executes `$4AFC`/illegal on a bare Moira (`grep` of `tests/`); `jit_backend_test`
  only asserts that illegal encodings are not emitted natively.
- All machine etalons pass through `execIllegal` only if a guest executes an illegal opcode
  (unverified per guest).

## Risk grade and recommendation

**Low.** Recommendation: **apply as-is** (inert seam; first in order). Worth applying only if textual convergence with NeoST is wanted; nothing in POM68K consumes it yet.
