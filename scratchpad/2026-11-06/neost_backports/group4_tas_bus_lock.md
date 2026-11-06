# Group 4 — setBusLock around the 68000 TAS read-modify-write

## NeoST change

Commit `ce1cc52` (2026-10-19). Moira files: `Moira.h` (declarations, in the same hunks as
group 3), `MoiraExec_cpp.h`.

```diff
# NeoST ce1cc52 — extern/moira/Moira/MoiraExec_cpp.h
@@ -5438,16 +5446,24 @@ Moira::execTasEa(u16 opcode)

     if (C == Core::C68000) {

-        readOp<C, M, Byte>(dst, &ea, &data);
+        // NeoST : /RMC interdit une prise de bus entre lecture et écriture.
+        // Libérer aussi le verrou si readOp/writeOp provoque une exception.
+        setBusLock(true);
+        try {

-        reg.sr.n = NBIT<Byte>(data);
-        reg.sr.z = ZERO<Byte>(data);
-        reg.sr.v = 0;
-        reg.sr.c = 0;
-        data |= 0x80;
+            readOp<C, M, Byte>(dst, &ea, &data);

-        if (!isRegMode(M)) SYNC(2);
-        writeOp<C, M, S>(dst, ea, data);
+            reg.sr.n = NBIT<Byte>(data);
+            reg.sr.z = ZERO<Byte>(data);
+            reg.sr.v = 0;
+            reg.sr.c = 0;
+            data |= 0x80;
+
+            if (!isRegMode(M)) SYNC(2);
+            writeOp<C, M, S>(dst, ea, data);
+
+        } catch (...) { setBusLock(false); throw; }
+        setBusLock(false);

         prefetch<C, POLL>();

```

(`Moira.h`: `virtual void setBusLock(bool /*locked*/) { }` in the virtual branch and
`void setBusLock(bool) { }` in the non-virtual branch — see group 3's `Moira.h` hunks.)

**NeoST rationale (translated).** `setBusLock` protects the memory TAS read-modify-write; released
before the prefetch and on exception. The MegaSTE blitter can thus resume mid-instruction without
breaking TAS atomicity. **Evidence:** Hatari strict timing probes including "TAS" among the ten
blitter probes; TAS memory/D0 tests in `tests/blitter_rmw_logic.cpp`.

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `execTasEa`, `if (C == Core::C68000)` branch | `MoiraExec_cpp.h` l.6555, `Moira::execTasEa` | The **68000 branch is identical**; POM68K changed only the `else` (68010/020+) branch: `mmuRmw = true` for the 68030 (row 5) and `mmu040Lrmw = true` for the 68040 (row 18) before `readOp`. 7 changed lines, none in the edited block. |
| `Moira.h` declarations | anchored before `// Reads a value from memory` in both API branches (`Moira.h` l.1109-1110 and l.1308-1309) | Identical context. |

**Conflicts flagged:** none textual. Notes:
- NeoST locks only the 68000 branch (the `if` is a plain runtime `if (C == Core::C68000)`, kept). The
  030/040 already model the locked RMW through `mmuRmw`/`mmu040Lrmw` (ATC/RMW semantics, not a bus
  grant) — left alone.
- The `try { } catch (...) { setBusLock(false); throw; }` rethrows POM68K's `AddressError`/`BusError`
  unchanged to `processException`; no interaction with (a) the address-error frame (built inside
  `readOp`/`writeOp`), (b) `irqDelay`, (c) JIT seams (TAS is a block boundary in `jit_backend_test`:
  `checkUnsafe(0x4AC0, "TAS (locked RMW)")`, and the threaded backend replays this handler).
- Not on a hot path: one virtual call pair per TAS; no switch needed (the group-3 switch is not
  required for this group).
- No POM68K consumer: no 68000 Macintosh has a second bus master that could be granted the bus
  mid-instruction (video/sound contention is modelled as wait states in `read16/write16`).

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**4.1 — `extern/moira/Moira/Moira.h` — Moira.h (virtual API branch): declare setBusLock**

OLD:
```cpp
    // Reads a value from memory
    virtual u8 read8(u32 addr) const = 0;
```
NEW:
```cpp
    // POM68K (ported from NeoST ce1cc52): /RMC of the 68000 TAS read-modify-
    // write — a bus master must not be granted the bus between the read and
    // the write. Called with true before the read and false after the write
    // (also on an exception out of either). TAS only: not a hot path.
    virtual void setBusLock(bool /*locked*/) { }

    // Reads a value from memory
    virtual u8 read8(u32 addr) const = 0;
```

**4.2 — `extern/moira/Moira/Moira.h` — Moira.h (non-virtual API branch): no-op twin**

OLD:
```cpp
    // Reads a value from memory
    u8 read8(u32 addr) const;
```
NEW:
```cpp
    void setBusLock(bool) { }  // POM68K (ported from NeoST ce1cc52)

    // Reads a value from memory
    u8 read8(u32 addr) const;
```

**4.3 — `extern/moira/Moira/MoiraExec_cpp.h` — MoiraExec_cpp.h execTasEa (68000 branch): lock around the RMW**

OLD:
```cpp
    if (C == Core::C68000) {

        readOp<C, M, Byte>(dst, &ea, &data);

        reg.sr.n = NBIT<Byte>(data);
        reg.sr.z = ZERO<Byte>(data);
        reg.sr.v = 0;
        reg.sr.c = 0;
        data |= 0x80;

        if (!isRegMode(M)) SYNC(2);
        writeOp<C, M, S>(dst, ea, data);

        prefetch<C, POLL>();
```
NEW:
```cpp
    if (C == Core::C68000) {

        // POM68K (ported from NeoST ce1cc52): /RMC forbids a bus grant
        // between the read and the write. Release the lock as well if
        // readOp/writeOp throws (address or bus error).
        setBusLock(true);
        try {

            readOp<C, M, Byte>(dst, &ea, &data);

            reg.sr.n = NBIT<Byte>(data);
            reg.sr.z = ZERO<Byte>(data);
            reg.sr.v = 0;
            reg.sr.c = 0;
            data |= 0x80;

            if (!isRegMode(M)) SYNC(2);
            writeOp<C, M, S>(dst, ea, data);

        } catch (...) { setBusLock(false); throw; }
        setBusLock(false);

        prefetch<C, POLL>();
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group4.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green, by construction** (TAS.json: empty hooks, same reads/writes, same
`SYNC(2)`, same prefetch, same cycles).

## Other gates that exercise the area

- `sst68000` (TAS.json, 8 065 vectors incl. address errors through the `catch (...)` rethrow).
- `jit_backend_test` (TAS stays a block boundary).
- No bare-Moira TAS gate with a bus master exists in POM68K.

## Risk grade and recommendation

**Low.** Recommendation: **apply as-is** (inert seam, plain virtual).
