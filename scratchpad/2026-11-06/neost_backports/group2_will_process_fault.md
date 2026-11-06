# Group 2 — willProcessFault — fault notification incl. double faults

## NeoST change

Commit `ce1cc52` (2026-10-19, "Renforce le timing MegaSTE et les gardes de fidélité vidéo"),
first bullet of NEOST_VENDOR.md § "Bornes bus et trace d'instruction". Moira files: `Moira.cpp`,
`Moira.h` (the same `Moira.h` hunk also carries group 3/4 declarations).

```diff
# NeoST ce1cc52 — extern/moira/Moira/Moira.cpp
@@ -439,12 +439,14 @@ Moira::processException(const std::exception &exc)

         if (auto ae = dynamic_cast<const AddressError *>(&exc); ae) {

+            willProcessFault(ae->stackFrame);
             execAddressError<C>(ae->stackFrame);
             return;
         }

         if (auto be = dynamic_cast<const BusError *>(&exc); be) {

+            willProcessFault(be->stackFrame);
             execBusError<C>(be->stackFrame);
             return;
         }
@@ -463,15 +465,17 @@ Moira::processException(const std::exception &exc)
         halt();
         return;

-    } catch (AddressError &) {
+    } catch (AddressError &ae) {

+        willProcessFault(ae.stackFrame);
         // NEOST : erreur d'adresse PENDANT le traitement d'une exception groupe 0
         // (ex. vecteur BUS ERROR impair) = double faute → HALT, comme le vrai 68000.
         halt();
         return;

-    } catch (BusError &) {
+    } catch (BusError &be) {

+        willProcessFault(be.stackFrame);
         // NEOST : bus error pendant l'empilement d'une exception groupe 0
         // (pile dans une zone bus-error) = double faute → HALT.
         halt();
```

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
```

**NeoST rationale (translated).** Protected hook `willProcessFault(StackFrame)` called for bus and
address errors, initial or nested. NeoST records the last address, direction and PC there so that a
HALT on an odd address does not keep the diagnosis of an older MMIO probe. The hook stays virtual
even outside `MOIRA_VIRTUAL_API`, like the fault callbacks used by the wrapper; no change to the CPU
flow. **Evidence:** diagnostic only (no timing claim); covered by NeoST's halt-diagnosis path
(`Cpu68k.cpp:328`).

## POM68K target

| NeoST hunk | POM68K location now | Changed since the copy? |
|---|---|---|
| `Moira.h`: `protected:` + virtual `willProcessFault` before `#if MOIRA_VIRTUAL_API == true` | `Moira.h` l.1087-1104: POM68K inserted the MC68020 CALLM/RTM CPU-space hooks (`readModuleCpuSpace8`, `writeModuleCpuSpace8`, `writeModuleCpuSpace32`, row 25) between `protected:` and the `#if`. The edit anchors after `writeModuleCpuSpace32`. | Yes (insertion point moved by row 25), no semantic conflict. |
| `Moira.cpp` `processException<C>`: AE/BE `dynamic_cast` branches | `Moira.cpp` l.750-760, `template <Core C> void Moira::processException(const std::exception &)` | The AE/BE branches and the two `catch` blocks are byte-identical to NeoST's pre-change text; POM68K **prepended** two `Core::C68020` blocks (FPU mid-instruction interrupt l.713-725, `MmuBusError` l.730-748 with row 37's `fcSource = 0`). |
| `Moira.cpp` `catch (AddressError &)` / `catch (BusError &)` | l.776-789, same function | Identical. |

POM68K current (excerpt, l.727-760):
```cpp
        // POM68K O4 slice 3: 68030 MMU translation fault → bus error with
        // a format $A/$B frame; a nested fault while stacking it is a
        // double fault → HALT (WinUAE Exception_mmu030 / cpu_halt)
        if constexpr (C == Core::C68020) {
            if (dynamic_cast<const MmuBusError *>(&exc)) {
                ...
            }
        }

        if (auto ae = dynamic_cast<const AddressError *>(&exc); ae) {

            execAddressError<C>(ae->stackFrame);
            return;
        }
```
**Conflicts flagged:**
- (a) SST address-error machinery (`makeFrame` PC−2 on 68000 data faults, row 2): untouched — the
  hook receives the frame *after* `makeFrame` built it, i.e. the POM68K-corrected frame.
- The 68030/68040 `MmuBusError` path (format $A/$B/$7 frames) has **no `StackFrame`** and is **not
  reported** by this port (NeoST has no MMU). A POM68K consumer wanting MMU faults would need a
  second hook or an overload — out of scope, recorded as a deliberate gap in the comment.
- Plain-68020 external /BERR (row 21) goes through `BusError` and **is** reported.
- (b) `irqDelay`, (c) JIT seams: not touched.
- No consumer in POM68K today (candidate: the debugger's halt diagnosis, `DebugSession`).

## Ready-to-apply edit list for POM68K

Exact replacements against POM68K's current text; each OLD block occurs exactly once in its file.
Apply in the listed order.

**2.1 — `extern/moira/Moira/Moira.h` — Moira.h: declare willProcessFault (protected, before the virtual-API block)**

OLD:
```cpp
    virtual bool writeModuleCpuSpace32(u32 addr, u32 val) const {
        (void)addr; (void)val; return false;
    }
```
NEW:
```cpp
    virtual bool writeModuleCpuSpace32(u32 addr, u32 val) const {
        (void)addr; (void)val; return false;
    }

    // POM68K (ported from NeoST ce1cc52): the fault being processed,
    // including one that interrupts an exception (double fault -> HALT).
    // Called for AddressError/BusError, whose StackFrame it receives; the
    // 68030/68040 MMU faults (MmuBusError, format $A/$B/$7) are NOT reported.
    // Fault path only, so virtual regardless of MOIRA_VIRTUAL_API, as in NeoST.
    virtual void willProcessFault(const StackFrame & /*frame*/) { }
```

**2.2 — `extern/moira/Moira/Moira.cpp` — Moira.cpp processException<C>: report the initial fault**

OLD:
```cpp
        if (auto ae = dynamic_cast<const AddressError *>(&exc); ae) {

            execAddressError<C>(ae->stackFrame);
            return;
        }

        if (auto be = dynamic_cast<const BusError *>(&exc); be) {

            execBusError<C>(be->stackFrame);
            return;
        }
```
NEW:
```cpp
        if (auto ae = dynamic_cast<const AddressError *>(&exc); ae) {

            willProcessFault(ae->stackFrame);  // POM68K (ported from NeoST ce1cc52)
            execAddressError<C>(ae->stackFrame);
            return;
        }

        if (auto be = dynamic_cast<const BusError *>(&exc); be) {

            willProcessFault(be->stackFrame);  // POM68K (ported from NeoST ce1cc52)
            execBusError<C>(be->stackFrame);
            return;
        }
```

**2.3 — `extern/moira/Moira/Moira.cpp` — Moira.cpp processException<C>: report the nested (double) fault**

OLD:
```cpp
    } catch (AddressError &) {

        // NEOST : erreur d'adresse PENDANT le traitement d'une exception groupe 0
        // (ex. vecteur BUS ERROR impair) = double faute → HALT, comme le vrai 68000.
        halt();
        return;

    } catch (BusError &) {

        // NEOST : bus error pendant l'empilement d'une exception groupe 0
        // (pile dans une zone bus-error) = double faute → HALT.
        halt();
```
NEW:
```cpp
    } catch (AddressError &ae) {

        willProcessFault(ae.stackFrame);  // POM68K (ported from NeoST ce1cc52)
        // NEOST : erreur d'adresse PENDANT le traitement d'une exception groupe 0
        // (ex. vecteur BUS ERROR impair) = double faute → HALT, comme le vrai 68000.
        halt();
        return;

    } catch (BusError &be) {

        willProcessFault(be.stackFrame);  // POM68K (ported from NeoST ce1cc52)
        // NEOST : bus error pendant l'empilement d'une exception groupe 0
        // (pile dans une zone bus-error) = double faute → HALT.
        halt();
```


Mechanical check of this edit list: `tools/verify.py` (same directory) asserts
that every OLD block occurs exactly once in POM68K's current working tree
(`/home/gistarcade/src/pom68k/extern/moira/Moira/`, branch
`refactor/core-no-getenv`, uncommitted edits included), applies the group on a
scratch copy, and applies all eight groups cumulatively in the recommended
order — all OK on 2026-11-06. The same edits as a unified diff (paths
`a/extern/moira/Moira/...`, for `git apply` from the repository root):
`patches/group2.patch`. **Not compiled** (no build allowed during this audit).

## Predicted effect on `sst68000`

**(i) stays fully green, by construction** — empty default; `TestCpu` does not override it. No
cycle, register or bus effect. The call sits on the fault path only (cost irrelevant).

## Other gates that exercise the area

- `sst68000` (address-error vectors in every file with memory operands; group-0 frames are
  compared through RAM cells and SSP).
- `sst68030` / `sst68040` / `berr030_test` exercise the neighbouring `MmuBusError` and
  `extBusError` paths that the edit does not modify (regression guards for the context).
- `debug_session_test`, `debug_inspection_test` (halt/stop reporting; not a consumer yet).

## Risk grade and recommendation

**Low.** Recommendation: **apply as-is** (inert seam).
