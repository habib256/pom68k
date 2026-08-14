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
| F2 | Sonora CLUT: blue-channel duplication missing in mono portrait | **WITHDRAWN 2026-08-13 — the finding was mis-scoped, and the behaviour was already right.** MAME applies the blue gun at the CLUT **write** (`mv_sonora.cpp:373-388` `dac_w`), keyed on the active modeline's `monochrome` flag, NOT in the decoder — and so does POM68K, in `SonoraMemory::dacWrite`. The audit looked in `SonoraVideo.h`, where MAME has no such path either. What was genuinely missing was the **gate**: no boot etalon reaches it (both LC III monitors are RGB). Now `sonora_video_test`, 11 checks, verified to bite (neutralising the blue gun fails exactly the 4 mono assertions). | `sonora_video_test` ✓ |
| F3 | Duo: `AscV8` `$E8` instead of the MSC variant `$E9` | **CLOSED 2026-08-13 — and it was not one byte.** `classic()` tested `version_ != 0xE8`, so an honest `$E9` would have turned the Duo into the Mac II *discrete* cell: stereo FIFO B, writable MODE/CONTROL, memory-mapped FIFO windows. The predicate now names the one classic part (`version_ == 0x00`), which is what MAME's hierarchy says — `asc_msc_device : asc_v8_device` overriding `get_version()` and nothing else. Bite-checked: the old predicate fails exactly the four new V8-behaviour assertions. | `msc_parity_test` ✓ (+5 checks) |
| F4 | Egret/Cuda PC3 reset line = boot release only | **CLOSED 2026-08-13 on the six platforms that carried an Egret/Cuda LLE, and on the seventh — the Eclipse Q900/950 — on 2026-08-14 when it gained one** (V8, Sonora, VASP, RBV, Q605, Q630, Q700-eclipse; *not* eight: Centris has no Egret at all). Deferred by construction, as the trap demanded: the callback latches `restartPending_` and re-arms the overlay, and the CPU wrapper consumes it at a run boundary — the Duo's `MscCpu.cpp:59` contract. `CudaLle::hostReset()` factors the action out of the PC3 handler so the gate drives the shipped path, not a replica. | `cuda_restart_test` ✓ (30 checks, both flavours, three bindings) |
| F5 | Classic ASC wavetable mode = silence stub | **CLOSED 2026-08-13.** The four-voice engine is in (`Asc.cpp`, MAME `asc.cpp:248-281`): 24-bit phase accumulators, bits 23-15 as a 512-byte index, voices 0/1 on FIFO A and 2/3 on FIFO B with odd voices on the upper half, offset-binary samples, no FIFO IRQ. The oscillators are their own state — two of the four pairs (`$821-$82F`) live past `regs_` — so they are serialized, which moved the snapshot format to **v5**. | `asc_test` ✓ (+16 checks) |
| F6 | Duo input through the ADB cell instead of the PMU matrix **+** PG&E NVRAM not persisted | **CLOSED 2026-08-14.** NVRAM persistence shipped with the 37th profile (`MscMemory.h:118-127`, `$91` power-flag scrub included); the matrix keyboard landed 2026-08-13 and the **trackball** on 2026-08-14 (`PgePmu`, quadrature counters `$14`-`$16` **latched at 60 Hz** — drained on read instead, a firmware poll that reads a register twice races itself and half the directions silently stop working). Nothing arrives on the ADB cell, which is what an *external* Duo keyboard/mouse would use: measured, the guest's own Mouse global never moves on that route (`POM68K_PGE_ADBMOUSE=1` keeps it for A/B). The original ordering advice — do it *before* the `kProfiles` row — was overtaken: the Duo shipped as a GUI profile on 2026-08-06 anyway. | `duo_soak/persist_etalon` ✓ (the persist leg types Cmd-N *and* steers the pointer into the Finder's Special menu) |
| F7 | Floppy flux/PLL — ideal cells | **OPEN, step 1 of 4.** `src/FluxPll.h` is gated (`flux_pll_test`) and still read by nothing but its own test. Would also wake SWIM1's dead LS-pair correction machinery and the CSM error bits. **But** no guest symptom since the boost/denibble fix — priority *behind* the `TODO.md` test-depth pass, not in front of it. | add a flux-path read behind a flag, compared bit-for-bit against the byte path |

**F2 to F6 are closed** (F2-F5 on 2026-08-13, F6 on 2026-08-14), each with
the gate named beside it. What is left of the seven is **F7** alone — the
floppy flux/PLL layer, step 1 of 4, deliberately behind the `TODO.md`
test-depth pass.

Two lessons the closures paid for, both worth more than the code:

- **F2 was not a defect, it was a mis-scoped reading** — the second time this
  review has spent a slot on a finding taken from a document rather than the
  code (F1 was the first). The behaviour had been correct since the Sonora
  landed; what was missing was a gate. *Read the code, and say which line.*
- **F3 was labelled "one byte" and was not.** The version byte was a
  *behaviour selector* through `classic()`, so the honest value would have
  silently reclassified the whole chip. A closure sized by its diff is sized
  wrong; the bite check is what proves which.

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
