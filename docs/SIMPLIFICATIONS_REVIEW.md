# Deliberate simplifications — the keep/close decision, and where it stands

Decision review of the ~40 simplifications the MAME parity audit turned up
(`MAME_PARITY_AUDIT.md` § 2.x), crossed with the `LLE_VS_HLE.md` inventory.
Written 2026-08-06; **status column re-verified against the code 2026-08-12.**

Three verdicts were available: **KEEP** (no known consumer, or strict parity
would be *worse* — it mutes a machine or breaks a measured fix), **CLOSE**
(a guest-visible benefit exists; goes to the backlog with its gate), **PIN**
(keep the shortcut, but write the inventory entry and the reopening condition
the house rule demands — `LLE_VS_HLE.md` § 5).

The rule that dominates everything: *"Adding fidelity on top of unverifiable
coverage is work with no way to know it landed"* — 28 of the 37 profiles are
pinned only by "it reached the Finder". **Every closure recommended here must
bring its gate.**

## Verdict, and what has happened since

**28 of 40: KEEP.** Same profile every time — the absent behaviour has no
producer or consumer anywhere in the target software (Apple ROMs, System 6 →
Mac OS 8.1, shipped drivers), and half were already inventoried with a
reopening condition. Redoing them would be fidelity with neither oracle nor
gate. The per-chip lists live in `MAME_PARITY_AUDIT.md` § 2.x under
*Simplifications*; they are not repeated here.

**7: CLOSE.** Status below.

**5: PIN.** All five are now inventory entries in `LLE_VS_HLE.md` (done
2026-08-12) — that action is complete.

### The seven closures

| # | Simplification | Status 2026-08-12 | Gate |
|---|---|---|---|
| F1 | "PRAM persistence missing on 4 platforms" | **WITHDRAWN — the finding was false.** All twelve platforms declare `loadPram`/`savePram` and all twelve runners wire them (`main.cpp`, first pair `:1117`/`:1340`). The claim came from a stale `CLAUDE.md` table row, never from the code. `MAME_PARITY_AUDIT.md` § 2.2 corrected. | `rtc_pram_test` |
| F2 | Sonora CLUT: blue-channel duplication missing in mono portrait | **OPEN.** `SonoraVideo.h` has no blue-gun path; `RbvVideo.h:66-72` has it. Intra-project inconsistency more than a simplification; trivial. | a check in an existing Sonora video test |
| F3 | Duo: `AscV8` `$E8` instead of the MSC variant `$E9` | **OPEN.** `MscMemory.h:222` still carries the in-file TODO. Clone + version register, with the next Duo milestone. | `msc_parity_test` (exists) |
| F4 | Egret/Cuda PC3 reset line = boot release only | **OPEN, and it is the last thread of the whole audit.** A firmware `RESET_SYSTEM $11` never reaches the 68k (`CudaLle.cpp:273-297`) — that is the Finder's *Restart*. The Duo half shipped (`PgePmu::onCpuReset`), so the pattern exists. Inventoried at `LLE_VS_HLE.md` § 1.9. | a new "Restart from the Finder" etalon on an Egret machine — **no gate walks this path today** |
| F5 | Classic ASC wavetable mode = silence stub | **OPEN.** `Asc.cpp:124`, `:213`. The only simplification with a plausible *audible* miss (Mac II-era wavetable playback), and the one where an oracle exists: MAME implements it and ships the ASCTester dump. Inventoried at `LLE_VS_HLE.md` § 1.7. | extend `asc_test` (wavetable mode, registers 2/3, non-silent output) |
| F6 | Duo input through the ADB cell instead of the PMU matrix **+** PG&E NVRAM not persisted | **HALF DONE.** NVRAM persistence shipped with the 37th profile (`MscMemory.h:118-127`, `$91` power-flag scrub included). The matrix keyboard and trackball are still milestone 4 (`MscMemory.h:128-131`, `DUO_BRINGUP.md`). The original ordering advice — do it *before* the `kProfiles` row — was overtaken: the Duo shipped as a GUI profile on 2026-08-06 anyway. | the Duo milestone gates |
| F7 | Floppy flux/PLL — ideal cells | **OPEN, step 1 of 4.** `src/FluxPll.h` is gated (`flux_pll_test`) and still read by nothing but its own test. Would also wake SWIM1's dead LS-pair correction machinery and the CSM error bits. **But** no guest symptom since the boost/denibble fix — priority *behind* the `TODO.md` test-depth pass, not in front of it. | add a flux-path read behind a flag, compared bit-for-bit against the byte path |

**Recommended order, unchanged: F2/F3 → F4 → F5 → F6 → F7.** F2-F4 are
regret-free closures. F5-F7 each deserve a dated `TODO.md` entry.

## The standing conclusion

1. **The current policy holds.** The multi-agent audit found **no hidden,
   unowned simplification** — only 5 pinning defects, and those are now fixed.
   The triptych *written reason + reopening condition + announced fallback*
   is doing its job; keep applying it.
2. **A "simplification" read in a document and not in the code is not a
   finding.** F1 cost a real audit slot because a stale doc line was taken as
   evidence. Read the code.
3. **Coverage still outranks fidelity.** Nothing on the CLOSE list should
   land without the gate named beside it.
