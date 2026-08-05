# CHANGELOG

POM68K's memory of **why**. Every entry is a dated snapshot of what was
believed, measured and changed on that day — root causes, wrong turns,
numbers. **Nothing here is deleted when it turns out to be wrong**: a later
entry corrects it, and the two are cross-linked under
[Retractions, reversals and corrections](#retractions-reversals-and-corrections).
Read an old entry as history, not as current truth — for the current state of
the tree see `CLAUDE.md` (index), `DEV.md` (internals) and `TODO.md` (backlog).

**Format.** One entry = one `## YYYY-MM-DD — hook` heading, newest first;
`grep -n '^## 20' CHANGELOG.md` lists all 175 entries in order. The hook
states the *finding*, not the files touched. Several entries on one day carry
a qualifier — `(later)`, `(evening)`, `(third pass)` — and are likewise newest
first. A `> **Superseded:**` blockquote under a heading points at the entry
that overturned it (`grep -n '^> \*\*Superseded' CHANGELOG.md`).

**Adding an entry:** prepend the section, add ONE line to
[Index by date](#index-by-date), and add a pointer to
[Index by topic](#index-by-topic) if it answers a question a future reader
will ask.

---

## Index by topic

Each line is a **question a reader arrives with**, pointing at the entry that
answers it. Not exhaustive — the complete list is [by date](#index-by-date).

### Retractions, reversals and corrections

- **the 7.5.5 hot-insert refusal is NOT a dskchg modelling gap (mac_floppy re-arms it on insertion)** → [2026-08-05 (fourth) — IWM/SWIM bughunt…](#2026-08-05-iwm-swim-bughunt)

- **a belief held for a whole day and overturned three times: Q6.4 "not a Cuda framing bug" … then it was** → [2026-07-19 — Q6.4 re-localized: it is a System-launch HANDOFF failure, NOT a Cuda…](#2026-07-19--q64-re-localized-it-is-a-system-launch-handoff-failure-not-a-cuda-reply-framing-bug-the-prior-completion-isr-buffer-smash-lead-is-disproven-no-fix-landed-yet)
- **…and the entry that closed it** → [2026-07-19 — Q6.4 + Q6.2 BOTH RESOLVED: the boot restart loop AND the block-0 loop…](#2026-07-19--q64--q62-both-resolved-the-boot-restart-loop-and-the-block-0-loop-were-one-coupled-cuda-reply-framing-bug-the-system-now-loads)
- **the Q605 `CACHE_BOOST` default of 1 ("boost 2+ fails SCSI bring-up") was stale — it is 4 now** → [2026-07-25 — Quadra 800 (26th machine), the 040 boost ceiling lifted…](#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted)
- **…the measurement that originally pinned it at 1** → [2026-07-20 — Q8.8: CACHE_BOOST calibration (default stays 1)](#2026-07-20--q88-cache_boost-calibration-default-stays-1)
- **the Egret LLE went default → opt-in → default again on the LC II** → [2026-07-23 — LC II: Egret firmware LLE back to OPT-IN (mouse starvation)](#2026-07-23--lc-ii-egret-firmware-lle-back-to-opt-in-mouse-starvation)
- **…and what made it a safe default (event-driven ADB wire)** → [2026-07-24 — Event-driven ADB wire: the Egret firmware LLE is the LC II DEFAULT](#2026-07-24--event-driven-adb-wire-the-egret-firmware-lle-is-the-lc-ii-default)
- **the Color Classic "Cuda 0417 wedge" was never a core bug** → [2026-07-29 — The Color Classic "0417 wedge" was a missing DFAC2, not a core bug…](#2026-07-29--the-color-classic-0417-wedge-was-a-missing-dfac2-not-a-core-bug-both-factory-cudas-land)
- **"the IIsi has no working ADB" was wrong three times over — `peek8()` is PHYSICAL** → [2026-07-29 — Input-delivery gates for the 030 families…](#2026-07-29--input-delivery-gates-for-the-030-families-loud-hle-fallbacks-and-a-retracted-bug)
- **the "PGO divergence" in the JIT was the U bit, not the optimizer** → [2026-07-28 (fifth pass) — The "PGO divergence" was the U bit all along](#2026-07-28-fifth-pass--the-pgo-divergence-was-the-u-bit-all-along)
- **"the break is between the ADB driver and the Event Manager" was wrong — KeyTime was a false observable, the cause was Slow Keys in the image** → [2026-07-31 — The ten-month red gate was Slow Keys](#2026-07-31-slow-keys)
- **…and the "Quadra modifier path has a second cause" that survived it was ALSO the image** → [2026-08-02 — The "Quadra modifier bug" retracted](#2026-08-02-cmdn-retracted)
- **a 143/143 ctest that was quoted in `CLAUDE.md` before anyone checked the binaries were fresh** → [2026-08-03 — Three items closed by measurement](#2026-08-03-three-items)
- **the "DAFB sense" theory for the Slot-Manager blocker is disproven** → [2026-07-18 — Q5.1a: the Slot-Manager blocker re-localized…](#2026-07-18--q51a-the-slot-manager-blocker-re-localized--the-dafb-sense-theory-is-disproven-the-fault-is-a-decl-rom-parse)
- **the "×1.6 on the LC II" fetch-window figure went stale when ATC capping landed** → [2026-07-30 — Engine re-baseline (idle host)…](#2026-07-30--engine-re-baseline-idle-host--the-cpu-menu-reaches-the-030s)
- **the JIT "density" item was deprioritized once the idle ceiling was measured** → [2026-07-30 — JIT measured honestly: x64 wins both regimes…](#2026-07-30--jit-measured-honestly-x64-wins-both-regimes-the-next-lever-is-5-opcodes)
- **a boot hang that was NOT the pending changes and NOT the disk** → [2026-07-18 — GISTPERSO (7.5) boot hang: heap corruption racing an app launch at…](#2026-07-18--gistperso-75-boot-hang-heap-corruption-racing-an-app-launch-at-finder-startup--not-the-pending-changes-not-the-disk)
- **"dead arrow keys" that were not a bug at all** → [2026-07-17 — Lode Runner "dead arrow keys"…](#2026-07-17--lode-runner-dead-arrow-keys-not-a-bug--the-game-binds-the-numeric-keypad-by-default)
- **the adaptive cache boost, introduced then retired the same day** → [2026-07-17 — retire the adaptive cache boost for a constant ratio](#2026-07-17--retire-the-adaptive-cache-boost-for-a-constant-ratio)

### Timing — what an emulated cycle is charged against

- **why Q605/V8 wake peripherals from conservative event deadlines, and why the 6805 IRQ costs 11 again** → [2026-08-03 — Event deadlines close the Cuda phase accommodation](#2026-08-03-event-deadlines)
- **why the VIA E clock is 783.36 kHz and not a divisor of the CPU** → [2026-08-02 (third) — Two rates that were rounded…](#2026-08-02-eclock-asc)
- **why bus/peripheral time is charged in MACHINE cycles, never the boosted core clock** → [2026-07-25 — The i-cache boost was accelerating the VIA bus…](#2026-07-25--the-i-cache-boost-was-accelerating-the-via-bus-lc-iii--lc-iii--iivx-fixed-and-the-iisis-boost-restored)
- **why the PIC1654S must be co-stepped off the un-boosted clock** → [2026-07-25 — Quadra 800 (26th machine), the 040 boost ceiling lifted…](#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted)
- **why the MCU must carry `run()` overshoot as debt (RTC drift)** → [2026-07-24 — Beyond-boot gates on the LC II…](#2026-07-24--beyond-boot-gates-on-the-lc-ii--a-clock-drift-bug-they-caught)
- **why a 2 % shift in MCU instruction rate is a deadlock, not a slowdown** → [2026-07-27 — The Macintosh TV boots again…](#2026-07-27--the-macintosh-tv-boots-again-a-2--mcu-shift-is-a-deadlock)
- **the 68030 i-cache throughput model that replaced the flat boost** → [2026-07-17 — 68030 instruction-cache timing overlay (replaces the flat boost)](#2026-07-17--68030-instruction-cache-timing-overlay-replaces-the-flat-boost)
- **…folded into Moira's fetch path** → [2026-07-17 — i-cache overlay folded into Moira's fetch path (-15%)](#2026-07-17--i-cache-overlay-folded-into-moiras-fetch-path--15)
- **the 040 I/D ATC + throughput overlay** → [2026-07-20 — Q8.7: 040 I/D ATC + Cpu040 throughput overlay](#2026-07-20--q87-040-id-atc--cpu040-throughput-overlay)
- **sound tempo locked to the host DAC; odd-SP interrupt frames** → [2026-07-17 — Lode Runner launch freeze: odd-SP interrupt frames were corrupt…](#2026-07-17--lode-runner-launch-freeze-odd-sp-interrupt-frames-were-corrupt-vendored-moira-fix--sound-tempo-locked-to-the-host-dac)

### Execution engines — the interpreter, the JIT, PGO

- **AArch64 becomes automatic; two quadratic/128-MiB publication costs removed** → [2026-08-04 — AArch64 Finder gate green and fast](#2026-08-04-a64-green-fast)
- **the JIT design: host-agnostic engine + `jit::Backend`** → [2026-07-27 — A second execution engine: the multi-target JIT (J0 + J1)](#2026-07-27--a-second-execution-engine-the-multi-target-jit-j0--j1)
- **the x86-64 code generator and what it measured** → [2026-07-28 — The x86-64 code generator (J2), and what it measured](#2026-07-28--the-x86-64-code-generator-j2-and-what-it-measured)
- **why the fetch WINDOW is the win, not the block cache** → [2026-07-28 (fourth pass) — The data window and PGO: the interpreter's turn](#2026-07-28-fourth-pass--the-data-window-and-pgo-the-interpreters-turn)
- **block linking, and why LINK/UNLK/NOP left the exclusion list** → [2026-07-28 (later) — Block linking, and LINK/UNLK/NOP out of the exclusion list](#2026-07-28-later--block-linking-and-linkunlknop-out-of-the-exclusion-list)
- **the density experiment and its honest result** → [2026-07-28 (third pass) — The density work, and what it finally measured](#2026-07-28-third-pass--the-density-work-and-what-it-finally-measured)
- **O(1) probes, per-space eviction, and the 020 seam** → [2026-07-28 (eighth pass) — O(1) probes, per-space eviction, and the 020 seam](#2026-07-28-eighth-pass--o1-probes-per-space-eviction-and-the-020-seam)
- **why the JIT reached the 68030 (V8) and then all four 030 families** → [2026-07-28 (sixth pass) — The JIT reaches the 68030: the V8 family](#2026-07-28-sixth-pass--the-jit-reaches-the-68030-the-v8-family)
- **…all four 030 families** → [2026-07-28 (seventh pass) — All four 030 families under the engine](#2026-07-28-seventh-pass--all-four-030-families-under-the-engine)
- **why a backend is valid per GUEST family, not just per host** → [2026-07-30 — A JIT backend is valid per GUEST family, not just per host](#2026-07-30--a-jit-backend-is-valid-per-guest-family-not-just-per-host)
- **the honest re-measure: x64 wins both regimes; the idle ceiling is the exactness contract** → [2026-07-30 — JIT measured honestly: x64 wins both regimes…](#2026-07-30--jit-measured-honestly-x64-wins-both-regimes-the-next-lever-is-5-opcodes)
- **MOVEM + DBcc + JMP compiled (the five census opcodes)** → [2026-07-30 — The five opcodes, same day: MOVEM + DBcc + JMP compiled](#2026-07-30--the-five-opcodes-same-day-movem--dbcc--jmp-compiled)
- **the biggest JIT win since the fetch window: one deleted arm-time DTLB flush** → [2026-07-31 — The window-churn investigation ends on one deleted line…](#2026-07-31-window-churn-dtlb-flush)
- **PGO trained per CPU family (−26 % on the LC II); why the page-granular dispatch table was dropped** → [2026-07-29 (late) — PGO across all four CPU families (−26 % on the LC II)…](#2026-07-29-pgo-four-cpu-families)
- **the first performance pass: 0.40× → 1.91× realtime** → [2026-07-17 — Performance pass: 0.40× → 1.91× realtime at the Finder (the sound…](#2026-07-17-performance-pass-realtime)

### CPU cores, MMU/FPU, and the WinUAE oracle

- **68000: 1 000 058 SingleStepTests vectors** → [2026-07-14 — M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060](#2026-07-14--m45-singlesteptests680x0--1-000-058--1-000-060)
- **the two-oracle arbitration loop starts** → [2026-07-15 — Phase 2 live: two 68030 oracles + arbitration turn 1](#2026-07-15--phase-2-live-two-68030-oracles--arbitration-turn-1)
- **Moira executes the 68030 MMU instructions** → [2026-07-15 — O4 slice 1: Moira executes the 68030 MMU instructions](#2026-07-15--o4-slice-1-moira-executes-the-68030-mmu-instructions)
- **the 68030 MMU bus layer** → [2026-07-15 — O4 slice 3: the 68030 MMU bus layer (Moira translates)](#2026-07-15--o4-slice-3-the-68030-mmu-bus-layer-moira-translates)
- **integer-family arbitration (O4 complete)** → [2026-07-15 — O4 slice 4: integer-family arbitration (O4 complete)](#2026-07-15--o4-slice-4-integer-family-arbitration-o4-complete)
- **why Musashi was retired and the loop went WinUAE-solo** → [2026-07-15 — Musashi oracle retired: the loop is WinUAE-solo](#2026-07-15--musashi-oracle-retired-the-loop-is-winuae-solo)
- **68882 FPU execution** → [2026-07-15 — O5 slice 2: 68882 FPU execution in Moira](#2026-07-15--o5-slice-2-68882-fpu-execution-in-moira)
- **68882 timing + FRESTORE frame acceptance** → [2026-07-15 — O5 follow-ups: 68882 timing + FRESTORE frame acceptance](#2026-07-15--o5-follow-ups-68882-timing--frestore-frame-acceptance)
- **the 68LC040 integer core, WinUAE-differential** → [2026-07-18 — Q2+Q4: the 68LC040 integer core executes in Moira…](#2026-07-18--q2q4-the-68lc040-integer-core-executes-in-moira-winuae-differential-5-4005-400-no-fpu-f-line-included)
- **the 68040 MMU: 7 200/7 200 pinned** → [2026-07-18 — Q3: the 68040 MMU translates in Moira…](#2026-07-18--q3-the-68040-mmu-translates-in-moira--full-grid-7-2007-200-pinned-the-lc-475-cpu-side-is-complete)
- **RTE must honor a cleared SSW.DF (the vector-2 storm)** → [2026-07-15 — O6.9 resolved: GISTPERSO's vector-2 storm…](#2026-07-15--o69-resolved-gistpersos-vector-2-storm--rte-honors-a-cleared-sswdf)
- **bare no-FPU: _FP68K binds the integer PACK 4** → [2026-07-21 — Bare no-FPU solved: _FP68K binds the integer PACK 4 (Cuda XPRAM echo…](#2026-07-21--bare-no-fpu-solved-_fp68k-binds-the-integer-pack-4-cuda-xpram-echo-bug)
- **…and the UniversalInfo FPU masking that was deleted to get there** → [2026-07-21 — LLE step 5: UniversalInfo FPU masking deleted…](#2026-07-21--lle-step-5-universalinfo-fpu-masking-deleted-bare-no-fpu-fully-mapped)

### MCU firmware LLE — M68HC05, Cuda, Egret, PIC1654S, and ADB

- **the 11-cycle 6805 IRQ restored; second mouse button and right modifiers reach the GUI path** → [2026-08-03 — Event deadlines close the Cuda phase accommodation](#2026-08-03-event-deadlines)
- **the M68HC05E1 core: real Cuda firmware executes** → [2026-07-23 — M68HC05E1 core: the real Cuda firmware executes (step 10 groundwork)](#2026-07-23--m68hc05e1-core-the-real-cuda-firmware-executes-step-10-groundwork)
- **Mac OS 8.1 boots on the REAL Cuda firmware** → [2026-07-23 — Mac OS 8.1 boots to the Finder on the REAL Cuda firmware (blueprint…](#2026-07-23--mac-os-81-boots-to-the-finder-on-the-real-cuda-firmware-blueprint-step-3)
- **…and becomes the Quadra default** → [2026-07-23 — The real Cuda firmware is the Quadra's DEFAULT (blueprint step 4)](#2026-07-23--the-real-cuda-firmware-is-the-quadras-default-blueprint-step-4)
- **the LC II gets the real Egret firmware** → [2026-07-23 — The LC II runs the real Egret firmware too (same day, same glue)](#2026-07-23--the-lc-ii-runs-the-real-egret-firmware-too-same-day-same-glue)
- **ADB Talk R0 answers on PENDING data, not on changed bytes** → [2026-07-23 — ADB Talk R0 answers on PENDING data, not on changed bytes](#2026-07-23--adb-talk-r0-answers-on-pending-data-not-on-changed-bytes)
- **the Cuda/Egret wire model redone (per-reader hacks deleted)** → [2026-07-22 — LLE step 7: Cuda/Egret wire-model redo (the per-reader hacks are gone)](#2026-07-22--lle-step-7-cudaegret-wire-model-redo-the-per-reader-hacks-are-gone)
- **Mac II ADB over a real PIC1654S transceiver** → [2026-07-22 — Mac II ADB goes LLE: real PIC1654S transceiver (opt-in)](#2026-07-22--mac-ii-adb-goes-lle-real-pic1654s-transceiver-opt-in)
- **…and the three bugs that made the mouse move** → [2026-07-22 — Mac II LLE ADB default: mouse moves…](#2026-07-22--mac-ii-lle-adb-default-mouse-moves-three-bugs-none-where-predicted)
- **the missing DFAC2 I2C ACK (both factory Cudas land)** → [2026-07-29 — The Color Classic "0417 wedge" was a missing DFAC2, not a core bug…](#2026-07-29--the-color-classic-0417-wedge-was-a-missing-dfac2-not-a-core-bug-both-factory-cudas-land)
- **Egret mid-flight packet retraction manufactured ghost ADB sessions** → [2026-07-17 — SC2K "coprocesseur absent" ROOT-CAUSED AND FIXED…](#2026-07-17--sc2k-coprocesseur-absent-root-caused-and-fixed-egret-mid-flight-packet-retraction-manufactured-ghost-adb-sessions)
- **input-delivery gates for the 030 families; every HLE fallback is now LOUD** → [2026-07-29 — Input-delivery gates for the 030 families…](#2026-07-29--input-delivery-gates-for-the-030-families-loud-hle-fallbacks-and-a-retracted-bug)
- **why the ADB handler ID is a register (and why a standard keyboard must REFUSE handler 3)** → [2026-08-02 — The ADB device model and two SCC pins…](#2026-08-02-lle-devices)
- **where the second mouse button actually lives on this bus** → [2026-08-02 — The ADB device model and two SCC pins…](#2026-08-02-lle-devices)

### Save states

- **the archive core: one `visit<Ar>()` drives both save and load** → [2026-07-30 — Save states: the archive core + the whole LC II tree](#2026-07-30--save-states-the-archive-core--the-whole-lc-ii-tree)
- **restore determinism under the real Finder (LC II)** → [2026-07-30 — Save states survive the real Finder…](#2026-07-30--save-states-survive-the-real-finder-lcii_savestate_etalon)
- **…and on the 040 side** → [2026-07-30 — `q605_savestate_etalon`: real-OS restore determinism on the 040 side](#2026-07-30--q605_savestate_etalon-real-os-restore-determinism-on-the-040-side)
- **all 10 machine families serialize** → [2026-07-30 — Save-state fan-out: all 10 machine families serialize](#2026-07-30--save-state-fan-out-all-10-machine-families-serialize)
- **the GUI hook** → [2026-07-30 — Save states in the GUI: « Sauver / Restaurer l'état »](#2026-07-30-save-states-gui)

### Storage — SCSI, IWM/SWIM, media

- **53C96 pseudo-DMA reads** → [2026-07-18 — Q6.1: 53C96 pseudo-DMA reads work…](#2026-07-18--q61-53c96-pseudo-dma-reads-work--the-mac-os-81-scsi-driver-now-transfers-full-512-byte-blocks-off-the-disk)
- **the block-0 re-read loop was a Cuda ReadXPram framing divergence** → [2026-07-19 — Q6.2 RESOLVED: the block-0 re-read loop was a Cuda ReadXPram…](#2026-07-19--q62-resolved-the-block-0-re-read-loop-was-a-cuda-readxpram-reply-framing-divergence--the-boot-now-loads-the-driver-partition-map-and-system-progresses-to-a-new-scsi-blocker)
- **multi-block read needed the DATA IN bus-service interrupt** → [2026-07-19 — Q6.3 RESOLVED: SCSI multi-block read…](#2026-07-19--q63-resolved-scsi-multi-block-read--the-polled-10-transfer-info-needed-the-data-in-bus-service-interrupt)
- **a THIRD Cuda framing for the POST XPRAM validity read** → [2026-07-19 — Q6.5: the boot restart loop is ACTUALLY resolved…](#2026-07-19--q65-the-boot-restart-loop-is-actually-resolved--the-roms-post-xpram-validity-read-uses-a-third-cuda-framing-direct-driver-getpram)
- **the async SIM crash + the SCC/reselection spin** → [2026-07-19 — Q6.5b/c: the async SCSI SIM crash…](#2026-07-19--q65bc-the-async-scsi-sim-crash--the-sccreselection-spin-are-both-fixed--the-boot-loads-system-applies-patches-stops-at-dsbadpatch)
- **dsBadPatch(99) was a 53C96 FIFO-count lie** → [2026-07-19 — Q6.5d RESOLVED: dsBadPatch(99) was a 53C96 FIFO-count lie that sent…](#2026-07-19--q65d-resolved-dsbadpatch99-was-a-53c96-fifo-count-lie-that-sent-the-os-scsi-managers-resource-read-into-its-discard-engine)
- **the FPU trap and the DMA-final-chunk STATUS race (8.1 reaches the desktop)** → [2026-07-20 — Q6.6 RESOLVED: Mac OS 8.1 boots the Quadra 605 (68LC040) to the…](#2026-07-20--q66-resolved-mac-os-81-boots-the-quadra-605-68lc040-to-the-finder-desktop--two-blockers-the-fpu-trap-and-a-dma-final-chunk-status-race)
- **the polled WRITE path (7.5.5 / 7.6)** → [2026-07-21 — Q605 Sys 7.5.5 / 7.6 → Finder (53C96 polled WRITE)](#2026-07-21-q605-sys755-76-finder)
- **the TurboSCSI wait-state cell + scheduled 53C96 delays** → [2026-07-23 — LLE step 9 (partial): TurboSCSI wait-state cell…](#2026-07-23--lle-step-9-partial-turboscsi-wait-state-cell--53c96-scheduled-delays)
- **SWIM2 register/FIFO core** → [2026-07-20 — Q8.4: SWIM2 register/FIFO core replaces the zero stub](#2026-07-20--q84-swim2-registerfifo-core-replaces-the-zero-stub)
- **SWIM2 SuperDrive media (MFM 1.44 + GCR)** → [2026-07-20 — Q8.6: SWIM2 SuperDrive media (MFM 1.44 + GCR)](#2026-07-20--q86-swim2-superdrive-media-mfm-144--gcr)
- **SWIM2 real cell engines (MFM cell timing + CRC)** → [2026-07-23 — SWIM2: the real cell engines (MFM cell timing + CRC)](#2026-07-23--swim2-the-real-cell-engines-mfm-cell-timing--crc)
- **IWM write engine + GCR write-back** → [2026-07-23 — IWM write engine + GCR write-back: floppies are writable](#2026-07-23--iwm-write-engine--gcr-write-back-floppies-are-writable)
- **floppy writes reach the host image file** → [2026-07-24 — Floppy write persistence (gate `floppy_persist_test`)](#2026-07-24--floppy-write-persistence-gate-floppy_persist_test)
- **guest disk writes persist (SCSI)** → [2026-07-16 — SCSI write-back (persist guest disk writes)](#2026-07-16--scsi-write-back-persist-guest-disk-writes)
- **the flat-HFS façade, and `dir2hfs`** → [2026-07-20 — SCSI flat-HFS façade](#2026-07-20--scsi-flat-hfs-façade)
- **…the host-folder volume** → [2026-07-22 — dir2hfs: host folder → desktop volume (data-only flat-HFS façade)](#2026-07-22-dir2hfs)
- **CD-ROM: the target, then a disc mounting in the guest (and why 8.6 cannot boot)** → [2026-07-29 (evening) — A CD mounts in the guest; .cue/.bin; and why 8.6 cannot boot](#2026-07-29-evening--a-cd-mounts-in-the-guest-cuebin-and-why-86-cannot-boot)
- **…the SCSI CD-ROM target itself** → [2026-07-29 (later) — SCSI CD-ROM support, a guest-level floppy gate…](#2026-07-29-later--scsi-cd-rom-support-a-guest-level-floppy-gate-and-the-lle-inventory-re-synced)

### Video

- **DAFB stride/depth and 256-color host rendering** → [2026-07-20 — Q8.1: DAFB stride/depth model and 256-color host rendering](#2026-07-20--q81-dafb-stridedepth-model-and-256-color-host-rendering)
- **DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)** → [2026-07-21 — LLE step 6: DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)](#2026-07-21--lle-step-6-dafb-toward-mame-parity-swatch-crtc-gazelle-sense)
- **DAFB extracted to its own file** → [2026-07-21 — DAFB extracted into Dafb.h/.cpp (one concern per file)](#2026-07-21--dafb-extracted-into-dafbhcpp-one-concern-per-file)
- **there are THREE DAFB clock generators behind one window** → [2026-07-27 — Three DAFB clock generators, the pseudo-VIA's second flavour, two GUI…](#2026-07-27--three-dafb-clock-generators-the-pseudo-vias-second-flavour-two-gui-races)
- **the MEMCjr/DAFB bus-holding split that unblocked the Slot Manager** → [2026-07-18 — Q5.1d: Q5 Slot-Manager blocker RESOLVED…](#2026-07-18--q51d-q5-slot-manager-blocker-resolved--the-missing-memcjr-dafb-bus-holding-split-boot-now-drives-the-scsi-bus)
- **…the decl-ROM parse that was actually at fault** → [2026-07-18 — Q5.1a: the Slot-Manager blocker re-localized…](#2026-07-18--q51a-the-slot-manager-blocker-re-localized--the-dafb-sense-theory-is-disproven-the-fault-is-a-decl-rom-parse)
- **…fully anatomised** → [2026-07-18 — Q5.1c: the fatal `_sReadStruct` fully anatomised…](#2026-07-18--q51c-the-fatal-_sreadstruct-fully-anatomised-drhw-pick-proven-correct-full-machine-oracle-blocked-round-2)
- **Toby: CRTC-derived frame clock** → [2026-07-23 — Toby: CRTC-derived frame clock…](#2026-07-23--toby-crtc-derived-frame-clock--the-register-file-actually-writes)
- **why the raster beam owns no clock of its own** → [2026-08-02 (later) — The raster beam…](#2026-08-02-beam)
- **why a decoder sampling once per frame needs a frame COUNTER, not a position** → [2026-08-02 (later) — The raster beam…](#2026-08-02-beam)
- **the LC II black screen (texture alpha)** → [2026-07-16 — LC II GUI showed a black screen (texture alpha)](#2026-07-16--lc-ii-gui-showed-a-black-screen-texture-alpha)
- **selectable resolution and per-monitor depth** → [2026-07-16 — Selectable resolution (512×384 / 640×480)…](#2026-07-16-selectable-resolution)
- **LC II color at 8 bpp** → [2026-07-16 — LC II color (8 bpp by default) + peripheral-tick batching](#2026-07-16--lc-ii-color-8-bpp-by-default--peripheral-tick-batching)

### Sound

- **the ASC drain follows $807, and why that is free on every booting machine** → [2026-08-02 (third) — Two rates that were rounded…](#2026-08-02-eclock-asc)

- **the startup chime** → [2026-07-15 — M6: the startup chime plays](#2026-07-15--m6-the-startup-chime-plays)
- **IOSB ASC stereo** → [2026-07-20 — Q8.2: Quadra 605 PrimeTime/IOSB ASC stereo](#2026-07-20--q82-quadra-605-primetimeiosb-asc-stereo)
- **the Mac II classic-ASC idle IRQ** → [2026-07-20 — Classic ASC idle IRQ (Mac II)](#2026-07-20--classic-asc-idle-irq-mac-ii)
- **app sound: the pseudo-VIA ASC IRQ was edge-only** → [2026-07-17 — app sound reaches the ASC (pseudo-VIA ASC IRQ was edge-only)](#2026-07-17--app-sound-reaches-the-asc-pseudo-via-asc-irq-was-edge-only)
- **mechanical floppy + hard-disk drive sounds** → [2026-07-23 — Mechanical drive sounds (floppy + SCSI hard disk)](#2026-07-23--mechanical-drive-sounds-floppy--scsi-hard-disk)

### Serial, LocalTalk and AppleTalk

- **SCC receive path + the LToUDP virtual cable** → [2026-07-22 — LLAP milestone 1: SCC receive path + LToUDP virtual cable](#2026-07-22--llap-milestone-1-scc-receive-path--ltoudp-virtual-cable)
- **two Systems acquiring LLAP addresses across the cable** → [2026-07-22 — LLAP two-System etalon: real address acquisition between two Systems](#2026-07-22--llap-two-system-etalon-real-address-acquisition-between-two-systems)
- **the guest programs the wire pace (async baud)** → [2026-07-23 — SCC async-baud machinery: the guest programs the wire pace now](#2026-07-23--scc-async-baud-machinery-the-guest-programs-the-wire-pace-now)
- **a real transmitter on the wire** → [2026-07-23 — SCC Tx/Rx engine: the wire gets a real transmitter (Medium tier)](#2026-07-23--scc-txrx-engine-the-wire-gets-a-real-transmitter-medium-tier)
- **the virgin line reads clean; `POM68K_SCC_CLEANLINE` retired** → [2026-07-28 — LLE step 7: the virgin line reads clean…](#2026-07-28--lle-step-7-the-virgin-line-reads-clean-pom68k_scc_cleanline-retired)
- **real LLAP carrier sense — the LocalTalk watchdogs deleted** → [2026-07-21 — LLE step 3: real LLAP carrier sense…](#2026-07-21--lle-step-3-real-llap-carrier-sense--localtalk-watchdogs-deleted)
- **the whole AppleTalk stack moves in-process** → [2026-07-24 — AppleTalk moves in-process…](#2026-07-24--appletalk-moves-in-process-noderouter--appleshare--laserwriter--macip-one-gui-window)
- **the netatalk 2.4.9 + TashRouter bridge it replaced** → [2026-07-22 — AppleShare bridge vendored: netatalk 2.4.9 + TashRouter](#2026-07-22--appleshare-bridge-vendored-netatalk-249--tashrouter)
- **the Egret XPRAM protocol fix that makes AppleTalk genuinely inactive** → [2026-07-16 — O6.11 RESOLVED: GISTPERSO boots to the Finder…](#2026-07-16--o611-resolved-gistperso-boots-to-the-finder--egret-xpram-protocol-fix-makes-appletalk-genuinely-inactive)
- **LocalTalk LAP: SCC abort stream + HLE watchdog** → [2026-07-16 — O6.11: LocalTalk LAP — SCC abort stream + HLE watchdog](#2026-07-16--o611-localtalk-lap--scc-abort-stream--hle-watchdog)
- **SCC word fast path** → [2026-07-20 — O6.13: SCC word fast path + LC II NOFPU diagnosis](#2026-07-20--o613-scc-word-fast-path--lc-ii-nofpu-diagnosis)
- **why `/RTS` is not a view of WR5 bit 1, and where its release has to live** → [2026-08-02 — The ADB device model and two SCC pins…](#2026-08-02-lle-devices)
- **the SDLC residue code: why the idle RR1 and a frame byte disagreed** → [2026-08-02 — The ADB device model and two SCC pins…](#2026-08-02-lle-devices)

### LLE-vs-HLE migration (the numbered series)

- **step 1 — Mac II boots an UNMODIFIED ROM** → [2026-07-21 — LLE step 1: Mac II boots an UNMODIFIED ROM (RTC was mute, then…](#2026-07-21--lle-step-1-mac-ii-boots-an-unmodified-rom-rtc-was-mute-then-bit-shifted)
- **step 2 — per-tick SPConfig clamps removed** → [2026-07-21 — LLE step 2: per-tick SPConfig clamps removed (all three machines)](#2026-07-21--lle-step-2-per-tick-spconfig-clamps-removed-all-three-machines)
- **step 3 — real LLAP carrier sense** → [2026-07-21 — LLE step 3: real LLAP carrier sense…](#2026-07-21--lle-step-3-real-llap-carrier-sense--localtalk-watchdogs-deleted)
- **step 4 — Mac II EvQ soft-post deleted** → [2026-07-21 — LLE step 4: Mac II EvQ soft-post deleted…](#2026-07-21--lle-step-4-mac-ii-evq-soft-post-deleted--alerts-dismissed-over-real-adb)
- **step 5 — UniversalInfo FPU masking deleted** → [2026-07-21 — LLE step 5: UniversalInfo FPU masking deleted…](#2026-07-21--lle-step-5-universalinfo-fpu-masking-deleted-bare-no-fpu-fully-mapped)
- **step 6 — DAFB toward MAME parity** → [2026-07-21 — LLE step 6: DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)](#2026-07-21--lle-step-6-dafb-toward-mame-parity-swatch-crtc-gazelle-sense)
- **step 7 — Cuda/Egret wire-model redo** → [2026-07-22 — LLE step 7: Cuda/Egret wire-model redo (the per-reader hacks are gone)](#2026-07-22--lle-step-7-cudaegret-wire-model-redo-the-per-reader-hacks-are-gone)
- **step 7 — the virgin SCC line** → [2026-07-28 — LLE step 7: the virgin line reads clean…](#2026-07-28--lle-step-7-the-virgin-line-reads-clean-pom68k_scc_cleanline-retired)
- **step 9 — TurboSCSI wait states** → [2026-07-23 — LLE step 9 (partial): TurboSCSI wait-state cell…](#2026-07-23--lle-step-9-partial-turboscsi-wait-state-cell--53c96-scheduled-delays)
- **step 9 closed; the quick wins are exhausted** → [2026-07-23 — LLE audit: step 9 closed, the quick wins are exhausted](#2026-07-23--lle-audit-step-9-closed-the-quick-wins-are-exhausted)
- **inventory re-synced to the live tree** → [2026-07-22 — `docs/LLE_VS_HLE.md` third pass…](#2026-07-22--docslle_vs_hlemd-third-pass-inventory-re-synced-to-the-live-tree)
- **…and again, with the CD/floppy work** → [2026-07-29 (later) — SCSI CD-ROM support, a guest-level floppy gate…](#2026-07-29-later--scsi-cd-rom-support-a-guest-level-floppy-gate-and-the-lle-inventory-re-synced)

### Machine bring-ups, in the order they landed

- **Macintosh Plus — first real-ROM boot** → [2026-07-14 — M0–M3.5 + first real-ROM boot](#2026-07-14-m0-m35-first-rom-boot)
- **Plus — cycle-accurate boot hardware** → [2026-07-14 — M4 complete: cycle-accurate boot hardware](#2026-07-14--m4-complete-cycle-accurate-boot-hardware)
- **Plus — System 6.0.5 from floppy** → [2026-07-14 — M5: System 6.0.5 boots to the Finder from floppy](#2026-07-14--m5-system-605-boots-to-the-finder-from-floppy)
- **Plus — keyboard + mouse** → [2026-07-14 — M5.5: the Finder is drivable (keyboard + mouse)](#2026-07-14--m55-the-finder-is-drivable-keyboard--mouse)
- **Plus — SCSI hard disk** → [2026-07-15 — M7: System 6 boots from a SCSI hard disk](#2026-07-15--m7-system-6-boots-from-a-scsi-hard-disk)
- **Mac LC II — first six slices** → [2026-07-15 — O6 (LC II machine): first six slices](#2026-07-15--o6-lc-ii-machine-first-six-slices)
- **LC II — the blinking-? screen** → [2026-07-15 — O6: the LC II ROM boots to the blinking-? screen](#2026-07-15--o6-the-lc-ii-rom-boots-to-the-blinking--screen)
- **LC II — the Finder desktop** → [2026-07-15 — O6: **Mac LC II boots to the Finder desktop**](#2026-07-15--o6-mac-lc-ii-boots-to-the-finder-desktop)
- **Macintosh II — System 6** → [2026-07-20 — Mac II boots System 6 to the Finder](#2026-07-20--mac-ii-boots-system-6-to-the-finder)
- **Mac II — System 7** → [2026-07-20 — Mac II Sys7 → Finder (AppleTalk alert dismiss)](#2026-07-20-macii-sys7-finder)
- **Quadra 605 — Mac OS 8.1 desktop** → [2026-07-20 — Q6.6 RESOLVED: Mac OS 8.1 boots the Quadra 605 (68LC040) to the…](#2026-07-20--q66-resolved-mac-os-81-boots-the-quadra-605-68lc040-to-the-finder-desktop--two-blockers-the-fpu-trap-and-a-dma-final-chunk-status-race)
- **Quadra 605 — GUI profile, audio, ROM discovery** → [2026-07-20 — Q7: Quadra 605 GUI profile, audio and ROM discovery](#2026-07-20--q7-quadra-605-gui-profile-audio-and-rom-discovery)
- **Macintosh LC (68020) and Classic II (Eagle)** → [2026-07-24 — Phase C: Macintosh LC (68020) and Classic II (Eagle) boot to the Finder](#2026-07-24--phase-c-macintosh-lc-68020-and-classic-ii-eagle-boot-to-the-finder)
- **Color Classic (Spice + Cuda) and LC III (Sonora + Egret)** → [2026-07-24 — Phase C: Color Classic (Spice…](#2026-07-24--phase-c-color-classic-spice--cuda-lle-and-lc-iii-sonora--egret-lle)
- **LC 475 (68LC040) and LC III+ (33 MHz Sonora)** → [2026-07-24 — Phase C: LC 475 (68LC040 + Cuda LLE) and LC III+ (33 MHz Sonora +…](#2026-07-24--phase-c-lc-475-68lc040--cuda-lle-and-lc-iii-33-mhz-sonora--egret-lle)
- **LC 520 — the EDE66CBD all-in-one family, cracked from the ROM** → [2026-07-24 — Phase C: Macintosh LC 520 — the EDE66CBD all-in-one family boots…](#2026-07-24--phase-c-macintosh-lc-520--the-ede66cbd-all-in-one-family-boots-cuda-341s0060-lle)
- **LC 550 and Color Classic II** → [2026-07-24 — Phase C: LC 550 and Color Classic II…](#2026-07-24--phase-c-lc-550-and-color-classic-ii--the-aio-family-fans-out)
- **Mac IIvx / IIvi (VASP) — *inside* the floppy-persistence entry** → [2026-07-24 — Floppy write persistence (gate `floppy_persist_test`)](#2026-07-24--floppy-write-persistence-gate-floppy_persist_test)
- **Centris 650 / 610 (djMEMC + IOSB)** → [2026-07-24 — Phase C: Mac Centris 650 + Centris 610 (djMEMC + IOSB, PIC1654S LLE)](#2026-07-24--phase-c-mac-centris-650--centris-610-djmemc--iosb-pic1654s-lle)
- **Quadra 650 / 610** → [2026-07-24 — Phase C: Quadra 650 + Quadra 610 (full 68040 on the djMEMC+IOSB machine)](#2026-07-24--phase-c-quadra-650--quadra-610-full-68040-on-the-djmemciosb-machine)
- **Mac TV, IIsi, IIci, IIx, IIcx** → [2026-07-25 — Five more machines: Mac TV, IIsi, IIci, IIx, IIcx](#2026-07-25--five-more-machines-mac-tv-iisi-iici-iix-iicx)
- **Quadra 800** → [2026-07-25 — Quadra 800 (26th machine), the 040 boost ceiling lifted…](#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted)
- **Macintosh SE, SE FDHD, Classic (one enum, not a machine)** → [2026-07-25 — Macintosh SE, SE FDHD and Classic…](#2026-07-25--macintosh-se-se-fdhd-and-classic-three-machines-for-one-enum)
- **Quadra 630 / LC 580 (F108 + Valkyrie)** → [2026-07-25 — Macintosh Quadra 630 / LC 580…](#2026-07-25--macintosh-quadra-630--lc-580-f108--valkyrie-the-last-68k-desktop-board)
- **Quadra 700 (the first discrete-040 board)** → [2026-07-25 — Macintosh Quadra 700: the 27th machine…](#2026-07-25--macintosh-quadra-700-the-27th-machine-and-the-dafb-turboscsi-cell)

### Audits, doc syncs and cross-cutting reviews

- **adversarial subsystem audit — 3 fixes** → [2026-07-17 — adversarial subsystem audit: 3 correctness fixes](#2026-07-17--adversarial-subsystem-audit-3-correctness-fixes)
- **adversarial subsystem audit #2 — 9 fixes** → [2026-07-17 — adversarial subsystem audit #2: 9 correctness fixes](#2026-07-17--adversarial-subsystem-audit-2-9-correctness-fixes)
- **8-angle bug hunt + UI work** → [2026-07-16 — review fixes (8-angle bug hunt) + UI…](#2026-07-16--review-fixes-8-angle-bug-hunt--ui-mouse-capture-drag-fix-machine-menu)
- **Basilisk II knowledge applied (rominfo, XPRAM defaults)** → [2026-07-15 — Basilisk II knowledge applied: rominfo, XPRAM defaults](#2026-07-15--basilisk-ii-knowledge-applied-rominfo-xpram-defaults)
- **the Finder boot matrix, Phase A** → [2026-07-21 — Finder matrix Phase A complete (all four machines)](#2026-07-21--finder-matrix-phase-a-complete-all-four-machines)
- **status pass: what is actually left** → [2026-07-25 — Doc sync + status pass: 25 machines, 90 gates, what is actually left](#2026-07-25--doc-sync--status-pass-25-machines-90-gates-what-is-actually-left)

---

## Index by date

Newest first.

- **2026-08-05** — [Beyond-boot reaches a second machine: Quadra 605 soak + persist, and the 53C96 finally takes a real guest WRITE](#2026-08-05-q605-beyond)
- **2026-08-05** — [IWM/SWIM bughunt: the Q700 spindle ran 1.6x fast, and the IWM personality was half-speed-blind on C15M hosts](#2026-08-05-iwm-swim-bughunt)
- **2026-08-05** — [The m040 sweep is paid and the cache chantier closes at M1](#2026-08-05-cache040-closed)
- **2026-08-05** — [The M1 bughunt: three real defects the gates were green over](#2026-08-05-cache040-bughunt)
- **2026-08-05** — [Cache 040 M1: CINV and CPUSH finally act on real state, and the tags cost nothing](#2026-08-05-cache040-m1)
- **2026-08-04** — [The IIfx SCSI mirror mounted one volume seven times; CDs hot-mount under 8.1; the MacIP window opens up](#2026-08-04-iifx-mirror-cd-hot)
- **2026-08-04** — [Event deadlines reach six more platforms: min(MCU bound, historical batch)](#2026-08-04-deadlines-six)
- **2026-08-04** — [Hot floppy swap reaches every runner; release CI for four OS targets; the x64 dynamic-link regression found and fixed](#2026-08-04-floppy-ci)
- **2026-08-04** — [AArch64 Finder gate green and fast: hidden-state lockstep plus two host-side bottlenecks removed](#2026-08-04-a64-green-fast)
- **2026-08-03** — [Event deadlines close the Cuda phase accommodation; extended ADB input reaches every GUI runner](#2026-08-03-event-deadlines)
- **2026-08-03** — [Three items closed by measurement — and a GREEN ctest that proved nothing](#2026-08-03-three-items)
- **2026-08-02** — [The "Quadra modifier bug" retracted: same machine, other image, works](#2026-08-02-cmdn-retracted)
- **2026-08-02** — [Two LLE gaps closed: the Cuda's I2C bus gets a second slave, and SWIM1 gets its DMA request line](#2026-08-02-i2c-dat1byte)
- **2026-08-02** — [Both Quadra towers boot the Finder: the IOP's BRK was a 68k word READ, not a 65C02 bug](#2026-08-02-q900-finder)
- **2026-08-02** — [Two rates that were rounded, and one that was only ever a price](#2026-08-02-eclock-asc)
- **2026-08-02** — [The raster beam: nine video decoders stop painting the whole frame at once](#2026-08-02-beam)
- **2026-08-02** — [The ADB device model and two SCC pins: closing the LLE gaps that needed no new hardware](#2026-08-02-lle-devices)
- **2026-08-01** — [Quadra 900: the Eclipse platform lands and both IOPs run — the wall is a BRK inside byte-perfect firmware](#2026-08-01-q900)
- **2026-08-01** — [The IIfx is the 34th profile: GUI, save states, and an input gate whose thresholds were measured, not invented](#2026-08-01-iifx-profile)
- **2026-08-01** — [The Mac IIfx boots the Finder — ADB bit-banged by the IOP's own 65C02 firmware against AdbLine](#2026-08-01-iifx-finder)
- **2026-08-01** — [IOP M3: the IIfx POSTs — both IOP firmwares upload byte-perfect, and the boot scan reads the disk](#2026-08-01-iifx-post)
- **2026-08-01** — [IOP M2: the Apple PIC device lands — a window-uploaded 65C02 program talks both mailbox directions](#2026-08-01-applepic)
- **2026-08-01** — [The IIfx/Quadra-900 IOP brick opens: the R65C02 core lands, and it needed no dump](#2026-08-01-r65c02)
- **2026-07-31** — [`duo230_boot_etalon` GREEN: milestone 3 gated, the GSC decoder lands](#2026-07-31-duo-gate)
- **2026-07-31** — [The SE/30 lands as the 33rd profile: wiring only, Finder on the first run](#2026-07-31-se30)
- **2026-07-31** — [The Duo 230 boots the Finder: /PMU_INT is a LEVEL, and $E1 re-uploads the PMU firmware](#2026-07-31-duo-finder)
- **2026-07-31** — [The ten-month red gate was Slow Keys: the GUEST was rejecting the keys](#2026-07-31-slow-keys)
- **2026-07-31** — [Two negative results, recorded on purpose](#2026-07-31-two-negative-results)
- **2026-07-31** — [The window-churn investigation ends on one deleted line: −23 to −33 %](#2026-07-31-window-churn-dtlb-flush)
- **2026-07-30** — [The five opcodes, same day: MOVEM + DBcc + JMP compiled](#2026-07-30--the-five-opcodes-same-day-movem--dbcc--jmp-compiled)
- **2026-07-30** — [JIT measured honestly: x64 wins both regimes; the next lever is 5 opcodes](#2026-07-30--jit-measured-honestly-x64-wins-both-regimes-the-next-lever-is-5-opcodes)
- **2026-07-30** — [`q605_savestate_etalon`: real-OS restore determinism on the 040 side](#2026-07-30--q605_savestate_etalon-real-os-restore-determinism-on-the-040-side)
- **2026-07-30** — [Engine re-baseline (idle host) + the CPU menu reaches the 030s](#2026-07-30--engine-re-baseline-idle-host--the-cpu-menu-reaches-the-030s)
- **2026-07-30** — [Save states in the GUI: « Sauver / Restaurer l'état »](#2026-07-30-save-states-gui)
- **2026-07-30** — [Save-state fan-out: all 10 machine families serialize](#2026-07-30--save-state-fan-out-all-10-machine-families-serialize)
- **2026-07-30** — [Save states survive the real Finder: `lcii_savestate_etalon`](#2026-07-30--save-states-survive-the-real-finder-lcii_savestate_etalon)
- **2026-07-30** — [A JIT backend is valid per GUEST family, not just per host](#2026-07-30--a-jit-backend-is-valid-per-guest-family-not-just-per-host)
- **2026-07-30** — [Save states: the archive core + the whole LC II tree](#2026-07-30--save-states-the-archive-core--the-whole-lc-ii-tree)
- **2026-07-29 (late)** — [PGO across all four CPU families (−26 % on the LC II); the dispatch-table item measured and dropped](#2026-07-29-pgo-four-cpu-families)
- **2026-07-29 (evening)** — [A CD mounts in the guest; .cue/.bin; and why 8.6 cannot boot](#2026-07-29-evening--a-cd-mounts-in-the-guest-cuebin-and-why-86-cannot-boot)
- **2026-07-29 (later)** — [SCSI CD-ROM support, a guest-level floppy gate, and the LLE inventory re-synced](#2026-07-29-later--scsi-cd-rom-support-a-guest-level-floppy-gate-and-the-lle-inventory-re-synced)
- **2026-07-29** — [Input-delivery gates for the 030 families; loud HLE fallbacks (and a retracted "bug")](#2026-07-29--input-delivery-gates-for-the-030-families-loud-hle-fallbacks-and-a-retracted-bug)
- **2026-07-29** — [The Color Classic "0417 wedge" was a missing DFAC2, not a core bug; both factory Cudas land](#2026-07-29--the-color-classic-0417-wedge-was-a-missing-dfac2-not-a-core-bug-both-factory-cudas-land)
- **2026-07-28** — [LLE step 7: the virgin line reads clean; `POM68K_SCC_CLEANLINE` retired](#2026-07-28--lle-step-7-the-virgin-line-reads-clean-pom68k_scc_cleanline-retired)
- **2026-07-28 (eighth pass)** — [O(1) probes, per-space eviction, and the 020 seam](#2026-07-28-eighth-pass--o1-probes-per-space-eviction-and-the-020-seam)
- **2026-07-28 (seventh pass)** — [All four 030 families under the engine](#2026-07-28-seventh-pass--all-four-030-families-under-the-engine)
- **2026-07-28 (sixth pass)** — [The JIT reaches the 68030: the V8 family](#2026-07-28-sixth-pass--the-jit-reaches-the-68030-the-v8-family)
- **2026-07-28 (fifth pass)** — [The "PGO divergence" was the U bit all along](#2026-07-28-fifth-pass--the-pgo-divergence-was-the-u-bit-all-along)
- **2026-07-28 (fourth pass)** — [The data window and PGO: the interpreter's turn](#2026-07-28-fourth-pass--the-data-window-and-pgo-the-interpreters-turn)
- **2026-07-28 (third pass)** — [The density work, and what it finally measured](#2026-07-28-third-pass--the-density-work-and-what-it-finally-measured)
- **2026-07-28 (later)** — [Block linking, and LINK/UNLK/NOP out of the exclusion list](#2026-07-28-later--block-linking-and-linkunlknop-out-of-the-exclusion-list)
- **2026-07-28** — [The x86-64 code generator (J2), and what it measured](#2026-07-28--the-x86-64-code-generator-j2-and-what-it-measured)
- **2026-07-27** — [A second execution engine: the multi-target JIT (J0 + J1)](#2026-07-27--a-second-execution-engine-the-multi-target-jit-j0--j1)
- **2026-07-27** — [The Macintosh TV boots again: a 2 % MCU shift is a deadlock](#2026-07-27--the-macintosh-tv-boots-again-a-2--mcu-shift-is-a-deadlock)
- **2026-07-27** — [Three DAFB clock generators, the pseudo-VIA's second flavour, two GUI races](#2026-07-27--three-dafb-clock-generators-the-pseudo-vias-second-flavour-two-gui-races)
- **2026-07-25** — [Macintosh Quadra 700: the 27th machine, and the DAFB TurboSCSI cell](#2026-07-25--macintosh-quadra-700-the-27th-machine-and-the-dafb-turboscsi-cell)
- **2026-07-25** — [Macintosh Quadra 630 / LC 580: F108 + Valkyrie, the last 68k desktop board](#2026-07-25--macintosh-quadra-630--lc-580-f108--valkyrie-the-last-68k-desktop-board)
- **2026-07-25** — [Macintosh SE, SE FDHD and Classic: three machines for one enum](#2026-07-25--macintosh-se-se-fdhd-and-classic-three-machines-for-one-enum)
- **2026-07-25** — [Quadra 800 (26th machine), the 040 boost ceiling lifted, and the PIC co-step un-boosted](#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted)
- **2026-07-25** — [The i-cache boost was accelerating the VIA bus: LC III / LC III+ / IIvx fixed, and the IIsi's boost restored](#2026-07-25--the-i-cache-boost-was-accelerating-the-via-bus-lc-iii--lc-iii--iivx-fixed-and-the-iisis-boost-restored)
- **2026-07-25** — [Doc sync + status pass: 25 machines, 90 gates, what is actually left](#2026-07-25--doc-sync--status-pass-25-machines-90-gates-what-is-actually-left)
- **2026-07-25** — [Five more machines: Mac TV, IIsi, IIci, IIx, IIcx](#2026-07-25--five-more-machines-mac-tv-iisi-iici-iix-iicx)
- **2026-07-24** — [Phase C: Quadra 650 + Quadra 610 (full 68040 on the djMEMC+IOSB machine)](#2026-07-24--phase-c-quadra-650--quadra-610-full-68040-on-the-djmemciosb-machine)
- **2026-07-24** — [Phase C: Mac Centris 650 + Centris 610 (djMEMC + IOSB, PIC1654S LLE)](#2026-07-24--phase-c-mac-centris-650--centris-610-djmemc--iosb-pic1654s-lle)
- **2026-07-24** — [AppleTalk moves in-process: node/router + AppleShare + LaserWriter + MacIP, one GUI window](#2026-07-24--appletalk-moves-in-process-noderouter--appleshare--laserwriter--macip-one-gui-window)
- **2026-07-24** — [Beyond-boot gates on the LC II + a clock-drift bug they caught](#2026-07-24--beyond-boot-gates-on-the-lc-ii--a-clock-drift-bug-they-caught)
- **2026-07-24** — [Floppy write persistence (gate `floppy_persist_test`)](#2026-07-24--floppy-write-persistence-gate-floppy_persist_test)
- **2026-07-24** — [Phase C: LC 550 and Color Classic II — the AIO family fans out](#2026-07-24--phase-c-lc-550-and-color-classic-ii--the-aio-family-fans-out)
- **2026-07-24** — [Phase C: Macintosh LC 520 — the EDE66CBD all-in-one family boots (Cuda 341S0060 LLE)](#2026-07-24--phase-c-macintosh-lc-520--the-ede66cbd-all-in-one-family-boots-cuda-341s0060-lle)
- **2026-07-24** — [Phase C: LC 475 (68LC040 + Cuda LLE) and LC III+ (33 MHz Sonora + Egret LLE)](#2026-07-24--phase-c-lc-475-68lc040--cuda-lle-and-lc-iii-33-mhz-sonora--egret-lle)
- **2026-07-24** — [Phase C: Color Classic (Spice + Cuda LLE) and LC III (Sonora + Egret LLE)](#2026-07-24--phase-c-color-classic-spice--cuda-lle-and-lc-iii-sonora--egret-lle)
- **2026-07-24** — [Phase C: Macintosh LC (68020) and Classic II (Eagle) boot to the Finder](#2026-07-24--phase-c-macintosh-lc-68020-and-classic-ii-eagle-boot-to-the-finder)
- **2026-07-24** — [Event-driven ADB wire: the Egret firmware LLE is the LC II DEFAULT](#2026-07-24--event-driven-adb-wire-the-egret-firmware-lle-is-the-lc-ii-default)
- **2026-07-23** — [ADB Talk R0 answers on PENDING data, not on changed bytes](#2026-07-23--adb-talk-r0-answers-on-pending-data-not-on-changed-bytes)
- **2026-07-23** — [LC II: Egret firmware LLE back to OPT-IN (mouse starvation)](#2026-07-23--lc-ii-egret-firmware-lle-back-to-opt-in-mouse-starvation)
- **2026-07-23** — [IWM write engine + GCR write-back: floppies are writable](#2026-07-23--iwm-write-engine--gcr-write-back-floppies-are-writable)
- **2026-07-23** — [Mechanical drive sounds (floppy + SCSI hard disk)](#2026-07-23--mechanical-drive-sounds-floppy--scsi-hard-disk)
- **2026-07-23** — [SWIM2: the real cell engines (MFM cell timing + CRC)](#2026-07-23--swim2-the-real-cell-engines-mfm-cell-timing--crc)
- **2026-07-23** — [LLE audit: step 9 closed, the quick wins are exhausted](#2026-07-23--lle-audit-step-9-closed-the-quick-wins-are-exhausted)
- **2026-07-23** — [SCC Tx/Rx engine: the wire gets a real transmitter (Medium tier)](#2026-07-23--scc-txrx-engine-the-wire-gets-a-real-transmitter-medium-tier)
- **2026-07-23** — [The LC II runs the real Egret firmware too (same day, same glue)](#2026-07-23--the-lc-ii-runs-the-real-egret-firmware-too-same-day-same-glue)
- **2026-07-23** — [The real Cuda firmware is the Quadra's DEFAULT (blueprint step 4)](#2026-07-23--the-real-cuda-firmware-is-the-quadras-default-blueprint-step-4)
- **2026-07-23** — [Mac OS 8.1 boots to the Finder on the REAL Cuda firmware (blueprint step 3)](#2026-07-23--mac-os-81-boots-to-the-finder-on-the-real-cuda-firmware-blueprint-step-3)
- **2026-07-23** — [M68HC05E1 core: the real Cuda firmware executes (step 10 groundwork)](#2026-07-23--m68hc05e1-core-the-real-cuda-firmware-executes-step-10-groundwork)
- **2026-07-23** — [SCC async-baud machinery: the guest programs the wire pace now](#2026-07-23--scc-async-baud-machinery-the-guest-programs-the-wire-pace-now)
- **2026-07-23** — [Toby: CRTC-derived frame clock + the register file actually writes](#2026-07-23--toby-crtc-derived-frame-clock--the-register-file-actually-writes)
- **2026-07-23** — [LLE step 9 (partial): TurboSCSI wait-state cell + 53C96 scheduled delays](#2026-07-23--lle-step-9-partial-turboscsi-wait-state-cell--53c96-scheduled-delays)
- **2026-07-22** — [`docs/LLE_VS_HLE.md` third pass: inventory re-synced to the live tree](#2026-07-22--docslle_vs_hlemd-third-pass-inventory-re-synced-to-the-live-tree)
- **2026-07-22** — [LLE step 7: Cuda/Egret wire-model redo (the per-reader hacks are gone)](#2026-07-22--lle-step-7-cudaegret-wire-model-redo-the-per-reader-hacks-are-gone)
- **2026-07-22** — [AppleShare bridge vendored: netatalk 2.4.9 + TashRouter](#2026-07-22--appleshare-bridge-vendored-netatalk-249--tashrouter)
- **2026-07-22** — [LLAP two-System etalon: real address acquisition between two Systems](#2026-07-22--llap-two-system-etalon-real-address-acquisition-between-two-systems)
- **2026-07-22** — [LLAP milestone 1: SCC receive path + LToUDP virtual cable](#2026-07-22--llap-milestone-1-scc-receive-path--ltoudp-virtual-cable)
- **2026-07-22** — [dir2hfs: host folder → desktop volume (data-only flat-HFS façade)](#2026-07-22-dir2hfs)
- **2026-07-22** — [Mac II LLE ADB default: mouse moves; three bugs, none where predicted](#2026-07-22--mac-ii-lle-adb-default-mouse-moves-three-bugs-none-where-predicted)
- **2026-07-22** — [Mac II ADB goes LLE: real PIC1654S transceiver (opt-in)](#2026-07-22--mac-ii-adb-goes-lle-real-pic1654s-transceiver-opt-in)
- **2026-07-21** — [Bare no-FPU solved: _FP68K binds the integer PACK 4 (Cuda XPRAM echo bug)](#2026-07-21--bare-no-fpu-solved-_fp68k-binds-the-integer-pack-4-cuda-xpram-echo-bug)
- **2026-07-21** — [DAFB extracted into Dafb.h/.cpp (one concern per file)](#2026-07-21--dafb-extracted-into-dafbhcpp-one-concern-per-file)
- **2026-07-21** — [LLE step 6: DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)](#2026-07-21--lle-step-6-dafb-toward-mame-parity-swatch-crtc-gazelle-sense)
- **2026-07-21** — [LLE step 5: UniversalInfo FPU masking deleted; bare no-FPU fully mapped](#2026-07-21--lle-step-5-universalinfo-fpu-masking-deleted-bare-no-fpu-fully-mapped)
- **2026-07-21** — [LLE step 4: Mac II EvQ soft-post deleted — alerts dismissed over real ADB](#2026-07-21--lle-step-4-mac-ii-evq-soft-post-deleted--alerts-dismissed-over-real-adb)
- **2026-07-21** — [LLE step 3: real LLAP carrier sense — LocalTalk watchdogs deleted](#2026-07-21--lle-step-3-real-llap-carrier-sense--localtalk-watchdogs-deleted)
- **2026-07-21** — [LLE step 2: per-tick SPConfig clamps removed (all three machines)](#2026-07-21--lle-step-2-per-tick-spconfig-clamps-removed-all-three-machines)
- **2026-07-21** — [LLE step 1: Mac II boots an UNMODIFIED ROM (RTC was mute, then bit-shifted)](#2026-07-21--lle-step-1-mac-ii-boots-an-unmodified-rom-rtc-was-mute-then-bit-shifted)
- **2026-07-21** — [Plus keyboard regression (6522 SR auto-shift) + nofpu gate floor](#2026-07-21--plus-keyboard-regression-6522-sr-auto-shift--nofpu-gate-floor)
- **2026-07-21** — [Finder matrix Phase A complete (all four machines)](#2026-07-21--finder-matrix-phase-a-complete-all-four-machines)
- **2026-07-21** — [Q605 Sys 7.5.5 / 7.6 → Finder (53C96 polled WRITE)](#2026-07-21-q605-sys755-76-finder)
- **2026-07-21** — [Q605 Sys 7.5 / GISTPERSO Finder at 1bpp; 7.5.5/7.6 hang](#2026-07-21--q605-sys-75--gistperso-finder-at-1bpp-75576-hang)
- **2026-07-20** — [LC II Sys 7.1 / 7.5.5 → Finder (SPConfig clamp)](#2026-07-20-lcii-sys71-755-finder)
- **2026-07-20** — [Mac II Sys7 → Finder (AppleTalk alert dismiss)](#2026-07-20-macii-sys7-finder)
- **2026-07-20** — [Classic ASC idle IRQ (Mac II)](#2026-07-20--classic-asc-idle-irq-mac-ii)
- **2026-07-20** — [SCSI flat-HFS façade](#2026-07-20--scsi-flat-hfs-façade)
- **2026-07-20** — [Mac II boots System 6 to the Finder](#2026-07-20--mac-ii-boots-system-6-to-the-finder)
- **2026-07-20** — [Mac II: overlay is a one-way latch](#2026-07-20--mac-ii-overlay-is-a-one-way-latch)
- **2026-07-20** — [Mac II: PDMA $50F060xx must decode A0..A1](#2026-07-20--mac-ii-pdma-50f060xx-must-decode-a0a1)
- **2026-07-20** — [Mac II: prefer SCSI boot over empty floppy](#2026-07-20--mac-ii-prefer-scsi-boot-over-empty-floppy)
- **2026-07-20** — [Mac II: StartBoot wantType=$FF was skipping Apple_HFS](#2026-07-20--mac-ii-startboot-wanttypeff-was-skipping-apple_hfs)
- **2026-07-20** — [O6.13: SCC word fast path + LC II NOFPU diagnosis](#2026-07-20--o613-scc-word-fast-path--lc-ii-nofpu-diagnosis)
- **2026-07-20** — [Q8.8: CACHE_BOOST calibration (default stays 1)](#2026-07-20--q88-cache_boost-calibration-default-stays-1)
- **2026-07-20** — [Q8.6: SWIM2 SuperDrive media (MFM 1.44 + GCR)](#2026-07-20--q86-swim2-superdrive-media-mfm-144--gcr)
- **2026-07-20** — [Q8.7: 040 I/D ATC + Cpu040 throughput overlay](#2026-07-20--q87-040-id-atc--cpu040-throughput-overlay)
- **2026-07-20** — [Q8.5: 68LC040 NOFPU path (soft FPU; bare NONE = dsNoFPU 90)](#2026-07-20--q85-68lc040-nofpu-path-soft-fpu-bare-none--dsnofpu-90)
- **2026-07-20** — [Q8.4: SWIM2 register/FIFO core replaces the zero stub](#2026-07-20--q84-swim2-registerfifo-core-replaces-the-zero-stub)
- **2026-07-20** — [Q8.3: Quadra 605 whole-machine boot gate](#2026-07-20--q83-quadra-605-whole-machine-boot-gate)
- **2026-07-20** — [Q8.2: Quadra 605 PrimeTime/IOSB ASC stereo](#2026-07-20--q82-quadra-605-primetimeiosb-asc-stereo)
- **2026-07-20** — [Q8.1: DAFB stride/depth model and 256-color host rendering](#2026-07-20--q81-dafb-stridedepth-model-and-256-color-host-rendering)
- **2026-07-20** — [Q7: Quadra 605 GUI profile, audio and ROM discovery](#2026-07-20--q7-quadra-605-gui-profile-audio-and-rom-discovery)
- **2026-07-20** — [Q6.6 RESOLVED: Mac OS 8.1 boots the Quadra 605 (68LC040) to the Finder desktop — two blockers, the FPU trap and a DMA-final-chunk STATUS race](#2026-07-20--q66-resolved-mac-os-81-boots-the-quadra-605-68lc040-to-the-finder-desktop--two-blockers-the-fpu-trap-and-a-dma-final-chunk-status-race)
- **2026-07-19** — [Q6.5d RESOLVED: dsBadPatch(99) was a 53C96 FIFO-count lie that sent the OS SCSI Manager's resource read into its DISCARD engine](#2026-07-19--q65d-resolved-dsbadpatch99-was-a-53c96-fifo-count-lie-that-sent-the-os-scsi-managers-resource-read-into-its-discard-engine)
- **2026-07-19** — [Q6.5b/c: the async SCSI SIM crash + the SCC/reselection spin are BOTH fixed — the boot loads System, applies patches, stops at dsBadPatch](#2026-07-19--q65bc-the-async-scsi-sim-crash--the-sccreselection-spin-are-both-fixed--the-boot-loads-system-applies-patches-stops-at-dsbadpatch)
- **2026-07-19** — [Q6.5: the boot restart loop is ACTUALLY resolved — the ROM's POST XPRAM validity read uses a THIRD Cuda framing (direct-driver GetPram)](#2026-07-19--q65-the-boot-restart-loop-is-actually-resolved--the-roms-post-xpram-validity-read-uses-a-third-cuda-framing-direct-driver-getpram)
- **2026-07-19** — [Q6.4 + Q6.2 BOTH RESOLVED: the boot restart loop AND the block-0 loop were one coupled Cuda-reply-framing bug; the System now loads](#2026-07-19--q64--q62-both-resolved-the-boot-restart-loop-and-the-block-0-loop-were-one-coupled-cuda-reply-framing-bug-the-system-now-loads)
- **2026-07-19** — [[superseded within the day] Q6.4 root-caused, un-masking Q6.2](#2026-07-19--superseded-within-the-day-q64-root-caused-un-masking-q62)
- **2026-07-19** — [Q6.4 re-localized: it is a System-launch HANDOFF failure, NOT a Cuda reply-framing bug (the prior "completion ISR buffer-smash" lead is disproven; no fix landed yet)](#2026-07-19--q64-re-localized-it-is-a-system-launch-handoff-failure-not-a-cuda-reply-framing-bug-the-prior-completion-isr-buffer-smash-lead-is-disproven-no-fix-landed-yet)
- **2026-07-19** — [Q6.4 deeply localized: the console divert is a periodic boot-RESTART loop, not a fault — several candidates ruled out (no fix yet)](#2026-07-19--q64-deeply-localized-the-console-divert-is-a-periodic-boot-restart-loop-not-a-fault--several-candidates-ruled-out-no-fix-yet)
- **2026-07-19** — [Q6.3 RESOLVED: SCSI multi-block read — the polled ($10) Transfer Info needed the DATA IN bus-service interrupt](#2026-07-19--q63-resolved-scsi-multi-block-read--the-polled-10-transfer-info-needed-the-data-in-bus-service-interrupt)
- **2026-07-19** — [Q6.2 RESOLVED: the block-0 re-read loop was a Cuda ReadXPram reply-framing divergence — the boot now loads the driver, partition map and System (progresses to a new SCSI blocker)](#2026-07-19--q62-resolved-the-block-0-re-read-loop-was-a-cuda-readxpram-reply-framing-divergence--the-boot-now-loads-the-driver-partition-map-and-system-progresses-to-a-new-scsi-blocker)
- **2026-07-18** — [Q6.1: 53C96 pseudo-DMA reads work — the Mac OS 8.1 SCSI driver now transfers full 512-byte blocks off the disk](#2026-07-18--q61-53c96-pseudo-dma-reads-work--the-mac-os-81-scsi-driver-now-transfers-full-512-byte-blocks-off-the-disk)
- **2026-07-18** — [Q5.1d: Q5 Slot-Manager blocker RESOLVED — the missing MEMCjr DAFB bus-holding split; boot now drives the SCSI bus](#2026-07-18--q51d-q5-slot-manager-blocker-resolved--the-missing-memcjr-dafb-bus-holding-split-boot-now-drives-the-scsi-bus)
- **2026-07-18** — [Q5.1c: the fatal `_sReadStruct` fully anatomised; DrHW pick proven correct; full-machine oracle blocked (round 2)](#2026-07-18--q51c-the-fatal-_sreadstruct-fully-anatomised-drhw-pick-proven-correct-full-machine-oracle-blocked-round-2)
- **2026-07-18** — [Q6: NCR 53C96 wired into the Quadra 605 + the sReadWord producer chain pinned (boot-integration round 1)](#2026-07-18--q6-ncr-53c96-wired-into-the-quadra-605--the-sreadword-producer-chain-pinned-boot-integration-round-1)
- **2026-07-18** — [Q5.1a: the Slot-Manager blocker re-localized — the DAFB-sense theory is disproven, the fault is a decl-ROM parse](#2026-07-18--q51a-the-slot-manager-blocker-re-localized--the-dafb-sense-theory-is-disproven-the-fault-is-a-decl-rom-parse)
- **2026-07-18** — [Q3: the 68040 MMU translates in Moira — full grid 7 200/7 200 pinned, the LC 475 CPU side is complete](#2026-07-18--q3-the-68040-mmu-translates-in-moira--full-grid-7-2007-200-pinned-the-lc-475-cpu-side-is-complete)
- **2026-07-18** — [Q2+Q4: the 68LC040 integer core executes in Moira, WinUAE-differential (5 400/5 400), no-FPU F-line included](#2026-07-18--q2q4-the-68lc040-integer-core-executes-in-moira-winuae-differential-5-4005-400-no-fpu-f-line-included)
- **2026-07-18** — [GISTPERSO (7.5) boot hang: heap corruption racing an app launch at Finder startup — NOT the pending changes, NOT the disk](#2026-07-18--gistperso-75-boot-hang-heap-corruption-racing-an-app-launch-at-finder-startup--not-the-pending-changes-not-the-disk)
- **2026-07-17** — [LC II runs on a dedicated machine thread; boot & secondary SCSI volumes selectable from a "Disques" menu](#2026-07-17--lc-ii-runs-on-a-dedicated-machine-thread-boot--secondary-scsi-volumes-selectable-from-a-disques-menu)
- **2026-07-17** — [i-cache overlay folded into Moira's fetch path (-15%)](#2026-07-17--i-cache-overlay-folded-into-moiras-fetch-path--15)
- **2026-07-17** — [Lode Runner "dead arrow keys": not a bug — the game binds the numeric keypad by default](#2026-07-17--lode-runner-dead-arrow-keys-not-a-bug--the-game-binds-the-numeric-keypad-by-default)
- **2026-07-17** — [Performance pass: 0.40× → 1.91× realtime at the Finder (the sound stutter was the emulator falling behind real time)](#2026-07-17-performance-pass-realtime)
- **2026-07-17** — [Lode Runner launch freeze: odd-SP interrupt frames were corrupt (vendored Moira fix) + sound tempo locked to the host DAC](#2026-07-17--lode-runner-launch-freeze-odd-sp-interrupt-frames-were-corrupt-vendored-moira-fix--sound-tempo-locked-to-the-host-dac)
- **2026-07-17** — [SC2K "coprocesseur absent" ROOT-CAUSED AND FIXED: Egret mid-flight packet retraction manufactured ghost ADB sessions](#2026-07-17--sc2k-coprocesseur-absent-root-caused-and-fixed-egret-mid-flight-packet-retraction-manufactured-ghost-adb-sessions)
- **2026-07-17** — [68030 instruction-cache timing overlay (replaces the flat boost)](#2026-07-17--68030-instruction-cache-timing-overlay-replaces-the-flat-boost)
- **2026-07-17** — [retire the adaptive cache boost for a constant ratio](#2026-07-17--retire-the-adaptive-cache-boost-for-a-constant-ratio)
- **2026-07-17** — [app sound reaches the ASC (pseudo-VIA ASC IRQ was edge-only)](#2026-07-17--app-sound-reaches-the-asc-pseudo-via-asc-irq-was-edge-only)
- **2026-07-17** — [adaptive cache boost (fixes big-city SimCity crash)](#2026-07-17--adaptive-cache-boost-fixes-big-city-simcity-crash)
- **2026-07-17** — [adversarial subsystem audit #2: 9 correctness fixes](#2026-07-17--adversarial-subsystem-audit-2-9-correctness-fixes)
- **2026-07-17** — [adversarial subsystem audit: 3 correctness fixes](#2026-07-17--adversarial-subsystem-audit-3-correctness-fixes)
- **2026-07-17** — [LC II GUI defaults to 640×480](#2026-07-17-lcii-gui-640x480)
- **2026-07-16** — [LC II keyboard: arrow keys + numeric keypad](#2026-07-16--lc-ii-keyboard-arrow-keys--numeric-keypad)
- **2026-07-16** — [SimCity 2000 crash fixed: 68030 i-cache throughput model](#2026-07-16--simcity-2000-crash-fixed-68030-i-cache-throughput-model)
- **2026-07-16** — [Selectable resolution (512×384 / 640×480) + per-monitor depth](#2026-07-16-selectable-resolution)
- **2026-07-16** — [SCSI write-back (persist guest disk writes)](#2026-07-16--scsi-write-back-persist-guest-disk-writes)
- **2026-07-16** — [LC II color (8 bpp by default) + peripheral-tick batching](#2026-07-16--lc-ii-color-8-bpp-by-default--peripheral-tick-batching)
- **2026-07-16** — [LC II GUI showed a black screen (texture alpha)](#2026-07-16--lc-ii-gui-showed-a-black-screen-texture-alpha)
- **2026-07-16** — [review fixes (8-angle bug hunt) + UI: mouse capture, drag fix, machine menu](#2026-07-16--review-fixes-8-angle-bug-hunt--ui-mouse-capture-drag-fix-machine-menu)
- **2026-07-16** — [O6.11 RESOLVED: GISTPERSO boots to the Finder — Egret XPRAM protocol fix makes AppleTalk genuinely inactive](#2026-07-16--o611-resolved-gistperso-boots-to-the-finder--egret-xpram-protocol-fix-makes-appletalk-genuinely-inactive)
- **2026-07-16** — [O6.11: LocalTalk LAP — SCC abort stream + HLE watchdog](#2026-07-16--o611-localtalk-lap--scc-abort-stream--hle-watchdog)
- **2026-07-15** — [O6.9 resolved: GISTPERSO's vector-2 storm — RTE honors a cleared SSW.DF](#2026-07-15--o69-resolved-gistpersos-vector-2-storm--rte-honors-a-cleared-sswdf)
- **2026-07-15** — [Basilisk II knowledge applied: rominfo, XPRAM defaults](#2026-07-15--basilisk-ii-knowledge-applied-rominfo-xpram-defaults)
- **2026-07-15** — [O6: **Mac LC II boots to the Finder desktop**](#2026-07-15--o6-mac-lc-ii-boots-to-the-finder-desktop)
- **2026-07-15** — [O6: the LC II ROM boots to the blinking-? screen](#2026-07-15--o6-the-lc-ii-rom-boots-to-the-blinking--screen)
- **2026-07-15** — [O6 (LC II machine): first six slices](#2026-07-15--o6-lc-ii-machine-first-six-slices)
- **2026-07-15** — [O5 follow-ups: 68882 timing + FRESTORE frame acceptance](#2026-07-15--o5-follow-ups-68882-timing--frestore-frame-acceptance)
- **2026-07-15** — [Musashi oracle retired: the loop is WinUAE-solo](#2026-07-15--musashi-oracle-retired-the-loop-is-winuae-solo)
- **2026-07-15** — [O5 slice 2: 68882 FPU execution in Moira](#2026-07-15--o5-slice-2-68882-fpu-execution-in-moira)
- **2026-07-15** — [O4 slice 4: integer-family arbitration (O4 complete)](#2026-07-15--o4-slice-4-integer-family-arbitration-o4-complete)
- **2026-07-15** — [O4 slice 3: the 68030 MMU bus layer (Moira translates)](#2026-07-15--o4-slice-3-the-68030-mmu-bus-layer-moira-translates)
- **2026-07-15** — [Phase 2 live: two 68030 oracles + arbitration turn 1](#2026-07-15--phase-2-live-two-68030-oracles--arbitration-turn-1)
- **2026-07-15** — [O4 slice 1: Moira executes the 68030 MMU instructions](#2026-07-15--o4-slice-1-moira-executes-the-68030-mmu-instructions)
- **2026-07-15** — [M6: the startup chime plays](#2026-07-15--m6-the-startup-chime-plays)
- **2026-07-15** — [M7: System 6 boots from a SCSI hard disk](#2026-07-15--m7-system-6-boots-from-a-scsi-hard-disk)
- **2026-07-14** — [M5.5: the Finder is drivable (keyboard + mouse)](#2026-07-14--m55-the-finder-is-drivable-keyboard--mouse)
- **2026-07-14** — [M5: System 6.0.5 boots to the Finder from floppy](#2026-07-14--m5-system-605-boots-to-the-finder-from-floppy)
- **2026-07-14** — [M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060](#2026-07-14--m45-singlesteptests680x0--1-000-058--1-000-060)
- **2026-07-14** — [M4 complete: cycle-accurate boot hardware](#2026-07-14--m4-complete-cycle-accurate-boot-hardware)
- **2026-07-14** — [M0–M3.5 + first real-ROM boot](#2026-07-14-m0-m35-first-rom-boot)

---

<a id="2026-08-05-q605-beyond"></a>
## 2026-08-05 (fifth) — Beyond-boot reaches a second machine: Quadra 605 soak + persist, and the 53C96 finally takes a real guest WRITE

TODO § 2's first closer, on the LC II template
(`lcii_beyond_etalon.cpp`): `tests/q605_beyond_etalon.cpp`, one CTest
entry per scenario — `q605_soak_etalon` and `q605_persist_etalon`
(labels auto-derive to `etalon` + `m040`; 145 → 147 gates). The Q605
side reuses the boot etalon's own plumbing — GDevice-PixMap decode
through the DAFB CLUT and the full menu+desktop luminance signature —
rather than a hardcoded mode, and holds every key 150 frames per the
`q605_cudalle_key_etalon` convention, so the gates stay green whether
or not the image's Easy Access state ever regresses.

- **soak** — 180 emulated seconds idle after the Finder: the Mac clock
  advanced **exactly 180 s** (window 135-225), no halt, Finder
  signature intact, Cuda LLE active. This is the gate class that
  catches the MCU-overclock drift family (CHANGELOG 2026-07-29) which
  boot gates are blind to — now armed on a Cuda machine, not just the
  LC II's Egret.
- **persist** — Cmd-N in the 8.1 Finder, catalog needle
  `untitled folder` ×14 → ×16, image bytes modified, and **+145 838
  DMA bytes through the 53C96 during the gesture** — the first gate to
  drive the TurboSCSI WRITE path end to end from a real guest
  (every other 53C96 gate boots read-only). After a deliberate hard
  reset with no clean unmount, the machine boots back to the Finder
  off the modified volume and the folder is still in the catalog.
- One calibration finding, measured not assumed: the **post-reset
  reboot needs more than 12 000 frames** (the clean-boot budget that
  walled the first run) and fits in 24 000. Plausibly 8.1's
  consistency pass over the dirty volume (drVolAtrb bit 8 clear — the
  reset skips the unmount by design; surviving that is part of what
  "persist" claims), but the attribution is a hypothesis; the budget
  is the measurement. The LC II template's 16 000 on System 7.5 hid
  the same headroom question.

Also verified en route: `Ncr53c96::reset()` keeps its attached targets
and stat counters (only the transaction state clears), so a mid-run
`hardReset()` needs no re-attach — the reboot leg rests on that.

<a id="2026-08-04-iifx-mirror-cd-hot"></a>

<a id="2026-08-05-iwm-swim-bughunt"></a>
## 2026-08-05 (fourth) — IWM/SWIM bughunt: the Q700 spindle ran 1.6x fast, and the IWM personality was half-speed-blind on C15M hosts

A line-by-line hunt over `Iwm`/`Swim1`/`Swim2`/`SonyDrive` against the
MAME oracles (`iwm.cpp`, `swim1.cpp`, `swim2.cpp`, plus `floppy.cpp`
fetched for `mac_floppy`). Two real defects, three conformance fixes,
one wrong turn, two exonerations:

- **Q700/Q900/Q950 spindle clock mismatch** (the find of the hunt).
  `Q700Memory` set `setSpinClockHz(15667200)` — a leftover from the
  original Spike bring-up (a663b76) — while ticking the drives in
  machine cycles at 25/33 MHz. Every observable derived from `spin_`
  (senseSwim index reg 4/C, tach reg B, the rotation angle
  `syncCellsToRotation` lands on) ran cpuHz/C15M ≈ 1.6x (2.1x on the
  Q950) too fast. This is exactly the class a11c29c fixed *inside*
  `SonyDrive` the day before; the Q700 platform contract predated it
  and was never migrated — and no Eclipse floppy gate existed to
  notice. Now `setSpinClockHz(cpuHz_)`, the Centris/Q605 contract.
  The SWIM1 *cell* engines stay in C15M via `syncSwimFromCpu` — that
  domain is the controller's, not the spindle's.
- **The IWM personality ran the disk 2x fast on every C15M host**
  (Mac II family ticking `Iwm` in 15.6672 MHz machine cycles; the
  SWIM1 IWM personality on V8/RBV/IIfx/Q700). `kCyclesPerNibble = 128`
  assumed C7M ticks, and `SonyDrive::sense()` case 7 (TACH) hardcoded
  7833600 against a machine-cycle `spin_` — two errors that agreed
  with each other (nibbles AND tach both 2x), which is why floppy
  boots never noticed. MAME settles the intent: `swim1.cpp
  iwm_half_window_size()` returns exactly 2x `iwm.cpp`'s values —
  the real chip doubles its bit windows at C15M. `Iwm::setClockHz()`
  now scales the nibble window (Swim1 hardwires it, `MacIIMemory`
  sets it), and the IWM tach shares `spinCyclesPerRev()` with the
  SWIM senses — one clock rule everywhere, per-cylinder speed groups
  included.
- **IWM mode/data writes latched on even accesses**: MAME's
  `control()` only routes a write through `(offset & 1)`; POM
  applied it whenever Q6·Q7 ended up set, so a write to an even
  clear-line address in (1,1) state could clobber the mode register.
  Now gated on the odd address.
- **`SonyDrive::reset()` hardening**: clears `gcrWrBuf_` (a machine
  reset mid-write could commit a stale half-sector on the next
  flush) and adopts MAME's `m_mfm = m_has_mfm` — a SuperDrive powers
  up in MFM mode, giving the documented x011 capability signature
  before any media lands. And `insertImage()` no longer promotes the
  mechanism to SuperDrive on an HD image: an HD disk in a plain 800K
  drive is unreadable, exactly like the real thing.
- **The wrong turn, kept for the record**: the hunt flagged
  `decodeGcrBytes`' first-wins sector dedup as a defect ("a rewrite
  in the same window should last-win") and the fix promptly broke
  `swim2_media_test`. First-wins is load-bearing: a splice landing at
  the live rotation angle can leave the sector's OLD field intact
  elsewhere on the track, and the first field under the head is what
  a read returns. Reverted, and the comment now says why.
- **Exonerated, worth recording**: `Swim1`'s write timing
  `params_[P_TIME1] + 2*2` is verbatim MAME (not a precedence bug);
  and senseSwim reg 3 returning `!hasDisk()` is *equivalent* to
  MAME's dskchg because `mac_floppy` sets `m_dskchg_writable = true`
  (insertion re-arms the flag by itself) — the open 7.5.5 hot-insert
  refusal (TODO § 1) does **not** hide there.

Gates: `gcr/iwm_write/swim1/swim2/swim2_media_test`, unit 67/67,
smoke 8/8, and serially `disk_boot`, `system_boot`, `lcii_boot`,
`lcii_floppy` (the max-risk one: SWIM1 host, 2x window + MFM reset),
`q605_floppy_boot`, `q700_boot` (the fixed platform), `macii_boot` —
all green on freshly built binaries.

<a id="2026-08-05-cache040-closed"></a>
## 2026-08-05 (third) — The m040 sweep is paid and the cache chantier closes at M1

The 8.1 boot volume got its one-time GUI cleanup in the morning
(drVolAtrb back to $0100 at 10:08 — it had been left attached by a GUI
session the night before), which unblocked the sweep M1 still owed:
**`ctest -L m040`, 33/33 green with `POM68K_040_DCACHE=1`, on freshly
relinked binaries** (29 targets rebuilt first — the m040 tier shares
binaries: centris610/quadra610/650/800 ride `centris650_boot_etalon`,
q900/q950 ride `q700_boot_etalon`, lc580 rides `q630_boot_etalon`,
cdboot/cdhot ride `q605_cdrom_etalon`). 2 h 00 wall, partly alongside
another session's ctest — passes stand; only failures would have
required serial reruns, and there were none.

With that, the § 3 decision point resolves and **the chantier closes
at M1** — its named honest exit. No concrete motivation exists for
M2's data path: no guest or diagnostic in the roster observes cache
*content* (tags already give CINV/CPUSH real state), timing needs the
throughput overlay rather than a data path, and the first snoop
client (IIfx SCSIDMA) is itself deferred. Against zero observable
gain stood the JIT DTLB fence across the whole 040 fleet, the display
seam, MOVE16 and staleness gates. Three reopening conditions are
named in `docs/CACHE_040.md` § 3 (a content-dependent guest, the
SCSIDMA client, an overlay-breaking timing goal); until one lands,
the largest remaining wall for a *full 68040* claim stays the native
FPU opmodes $40-$7F, and the next action moves to test depth
(`TODO.md` § 2).

<a id="2026-08-05-cache040-bughunt"></a>
## 2026-08-05 (later) — The M1 bughunt: three real defects the gates were green over

An adversarial multi-agent hunt over the fresh M1 diff (4 finders with
distinct lenses, every finding re-verified by a refuter told to walk the
trigger through the code line by line; 10 found, 9 confirmed, 1 refuted)
caught three real code defects and four gate holes — all fixed the same
sitting, `cache040_test` grown 32 → 44 checks:

- **An infinite loop at the top of the address space.** `Cache040::
  touch`'s span walk compared `a <= hi` in u32 — a tautology when the
  access's last byte is PA `$FFFFFFFF`, so one cacheable access there
  (trivially reachable MMU-off, where the default CM is cachable) hung
  the CPU thread. Two finders caught it independently; both refuters
  reproduced it standalone. The walk now runs in u64 with an exclusive
  end. *Lesson: the model was tested at the addresses Mac OS uses, not
  at the edges the type system makes dangerous.*
- **The "non-faulting" peek walk could fault.** `mmu040PeekWalk`'s
  descriptor fetches ride the machine bus; a garbage-but-resident
  chain landing in unmapped space raised `extBusError040` — flag ON
  delivering a format $7 access error (with the *previous* access's
  stale fault context) where flag OFF's CINV was a silent no-op.
  `pomCache040Phys` now catches `MmuBusError` and treats the chain as
  unmapped. The refuter's caveat is honest: a correctly running guest
  never builds such a chain — but M1's contract is absolute, and a
  cache-less model's forgiveness of *buggy* guests is exactly what
  this chantier is about.
- **Phantom lines after /BERR.** The touch runs at translate time; if
  the bus access it describes then /BERRs, the model kept a valid —
  even dirty — line no real 040 would hold (TEA-terminated fills leave
  no line, UM § 7). `extBusError040` now rolls back the stamped span;
  the stamp is cleared by non-allocating touches and by the peek walk
  so it can never invalidate an older, legitimate line.
- **Gate holes**, each now pinned: the CINV/CPUSH resolver's MMU-on
  paths had zero coverage (part 2 now builds real page tables, enables
  the MMU mid-program, and exercises ATC-hit, peek-walk-after-flush,
  unmapped-skip and a /BERR descriptor chain through a bus hole);
  disabled-cache retention and CINV/CPUSH-on-disabled (UM § 4.4) were
  unpinned; the replacement check couldn't tell invalid-first+counter
  from any other policy (victims now asserted way by way); and PA[9]
  wasn't proven an index bit — a 32-set cache passed every geometry
  check (the house too-wide-assert failure mode, again).

One finding was refuted and stays refuted: the "flag ON == flag OFF has
no real check" report — non-interference is structural (the model
stores no data), and the OFF/ON evidence lives at whole-boot scale in
the § M1 gate list. Re-validated after the fixes: `cache040_test`
44/44, `sst68040` 7 200/7 200, the 5 JIT lockstep gates — all flag ON.

<a id="2026-08-05-cache040-m1"></a>
## 2026-08-05 — Cache 040 M1: CINV and CPUSH finally act on real state, and the tags cost nothing

M1 of the copyback/snooping chantier (`docs/CACHE_040.md`) landed in one
sitting: **the two 68040 on-chip caches exist as architectural TAG state**
— 4 KB, 4-way, 64 sets, 16-byte lines, per-longword dirty bits, the UM § 4
geometry — behind `POM68K_040_DCACHE` (default off). Loads and stores
allocate and dirty tags per the page's CM mode, CINV discards, CPUSH
pushes-and-invalidates with line/page/all scope, CACR DE/IE gate
allocation. **Data is still served by the bus**: the model observes, never
interferes, which is why the milestone was safe to land whole.

Design points worth remembering (detail: `POM68K_VENDOR.md` § *68040
cache-TAG model*):

- The touch rides `mmu040Translate`'s three return paths, so the CM mode
  is always the one the access really used — TTR field, descriptor
  status, or the MMU-off writethrough default (UM § 3.5.1). No new
  walk, no new state source: the same bits the M0 probe histogrammed.
- CINV/CPUSH line/page operands translate through a **non-faulting,
  side-effect-free** resolver (ATC scan + a read-only twin of
  `mmu040Walk`): the flag must never make the guest observe MMU state
  it would not have observed flag OFF. An unmapped operand skips the op
  — defensible only while memory is current, i.e. only in M1.
- The caches are flushed on save-state restore (`pomFlushAtcs`), never
  serialized — the ATC convention, unchanged.
- Two accepted approximations, both M2 work: SSW-size reuse on split
  sub-accesses can over-mark one dirty longword; the JIT DTLB fast path
  bypasses the touch, so tag state under the JIT is approximate.

Gates: `cache040_test` (new, 32 checks — the struct bare, then
CACR/DTT0/CINV/CPUSH through a bare 68040 Moira), and flag ON:
`sst68040` 7 200/7 200, the 5 JIT lockstep gates, and
`q605_boot_etalon` (interpreter, serial rerun) printing the
**byte-identical** signature to flag OFF — `menu 230.6/8.3, desktop
128.3/33.5, SCSI=5780`. Which is the *dirty-image* phantom signature:
`hdv/MacOS-8.1-boot.vhd` was left dirty by a GUI session (mtime 23:46
the night before, drVolAtrb bit 8 = 0), so the Quadra etalons prove
image, not code, flag ON and OFF alike. The full flag-ON
`ctest -L m040` sweep is **owed** after the one-time GUI cleanup
(`TODO.md` § 1). Same-night hazard worth naming: a second session's
concurrent `ctest` clobbered `LastTest.log` mid-read — the serial
rerun captured the binary's stdout directly instead.

## 2026-08-04 (soir) — The IIfx SCSI mirror mounted one volume seven times; CDs hot-mount under 8.1; the MacIP window opens up

**Seven icons, one disk.** On the IIfx desktop the boot volume appeared
seven times, and a POM68KProber report read straight out of the flushed
floppy image (block-diff against the pristine build, then strings) showed
why: seven drive-queue entries, driver refNums -33..-39 — one per SCSI ID.
`IIfxMemory::attachScsi` mirrored the one `ScsiDisk` on **all seven IDs**,
a bring-up workaround copied from `MacIIMemory`, whose ROM dedupes the scan
($B2E). The IIfx path does not: System 7.6.1 installed seven drivers and
mounted the same HFS volume seven times, and seven write-mode VCBs over one
store eventually **corrupted `hdv/MacOS-7.6-boot.vhd`** (it now halts at
the extension parade; the IIfx etalons stay red until the image is
restored). Fix: attach at the requested ID only — `boot.vhd` reaches the
Finder. One open case: GISTPERSO (7.5.5, never an IIfx volume) wedges
under single-ID at ROM `$4081B66E`, a list-search that only exits by
finding the searched id; the mirror masked it because every id existed.
The Mac II keeps its mirror (its ROM dedupes — same reasoning as before).

**CDs hot-mount under Mac OS 8.1.** The missing physics: an empty drive
that is *present* at boot (`ScsiDisk::attachCdromEmpty` — INQUIRY answers,
TEST UNIT READY says NOT READY), then, on media arrival, exactly one CHECK
CONDITION / UNIT ATTENTION `$28` (not-ready-to-ready), and — the piece the
trace surfaced — **READ SUB-CHANNEL ($42)** answered instead of rejected:
the 8.1 CD extension sends `42 02 40 01` right after the change and aborts
the mount it had already started on a CHECK. With both in place the
hot-inserted disc mounts and opens its window (~39 READs). Gated:
`q605_cdhot_etalon` (third mode of `q605_cdrom_etalon`, `POM68K_CD_HOT=1`)
plus the UA/$42 pins in `scsi_cdrom_test`. The Disques window's «Réserver
un lecteur CD vide» checkbox — previously wired to nothing — now stages a
`cdbay` extras token every runner turns into an empty drive at boot, and
CD bays swap media live through the same command-queue path as the floppy.

**MacIP throughput.** The host→guest TCP window was capped at 4×MSS
(~2 KB in flight) since the gateway's first commit — throughput was 2 KB
per guest ACK round-trip, tens of KB/s against a ~230 KB/s boosted wire.
Now `min(32×MSS, guestWin)`; the advertised window stays the hard bound.

**The 7.5.5 hot-floppy refusal, first layer.** A GCR floppy hot-inserted
on a Quadra mounts under Mac OS 8.1 but comes up "unreadable — format?"
under System 7.5.5 (reproduced headless: the driver spins up, selects GCR,
steps to track 4, motor off, gives up). The 7.5 Sony driver times the
drive's INDEX pulses after an insert; `senseSwim` reg 4/C generated them
from `spin_ % (7833600/5)` — the Mac Plus clock and 300 RPM hardcoded,
while `spin_` counts machine cycles (25 MHz on the Q605) and a GCR
cylinder spins at 394-590 RPM. The measured speed came out ~2.4× off and
the driver refused the medium. Reg B (tach) carried the same wrong-clock
constant. Both now derive from `spinCyclesPerRev()` (drive clock ×
per-cylinder speed group, MAME `!m_idx` semantics) — a real bug on every
non-Plus host, kept.

> **Retracted the next morning (2026-08-05):** the *motivating symptom*
> was never reproduced. The headless probe judged "mounted" from
> `SonyDrive::nibblesRead`, which only the IWM path increments — on a
> SWIM2 machine it reads 0 whatever happens, so every run printed "NOT
> MOUNTED", including the 8.1 arm the user reports as working. Re-judged
> on the DESKTOP (a mounted volume paints its icon; screen-diff on the
> icon strip), the *unmodified* tree mounts a hot-inserted Rogue.dsk
> under 7.5.5 on the Quadra — verified by dumping the framebuffer: the
> volume icon is there, no dialog. Two changes built on that false
> reading were dropped before they reached a commit (a hot-insert
> spin-up delay in `SonyDrive`, a write-mode gate on SWIM2 register 2),
> and the flux-rate variant of reg 4/C was reverted to MAME's per-rev
> pulse. **The user's GUI symptom is real but is NOT reproduced by the
> headless path** — the difference to chase next is the GUI itself
> (machine-thread insert against a running emulation, PRAM/Finder state,
> the actual image on the actual profile), not the SWIM2 model.
> Lesson, again: a "0" observable that cannot rise is not evidence
> ([[pom68k-false-green-wide-assert]] in memory, now joined by this).

Also: the Disques window lists the floppy (the always-hot bay) above the
cold boot/SCSI choices; `disks35/Stuffit_Expander_5.5.dsk` was 1440K plus
84 bytes of $F6 tail padding, which `insertImage`'s exact-size check
rejected — trimmed, it inserts.

<a id="2026-08-03-event-deadlines"></a>
<a id="2026-08-04-deadlines-six"></a>

## 2026-08-04 — Event deadlines reach six more platforms: min(MCU bound, historical batch)

Sonora, VASP, RBV, Centris, Q700/Eclipse and Q630 leave their fixed
peripheral batches for the deadline mechanism, with one uniform and
provably safe bound: **min(the machine-cycle bound the memory can state,
the wrapper's historical batch)**. The cap is the load-bearing half of
the design — every transport without a deadline API (the IIci's PIC ADB
modem, the Centris/Spike AdbVia, the Eclipse IOPs and Egret HLE) keeps
exactly its former cadence, so exactness can only refine, never coarsen.
Where a firmware MCU LLE runs (Sonora and VASP Egret/Cuda, the IIsi, the
Q630 Cuda), the memory states `egretLle_.cyclesToNextEvent()` and the
guest now sees MCU transitions on their true cycle instead of quantized
to a 128-Moira-cycle batch — the same exactness trade the V8 conversion
made on 2026-08-03. The three 040 cousins (Centris, Q700, Q630) also
switch their JIT pacing from `setPeriphPacing` to `setPeriphDeadline`,
the mode the leaveToDynamic fix (previous entry) made trustworthy, and
every converted wrapper serializes its new `periphDeadline_`.

Gated the expensive way: **27 serial gates, 95 minutes, all green** — the
five Sonora boots + two input etalons, three VASP, three RBV (IIsi Egret
and IIci PIC both), the five djMEMC/IOSB boots, both F108 boots, the
three Q700-board boots (q700 AND q900 AND q950, per the guard rail),
both jit cousins' full boots on the new deadline mode, and the 030/040
save-state suites covering the serialized field. Still on a fixed batch,
each for a stated reason (TODO § 4): the compacts (cycle-exact by
construction — nothing to buy on the cheapest machine to emulate), the
Mac II family, the IIfx (two always-executing IOPs make the fan-out
intrinsic) and the MSC.

<a id="2026-08-04-floppy-ci"></a>

## 2026-08-04 — Hot floppy swap reaches every runner; release CI for four OS targets; the x64 dynamic-link regression found and fixed

**Floppy hot-swap is now universal.** The Disques window's floppy bay
existed but was wired into only the four 040 runners. The same command
pair (`Cmd::InsertFloppy`/`EjectFloppy`, pending path swapped under the
queue mutex, an atomic inserted flag) now lives in `MacIiMachine`,
`IIfxMachine`, `LcMachine` and `SonoraStyleMachine` — which carries
Sonora, VASP and RBV at once — and the single-threaded compacts loop
calls `mem.insertDisk/ejectDisk` directly between frames. The seven
platform memories that lacked it gained a one-line `ejectDisk()`
(SonyDrive flushes write-back on eject by itself). Two adjacent gaps
closed in the same pass: `diskBaysInstallDrop` had **zero callers** — the
window advertised drag-and-drop that could never fire; it is installed
after ImGui init in all 11 GUI loops now — and `POM68K_FLOPPY` seeds a
startup floppy on the runners that had no CLI floppy path. Guard rails
re-run green on fresh binaries: `lcii_floppy_etalon` (the hot-insert
proof) and 7/8 `ctest -L smoke`. The eighth is the red gate below.

**Release CI, adapted from POM1's battle-tested workflows.** Three
workflows (`ci.yml` — build + the ROM-free `unit` tier on every push;
`build-bionic-image.yml` — the frozen glibc-2.27 toolchain image, one
native job per arch; `release.yml` — tag-triggered artifacts + GitHub
Release) and the packaging tree they drive. Four artifacts: Linux
x86_64 **and aarch64** AppImages with a **glibc 2.27 floor** (bionic
builder image, g++-11 for C++20, ET_EXEC runtime — the aarch64 one IS
the Raspberry Pi 400 package: Pi OS ships glibc 2.36 and POM68K's GL 3.0
request fits V3D's desktop GL 3.1), a macOS **Universal 2** .dmg
(static universal GLFW, no Homebrew prefix ever baked in), and a
Windows x64 zip (vcpkg `x64-windows-static`, /MT, zero app-local DLL).
POM1's hard-won invariants are asserted, not assumed: ET_EXEC + magic +
glibc-floor checks, lipo both-slices, no-stray-DLL, and a
`--version` smoke-launch — a new flag that prints and exits before any
window, stamped from the new repo-root `VERSION` file
(`-DPOM68K_VERSION` overrides; target-scoped so a bump recompiles one
TU). ROMs ship in **no** artifact; every package provisions a writable
user data dir (`roms/`, `hdv/`, `disks35/`) and says so in a README.
Bootstrap order: run build-bionic-image once, pin the digests it prints
into release.yml, then tag.

**`jit_q605_boot_etalon` went red and green again the same day — the x64
dynamic-link target rode a caller-saved register across the pacing
callout.** The smoke run caught it red after the A64/pacing merge
`93ae352` (default x64 wedged with zero SCSI in 20 G cycles). The hunt,
in order: env bisection (`POM68K_JIT_LINKS=0` and `threaded` both PASS →
the deadline pacing itself exonerated); the merge's own coarse
hidden-state lockstep turned against x64 → a deterministic 100 s
reproducer diverging at step 3 785 392 in a 4-block linked poll loop,
the jit suddenly at pc `$00008D22`; emitter probes (link lookup emitted
but jump suppressed → PASS; static links only → PASS) pinning the defect
to **`leaveToDynamic`**, the RTS/JSR/JMP-`<ea>` dispatch. Root cause:
those emitters computed the target into **RDI**, stored it to `pc`, then
ran `chargeCycles` — whose deadline callout (`pom68kJitSync`) clobbers
every caller-saved register — and only then did the link lookup **from
RDI**. With the new ~12-cycle Cuda deadline firing on nearly every RTS
in a device-poll loop, garbage RDI hit the table millions of times until
one leftover value matched a populated slot's tag and the chain jumped
into an unrelated compiled block (`$8D22` was a *real* block — a handler
entered without its interrupt). This also explains the eeriest symptom:
installing a pure, never-invoked lockstep hook made 5 M steps pass,
because the deeper C++ callee left different garbage in RDI. The fix is
the shape the **A64 backend already had** (its `leaveToDynamic` does
`ldrW(11, 0, L.pc)` — why only x64 ever failed): reload the target from
`at(L_.pc)` inside `leaveToDynamic`, registers not trusted across the
callout. Latent since block linking landed; detonated by exact pacing.
Green after fix: the coarse x64 lockstep (5 M identical, links active),
`jit_q605_boot_etalon` (Finder, 256 colors), `ctest -L smoke` **8/8** —
and the tier dropped 193 s → 114 s, the wedge no longer burning its
whole cycle budget.

<a id="2026-08-04-a64-green-fast"></a>

## 2026-08-04 — AArch64 Finder gate green and fast: hidden-state lockstep plus two host-side bottlenecks removed

The coarse AArch64 failure was not peripheral drift. Lockstep now snapshots
CPU pacing and serialized VIA/Cuda/ADB/SCC/ASC/SWIM/SCSI/DAFB state and can
trace the exact coarse slice. That localized four emitter defects: NEG moved
an immediate instead of a register, EA commit reused a live ALU scratch,
MOVE.B/W immediates were not masked after signed decode, and BFINS lost its
mask while updating Z. Five-million-step fine/coarse locksteps and the complete
Q605 Finder gate pass, so arm64 `auto` now selects AArch64.

Two macOS `sample` runs found the real host costs. `CodeBuffer` invalidated its
entire 128 MiB reservation after every compiled block; it now flushes only the
newly appended tail. Later, 766/767 samples in a long boot landed in
`Engine::markPages`: libc++'s `unordered_multimap` linearly scanned thousands
of equivalent slice keys and compilation inserted every block a second time.
The replacement `slice -> vector<block key>` index appends in amortized O(1),
and compilation only performs the required DTLB flush.

Release/native/LTO, identical assets and signatures: the fixed 1,000-frame
workload is 1.22 s A64 versus 4.55 s threaded (3.73x); the full Finder gate is
9.19 s versus 21.14 s (2.30x). Existing LLVM PGO gives 1.01 s versus 3.41 s
(3.38x), and the full gate 7.86 s versus 15.28 s (1.94x). The cache-tail and
slice-index fixes are host-neutral and also benefit Linux AArch64/Raspberry Pi.

## 2026-08-03 — Event deadlines close the Cuda phase accommodation; extended ADB input reaches every GUI runner

The M68HC05 interrupt entry again costs the silicon's **11 cycles**. The old
zero-cycle return was not an instruction-core interpretation: it was a timing
accommodation after the Mac TV's Cuda/VIA byte handshake stopped at seven CB1
edges when that 2 % throughput cost moved the MCU phase. A synthetic ROM in
`m68hc05_test` now pins the oracle shape directly: 11 cycles of entry followed
by the handler's two-cycle NOP in the same `run()` iteration = 13.

The fixed Q605/V8 peripheral batches were replaced by absolute deadlines.
`CudaLle` derives the first machine cycle able to pay its fractional clock
bridge plus carried instruction-overshoot debt. Q605 takes the conservative
minimum over Cuda, the VIA E clock, SCC live countdowns, ASC drain, active SWIM
cells, 53C96 service latency, 60.15 Hz CA1 and DAFB interrupts. Device accesses
still force a flush, so continuously accumulated state is current when read.
The HLE V8 fallback deliberately keeps its historical 128-cycle batch.

The contract has its own bite: `cuda_lle_test` proves no MCU cycle occurs
before the reported deadline and that progress occurs exactly on it. A Q605
boot with `POM68K_PERIPH_STATS=1` delivered 1.675 G machine cycles through
86.65 M full `mem.tick()` fan-outs (**19.34 cycles/call**), versus 833.2 M
under the former exact batch-1 mode — **9.6× fewer entries** without timing
approximation. The former phase reproducer is green with the real cost:
`mactv_boot_etalon` reaches the Finder. So do all three longer firmware gates:
`q605_cudalle_key_etalon` (including 500 fast transitions), boot and mouse.

The host-side ADB follow-up closed in the same pass. `ScreenInput` sends left
and right mouse state with a button index through every threaded machine
command; `AdbLine` receives button 1, while HLE and quadrature one-button paths
discard it. All ten ADB GUI key maps now emit distinct right Shift, Option and
Control codes (plus both Command keys). Handler 3 preserves the right-hand
codes; handlers 1/2 retain their intentional fold onto the left modifiers.

<a id="2026-08-03-three-items"></a>
## 2026-08-03 — Three items closed by measurement — and a GREEN ctest that proved nothing

Three `LLE_VS_HLE.md` entries settled, two of them by *removing* a claim
rather than adding code. And one process failure worth more than any of them.

### The `ctest` that proved nothing

`CLAUDE.md` briefly carried "143 CTest gates, 143/143 green — a FULL run".
It was a full run. It was also **worthless**, and the check that caught it was
almost an afterthought: before committing, compare each test binary's mtime
against `libpom68k_core.a`.

**102 of ~110 binaries were older than the library.** `ctest` does not build;
it runs whatever is on disk. The day had been spent rebuilding *by target*
(`make -j4 q605_boot_etalon`, …) — which is the documented house practice,
because a full `make` relinks ~110 binaries under tree-wide LTO. The result is
that the binary tree had **laminated**: each etalon carried the source state as
of its own link. `src/Cpu040.cpp` changed at 07:16 and only `q605_boot_etalon`
was relinked behind it — while `Cpu040` serves all eight 040 machines.

The repo already warns: *do not edit during a build — mixed-tree binaries give
phantom FAILURES*. This is the other half, and it is the dangerous half:
**a phantom failure gets investigated; a phantom pass gets quoted.**

> **A green `ctest` is only worth the freshness of its binaries. Run `make`
> first, always.**

Two good habits — targeted rebuilds, and a full suite run — combined into a bad
measurement. Nothing was committed on it.

### Classic II `$F18000` — named, after sitting as "identify the real block"

It is the analogue **CRT brightness/contrast DAC**, the cell MAME maps as
`spice_device::bright_contrast_w` on the Color Classic. Three independent
pieces of evidence:

1. `rominfo --universal` on the `$3193670E` ROM: the Classic II record
   (`$003D14`, Gestalt 23, `UnivROMFlags $1A6`) has its own DecoderInfo
   `$04C7A6` with **`decoder[6] = $50F18000`** → LMG `$0B0A`/`$0312`. That
   entry is **zero on every LC / LC II / IIci / IIfx / IIsi record** in the
   same table.
2. The ROM routine at `$A51350` disassembles as a DAC feed: a 0-255 setting
   scaled `×$2B >> 8` to 0-42, de-scrambled through a 43-entry table, then
   shifted out **370 times at stride 2**. The other two sites are its presence
   test and a 768-byte ramp init.
3. Same address, same purpose, in MAME's Spice.

The prior note called these "wild pointers". They are three deliberate
routines. Behaviour is unchanged — the Eagle's forgiving bus already discarded
the writes — but a **named** device that is deliberately ignored is not the
same artefact as an unexplained hole. Also settled: the hole VALUE is
unobservable here (`$00` and `$FF` both boot with 9619 SCSI commands,
identical), and MAME's 0 is its `address_space` default, **not** a modelled
decision — so there is no oracle to be parallel to.

### VRAM arbitration — audited, and ACCEPTED

Closed by the negative, which is a result. `vram_r`/`vram_w` are a bounds check
plus `COMBINE_DATA` in **all four** relevant MAME devices — `v8.cpp`,
`sonora.cpp`, `valkyrie.cpp`, `dafb.cpp:915-933` — with no wait state and no
beam dependency. No figure in the Guide or our notes, no guest symptom ever
attributed to it. Implementing it would mean **inventing timing numbers**,
which the source ranking exists to prevent. Contrast the Mac Plus, where
contention IS cycle-exact — because GttMFH Table 5-3 gives 2.56 MB/s and
`contention_test` reproduces it. Reopening condition recorded, and the
tractable sub-case named (RBV, which displays from *main* RAM).

### Peripheral batching — the cost is located, and priced

`POM68K_PERIPH_STATS=1` counts the path. Over 1200 frames of
`q605_boot_etalon`: batch 256 → 25.6 M `mem.tick()` calls / 60.1 s; batch 1 →
**833.2 M calls** / 103.3 s, with the **same 1.650 G machine cycles delivered
either way** and `catchUp()` called 879 M times in both. So the cost is
neither the devices nor the hook: it is entering the ~15-device fan-out
**32.5× more often**.

A hypothesis died on the way, and it is recorded because it was a good one:
peripheral time is machine time, so below `cacheBoost_` Moira cycles
`flushTicks` computes `m == 0` and ticks nothing — pure call overhead. The
guard went in. **Worth 2 %.** The measurement disagreed with the reasoning,
and the measurement wins.

The fix is MAME's: a **deadline** instead of a batch, and the hard part —
deriving each device's bound from the code — is done. **No device has to fall
back to 1**: the Cuda's 6805 binds at ~12 machine cycles, the VIA E clock at
~32, the ASC drain at ~1123, the 60.15 Hz tick and DAFB VBL at ~416 000, and
the SCC / SWIM / drives / SCSI are **infinite while idle**, which is most of
the time. Against today's measured 2.0 machine cycles per fan-out entry that
is ~6× fewer entries: interpolating the measured curve, **exactness for ~12 %
instead of ~72 %**. Full table and migration order in `TODO.md` § 4.

<a id="2026-08-02-cmdn-retracted"></a>
## 2026-08-02 (sixth) — The "Quadra modifier bug" retracted: same machine, other image, works

`LLE_VS_HLE.md` § 5 listed one live **bug** among its simplifications: "Cmd-N
still fails to repaint on the Quadra under System 7.6, while the LC II control
is unaffected → the Quadra's modifier path has a second, unidentified cause."
It is wrong, and the inventory now has **no known live bug at all**.

**The experiment.** Cross machine against image instead of changing one and
concluding — three runs of `tests/adb_key_probe.cpp`:

| machine | image | hold | `$484185` | Cmd + N both live in KeyMap | repaints |
|---|---|---|---|---|---|
| Quadra 605 | `MacOS-8.1-boot.vhd` | 150 fr | `$FF` on | **yes** | no |
| Quadra 605 | `MacOS-8.1-boot.vhd` | 6 fr | `$FF` on | no | no |
| Quadra 605 | `GISTPERSO-boot.vhd` | **6 fr** | `$B6` n/a | **yes** | **yes** |
| LC II | `GISTPERSO-boot.vhd` | 150 fr | — | yes | yes |

Row 3 is the one that ends the argument: the **same machine**, with the
**same 6-frame taps** the 8.1 image throws away, delivers every key and
repaints on another image.

The Quadra repaints on another image. The variable was never the machine. And
on **every** cell — including the failing one — the guest's own KeyMap holds
Command and N *simultaneously*, which is the deepest observable the input
pipeline owns: the modifier reaches the guest. The failing cell is the 8.1
image that still has Easy Access **Slow Keys** enabled, i.e. the same dirty
image that caused the ten-month red gate five weeks ago.

**Why it survived that hunt — the probe was lying, in the way we had already
been taught.** `adb_key_probe`'s Cmd-N block **hardcoded** a 3-frame Command
hold and a 6-frame N hold. The 2026-07-31 diagnosis had just established that
this image rejects taps shorter than the Slow Keys acceptance delay — so every
"Cmd-N fails on the Quadra" measurement was taken with a gesture the guest was
entitled to throw away. The `POM68K_PROBE_HOLD` knob added by that very
investigation reached the typed sequence and *not* this block. The probe also
never sampled KeyMap **during** the gesture, so it could not distinguish "the
modifier is lost" from "the Finder did not act" — which is precisely the
question the entry claimed to have answered.

Both are fixed: the Cmd-N block honours `POM68K_PROBE_HOLD`, and it reports
Command / N / both-at-once separately.

**The reusable lesson, now paid for twice on the same image.** `KeyTime` was a
false observable because a periodic task stamped it without keystrokes; the
Cmd-N *screen hash* was a false observable because the stimulus was one the
guest discards. Both times the fix was the same discipline — **an observable
is worth nothing until it has demonstrated sensitivity, and a stimulus is
worth nothing until it has demonstrated the guest accepts it.** And when two
cells differ, change ONE variable: it took three runs to do what five weeks of
reasoning had not.

Outstanding, and now clearly worth its own line: **clean Slow Keys out of
`hdv/MacOS-8.1-boot.vhd`** (`TODO.md` § 1). That image has now cost two
investigations.

<a id="2026-08-02-i2c-dat1byte"></a>
## 2026-08-02 (fifth) — Two LLE gaps closed: the Cuda's I2C bus gets a second slave, and SWIM1 gets its DMA request line

Both are `LLE_VS_HLE.md` § 1 entries, both were small, and both turned out to
be *live* rather than theoretical — which is the part worth recording, because
"nothing reads it" was the stated reason each had been left open.

**The Valkyrie's pixel clock is programmed over I2C, and the guest does it.**
The Quadra 630 / LC 580's video clock had been a constant (31.3344 MHz) with
the note "that bus is not modelled". Tracing a real Q630 boot shows the Cuda
issuing four transactions to slave `$28` during video bring-up:

```
50 00 00      address $28/W, sub-address 0, data 0   (register 0: ignored)
50 02 1B      N = 27
50 01 0E      M = 14
50 03 02      P = 2
```

which is `3986400 × 2² × 27 / 14` = **30.752 MHz**. At 640×480 (htotal 864,
vtotal 525) that is **67.80 Hz** where the frozen default gave 69.08 Hz — a
guest-visible VBL cadence, moved from 3.6 % off Apple's nominal 66.67 Hz to
1.7 % off.

The work was **not** in `Valkyrie` — it was in `CudaLle`. Its I2C slave was
single-address (`$6F`) and **discarded the payload**, which is exact oracle
parity for the DFAC2 (MAME's `write_data` only logs; those registers are still
being reverse-engineered upstream) and useless for a device whose payload
means something. It now carries the whole `i2c_hle` frame — address,
**sub-address** seeding an auto-incrementing register pointer, then data
(`i2chle.cpp:108-200`) — and two slaves, because MAME merges them onto one
wired-AND SDA (`macquadra630.cpp:187-199`). The DFAC2 still gets nothing but
its ACK; the Valkyrie gets its bytes.

Two things recorded rather than smoothed over:

- **MAME's guard for the 512×384 monitor is a typo.** `valkyrie.cpp:566`
  reads `if ((m_M == 0) && (m_N == 0) && (m_P = 98))` — an *assignment*, so
  it fires on `M == N == 0` alone and clobbers `P` as a side effect. We
  implement its effect (`M == 0` is a divide by zero regardless) without
  corrupting a register, and the line says so. The house rule "on conflict
  the oracle wins" is about behaviour we cannot otherwise derive; it is not a
  licence to copy a bug that reading the code makes obvious.
- **The result is still 1.7 % high, and we did not "fix" it.** Apple's
  nominal 640×480 Mac mode is 66.67 Hz on a 30.24 MHz dot clock. A reference
  of `31.3344/8 = 3.9168 MHz` instead of MAME's `3986400` would give
  30.215 MHz → 66.70 Hz, essentially exact. That is a strong hint that MAME's
  constant is slightly wrong — but a hint is not a measurement, so the oracle
  value ships and the suspicion is written down. Anyone reopening this needs
  a real observable, not arithmetic that lands nicely.

Gate `valkyrie_i2c_test`: it replays the captured boot sequence byte for byte
and asserts on the **frame cadence** (CPU cycles per frame), not on
`pixelClock()` — the latter would only prove the setter compiles. It also
covers the `M = N = 0` guard and that writes outside registers 1-3 change
nothing.

**SWIM1's DAT1BYTE line was "not wired (the LC II polls the FIFO)" — true of
the LC II, false of every IOP machine.** `swim1.cpp:1226` asserts it while the
2-deep ISM FIFO has room (write mode) or holds data (read mode); it is what
lets an Apple PIC move a sector without its 65C02 polling for each byte. It is
now a level callback re-evaluated on every FIFO push/pop/clear, on the mode
registers `$6`/`$7` (the direction bit lives there) and at reset
(`swim1.cpp:109`).

The two consumers do **not** wire it the same way, and on this board that
distinction has already cost a day: the Quadra 900/950 feed **both** DMA
channels (`macquadra700.cpp:879-880`), the Mac IIfx only channel A
(`maciifx.cpp:486`). Per-machine wiring, not a shared rule — the same lesson
as § 5b of `IOP_BRINGUP.md`. The LC II leaves the callback unset and is
bit-for-bit unchanged.

Verified by re-running every boot etalon that owns a `Swim1` or a Valkyrie —
`lcii`, `iifx`, `q700`, `q900`, `q950`, `q630`, `lc580` — plus `swim1_test`
and `cuda_lle_test`.

<a id="2026-08-02-q900-finder"></a>
## 2026-08-02 (fourth) — Both Quadra towers boot the Finder: the IOP's BRK was a 68k word READ, not a 65C02 bug

`q700_boot_etalon q900` **PASSED** — 640×480, menu bar 0.00, desktop 0.82,
5830 SCSI commands, 5 min 05 s. The M7 wall recorded on 2026-08-01 is down,
and the thing that was wrong was in neither the IOP nor its firmware.

**The Quadra 950 came up on the same fix, unprompted**: `q950` PASSED at
33.333 MHz on its own `$3DC27823` ROM — and at 640×480×**8**, where the
Q900 lands at 1 bpp, so the Zydeco gate is also the tower's colour-DAFB
cell. Nothing Q950-specific needed touching; `via_in_a_q950`, the DAFB_Q950
flavour and the clock were already right from the platform work. Guard rail
re-run and green: the Quadra 700 still boots (5838 SCSI commands). Gates
`q900_boot_etalon` and `q950_boot_etalon` registered the same day — one
binary, machine selected by argv.

**What the 2026-08-01 entry had right, and what it had wrong.** It was right
that the SWIM IOP's firmware is byte-perfect (54 chunks / 11516 B), right that
the BRK at `$0042` is reached through the ordinary epilogue
`$53FC PLY; PLX; PLA; $53FF RTS`, and right on both negatives — *not* a stack
imbalance, *not* an interrupt storm. Its **next step was a wrong lead**: it
named the state table at `$4E87` and asked which SWIM/PIC register feeds it.
The state machine at `$5418` was running correctly the whole time; it was
murdered on the way out.

**The chain, measured.** `Q700Memory::read16` still applied the Spike's rule —
*the SWIM1 sits on the odd byte lane, so a word read there yields only its high
half* — to the `$5001E000` window. On the Eclipse that window is the SWIM IOP's
**host window**, and MAME installs its handler on **both** byte lanes
(`macquadra700.cpp:590-591`: the same range mapped twice, umask `$FF00FF00`
**and** `$00FF00FF`). So:

1. ROM `$408050CA` `move.w (a2),d3` re-reads the IOP's 16-bit shared-RAM
   address register (a2 = `$5001E001`) and gets `hi<<8` — `$0203` read as
   `$0200`.
2. `$408050D0-D6` `subq.w #1,d3; move.w d3,(a2); move.b d1,(a3)`: the ROM
   rewrites the byte it just read, the read having auto-incremented past it.
   With the truncated value it aims at **`$01FF`** instead of `$0202`.
3. `$01FF` is the top of the 65C02's stack (`$5000: ldx #$FF; txs`). The host
   wrote `$00` over the **high byte of the return address** the `jsr $53ED` at
   `$503F` had just pushed.
4. `$53FF RTS` therefore returned to `$0042` instead of `$5042`, hit a `$00` in
   page zero, and the firmware landed in its own panic handler.

**This is the FOURTH bug of the same class** — a Quadra 700 rule that must not
apply to the tower (`docs/IOP_BRINGUP.md` § 5b). It is worse than that: it is
the *unfixed half* of §5b bug #2. On 2026-08-01 `write16` got its `!eclipse()`
guard and `read16`, three dozen lines away, did not. **When a byte-lane quirk is
wrong on one direction of the bus, check the other direction the same hour.**

**Tooling that found it, kept in-tree.** The PC trail alone could not have; it
took three additions, each answering one question the previous one raised:

- `R65c02::spTrail()` — the SP alongside every PC in the ring. It showed
  `$53FC/FA $53FD/FB $53FE/FC $53FF/FD`: the depth was exact, so the *bytes*
  were wrong, not the discipline. That retired the stack hypothesis in one run
  instead of by argument.
- `ApplePic::watch` + `POM68K_Q900_IOPWATCH=<hex>` — an IOP RAM byte has three
  possible writers (the 65C02, the host window, the DMA engine) and the
  watchpoint names which one fired: `[IOP-WATCH] $01FF <- $00 by host`.
- A 64-deep ring of host-window touches (decoded 5-bit offset, the shared-RAM
  pointer *at that moment*, the 68k PC), dumped by the BRK trap. It printed the
  smoking gun verbatim: `W off=00 v=01` / `W off=01 v=FF` /
  `W off=04 v=00 ramAddr=$01FF pc=$408050D8`, i.e. the ROM setting the address
  to `$01FF` and firing.

The generalisable part: when an emulated CPU walks off a return address, the
question is not *where did it jump* but *who wrote that byte* — and in a
machine where three engines share one RAM, only a tagged watchpoint answers it.

**Registered the same day — profiles 35 and 36**, the house rule satisfied
(a `kProfiles` row is earned by a Finder cell, not by a memory map):

- `kProfiles` rows under a renamed group, "Discret 040 (Quadra 700/900/950)";
  `POM68K_Q700_MODEL` = `q700|q900|q950` selects inside `runQ700`. The
  `$3DC27823` dump **overrides the environment** — a Zydeco ROM *is* a
  Quadra 950 whatever the last launch left in the env — and asking for a
  Q950 with the Quadra 700 dump falls back to the 700 rather than pretending.
  Verified by launching the GUI on all four combinations.
- `SnapMachine::Quadra900 = 35, Quadra950 = 36`. The save/load pair is
  `Q700Memory`/`Q700Cpu`'s, unchanged; the tag is what separates a Spike
  snapshot from a tower one, since the Q900 shares the Spike's ROM and the
  header checksum therefore cannot.
- The PRAM file now carries the **profile**, not the family
  (`.q700`/`.q900`/`.q950.pram`, and the save-state path derives from it):
  on the Spike that store is the discrete RTC's XPRAM, on the towers it is
  the Egret's. One file serving both would have mixed two different devices'
  batteries. Same reason `hostMacSeconds()` now goes to `egret()` on the
  Eclipse — there is no discrete RTC in that loop at all.
- `savestate_040_test` gained a **`Q900Rig`**. This is the point worth
  keeping: the `eclipse()` branch of `Q700Memory::visit` serializes two
  `ApplePic` (each with 32 KB of host-uploaded 65C02 firmware plus its CPU,
  timer and DMA phase), `AdbLine`, the `Egret` and the second 53C96 — a tail
  **no existing rig reached**. A dropped chunk in it would have shipped
  invisibly. 11/11 green, including load→save byte-identity and
  determinism across a restore.

<a id="2026-08-02-eclock-asc"></a>
## 2026-08-02 (third) — Two rates that were rounded, and one that was only ever a price

Both are § 1 items of `LLE_VS_HLE.md`, both were approximations nobody had
measured, and both turned out to be closable without inventing anything.

**The VIA E clock was a rounded divisor on four boards.** A 6522's φ2 on a Mac
is the board's **783.36 kHz** E clock (C7M ÷ 10), generated independently of
the CPU. Where the CPU clock is an integer multiple, dividing it is exact and
POM68K was right all along — Plus ÷ 10, Mac II and IIfx ÷ 20 — and the
V8/Sonora/RBV/VASP classes already carried a fractional accumulator. Where it
is not, the rounding was a real error: **25 MHz ÷ 32 = 781.25 kHz, 0.27 %
slow** on the Q605, Q700 and Centris; **33 MHz ÷ 42 = 785.7 kHz** on the
Q630.

The part that made this more than a one-line fix: the same divisor is also a
**phase grid**. `viaSync()` stalls the CPU until the middle of the next
E-clock cycle so a VIA access lands on an edge — and mis-scaling that grid is
exactly what wedged the IIsi and blacked out the LC III on 2026-07-25. Rate
and grid must not drift apart, so both now use exact rational arithmetic and
both live in `src/ViaEClock.h`, which exists so the formula is written **once**.
`viaDiv()` and `kViaDiv` were left orphaned by the change and deleted.

Verified where it could plausibly break — the Cuda↔VIA bit-bang transport is
documented as phase-fragile: `q605_`, `centris650_`, `quadra800_`, `q700_` and
`q630_boot_etalon` all reach the Finder, `ctest -L unit` 64/64.

**The ASC drain ignored the register that programs it.** `$807` CLOCK RATE —
0 = the Mac's 22 257 Hz, 2 = 22 050, 3 = 44 100 — was stored and never read;
the drain was pinned at 22 257. MAME *documents* the register (`asc.cpp:30`)
and does not implement it either, so for once the manual is the reference and
there is no oracle to defer to. Code 1 is undefined: it keeps the Mac rate
rather than inventing one.

Worth stating why this is free on every machine that boots today, because it
is a property of the hardware and not luck: **only the classic (Mac II
discrete) ASC accepts a write to `$807`**. On the V8, Sonora and IOSB
integrations it is read-only and reads back 0 — which the gate asserts next to
the rates, so "no change today" is a tested claim rather than an assumption.

*Deliberate caveat*: the output ring feeds a fixed-rate host DAC, so a guest
that really programmed 44.1 kHz would get its FIFO interrupts at the right
cadence while the emulator paced to half speed. Resampling is out of scope.


**And the third item, which needed a number rather than code.** § 1.2's
peripheral-tick batching entry closed with "drop `kPeriphBatch` toward 1 and
re-measure the boot etalons for the cost". Done, on the 040 where the batch is
largest (256 cycles ≈ 10 µs of IRQ-latency jitter at 25 MHz):
`POM68K_PERIPH_BATCH` overrides it, and `q605_boot_etalon` reaches the Finder
at **every** setting including exact.

| batch | wall | vs default | max IRQ jitter |
|---|---|---|---|
| 256 (default) | 61.3 s | — | ≤ 10.2 µs |
| 64 | 67.3 s | +10 % | ≤ 2.6 µs |
| 16 | 79.3 s | +29 % | ≤ 0.64 µs |
| **1 (exact)** | 107.9 s | **+76 %** | 0 |

That reframes the item honestly: it was never a missing implementation, it is
a **priced trade with the exact setting available**. Conformance here costs
76 % and is one env var away. Moving the *default* is a separate product
decision and deserves its own measured reason — this entry deliberately does
not make it.

**The gate measures the observable, not the accessor.** `asc_test` times how
many CPU cycles it takes to drain a fixed number of FIFO samples at each
setting; asserting on `drainHz()` would only have proved the switch statement
compiles. Checked to bite: pinning the rate back fails both rate assertions
and neither of the two controls.

<a id="2026-08-02-beam"></a>
## 2026-08-02 (later) — The raster beam: nine video decoders stop painting the whole frame at once

`LLE_VS_HLE.md` § 5 ranked whole-frame video as the **number one** remaining
LLE gap — "the one gap that makes a whole class of software *wrong* rather
than approximate". This is the first half of closing it.

**What was actually wrong, and it was looser than the doc said.** The doc
described a "whole-frame decode at frame end". The code was worse: the
machine loops called `video.decode(fb_)` from `publish()`, i.e. at an
arbitrary moment ~60 times a second, reading whatever VRAM and whatever
registers happened to be live right then. Two guest-visible consequences:

- a **mid-frame register change repainted every line**, so a raster split —
  different palette above and below a scanline, the classic Mac trick for
  more than 256 colours on screen — showed only the last state, everywhere;
- **VRAM written after the beam had passed a row still appeared on that
  row**: the emulator was more up to date than the glass.

**The design decision that matters: the beam owns no clock.** Every platform
already accumulates CPU cycles into the current frame to generate its VBL —
`framePos_` in `V8Memory`, `SonoraMemory`, `VaspMemory`, `RbvMemory`,
`MacIIMemory`, `TobyVideo`, `Dafb`, `Valkyrie`. `VideoBeam::setPos()` adopts
that accumulator instead of running a second one. So there is exactly one
source of frame time, and the VBL edges — which are load-bearing, and behind
more than one entry in this file — are untouched by anything in the raster
path. Adding a parallel clock would have been the easy version and the wrong
one.

Decoders gained `decodeRows(out, y0, y1)` and a `raster(out)` that renders
each visible row **once, when the beam scans it**. The cost is not higher:
it is the same rows, decoded once each per frame, merely placed correctly.
Converted: `V8Video`, `SonoraVideo`, `VaspVideo`, `RbvVideo`, `TobyVideo`
(its own CRTC clock) and `Se30Video` (no CRTC of its own — it rides
`MacIIMemory`'s 60 Hz accumulator, passed in rather than duplicated).

**Wiring it without touching CPU timing.** `runQuantumWithWire` *already*
slices the frame into 16 or 64 pieces whenever the wire is active — and
AppleTalk is on by default in the GUI. The catch-up hooks that existing
slicing, so the default path gets 16-64 raster points per frame (real
intra-frame fidelity) while the no-wire path keeps its quantum in a single
`runCycles` and simply gets one whole-frame repaint per frame, exactly as
before. No new slicing, no new timing risk.

**Two bugs the gates found that review would not have.**

1. A row is emitted once it has been **scanned**, not when the beam enters
   it. My first assertion expected the latter; the test was wrong, the model
   was right. Worth recording because the off-by-one is the whole semantics.
2. The serious one: **a caller sampling once per frame at a fixed phase
   cannot be served by the position alone.** `framePos_` is modulo, so a
   whole elapsed frame is indistinguishable from no time at all — the row
   cursor would never advance and the screen would never update again. The
   fix is `frameCount_` on every memory class (real machine state, so
   serialized), and `VideoBeam` treats either a position decrease *or* a
   sequence change as a wrap. The `v8_raster_test` "one raster() per frame
   still repaints the whole frame" check is what caught it — a check added
   because the machine loops *are* such a caller.

**Two more found by re-reading the diff, not by any gate** — both invisible
to a boot etalon, which is the point:

- **The "CRTC not programmed yet" fallback ran a whole-frame decode on every
  wire slice.** Caught while writing `DafbMachine`, fixed there — and then
  *not propagated* to `SonoraVideo` and `RbvVideo`. Sonora is the bad case:
  `frameCycles_` is 0 from reset until the ROM programs the video controller,
  so for the whole POST that was up to 64 full 640×480 decodes per frame. The
  `full` flag now marks the once-per-publish call and the fallback answers
  only there. (`V8Video`/`VaspVideo` cannot reach it — their frame length is
  set at reset — and carry the parameter only so every decoder presents the
  same signature to the machine loops.)
- **`DafbMachine::decode()` became dead code** the moment `publish()` started
  going through the raster surface. Deleted with a pointer to the equivalent
  (`newFrameGeom(); rasterBeam(true);`) rather than left to rot — this
  project has already had to delete code that survived months unreferenced
  (`RsrcPatcher`, 2026-07-21).

**Gates.** `video_beam_test` pins the row schedule itself: every visible row
emitted exactly once per frame, in scan order, no gap, the frame tail flushed
on the wrap, and — the case that lets a 640×480 decode ride the V8's 407-line
modeline — the row schedule riding the ACTIVE window while `line()` tracks
the total. `v8_raster_test` pins the behaviour: a palette change at line 192
produces **exactly one seam, within a line or two of 192**, and a row already
scanned keeps the content it was scanned with. It also asserts the contrast
explicitly — `decode()` still shows only the latest state on every line — so
the difference is visible in the suite rather than argued in prose.

**The third gate, and the thing it taught.** `raster_equiv_test` pins the
row-range invariant — decoding a frame in ragged chunks (1, 7, 13, 64 …) must
be bit-identical to one pass, on four decoders at every depth — because the
conversion moved each row's source offset from an implicit `*ptr++` walk to
explicit arithmetic, and a boot etalon cannot see that: the Finder looks fine
while one row in eight comes from the wrong place at a depth nobody boots in.

**It was green three times before it tested anything**, each time for a
different reason, and only a deliberate reintroduced bug exposed it:

1. it imposed a resolution the machines do not reset to, so `equivalent()`
   failed on the size and never reached the pixels;
2. the VASP fill wrote to `$F9000000` (a Q605 address — VASP's VRAM is at
   `$60000000`), and the RBV CLUT writes went to `+$01` when that Bt478
   decodes only `(low & 3) == 0` with the register in bits 3-2, i.e. `+$04`;
3. worst, the RBV framebuffer **is system RAM**, and every write was dropped
   because the boot overlay only lifts on a read in the ROM range
   (`$40000000`) — a low read just returns the mirrored ROM.

Three uniformly black frames compared against three uniformly black frames,
reported as PASSED. The fix is in the harness, not in the cases:
`equivalent()` now **refuses a vacuous comparison** — a frame that decodes to
a single colour is not a pass, it is an absent test — and only the
blanked-by-design Sonora-at-reset case opts out, explicitly.

And the limit is written at the top of the file rather than discovered later:
since `decode()` *is* `decodeRows(0, vres)`, both sides share the pitch
arithmetic, so **a pitch that is wrong but consistent passes**. What the
invariant catches is the class the conversion actually introduces — an offset
that depends on where the chunk started. Pinning the pitch needs an
independent reference, which is why the RBV block asserts that an undefined
config renders *identically to config 3*. Verified by putting the bug back:
only that assertion fails, exactly as documented.

**All nine decoders landed, including the three that looked hardest.**
`Dafb` covers twelve profiles through one `DafbMachine` template (Q605,
LC 475/575, Centris 610/650, Quadra 610/650/800/700) plus `Valkyrie`
(Q630/LC 580) through the same struct; there the geometry is resolved **once
per frame** rather than per row, because walking the guest's GDevice → PixMap
through `peek8` costs more than the pixels — and it is also more correct, a
real CRTC latching its parameters for the frame. `MacVideo` (Plus/compacts)
was the machine where the beam was *already modelled*: the VIA PB6 "beam in
display portion" bit reads `cpu.getClock() % 130240`, so the decoder now
reuses that exact position instead of inventing a second one. Its frame loop
is sliced 16× over the display period, which is safe by construction —
`runUntil(t)` is "execute while clock < t", so a chain of increasing targets
ending at the same value runs the same instructions, the last target *is*
`frameBase + kVblankStart` (the vblank edge cannot move), and the
cycle-exact contention model reads the absolute clock, not a slice-relative
one.

**What is genuinely not done**: **no beam-position register is exposed to a
guest**. `VideoBeam::line()` is gated but nothing reads it from the guest
side — the registers that would (DAFB's line counter, Valkyrie's blanking
bit, `Valkyrie.cpp:51`) still answer from their own private arithmetic. Two
scan positions per machine that can disagree is exactly what should be
removed — but it only buys something if a guest consumer exists, which has
not been checked. That is the second half of § 1.1.

<a id="2026-08-02-lle-devices"></a>
## 2026-08-02 — The ADB device model and two SCC pins: closing the LLE gaps that needed no new hardware

`docs/LLE_VS_HLE.md` § 4 has carried a short list of *device-model* gaps —
things POM68K's peripherals do not have, as opposed to timing it does not
model. Four of them needed no new subsystem, only the right oracle. They are
closed; what is left on those two bullets is now deliberately deferred, each
with the reason written down next to it.

**The oracle question came first, and the answer was not MAME.** For all
four, MAME models nothing: `macadb.cpp` hardcodes the ADB handler ID to 1
(`:628,705`), has no keyboard LED register and no second mouse button, and
`z80scc.cpp` never loads a residue code (`RR1_RESIDUE_CODE_MASK` is a
constant it declares and never uses). **DingusPPC** is the design oracle here
— `adbkeyboard.cpp` / `adbmouse.cpp` implement the register semantics at our
abstraction — with the Zilog manual and MAME's `update_rts:1184-1206` for the
SCC pins. That is the same hierarchy the Egret/Cuda work settled on
(§ *Reference hierarchy*), applied to a different device.

**ADB — the handler ID was a constant pretending to be a register.**
`AdbLine` returned `$01` as R3's second byte no matter what, so a Listen R3
carrying a handler number did nothing at all. The ID is now its own field
(distinct from the R3 *flags* byte, which is what `kbdHandler_`/
`mouseHandler_` have always held — a conflation that already cost a round on
2026-07-31). The keyboard takes 1 / 2 / 3 and **a standard keyboard refuses
3**: that refusal is not an omission, it is the probe a driver uses to tell
an Extended Keyboard II from a standard one, so implementing it as "accept
everything" would have been a subtler bug than the constant. Under handler 3
the right-hand modifiers keep their own codes (`$7B`/`$7C`/`$7D`); the R2
bitmap still has one bit per modifier, so right Shift lights the Shift bit
under every handler.

**The second mouse button rides bit 7 of the second report byte** — the bit a
one-button Apple mouse holds at a constant 1 — and only under the Extended
Mouse Protocol (Listen R3 activator 4), which also brings register 1's 8-byte
identifier block (`'appl'`, 300 dpi, class 1, 2 buttons). One thing worth
pinning: a button-2 change counts as *pending* **only** under handler 4.
Otherwise a right click on a one-button mouse leaves a change the device can
never report, and every autopoll answers with an empty report forever — the
same shape of bug as the 2026-07-23 mouse starvation, found by reasoning
about it rather than by watching a machine stall.

**Listen R2 latches the keyboard LEDs** (bits 2-0, active low), read back in
Talk R2's second byte and exposed as `keyboardLeds()`. An untouched R2 still
reads `$FF`, so nothing on any existing path moved.

**And SendReset now restores the default handler, not just the default
address.** MAME resets only the addresses (`:742`) — harmless while a handler
is a constant, wrong the moment it is switchable.

**SCC — `/RTS` is not a view of WR5 bit 1.** Setting the bit asserts the pin
at once, but *clearing* it releases the pin immediately only without Auto
Enables; with WR3 bit 5 the chip holds the line until the transmitter is
completely empty, so the last character is not truncated by the line driver
going away. That deferral means the release cannot live at the register
write: it happens in `tick()` as the shifter drains, where MAME puts it
(`tra_complete:1090`). `/DTR` came along for free — it follows WR5 bit 7
unless WR14 bit 2 has handed the pin to the DMA request function. Both are
readable as **asserted** rather than as pin levels, so no caller has to
remember that the package pins are active low, and both travel in the save
state.

**SCC — the SDLC residue code was internally inconsistent, which is what
made it worth fixing.** RR1 bits 3-1 say how much of the I-field's last
character is valid. A byte-granular wire only ever produces the byte-aligned
code `011` — which is also the chip's reset value, and precisely why
`rr1Rd` has always been initialised to `$07`. But received frame bytes were
reporting `000`, a code that means a *partial* character on real silicon. So
the idle register and a live frame byte disagreed with each other. Both read
`011` now.

**And the ADB report rate is measured, not suspected.** The § 4 bullet asking
whether POM68K services *fewer* autopolls than real hardware is answered: it
does not. A full LC III run traced end to end (199 s emulated, 17 850 ADB
commands) gives an aggregate autopoll interval of **11.18 ms with p10 = p90 =
median** — dead steady — against the Egret's nominal 11.1 ms. **89.5 Hz vs
90**, a 0.6 % deficit, and exact by construction because the cadence is the
firmware's own timer, not ours. The mouse is polled at ~67 Hz *while it has
data* (SRQ-driven bursts, the keyboard holding the poll the rest of the time),
so more than one report lands per 60 Hz frame. The ~1.6× amplification of a
sustained drag is therefore System 7's mouse scaling on coalesced deltas, as
that entry suspected — the poll rate is off the suspect list, and re-opening
it needs a new observable.

**Gates.** `adbline_test` gained three blocks (extended mouse protocol +
handler IDs + LEDs), `scc_engine_test` two (residue codes, RTS/DTR). Both
green, and the ADB block was checked for the failure mode this project keeps
catching itself in — a *false green* from an assertion that cannot fail:
forcing `POM68K_ADB_KBD_ID=2` fails exactly the two handler-1 assertions and
nothing else, so the checks bite. Verification run: `ctest -L unit` **61/61**,
plus the twelve etalons these two devices are most exposed to — `input`,
`macii_mouse`, `iifx_input`, `iisi_input`, `lc3_input`, `lc520_input`,
`iivx_input`, `q605_cudalle_boot/mouse/key`, `q605_ot_bind`,
`llap_two_system` — **12/12 green**.

**Deliberately not done, with reasons.** The GUI never *sends* what the
device can now report — `main.cpp` reads `io.MouseDown[0]` and maps
`ImGuiKey_RightShift` onto the left code — so both new ADB capabilities are
reachable but unexercised by the front end; wiring them is a host-side change
across twelve call sites, listed in `TODO.md` § 4. Nothing reads
`rtsAsserted()` either: the pin is modelled correctly and has no consumer
until a real async transport exists, which is the same reason SCC bit-serial
sampling and the DPLL stay open.

<a id="2026-08-01-q900"></a>
## 2026-08-01 (M7) — Quadra 900: the Eclipse platform lands and both IOPs run — the wall is a BRK inside byte-perfect firmware

IOP milestone 7, **honestly incomplete**: the Quadra 900/950 platform is in
and deeply exercised, but neither reaches the Finder yet, so **no profile
was registered** — the house rule is that a `kProfiles` row is earned by a
Finder cell, not by a memory map.

The towers are the Quadra 700 board with the IIfx's front end grafted on,
so they live in `Q700Memory` behind `Model {Spike, Q900, Q950}` rather
than in a platform of their own: two `ApplePic` IOPs replacing the direct
SCC and SWIM decode (host windows at +$0C000 / +$1E000), `AdbLine` on the
SWIM IOP's GPIO, an `Egret` on VIA1 CB1/CB2 replacing the discrete RTC, a
second 53C96 bus, the `$D0`/`$90` VIA1 PA identities, no DFAC on VIA2 port
B, and 33.333 MHz for the Q950. `q700_boot_etalon <q700|q900|q950>` picks
the machine.

**What works, measured**: the Q900 ROM POSTs, paints 640×480 DAFB video,
uploads both IOP firmwares and releases them, and the SCC IOP runs its
real firmware (2484 B) through the ROM's bypass-mode walking test on the
SCC's WR2/RR2.

**Three bugs found, and they were all the same bug**: a Quadra 700 rule
silently inherited by a board that does not have it (see
`docs/IOP_BRINGUP.md` § 5b for the full list and the debugging order).
The two that cost real time:

- **`tick()` re-derived the SCC interrupt line from the SCC chip.** On the
  Eclipse that line is the SCC IOP's *host* interrupt; the chip's `/INT`
  is a *peripheral* interrupt into the IOP. The inherited line wiped the
  IOP's interrupt the instant it was raised, so the boot waited forever on
  an `_IOPMsgRequest` ($A087) that had already been answered.
- **The SWIM odd-byte-lane word-write quirk.** The Q700's SWIM1 takes only
  the low half of a word write. On the Eclipse that window is the SWIM
  IOP's host window, whose 16-bit RAM-address register the ROM writes with
  a single `move.w d0,(a2)` (a2 = `$5001E001`, ROM `$40804D38`). The quirk
  dropped the address's high byte, every upload landed in the low 256
  bytes, the ROM's 32 KB IOP RAM test failed, and the IOP was never
  released from `/RSTPIC`.

**The wall, characterised so the next session starts here.** The SWIM IOP
now runs, and its firmware is **byte-perfect**: the ROM's upload script at
`$5A7EB` is `[len][addr:2][data…]`, and all **54 chunks / 11516 bytes**
compare equal against the live IOP RAM. Despite that, the 65C02 ends in
the firmware's own **BRK panic handler** (`$5060`, which tests the pushed
B flag and then hangs walking the stack at `$5069`). So it executed a
`$00`: a control-flow divergence inside POM68K's device model, not a
corrupt upload. The host meanwhile sits at ROM `$4080A8E6` with VIA2
`IER=$01` — it has armed the SWIM IOP's host interrupt (VIA2 CA2) and
waits for a reply that never comes.

**Traced to the instruction**, with tooling that stays in the tree:
`R65c02::setTrace()/onBrk/pcTrail()` keeps a 256-deep ring of executed PCs
and dumps it on the first `$00` (`POM68K_Q900_IOPBRK=1`). The firmware
reaches the BRK at **`$0042`** through an ordinary epilogue — `$53FC PLY;
PLX; PLA; $53FF RTS` — whose `RTS` read a garbage return address
(`A=41 X=FF Y=59 SP=FF P=30`). That measurement **kills two hypotheses**,
and both are worth not re-opening: it is **not a stack-discipline
underflow** (SP was `$FA` entering the epilogue and `$FF` after its five
pulls — exactly right; the bad address was simply what sat at
`$01FE/$01FF`, so the routine returned one frame past its own top level),
and it is **not an interrupt storm** (no `$504E`, the firmware's IRQ
vector, anywhere in the last 256 executed PCs). What it was doing instead
is spinning in the state machine at `$5418`-`$5436`, whose inner routine
compares `$4E87,x` against `$4E87,y` — a table at `$4E87`-`$4E9F` that
never reaches its end condition.

Next suspects, refined by that trace: the SWIM device-register mapping
behind `readPeriph`/`writePeriph`; the `ApplePic` timer's reload
semantics; and `dat1byte` → `reqa_w`/`reqb_w`, which MAME wires to **both**
DMA channels here (`macquadra700.cpp:879-880`) — note POM68K's `Swim1` has
no `dat1byte` callback at all, so that one is a real extension of the
floppy controller, not a wiring line.

**The Quadra 700 is verified non-regressed** (`q700_boot_etalon` 531 s and
`jit_q700_boot_etalon`, both green after the shared-code surgery) — the
check that mattered, since `Q700Memory` now serves three machines.

Method note worth keeping: the cheap-to-expensive ladder that localised
this — IOP held/released → its cycle counter moving → how many firmware
bytes it holds → VIA IFR/IER (who is armed, who never fires) → the IOP's
own flags/mask → disassemble the IOP RAM at its stuck PC — is now written
down in the blueprint, because every step of it paid.

<a id="2026-08-01-iifx-profile"></a>
## 2026-08-01 (late night) — The IIfx is the 34th profile: GUI, save states, and an input gate whose thresholds were measured, not invented

IOP milestone 6. The IIfx stops being a test rig and becomes a machine:
`MachineKind::IIfx` + a `kProfiles` row under a new **"OSS + IOP (IIfx)"**
group, `SnapMachine::IIfx = 34` with its save/load pair, an `IIfxMachine`
GUI loop (the `MacIiMachine` contract — queued input, decoded framebuffer,
relaxed-atomic status), and ROM dispatch on the 512 KB `$4147DD77` — the
only 512 KB ROM in the tree that is neither RBV nor V8. **34 profiles, 20
`MachineKind` values, 11 platform implementations, 136 gates.**
*(Correction, 2026-08-01 doc pass: **12** platform implementations. The count
above forgot the MSC + PG&E board the Duo 230 booted on the day before — it
has no `kProfiles` row, which is exactly why it was easy to miss. The 34 / 20
/ 136 figures are right.)*

Two things specific to this machine were worth the extra wiring:

- **The CPU window shows both IOP cycle counters.** The IIfx has three
  processors; "is the SWIM IOP still executing?" is the first question
  every IIfx bug asks (no ADB, no floppy), and it is now one glance.
- **Save states carry the IOP RAM.** Each `ApplePic` chunk nests an
  `R65c02` plus its 32 KB — which IS the firmware, host-uploaded with no
  ROM to reload. A restore that dropped it would resurrect a machine whose
  I/O processors have no program. Covered in `savestate_030_test`
  (byte-identical re-save + 150 k-cycle determinism across a restore, both
  green first run; the rig's stub sits at ROM+$2A because the IIfx wrapper
  hardcodes the Basilisk-style reset frame).

**`iifx_input_etalon`** proves ADB *delivery* through the IOP firmware, and
its first run is the entry's real lesson. Two observables, both held to the
TODO §1 standard — an observable is believed only after demonstrating
sensitivity **and** silence without stimulus:

- **Mouse → pixels.** Idle 0 px, after motion 43 px. My first threshold
  (`moved > 200`) was **invented and failed the gate on a working
  machine** — the RBV family's measured figure is 46 px, the same class.
  Fixed to `family_input_etalon`'s `moved > idle + 20`. Inventing a
  threshold is the same error as trusting an unmeasured observable.
- **Keyboard → KeyMap ($0174, exactly 8 bytes** — never wider, see the
  2026-07-29 false green). The 030's PMMU is **on** here (TC=$80F05750),
  which on an RBV machine would make the read meaningless. It is sound on
  the IIfx for a structural reason — no built-in video, so physical low
  RAM is ordinary RAM — and, more to the point, the measured signature is
  **0 → 1 → 0 bits** across press/hold/release. The gate asserts it, with
  the reasoning recorded at the assertion rather than a silent skip.

<a id="2026-08-01-iifx-finder"></a>
## 2026-08-01 (night) — The Mac IIfx boots the Finder — ADB bit-banged by the IOP's own 65C02 firmware against AdbLine

Gate `iifx_boot_etalon` (**135 gates**): the IIfx ROM + System 7.6 FR to
a verified Finder — menu bar 0.06 black, desktop 0.83, 6648 SCSI
commands, screenshot eyeballed before any threshold moved (menu bar,
clock, Corbeille, the volume mounted; seven copies of it, in fact — the
multi-ID mirror workaround inherited from `MacIIMemory::attachScsi` is
guest-visible on 7.6, kept for scan-speed parity and noted in the
etalon). Finder at frame 1559 (~26 guest-seconds at 40 MHz).

The ADB chain that makes this the milestone it is: **no HLE anywhere on
the wire.** The System's ADBReInit sends IOP-mailbox commands to the SWIM
PIC; the PIC's real firmware (uploaded by the ROM, M3) bit-bangs the ADB
line through its GPIO pair (gpout0 inverted, `maciifx.cpp:483`); and
`AdbLine`'s LLE keyboard+mouse decode the wire traffic and answer.
Measured on the wire: 8-bit commands in ~70 µs cells, the full Talk-R3
enumeration sweep ($0F→$FF), devices replying (dsz=2 sends), then the
Listen-R3 address shuffle — textbook ADB, served end to end by two
emulated processors talking over one emulated wire.

The hunt, for the record: after M3 the boot stalled spinning on **bit 5
of $15D(A3)** at ROM $4080A8EC — the same ADB-driver soft-flag byte the
Mac II family uses, here waiting for an IOP command completion. Wiring
`AdbLine` to the SWIM PIC's GPIO (12 lines in `IIfxMemory`) dissolved it:
the polarity guesses held on first try (MAME's `macadb` "push model echo"
comment confirms state 1 = line high on both callbacks). Two lesser
traps: a stale `iifx_trace` binary measured one whole run of the OLD core
after the ODR fix (`pom68k-no-edits-during-bg-build`, paid again), and
the etalon's frame constant `40000000 * 100` overflowed int and froze the
CPU at the reset vector for 18 000 silent frames — `LL` suffix, and the
diag line that exposed it stays in the gate.

Remaining before the IIfx is a *profile*: `kProfiles` + `SnapMachine` +
save-state wiring + the GUI machine loop (M6), then the Quadra 900/950
(M7). The SCSIDMA's real DMA + restartable handshake (A/UX-only per
`scsidma.cpp:12`) stays deferred as M4 with a LOUD gap note in
`docs/IOP_BRINGUP.md`.

<a id="2026-08-01-iifx-post"></a>
## 2026-08-01 (evening) — IOP M3: the IIfx POSTs — both IOP firmwares upload byte-perfect, and the boot scan reads the disk

Platform #12 exists: `src/IIfxMemory.*` (the map, the OSS, the SCSIDMA
subset, the clock plumbing) + `src/IIfxCpu.*` (Moira 030 @ 40 MHz, the
Cpu020 wrapper pattern with the same reset-vector frame — the IIfx ROM's
header is checksum-then-entry `$4080002A` like the Mac II's). Gate
`iifx_post_etalon` (**134 gates**): on the real `4147DD77` ROM,

- **both IOP firmwares upload and run.** The ROM downloads the SCC PIC's
  65C02 image (reset vector $040E — found byte-perfect at ROM+$5F471) and
  the SWIM PIC's (vector $5000 — ROM+$5A7EE), releases /RSTPIC on each,
  and the two `ApplePic` instances execute them for millions of cycles.
  The Duo BORG-upload verification pattern, paid off a third time.
- **the OSS priority file gets programmed** exactly as the blueprint
  guessed from MAME: NuBus inputs at level 2, SCC IOP at 4, tick + VIA1
  at 1 — and the POST's VIA T1/T2 interrupt test passes through the OSS.
- **the SCSI boot scan reads the System**: textbook 5380 arbitration →
  selection 6→0 through the SCSIDMA's register window, 323 commands in
  the gate's 4.5 guest-seconds.

Two findings worth their line:

- **The SCSIDMA handshake-data window swallowed the ODR.** The ROM puts
  the target ID on the bus by writing SCSIDMA+$00 — which is BOTH the
  handshake data port and 5380 reg 0. MAME's `handshake_w` falls through
  to `ncr->write(0, …)` when no DRQ is active (`scsidma.cpp:300-310`);
  the first POM68K decode returned early instead, so arbitration ran,
  selection fired, and no target ever heard its ID. Symptom: an eternal
  wait-for-BSY at ROM $40807860 with `selects` climbing and `commands`
  stuck at 0. Fixed for reads too (CSD, `scsidma.cpp:262-269`).
- **The M2 WAI/STP question is closed.** With the firmware located, a
  65C02 disassembly sweep over both blobs (capstone MOS65XX, ~8 K
  instructions) finds **zero** WAI/STP — every $CB/$DB byte is operand
  data. The vendored core's WDC-halt behaviour is unreachable on this
  firmware; no Rockwell-NOP personality flag needed.

Not yet claimed: the Finder. ADB is still a stubbed line (the SWIM PIC's
GPIO floats high), so the boot's ADBReInit has no transceiver to talk to
— that is milestone 5 by design (`docs/IOP_BRINGUP.md` §5), with the
long-run behaviour past the System load still to be measured.

<a id="2026-08-01-applepic"></a>
## 2026-08-01 (later) — IOP M2: the Apple PIC device lands — a window-uploaded 65C02 program talks both mailbox directions

`src/ApplePic.*` — the 343S1021 modelled from MAME `applepic.cpp`, register
for register: the 32-byte host window (offset-bit decode; the shared-RAM
data port goes through the full internal 65C02 space, registers included),
auto-increment, /RSTPIC hold-and-release (the release edge runs the
65C02's reset sequence through the vectors the host just uploaded), the
timer (one-shot `latch*8+12` clocks, continuous `(latch+2)*8`), the
two-channel DMA engine (1 byte/channel per 8 input clocks, DREQ wires,
`DENxONx` alternating-buffer chaining, completion IRQs), the 6502-side
interrupt unit (masked flag reads — MAME's own warning about firmware
confusion), the GPIO pair (gpout0 = the future ADB out on the IIfx's SWIM
PIC) and the host-bypass mode (the boot path talks to the SCC straight
through the idle PIC).

The one structural departure from MAME is the clock plumbing: no attotime
schedulers — `tick(clocks)` counts input clocks slaved to the 65C02's own
instruction stream with debt carry, the `pom68k-mcu-lle-clock-drift`
pattern, and the continuous timer re-arms **from the scheduled expiry**,
not from "now", so instruction-granular firing cannot accumulate cadence
drift. Save states carry the RAM (the firmware is host-uploaded — there
is no ROM to reload) and the clock counters (IOP↔host phase is
load-bearing; the Cuda↔VIA lesson).

Gate `applepic_test` (unit, green first run): a hand-assembled 65C02
program is uploaded through the window — reset hold, upload/readback with
auto-increment on and off, INTHST0 out to the host line + ack, host
INTPIC in to the program's ISR, the timer one-shot then 100 measured
continuous periods, DMA in both directions with completion flags and
DMAEN auto-clear, and bypass reads reaching the fake peripheral. **133
gates.** Next: M3, `IIfxMemory` + OSS — where the real ROM's firmware
upload replaces the hand-assembled one.

<a id="2026-08-01-r65c02"></a>
## 2026-08-01 — The IIfx/Quadra-900 IOP brick opens: the R65C02 core lands, and it needed no dump

The Apple PIC IOP + OSS chantier (`TODO.md` §7, the one brick that unlocks
the **Mac IIfx** and the **Quadra 900/950**) opened today with a recon pass
and milestone 1. Blueprint: `docs/IOP_BRINGUP.md`. Three findings moved the
cost of the whole brick down:

- **The IOP firmware needs no dump.** MAME's `applepic.cpp:63-77` maps the
  PIC's entire 64 KB internal space as RAM + registers — the host ROM
  *downloads* the 65C02 firmware through the shared-RAM window at boot,
  exactly the Duo BORG pattern. Unlike Egret/Cuda there is no
  non-distributable MCU ROM; the IIfx system ROM already in `roms/`
  (`4147DD77`) is the firmware source.
- **The Q900/950 keep the Egret** (`macquadra700.cpp:190-216`) — their two
  IOPs carry only SCC and SWIM. The towers are the Q700 platform POM68K
  already ships plus `ApplePic` and map deltas; the new ADB work (IOP
  GPIO bit-bang ↔ `AdbLine`) is IIfx-only.
- **The right 65C02 to vendor was POM2's, not POMIIGS's.**
  `68K_FAMILY_SCOPE.md` §3 designated POMIIGS `CPU65816` (emulation mode);
  recon showed MAME's PIC core is an **R65C02** with the Rockwell bit ops
  `RMB/SMB/BBR/BBS` — which 65816 emulation mode does *not* have (those
  columns are `ORA [dp]` etc. on a 65816). POM2's `M6502` has a complete
  CMOS mode *including* the Rockwell set, already validated against MAME
  `ow65c02.lst` and Tom Harte `wdc65c02`.

**Milestone 1 landed**: `src/R65c02.*` vendored from POM2 `M6502` with two
reductions (CMOS-only — the NMOS runtime mode and its opcode remap are
POM68K-dead-weight; bus by `read8`/`write8` callbacks, the `M68hc05`
pattern) and the Apple II diagnostics stripped — those held process-global
trace state that the IIfx's *two* PIC instances would have raced on. Gate
`r65c02_test` (label `unit`): Klaus Dormann's 6502 functional image to its
$3469 success trap in **96 561 327 cycles** (Klaus documents ~96 M — the
cycle accounting agrees) and the 65C02 extended-opcodes image (full
Rockwell set, WAI/STP, CMOS decimal flags) to $24F1. The images are
committed under `tests/assets/` — GPL test code, not ROMs.

One decision deliberately deferred to M2: POM2's WDC halts WAI/STP
($CB/$DB) are kept, but MAME's `r65c02` decodes those as NOPs — scan the
uploaded IOP firmware for $CB/$DB before choosing the PIC personality
(`docs/IOP_BRINGUP.md` §6).

<a id="2026-07-31-duo-gate"></a>
## 2026-07-31 (late night, later) — `duo230_boot_etalon` GREEN: milestone 3 gated, the GSC decoder lands

The Duo 230's Finder boot (previous entry but one) is now a registered
CTest gate — the house rule ("each milestone gated before the next
depends on it") is paid before input/sleep work starts. New:
`MscMemory::decodeScreen()` (the milestone-3 GSC decoder — 1/2/4 bpp per
MAME `gsc.cpp`, the P5 gray ramp the trace's PGM dump used) and
`tests/duo230_boot_etalon.cpp` (PG&E release wait → 12 000 frames →
menu-bar/desktop probe on the decoded 640×400 panel). Measured: menu bar
0.04 dark, desktop 0.43, SCSI 3448 commands — the same 3448 plateau the
night's fix produced, now pinned. 131 gates. `DUO_BRINGUP.md` milestones
1-3 are checked off; next per §*Milestones*: PMU input (4), then the
platform's whole reason to exist, sleep/wake (6).

<a id="2026-07-31-se30"></a>
## 2026-07-31 (late night) — The SE/30 lands as the 33rd profile: wiring only, Finder on the first run

`docs/68K_FAMILY_SCOPE.md` scored the SE/30 as "free — wiring only: a
compact Mac IIx", and the estimate held exactly: **the machine booted to
the Finder on the first execution of its gate** (`se30_boot_etalon`,
menu bar 0.07 black / desktop 0.48 / 1155 SCSI commands, System 6.0.8 HD).
No new brick was written — the new code is one 80-line `Se30Video.h`.

What the SE/30 actually is (MAME `macii.cpp` `macse30`, "IIx with no
slots and built-in video"), mapped onto what already existed:

- **`MacIIMemory::Model::SE30`** on the shared mac2fdhd ROM ($97221136).
  Machine-ID pins are the *combination* of both 030 siblings: VIA1 PA
  $C1 (like the IIcx) + VIA2 PB $87 (like the IIx).
- **Internal 512×342×1 video = a NuBus card on pseudo-slot $E.** The
  existing `NuBus` HLE already did everything needed: the 8 KB
  `se30vrom.uk6` dump is a *linear* Apple declaration ROM (byteLanes
  $0F, no Toby descrambling) installed at the top of the $FE window;
  the 64 KB VRAM is served across the slot (MAME maps it at $FE000000
  + the $FEE00000 24-bit mirror). Page select is VIA1 PA6, from the
  written byte — PA6-as-input carries the machine-ID pull-up.
- **VBL is a slot-$E interrupt gated by VIA1 PB6=0** (MAME
  `se30_via_out_b` / `vblank_irq`, kept at MAME's every-other-frame
  phase toggle on purpose); disabling PB6 *is* the driver's ISR ack.
  The existing `via2Ca1SlotTaskArmed` guard (slot 9's SysError(51)
  lesson) covers slot $E unchanged.
- **Save states:** `SnapMachine::SE30 = 33`; the SE/30-only chunk tail
  is gated on the model, so existing II/IIx/IIcx snapshots stay valid
  without a format bump.

Also observed while gating: `q605_savestate_etalon` red with a
**byte-identical** save/load round trip and direct==restored hashes —
the guest continuation itself loses the Finder signature because
`MacOS-8.1-boot.vhd` is dirty again (drVolAtrb bit 8 = 0, image mtime
21:23 tonight). The known false-failure class (2026-07-25 entry;
`TODO.md` § 1 one-time GUI cleanup), not a regression.

<a id="2026-07-31-duo-finder"></a>
## 2026-07-31 (night) — The Duo 230 boots the Finder: /PMU_INT is a LEVEL, and $E1 re-uploads the PMU firmware

The PowerBook Duo 230 (platform #11, MSC + PG&E) **boots System 7.5.5 to
the Finder** — menu bar, battery icon, Control Strip, mounted volume,
all three ADBReInits completing. The "third ADBReInit hang" resolved
into a chain of three findings, each overturning the previous session's
frame (full narrative + instrumentation list:
`docs/DUO_BRINGUP.md` § *The deadlock, solved*):

- **The "empty" ADB request was legitimate.** `$4080AA1A` builds
  D0=D1=D2=0 → command `$00` = SendReset, ADBReInit's normal kick-off.
  Byte-identical requests succeed at reinit #1 and die later — the
  guest-ROM lead of the previous commit was a dead end.
- **PmgrOp `$E1` is a full PMU firmware upload.** System 7.5.x streams a
  second ~32 KB BORG image over SPI mid-boot ("BORG" magic on the wire)
  and the PG&E switches to it. Every PC-based firmware measurement after
  that point had been reading v1 addresses inside v2 code — the day's
  costliest detour. The v2 image handles the failing SendReset
  *correctly*: armed, tick-timed complete, cause posted, /PMU_INT
  asserted. The PMU was innocent too.
- **The deadlock was a lost interrupt edge.** The line's last falling
  transition (v2's once-per-second battery cause) landed inside the
  guest driver's masked-IER PmgrOp window; the driver's stale-flag ack
  ate the edge-latched IFR.CB1, and since the PMU never deasserts before
  a readINT drain, no edge could ever come again. MAME's
  `mscvia::pmu_int` (per the leaked System 7.1 source) asserts AND
  clears INT_CB1: **/PMU_INT is a level.** `Via6522::pmuIntLevel()` now
  holds IFR.CB1 while the line is low (survives IFR/ORB clears); only
  the PG&E drives it. 430 frozen int edges → 1326; SCSI 281 → 3448
  commands; Finder.

Also pinned: System 7.1 vanilla refuses Duos with a fully-rendered
gearing alert (an unplanned proof of dialog rendering + GSC 4 bpp);
unplugging the charger makes boot strictly worse (battery state machine
is load-bearing); capstone's 6805 disassembler prints bset/bclr
inverted. Next: `duo230_boot_etalon`, input, sleep/wake.

<a id="2026-07-31-slow-keys"></a>
## 2026-07-31 — The ten-month red gate was Slow Keys: the GUEST was rejecting the keys

`q605_cudalle_key_etalon` (RED since the commit that added it) is green, and
no emulator code changed to make it so. The keyboard path was never broken:
**the Mac OS 8.1 disk image has Easy Access Slow Keys enabled**, and a
Slow-Keys guest *correctly* rejects any key-down held shorter than the
acceptance delay. The gate typed 6-frame (~100 ms) taps; the guest dropped
every one, exactly as a real Quadra with that setting would.

**The instrument that cracked it** — `adb_key_probe` grew a RAM write-watch
(`POM68K_PROBE_WWATCH=<hex>`: writer PC + regs + one-shot disasm of each new
writer site). Walking the guest's own pipeline stage by stage:

1. *KeyTime ($0186) is a lie.* Its writer under 8.1 is a **periodic task**
   (`move.l $16A.w,D0; move.l D0,$186.w` every ~8 ticks, from boot minute
   one, zero keys involved). The prior session's "KeyTime advances in
   lockstep with keystrokes" was sampling-cadence coincidence. That retracts
   the old conclusion "the driver hears ADB" — it never heard anything.
2. *The classic ADB keyboard driver is present and healthy* (RAM `$D5xx`,
   same code under 7.6 and 8.1, loaded $10 apart). Under 7.6 its service
   head fires per report: press → `BSET` shadow (pc `$D59E`), release →
   `BCLR` (pc `$D5A4`), shadow `movem`-copied to KeyMap $174-$183.
3. *Under 8.1 only the release path ever fired* (`BCLR` ×4 for the four '8'
   releases; not one `BSET`). Presses vanished; releases landed. The wire
   trace shows every press AND release delivered as its own clean
   single-event report (`kbd report FF 1C` / `FF 9C`, queue 0) — transport
   exonerated end to end, again.
4. *The ADB device table (ADBBase $0CF8) names the culprit*: the keyboard's
   registered service routine is not the classic driver but a **system
   wrapper at `$00484A54`** — an acceptance-delay engine (globals `$484184`:
   `+$01` active flag, `+$22` press pending, `+$26` deferred keycode, `+$28`
   deadline in Ticks). Key-downs are buffered against the deadline and
   cancelled by the release if it comes first; key-ups pass through. That is
   Slow Keys, and the 2026-07-23 field report's "beep per letter in
   Netscape" is its rejection beep — same cause, now explained.

**Proof, both directions**: `POM68K_PROBE_HOLD=150` (2.5 s holds) → the
missing `BSET` appears, KeyMap live, on the exact cell that was dead. And
`POM68K_PROBE_RETURN_TOGGLE=600` (the Easy Access hold-Return gesture,
headless) flips the engine flag `$484185` `$FF→$00`, after which **6-frame
taps land AND Cmd-N repaints the screen** — pipeline proven end to end,
Command modifier included.

**Gate change, deliberately state-agnostic**: the timed phase now holds each
key 150 frames — accepted by a Slow-Keys guest *and* a normal one, so the
gate stays green if/when the image is cleaned. The Return-toggle was
rejected for gate use on purpose: it is a *toggle*, and would re-enable Slow
Keys the day the image is fixed. Also per the false-green memory: KeyMap
assert window narrowed 16→8 bytes. The image itself still has Slow Keys ON —
GUI cleanup is a TODO follow-up (toggle once + clean Shut Down).

Exonerated along the way, each measured: the ×50 boot-time R3 enumeration
dance (7.6 does the identical 50 rounds and works); Talk R2 (never polled by
this ROM — the `f1278ba` R2 bitmap is moot on the Quadra); the R0 byte-order
question (our MAME-inherited `[filler, event]` singles are accepted fine by
both drivers); both transports (LLE firmware and HLE byte model fail/succeed
identically — the discriminator was always the image).

**Methodology, the expensive lesson twice-paid now written down**: an
observable earns trust only after demonstrating BOTH sensitivity (it moves
with the stimulus) and silence (it does not move without it). KeyTime passed
the first test and was never given the second.

<a id="2026-07-31-two-negative-results"></a>
## 2026-07-31 — Two negative results, recorded on purpose

Neither shipped anything; both are here because the next person will
otherwise pay for them again.

**An O(1) ATC lookup: correct, and worth nothing.** callgrind put
`mmu040Translate` at **38.6 %** of the whole interpreter, so a 256-entry
direct-mapped page→entry hint table went in front of the 32-entry linear
scan. It was bit-exact by construction (each hint is validated against the
live entry before use, so a stale one costs a failed compare) and green
everywhere — `sst68040`, the smoke tier, a 60 M-step lockstep. Measured:
interpreter 213.2 s vs 213.2 s on the 5 G regime, 906.7 s vs 903.9 s on
20 G; x64 98.9 s vs 97.6 s. **Flat, or marginally negative.** The existing
single-entry `last[write]` memo already catches the hot case and the
residual scans are too short to beat. Reverted. The 38.6 % is real but it
is the WALK and the per-access bookkeeping, not the lookup — re-open only
with a profile that separates them.

**`q605_cudalle_key_etalon` is red, and it is not a regression.** Found by
the first full `ctest -L m040` in a while (29/30). Bisected in a clean
worktree: it fails at `b472a7b`, the commit that INTRODUCED it, with
byte-identical numbers. Ruled out by measurement rather than argument —
not a wedge (Ticks advance, boot completes at the usual 5236 SCSI
commands); not the Cuda wire (silent on the firmware LLE *and* on the HLE
substitute); not the `peek8`-is-physical trap (`TC = 0`, translation off);
not a missing device (`keyboardAddr` = 2, `ADBBase` = $00006DD0); not a
dirty volume (`drVolAtrb` bit 8 set); not the too-wide-window false green
(all sixteen bytes read 00).

A full `POM68K_ADB_LLE_TRACE` run then showed the transport is CLEAN end
to end: the guest polls Talk R0 to device 2 (2906×), our device queues
1028 key events and emits 829 reports, the Cuda relays each one
(TREQ → session → VIA SR), and the model matches `macadb.cpp` at every
checked point. The guest even stamps `KeyTime` in cadence — but `KeyLast`
stays 0 and KeyMap stays empty. **The break is inside the guest**, between
its ADB keyboard driver and its Event Manager.

A three-cell experiment (`tests/adb_key_probe.cpp`, the fourth cell is
impossible — 8.1 needs a 68040) then exonerated the machine:

| cell | KeyMap | Cmd-N repaints |
|---|---|---|
| LC II + System 7.5 (control) | live | yes |
| Quadra 605 + System 7.6 | live | no |
| Quadra 605 + Mac OS 8.1 | dead | no |

Same machine, same Cuda, same `AdbLine`: keys land under 7.6 and not
under 8.1. Two methodology notes, both paid for twice: `KeyLast` moves in
NO cell including working ones, so it is not a usable observable; and a
first pixel probe (256 KB of VRAM, typing a-b-c-d-e) reported "keys lost"
on a cell where KeyMap proves they arrive, because letters select nothing
on a bare desktop. Only the control cell caught both — believe an
observable after it has demonstrated sensitivity, not before.

<a id="2026-07-31-window-churn-dtlb-flush"></a>
## 2026-07-31 — The window-churn investigation ends on one deleted line: −23 to −33 %

The idle-Finder ceiling item ("one window death per ~15 instructions")
opened as an eviction-selectivity investigation and closed somewhere else
entirely. The eviction hook was already exact — per page, per space. What
every window re-arm ALSO did, on a path hit once per ~15 idle
instructions, was an unconditional `pomJitDtlbFlush()`: a 2 KB memset,
≈1.3 TB of memory traffic over a long run, and paid by BOTH backends —
including `threaded`, which never uses the data TLB at all.

The deletion argument is that every invalidation the flush stood in for
already has an exact owner: an MMU-generation bump flushes at the source
(`pomJitMapMoved`), an ATC eviction kills its derived entries per page and
per space (`pomJitAtcEvict`), privilege rides in each entry's tag, a page
gaining translated code flushes when it is marked and one losing its last
block flushes when it is unmarked. What survives between two arms is
exactly the set of entries whose backing ATC rows are still resident —
the exactness contract itself. Proven: two 60 M-step locksteps (x64 and
threaded) bit-identical, `ctest -L jit` 16/16.

Measured the honest way after an evening of desktop-load contamination
(the first bench cells read +5 % and were meaningless — load 10 on the
host): a NIGHT A/B on an idle machine, reference and fixed binaries in
adjacent pairs, three repetitions, load 1.00 on every cell. Result:
threaded −22.8 % / −26.7 %, x64 −28.5 % / −33.0 % (5 G / 20 G regimes);
DTLB fills 942 M → 7.8 M; the full matrix now reads interp 213.1/903.9 s,
threaded 126.1/565.7 s, x64 97.6/432.3 s — and the user-facing boot
etalon 61.3 s interpreted → **22.9 s on the JIT, ×2.68**, through the old
"~×2.5 conformant ceiling". One deleted call, the biggest JIT win since
the fetch window.

Also from the same evening's callgrind pass (the non-JIT profile the
churn numbers motivated): the interpreter spends **38.6 %** of all host
instructions in `mmu040Translate` and ~60 % in the translate/fetch chain
— the next conformant interpreter lever, ahead of everything else — and
1.6 % in `istreambuf_iterator` byte-wise asset loading at startup (a
trivial fread fix).

## 2026-07-30 — The five opcodes, same day: MOVEM + DBcc + JMP compiled

The census named them at 18h, they were emitting by 19h30. `MOVEM` in both
directions and sizes over the plain modes, as ONE span probe per burst — n
contiguous registers either all served by a single DTLB entry or the whole
instruction bails to Moira with nothing committed, which is also what makes
multi-access safety trivial. Two 040 rules carried over from `execMovem*`:
a compiled MOVEM bails while the RESTART LATCH is armed (a fault handler
may have moved the base register, and the restart must resume from the
saved ea — `PomJitLayout` gained `movemArmed` for the test, one byte
compare), and the `-(An)` base-in-list form stores initial−size with An
written once at the end. `DBcc` became an emitted terminator (its loops
close internally like Bcc's; taken 6 / expired 10 / condition-true 6, the
test-before-decrement order, the 040 odd-target error resolved at compile
time). `JMP <ea>` is the JSR emitter minus the push.

The harness earned its keep twice. The 5 M-step lockstep gates passed over
a MOVEM bug that the boot etalon then caught (a wedge in ROM): the
`-(An)` form stored the registers REVERSED in memory — Moira walks
descending registers at descending addresses, so ascending register order
must land at ascending span offsets. And the fix was then held to a
60 M-step lockstep (9 G JIT instructions, bit-identical), `ctest -L jit`
16/16 and the smoke tier.

Measured: boot etalon 25.6 → 24.7 s; `jit_bench` −3.0 % (5 G regime) /
−1.7 % (20 G) — the census's ~5 % Amdahl share, delivered. The idle
residue stays owned by the ATC window churn, now its own TODO item (an
investigation under the exactness contract, not a patch).

## 2026-07-30 — JIT measured honestly: x64 wins both regimes; the next lever is 5 opcodes

Three measurements that rewrote the performance backlog (details in
`POM68K_JIT.md` §3 addendum):

1. **The doc's bench table was stale** (pre-ATC-fix). Re-run on an idle
   host: `jit_bench` 5 G — threaded 167.8 s, x64 151.6 s; 20 G — threaded
   788.3 s, x64 709.5 s. **The Finder-idle crossover has flipped**: with
   JSR/BSR/RTS compiled, the link table and deferred boundaries all landed,
   x64 now beats threaded on BOTH regimes (~−10 %), validating `auto`.
2. **The idle-Finder ceiling is the exactness contract, not code size**:
   794 M window-lost exits over 12.2 G instructions = one window death per
   ~15 instructions. Mac OS 8.1's VM page-aging writes U bits, every ATC
   eviction kills the derived window/TLB state, and no conformant backend
   escapes that. The old "density" item is deprioritized accordingly.
3. **Coverage is 89.6 % native, and the uncovered idle top is FIVE
   opcodes** (dynamic census, 12.2 G instructions): MOVEM push/pop/load
   3.3 %, DBRA 1.26 %, JMP abs.L 0.66 %. DBRA is the sharp one — it is
   classified as a Branch terminator but `canEmit` refuses it, so every
   iteration of every DBRA loop in the System exits its block. Compiling
   these five (MOVEM + DBcc + JMP — two terminators simpler than the JSR
   already shipped, one register-mask loop) is the next conformant lever,
   worth ~5 % Amdahl plus the block-shape effect on tight loops.

## 2026-07-30 — `q605_savestate_etalon`: real-OS restore determinism on the 040 side

The lcii_savestate_etalon pattern applied to the other device-rich tree:
boot Mac OS 8.1 to the 640×480×8 Finder on the Q605 machine (default
LC 475 identity), snapshot, run 1200 frames of deterministic mouse
activity, hash; restore, run the same 1200 frames — byte-identical
re-save, identical machine hash, Finder signature still live at the end.
Passed first try (108.8 s). That takes the 040-wave chunks — DAFB
mid-frame, the AscIosb FIFOs, the Ncr53c96 session including its
deferred-IRQ countdown, the Cuda LLE MCU mid-transaction — from
unit-ROM-verified to real-OS-verified. With the LC II etalon this covers
both ends of the device spectrum; the remaining families reuse the
template as needed.

## 2026-07-30 — Engine re-baseline (idle host) + the CPU menu reaches the 030s

Two small items from the performance review, one real finding.

**Re-baseline** (the fix-day numbers were taken at load 14; these at ~1.3,
two runs per config): Q605 boot — interpreter 61.3-61.6 s, threaded fetch
window 32.3 s (×1.90), x64 generator 25.3-26.0 s (×2.40); `auto` lands on
25.2 s, confirming it now selects x64 on the 040s. LC II boot —
interpreter 137.8-152.3 s, window 141.1-143.8 s.

**The finding: the fetch window is now NEUTRAL on the LC II boot.** The
"×1.6 on the LC II" figure that motivated wiring the 030 engine menu
predates the ATC bit-exactness capping (2026-07-28): the LC II boots with
the PMMU on, so ATC evictions kill its windows exactly as they do on the
LC III (where the same pass had already shrunk the win to −9 %). Filed in
TODO where the stale figure lived. Consequence for the backlog: the 030
fleet's next conformant lever is widening the x64 backend (+ block
linking), not the window.

**CPU menu on the 030s** (`main.cpp`): `Cmd::CpuEngine` + engine/gauge
hooks on `LcMachine` and `SonoraStyleMachine` — V8, Sonora, VASP and RBV
run sites all bind `gSetCpuEngine`/`gGetCpuEngine`/`gJitStats`, the exact
QuadraMachine pattern (swap on the machine thread, between two quanta).
Shipped despite the finding above: it is the honest control surface, and
the day the x64 backend covers the 030s the menu is already in place.

<a id="2026-07-30-save-states-gui"></a>
## 2026-07-30 — Save states in the GUI: « Sauver / Restaurer l'état »

The last structural piece of TODO § C: every machine's **Machine** menu now
carries « Sauver l'état » / « Restaurer l'état » (the Mac II gets buttons in
its CPU panel), backed by one shared `SaveStateSlot` embedded in each
machine-thread struct. The threading contract is the `Cmd::CpuEngine`
precedent taken seriously: the GUI thread only queues a request; the
machine thread performs the actual save/load inside `applyCmds()`, between
two quanta — a restore replaces the entire device tree, so it must never
run mid-quantum from another thread. The outcome comes back as a one-line
message the menu displays (and stdout logs).

Decisions worth keeping:
* **State files pair with the boot volume**, like the `.pram` files and for
  the same reason: `<image>.<profile>.pomss`, derived from the pramPath at
  every site. Written temp+rename (the floppy write-back convention) so a
  crash never leaves a truncated state.
* **A refused snapshot is a message, not a crash**: `load()`'s identity
  refusal (wrong profile, ROM, RAM size, corruption) reaches the user as
  the menu line, and the running machine is untouched.
* **The Plus/compact loop is single-threaded**, so it applies the slot
  inline between frames and calls `MacFrameClock::resync()` after a
  restore — `frameBase` is derived from the CPU clock, which the restore
  just moved.
* Profile tags are re-derived at each run site (env `POM68K_Q605_ID` /
  `POM68K_CENTRIS_MODEL` / `POM68K_Q630_ID` for the identity twins), so a
  Quadra 605 state cannot be restored into an LC 475 sharing the same ROM.

The machine-level save/load is behaviour-gated (`savestate_*_test`,
`lcii_savestate_etalon`); the GUI layer itself is compile-verified — a
hands-on click-through on a booted machine is the remaining validation.

## 2026-07-30 — Save-state fan-out: all 10 machine families serialize

The LC II foundation generalized to the whole tree in one pass, three waves:

* **030 wave** — `SonoraMemory`, `VaspMemory`, `RbvMemory` (+ the IIci
  flavor, which pulls `AdbVia` + `Pic1654s` + `Rtc` into the format), CPU
  wrappers `SonoraCpu`/`VaspCpu`/`RbvCpu` rebased onto `MoiraSnapshot`.
  The Sonora modeline pointer is derived state: `mode_ = modeline(vidMode_)`
  is an invariant of `vctrlWrite`, so load re-resolves it instead of
  serializing a pointer.
* **040 wave** — `Q605Memory`, `CentrisMemory`, `Q700Memory`, `Q630Memory`
  with the family's own cells: `Dafb` (all three clock generators' serial
  state), `Valkyrie`, `AscIosb`, `Ncr53c96` (whose `disk_` travels as a
  target ID and is re-resolved, the Ncr5380 rule).
* **68k wave** — `MacIIMemory` (NuBus IRQ lines; `TobyVideo` serialized by
  the machine that owns it, presence recorded and refused on mismatch) and
  `MacMemory` (M0110 keyboard transaction engine, `MacKeyboard`/`MacMouse`),
  `Cpu020`/`Cpu68k` on `MoiraSnapshot`.

The container is now machine-shape-generic (`saveT`/`loadT` templates +
one-line public forwards), `SnapMachine` carries one tag per profile (32 —
identity twins share a ROM, so the checksum alone cannot tell an LC III from
an LC III+), and `SaveStateMachines.cpp` names every archive instantiation
so a chunk that stops compiling fails the build, not the eventual user.

Gates (all `unit`, synthetic counter-loop ROMs, no assets):
`savestate_030_test` (Sonora/VASP/RBV/RBV-iici), `savestate_040_test`
(Q605/Centris/Q700/Q630), `savestate_68k_test` (Plus/SE/Mac II+Toby) —
re-save byte-identity AND run-N-vs-restore-run-N determinism per family.
Blast-radius pass: `ctest -L smoke` + the lc3/iisi/iivx/se/macii boot
etalons, all green.

Two machine-entry quirks found writing the synthetic ROMs, worth keeping:
the Mac II does not enter through the ROM header vectors — `Cpu020::
hardReset` hardcodes the Basilisk entry (SSP=$2000, PC=ROMBase+$2A) and
starts in 32-bit mode (`hmmu24_` off until VIA2 PB3), so overlay code must
poke VIA1 at `$50F01E00`, not a 24-bit alias; and the Plus overlay latch
samples `portA()`, so DDRA must be written before ORA_NH for PA4 to stick.

Not yet: real-OS savestate etalons for the nine new families (the LC II
`lcii_savestate_etalon` is the template) and the GUI/CLI hook.

## 2026-07-30 — Save states survive the real Finder: `lcii_savestate_etalon`

The archive core's second gate, and the one that mattered: `savestate_v8_test`
proved determinism over a synthetic counter-loop ROM, which exercises the
plumbing but not the devices. The new etalon boots the LC II to the System 7
Finder off the SCSI image, snapshots the live machine (10.9 MB, 3 dirty SCSI
blocks), runs **1200 frames of deterministic mouse activity** (wiggle +
desktop click, injected at frame boundaries so both runs see identical
machine times), and hashes the whole machine; then restores the snapshot and
runs the same 1200 frames. Result, first try: `load→save` byte-identical, and
the direct and restored runs converge on the **same 10 874 549-byte machine
hash** (`b06e3c57…`), with the menu bar still live at the end (guarding
against a "deterministic corpse" pass where both runs wedged identically).

That upgrades every LC II device chunk — the ScsiDisk copy-on-write log
replayed over the pristine image, SWIM1, both ASCs, the SCC, and the Egret
LLE MCU snapshotted mid-transaction — from compile-verified to
behaviour-verified, which is what the TODO said was the gap. The mouse
scenario is load-bearing, not decoration: an idle Finder barely touches the
autopoll→MCU→VIA-SR→ADB chain, and a field omitted from a `visit()`
round-trips silently until that path runs on it.

## 2026-07-30 — A JIT backend is valid per GUEST family, not just per host

`jit_lcii_boot_etalon` was timing out at its full hour. It was not a save-state
regression (the same tree boots the LC II in 2 min 21 s on `threaded`): letting
`auto` reach the x86-64 generator also handed it the **68030** machines, and
that generator is written entirely against the 68040.

**The mechanism, measured rather than assumed.** With `POM68K_JIT_VERBOSE=1`
the engine compiles eight blocks — all `BTST`/`TST` + branch around
`$40A148xx-$40A149xx` and `$40A0A8E6`, which is the ROM's Egret handshake
poll loop (`Egret.h` pins that code at `$A14CE0-$A14E9C`) — and then prints
nothing more. Not a slowdown and not a recompilation storm: the guest is
wedged *inside* the compiled blocks. The discriminator was
`POM68K_JIT_ACCESS_THUNK=0`, which hands any memory-touching instruction back
to the interpreter: with it, **x64 boots the LC II to the Finder**. So the
divergence lives in the natively-compiled memory-access path, where
`pomJitReadData`/`pomJitWriteData` reach `mmu040Read`/`mmu040Write` with no
model test and `pomJitProbeData` refuses everything below `M68EC040`
outright. That localization is the starting point for widening the backend;
it does *not* exonerate the other 040 assumptions baked in around it
(`(An)+` updates before the access on an 030 and after it on an 040,
`MoiraDataflow_cpp.h:326-332`; the 030's restartable last write, `:355-361`;
the prefetch queue refilled on one core and not the other, so `queue.irc`
means different things at a block exit).

**The fix makes validity a declared capability, not a scattered test.**
`BackendCaps::guestFamilies` (`JitBackend.h § GuestFamily`): `threaded` is
`kGuestAny` — structurally, because it replays Moira's own handlers and
therefore inherits whatever the model does — and x64 is `kGuest68040`.
Selection now tests guest validity *before* ranking by host capability, so
`auto` lands on `threaded` for the 68000/020/030 machines and keeps x64 on
the 040s, which is exactly where its 26.1 s vs 32.6 s was measured. An
explicit `POM68K_JIT_BACKEND=x64` on an 030 is refused with an explanation
rather than honoured into a silent wedge; `POM68K_JIT_UNSAFE_BACKEND=1`
forces it for whoever is working on that family. The field defaults to
`0 = undeclared`, which selection treats as unusable — a new backend (a64 is
planned) that forgets to state its scope gets a diagnostic, not a wedge on
the first machine nobody tested.

**One wrong turn, worth recording because the fix was self-diagnosing.** The
first version read the family from `cpu.getModel()` inside the Engine
constructor. An Engine is a MEMBER of every CPU wrapper, so it is built
before the wrapper's body reaches `setModel()` — the diagnostic printed
"not valid for a 68000/68010 guest" for the 68030 LC II, and the Quadra
quietly lost x64 to the same mistake. `Engine`'s `guestFamily` is now a
REQUIRED constructor parameter supplied by each of the eight wrappers
(`Cpu030` derives it from its `as020` flag), so a new wrapper that forgets it
fails to compile instead of silently falling back.

## 2026-07-30 — Save states: the archive core + the whole LC II tree

**One `visit<Ar>()` per class drives both save and load.** `src/SaveState.h`
holds two archives (`Writer`, `Reader`) that instantiate the same visit body,
so a field added to a device is live on both paths at once. Hand-written
save/load pairs are the classic source of save-state corruption — a field
added to one and forgotten in the other restores as garbage months later —
and the visitor removes that failure mode by construction rather than by
review. Container: magic + version + a sequence of `tag`/`length` chunks, so
an unknown chunk is skipped and *reported*, never silently dropped.

**Covered:** the LC II tree, 20 classes — `Via6522`, `PseudoVia`, `Ariel`,
`Egret` (HLE), `CudaLle` + `M68hc05` + `AdbLine` (LLE), `AdbBus`, `AscV8`,
`AscSonora`, `Ncr5380`, `ScsiDisk`, `Iwm`, `Swim1`, `Swim2`, `SonyDrive`,
`Scc8530` (+ its three nested structs), `V8Memory`, `Cpu030`. `V8Video` is
stateless. Gates `savestate_test` (archive core) and `savestate_v8_test`
(end to end on a live machine), both in the `unit` tier — no ROM, no image.

**Four rulings worth keeping.**

*Callbacks and cross-device pointers are re-bound, never serialized.* The
device tree is full of `std::function` hooks and raw pointers; a restored
pointer either dangles or, worse, addresses the previous session's object.
`Ncr5380::disk_` shows the rule: it travels as a target ID and is
re-resolved against `targets_[]` on load.

*Pure caches are flushed, not carried.* A restore replaces RAM — and
therefore the page tables — underneath a CPU whose ATCs still hold the old
translations. Carrying a stale ATC is a bug; an empty one costs a refill.
Same for the 030 i-cache and the JIT guard (`JitGuard.h § invalidate` — a
wholesale RAM change is exactly the thing a *write* cannot express). This
needed **one line in the vendored tree**, `Moira::pomFlushAtcs()`, because
both flushes are private and no public setter reaches the 030 one
(`setURP040` covers only the 040) — recorded in `POM68K_VENDOR.md`.
Everything else stays on the POM68K side of the seam: the CPU chunk lives in
`MoiraSnapshot::visitCpuCommon()`, which works only because Moira's
execution state is already `protected` and the wrappers derive from it.

*The disk plan "path + checksum" was wrong, and the reasoning matters.* The
obvious design records the image path and the blocks the guest modified. But
a block written **after** the snapshot is not in the snapshot's set, and its
pristine content is no longer anywhere in memory — so re-reading the host
file restores post-write bytes with nothing to correct them. `ScsiDisk` now
keeps a **copy-on-first-write log**: the pre-write bytes of every block
touched since `open()`. Restore reverts the log, then replays the snapshot's
blocks. Exact, and it costs only what the guest actually wrote. `SonyDrive`
needs none of it — a floppy medium is ≤ 1.44 MB, so the whole image travels
through the zero-run codec.

*A compression guard must not be a ratio.* The first zero-run decoder capped
the decoded size at `encoded << 12`, which `savestate_test` immediately
failed on 64 KB of zeros: a zero run costs a varint whatever its length, so
that buffer encodes in 7 bytes (~9000:1) and the ratio bound rejected
precisely the best-compressed input. The cap is now absolute (1 GiB, above
any emulated machine's RAM), so it constrains only corrupt files.

**Method note.** `visit()` bodies are templates, so a successful build proves
nothing about them — they are not compiled until instantiated. The explicit
instantiations in `SaveStateMachines.cpp` are that check, one line per class
per archive, and they caught the private-ATC problem above on the first run.
The strong gate is likewise not round-tripping but **determinism**: run N
cycles from a state and snapshot; restore the state, run the same N, snapshot
again; require identical bytes. Omitted state round-trips fine (nobody wrote
it, nobody read it) and only diverges once the machine runs.

**Not yet true:** nothing in the GUI or CLI can take a snapshot, the other
nine machines are untouched, and there is **no boot etalon** — determinism is
currently proven only over what a synthetic counter-loop ROM exercises
(CPU, RAM, VIA timers, MCU), not over SCSI/floppy/ASC/SCC under a real OS.
The device chunks are compile-verified, not behaviour-verified. `TODO.md § C`
carries the remaining work in ROI order.

<a id="2026-07-29-pgo-four-cpu-families"></a>
## 2026-07-29 (late) — PGO across all four CPU families (−26 % on the LC II); the dispatch-table item measured and dropped

> **Corrected the same evening (commit `b4ef0a6`, recorded in `TODO.md` — this
> entry is the only place the old number survives).** The lazy-condition-codes
> figure below, **+2.5 % for a whole duplicated flag set**, was measured with
> `POM68K_CPU_ENGINE=jit`, which selected the *threaded* backend — so the
> modified `JitBackendX64.cpp` never ran and the delta was noise. Re-measured
> with `POM68K_JIT_BACKEND=x64`: 26.13/26.15 s → 26.37/26.30 s = **+0.8 %**.
> The conclusion (lazy CC is not worth the bit-exactness risk) stands; the
> number that supported it did not. The same mis-measurement exposed
> `selectBackend("auto")` filtering its loop on `dflt`, so `auto` always landed
> on `threaded` — fixed in the 2026-07-30 JIT entries.

**PGO now trains on one machine per CPU family** (`tools/pgo_train.sh`):
Quadra 605 (040 + MMU), LC II (030, MMU off), LC III (030, MMU on) and
the LC (020 + HMMU), on BOTH execution engines. The shipped recipe
trained on the Quadra boot alone, so the profile optimized the 68040
paths and left every 030/020 path cold — `mmuFetchWord`,
`mmuTranslateAccess` and the V8/Sonora decode cascades never appeared in
it. **Measured on two families: the LC II boot 145.0 / 144.2 s → 107.0 /
107.6 s (−26 %, ×1.35), and the 68020 LC boot 96.2 s → 84.3 s (−12 %,
×1.14)** — same Release flags on both sides, gates passing with
bit-identical signatures. The gain lands on the DEFAULT engine, which is
the one users actually run. PGO changes code
layout, never semantics.

**Two more backlog items dropped on measurement.** *Lazy condition
codes*: measured by duplicating the flag emission (storing the same byte
twice is semantically a no-op, so the guest is unaffected and the delta
is the marginal cost of one full materialisation set) — Q605 JIT boot
32.6 s → 33.4 s, **+2.5 % for a whole extra set**. So removing a set
saves at most ~2.5 %, and lazy CC can only remove the dead subset:

1-2 % for an intricate codegen change that silently breaks
bit-exactness when wrong. The backlog's "a third off the
per-instruction contract" was true of contract SIZE, not of time.

**The page-granular dispatch-table item is dropped, and the measurement
is why.** It assumed `read8`/`write16` re-run a deep range-compare
cascade on every access. Counting accesses by destination over an LC II
boot — 1 475 M of them — gives **RAM 69.6 %, ROM 29.3 %, I/O 0.33 %,
other 0.80 %**. Ninety-nine percent land on RAM or ROM, whose paths are
already two to four *perfectly predicted* compares (`V8Memory::ramIndex`
was inlined for exactly this in 2026-07-17), while the long cascades a
table would shorten carry a third of one percent of the traffic. A 4 KB
table would put a dependent load in front of the 99 % case to save
branches that cost nothing. Recorded in TODO as dropped-with-evidence
rather than deferred: re-opening it needs a profile showing decode as a
real share, and the honest lever for I/O-heavy code would be per-device
caching, not a global table.


## 2026-07-29 (evening) — A CD mounts in the guest; .cue/.bin; and why 8.6 cannot boot

**The guest mounts a CD.** `q605_cdrom_etalon`: Mac OS 8.1 boots off the
hard disk and the Mac OS 8.6 disc arrives as data — the 640×480×8 Finder
signature plus ~100 blocks of catalog traffic served by the CD target.
Layout is part of the result: the ROM's SCSI scan runs 6→0, so the boot
volume sits at ID 6 and the CD at 3, otherwise a bootable disc wins the
scan and the hard disk never boots.

Getting there took three real fixes that hand-written CDBs had not
caught — a live Mac OS driver found them (new `POM68K_CD_TRACE` logs
every CDB), all against MAME `bus/nscsi/cd.cpp`: MODE SENSE was omitting
the **block descriptor** (how the driver learns the disc is 2048
bytes/block — without it OS 8.1 asked for the Apple magic page once and
went silent), READ TOC handled only **format 0** (the driver also wants
format 1 session info, and reads the format from the SFF8020 legacy
field in `cdb[9]` when `cdb[2]` is zero), and **mode page $0E** (CD audio
control) was missing, which the driver requests the moment it accepts a
disc. Format 2 now answers CHECK CONDITION exactly as MAME does — an
honest refusal beats an invented reply.

**`.cue`/`.bin` and raw MODE1/2352.** A 2352-byte rip is de-framed to its
2048-byte user data (12-byte sync + 4-byte header + data + ECC), and only
when the sync pattern actually says so — a 2352-multiple without it is
refused rather than mis-framed, because a mis-framed volume looks exactly
like a corrupt disc to the guest. A `.cue` sheet is parsed for its `FILE`
and first MODE1 track, resolved beside the sheet. The CLI now routes
`.iso/.cdr/.toast/.cue/.bin`.

**Boot from CD works** (`q605_cdboot_etalon`, added once a Mac OS 8.1
retail disc was available): with no hard disk attached, the ROM's 6→0
scan reaches the CD at ID 3, loads its `Apple_Driver43_CD` partition and
boots the disc — **3913 blocks / 7.8 MB of System read off the CD
target**, ending at the 640×480×8 Finder. The traffic is what is
asserted, not the pixels: a Finder drawn from a hard disk looks
identical.

This also settled the earlier 8.6 result. **Mac OS 8.5/8.6 are
PowerPC-only** — 8.1 is the last 68k release — so the black screen that
disc produced was correct behaviour, not an emulation bug. The ROM had
picked the disc and loaded its driver exactly as it should.

Also observed, not yet explained: only discs whose driver descriptor map
declares `sbBlkSize = 2048` mount. The 512-byte-DDM hybrid `TIM_3.iso`
and the bare-HFS `.toast` images are probed (4 blocks) and ignored. That
may well be correct — the 8.6 disc is the only one carrying an
`Apple_Driver43_CD` partition — so it is recorded in TODO as an
observation to verify, not a bug to chase.


## 2026-07-29 (later) — SCSI CD-ROM support, a guest-level floppy gate, and the LLE inventory re-synced

**CD-ROM targets are in.** `ScsiDisk` gained a second personality
(`openCdrom`) rather than a new class, because both SCSI controllers and
all 32 machines already route to `ScsiDisk*` — MAME derives its cdrom
from a shared base for the same reason. A CD differs in its INQUIRY type
($05, removable), 2048-byte blocks, being read-only, and a handful of
commands: READ TOC (one MODE1 data track + lead-out, LBA and MSF),
START/STOP UNIT with eject, PREVENT/ALLOW REMOVAL, MODE SELECT, and
WRITE refused as DATA PROTECT. The load-bearing piece is the **Apple
magic MODE SENSE page $30** carrying "APPLE COMPUTER, INC" (MAME
`bus/nscsi/cd.cpp:604-618`) — Apple's CD-ROM driver reads it to decide a
drive is genuine, and without it a disc never mounts however correct the
rest of the target is. Ejecting empties the medium but keeps the target
present, so the guest sees an empty drive rather than a missing device,
and reads then fail NOT READY instead of silently returning zeros.
Raw 2352-byte rips are refused outright rather than mis-read as MODE1 —
a wrong block size looks exactly like a corrupt volume from the guest.

Wiring: `attachCdrom(path, id = 3)` on all nine multi-target machines
(MAME puts the Mac's CD at SCSI 3, `maciivx.cpp:323`), and the CLI now
routes `.iso`/`.cdr`/`.toast` there automatically — name-based on
purpose, since a `.dsk` that happens to be 2048-aligned is still a hard
disk. Gate `scsi_cdrom_test` (31 checks, self-contained — it builds its
own image, no assets needed).

**A guest-level floppy gate** (`lcii_floppy_etalon`): the guest side of
the floppy path had no coverage at all — `floppy_persist_test` drives
`SonyDrive` directly. The new scenario inserts an 800K HFS medium after
the Finder is up (an insert EVENT is what makes the System poll, and it
is the real user gesture) and requires it to read ~1.7 M nibbles over the
real IWM, mount the volume, open its window, and survive eject +
re-insert byte-intact. A guest-INITIATED write is deliberately NOT
asserted: Cmd-N in the post-insert state modifies neither the floppy nor
the hard disk while the identical gesture works on the HD in `persist`,
so the keystroke is dropped rather than misrouted. That gap is written up
in TODO with the evidence instead of the assertion being quietly
weakened to look like coverage it does not have.

**`docs/LLE_VS_HLE.md` sixth pass**: the header was two rounds stale (26
profiles / 91 gates, now 32 / 120). The hack list is empty on every
default path; §4 gained the Cuda I2C/DFAC2 fact and the RBV
physical-vs-logical low-memory split; §5 now states plainly that the
remaining LLE distance is §3 (whole-frame video, tick batching, no 040
copyback, SCC bit-serial, floppy flux, NuBus/DAFB timing) — with the
caveat that 22 of 32 profiles still have no beyond-boot gate, so the
test-depth pass outranks every fidelity item on that list.


## 2026-07-29 — Input-delivery gates for the 030 families; loud HLE fallbacks (and a retracted "bug")

**Four beyond-boot input gates** (`family_input_etalon`, one binary —
`lc3_input_etalon`, `lc520_input_etalon`, `iivx_input_etalon`,
`iisi_input_etalon`) prove the firmware-LLE ADB path actually DELIVERS
on the Phase C families, not just that it boots: System 7.5 up blind,
mouse deltas injected into the bit-serial `AdbLine`, the input must
arrive — wire → MCU firmware autopoll → VIA1 SR → ADB Manager →
drivers, end to end. Green on the LC III (Egret 341s0851), LC 520
(Cuda 341s0060 + the new DFAC2 slave), IIvx (VASP @ 31.3 MHz) and the
IIsi (Egret 344s0100, RBV).

**Retraction, same day: the "IIsi has no working ADB" finding was
wrong — three times over — and the machine was fine all along.** The
gate and every follow-up probe read low memory through `peek8()`, which
is PHYSICAL. The IIsi is a **RAM-based-video** machine: physical low RAM
*is* the framebuffer, and the ROM uses the PMMU (TC = `$80F84500`,
translation on) to put the System's logical low memory elsewhere. So
every "global" I read was a screen pixel — `$55555555` in "ADBBase" is
the 50%-gray desktop pattern, `$FFE7C7FF` is dithered content, the
zeros are black, and the "QuickDraw fill loop zeroing the globals" at
`$4082D01A` was QuickDraw painting the desktop, exactly as it should.
An MMU-independent check settles it: capture the framebuffer, inject
motion, capture again — idle diff **0 px**, after motion **46 px**. The
cursor moves. `iisi_input_etalon` now asserts that and is registered.

Two things worth keeping from the wrong turn. First, **`peek8()` is
physical**: on any machine whose ROM relocates low memory behind the
MMU, guest-global assertions are meaningless — check `TC` bit 31 before
trusting one, or assert on something the MMU cannot move. Second,
**corroborate before concluding**: each round produced a coherent story
(a spin at `$4080A8E6`, 51 relocation passes, "nobody ever writes
ADBBase") and every one of them was consistent with the artifact, so
internal coherence proved nothing. One cheap end-to-end observation —
does the cursor move? — would have killed all three at the outset.
Prefer the observation closest to the user's experience over the one
closest to the code.

**A real gate defect was found and fixed en route**: the keyboard check
scanned 16 bytes at `$0174` when KeyMap is exactly 8, so the neighbour
`$017D = $41` read as a keystroke. Narrowed to 8; the globals-based
machines still pass, so a real keypress does land in KeyMap. A positive
assertion over a too-wide window is a false green.

**Every HLE ADB fallback is now LOUD** (the §1.9/§2 retirement-policy
pass, `docs/LLE_VS_HLE.md`): all eight machine classes and
`AdbVia::attach` print a NON-CONFORMANT-substitute notice when the MCU
dump is missing or `POM68K_*_LLE=0`/`POM68K_ADB_LLE=0` forces the HLE.
The fallbacks are kept — dumps are user-provided and non-distributable,
and the V8-class machines cannot boot without an Egret/Cuda — but the
"visible non-conformant flag" principle now holds everywhere; §1.9's
ORB→SHIFT re-arm lives only inside that announced fallback, and actually
deleting `Egret.*`/`AdbBus`/the byte-model is recorded as a deliberate
product decision, not a cleanup. Diag knobs added en route:
`POM68K_INPUT_ANYPATH=1` (input gates on the HLE path), SRQ-presented +
mouse-Listen-R3 traces under `POM68K_ADB_LLE_TRACE`.

## 2026-07-29 — The Color Classic "0417 wedge" was a missing DFAC2, not a core bug; both factory Cudas land

**The factory Color Classic Cuda 341S0417 (2.35) is the CC's default
firmware.** The 2026-07-24 bring-up note said it "wedges the M68hc05 —
releases the host reset then never answers the VIA transport" and parked
the CC on the Q605's 341S0788 (2.37). The differential trace (new diag
knob `POM68K_CUDA_FW=<path>` forces a dump; scratchpad tracer, PC
histogram + I2C edge log) showed the truth: the 2.35 was never stuck —
after the first host VIA session it idled in its normal main loop,
having decided the transaction was over WITHOUT clocking the final ack
shift the host waits on. The real divergence happened earlier: right
after reset release, both firmwares bit-bang an I2C probe on PB7/PB6
(same routine, 4 bytes apart in the two ROMs) — and MAME's `maccclas`
shows what's on that bus: an **Apple DFAC2 audio chip at I2C address
$6F** (`maclc.cpp:505`, `dfac2.cpp` `i2c_hle_interface(…, 0x6f)`). Our
`CudaLle` hardcoded SDA=1 — an eternal NACK. The 2.37 shrugs (retries
once, moves on); the 2.35 takes a DFAC error path after ONE aborted
probe and mutes the next host session. With the ACK, the factory 2.35
boots System 7.5 to the Finder.

The fix is a **minimal I2C slave in `CudaLle`** (`setI2cDfac`):
START/STOP detection, 9-pulse byte cadence, SDA held low through the
ACK clock whenever the transfer opened at $6F; payload discarded —
oracle parity, since MAME's `dfac2_device::write_data` only logs (its
registers are still being reverse-engineered upstream). Enabled
per machine, following MAME's wiring exactly: Color Classic
(`maclc.cpp:505`), the Cuda AIOs LC 520/550/CC II (`maclc3.cpp:403`),
Quadra 630/LC 580 (`macquadra630.cpp:196`, bus shared with Valkyrie).
NOT the Q605 (empty bus) and NOT the Mac TV (`device_remove("dfac")`,
nothing re-added).

**The Mac TV's factory Cuda 341S0789 (2.38) landed the same day**
(dump CRC `682d2ace` = MAME's) — the `kTvFw` list already preferred it,
so the TV now boots on its factory firmware too. With that, **every
Egret/Cuda machine runs its exact factory MCU part**. Gates:
`m68hc05_test` extended to all four Cuda dumps (0417/0788/0789/0060
execute clean); `cclassic_boot_etalon` now exercises the factory 2.35 +
DFAC2 path by default; `mactv_boot_etalon` (the phase-fragile one)
green on the factory 2.38; `lc520_boot_etalon` / `q630_boot_etalon`
green with their new DFAC2 ACKs; `cuda_lle_test` / `egret_lle_test`
unchanged green.

## 2026-07-28 — LLE step 7: the virgin line reads clean; `POM68K_SCC_CLEANLINE` retired

**The standing no-peer SDLC abort is now fully a LINE state** — the last
place where machine configuration decided a wire condition is gone
(`docs/LLE_VS_HLE.md` §1.10 RESOLVED). The LLE story: LocalTalk is FM0,
so the SCC recovers its receive clock from the line's own transitions —
a **virgin line that has never carried a frame has never given the DPLL
an edge**, hence no recovered clock, no sampled 1s, **no abort**; RR0
bit 7 reads clean. The abort condition genuinely begins with the first
frame the line carries (an LLAP trailer ends in a real abort sequence,
then the driver releases the line mid-mark — the receiver's last
recovered state IS the abort): `Scc8530` latches `lineDriven_` at local
SDLC frame completion (EOM), at Send Abort, and at any transport frame
(`injectRxFrame`), and `openLine()` requires it alongside the §1.8
peer-hold suppression. The EOM path also presents the trailer's abort
as the ext/status event when only WR15 bit 7 is armed — on a
previously-virgin line the guest's own first ENQ probe is what starts
the LAP's abort stream, which keeps the System 7 no-peer timeout
mechanics intact (`lcii_boot_etalon` / `q605_boot_etalon` timing
unchanged, measured 137.9→137.3 s / 64.1→62.6 s — within noise).

This is what Open Transport waits for: OS 8.1's `.MPP` bind spins until
RR0 bit 7 clears (the §1.10 wedge, SCCDBG capture 2026-07-24), and a
virgin-clean line satisfies it by construction. Notably, **the wedge
itself is no longer reproducible on today's tree** — the new gate's
configuration (in-process AppleTalk hub attached, boosted lossless
wire, exactly the `main.cpp` wiring) boots OS 8.1 to a bound `.MPP`
with the old standing-abort-at-reset code too, bit-identical traffic
(352 lapENQ + 3 DDP) with and without the env; the 2026-07-25 bus-time
pass (i-cache boost no longer compressing SCC-visible pacing) most
plausibly unwedged it in passing. The virgin-line semantics is landed
as the honest model that *guarantees* the bind regardless of timing,
and `POM68K_SCC_CLEANLINE` is deleted from all eight memory classes
(`setAbortIdle(true)` everywhere — the flag is again purely "this
connector has no hardwired peer").

Gates: **`q605_ot_bind_etalon`** (new — OS 8.1 + the in-process hub;
proof of bind is the guest's DDP conversation with the stack after its
lapENQ dance, since a wedged OT probes its node ID and then never
speaks DDP), `scc_ext_test` + `llap_loop_test` re-pinned (virgin line
clean → drive the line → standing abort; peer-drop/return/express pins
unchanged), smoke + the SCC/LLAP/boot etalons of every touched family
green.

## 2026-07-28 (eighth pass) — O(1) probes, per-space eviction, and the 020 seam

**The 030 probe is O(1) now.** `pomJitProbeCode` checks the interpreter's
own last-hit memo (`mmuAtcLast` / `mmu040AtcLastI`) before scanning, and
`pomJitAtcEvict` learned which SPACE the evicted entry backed: an I-side
eviction kills the code window only, a D-side eviction kills the data TLB
only — the first cut killed both on either, which threw the window away
every time a data page sharing its logical page churned. Fingerprints
unchanged by both (the eviction hook keeps window-on and window-off
walk-identical, so engine-side tuning cannot touch the guest). Measured on
the LC III, idle host, back to back: the JIT went from 32 % SLOWER than the
interpreter to 9 % slower (254.7 s vs 276.8 s). The remaining gap is
structural — a unified 22-entry ATC under System 7.5 evicts code pages
constantly, and the window must die with them.

**The 020 seam is in, and honestly measured as not worth switching on.**
`pomJitFetch020` replicates the head of a `read<PROG,Word>` (the POLL_IPL
riding on the prefetch, the FC pins, the /BERR access stamp) and serves the
tail from the window, hooked at the three C68020-only fetch sites
(`prefetch`, `fullPrefetch`, `readExt`) — the 68000/68010 cores never see a
single added branch. The probe is identity (no MMU), and `pomJitExecOne`
grew the generic dispatch branch. The Macintosh LC — V8 map + `as020`, so
ZERO new machine plumbing — boots to the Finder on both engines… in 100 s
interpreted and 156 s under the JIT. The physics: a plain 68020 makes ONE
cheap fetch per instruction (no ATC, no translate, queue-fed), so the
window has almost nothing to save, and the engine's per-instruction loop
plus re-arming across far jumps costs more than the fetch tail was worth.
`jit_lc_boot_etalon` guards the seam anyway: it exists for the day a code
generator gives the 020 something real to win with. The Mac II family's map
plumbing (GLUE `physAddr` remap, the IIx PMMU interaction) is deliberately
deferred until then — plumbing a map for a configuration that loses is
work with negative value.

Where the engine is WORTH switching on, after all of today: the 68040
machines (×2-2.5 on real work, ×4 with PGO), and the MMU-off 030s (LC II
×1.6). Where it is not (yet): MMU-on 030s (−9 %) and plain 020s (−56 %).
The engine is off by default everywhere, so every one of those numbers is
an option, not a regression.

## 2026-07-28 (seventh pass) — All four 030 families under the engine

The remaining 030 memory maps got the V8 treatment: **Sonora** (LC III /
LC III+ / LC 520 / LC 550 / CC II), **VASP** (IIvx / IIvi) and **RBV**
(IIsi / IIci) each grew codeSpan/dataSpan/guard plumbing — these three are
flat maps, so the diff per machine is small — and their wrappers
(SonoraCpu / VaspCpu / RbvCpu) carry the same jit::Engine member as Cpu030.
With the V8 family from the previous pass, every 68030 machine POM68K
ships is now behind the engine; JIT gates registered for one machine per
family (`jit_lc3`, `jit_iivx`, `jit_iisi`, plus `jit_lcii` and the
phase-fragile `jit_mactv`).

**Correctness: complete.** Seven boot etalons ran to the Finder on BOTH
engines during this pass (LC II, Mac TV, LC III, IIvx, IIsi and the 040
smoke set), and the bench fingerprints are unchanged by any of it — the
ATC-eviction hook guarantees window-on and window-off execute the same
guest, and that guarantee is what every measurement below leans on.

**Performance: honest and uneven.** The LC II gains ×1.6 under the JIT.
The Sonora and VASP machines currently LOSE under it, and the reason is
the fifth pass's lesson operating at full force: their System 7.5 boots
run with the 030 MMU enabled, the 22-entry ATC churns, and every eviction
kills the code window it backed (it must — that is the bit-exactness
contract), so the engine pays arm/probe cycles for windows that die young.
An arm-failure backoff (32 interpreted instructions after a refused probe)
claws back part of it — LC III 558 s -> 528 s — but the structural fix is
cheaper probes and cheaper arming, not more retries. The JIT stays opt-in
per machine, the interpreter stays the default, and nothing regresses when
the engine is off.

All timing in this pass was taken on a heavily loaded host (load average
3-14); ratios within a run are meaningful, absolute numbers are not.

## 2026-07-28 (sixth pass) — The JIT reaches the 68030: the V8 family

The engine that drives the four 68040 machines now drives the 68030 V8
family — LC II, Classic II, Color Classic, Mac TV (and the LC constructs it
harmlessly: the probe refuses a 68020, so the window never arms there).
Gate: `lcii_boot_etalon` reaches the Finder on both engines — 248 s
interpreted, 156 s on the JIT (×1.6, measured under heavy host load; the
ratio is the trustworthy part) — and `jit_mactv_boot_etalon` guards the
phase-fragile Cuda transport: the window charges no guest cycles, and the
Mac TV would deadlock long before a signature failed if that ever changed.

The 030 seam is SMALLER than the 040's, because the 030 has a single fetch
choke point: `mmuFetchWord` serves the opcode, the lookahead and every
extension word, so ONE window hook covers what took three sites on the 040.
The rest mirrors the 040 pattern exactly:

* `pomJitExecOne` dispatches on the model — `mmuExecuteStart` plays the
  role `mmu040InstrStart` plays, the dispatch tail is shared, and the
  engine drives either core without knowing which it holds;
* `pomJitProbeCode` gained the 030 branch: TT registers (OK-match only),
  TC.E-off identity, then a read-only scan of the 22-entry fc-tagged ATC —
  page sizes from 256 bytes to 32 KB, and the window machinery was already
  size-agnostic;
* the four 030 ATC flushes bump the generation, and both 030 eviction
  sites got the `pomJitAtcEvict` hook — the U-bit lesson of the fifth pass
  was learned once and applied everywhere;
* `V8Memory` grew `codeSpan`/`dataSpan`/guard plumbing. Two V8-specific
  subtleties: the fixed 2 MB alias means one RAM byte carries TWO bus
  addresses, so the write guard notes both views; and the pseudo-VIA can
  REWRITE the bank mapping at any time (`applyRamConfig`), which is an
  address-map move the guard is now told about.

The icache overlay keeps charging its miss penalty before the window is
consulted, so 030 cycle accounting is untouched — same invariant as the
040: the window is a pure host-side saving.

Remaining for the other 030 families (Sonora, VASP, RBV) and the 020s: the
Moira seam is DONE and shared; each family needs only its memory map's
codeSpan/dataSpan/guard plumbing and the wrapper's engine member — the
V8Memory diff is the template.

## 2026-07-28 (fifth pass) — The "PGO divergence" was the U bit all along

The open issue from the fourth pass — the x86-64 backend diverging under a
PGO build — is closed, and the diagnosis is worth its length because every
step of it pointed somewhere else first.

**The hunt.** Lockstep placed the divergence at ~6.59 G cycles, fully
developed within one 4 096-cycle window. Then the eliminations, each by
measurement: not the generated code (block dumps identical between builds);
not PGO (excluding the backend from profile-use *changed* the wrong
fingerprint instead of fixing it); not the block machinery (`BLOCKS=0`,
thunks off, `HOT=1000000` — all still diverged, each with a DIFFERENT
fingerprint); not the JIT at all (the plain INTERPRETER diverged on the same
binary); and not undefined behaviour (valgrind, track-origins, across the
corruption window: 0 errors).

**The actual mechanism.** The vendor doc had documented an "architecturally
invisible" divergence: a fetch served from a window skips the ATC lookup the
interpreter would have done. The missing half: a window or data-TLB entry
could OUTLIVE the ATC entry it was derived from. Once the backing entry was
evicted, the interpreter re-walks on its next access — and a walk WRITES the
descriptor's U bit. Mac OS VM (8 KB pages, active once the System is up)
READS those U bits for page aging. So each engine — skipping a different
subset of walks — steered the guest's own paging decisions differently:
three engines, three different-but-each-internally-valid executions. Every
rebuild or env change reshuffled the pattern, which is exactly what memory
corruption looks like from the outside.

**The fix (`Moira::pomJitAtcEvict`).** Derived state dies with its ATC
entry: both eviction sites (replacement in `mmu040AtcFill`, the write-M
invalidate in `mmu040AtcLookup`) kill the derived TLB slices, the hot pair
and the code window for that page, in O(slices). A window hit now implies
the interpreter would have ATC-hit too — same walks, same U bits, same
guest, bit for bit. Verified: interp, interp+window, `threaded` and `x86-64`
all print fingerprint 8f26fcba22986fc6 at identical cycle counts over 20 G
cycles. The full smoke tier is green.

**The honest cost.** Capped at the ATC's 32-page coverage, the
INTERPRETER's data window stopped paying for itself — under VM the
eviction/refill churn exceeds what the surviving hits save — so it is now
opt-in (`POM68K_DATA_WINDOW=1`), which also hands invariant 3 back its
byte-identical default interpreter. The x86-64 backend keeps its inline TLB
(same cap, but it replaces a C++ call chain rather than a hot MRU probe).
Engine timings from the fix session are struck: the host was at load
average 14. Re-measure idle before quoting anything.

## 2026-07-28 (fourth pass) — The data window and PGO: the interpreter's turn

Two changes aimed at POM68K as a whole rather than at the JIT — and between
them they moved the needle more than everything before combined.

**PGO (`-DPOM68K_PGO=generate` / `use`, see README).** An interpreter is
dispatch and branches, which is precisely what profile-guided optimization
predicts. Training = booting the Quadra 605 to the Finder once per engine.
Measured alone: interpreter −33 %, JIT −18 %, bit-identical emulation.

**The data window (J3).** The fetch window took instruction fetches off the
ATC/translate/virtual-decode chain; this takes DATA accesses off it, for
both engines at once: `mmu040Read`/`mmu040Write` consult the JIT's data TLB
before the long path. The fill door and every refusal rule stay in
`jit::Engine::fillDtlb` — plain RAM/ROM/VRAM only, resident and permitted
translations only, never a page holding translated code — and the engine
binds the callback whether or not the JIT is enabled, because the DEFAULT
engine is exactly who benefits.

Three findings along the way, each measured the hard way:

* **The first cut lost 15 %.** It flushed the whole TLB on every privilege
  change (entries carried no privilege), and several thousand interrupts a
  second emptied it faster than it filled. The privilege now rides in tag
  bit 31 — user and supervisor entries coexist, nothing flushes on an RTE,
  and the generated code folds the bit in at compile time since a block's
  privilege cannot change.
* **The second cut still lost 15 %.** Consulting the 8 KB table from the
  interpreter added an L1 miss to accesses the long path served out of hot
  machine state. The interpreter now goes through a 16-byte-per-direction
  level 0 sitting next to the fetch window in the object's hot zone, with
  the table as level 1 — copy loops are one read page plus one write page,
  which is exactly what two entries catch.
* **And the real villain was neither.** `fillDtlb` refused anything but
  4 KB pages ("the 68040 boots with 4 KB on every Mac") — false the moment
  the System arms paging, which uses 8 KB pages on the 040. Probe results
  are transient by design, so the refusal was never remembered: once the
  MMU was up, EVERY data access paid a call, a probe and a refusal — a
  constant ~7 s tax across engines. An 8 KB page now fills as two 4 KB
  slices, and the sign flipped everywhere at once.

**Measured** (fixed-cycle harness; every number bit-exact against the
original interpreter's fingerprint):

| Quadra 605 + Mac OS 8.1, 5 G cycles | wall | vs pre-everything interpreter |
|---|---|---|
| interpreter, before all of this | 41.9-42.6 s | — |
| interpreter + data window | 37.6 s | ×1.13 |
| interpreter + data window + PGO | **24.5 s** | **×1.73** |
| JIT `threaded` + window + PGO | **10.5 s** | **×4.0** |
| JIT `x86-64` + window + PGO | 6.8 s | ×6.2 |

Boot to the 256-colour Finder, end to end: **60.0 s → 19.5 s (×3.1)** on the
default backend. The full smoke tier is green on both the plain and the PGO
build, and the lockstep gates pass at both comparison resolutions.

**One open issue, contained:** the x86-64 backend under PGO (and only under
PGO — plain build and `threaded`-PGO are bit-exact) diverges from the
interpreter somewhere between 5 G and 20 G cycles, reproducibly. It still
reaches the correct Finder signature, but it is not bit-exact, so it stays
non-default and the hunt is logged in TODO. The ×4.0 default path does not
depend on it.

## 2026-07-28 (third pass) — The density work, and what it finally measured

Three changes to the x86-64 backend, aimed at the finding of the previous
entry (footprint, not coverage), plus the experiment that decided what the
constraint actually is.

**The experiment first.** Varying only the compilation threshold — that is,
only how much code gets generated — over identical guest work:

| `POM68K_JIT_HOT` | blocks compiled | native coverage | 20 G cycles |
|---|---|---|---|
| 8 | 120 648 | 77.7 % | 175.6 s |
| 64 | 54 525 | 73.7 % | 171.7 s |
| 512 | 17 346 | 65.8 % | 166.8 s |
| 4096 | 1 239 | 50.0 % | 155.0 s |

Monotone, and the opposite way round from the intuition: coverage falls from
78 % to 50 % and the machine gets 12 % faster. **Compiling less is faster.**

Solving for the per-path cost at two operating points puts a number on it:
generated code costs **~60 ns per guest instruction against the interpreter's
~44 ns through the fetch window**. It is not the footprint — at the 4096
threshold the whole code cache is ~2 MB and the ratio does not move. The
generated code is simply more expensive per instruction, and the reason is
not its length but its shape.

**What was done.**

1. **pc and pc0 deferred to the cold exit stubs.** Nothing inside a block
   reads them; only the exception, trace and interpreter paths downstream of
   an exit do. −20 bytes per compiled instruction. The prefetch queue could
   NOT follow them, and that is worth writing down: `ird` at a boundary is
   the opcode of whatever ran last, and an instruction a backward branch
   jumps to is reached from two different predecessors, so one cold stub
   cannot carry both answers. Guessing cost a divergence 800 million cycles
   into a boot. A second, subtler one came with it: the write-guard exit
   inside a thunked store was the one path out of a block that never passed
   through a boundary stub — it had been getting away with it only because
   pc was already correct there.
2. **The peripheral-sync call moved out of line.** It fires roughly once
   every thirty-six instructions and was sitting in the hot path.
3. **The cycle clock lives in a callee-saved register** for the whole chain
   of linked blocks, spilled around anything that calls out. This is the one
   that paid: the charge used to store the clock and the next instruction's
   budget guard loaded it straight back, putting a store-to-load forward on
   the critical path of every single emitted instruction. 17.9 s -> 16.9 s
   on the loading phase by itself.

**Where that leaves it** (fixed-cycle harness, same fingerprints throughout):

| Quadra 605 + Mac OS 8.1 | 0.83 G cycles | 5 G cycles | 20 G cycles | boot -> Finder |
|---|---|---|---|---|
| Moira interpreter | 6.41 s | 42.45 s | 215.77 s | **60.05 s** |
| JIT `threaded` (default) | 3.48 s x1.84 | 17.33 s x2.45 | **131.77 s x1.64** | **29.02 s x2.07** |
| JIT `x86-64` | 3.48 s x1.84 | **16.93 s x2.51** | 150.9 s x1.43 | 32.01 s x1.88 |

The default threshold moves from 64 to **512**, which is where the curve
above says the generator should sit.

**The honest verdict on the code generator.** It is correct — bit-exact
against the interpreter on registers, stacks, clock and low RAM, over
hundreds of millions of instructions, across five gate configurations — and
it is worth about 3 % over the fetch window on guest code that is executing
a program, and nothing at all on guest code that is idling. Three rounds of
coverage work (40 % -> 66 % -> 70 % -> 74 %) and one round of density work
moved the wall clock by single-digit percentages each time.

What that says is that the remaining cost is per-instruction *shape*, and
the shape is dominated by the contract every emitted instruction has to
honour: two guard branches, `POLL_IPL`, the cycle charge with its pacing
test, and — on any memory operand — an inline TLB probe with two more
branches. Five conditional branches and a handful of dependent memory
accesses per guest instruction, against an interpreter whose handler is a
straight run of well-predicted, cache-resident code. Closing that needs the
contract itself to change (block-level guards instead of per-instruction
ones, a cheaper translation path), not more instruction coverage and not a
register allocator — which would save about one byte per operand and attack
a latency that is not the bottleneck.

## 2026-07-28 (later) — Block linking, and LINK/UNLK/NOP out of the exclusion list

Two changes aimed at the one number the J2 entry below could not explain:
compiled blocks were running **five instructions per entry** once the Finder
was up, so the cost of *entering* a block was being paid every five
instructions.

**Block linking.** A block exit whose target is a compile-time constant —
branch taken, branch not taken, running off the end of the recorded line —
now looks the target up in a direct-mapped table and jumps straight into the
next compiled block, past its prologue. Four instructions instead of a
return to the engine, a hash lookup and a fresh frame. A table rather than
patched jumps on purpose: a boot evicts blocks **237 000 times**, and
un-patching every jump INTO an evicted block means incoming/outgoing link
lists that can dangle; invalidating one table slot is O(1), exact (a block's
slot is a function of its pc) and cannot dangle. The tag is `pc | super` —
pc is always even, so the privilege bit rides in bit 0 for free.

**`LINK`, `UNLK` and `NOP` are no longer `Unsafe`.** The classifier excluded
the whole `$4Exx` group with the note that "LINK/UNLK/NOP are harmless but
rare enough that keeping this whole group out costs nothing". The census
says 3.6 % of a real Mac OS workload, sitting at **every function entry and
exit** — which is exactly where straight-line code begins. They transfer no
control and touch no SR/MMU/cache state; they are now compiled.

**Measured** (same fixed-cycle harness, same fingerprints):

| Quadra 605 + Mac OS 8.1 | 0.83 G cycles | 5 G cycles | 20 G cycles |
|---|---|---|---|
| Moira interpreter | 6.41 s | 42.59 s | 215.77 s |
| JIT `threaded` (default) | 3.48 s ×1.84 | 17.33 s ×2.46 | 131.77 s ×1.64 |
| JIT `x86-64` | 3.48 s ×1.84 | **16.75 s ×2.54** | 180.6 s ×1.19 |

Boot to the 256-colour Finder end to end: **60.05 s interpreted, 29.02 s on
the JIT — ×2.07.**

On the System-loading phase the generator now **passes the fetch window**
for the first time (18.4 s -> 16.75 s against the window's 17.33 s): block
entries fell 53 % there, from 2.41 M to 1.14 M, and blocks run 566
instructions per entry instead of 268.

**It still loses once the Finder is up, and the reason has moved.** Blocks
there went from 4.9 to 6.5 instructions per entry only — linking helps less
because those exits MISS: their targets are `JSR`/`RTS`, still `Unsafe`.
But native coverage is 66 %, barely under the loading phase's 70 %, so
coverage is not the explanation either. What is left is footprint: 47 000
compiled blocks at ~1.5 KB each is 70 MB of generated code. Both engines
slow down in that phase — the interpreter drops from 37 to 23 M instr/s —
and generated code drops further, because an interpreter's hot handlers stay
in L1 whatever the guest does and a code generator's do not.

Opening the framebuffer to the inline data path (all four 68040 maps —
`read8`/`write8` treat that window as bytes and nothing else) changed
nothing measurable, and that is itself the finding: the instructions that
draw are not compiled at all. QuickDraw's blitters are `MOVEM` and 68020
indexed addressing modes, neither of which this backend covers.

So the remaining order is: **density** (~150 bytes of host code per guest
instruction, most of it the per-instruction contract rather than the
operation — the boundary state is only read when a block exits, so it
belongs in the cold stubs), then **`JSR`/`RTS`/`BSR`** (7 % of a real
workload, and every one is both an interpreter round trip and a link that
misses), then `MOVEM` and the indexed modes.

**Follow-up the same day: `JSR`, `BSR` and `RTS` compiled too**, as block
terminators with the link table handling their targets — `RTS` and `JSR (An)`
compute the slot index at run time instead of folding it. Native coverage on
a full boot went 66 % -> 73.7 %, the share still running on the fetch-window
path 25.4 % -> 17.3 %, and blocks 6.5 -> 7.9 instructions per entry.

**And the wall clock barely moved** (173.3 s against 177.3 s; the loading
phase is a wash with the window at 17.4-18.5 s either way). That is the
third coverage increase in a row — 40 %, 66 %, 70 %, 74 % — with no
corresponding speed-up, and it is now the measurement rather than a
suspicion: **the binding constraint is the footprint of the generated code,
not which instructions it covers.** Subtracting the interpreted share, the
native path runs at ~15.8 M instr/s, which is ~250 host cycles for a
sequence of about thirty emitted host instructions. That ratio is only
explicable as instruction-cache misses: 54 525 compiled blocks at ~1.5 KB
each is 82 MB of code that the Finder's working set walks over.

So the next work is density, and it is not a tweak — it is the emitter's
calling convention. ~150 bytes per guest instruction today, of which the
operation itself is a small part: the boundary state (pc/pc0/prefetch queue)
belongs in the cold exit stubs because it is only read when a block exits,
the cycle clock belongs in a callee-saved register, the inline TLB probe is
45 bytes on its own, and — the one thing every serious JIT does that this
one does not — the guest register file is hit in memory for **every**
operand instead of being allocated into host registers across a block.

**The disk image is clean again** — `q605_boot_etalon` and
`jit_q605_boot_etalon` both pass, with byte-identical output. The full smoke
tier is green: 8/8.

## 2026-07-28 — The x86-64 code generator (J2), and what it measured

The JIT's second backend now emits **host machine code**:
`src/jit/backends/JitBackendX64.cpp`, with `X64Asm.h` beneath it as a pure
encoder that knows nothing about the 68k. It is bit-exact against the
interpreter and it is **not** what `auto` selects — the measurement below is
why, and that is the honest headline of this entry.

**What it compiles.** `MOVE`/`MOVEA`, the `ADD`/`SUB`/`AND`/`OR`/`EOR`/`CMP`
families in both directions, `ADDA`/`SUBA`/`CMPA`, `ADDQ`/`SUBQ`, `MOVEQ`,
the six immediates, `TST`, `CLR`, `NEG`, `NOT`, `EXT`, `SWAP`, `LEA`,
`BTST`, and — the reason a code generator is worth having here —
`Bcc`/`BRA` as block *terminators*: their target is a compile-time constant,
so a backward branch into the block it belongs to becomes an internal jump
and the loop never returns to the engine. Addressing modes `Dn`, `An`,
`(An)`, `(An)+`, `-(An)`, `d16(An)`, `(xxx).W`, `(xxx).L`, `d16(PC)` and
immediate. Everything else falls back per instruction. On the System-loading
phase of a Mac OS 8.1 boot that is **70 % of executed instructions**.

**Three rules the design is built on.** (1) No C++ exception may cross
generated code — there is no unwind information for bytes we emitted
ourselves, so every call out goes to a `noexcept` thunk reporting failure by
return value, and every failure is taken at an instruction boundary with
nothing committed. (2) Guest registers stay in memory: the 68k leaves the
upper bits of a destination alone on byte and word operations and x86's 8-
and 16-bit forms have exactly that semantics on a memory operand, so there
is no masking and no register allocator, and no bail-out can strand a live
guest value in a host register. (3) Cycle counts are **checked, not
trusted** — the tables are transcribed from Moira's own `CYCLES_*` (68020
column) and an instruction is compiled only if the table agrees with what
the tracer measured when it actually ran it, which is also what
automatically excludes every access whose cost depends on a device wait
state.

**The data path.** Generated code cannot call `mmu040Read` (it throws), so
data addresses translate inline against a new 64-entry direct-mapped TLB in
Moira, filled only through `jit::Engine::fillDtlb` — which refuses anything
needing a page-table walk or an M-bit write-back, anything that is not plain
RAM/ROM, and any *write* to a page holding translated code. Refusals are
cached (a null host pointer), because a hardware poll loop would otherwise
pay a call per iteration to be told the same thing, and such a loop turns
out to be **71 % of all instructions executed** during this boot. What the
TLB refuses goes through a per-access thunk rather than handing the whole
instruction back, which is what took native coverage from 40 % to 77 %.

**Measured** (`tests/jit_bench.cpp`, a new dev harness: a FIXED guest-cycle
budget, so both engines are timed over identical work — a boot etalon stops
when it recognises the Finder and therefore times the two engines over
different amounts of it. All rows print the same architectural fingerprint.)

| Quadra 605 + Mac OS 8.1 | 0.83 G cycles | 5 G cycles | 20 G cycles |
|---|---|---|---|
| Moira interpreter | 6.42 s | 42.44 s | 216.80 s |
| JIT `threaded` (default) | **3.48 s ×1.84** | **17.89 s ×2.37** | **127.25 s ×1.70** |
| JIT `x86-64` | 3.49 s ×1.84 | 18.52 s ×2.29 | 223.4 s ×0.97 |

**Why the code generator loses, and what fixes it.** Blocks run 284
instructions per entry while the guest is loading the System — loops closing
on themselves inside generated code, exactly as intended — and **4.9** once
the Finder is up. Finder-era 68k is branch-dense enough that a block is five
instructions, so a block *entry* (hash lookup, frame, prologue, epilogue) is
paid every five instructions. **Block linking** — a branch jumping straight
into the next compiled block instead of returning to the engine — is the one
thing missing, and nothing else is worth doing before it. Density is the
other half: ~150 bytes of host code per guest instruction, most of it the
per-instruction contract rather than the operation. `auto` therefore keeps
`threaded`; `POM68K_JIT_BACKEND=x64` selects the generator, and
`src/jit/JitBackend.cpp` now states in the registry that this is a *measured*
choice and not a capability ranking.

**Four bugs worth recording, because each was invisible to the gate that
should have caught it.**

* **The lockstep gate never left the power-on self test.** It did not
  release the Cuda's reset hold, so it spent its whole budget where every
  memory access is an I/O register: it reported the JIT's data path green
  over 768 million cycles having never performed a single data-TLB fill. It
  now releases the hold, attaches the boot disk read-only to both machines,
  and compares **the first 2 KB of guest RAM** as well as the registers — a
  JIT bug in a store shows up in a register only much later, when something
  reads the byte back.
* **A missing REX prefix.** Byte operations whose r/m operand is RSI/RDI
  address AH/BH without REX and SIL/DIL with it. `cmp dil, 4` was assembling
  as `cmp bh, 4` — a comparison against the high byte of the *Moira object
  pointer*. It surfaced as `cmpi.b #4,CPUFlag` deciding the machine was not
  a 68040, a hundred million instructions into the boot.
* **`BTST` computed its bit mask before fetching the operand.** A memory
  access is not three instructions — it is a TLB probe, possibly a call to
  fill it and possibly a call to perform it — and it treats every
  caller-saved register as scratch. The mask did not survive.
* **Generated code advanced `clock` without going through `sync()`.** The
  CPU wrapper hands out peripheral time from there, so blocks ran the guest
  forward with the VIA, the ASC, SWIM and the Cuda MCU frozen behind them.
  The pacing test is now inlined (the wrapper's own batching condition), so
  the call happens only when peripheral time is actually due.

**Invalidation: evict, do not flush.** A write into memory a block was
translated from used to drop the entire cache. With generated code in it
that is ruinous — one boot phase took **5 313** such flushes and the engine
spent its life re-translating what it had already translated. The write
guard went from 4 KB granularity to 256 bytes (68k code and its data share
pages constantly), `serviceGuard()` now evicts only the blocks overlapping
the written slices, and a `slice -> blocks` index makes that cost
O(blocks in the slice) instead of O(cache) — without the index, servicing
1.37 million trips on a full boot took 436 s on its own. Same phase, after:
**27** flushes.

**Gates.** `jit_backend_test` now also pins the code generator's coverage
and the classifier's branch rules. `jit_lockstep_test` gained three
registrations that name the x86-64 backend — one at 256 cycles per
comparison (long blocks and internal loops), one at a single cycle (the
sharpest check there is), one with the per-access thunk disabled — plus a
`POM68K_JIT_LOCKSTEP_FINE_AT` mode that drops to instruction resolution near
a known divergence and prints the last eight instruction boundaries, because
a divergence is almost never at the pc it is noticed at. `make -j4 jitdev &&
ctest -L smoke` covers all of it.

**Known, unrelated:** `q605_boot_etalon` fails on both engines with an
identical signature — Mac OS 8.1 puts up its "this computer may not have
been shut down properly" alert, which is modal and blanks the menu bar the
gate measures. The disk image needs the alert dismissed once (or a clean
re-image); nothing in this entry changes it, and both engines produce
byte-identical output.

## 2026-07-27 — A second execution engine: the multi-target JIT (J0 + J1)

POM68K now has **two** CPU engines. The Moira interpreter stays the default
and the reference everywhere — GUI, headless, all CTest gates — and the JIT
sits **beside** it, selected from a new **CPU** menu (or
`POM68K_CPU_ENGINE=jit`). Wired on the four 68040 profiles: Quadra 605 /
LC 475 / LC 575, Centris + Quadra 610/650/800, Quadra 630 / LC 580,
Quadra 700. Design and invariants: `src/jit/POM68K_JIT.md`; the five-point
extension inside the vendored core: `extern/moira/POM68K_VENDOR.md` §
*JIT seam*.

**Multi-target from the first line, not as a later refactor.** POM68K is
multiplatform, so an x86-only JIT would make the emulator behave differently
depending on the host and would turn every further architecture into a
rewrite. The engine is therefore split at a hard boundary: `jit::Engine`
(blocks, code window, invalidation, fallback policy, gauges) knows nothing
about the host; `jit::Backend` is where an architecture lives. The
`threaded` backend generates no code at all and is **always** compiled and
always usable, so `POM68K_JIT_BACKEND=auto` cannot fail to produce a working
engine — Emscripten, a hardened kernel that refuses executable pages, an
architecture nobody has written a code generator for. `jit::CodeBuffer`
already implements portable W^X memory (mmap/VirtualAlloc, `MAP_JIT` plus
the per-thread write-protect toggle on Apple silicon, explicit i-cache
invalidation off x86) for the generators to come, and
`src/jit/backends/JitBackendA64.md` was written now, before any generator
exists, to check the IR against a *second* architecture — which is why the
IR spells out CCR semantics (x86 CF and AArch64 C disagree on the carry of a
subtraction; the 68k X bit exists on neither) and byte order explicitly
instead of leaving them to whatever the host happens to do.

**Where the time actually was.** On the 040 Moira has no prefetch queue:
`mmu040InstrStart` fetches the opcode *and* the `pc+2` lookahead through the
MMU on **every instruction**, and `readExt` does the same for every extension
word. Each of those is an ATC probe, a virtual `read16()`, and the machine's
whole address decode. The **code window** replaces that, for instruction
fetches only, with a bounds check and a load from a host pointer into the
guest RAM/ROM buffer. Two facts made this cheap and safe: on `Core::C68020`
Moira's `SYNC(x)` expands to nothing, so the window changes **no** cycle
accounting whatsoever; and the window points into the live guest buffer, so
self-modifying code stays correct with no invalidation at all.

**Blocks are recorded by executing them.** No second 68k decoder: the engine
runs instructions through `Moira::pomJitExecOne()` and notes where each one
started and how far the pc moved, so instruction lengths fall out of pc
deltas and a recorded block cannot describe something that did not happen.
Replay re-checks the cycle budget, `flags == 0`, and that the pc is where the
trace said. That last check is the one that matters: `execException` sets no
`flags` bit, so a `TRAP`, a `CHK` or a divide-by-zero completes with
`flags == 0` and the pc already in the handler — a replayer testing only
`flags` would happily run the rest of the block at the wrong address.

Three non-obvious rulings, each of which would have been a silent corruption:

- **The translation probe must not translate.** Arming the window needs
  logical → physical, but `mmu040Translate` writes the descriptor U/M bits
  back into guest RAM through `mmuWrite32` and faults by throwing — neither
  of which may happen *between* instructions. `pomJitProbeCode` is therefore
  a read-only TTR match + MMU-disabled identity + ATC scan, and a miss simply
  declines to arm: the interpreter then fetches normally, fills the ATC, and
  the next probe succeeds.
- **The overlay flip is a *read* side effect.** `Q605Memory::read8` clears the
  boot overlay on the first `$4xxxxxxx` access, remapping a whole gigabyte —
  no byte is written, so no write guard can see it. `codeSpan()` refuses every
  span while the overlay is up (a `const` probe cannot perform the clear), and
  the flip sites call `jitMapChanged()` directly.
- **An address range cannot detect a stale translation.** `PFLUSH` and writes
  to TC/URP/SRP/TTR change no address and set no flag. `Moira::pomJitMmuGen`
  is bumped by every ATC flush and TTR write, recorded when the window is
  armed, and compared on every fetch.

**Gates** (`ctest`): `jit_backend_test` (backend registry, W^X buffer,
classifier safety rules — no assets, runs anywhere) and `jit_lockstep_test`,
the decisive one — two Quadra 605 machines built from the same ROM, one
interpreted and one JIT-driven, compared on D0-D7/A0-A7/PC/SR/USP/ISP/MSP and
the clock at **every instruction boundary**, and failing if the JIT never
actually replayed a block, so a silent fallback cannot masquerade as a green
gate. Plus four `jit_*_boot_etalon` twins: the same etalon executables re-run
with `POM68K_CPU_ENGINE=jit`.

**Measured, on `q605_boot_etalon` (same host, same load, boot to the 256-colour
Finder):**

| engine | wall clock | vs interpreter |
|---|---|---|
| Moira interpreter (default) | 58.7 s | — |
| JIT, **fetch window only** (default) | **27.6 s** | **−53 %**, ×2.13 |
| JIT, window + block cache | 47.9 s | −18 % |
| JIT, no window (engine overhead alone) | 62.1 s | +6 % |

So the window is the whole win, and **the block cache is a net loss** — it is
therefore **off by default** (`POM68K_JIT_BLOCKS=1` turns it on). The reason is
structural, not a tuning failure: real 68k code is branch-dense, recorded
blocks average **1.04 instructions**, and a hash lookup plus a trace attempt at
every branch costs more than a one-instruction replay saves. The gauges make
it plain — over a million instructions of boot ROM, the window alone hands
**246** instructions back to the interpreter; with blocks on, it hands back
**633 692**, one per branch. The machinery
stays because it is precisely what a code generator needs — block boundaries,
an IR, `caps()`-driven fallback — and `jit_lockstep_blocks_test` keeps it
honest. The last row is the zero point: the engine's own dispatch overhead,
with the window switched off, is +6 %.

And the same speedup on all four 68040 machines, measured inside one
`ctest` run (each etalon against its own `jit_` twin, same disk image, same
Finder signature):

| gate | interpreter | JIT | |
|---|---|---|---|
| `q605_boot_etalon` | 65.3 s | 28.1 s | **×2.32** |
| `centris650_boot_etalon` | 303.9 s | 178.2 s | ×1.71 |
| `q630_boot_etalon` | 82.8 s | 44.4 s | ×1.87 |
| `q700_boot_etalon` | 319.4 s | 174.3 s | ×1.83 |

Full suite: **104/104 green**, the 97 pre-existing gates plus the 7 new ones,
with the interpreter still the default everywhere.

Two defects found by an adversarial review pass and fixed before the gates
were declared green, both in the block path:

- **Use-after-free, critical.** A guest `MOVEC` to CACR reaches
  `Moira::setCACR` → `Cpu040::didChangeCACR` → `Engine::flushAll()` — from
  *inside* a replaying block, freeing the `BlockIr` the backend was iterating.
  `flushAll()` is now deferred while a block is in flight and serviced the
  moment it returns. This is the exact shape that would free live host code
  once the x86-64 backend exists.
- **Stale blocks across a remap, major.** A recorded block is a script of
  *logical* addresses; a `PFLUSH` or a write to TC/URP/SRP/TTR can point the
  same logical pc at entirely different code. The window already refuses on a
  generation mismatch, but nothing in a `(pc, super)` key could notice, so the
  block cache now tracks `Moira::pomJitMmuGen` and drops itself when it moves.

## 2026-07-27 — The Macintosh TV boots again: a 2 % MCU shift is a deadlock

The full 97-gate run turned up exactly one red machine: `mactv_boot_etalon`,
black screen, `SCSI commands 0` — wedged *before* the SCSI Manager. It was
not that day's pseudo-VIA work (reverting `PseudoVia.{h,cpp}` to HEAD
reproduced it bit for bit) but an in-flight change to the 6805 core.

Traced end to end. The 68030 spins at ROM `$40AB3BC8`, which is the tail of
the Cuda's VIA bit-bang transport: `move.b d2,SR` / `eori.b #$10,ORB`
(toggle BYTEACK) / `btst #2,IFR` / `beq` — it has loaded a byte into VIA1's
shift register and is waiting for the shift-register interrupt that says the
byte went out. It never comes. A per-edge trace of `Via6522::extShiftCB1`
shows why: on the byte that hangs, the Cuda delivers **7 of the 8 CB1 rising
edges** and then stops, waiting for BYTEACK — while the ROM waits for
IFR.SHIFT. Neither side moves again, and there is no timeout in that loop.

The trigger was `M68hc05::serviceInterrupts` starting to charge the 11
cycles a real 6805 burns on the push + vector fetch (`m6805.cpp:570`). That
is *correct* — and it is still a deadlock, because it costs the MCU ~2 % of
its instruction throughput against machine time and so shifts the **phase**
between the MCU's instruction stream and the host VIA. The Mac TV is where
that bites first: at 31.3344 MHz it is the fastest 030 in the tree, hence
the tightest MCU:CPU ratio. `CudaLle.cpp:20-28` already recorded that this
phase is load-bearing — two earlier attempts to move it crashed the guest.

Bisected to the cycle, not guessed: the one-second-timer clock model in the
same commit is innocent (reverting it alone still fails), and so is the
control flow — MAME's `execute_run` takes the interrupt and then executes an
instruction in the *same* iteration, and matching that shape exactly still
fails. Charging 0 vs 11 is the whole difference. A plausible robustness fix
(clearing the VIA's external bit counter when an ACR write changes the shift
mode, which a trace showed leaking one stray shift from a half-counted
shift-IN byte into the shift-OUT byte after it) did not change the outcome
either, so it was dropped rather than kept on speculation.

So the charge stays at 0: a deliberate, documented inaccuracy at the call
site, worth ~2 % of MCU rate against a machine that otherwise does not boot.
The run() loop keeps MAME's shape. The real fix is to make the transport
survive a phase shift — tracked in `TODO.md`, and re-landing the 11 is then
one line.

## 2026-07-27 — Three DAFB clock generators, the pseudo-VIA's second flavour, two GUI races

Fallout from an adversarial read of the tree (`.bughunt/`). Three fixes,
each one a case of **one model standing in for several devices**.

**DAFB: the +$300 window is three different chips.** `Dafb::clockgenWrite8`
implemented only Apple's "Gazelle" (`dafb_memcjr_device::clockgen_w`,
`dafb.cpp:1322`) and applied it to every owner. MAME has two more behind
the same address range, sharing nothing but that range: the **DP8534** on
djMEMC (`:1197` — a bitstream clocked into `$303` MSB-first and committed by
any write to `$313`, then read back as five *bit-reversed* bytes) and the
**DP8531** on the discrete DAFB of the Quadra 700 (`:884` — a nibble
register file at `$303,$313,…,$3F3`, latched by writing register 15). So
the Centris 610/650, Quadra 610/650/800 and LC 575 dropped their clock
programming on the floor and kept the 31.3344 MHz reset value, while the
Q700 was worse than silent: its DP8531 nibbles for **register 12 land
exactly on `$3C3`**, the Gazelle's serial port, where bit 1 reads as a clock
edge and bit 0 as data — 20 rising edges later `pixelClock_` was latched
from unrelated nibbles. Since `tick()` derives the frame length from
`htotal × vtotal × cpuHz / pixelClock_`, both classes had the wrong VBL
cadence. Now a `Dafb::Clockgen` ctor variant routes to the right decoder.
The ROMs prove the port: with `POM68K_DAFB_CLOCK_TRACE=1` the Centris 650
latches **30.26 MHz** and the Quadra 700 **25.175 MHz then 30.24 MHz** — the
textbook VGA and Apple 13" pixel clocks, out of two decoders that had never
been fed a correct bitstream before. Gate: `q605_dafb_test` (all three).

**PseudoVia: level was never universal.** IFR bit 4 (ASC) is
level-triggered on the V8 — the ack is a NOP — and POM68K hardcoded that
for every owner. MAME splits the device: `v8_pseudovia_device` (V8, Eagle,
Spice, Tinker Bell) and `sonora_pseudovia_device` get the level flavour,
but the **Mac IIsi/IIci** (`rbv.cpp:66`) and **Mac IIvx/IIvi**
(`vasp.cpp:90`) wire the *base* `pseudovia_device`, where only the 0→1
transition latches (`pseudovia.cpp:136-146`, guarded by `m_live_main_ints`)
and the write path carries no `~$10` mask. It matters on VASP especially,
which pairs the base pseudo-VIA with a **V8-flavour ASC** whose IRQ line is
itself level-sticky: with both level behaviours stacked, an enabled ASC
interrupt could never be acknowledged — the ack discarded, the level
re-applied after every RTE, the same shape as the "Bienvenue." livelock.
`PseudoVia::Flavour` now selects; `RbvMemory`/`VaspMemory` take Base.
(MAME's base `write` case `0x13` falls through to a bare recalc without
touching the IER — a MAME slip, not hardware: both flavours keep the
bit-7-selector form, or the RBV/VASP ROMs could not arm anything.)
Gate: `pseudovia_test` grew a base-flavour section; the four machines still
reach the Finder.

**Two GUI↔machine races.** `AtalkHub::snapshot()` ran on the GUI thread and
reached through `wire_()` into `Scc8530::rxBacklog(0)` — a `std::deque` the
machine thread pops from unlocked. The meters are now sampled in `tick()`
(machine-thread bound) and served from that copy under `mu_`; the file's
thread contract says so explicitly. And the LC II / LC III monitor-sense
buttons read `mem.monitorSense()` straight from the GUI thread — with a
comment claiming only the GUI writes it, when `Cmd::Sense` writes it on the
machine thread as a multi-field update (`vidSpram_`/`vidSpramSaved_`/
`montype_`). It now crosses as a `stSense_` atomic in `publish()`, like
every other machine→GUI value.

## 2026-07-25 — Macintosh Quadra 700: the 27th machine, and the DAFB TurboSCSI cell

The **first** Quadra — and the last 68040 desktop POM68K was missing that
needed no new co-processor. MAME calls it `spike_state`
(`macquadra700.cpp`): a full 68040 @ 25 MHz built from **discrete** chips,
*before* djMEMC/IOSB condensed them. That makes it a recombination of two
machines already in the tree rather than a bring-up: the **Mac II's** front
end (VIA1 + a real VIA2 6522, discrete 343-0042 RTC, PIC1654S ADB
transceiver — the same firmware LLE) on the **Quadra's** I/O block (DAFB
video, 53C96, SWIM1, EASC, SCC, SONIC) at the `$5000xxxx` layout the
Centris/Q800 already use. New: `Q700Memory`, `Q700Cpu`.

**The one genuinely new cell**: on this machine SCSI hangs off **DAFB**, not
IOSB — `docs/LLE_VS_HLE.md` §3 had recorded the DAFB TurboSCSI cell as
"absent, N/A on the Q605, only matters for a future Q700/Q950-class
profile". It is now modelled: DAFB register `$24` latches the four
wait-state selections (`dafb.cpp:480-530`) and reads back the **live DRQ in
bit 9**, with the registers at `+$0F000` and the pseudo-DMA at `+$0F100`
(bit-18 waitstated alias).

Other deltas from the Centris: a real 6522 for VIA2 with the Mac II
interrupt fan-in (slot/DAFB IRQs on CA1 + port A, ASC on CB1, SCSI on CB2,
SONIC on PA0, all active low), SWIM1 instead of SWIM2, EASC instead of the
IOSB ASC, 2 MB VRAM, no machine-ID longword (VIA1 PA reads `$C1` —
diagnostic disabled, the IIci lesson), and the 60.15 Hz tick generated
directly rather than through VIA2's T1 → PB7 → VIA1 CA1 chain (documented
simplification). **Boots Mac OS 8.1 to the Finder on the first run** (5063
SCSI commands, 640×480 DAFB). Gate `q700_boot_etalon`; GUI entry under a new
"Discret 040" group; CLI dispatch on ROM checksum `$420DBFF3`.

**Also: the compact-Mac brick turned out not to exist.** `docs/68K_FAMILY_
SCOPE.md` listed an "ADB-over-VIA compact MCU (not Egret — the SE/Classic
shift-register transcoder)" as the blocker for SE / SE FDHD / Classic /
SE/30. MAME's `mac128.cpp` shows the SE driving **`adbmodem`** — the very
PIC1654S POM68K already runs as firmware LLE (`m_adbmodem->set_via_state
((data & 0x30) >> 4)` on VIA PB4/PB5). See the next entry.

## 2026-07-25 — Macintosh Quadra 630 / LC 580: F108 + Valkyrie, the last 68k desktop board

"Show and Tell" (MAME `macquadra630.cpp`) is the Quadra 605 board cost-reduced
twice more: **DAFB replaced by "Valkyrie"**, a framebuffer with no CRTC to
program — you write a *video timing number* and it picks one of a handful of
hardwired modes — the SCSI hard disk replaced by **ATA/IDE**, and MEMCjr by
the **F108** memory controller (ROM/RAM switch + ATA + SCC + a cell "just like
a 53C96"). The I/O block is otherwise PrimeTime, so the machine is
`Q605Memory` with the video cell swapped: the VASP recombination pattern
again, and again it **booted Mac OS 8.1 to the 640×480×8 Finder on the first
run** (5299 SCSI commands).

New: `Valkyrie` (registers at PrimeTime +$2A000, RAMDAC at +$24000 with the
payload in the TOP byte, VRAM frame buffer at +$1000, stride = mode stride
<< depth index, VBL through `via2_irq_w<0x40>`), `Q630Memory`, `Q630Cpu`
(full 68040 @ 33 MHz; `POM68K_Q630_LC040` for the LC/Performa siblings).
The **ATA port is mapped but empty** — the ROM's IDE probe finds no device
and falls through to SCSI, where POM68K's disks live; PrimeTime II's
$5001A100 special-status register reports the VBL and ATA lines as MAME
does. The Cuda is the **341S0060** the LC 520 family already runs.

Gates `q630_boot_etalon` and `lc580_boot_etalon` (the $A55A225A identity on
the later $064DC91D ROM — it boots the same desktop, just slower, so the
gate gives it 30 000 frames). GUI group "F108 + PrimeTime II + Valkyrie",
CLI dispatch on the `$06684214` / `$064DC91D` checksums.

## 2026-07-25 — Macintosh SE, SE FDHD and Classic: three machines for one enum

The 28th, 29th and 30th profiles cost a `Model` enum on `MacMemory`, not a
new machine: `mac128.cpp`'s `macse_map` **is** the Plus map. What differs:

* **ROM size** — 256 KB (SE, SE FDHD) / 512 KB (Classic) instead of 128 KB,
  mirrored into the $400000 window and the boot overlay;
* **the overlay** clears on the first ROM access instead of on VIA1 PA4
  (`ram_w_se`); PA4 is the SE's *drive* select instead;
* **ADB instead of the M0110** — and the transceiver is the PIC1654S
  342S0440-B the Mac II / IIci / Quadra 700 already run as **firmware LLE**
  (`AdbVia` + `Pic1654s` + `AdbLine`), on VIA1 PB4/PB5 = ST, PB3 = /IRQ,
  CB1/CB2 = the shifter. No mouse quadrature — the mouse is an ADB device.

All three boot System 6.0.5 off a floppy to the Finder desktop, on the LLE
ADB path, through the existing IWM + Sony GCR + NCR 5380 stack. The
bring-up had exactly one long pause: ~350 frames of ADBReInit followed by
~500 frames of the 4 MB RAM test before the disk driver starts, which reads
as a wedge until you let it run. Gates `se_boot_etalon`,
`sefdhd_boot_etalon`, `classic_boot_etalon` (one binary,
`POM68K_COMPACT_MODEL`); GUI entries under the existing "68000" group, CLI
dispatch on the ROM checksums `$B2E362A8` / `$B306E171` / `$A49F9914`.
`POM68K_SE_VIA_TRACE=1` dumps the VIA1 accesses with the ST lines decoded.

**Still missing from the compact line: the SE/30** — no ROM dump on hand
(it is a IIx on a compact board, so the machine work is `MacIIMemory` +
compact video, not a new bus).

## 2026-07-25 — Quadra 800 (26th machine), the 040 boost ceiling lifted, and the PIC co-step un-boosted

Three follow-ups to the bus-time fix below, in the order they were found.

### The PIC1654S was being clocked from the boosted core clock

Audit of every `getClock()` reader that models **bus or wire time** (the
follow-up the previous entry asked for). The 040 machines were already
boost-aware everywhere (`Q605Memory`/`CentrisMemory` `viaSync` and
`syncSwimFromCpu` both divide by `cacheBoost()`, `Cpu040::stall` scales) —
so the earlier suspicion that `Q605Memory::viaSync` had the same bug was
**wrong**. But `AdbVia::syncTo(cpu_->getClock())` fed the **raw core clock**
into a fixed divisor (`kCyclesPerPicInsn`), so under any boost the PIC1654S
transceiver firmware ran `cacheBoost_`× fast — the same class as the
`mcuDebt_` MCU overclock. Harmless on the Mac II (Cpu020 has no overlay) but
**live on the IIci** the moment `RbvCpu`'s boost came back, and latent on
the Centris/Quadra. All four sites now pass `machineClock()`, and the
accessor exists on every CPU (`Cpu020`'s returns the clock unchanged) so
bus/wire models read one idiom.

### The Quadra's boost-1 pin was stale

`POM68K_Q605_CACHE_BOOST` had defaulted to 1 since Q8.8 with the note "boost
2+ fails SCSI bring-up". Re-measured: `q605_boot_etalon` passes at 2, 4 and
8, and the whole 040 family (Q605, LC 475, LC 575, Centris 610/650, Quadra
610/650, the three Cuda-LLE gates, DAFB, floppy, no-FPU and bare-FPU) is
green at boost 4. The only casualties were two unit tests measuring wait
states on the boosted clock — `swim2_test` ("SWIM access costs five cycles")
and `q605_turboscsi_test` (3-cycle register stalls) — both now read
`machineClock()` and are boost-invariant. **`Cpu040` and `CentrisCpu` now
default to `cacheBoost_ = 4`**, matching the 030 family: ~4× emulated
throughput on seven machine profiles.

### Macintosh Quadra 800 — the 26th profile

The cheapest machine left, and it landed as a fifth model of the existing
djMEMC + IOSB machine (`POM68K_CENTRIS_MODEL=q800`): same `F1ACAD13` ROM
already dispatched, full 68040 @ 33 MHz, VIA1 port-A ID pins **`$12`**
(pa1|pa4 — the only model of the family with pa6 clear, MAME
`macquadra800.cpp macqd800`). What the Q800 adds over the Centris is SONIC
Ethernet and three NuBus slots, neither of which the boot path binds: the
SONIC registers at `$5000A000` stay unmapped-0 like the rest of the IOSB
map, and only the **Ethernet address ROM** at `$50008000` needed modelling
(six MAC bytes + the inverted XOR check byte at +7, `ethernet_mac_r`).
Boots Mac OS 8.1 to the 640×480×8 Finder on the first run (5214 SCSI
commands). Gate `quadra800_boot_etalon`, GUI entry under djMEMC + IOSB.

## 2026-07-25 — The i-cache boost was accelerating the VIA bus: LC III / LC III+ / IIvx fixed, and the IIsi's boost restored

**Symptom.** A full-suite run found three red gates — `lc3_boot_etalon`,
`lc3plus_boot_etalon`, `iivx_boot_etalon`: black screen, **`SCSI commands
0`**, i.e. the machine never reached its disk. Reproduced serially, so not
test contention. The set was suspiciously exact: the **Egret-LLE machines
clocked ≥25 MHz**. The Cuda-LLE all-in-ones at the same clocks (LC 520/550/
CC II) passed, and so did every slower Egret machine (LC II and IIvi at
15.7 MHz, IIsi at 20 MHz — the last one only because it had already been
given `cacheBoost_ = 1`). `POM68K_CACHE_BOOST=1` made the LC III boot,
which pointed at the i-cache throughput overlay.

**Root cause — not the boost, but what the boost was allowed to touch.**
`Cpu030`/`SonoraCpu`/`VaspCpu`/`RbvCpu` run the core at `cacheBoost_`×
machine rate and scale peripheral ticks back down, so the guest's cached
code executes at a realistic 030 rate. But the **VIA E-clock alignment**
(`viaSync`) computed its target from `cpu_->getClock()` — the *boosted*
clock — and `stall()` charged the resulting wait in boosted cycles too. So
the 783.36 kHz E-clock quantum was spent in 1/`cacheBoost_` of its real
duration: every VIA-paced pulse came out **4× too short in machine time**.
The ROM's Egret transport acks each byte with a back-to-back `bclr`/`bset`
of VIA1 PB4 (via_full); at 15.7 MHz the compressed pulse still cleared the
2.097 MHz MCU's sampling, at 25–33 MHz it did not, and the transport wedged
after the first byte — no ADB, no boot. On real silicon an i-cache
accelerates **instruction fetch**, never a VIA bus cycle; the model was
accelerating both. (The 37 %-MCU-overclock fix of 2026-07-24, `mcuDebt_`,
removed the slack that had been hiding this.)

**Fix.** Bus time is charged in **machine cycles** on every 030 machine:
`stall(int cycles)` now scales by `cacheBoost_` when adding to the core
clock, and `viaSync` computes E-clock alignment from a new
`machineClock() = clock / cacheBoost_`. This is exactly the convention
`Cpu040` already documented and used ("wait states are specified in machine
cycles") — the four 030 CPUs had copied the pre-boost version. Touched:
`Cpu030.{h,cpp}` + `V8Memory`, `SonoraCpu.{h,cpp}` + `SonoraMemory`,
`VaspCpu.{h,cpp}` + `VaspMemory`, `RbvCpu.{h,cpp}` + `RbvMemory`.

**Two gates had been leaning on the wrong timing** and were fixed with it:
- `cclassic2_boot_etalon` sampled its "desktop weave" in the right-hand
  column, which at 512×384 is covered by an open Finder window on the
  reference volume — it read window interior (0.32, a hair under the band)
  and only passed before because the faster machine happened to be at a
  different point in the desktop draw. It now samples the strip *below* the
  windows (0.79), which is unambiguously desktop.
- `lcii_launch_etalon` steered the mouse **open-loop**, assuming "≤3-unit
  steps land 1:1 in pixels". That only holds while each ADB report carries
  ≤3 units; with reports coalescing, System 7's mouse scaling amplifies
  (~1.6× measured) and the run overshot the icon into the screen corner
  (pointer ended at 0,0 — the new diagnostic line prints it). It now steers
  **closed-loop** on the guest's own low-mem `Mouse` position, halving the
  remaining distance, which is immune to both the scaling curve and the
  report rate. Verified separately that delivery itself is healthy: a single
  3-unit move lands within one frame, exactly 1:1.

**Results.** All three gates pass at the **default** boost, with metrics
identical to their `POM68K_CACHE_BOOST=1` runs (LC III 9591 SCSI commands,
IIvx 1662). And the workaround the IIsi shipped with is **retired**: with
bus time honest, the IIsi boots at the shared default boost (2065 SCSI
commands) instead of being pinned to 1 — `RbvCpu`'s `cacheBoost_ = 1` line
is gone, so the IIsi and IIci get the same ~4× throughput as their
siblings. **Full suite: 90/90.** Follow-up worth trying: the Quadra's `POM68K_Q605_CACHE_BOOST`
has defaulted to 1 since "boost 2+ fails SCSI bring-up" — `Q605Memory`'s
`viaSync` has the same boosted-clock reading, so that ceiling may be the
same bug.

## 2026-07-25 — Doc sync + status pass: 25 machines, 90 gates, what is actually left

No code changed; this entry records the state the docs were re-synced to
after the RBV / Tinker Bell / 68030-Mac II round, and the honest reading of
what remains.

**Counted, not estimated.** `ctest -N` reports **90 gates** (CLAUDE.md and
README said 81; the TODO audit said 73). The GUI Machine menu carries **25
profiles** (`main.cpp kProfiles`), against "21" in
`docs/68K_FAMILY_SCOPE.md` and "15" in the TODO audit section.

**A full run on HEAD was 87/90 — three red gates, now root-caused and
fixed** (next entry): `lc3_boot_etalon`, `lc3plus_boot_etalon`,
`iivx_boot_etalon`.

**Machine menu grouped by platform** (`main.cpp`): the `Profile` table
gained a `group` field and the menu emits a `SeparatorText` per platform,
in the same seven-way split DEV.md now uses.

**The fan-out changed the shape of the risk.** Nine machines landed in two
days, but the number with a gate that exercises anything *past* the boot
signature did not move much: three — Mac II (`macii_mouse_etalon`),
Quadra 605 (`q605_cudalle_mouse/key_etalon`) and LC II
(`lcii_soak/persist/launch_etalon`). **22 of 25 profiles are proven only to
reach a Finder screenshot.** That is now the project's largest gap and the
TODO's "Test & validation depth" section was rewritten around it — adding a
26th machine is cheaper than hardening the 25 that exist, and the roadmap
should be read against that trade.

**LLE state, re-inventoried** (`docs/LLE_VS_HLE.md`, fifth pass): every ADB
machine POM68K ships now runs **real MCU firmware by default** — the
Egret/Cuda 68HC05 images on one side, and the **PIC1654S ADB modem** on
three families since this round (Mac II/IIx/IIcx, IIci, Centris/Quadra
610/650). `Egret.*`/`AdbBus.*`/the `AdbVia` byte-model survive only as
no-dump fallbacks. Two fidelity facts were added to the inventory: the 030
PMMU must not double-translate against the GLUE 24-bit remap, and the
i-cache throughput overlay is **not free** — it compresses a CPU-paced
bit-bang in a fixed-rate MCU's time domain (why `RbvCpu` ships with the
boost off). One new **open** entry was filed: §1.10
`POM68K_SCC_CLEANLINE`, an env knob that lets machine configuration decide
a wire condition — the same class §1.8 removed — needed today because Open
Transport waits forever on the standing no-peer abort.

**Docs touched**: `CLAUDE.md` (roster + counts + next machines),
`README.md` (LC 575, 25 profiles, gate count), `TODO.md` (stale "Mac TV
BLOCKED" and "remaining 040" entries closed; a table of remaining machines
with the ROM already in `roms/`; test-depth audit re-counted; per-machine
LLE scores re-scored), `DEV.md` (**restructured around seven machine platforms**, one reference
machine each — Plus, GLUE/NuBus, V8, RBV, Sonora, MEMCjr/PrimeTime,
djMEMC/IOSB — with every other machine folded in as a variant of its
platform; RBV, Sonora and djMEMC were promoted from "derived" to full
reference sections, and IIx/IIcx now sit under the Mac II, the V8 spread
under the LC II),
`docs/68K_FAMILY_SCOPE.md` (25 profiles, brick table re-verdicted, roadmap
reordered — Quadra 800 is now the cheapest remaining machine, sharing the
Centris `F1ACAD13` ROM), `docs/LLE_VS_HLE.md` (fifth pass).

## 2026-07-25 — Five more machines: Mac TV, IIsi, IIci, IIx, IIcx

New gates `mactv_boot_etalon`, `iisi_boot_etalon`, `iici_boot_etalon`,
`iix_boot_etalon`, `iicx_boot_etalon` (90 CTest total). The first three are
firmware-LLE MCU machines; the IIx/IIcx are 68030 Mac II variants.

### Mac IIx / IIcx (68030 on the Mac II GLUE board)

The Mac II FDHD with a 68030 (built-in PMMU + 68882) instead of the 020 —
same GLUE, same Toby NuBus video, the shared `mac2fdhd` ROM (`$97221136`).
Added as `MacIIMemory::Model {MacII, IIx, IIcx}` + a `Cpu020` `is030` flag
(`M68030`/`M68882`), distinguished only by the VIA machine-ID pins (IIx
VIA2 PB `$87`, IIcx VIA1 PA `$C1`). The one wall: on the 030, once the
ROM/System turns the CPU's **own PMMU** on (TC bit 31), Moira has already
translated logical→physical, so `MacIIMemory::physAddr`'s GLUE 24-bit remap
double-translated and the boot wedged mid-System (SCSI froze ~351 cmds).
Isolating with `POM68K_MACII_020` proved the FDHD ROM boots fine on the
020, pinning it to the 030; skipping the GLUE remap when the PMMU is enabled
(the same 020-HMMU-vs-030-PMMU split as `V8Memory`) boots both to the Finder
(SCSI 1158). Dispatch: `$97221136 → IIx` by default
(`POM68K_MACII_MODEL=iicx/fdhd` for the siblings); GUI menu entries added.

**Macintosh TV** — the EDE66CBD `$2000-$2003` probe (CHANGELOG 2026-07-24)
was the wrong ROM family. Real Mac TV (MAME `mactv` in `maclc.cpp`) boots
**`eaf1678d.bin`** (header `$EAF1678D`, CRC `0644f05b`) on the **Tinker
Bell** system ASIC (`TINKERBELL`/`v8tkbell` in `v8.cpp` — a Spice/V8
evolution: PA id `$84`, fixed 13" 640×480 sense `$06`, 8 MB RAM cap,
16 bpp mode), a **Cuda** MCU (341s0789 factory; the LLE runs the AIO's
341s0060) and a 68030 @ **31.3344 MHz** (C32M, no FPU). Rather than a new
machine, this became a `V8Memory::Model::MacTv` + a `spiceClass()` predicate
(Color Classic ∪ Mac TV: SWIM2 + Sonora EASC + Cuda) and a `cpuHz`
constructor parameter — the gate array stays in the C15M domain, the CPU
ticks are rescaled (the VASP pattern). Boots color first try; the archive's
"Macintosh TV" in the EDE66CBD filename was misleading.

**Mac IIsi** — a genuinely new machine (`RbvMemory`/`RbvCpu`/`RbvVideo`),
the first with **RBV** (RAM-Based Video — the pseudo-VIA + out-of-system-RAM
video that the V8/VASP/Sonora line descends from, MAME `rbv.cpp`). 68030 @
20 MHz, Egret 344S0100 LLE, SWIM1, discrete ASC, Bt478 CLUT (the Ariel
register model), framebuffer at the start of system RAM. Map from
`maciici.cpp maciisi`. One non-obvious wall: the IIsi ROM's Egret transport
is a tight **host-paced** bit-bang (acks each byte with a back-to-back
`bclr`/`bset` of VIA1 PB4/via_full), and the 030 i-cache throughput boost
compresses that pulse ~4× in the fixed-rate MCU's time domain — at boost 4
the Egret firmware misses the via_full low pulse and the transport wedges
after the first byte (the LC II/LC III exchanges are MCU-paced, so they
tolerate it). `RbvCpu` therefore defaults to **no i-cache boost**
(correctness over the boot-time hack). Boots the French System 7.5 Finder
to a 1-bpp B&W dither desktop.

Both wired into the CLI (checksum dispatch) and GUI (Machine menu):
Mac TV via `runLcII(Model::MacTv)`, IIsi via a new `runIIsi`
(`RbvMachine = SonoraStyleMachine<RbvMemory, RbvCpu, RbvVideo>`).

**Mac IIci** followed (gate `iici_boot_etalon`) as an `iici` flavor of the
same RBV machine — the IIsi's near-twin. It swaps the Egret for the **ADB
modem** (a PIC1654S transceiver, `AdbVia` firmware LLE + `roms/adbmodem/
342s0440-b.bin`) on VIA1 CB1/CB2 + PB4/PB5 and a **discrete 343-0042 RTC**
on VIA1 PB0-2/CA2 — the exact Centris/Mac II wiring — with no MCU
reset-hold (the 030 runs from power-on) and three empty NuBus slots.
68030 @ 25 MHz, ROM `$368CADFE`. One wall: MAME's `via_in_a` is
`0xC6 | BIT(config,1)`, and with diagnostic mode disabled (default) that
bit is 1, so PA reads **`$C7`**; feeding a bare `$C6` (PA0=0) sent the ROM
down the diagnostic burn-in path and it spun forever in the VIA-T2
calibration loop. Booted the French Sys 7.5 Finder once PA0 read 1.
`runIIsi` grew an `iici` parameter; dispatch on `$368CADFE`.

## 2026-07-24 — Phase C: Quadra 650 + Quadra 610 (full 68040 on the djMEMC+IOSB machine)

Two identity-variant siblings of the Centris, each the same
`CentrisMemory`/`CentrisCpu` djMEMC+IOSB machine but with a **full 68040**
(hardware FPU) instead of the Centris's 68LC040, and its own VIA1 port-A ID
pins: **Quadra 650** ($52, 33 MHz) and **Quadra 610** ($44, 25 MHz). The
harness became a four-way model selector (`POM68K_CENTRIS_MODEL` =
c650/c610/q650/q610; the Quadra rows flip `POM68K_CENTRIS_FPU` so `CentrisCpu`
builds the full `M68040`), the GUI menu gained both entries, and `runCentris`
selects all four. Both boot Mac OS 8.1 to the 640×480×8 Finder on the first
full run — the machine was already proven by the Centris, so the FPU + ID
were the only moving parts. Gates `quadra650_boot_etalon`,
`quadra610_boot_etalon` (81 total). Quadra 800 (adds SONIC + NuBus) is the
remaining `macquadra800.cpp` sibling.

## 2026-07-24 — Phase C: Mac Centris 650 + Centris 610 (djMEMC + IOSB, PIC1654S LLE)

A new machine family — the first with the **djMEMC + IOSB** two-ASIC I/O
(MAME macquadra800.cpp), a genuine bring-up rather than a rebadge.
`CentrisMemory`/`CentrisCpu` recombine parts POM68K already had: the Q605's
DAFB video, TurboSCSI 53C96, SWIM2, IOSB ASC and Quadra pseudo-VIA2, plus a
**discrete 343-0042 RTC** on VIA1 PB0-2/CA2 (the Mac Plus chip) and a
**PIC1654S ADB transceiver** on VIA1 PB3-5 + CB1/CB2 (the Mac II's `AdbVia`,
firmware LLE) — no reset-holding MCU, so the 68LC040 runs from power-on.
Model ID is strapped in VIA1 port A pins ($46 Centris 650 / $40 Centris 610;
the `$5FFFxxxx` longword is the fixed IOSB `$A55A2BAD`).

**The one real wall:** djMEMC maps a **2 MB** VRAM window ($F9000000-
$F91FFFFF) where the Q605's MEMCjr mapped 1 MB. POM68K's 1 MB VRAM
bus-errored at $F91FFFFC during the ROM's VRAM sizer → an unhandled
exception → the ROM serial monitor (the LC 520 signature). Mirroring the
1 MB VRAM across the 2 MB window cleared it, and the machine went straight
to loading Mac OS 8.1 off SCSI. (A harness stride bug — decoding at the DAFB
register stride instead of the PixMap `rowBytes` — tore the screenshot until
fixed; the machine itself was fine.)

Both **Centris 650** (68LC040 @ 25 MHz) and **Centris 610** (@ 20 MHz,
`POM68K_CENTRIS610=1`) boot Mac OS 8.1 to the 640×480×8 DAFB Finder. Gates
`centris650_boot_etalon`, `centris610_boot_etalon`. The GUI's Quadra machine
thread was generalized into a `DafbMachine<Mem, Cpu>` template shared by the
Quadra 605/LC 475 and the Centris (no change to the proven Quadra path); the
Machine menu gained the two Centris entries and the 1 MB ROM dispatch routes
header checksum `$F1A6F343`/`$F1ACAD13` to `runCentris`. 79 CTest gates.

## 2026-07-24 — AppleTalk moves in-process: node/router + AppleShare + LaserWriter + MacIP, one GUI window

The whole AppleTalk service side used to live *outside* POM68K: a
TashRouter process for DDP/RTMP/ZIP/NBP, netatalk's `afpd`/`papd` for
files and printing, and `macipgw` + a `tun` device + `iptables` for
TCP/IP — all bring-up by `tools/netatalk2/appleshare.sh` and
`tools/macip/macip.sh`, all needing root. That still works and stays
supported (it is the way to reach a *real* LocalTalk network), but a
stock POM68K now carries the entire stack itself, on the same
`Scc8530` LocalTalk wire the guest already drives — **no external
processes, no root**.

Four new core files, each one concern, all riding `AtalkStack`'s ATP
engine:

- **`AtalkStack`** — the in-process LLAP node + single-segment
  router-lite: DDP (short/long), RTMP beacon + request/response, ZIP
  GetNetInfo/GetZoneList (zone **"POM68K"**), NBP registry + LkUp
  answering and BrRq→LkUp relay, AEP echo, and an ATP engine that plays
  **both roles** (responder with an exactly-once cache + release timer;
  requester with retries — needed for the server-initiated ASP tickle /
  WriteContinue and PAP SendData). It defends its own node ID against
  the guest's ENQ probes.
- **`AfpServer`** — AppleShare over ASP: GetStatus/OpenSession, then an
  AFP 2.1 subset covering what System 6–8 Finders actually issue
  (Login guest/cleartext, GetSrvrParms, OpenVol/GetVolParms, Enumerate,
  GetFileDirParms, Set*Parms, Open/Read/Write/SetForkParms/CloseFork,
  Create/Delete/Rename/MoveAndRename, ByteRangeLock grant-all, Desktop
  DB stubs). Resource forks + Finder info live in netatalk-compatible
  `.AppleDouble/<name>` sidecars, so a folder previously served by the
  external `afpd` keeps its metadata. The ASP SPWrite → server
  WriteContinue → FPWrite round-trip is what forced the ATP requester
  role.
- **`PapServer`** — a LaserWriter (PAP): NBP registration,
  OpenConn/SendData pull loop, `*` answers to the driver's
  `%%?Begin…Query` lines, and on EOF it spools the PostScript to CUPS
  (`lp`) when present, else a timestamped `.ps` under `run/print`.
- **`MacIpGateway`** — MacIP (IP-in-DDP type 22) with a **user-mode
  NAT** (no `tun`, no root): NBP IPGATEWAY, ATP socket-72 address
  assignment (macipgw wire layout), a from-scratch TCP-lite endpoint
  proxied onto host sockets, per-flow UDP (DNS included), and ICMP echo
  to the gateway. Plain-HTTP-era caveat unchanged.

`AtalkHub` (GUI-side) ties all four to a machine's SCC in
`wireLocalTalk`, coexisting with the LToUDP cable — when
`POM68K_LTOUDP=1` the internal node is multicast alongside real peers.
It is **on by default in the GUI** (`POM68K_APPLETALK=0` disables;
`POM68K_SHARE_DIR` picks the shared folder, default `./AppleShare`).
The new **Réseau → AppleTalk** window shows, live: the node/router
(zone, guest node, frame + NBP + ATP counters), AppleShare (registered?
folder writable? name, sessions, last user/command, bytes), the printer
(registered? idle/busy, jobs, last job path), and MacIP (gateway
visible? lease attributed = *works*, IP counters, flows) — each with an
on/off toggle. Gates: `atalk_stack_test`, `afp_server_test`,
`pap_server_test`, `macip_gw_test` (the last drives real loopback
sockets through the NAT: UDP echo + a full TCP SYN→data→FIN both ways).
The in-process path is GUI-only, so every boot etalon is untouched.

**The bug that made the Chooser's server list come up empty (LC II,
System 7.5).** The node answered 52 NBP AFPServer lookups yet the server
never appeared. Root cause: `Scc8530::injectRxFrame` **drops** a
non-express frame when the guest's Rx is disabled (`Scc8530.cpp:261`,
"receiver off = no ear"). The internal node generates its LkUpReply
*synchronously inside the guest's TX callback* — `onGuestFrame` runs from
`onTxFrame`, at LocalTalk's half-duplex turnaround, when the guest hasn't
yet run its EOM ISR to re-arm Rx — so every reply was injected into a
deaf receiver and dropped. (The LToUDP bridge never hit this: its replies
arrive later, from `poll()`, after Rx is back on.) Fix: `AtalkHub` now
**defers** delivery — the node queues its frames and the hub flushes them
from `tick()`, which runs after the CPU has executed the slice and
re-armed Rx, i.e. the exact timing the working poll path already had.
Two smaller fixes alongside: (a) `AtalkStack::setBridgeRelay` — the
BrRq→LkUp broadcast relay is only useful to reach *external* LToUDP
peers, so the hub enables it solely while the cable is up; solo it would
just collide with our own reply in the Rx FIFO (`APPLETALK.md` §2.4).
(b) The ImGui default font has Latin-1 (é è ç render) but not
`●`/`○`/`œ`/`→` (shown as `?`); the status window now uses
`ImGui::Bullet()` for the colored indicators and ASCII labels.
`POM68K_ATALK_DEBUG=1` traces DDP/NBP/ATP to stderr.

**Follow-ups from the second live boot:** the volume mounted but the
desktop icon had no name — the Finder labels an AFP volume from its root
directory's `DIRPBIT_LNAME`, and the root (rel `""`) was reporting an
empty long name; `FPGetFileDirParms` on DID 2 now returns the volume name
(gated in `afp_server_test`). The volume name itself is now **derived
from the shared folder's own name** (netatalk's behaviour for an
unnamed volume) rather than a hardcoded "Partage": a folder called
`AppleShare` mounts as *AppleShare*. And the default share folder moved
from `build/AppleShare` (throwaway build tree) to **`AppleShare/` at the
repo root** — the exec dir's parent, so it sits next to the sources;
`POM68K_SHARE_DIR` still overrides.

**Copy speed + the "saccade" (2026-07-25).** Real LocalTalk is
230 kbit/s (~28 KB/s), so a multi-MB Finder copy to the in-process
AppleShare ran minutes with visible bursts-then-stalls. Two findings:

1. *The first speed attempt did nothing.* It lowered `setByteCycles`,
   but in SDLC mode `updateSerial` DERIVES the exact 230.4 kbit/s pace
   from the guest's WR4 + `setClocks`, and `paceOf` prefers the derived
   pace on **every** machine — so copies still ran at authentic wire
   speed.
2. *The stall mechanism*: any dropped byte (3-deep Rx FIFO overrun, or a
   frame landing while the driver is mid-turnaround) loses a whole frame,
   and the client then sits out a 1–2 s ATP retransmit timer — burst,
   stall, burst. The saccade IS the retransmit timer.

The proper design is a **lossless boosted virtual wire** (hub without an
LToUDP cable only; async terminal serial untouched — the override is
SDLC-scoped):
- `Scc8530::setWirePace` — an explicit SDLC per-byte pace override that
  wins over the derived real pace, both directions (default boost 8,
  floor 64 cycles/byte; `POM68K_ATALK_WIRE_BOOST`, `=1` = authentic).
- `Scc8530::setLosslessRx` — flow control a real cable cannot have, on
  **both** stall paths:
  (a) a full Rx FIFO **pauses** the frame on the wire (no overrun); and
  (b) — the dominant one, found live at 156 retransmissions on a Read —
  `injectRxFrame` no longer DROPS a frame that arrives while the guest's
  Rx is off. On a Read the server's reply is generated inside the guest's
  own request-transmit callback (half-duplex → Rx down until its EOM ISR
  re-arms), and the finer 64× slicing flushes it *before* the re-arm, so
  it hit a deaf receiver every time. Lossless now QUEUES it and opens it
  only once Rx is back and the FIFO has drained (which also subsumes the
  old inter-dialog serialization). Throughput self-limits to the guest's
  ISR drain rate — smoothly, no retransmits. Cap is therefore the guest's
  real drain speed (~a few × LocalTalk), not the raw boost; that is
  correct (a real fast wire would stall on the same receiver).
- Guest-code timing windows stay REAL regardless of boost: the
  express-CTS gap and the LLAP inter-dialog gap model the driver's
  turnaround (`realPaceOf`), and an open Tx frame that underruns gets one
  real byte-time of grace before the tail flushes — a feed loop that kept
  up at hardware cadence can never have its frame truncated by a faster
  virtual shifter (intentional ends just see the EOM one byte-time
  later).
- Observability: XO-cache hits = client retransmissions
  (`Stats::atpDupReqs`), surfaced in the AppleTalk window — 0 means the
  wire is loss-free; a growing count says lower the boost.
Gated in `llap_loop_test` (override is SDLC-only so async 9600 baud is
unaffected; lossless delivers every byte of a 10-byte frame through a
stalled reader with no overrun flag; and a reply injected into a
disabled receiver is held, then delivered intact once Rx re-arms — never
dropped). Round-trip latency also dropped:
the quantum is sliced 64× (vs 16) while the hub is active, so queued
replies flush ~260 µs after each transaction instead of ~1 ms.

## 2026-07-24 — Beyond-boot gates on the LC II + a clock-drift bug they caught

Three gates that prove the reference LC II is *usable*, not just that the
Finder paints (`tests/lcii_beyond_etalon.cpp`, `POM68K_BEYOND=` selector):

- **`lcii_soak_etalon`** — idle ~3 emulated minutes after the Finder and
  assert the low-memory Time global ($20C) advanced in step (135–225 s for
  180 s of frames), no halt.
- **`lcii_persist_etalon`** — drive the Finder by keyboard: Cmd-N makes a
  new folder, Return commits its name; the SCSI image must gain the
  "untitled folder" catalog entry, and after a **hard reset** the machine
  must boot back to the Finder off the modified volume with the folder
  still there. End-to-end file-survival proof.
- **`lcii_launch_etalon`** — drive the Finder by **mouse**: relative-motion
  double-click opens a folder; a new window appears (screen delta) and the
  Finder reads its catalog. Proves mouse input reaches the desktop.

**The bug the soak gate caught:** the Egret/Cuda firmware-LLE MCU ran ~37 %
fast (516 M cycles where 377 M was due for 180 s), so the LC II's Egret RTC
drove the Mac's wall clock 247 s ahead in 180 s of machine time — invisible
to every boot-signature gate. Cause: `CudaLle::tick` fed `M68hc05::run`
instruction-sized budgets, and `run` finishes its last instruction *past*
the budget; on a ~4-cycle budget that overshoot is ~37 %. Fixed by carrying
the overshoot as a debt against the next slice (`mcuDebt_`, reset on
`CudaLle::reset`). The HLE Egret path was already time-based and exact —
the soak passes identically on both now. This corrects real clock drift on
**every** Egret/Cuda-LLE machine (LC/LC II/Classic II/Color Classic/LC III
family/AIO family/IIvx-IIvi/Quadra), the default path.

## 2026-07-24 — Floppy write persistence (gate `floppy_persist_test`)

Committed floppy sectors now reach the host image file: `SonyDrive` tracks
the inserted path + a dirty flag (single write choke point `writeSector`,
which every engine — IWM GCR flush, SWIM2 byte/cell commits — funnels
through) and `flushToFile()` writes the image back via temp + rename.
DiskCopy 4.2 images keep their header and get the data checksum
regenerated (rolling add + ror32). Flush fires on **eject** (the moment
Mac OS has flushed its own caches — the Finder-eject path) and on **GUI
exit** (all six machine teardowns). Write-back is opt-in and the GUI
enables it (`POM68K_FLOPPY_RO=1` opts out); tests never enable it, so
etalons stay hermetic — pinned by the gate's "no write-back without
opt-in" case. This closes the "writes stay in memory" data-loss hole
(README) and unblocks write→reboot→read testing.

**Macintosh IIvx** (gate `iivx_boot_etalon`) and **Macintosh IIvi** (gate
`iivi_boot_etalon`, same binary + `POM68K_IIVI=1`): a new machine family on
the **VASP** gate array — MAME's vasp.cpp calls it out as "V8 video on
Sonora addressing", and that is exactly how POM68K builds it:
`VaspMemory`/`VaspCpu` follow the SonoraMemory/SonoraCpu shell (contiguous
RAM at $0, 1 MB ROM ×16 at $40000000, I/O page $50xxxxxx, VRAM at
$60000000, machine ID at $5FFFFFFC — $A55A2015 vx / $A55A2016 vi) while
the peripherals are the LC II's V8 set: **AscV8** at +$14000, **SWIM1** at
+$16000, **Ariel DAC** at +$24000, V8-style video config/monitor sense
through the pseudo-VIA hooks, and `VaspVideo` = the V8 framebuffer decode
at VASP's 2048-byte row pitch (vasp.cpp screen_update). ADB is the LC
III's **Egret 341S0851 firmware LLE**. CPU: 68030 + 68882 @ 31.3344 MHz
(IIvx, C32M) or 15.6672 MHz (IIvi, C15M). The three NuBus slots read as
empty (MAME-unmapped parity, no /BERR — maciivx boots that way in MAME
too). Both boot System 7.5 to the 640×480×8 color Finder — the IIvx
reached it on the first full-machine run. GUI entries + `$4957EB49 →
runVasp` dispatch; `Lc3Machine` became the `SonoraStyleMachine<Mem, Cpu,
Video>` template shared by the Sonora and VASP shells (its frame quantum
now derives from `mem.cpuHz()`, so the LC III+/LC 550 GUI pace their real
33 MHz). 73 CTest gates.

**Mac TV update** (negative result worth recording): the EDE66CBD
machine-table entries `$2000/$2001/$2003` (MCU-type field 0 = Egret,
unlike the LC 520/550's type 3 = Cuda) boot with **neither** MCU on the
Sonora AIO board — the Mac TV needs its own bring-up pass
(docs/LC520_BRINGUP.md § Siblings).

## 2026-07-24 — Phase C: LC 550 and Color Classic II — the AIO family fans out

With the LC 520 booting (below), its two 33.33 MHz siblings are each one
profile away — the fourth 2-machines round, both on the Cuda 341S0060
firmware LLE:

**Macintosh LC 550 / Performa 550** (gate `lc550_boot_etalon`): the LC 520
board at 33.33 MHz (`kCpuHzPlus`) with the model longword **$A55A0101**
(maclc550_map), monitor sense 6 → the ROM machine-table entry with video
type `$4A`. System 7.5 Finder at 640×480×8 bpp color.

**Macintosh Color Classic II / Performa 275** (gate
`cclassic2_boot_etalon`): the same $A55A0101 board in the CC case — the
built-in 512×384 Trinitron reports **monitor sense 2**, which selects the
table entry with video type `$4D` and a **512×384×8 bpp color** Finder.
The sense line is the whole machine difference (the LC III/LC III+ and
LC 475/Quadra 605 identity-variant precedent, now via the display).

`runLc3` was generalized into a `SonoraModel` profile table (LC III /
LC III+ / LC 520 / LC 550 / Color Classic II — name, clock, model id,
Egret-vs-Cuda, default sense); the GUI Machine menu gained the three AIO
entries (`POM68K_AIO_ID` = `A55A0100` / `A55A0101` / `CC2`), and the 1 MB
ROM dispatch routes header checksum `$EDE66CBD` there. 71 CTest gates.

## 2026-07-24 — Phase C: Macintosh LC 520 — the EDE66CBD all-in-one family boots (Cuda 341S0060 LLE)

The LC 520 was a genuine from-scratch bring-up (MAME's `maclc520` is a
non-booting stub, so the 1 MB EDE66CBD universal ROM itself was the oracle —
branch-target trails + Capstone disassembly, story in
`docs/LC520_BRINGUP.md`). Three findings unlocked it:

1. **The ROM's reset-time MCU handshake is Cuda-protocol.** The Sonora AIO
   family carries a **Cuda, not the LC III's Egret** (MAME maclc3.cpp:379
   `CUDA_V2XX` 341s0060). With the Egret 341S0851 the handshake at
   `$408D1AE6` times out, the ROM skips ALL startup tests (d7 bit 26 =
   "tests passed" never set at `$408471EC`), plays what we had believed was
   the boot chime — it is the **error chime** — and sits in the ROM serial
   monitor polling SCC RR0. `SonoraMemory` gained a `cudaAdb` constructor
   flag: Egret-HLE cuda polarity + `CudaLle Flavor::Cuda`, firmware order
   341s0060 → 341s0788.
2. **The firmware version matters**: Cuda 2.37 (341s0788, the Q605/CC part)
   livelocks this ROM at `$408B399C` — its early config path sends pseudo
   command `[01 0E]` (where the LC 475 ROM sends `[01 07]`) and 2.37 keeps
   re-asserting TREQ instead of answering. **Cuda 2.40 (341s0060), the
   factory LC 520 part, answers it** and the boot sails through.
3. The earlier bring-up notes' Wall 2/3 were a misread: **$A55A0100 IS in
   the ROM's machine table** — twice, video type `$32` (sense 6, built-in
   640×480) and `$4B` (sense 2); ditto `$A55A0101` (`$4A`/`$4D`). MAME's
   model ids are correct and sense 6 is the right default.

Also: `SonoraMemory` VIA1 port B undriven input bits now read pulled-up
(`$C7 | session<<3`), matching `V8Memory`/`Q605Memory` (no LC III/III+/CC
regression — all four sibling etalons re-run green).

Gate `lc520_boot_etalon`: System 7.5 to the Finder at **640×480×8 bpp
color** (the first color-desktop gate — its Finder signature is
luminance-weighted because the blue-channel ratio the mono gates use reads
the orange/green desktop weave as solid black).

## 2026-07-24 — Phase C: LC 475 (68LC040 + Cuda LLE) and LC III+ (33 MHz Sonora + Egret LLE)

The third 2-machines-in-LLE round of the day. Both new profiles reuse an
already-Finder-booting machine unchanged except for the model identity, so
each is one gate away — and each rides the same firmware-LLE MCU path as its
sibling (Cuda for the 040, Egret for the 030).

**Macintosh LC 475 / Performa 475** (gate `lc475_boot_etalon`): the Quadra
605 machine (`Q605Memory` + `Cpu040`, MEMCjr/PrimeTime/DAFB/53C96/Cuda LLE)
with the LC 475 identity — model longword **$A55A2221** at $5FFFFFFC and the
**68LC040** CPU (`M68LC040` + soft 68882, the Finder-usable no-FPU path).
The old combined "Quadra 605 / LC 475" menu entry is split in two: **LC 475**
($A55A2221, 68LC040, default) and **Quadra 605** ($A55A2225, full 68040 +
FPU), each set via `POM68K_Q605_ID` / `POM68K_Q605_NOFPU` (the GUI menu sets
them before the execv relaunch). Boots Mac OS 8.1 to the 640×480×8 Finder.

**Macintosh LC III+** (gate `lc3plus_boot_etalon`): the Sonora machine
clocked at **33.33 MHz** with the model longword **$A55A0003** (MAME
`maclc3p`; same ROM as the LC III, `#define rom_maclc3p rom_maclc3`).
`SonoraMemory` gained constructor parameters for the CPU clock and the model
ID (`kCpuHzPlus`/`kIdLc3Plus`); every internal `kCpuHz` became the `cpuHz_`
member, so the VIA/video/SWIM/SCC divisors scale while the C7M bus clock
stays put — matching `maclc3p`, which only swaps the 68030 XTAL. Selected via
`POM68K_LC3_PLUS`; boots System 7.5 to the Finder on the Egret firmware LLE.

**The root cause that had wedged the LC III+** (and why the 33 MHz clock
alone was *not* the problem — a 33 MHz machine boots fine with the LC III
id): the LC III+ ProductInfo relocates a device-init routine to RAM ($441A)
that pokes an un-emulated register at **$50F0A000** and spins on `btst #3` of
the readback. POM68K returned open-bus **$FF** for unmapped Sonora I/O; MAME's
sonora map has nothing there either but its space unmaps to **0**, so the
poll's bit 3 reads clear and the routine falls through. Changed the Sonora
I/O catch-all read from `$FF` to `$00` (oracle parity — the LC III path never
touches $A000, so `lc3_boot_etalon` stays green). `peek8` on both
`SonoraMemory` and `Q605Memory` now also mirrors the $5FFFFFFC model longword
(was open bus) so the etalons can assert identity.

Gates: `lc475_boot_etalon`, `lc3plus_boot_etalon` (67 total).

## 2026-07-24 — Phase C: Color Classic (Spice + Cuda LLE) and LC III (Sonora + Egret LLE)

Two more machine profiles, both on firmware-LLE MCU paths — the second
2-machines-in-LLE round of the day. 1 MB ROMs now dispatch by header
checksum before falling to the Quadra: `$ECD99DC0` → Color Classic,
`$ECBBC41C`/`$EC904829` → LC III, else Quadra 605.

**Macintosh Color Classic** (`V8Memory::Model::ColorClassic`): the
SPICE V8 derivative (MAME v8.cpp:693-929) — built-in 512×384 Trinitron
(fixed sense 2, `via2_video_config_r` = $02<<3), VIA1 PA id $82, the
gate-array **SWIM2** at $F16000 (the Q605 `Swim2` cell, C15M = CPU 1:1),
a brightness/contrast DAC stub at $F18000, a 1 MB ROM at $A00000 (no
mirror; `romSize_`/`romMask_` replace the fixed 512 KB assumption), and
a **Cuda MCU instead of the Egret**. The LLE runs the Q605-proven
341S0788 (Cuda 2.37) under `POM68K_CUDA_LLE`; the factory 341S0417
(Cuda 2.35) wedges on our M68hc05 — releases the host reset then never
answers the VIA transport — left as a TODO. HLE fallback = the Egret
class with the Cuda polarity (the Q605 pattern).

**Macintosh LC III "Vail"** (new `SonoraMemory`/`SonoraCpu`/
`SonoraVideo`): first 32-bit-clean V8-era machine — Sonora gate array
(MAME sonora.cpp + maclc3.cpp): contiguous RAM at $0 (no V8 config
banking), 1 MB ROM at $40000000 ×16, I/O at $50xxxxxx (VIA1 mirror
$FC0000, SCC, NCR 5380 + pseudo-DMA windows, EASC, SWIM2, pseudo-VIA),
machine ID $A55A0001 at $5FFFFFFC, 1 MB VRAM at $60000000 (mirrored
across the select — the System addresses the framebuffer through the
$0FF00000 mirror, invisible VRAM writes otherwise), and the mv_sonora
video cell: 5 modelines, CLUT DAC, monitor sense in the video control
regs, modeline-driven VBL. 68030 @ 25 MHz (`SonoraCpu`, the Cpu030
i-cache overlay at the non-integer 783.36 kHz VIA ratio). ADB = Egret
firmware LLE off the factory **341S0851** (0850 fallback).

**`AscSonora`** (new, shared by both): the Sonora/Spice EASC ($BC),
hardware-pinned by MAME's ASCTester dumps — stereo FIFO pair, combined
status folded onto the B bits (bit 2 = either half-empty, bit 3 =
either empty), playback mode reporting FIFO A empty per sample, 804
idle = $0E, writable per-FIFO IRQ enables. **Bring-up root cause**: the
System 7.5 boot enables pseudo-VIA IER bit 4 with the chip's idle IRQ
storm live; MAME gates the $804-read IRQ clear on !(HALF_B), which is
permanently set at idle → the level never drops → the whole boot lives
inside the IPL-2 autovector (RTE → immediate re-entry, TickCount frozen
at 221, SCSI frozen — both machines froze at "Bienvenue." identically).
The real LC III (ASCTester) counts ~50 000 DISTINCT idle IRQs — one per
22 257 Hz sample — so the latch drops on $804 read and re-arms per
sample: modelled that way, the boot breathes between samples.

GUI: both machines in the Machine menu (checksum scan), the LC III on
its own `runLc3`/`Lc3Machine` thread (the QuadraMachine pattern) with
512×384/640×480 monitor sense buttons. Gates: `cclassic_boot_etalon`,
`lc3_boot_etalon` (65 total).

## 2026-07-24 — Phase C: Macintosh LC (68020) and Classic II (Eagle) boot to the Finder

Two new machine profiles on the V8 platform (MAME `maclc.cpp` oracle),
both running the **Egret firmware LLE** ADB path by default — the
2-machines-in-LLE goal. A 512 KB ROM now dispatches by header checksum:
`$350EACF0` → LC, `$3193670E` → Classic II, anything else → LC II.

**Macintosh LC** (`V8Memory::Model::Lc`): the LC II board with a 68020
(`Cpu030(..., as020)` → Moira `Model::M68020`) and 2 MB soldered
(`mbRam_`, MAME `set_baseram_is_4M(false)`). The Apple HMMU is modelled
as the address-mask switch it is (MAME `m68kmmu.h` HMMU_ENABLE_LC =
`addr & $FFFFFF`), driven by pseudo-VIA PB3 (LOW = 24-bit). Getting it
to boot exposed **two real Moira gaps in the plain-68020 bus-error
path** (the LC ROM is the first guest to take an external /BERR on the
020 core and RESUME): the prefetch queue was never refilled after the
$B frame (the faulted instruction re-ran forever → the ROM's
AddrMapFlags probe died), and the frame carried a stale fault address
(the ROM's 32-bit probe catcher compares it → forwarded every expected
fault to SysError DS 1 → Sad Mac + death chime + the LC ROM's
factory-test serial monitor, which is where the "black screen, polls
RR0 forever" symptom came from). Fixes + rationale in
`extern/moira/POM68K_VENDOR.md` § External /BERR on the plain 68020.

**Macintosh Classic II** (`Model::ClassicII`): the Eagle flavor — VIA1
PA id $92, no monitor sense (`via2_video_config_r` = 0), built-in
512×342 1bpp scanned out of MAIN RAM at device offset $1F9A80
(v8.cpp:667-691; `V8Video` Eagle branch, 64-byte pitch), no PDS, MAME's
ROM patch applied at load (boxflag table JMP → RTS + checksum fix,
maclc.cpp:614-630). Key machine behavior: the Eagle bus is FORGIVING —
the ROM dereferences a pointer read from unmapped $50F18038 and pokes
through it with NO bus-error catcher installed, so unmapped I/O and the
absent-PDS space answer open-bus on this model (MAME parity: macclas2
raises /BERR only from the SCSI helper timeout). The V8/LC II keep
their pinned BERR-on-unmapped behavior (AddrMapFlags $773F).

Also new: **SCC WR14 bit 4 Local Loopback** (`Scc8530` — a completed Tx
character re-enters the same channel's receiver; async only), found
while chasing the LC's serial POST. Gates: `lc_boot_etalon`,
`classic2_boot_etalon` (63 total); GUI menu gains both machines.

## 2026-07-24 — Event-driven ADB wire: the Egret firmware LLE is the LC II DEFAULT

TODO step 6 closed. The root cause pinned on 2026-07-23 — `CudaLle::tick`
ran the 68HC05's whole batch against a FROZEN wire, quantizing AdbLine's
35/65 µs bit cells to the ~8 µs machine tick, so the Egret ROM's
bit-banged ADB receive mis-heard device bytes as zeros (~1.5% mouse
delivery, resync only on the 1 Hz packet) — is fixed by SLAVING the wire
to the MCU's instruction stream: a new `M68hc05::onCycles` hook fires
after every instruction (and WAIT idle step) with the cycles consumed,
and `CudaLle` converts them to the AdbLine 15.6672 MHz domain
(`adbAcc_`, constructor lambda). The firmware now samples line edges at
instruction resolution (~1-5 µs), while the MCU-vs-host-VIA scheduling
is UNTOUCHED — the property both failed slicing experiments (CHANGELOG
2026-07-23) lacked: the boot-time PC3/VIA lockstep phase stays
bit-identical, only the wire's clock moved inside `mcu_.run`.

Result: `POM68K_EGRET_LLE=1 lcii_mouse_trace` saturates the screen with
the exact same delta as the Egret HLE — the re-flip criterion (within
10% of HLE) is met with margin, so `V8Memory` now defaults to the
firmware LLE (`POM68K_EGRET_LLE=0` keeps the HLE, missing dump falls
back silently — the Cuda rollout pattern). The Quadra face of the same
root cause (2026-07-24 field freeze: host Cuda command × autopoll TREQ
collision wedging the ADB manager at ~$D1F04) is covered by a new
stress phase in `q605_cudalle_key_etalon`: 500 tight press/release
pairs including the NUMERIC KEYPAD codes from the field session — green
on the slaved wire. All Cuda LLE etalons (boot/mouse/key) stay green.

The LC II's floppy controller grows its second personality (`Swim1.*`,
new): the chip comes up IWM-compatible (the proven `Iwm` embedded — GCR
800K plus today's write engine), and the .Sony driver's four
mode-register writes with bit 6 = 1-0-1-1 switch it to **ISM** (MAME
`swim1.cpp:555-579`, fetched to refs). The ISM half is the SWIM2-lineage
register file — data/mark/error/param/phases/setup/mode0-mode1, 2-deep
FIFO, TSS write serializer, serial CRC-CCITT — with SWIM1's 16-entry
parameter RAM and param-driven write cell timing (P_TIME0/P_TIME1 + 2×2
halves, `swim1.cpp:904-916`); a mode-clear dropping bit 6 returns to
IWM. MAME's LS-pair cell state machine + correction factors discriminate
real-flux jitter our ideal discrete cells don't have, so the read engine
reduces to the SWIM2 shifter (accepted simplification, LLE_VS_HLE §3).
`V8Memory` routes the SWIM window through `Swim1` and the internal drive
is a SuperDrive (as shipped). Gate: `swim1_test` (60th) — switch magic
incl. broken-pattern rejection, param-RAM ring, ISM MFM read of a
1.44 MB image with CRC verify through the FIFO flags, and a param-timed
TSS write that commits through the cell decoder. DAT1BYTE stays unwired
(the LC II polls). A 1.44 MB DiskCopy asset exists in `disks35/` for
future guest-level etalons.

## 2026-07-23 — ADB Talk R0 answers on PENDING data, not on changed bytes

The LC II mouse under the Egret firmware LLE delivered ~0.5% of injected
motion (`lcii_mouse_trace`: delta (63,42) for 12 000 px injected). First
layer of the diagnosis: `AdbLine` had inherited MAME macadb's byte-compare
dedup — a Talk R0 reply was sent only if its two report bytes DIFFERED
from the previous reply, but the motion accumulators were already drained.
Under the Egret's fast autopoll every steady-drag report is byte-identical
to the last, so the deltas were consumed and thrown away. Real ADB devices
answer Talk R0 whenever they have data pending (MAME's own
`adb_pollmouse()` "did it change since last poll" principle — its byte
compare is an approximation of that, valid only when polls sample
continuous host motion). `AdbLine` now replies iff motion/button change is
pending (mouse) or an event was popped (keyboard — byte-identical
consecutive keystroke pairs are legitimate and no longer eaten);
`lastMouse_`/`lastKbd_` are gone. One nuance found by the Q605 OS 8.1
boot etalons: the SRQ-initiated poll (`srqSwitch_`) must be ANSWERED
with its empty report, not timed out — the requesting flow needs the
reply to complete, and leaving it pending slowed the boot past the
etalon budgets (blank menu bar at measure time). Benefits every AdbLine
machine (Mac II PIC, Q605 Cuda, LC II Egret); `adbline_test` and the
mouse etalons stay green, the Q605 trace still saturates the screen
(delta (624,464)). Field report the same evening: typing froze the
Quadra outright on the pre-fix binary (the keyboard is the first real
SRQ consumer — the mouse rides autopoll); on the fixed tree the new gate
`q605_cudalle_key_etalon` (61st) types "8.8.8.8" through the firmware to
the low-mem KeyMap with Ticks advancing.

## 2026-07-23 — LC II: Egret firmware LLE back to OPT-IN (mouse starvation)

> **Superseded** by [2026-07-24 — Event-driven ADB wire: the Egret firmware
> LLE is the LC II DEFAULT](#2026-07-24--event-driven-adb-wire-the-egret-firmware-lle-is-the-lc-ii-default):
> the instruction-slaved wire removed the starvation and the LLE went back to
> being the default.

Second layer: even with the AdbLine fix the LC II LLE mouse delivers only
~1.5% — one packet per second. Instrumentation (new `POM68K_ADB_LLE_TRACE`
diagnostics in `CudaLle`: TREQ falls, TIP sessions with clock-edge counts,
wire bytes) shows AdbLine now ships one report per injected frame (3 982 /
4 000) and the firmware raises TREQ for each (5 119), but **every host
session closes after exactly one byte (16 clock edges, value $00)** and
the firmware clocks the real packet bytes after the close, into the void —
host and MCU are one step out of phase, and the System only resyncs on the
one-second packet (58 successes in 66 s = exactly the 1 Hz heartbeat).
The HLE Egret on the same trace saturates the screen, and MAME's maclc
runs the same firmware wiring fine — the defect is in OUR VIA-side glue
(per-byte via_full dance vs the 68HC05's expectations), not in the
firmware, AdbLine, or the System. Per the LLE_VS_HLE principle (a
deficient LLE must not be the default), `POM68K_EGRET_LLE` is **opt-in
again on the LC II** until the wire diff against MAME closes the gap; the
Q605 Cuda flavor is unaffected (its OS 8.1 dialect paces differently) and
stays LLE-default. `egret_lle_test` still forces and gates the firmware
path. Workaround era: none needed — the default IS the working path.

## 2026-07-23 — IWM write engine + GCR write-back: floppies are writable

The last dropped-write shortcut in the floppy chain is gone
(`LLE_VS_HLE.md` §3 — "GCR write-back deferred (logged)" since step 13,
and the Plus M5.1 "write support" stub from the very first IWM):

- **`Iwm` grows the real write mode** (MAME `iwm.cpp` `MODE_WRITE`,
  byte-granular): q7-while-enabled enters write mode, and the entering
  access carries the first data byte (control `$C0` + data in one store,
  exactly the ROM's sequence); the data register holds one pending byte
  the shifter consumes every 8 bit windows (128 cycles at mode `$1F`,
  first load at +7 like `S_IDLE`); handshake bit 7 = register empty,
  bit 6 drops on underrun, which halts the engine (`SW_UNDERRUN`) and
  flushes; leaving write mode (q7 or ENABLE clear) flushes too. The
  idle handshake now reads `$BF` (MAME's `m_whd` reset value), not the
  hardwired `$C0`.
- **`SonyDrive` decodes written GCR back into sectors** with the same
  inverse-6&2 + rolling-3-way-checksum loop `gcr_test` already pins
  (MAME `extract_sectors_from_track_mac_gcr6`), fed from either mouth:
  the IWM nibble buffer (`writeNibble`/`flushWrite`) or the raw cell
  track (`decodeGcrCells` — offline replica of the SWIM2 GCR framer,
  MSB-set bytes self-frame across sync-group zero cells). Only
  checksum-valid fields commit; the physical head position names
  track/side (a write can only land under the head), the field's own
  sector nibble names the slot; recovered tag bytes are dropped (flat
  images carry no tag space). `commitCells` dispatches on the media
  encoding — the only remaining logged drop is a true mismatch (MFM
  cells on GCR media), which real hardware would render unreadable.
- **Gates**: `iwm_write_test` (59th) harvests an encoded data field from
  a patterned drive's own read stream and replays it byte-for-byte
  through the IWM write registers into a blank drive — the sector must
  commit byte-identical, proving write is the exact inverse of read;
  underrun and write-protect paths pinned. `swim2_media_test` grows the
  same inverse proof through the SWIM2 TSS GCR write path (setup bit 6,
  8 cells/byte) + cell splice + rotation-angle landing.

Covers the Plus **and** the LC II (SWIM1 shares `Iwm`); the Quadra's
GCR writes through SWIM2 commit as well. Like SCSI (DEV.md), writes land
in the in-memory image only — host-file persistence stays in the TODO.

## 2026-07-23 — Mechanical drive sounds (floppy + SCSI hard disk)

`FloppySound` (new, GUI-side): a port of MAME's
`floppy.cpp::floppy_sound_device` via POM2's `FloppySoundDevice`, playing
MAME's BSD-3 sample set (committed under `assets/floppy_samples/` — a
`.gitignore` exception, since the tree otherwise ignores `*.wav`).

- The 3.5" samples voice the Sony drives on all four machines; the
  5.25" set plays at 0.25 gain as the SCSI hard-disk proxy (POM2
  SmartPort precedent) with an **auto-motor-off** addition — the HDD
  has no motor line, so the spin loop retires after 1.5 s without a
  block access.
- Events flow through the header-only `FloppySoundSink` (SonyDrive
  step/motor/insert/eject, ScsiDisk read/write) so the miniaudio TU
  never reaches headless builds; `MacAudioHost` mixes the FX after the
  machine ring, so drives are audible over a silent desktop.
- Step cadence is measured in **emulated microseconds** (POM2's turbo
  lesson: wall-clock sees gap≈0 through a turbo'd seek sweep) with a
  wall-clock fallback for unstamped callers (`kNoStamp`, ScsiDisk).
- En route: the LC II never ticked its SonyDrive (spindle/tach time
  frozen since O6) — `V8Memory` now ticks it, and LC II / Mac II
  declare their 15.6672 MHz spin clocks (`setSpinClockHz`).
- Gate: `floppy_sound_test` (58th; soft-skips without samples). Toggle:
  Machine ▸ "Sons des lecteurs"; `POM68K_DRIVE_SFX=0` starts muted.

## 2026-07-23 — SWIM2: the real cell engines (MFM cell timing + CRC)

The biggest remaining floppy LLE gap (`LLE_VS_HLE.md` step 13): `Swim2`
no longer synthesizes decoded bytes — it runs MAME `swim2.cpp`'s bit
engines verbatim over a raw-cell track in `SonyDrive`.

- **Read** (swim2.cpp:482-547): the MFM sync hunter (≥64 alternating
  cells, 16-cell windows, missing-clock `$4489` → MARK), the serial
  CRC-CCITT seeded `$CDB4` re-armed on every mark, the `M_CRC0` tag
  surfaced on handshake bit 1 — CRC verification is now real end to
  end (the encoder writes real CRCs on the track; corrupting them is
  detected). GCR frames nibbles on the high bit through 10-bit
  self-sync groups. FIFO overrun now **loses bytes** like hardware
  (error $01) instead of pausing the disk.
- **Write** (swim2.cpp:402-481): the TSS serializer in half-cycles
  (63/31-half spacings, `$C` missing-clock entry for marks, CRC token
  → the shifted CRC's two bytes). Transitions are rebuilt into cells
  **per-gap** like a PLL — an absolute divide would drift 31-vs-32
  halves per cell and clip the CRC tail (first version's bug, caught
  by the gate). The written span decodes through the offline replica
  of the read machine and only commits sectors whose address AND data
  CRCs verify.
- **SonyDrive raw cells**: the track is one padded revolution of
  discrete cells (MFM 16 / GCR 31 C15M clocks per cell — 500 kb/s HD,
  which also halves the old 2×-fast byte pace); the head lands at the
  **spin-counter angle** on every ACTION start, so rotational latency
  is real (`setSpinClockHz` declares the tick unit; Q605 = 25 MHz).
  The Iwm/SWIM1 nibble path (Plus, LC II) is untouched.
- Gates: `swim2_media_test` rewritten against the real engines (CRC0
  flags, mid-revolution latency, TSS format-style write, corrupt-CRC
  rejection); `swim2_test` re-pinned to oracle no-media behavior
  (PLL free-runs zeros → FIFO empty, pop = `$FF` + underrun error).
  `q605_floppy_boot_etalon` (ROM Sony driver, System 7.5 floppy)
  passed on the first run with the new engine.

## 2026-07-23 — LLE audit: step 9 closed, the quick wins are exhausted

A re-audit of every remaining `LLE_VS_HLE.md` gap against the oracles,
closing migration-plan step 9 (53C96/TurboSCSI) — the verdicts, so the
reasoning is not re-derived next pass:

- **53C96 tcounter↔FIFO staging rewrite: dropped.** Re-derived what a
  true staging engine would change observably: with instant staging,
  R_FLAGS, DRQ, S_TC0/I_BUS ordering and every byte are IDENTICAL to
  the current short-circuit model — only the internal array differs.
  A wire-paced engine would differ only under data starvation no Mac
  driver observes (they gate on S_TC0/FLAGS, both already honest), and
  it risks every pinned Q6.3-Q6.6b OS 8.1 interaction.
- **Instant selection timeout IS oracle parity**: MAME ships
  `#define DELAY_HACK` (`ncr53c90.cpp:382`) making its own empty-ID
  scan instant. What we had marked as a divergence is the oracle's
  shipping behavior.
- **SDTR / BUSMOD-16**: no consumer on a Q605 (drivers never negotiate
  sync; the chip is wired 8-bit through PrimeTime — 16-bit only
  matters for a DAFB-DMA machine profile, where the absent DAFB
  TurboSCSI cell would matter too).
- **VIA ±1-cycle timer latency, per-scanline Plus sound fetch**:
  surveyed, both unobservable to any gate or pinned driver path today;
  left in the Plus polish backlog rather than faked as fidelity.

What genuinely remains is big-ticket: SWIM2/SonyDrive MFM cell timing
+ CRC, 040 copyback/snooping, SCC bit-serial engines (needs an async
transport first), and the Egret/Cuda/AdbVia HLE retirement — a policy
call (the fallbacks still serve dump-less setups), not a code gap.

## 2026-07-23 — SCC Tx/Rx engine: the wire gets a real transmitter (Medium tier)

The z80scc-audit backlog's Medium tier, landed as one engine (gate
`scc_engine_test`; MAME `z80scc.cpp` as oracle):

- **WR5 bit 3 finally gates the transmitter** (`tra_callback :1037`
  sends marks while disabled): a byte written with Tx off parks in the
  buffer and flows the moment the driver enables Tx.
- **One-slot Tx buffer + paced shifter** replace the instant-accept
  model: the shifter drains one character per character time (the
  WR4/WR11/WR12-14-derived pace), TxIP fires on the buffer-empty
  TRANSITION (`tra_complete :1075`), and RR0 TBE / RR1 All Sent read
  live — the TBE poll a real LAP transmit loop paces itself on now
  means something. The Plus ticks its SCC now (`MacMemory::tick`);
  without it a TBE poll would never see the buffer free again.
- **The SDLC tail is timed, not flat**: after the underrun the chip
  drains CRC + closing flag in 24 bit times at the programmed pace
  (`kTailBytes`); the flat 1200-cycle `kUnderrunDelay` is deleted. A
  data byte arriving mid-tail cancels the flush and the frame
  continues — the old "each write pushes the underrun out", now with
  wire-true timing.
- **The receiver verifies the Rx FCS**: RR1 bit 6 (CRC error) is a
  computed verdict on the EOF byte, no longer hardwired good — a
  corrupted or truncated wire frame is flagged like silicon would.
- **Async error machinery**: `injectRxByte` (the future serial-port
  transport entry) carries parity/framing flags; the error status
  rides each byte through the FIFO and raises the special condition
  when the errored byte is READ (`data_read :2130`, the Zilog
  FIFO-lock rationale), parity only when WR1 bit 2 makes it special;
  Error Reset clears the RR1 error bits.

The LLAP gates were re-pinned on the paced wire (the test senders now
wait TBE between writes like the real driver always had to) and the
whole 57-gate suite is green — including `llap_two_system_etalon`, the
live AppleShare dialogue between two booted System 7 machines over the
now-honest transmitter.

## 2026-07-23 — The LC II runs the real Egret firmware too (same day, same glue)

The user dropped the Egret dumps (`roms/egret/`, SHA1-verified against
MAME `egret.cpp` — 341S0850 is the LC/LC II revision) and the Q605's
`CudaLle` glue absorbed the flavor in one parameter: the Egret is the
same customized 68HC05E1 at the same 4.19 MHz with the same
via_clock/via_data/ADB port assignments; the differences are the idle
input levels (PB bit 7 and the PC power-sense bits absent, PA bare),
the pull-up set (PB6 only, no PFW tap), and the host-reset edge — the
Egret releases on the PC3 **falling** edge where the Cuda uses the
rising one (`egret.cpp pc_w` vs `cuda.cpp pc_w`), which is also when
the staged PRAM installs.

First try: **the LC II boots System 7.5 AND 7.1 to the Finder on the
real firmware** (release +259.1 ms), and the mouse moves through the
firmware's own ADB autopoll (`lcii_mouse_trace` routed through
`V8Memory::mouseMove`; slower than the HLE's clamped deltas — that's
the real autopoll cadence). Default ON when the dump is present,
`POM68K_EGRET_LLE=0` keeps the HLE; gate `egret_lle_test` pins the
falling-edge release + PRAM install; PRAM persistence and monitor-sense
sPRAM parking route through the live MCU RAM.

With both machines on firmware, what remains of the Egret/Cuda HLE
inventory entry is fallback-only. Retirement of `Egret.*`/`AdbBus` (and
the Mac II §1.9 leftover) can proceed once the fallbacks feel redundant.

## 2026-07-23 — The real Cuda firmware is the Quadra's DEFAULT (blueprint step 4)

The `POM68K_ADB_LLE` rollout pattern, completed for the Cuda: whenever
`roms/cuda/341s0788.bin` is present the Quadra 605 runs the REAL
firmware — ADB, PRAM, real-time clock, host packets — with the Egret
HLE only as the `POM68K_CUDA_LLE=0` / missing-dump fallback.

What landed to get there:

- **Input routing**: `Q605Memory` grows `keyEvent/mouseMove/mouseButton`
  forwarders that feed the bit-serial `AdbLine` under the LLE (the
  Egret HLE's command-level `AdbBus` otherwise); the GUI's Quadra
  runner uses them.
- **The ADB polarity bug**: the firmware's autopoll ran (31k line
  edges per 10 s) but `AdbLine` never decoded a command — the trace
  showed idle-LOW/attention-HIGH, inverted. The Cuda's ADB output
  stage inverts: **the electrical line is ¬PA7**, PA6 senses the line
  directly (MAME encodes it as `write_linechange((bit7>>7)^1)` with
  macadb echoing the level back into PA6). One-line fix in the PA
  glue; the mouse then sweeps the Finder to the screen corner —
  gate `q605_cudalle_mouse_etalon` (delta (624,464) over the real
  autopoll chain: AdbLine wire → 341S0788 → VIA SR → mouse driver).
- **PRAM persistence routed**: `Q605Memory::loadPram/savePram` keep
  Egret's file format and factory fallback but re-mirror into the
  MCU's live internal RAM under the LLE (load re-stages, save
  harvests).

With the flip, every Quadra gate — the three boot etalons, DAFB/ASC/
SWIM2/TurboSCSI, the Finder matrix — exercises the firmware path by
default. The Egret/Cuda HLE inventory entry (LLE_VS_HLE §2) is now
fallback-only on the Q605; the LC II Egret flavor still needs its own
341S0850 dump for the same treatment.

## 2026-07-23 — Mac OS 8.1 boots to the Finder on the REAL Cuda firmware (blueprint step 3)

`POM68K_CUDA_LLE=1` now takes the Quadra all the way: the ROM's Cuda
device manager and the Mac OS 8.1 System talk to the actual 341S0788
firmware over the VIA SR wire — packet sync, the 86-command early PRAM
sweep, GET_REAL_TIME, autopoll config, the lot — and the three Q605
boot etalons (8.1, no-FPU, bare-FPU) reach the Finder. New gate
`q605_cudalle_boot_etalon` pins it.

The debugging (wire probe, `cuda_wire.log` methodology):

- **The transactions were already correct**: byte-level probing showed
  the READ_PRAM sweep responses from the firmware exactly matching the
  Egret HLE's (`[$01 $00 $07 data]`), the attention byte as a real
  dummy-SHIFT wire event, per-byte BYTEACK toggles, TREQ release on
  close — the step-7 HLE wire model was vindicated bit for bit.
- **The boot wedged ~4.7 s in**: the MCU parked the ADB line low and
  span in a delay loop at $1181-$118B polling PB0 (+5 V sense) —
  the firmware's power-fail shutdown path. Root cause: the firmware
  sets DDRA bit 0 (PFW) as an OUTPUT; on a stock 68HC05E1 that drives
  PFW low, and reading it back says "power failing". **The real Cuda
  is a lightly customized E1 whose PFW pin stays an input** — MAME
  installs a "cudapfw" write tap for exactly this (cuda.cpp:146-152).
  `M68hc05::setForcedInputs` now replicates it, `cuda_lle_test` pins
  the DDRA behaviour.
- Timer fidelity tightened along the way: the programmable timer and
  the one-second timer are armed by `pll_w`/`onesec_w` like MAME's
  (`m_prog_timer->adjust`/`m_timer->adjust`) instead of free-running
  from reset.

Remaining before the default flip (blueprint step 4): route host input
events into `CudaLle::adbLine()` (the UI feeds `AdbBus` today), a mouse
etalon on the firmware path, then per-machine default with the HLE as
`POM68K_CUDA_LLE=0` fallback — and the same rollout for the LC II's
Egret once its 341S0850 dump is on hand.

## 2026-07-23 — M68HC05E1 core: the real Cuda firmware executes (step 10 groundwork)

Blueprint step 1 of the Egret/Cuda **firmware** LLE (TODO; oracles
fetched to `refs/mame/src/`): `M68hc05` is a from-scratch 68HC05E1
interpreter — full HC05 opcode set with MAME's `s_hc_cycles` counts
(m6805.cpp:327-345), the E1 on-chip map (ports/DDRs with pullup mixing,
PLL with the rate-3 cheat, programmable timer at clock/1024, one-second
timer, RAM $0090-$01FF with the $C0-$FF stack, ROM $0F00-$1FFF), the
IRQ/TIMER/CPI vector priority of `m68hc05e1.cpp:66-84`, and WAIT/STOP.

Gate `m68hc05_test`: **all three real Cuda dumps run clean from their
reset vectors** — 341S0788 (Cuda 2.37, the MAME default) executes ~585k
instructions over 2 M cycles with zero undefined opcodes, programs the
PLL, sets its port directions and drives the port-B VIA handshake side;
341S0417 (2.35) and 341S0060 (2.40) pass the same bar. Idle port levels
are wired per `mame/apple/cuda.cpp` `pa_r/pb_r/pc_r`.

**Step 2 landed the same day** (`CudaLle`, gate `cuda_lle_test`): the
firmware runs behind the Quadra's VIA1 with the full signal map —
PB1 /TREQ → VIA PB3, PB4/PB5 via_clock/via_data on the VIA SR through
`Via6522::extShiftCB1` (the PIC1654S external-shift path, reused
verbatim), BYTEACK/TIP from host PB4/PB5, PA7↔`AdbLine` for the
bit-serial ADB wire, and PC3 as the host-reset release. On power-on the
real firmware boots on the MCU core and **releases the 68040 by its own
PC3 write after 280.8 ms of machine time**; the staged battery PRAM is
installed into the E1's internal RAM at $0100-$01FF on that edge
(MAME `pc_w` semantics — and note the address matches what the LLE
step 7 wire redo had already established for the HLE). Opt-in via
`POM68K_CUDA_LLE=1` in `Q605Memory`; the Egret HLE remains the default.

Next (blueprint 3-4): first host↔Cuda packet transactions over the SR
wire against the ROM's device manager, then flip the default behind
the boot etalons — the PIC1654S rollout pattern. This is the path that
retires the `Egret` HLE, `AdbBus`, the Mac II HLE `AdbVia` byte-model
and the §1.9 ORB hack.

## 2026-07-23 — SCC async-baud machinery: the guest programs the wire pace now

The "High" blocker of the SCC LLE backlog (TODO / `docs/LLE_VS_HLE.md`
§3) is closed. `Scc8530` derives each channel's byte pace from the
guest's own serial programming instead of a fixed constant — MAME
`z80scc.cpp` as oracle (`get_clock_mode` :1157, `get_brg_rate` :2476,
`update_serial` :2565), gate `scc_baud_test` (15 checks):

- **WR4** clock mode ×1/16/32/64 + stop bits (1/1.5/2) + parity bit;
  **WR5** data bits (5/6/7/8); **WR11** bits 4-3 Tx clock routing (RTxC
  pin / BRG; TRxC & DPLL-async fall back); **WR12/13 + WR14** BRG:
  rate = source / (2+(WR13<<8|WR12)) / (2·mode), source = WR14 bit 1
  ? PCLK : RTxC.
- **Clock wiring per machine** (`setClocks(cpuHz, pclkHz)`): RTxC is
  the 3.6864 MHz serial crystal on every Mac (MAME
  `configure_channels`; `LCII_HARDWARE.md:44`); PCLK = 3.9168 MHz on
  the Plus (DEV.md:74), C7M 7.8336 MHz on the II-class boards
  (`maclc.cpp:378`, `macquadra605.cpp:171`).
- **SDLC derives the LLAP constants exactly**: RTxC/16 = 230 400 bit/s
  → 272/544/868 cycles per byte at 7.8336/15.6672/25 MHz — the very
  numbers `setByteCycles` hardcoded, so the LocalTalk gates see zero
  timing shift while the constant becomes a *derived* quantity.
  `byteCycles_` remains only as the pre-programming fallback.
- Cross-check for 9600 8N1 through the BRG: constant 10 → 3.6864e6 /
  12 / 32 = 9600 baud exactly — the classic Mac baud-constant table
  falls out of the formula, confirming the crystal identification.

## 2026-07-23 — Toby: CRTC-derived frame clock + the register file actually writes

Two Toby fixes in one pass (MAME `nubus_m2video.cpp` fetched to
`refs/mame/src/devices/bus/nubus/` as oracle; gate `toby_test`
extended):

- **The TFB register file was silently write-dropped.** The machine
  splits every slot access into bytes (`MacIIMemory` → `NuBus::write8`)
  and `TobyVideo::write8` had no TFB branch — only the (never used from
  the bus) `write32` did. Nobody noticed because the reset defaults are
  the only mode the Mac II ROM uses (640×480×1). The byte path now
  mirrors MAME `tfb_w` (:253-268): the register stores the inverted
  lane, the last lane of a long carries the value, MISC2 commits.
- **CRTC-derived frame clock** — the Q8.1 DAFB treatment finally
  applied to Toby (the last §3 video gap of that kind): frame period =
  htotal × vtotal ticks of the card's 30.24 MHz pixel crystal
  (`calc_screen_params` / `attotime::from_ticks`), with the full
  sync/porch register decode for the totals. The fixed 60 Hz /
  261 120-cycle frame remains only until the guest commits a plausible
  CRTC (a half-programmed CRTC won't storm the VBL: sub-50k-cycle
  frames keep the fallback).

## 2026-07-23 — LLE step 9 (partial): TurboSCSI wait-state cell + 53C96 scheduled delays

The Quadra's SCSI path stops being zero-time. Two of step 9's four
items land, pinned by the new `q605_turboscsi_test` gate (17 checks,
no asset needed — it runs against a synthetic zero image):

- **PrimeTime TurboSCSI wait-state cell** (MAME `iosb.cpp:482-618`
  parity): every 53C96 register access now stalls the CPU 3 cycles;
  the pseudo-DMA window's **waitstated alias** (byte-address bit 19 —
  MAME's `BIT(offset<<1,18)` under `.select(0xfc0000)`) costs the
  guest-programmed count while the plain window stays free. **IOSB
  reg 2** (`$50018200`) programs the DMA read (bits 8-9) / write
  (bits 11-12) wait states through `times[4] = {5,5,4,3}` exactly as
  `iosb_regs_w` does; power-on default 3. This is the "DAFB TurboSCSI
  cell" of `docs/LLE_VS_HLE.md` §3 — on the Q605 the cell lives in
  PrimeTime/IOSB, so `iosb.cpp` (not `dafb.cpp`) is the oracle.
- **Scheduled selection/bus-service delays, default ON** (the step 9
  "non-zero default for `POM68K_SCSI_LAT`"): `Ncr53c96` grows a
  MAME-derived delay model in place of the flat Q6.5b latency knob.
  Selection-with-ATN raises I_BUS|I_FUNCTION only after the
  `ncr53c90.cpp` arbitrate/assert/settle chain (`delay(11)+delay(6)+
  delay_cycles(4)+delay(2)+delay_cycles(2)` = 19×CCF+6 SCSI clocks,
  CCF 0 ≡ 8) plus `sync_period` clocks per IDENTIFY/CDB byte; a
  Transfer Information defers its bus-service interrupt by
  `sync_period`×bytes+2 clocks (`delay_cycles(sync_period)` per byte,
  :462/:762). The chip runs at 40 MHz (`macquadra605.cpp:202`) against
  the 25 MHz CPU → ×5/8 rounded up. **This is the sync-register
  plumbing too**: the guest-programmed `R_CLOCK` conversion factor and
  `R_SEQ` sync-period now have a real timing effect instead of being
  write-only stubs. `POM68K_SCSI_LAT=0` forces the historical instant
  behaviour, `=N` a flat deferral; unit tests that drive the bare chip
  keep instant (the model is enabled by `Q605Memory`).
- The pseudo-DMA `/DTACK` holdoff comment now states the honest
  equivalence: our 53C96 changes DRQ only on CPU-driven accesses, so a
  `!DRQ` window access can never be released mid-hold — the immediate
  `/BERR` is the observable form of the eventual bus timeout (MAME
  spins because its chip runs on its own timers).
- **Two scheduling bugs the etalons caught** (q605_boot_etalon /
  q605_nofpu_boot_etalon red at SCSI=2416 on first landing):
  (1) a **polled (non-DMA) Transfer Info moves ONE byte**, not the
  remaining payload — MAME raises bus_complete for every received
  byte in non-DMA IN (`INIT_XFR_WAIT_REQ`, ncr53c90.cpp:652
  `fifo_pos == 1`). Charging the whole remainder armed a ~75 ms
  deferral per byte-tail XFER and wedged the Mac OS 8.1 boot in an
  endless retry loop (the probe log showed the same transaction
  cycling with 1.88 M-cycle gaps). (2) **a live completion absorbs
  the scheduled one**: `raiseIrq` now clears matching `pendBits_` —
  MAME's bus_complete fires once, when the counter is exhausted AND
  the FIFO has drained; our buffered model splits that into a
  scheduled fetch and an instant drain-completion, and when the CPU
  out-drains the schedule the stale deferral used to fire *after* the
  driver had consumed the completion — a phantom bus-service
  interrupt into the async SIM's ISR.
- **Still open from step 9** (tracked in `docs/LLE_VS_HLE.md` §3): the
  true tcounter↔FIFO staging engine (payload still short-circuits
  `dataIn_`/`dataOut_` around the physical FIFO; the carefully pinned
  Q6.3-Q6.6b driver interactions make that a high-risk rewrite),
  16-bit BUSMOD DMA widths, SDTR sync-negotiation messages, and the
  selection-timeout delay on empty IDs (kept instant — MAME itself
  ships a `DELAY_HACK` for it; the boot-time bus scan would otherwise
  spin ~250 ms × 6 IDs of virtual time in every etalon).

## 2026-07-22 — `docs/LLE_VS_HLE.md` third pass: inventory re-synced to the live tree

The LLE/HLE inventory was re-verified against `src/` after the Mac II
ADB PIC1654S default, Cuda wire redo, and SCC peer-hold landed. Drift
fixed (the doc still said ADB LLE was "not the default yet" / opt-in
while `AdbVia.cpp:34-49` had already flipped it):

- **ADB modem**: LLE is the default when `roms/adbmodem/342s0440-b.bin`
  is present; HLE = missing dump or `POM68K_ADB_LLE=0`. Principle + §2
  + migration step 11 now agree with the "Mac II LLE ADB default"
  entry below.
- **§1.7 factory PRAM**: real policy documented — `Rtc` reseeds only
  SPConfig when `'NuMc'` is present; `Egret` always rewrites the
  classic block + seeds video sPRAM `$58=$83`; both honour
  `POM68K_APPLETALK=1` → `$21`.
- **§1.9** (new): Slot Manager ORB → `armShiftComplete` phantom SHIFT
  listed as an HLE-path-only leftover (`MacIIMemory.cpp:273-290`,
  gated `!lle()`).
- **§3/§4**: CPU periph batches are 64/128/256 (`Cpu020`/`030`/`040`);
  `Pic1654s`/`AdbLine` counted as pure LLE; `DeclRom::buildSynthetic`
  as host convenience; Cuda dumps under `roms/cuda/` noted for step 10.

Inventory stays the living status board; the *why* of each LLE step
remains in the dated entries below.

## 2026-07-22 — LLE step 7: Cuda/Egret wire-model redo (the per-reader hacks are gone)

`Egret.cpp`'s reply wire now follows the real Cuda protocol (DingusPPC
`viacuda.cpp` as design oracle, MAME `cuda.cpp`/real firmware as the
timing reference) instead of a 4-byte HLE header with per-reader
patches. The three accommodations named the "#1 remaining fidelity
debt" in `docs/LLE_VS_HLE.md` §1.6b — the ReadXPram `$76` echo-pop
(Q6.5), the GetPram 2-byte header erase (Q6.4), and the Q8.2 echo-slot
data duplication — are **deleted**, along with the long/short
one-second-tick heuristic (`firstTick_`). 49/49 gates + the Finder
boot matrix stay green, bare no-FPU included.

What the redo actually is:

- **Real framing**: replies are `[type, flags, cmdEcho, data…]`, errors
  `[$02, errCode, pktType, cmd]`. The **attention byte is a wire event,
  not a buffer byte** (a dummy SHIFT with a stale SR): session close-ack
  +61 µs, initiated-packet attention +30 µs, host command byte ack
  +71 µs, response byte +88 µs, TREQ re-assert +13 µs (DingusPPC's
  measured schedule). That separation is the whole fix: the ROM
  device-manager ISR counts the close-ack as its discarded "sync" (4
  header bytes before data), the direct pollers and Mac OS 8.1's
  System-side reader consume it in their send ritual (3 header bytes
  before data) — every reader lands on the right byte *naturally*,
  which is why the hacks could go. The Egret flavor keeps its pinned
  LC II wire (buffer byte 0 doubles as the attention byte) with the
  real-framed header behind it.
- **BYTEACK edges are session-gated**: the ROM's `ori #$30` close
  raised TIP and BYTEACK in one write and the old code counted the
  BYTEACK half as a command byte — every captured command had its last
  byte duplicated, and WriteXPram wrote one extra adjacent byte. Ghost
  idle-bus polls ($408A9Cxx) get dummy SHIFTs like DingusPPC's null
  handler.
- **Commands $02/$08 are READ/WRITE_MCU_MEM with a 16-bit MCU address**:
  PRAM lives at $0100-$01FF, $0000-$00FF is 68HC05 scratch RAM (new
  `mcuRam_`). Wire captures (EGRET_CMD_LOG) showed both Systems writing
  a parameter block at MCU $B3 and reading it back via $A1 — the old
  8-bit model was silently corrupting PRAM $B3-$B5. PRAM/MCU reads are
  now genuinely **open-ended streams** (the fixed 32-byte push is gone;
  the host terminates the session after its count, as pinned in O6.11).
- **One-second packets obey pseudo command $1B** (mode 0 off / 1 full /
  2 header / 3 single tick byte; first packet after a change is always
  full, per the ERS). Captures showed the LC II ROM sending `$1B 00`
  in its first commands and both Sys 7.5 and Mac OS 8.1 sending
  `$1B 03` — the heuristic was reimplementing a real command we
  ignored. Power-on default is mode 1 (the boot heartbeat the LC II
  ROM's $A15376 reader consumes). Also implemented from the captures:
  $14/$16 autopoll rate, $19/$1A device bitmap.
- **The Quadra's Cuda seconds ran 1.6× fast**: `Egret` assumed
  15.6672 MHz but `Q605Memory` ticks it at 25 MHz. The constructor now
  takes the machine clock (µs pacing needs it anyway). Consequence:
  every seconds-keyed boot wait takes its true duration — the
  AppleTalk-active Mac OS 8.1 image's LAP timeouts most of all —
  so `q605_barefpu_boot_etalon`'s budget was re-pinned to 30 000
  frames (bare-SANE Finder lands around clk 10.2G) and
  `finder_boot_matrix`'s Q605 cell from 6 000 to 30 000 (its early-exit
  keeps passing cells cheap). The same pass fixed a latent matrix bug
  the slower boot exposed: the Q605 in-loop early-exit was looser than
  the final Finder check and broke out on the "Welcome to Mac OS"
  splash (menu.mean>170 with >35 contrast, no deviation test) — it now
  uses the strict criteria. All 9 Egret/Cuda matrix cells (Q605 × 8.1
  / 7.5 / 7.5.5 / 7.6 / GISTPERSO, LC II × boot.vhd / 7.1 / 7.5 /
  7.5.5) re-verified PASS on the new wire.

`egret_test` re-pins the header as `[attn, type, flags, echo]` and the
$1B mode machinery. HLE remaining in the Egret/Cuda (tracked in
`docs/LLE_VS_HLE.md` §2): it is still a packet-level HLE of the 68HC05
firmware — the next rung is the firmware itself (migration step 10).

## 2026-07-22 — AppleShare bridge vendored: netatalk 2.4.9 + TashRouter

The path from the emulated Chooser to a host folder is now fully in-tree
(user request: vendor like Moira, nothing gitignored):

- **`extern/netatalk2`** — netatalk 2.4.9 pristine sources (tag
  `netatalk-2-4-9`, GPL-2), the last AFP-over-DDP server; 3.x dropped
  AppleTalk and 2.x left modern distros. `tools/netatalk2/
  build_netatalk2.sh` builds it hermetically: static Berkeley DB 5.3 +
  libgcrypt/libgpg-error fetched with pinned sha256, everything under
  gitignored `extern/netatalk2-build/` — `afpd`/`atalkd` 2.4.9 verified
  running, kernel `appletalk` module present on Ubuntu 6.14.
- **`extern/tashrouter`** — TashRouter (MIT) vendored; its `LtoudpPort`
  speaks our exact LToUDP format (interop verified live earlier today).
- **Bridge**: `appleshare_bridge.sh` (sudo: appletalk module, veth pair +
  macvtap-on-veth so the DDP segment never touches the LAN, generated
  atalkd/afpd/AppleVolumes configs serving `input/` as "Input", guest
  login) + `router.py` (LToUDP ⇄ pomtap0, zone "POM68K") +
  `tools/netatalk2/README.md`. Also GUI: the RTC now seeds from the
  host's local wall clock at launch (the battery file froze while off).

## 2026-07-22 — LLAP two-System etalon: real address acquisition between two Systems

`llap_two_system_etalon` boots TWO full Mac II machines under System 7 on
a shared virtual LLAP cable and watches the real LAP Managers negotiate:
each side transmits ~650 ENQ probes (dst = src = tentative ID), hears the
other's traffic through the cable, and settles on distinct node IDs —
in ~12 s emulated (System 7 opens `.MPP` right at boot). Boots are offset
~2 s so the Ticks-seeded random IDs differ, as on real desks.

Enabler: **`POM68K_APPLETALK=1`** seeds SPConfig = `$21` (printer port =
LocalTalk) in both PRAM paths (`Rtc::factoryDefaults` for Plus/Mac II,
`Egret::factoryDefaults` for LC II/Quadra); the default stays the
deterministic AppleTalk-off `$22`. Learned: **System 6 never opens
`.MPP` headless** — it only opens lazily from the Chooser/apps, so the
first Sys 6 attempt produced zero wire traffic; Sys 7 is the vehicle.

## 2026-07-22 — LLAP milestone 1: SCC receive path + LToUDP virtual cable

The LocalTalk plan's first milestone (TODO): the SCC LLAP wire is now
**bidirectional** and frames travel between instances over UDP.

- **`Scc8530` Tx capture**: SDLC frame bytes accumulate across
  `writeData`; the Tx underrun (frame complete — CRC + closing flag on
  real hardware) hands the raw frame to `onTxFrame`; Send Abort discards
  it. **Rx path** (was `return 0` — dead line): `injectRxFrame` queues a
  frame (real CRC-16/X25 FCS appended), delivered at wire pace
  (`setByteCycles`, 544 cyc/byte @ 15.6672 MHz = 230.4 kbit/s) through a
  3-deep FIFO with Hunt exit/re-entry (the LLAP carrier sense, ext/status
  on WR15 bit 4), WR1 Rx-interrupt modes (first-char / all / special),
  SDLC Address Search (WR3 bit 2 vs WR6, $FF broadcast passes), overrun,
  and End-of-Frame + CRC-good status riding the last byte in RR1. RR2/RR3
  gained the Rx/special vector codes and IP bits.
- **RR0 latch fix** (all machines): only D7-D3 freeze at an ext/status
  latch; D2-D0 — including bit 0 Rx Character Available — always read
  LIVE (Zilog SCC UM §3.2). Freezing bit 0 hid every Rx byte from a
  driver with an unserviced ext/status pending.
- **`LtoUdp`** (new): the de-facto LocalTalk-over-UDP cable (Mini vMac /
  TashRouter interop): multicast 239.192.76.84:1954, 4-byte per-instance
  sender tag, no FCS on the wire. GUI opt-in `POM68K_LTOUDP=1` wires SCC
  channel B on Mac II / LC II / Quadra (Plus's inline loop: TODO).
  Mac II now ticks its SCC (it never did — DCD-only until today).

Gates: `llap_loop_test` — two SCCs on a virtual cable run the LLAP
address-acquisition dialogue (ENQ dst=src=ID both ways, foreign-ID
filtered in hardware, broadcast passes, aborted frames never sent,
hunt-exit IRQ as carrier sense); `ltoudp_test` — the real multicast
cable end-to-end (soft-skips where multicast is unavailable). Remaining
milestones (two-System etalon, RTS/CTS timing, netatalk bridge) in TODO.

<a id="2026-07-22-dir2hfs"></a>
## 2026-07-22 — dir2hfs: host folder → desktop volume (data-only flat-HFS façade)

`tools/dir2hfs.py` (machfs, repo venv `.venv-tools`) bakes a host folder
into classic-HFS volume(s) that mount as secondary SCSI disks: MacBinary
`.bin` decoded to native forks + Type/Creator, `.zip` expanded host-side,
`.sit`/`.hqx` typed for StuffIt Expander, CD images (`.toast`/`.cdr`/
`.iso`) extracted next to the output for direct SCSI attach. 31-char
MacRoman names sanitized/deduped, 1.9 GB first-fit split (`--max-mb`,
`--only`). Gate: `dir2hfs_selftest` (46 tests total). README documents
the workflow; `hdv/INPUT.vhd` (1845 MB, 141 files) baked from `input/`.

`ScsiDisk`'s flat-HFS façade now also detects **data-only** bare volumes
by the MDB `'BD'` signature at `$400` (zero boot blocks). The first
attempt stamped fake `'LK'` boot blocks instead — StartBoot scans SCSI
6→0, saw the higher-ID data volume before the ID-0 boot disk, believed
the LK, jumped into zeroed boot blocks and died pre-video in the ROM
serial-debugger stub (`$408BA0EA` on FF7439EE, 21 SCSI commands then
silence). Data volumes must stay honestly non-bootable. Mount proven
headless: OS 8.1/Q605 boots identically (5747 SCSI cmds = baseline) and
clears the volume's MDB clean-unmount bit through write-back.

## 2026-07-22 — Mac II LLE ADB default: mouse moves; three bugs, none where predicted

`macii_mouse_trace` PASSes under the PIC1654S LLE path and it is now the
**default** when `roms/adbmodem/342s0440-b.bin` is present
(`POM68K_ADB_LLE=0` restores the HLE byte-model; missing dump falls back
silently). The TODO ★ blocker ("PIC mis-routes the self-test loopback")
was three stacked bugs, and *none* was the predicted ST-edge aliasing —
`syncTo`'s burst-at-VIA-access interleaving is temporally exact, because
the VIA state only changes at VIA accesses (which sync first) and the CPU
only observes PIC effects through VIA reads (which also sync first):

1. **PIC instruction cost ignored** (`AdbVia::tickLle`): `Pic1654s::run(1)`
   returns 1–3 cycles (branches/skips 2, computed goto 3) but every
   instruction was charged one 34-cycle slot, so branch-heavy firmware
   (DECFSZ+GOTO delay loops = 3 cycles/iter) ran up to 2–3× too fast vs
   the 68k. Charging the real cost lands the firmware's wire timing
   exactly on the ADB spec — bit cell 1564 cyc ≈ 99.8 µs (spec 100 µs),
   attention 12410 ≈ 792 µs (spec ~800 µs) — where the old "calibrated"
   1054-cyc bit cell was the ×1.5 distortion of the 2-vs-3-cycle loop
   ratio. `AdbLine` constants recalibrated to the corrected wire
   (thresholds at the PIC's own pulse midpoints; `adbline_test` mirrors).
2. **Phantom SHIFT from the Slot-Manager ORB hack** (`MacIIMemory`): the
   `armShiftComplete()` re-arm fired on *every* VIA1 ORB write with
   ACR&$1C + IER.SHIFT — including the ADB driver's ST writes ($73DC ORB
   RMW), and its guard byte `$15D(A3)` is `ADBBase→flags` ($CF8), the very
   byte the driver mutates ($73E6 `bset #0`). The $7002 SHIFT ISR ran 400
   cyc after each ST=NEW — before the PIC produced a single CB1 clock —
   flipped ACR $1C→$0C ($7092 `bclr #4`), and the PIC later clocked 8 bits
   off a floating CB2: garbage command ($07) on the wire, then silence.
   Now gated off in LLE mode — the firmware's idle-timeout byte
   (0x044→0x065) provides the *real* SHIFT the $7100 POST wait needs.
3. **VIA mode-111 ext shift-out dropped bit7** (`Via6522::extShiftCB1`):
   the SR advanced on every CB1 falling edge, including the first — before
   the PIC's first read — so every ROM byte arrived `<<1` (Talk R0 $2C
   read as $58, the whole self-test ramp shifted). Real 6522: the MSB is
   presented from SR-load and rotates only after being consumed. Model:
   count rising edges (bits consumed), shift on the falling edges of the
   second and later cells.

With those three fixed the whole chain is honest: ROM self-test loopback
handshakes byte-per-byte, `ADBReInit` relocates kbd/mouse 2→15→2 / 3→15→3
over real Listen R3 wire frames, Sys 6/7 autopoll Talk R0 at the correct
addresses, and dy/dx bytes ($82/$83) reach the mouse driver.

**Bonus fix, default mode too — the Mac II cursor task never ran:**
the driver accumulated deltas into MTemp ($0828) but RawMouse/Mouse
stayed frozen, because `via2Ca1SlotTaskArmed()` misread the ROM's CA1
dispatcher tables: `($D08)` holds VIA2 PA *bit numbers* (slot 9/Toby =
bit 0, not 1) and `($D04)` is indexed by bit number, not loop position.
It never armed, so the slot VBL → SlotVInstall queue → `jCrsrTask`
(MTemp→RawMouse coupling) never fired after boot. Rewritten against ROM
$40806284/$62B0 semantics (gate: `macii_mouse_trace` + all etalons in
both ADB modes). Diagnostic tracers kept: `POM68K_ADB_LLE_TRACE=1` (wire
edges + decoded commands), `POM68K_ADB_PIC_TRACE=1` (ST samples, PIC
port writes, VIA1 SR/ACR/ORB/IFR traffic).

## 2026-07-22 — Mac II ADB goes LLE: real PIC1654S transceiver (opt-in)

The HLE `AdbVia` byte-model is fundamentally too coarse for the Mac II:
its fixed timer misses the ROM's rapid ST transitions and drops ~99.99 %
of `ADBReInit`, so the ROM maps the mouse to a phantom address and it
sits frozen (`macii_mouse_trace` reproduces it). Rather than keep
patching the shim, start replacing it with the *real* transceiver
firmware — the way MAME does — behind `POM68K_ADB_LLE=1` (default stays
HLE, all gates green, no regression).

- **`Pic1654s`** (new): PIC16C5x-family 12-bit core, the GI/NMOS PIC1654S
  Apple used as the ADB Modem 342S0440-B. Ported from MAME `pic16c5x.cpp`
  with the `0x1654` quirks (OPTION/SLEEP/TRIS = NOP, port read = pins AND
  latch, `/8` clock, external-RTCC timer). Runs the dumped
  `roms/adbmodem/342s0440-b.bin` (CRC `cffb33eb`). Gate `pic1654s_test`.
- **`AdbLine`** (new): bit-serial ADB keyboard+mouse device model
  (attention/sync/bit/stop, SRQ, Listen-R3 reassign, change-detected
  Talk R0), ported from MAME `macadb.cpp`, timed to the PIC's own wire
  rate. Gate `adbline_test`.
- **`Via6522::extShiftCB1`**: external-clock CB1 shift for the ADB modes
  (rising-edge, matching the firmware's shift routines).
- **`AdbVia`**: runs the firmware, wires the ports to the VIA shifter and
  the ADB line, and steps the PIC cycle-synced at each VIA1 access
  (`MacIIMemory::viaAccess` → `syncTo`).

Two non-obvious fixes made it run end-to-end (PIC receives ROM commands,
drives the ADB bus, `AdbLine` decodes them): **ST-idle pull-up** — PB4/PB5
read high when the 68k leaves them as inputs, so idle reads as ST=IDLE(3)
instead of a spurious NEW that made the PIC RESET-loop; and line timing
**recalibrated to the PIC's own delay loops** (bit cell ≈1054 CPU cyc),
not MAME's abstract 2 MHz ticks (which were ~2× too large — attention was
never even detected).

Not the default yet. Remaining blocker (TODO ★): the PIC mis-routes the
ROM's ADB self-test loopback as a command because `syncTo` runs it in
bursts (VIA state frozen mid-burst) and aliases the fast ST edges → fix
is cycle-exact PIC↔CPU co-stepping. Full architecture + repro in DEV.md
"Mac II ADB: PIC1654S LLE".

## 2026-07-21 — Bare no-FPU solved: _FP68K binds the integer PACK 4 (Cuda XPRAM echo bug)

The "LLE step 5" enigma is closed: `POM68K_Q605_NOFPU=2` (true
`FPUModel::NONE`, no soft 68882) boots Mac OS 8.1 to the Finder — new
gate `q605_barefpu_boot_etalon`. The chain was captured with a Moira
watchpoint on `$15AC`, which required new logical-address watchpoint
hooks in the translated MMU paths (`mmuRead`/`mmuWrite` for the 030,
`mmu040Read`/`mmu040Write` for the 040 — they bypass `readM`/`writeM`
where the debugger checks live; the crashed session had hooked only the
030 pair).

1. **The System-side selector is the ROM-resource combo.** Every entry
   of the ROM resource directory carries an 8-byte combo mask (FPU
   PACK 4 entry @rom `$E99F0`, mask `$70000000` = bit indices 1-3;
   integer PACK 4 @`$73910`, mask `$08000000` = bit 4). The
   InitResources walkers (boot `$4081AB28`, System-era `$408A07A6`)
   test entry bit N where **N = XPRAM byte `$AE`** (`_ReadXPRam`
   `#$1_00AE` at `$4081AB44`/`$408A07D0`).
2. **Validation** (`$4084BF86`): N = 0 or > max (dir header byte = 4)
   falls back to `UniversalInfo+$16` (defaultRSRCs, = 4 on the LC 475
   record at `$408A8080`); combo 4 is *promoted* to 3 when HWCfgFlags
   bit 12 says "FPU fitted". There is **no 3→4 demotion** — zapping the
   PRAM is Apple's own cure for a stale FPU combo.
3. Our inputs were all correct (XPRAM `$AE` = 0, defaultRSRCs = 4,
   HWCfg `$EC00`), and the boot-time pass did bind sanely — but the
   second InitResources pass, running through **Mac OS 8.1's own Cuda
   reader**, got `$02` from `_ReadXPRam($AE)`: that reader consumes a
   3-byte reply header where the ROM device-manager ISR consumes 4, so
   it took our READ_XPRAM **command echo** as the data (wire-traced:
   the session consumed exactly `01 00 00 02` then closed). Combo 2 =
   "FPU fitted" → map rebuilt with the FPU PACK 4 → its first
   `fmove.l fpcr,d0` (`$408E9AC0`) → dsNoFPU.
4. **Fix** (`Egret.cpp` Q8.2): the Cuda ReadXPram reply duplicates the
   first data byte into the echo slot — the ROM ISR is position-based
   and never verifies that echo, and the System reader now gets real
   data at its index 3. Exact for every 1-byte System read (combo,
   boot flags); multi-byte System reads stay off-by-one as they always
   were. The real cure — reply framing `[type, flags, cmdEcho, data…]`
   (DingusPPC `viacuda.cpp response_header`) plus an *unclocked*
   turnaround sync byte — hangs the ROM pollers, which wait on SHIFT
   for their sync byte; the wire-model redo is logged in
   `docs/LLE_VS_HLE.md`.

Gates: q605 suite 7/7 green, new `q605_barefpu_boot_etalon` (Finder
640×480×8, menu 204.4/70.1, desktop 129.6/31.3, SCSI 4341); full sweep
below.

## 2026-07-21 — DAFB extracted into Dafb.h/.cpp (one concern per file)

The DAFB II cell (register file, Swatch CRTC + interrupts, Antelope
RAMDAC/CLUT, Gazelle clock generator, monitor sense, frame clock) moves
out of `Q605Memory` into its own `Dafb` class, mirroring MAME's
device split: `Q605Memory` keeps only the MEMORY-CONTROLLER concerns —
the MEMCjr 6+6-bit bus-holding wrappers over the 12-bit window
(djmemc.cpp `dafb_holding_r/w`), the byte-lane assembly, and the VRAM
(bus decode + host-side rendering). The public `Q605Memory` API is
unchanged (forwarders); `dafb()` exposes the cell. Code moved verbatim
from the step-6 parity pass; behaviour identical — full 41/41 sweep
green.

## 2026-07-21 — LLE step 6: DAFB toward MAME parity (Swatch CRTC, Gazelle, sense)

The MEMCjr DAFB cell now implements the dafb.cpp semantics it was
hard-coding around:

- **Swatch CRTC timing registers** (`$124-$148` horizontal, `$14C-$164`
  vertical, 12-bit, half-line vertical units) are stored and
  `recalc_mode()` derives hres/vres/htotal/vtotal from HAL/HFP/VAL/VFP/
  HPIX/VFPEQ on each PCBR0 write — including the AC842a clock-divider
  bits, the convolution branch (register parity; MEMCjr machines never
  enable it) and interlace doubling. Exposed as
  `dafbHres()/dafbVres()`.
- **Gazelle clock generator** (`$3C3` byte port): the 20-bit M/N/P word
  is bit-banged in on rising clock edges; pclk = N/(M·P) × 31.3344 MHz
  (`dafbPixelClock()`).
- **Frame timing follows the guest**: `tick()` derives the frame length
  from the programmed `htotal × vtotal / pclk` instead of a hard-coded
  60 Hz/525 lines (legacy shape kept until the ROM programs the CRTC).
  At the OS 8.1 Finder the guest programs 896×525 at a Gazelle pclk of
  30 253 903 Hz — the machine now runs the VBL at the rate the driver
  asked for.
- **Extended monitor sense** (`$1C`): drive-pins write + ext(bc,ac,ab)
  read-back composition (plain type 6 = 13" Hi-Res stays the default;
  `setDafbMonitor()` selects others).
- **Swatch mode bit 0** (display disable) is honored via
  `dafbBlanked()` — set at reset, cleared by the driver, like MAME's
  `screen_update` early-out.

Gate: `q605_dafb_test` grows 11 checks (CRTC round-trip + derivation,
Gazelle programming, VGA extended sense dance, blank bit). A probe run
confirms the real ROM exercises the whole pipeline (640×480, mode 3,
unblanked, Gazelle-programmed pclk). Full 41/41 sweep green.

## 2026-07-21 — LLE step 5: UniversalInfo FPU masking deleted; bare no-FPU fully mapped

The §1.6 guest-state machinery (`romByteMasked` read-mask on HWCfgFlags
bit 28, the low-mem stale-copy scrub, the clock-gated
`maybePatchRomNoFpu`) is **deleted**. It was already unreachable —
nothing armed `romNoFpuPending_` since the soft-FPU path landed — and it
is provably unnecessary: with Moira's architectural format-$4 F-line,
the ROM's own fnop probe (site `$40804654`, VBR-shifted handler
`$40847054` → `jmp (a6)` with D1=1) concludes "no FPU" by itself and
builds HWCfgFlags `$EC00` (FPU bit 12 clear) with zero outside help.
`POM68K_Q605_NOFPU=1` (68LC040 + soft 68882, SoftwareFPU-equivalent)
stays the supported no-FPU configuration; ROM reads are now always the
plain bytes.

New experiment knobs: `POM68K_Q605_NOFPU=2` selects TRUE bare
`FPUModel::NONE`, and `POM68K_Q605_ID=<hex>` overrides the `$5FFFFFFC`
board ID (LC 475 `$A55A2221` default; Q605 `$A55A2225`; LC 575
`$A55A222E` — MAME macquadra605.cpp).

Bare-NONE status, fully mapped for the follow-up (probes in the
2026-07-21 session): the boot goes deep (SCSI 2733, HWCfg self-clears,
board ID already LC 475) and dies at the FIRST `_FP68K`: Mac OS 8.1's
boot binds `_FP68K` (tool table `$E00 + 4×$1EB = $15AC`) to the ROM's
FPU PACK 4 (`$408E9A2C`, combo `$70000000`) at the same moment it
installs its F-line handler (`$2749E`, ~frame 1019); that handler
(frame word `$002C` + FPU coprocessor id → dsNoFPU trampoline) then
kills the FPU accelerator's very first `fmove.l fpcr,d0`
(`$408E9AC0`). A real LC 475 must bind the ROM's integer PACK 4
(`$073940`, combo `$08000000`); the System-side selection input is the
one remaining unknown (decision code near RAM `$25974`). The LC 575
board-ID experiment stalls in POST for unrelated hardware reasons.

Gates: `q605_boot_etalon`, `q605_nofpu_boot_etalon`, `sst68040`,
`q605_dafb_test`, `q605_asc_test` green; full sweep below.

## 2026-07-21 — LLE step 4: Mac II EvQ soft-post deleted — alerts dismissed over real ADB

`MacIIMemory::postKeyReturn` / `maybeDismissBootAlerts` (which wrote a
synthetic keyDown EvQEl straight into guest memory at `$0F00` and
patched the event queue headers) are **gone**. Probing showed the ADB
modem path is healthy enough to deliver keystrokes during the Sys 7
EtherTalk CautionAlerts — the old "ST stuck EVEN" wedge is covered by
`AdbVia::tick`'s dead-timer re-arm. The dismissal now lives where it
belongs: `macii_sys7_boot_etalon` and `finder_boot_matrix` press Return
**over ADB** (`keyEvent $24`, ~100 ms hold) when a modal (`CurActivate`
bit 31) sits on a stalled boot — exactly what a user at the keyboard
would do; in the GUI the user clicks OK themselves. The machine no
longer touches guest state anywhere on the Mac II boot path.

Gates: `macii_post/boot/sys7` etalons + matrix Mac II × Sys 7.0
(menu 0.09 / desk 0.44 / SCSI 1045) green.

## 2026-07-21 — LLE step 3: real LLAP carrier sense — LocalTalk watchdogs deleted

The pointer-chasing LocalTalk watchdogs (`V8Memory` / `Q605Memory`
`localTalkWatchdog`, which cleared `.MPP` globals when a send looked
wedged) and the never-compiled Basilisk-style `RsrcPatcher` `ltlk` stub
are **gone**. Three SCC gaps, found by wire-tracing an AppleTalk-active
OS 8.1 boot with the watchdog off (SCCDBG=1), were what actually wedged
the LAP:

1. **RR15 read returned 0** instead of WR15 — the level-4 ISR reads the
   ext-IE mask to route the source; with 0 every ext/status looked
   disabled and the LAP state machine never advanced.
2. **The standing Break/Abort fired in async modes too.** An open line
   is an SDLC ABORT only while hunting (WR4 bits 5-4 = 10); continuous
   marks in async are normal idle. Presenting bit 7 unconditionally fed
   an interrupt storm to OS 8.1's async channel-B setup (WR4 `$4C`).
3. **RR0 bit 4 (Sync/Hunt) was never set.** This is the LLAP carrier
   sense: the sender enters hunt (WR3 bit 4, written 514× in the trace)
   and treats "still hunting" as "no carrier, clear to send". With
   bit 4 stuck 0 the driver saw a permanently busy line and never
   transmitted a single byte.

With hunt + the new Tx Underrun/EOM latch (RR0 bit 6, WR0 `$C0` reset,
Send Abort, EOM ext/status interrupt on frame completion — Zilog SCC UM),
the LAP now runs its real protocol: the trace shows 1254 data bytes of
`01 01 81` **LLAP ENQ address-acquisition probes**, no reply on the dead
line, address kept, AppleTalk up. Q605 × OS 8.1 reaches the Finder with
the watchdog deleted, and an LC II probe with XPRAM `$13` forced to
AppleTalk-ACTIVE boots System 7.5 to the Finder the same way.

Gates: `scc_ext_test` grows async-no-abort + Tx-underrun/EOM sections
(28 checks); full 41/41 sweep green. `POM68K_NO_LTALK_WD` no longer
exists; `SCCDBG=1` traces every SCC register access.

## 2026-07-21 — LLE step 2: per-tick SPConfig clamps removed (all three machines)

The tick-time guest-state clamps that forced XPRAM `$13` / low-memory
SysParam `$1FB` to `$22` on every `tick()` (`Q605Memory`, `V8Memory`,
`MacIIMemory` — `docs/LLE_VS_HLE.md` §1.3) are **deleted**. The
reset-time `factoryDefaults` seed (Egret / Cuda / Rtc, which always
reseeds SPConfig `$22` even over a persisted `.pram`) is now the only
AppleTalk-inactive policy, and it is sufficient: with the RTC/Egret
XPRAM paths actually working, the SysParam restore hands the guest
`$1FB = $22` legitimately.

Consequence surfaced by the gates: the Infinite Mac OS 8.1 image
re-activates AppleTalk from its on-disk prefs during boot (its right —
a real Quadra would do the same), so the Q605 boot now goes through the
`.MPP` LAP no-peer timeouts and reaches the same 256-color Finder a
little later. `q605_boot_etalon` broke on that: its early-exit sampled
the screen on "depth 8" alone (true long before the desktop is drawn)
and its 2.5 G-cycle budget was tuned for AppleTalk-off boots. Fixed the
gate, not the guest: early-exit now requires the full Finder
menu/desktop signature (like `finder_boot_matrix`) and the budget is
5 G cycles; the nofpu variant matches.

Gates: full 41/41 sweep green clamp-free (both LC II Sys 7 etalons, Mac
II Sys 6/7, Q605 8.1 + nofpu).

## 2026-07-21 — LLE step 1: Mac II boots an UNMODIFIED ROM (RTC was mute, then bit-shifted)

The three load-time Mac II ROM patches (forced StartBoot wantType,
retargeted drive matcher, `$B0E` btst bypass + checksum repair —
`docs/LLE_VS_HLE.md` §1.1) are **deleted**. They papered over two
wire-level bugs in the 343-0042 RTC model:

1. **/enable polarity inverted** at the `MacIIMemory` VIA-PB call site
   (PB2 passed as-is instead of `!(PB2)` like the Plus): the shifter was held
   in reset exactly while the chip was selected, so the Mac II ROM had
   **never completed a single RTC command** — every PRAM read floated to
   `$FF` ("virgin PRAM" on every boot), which is what the patches worked
   around.
2. **Read bit-phase one edge early**: the model presented bit 7 on the
   command byte's own completion edge instead of the next falling edge
   (MAME macrtc `--m_bit_count`). Once traffic flowed, the host sampled
   bits 6..0 plus a trailing idle 1 — every byte read back as
   `(v << 1) | 1`. GetDefaultStartup's `$FFDF` arrived as `$FFBF`, the
   `'NuMc'` validity readback failed, and the ROM re-initialized XPRAM
   cold on every boot; StartBoot's ddType hunt then sought type 3
   (corrupted `$0001` → `$0103`) and never matched the DDM's `$0001`.

`Rtc` now implements the **full 256-byte extended XPRAM protocol**
(`(cmd & $78) == $38`, two-byte address, MAME `macrtc.cpp` semantics),
shifts on the **falling** clock edge, uses MAME's unified classic-address
mapping (classic regs 8-11/16-31 = XPRAM `$08-$0B`/`$10-$1F` — SysParam
byte 3 = XPRAM `$13` = SPConfig), and `factoryDefaults` seeds the
Basilisk `XPRAMInit` block verbatim ('NuMc', DynWait, SPConfig `$22`).
The unpatched ROM cold-inits its own PRAM, reads back what it wrote,
sets its own startup defaults (`$78-$7B` = `FF FF FF DF` → driver refNum
-33 = SCSI ID 0) and boots the SCSI disk unaided.

Method: RTCDBG wire trace (zero traffic → polarity), then capstone
disassembly of `$15BE`/`$7AE0`/`$7B08` + a Moira breakpoint showing
wantType=3 arriving from a corrupted read — the `(v<<1)|1` signature
(`$DF→$BF`, `$0001→$0103`) identified the phase bug.

Gates: `macii_post_etalon`, `macii_boot_etalon`, `macii_sys7_boot_etalon`
green on the unpatched ROM; `rom_boot_etalon` + `input_etalon` (Plus
classic protocol) green on the falling-edge model.

## 2026-07-21 — Plus keyboard regression (6522 SR auto-shift) + nofpu gate floor

Full 41-gate sweep found two reds:

1. **`input_etalon`** — the Mac II Slot-Manager POST work (commit
   `0901f55`) made `Via6522::write(SR)` auto-raise IFR.SHIFT ~16 φ2
   clocks after any host SR write with ACR2-4 ≠ 0. Two bugs vs real
   silicon, found by instrumenting the M0110 transaction: (a) the
   auto-completion also armed in the **external-clock** shift modes
   (011/111), where a real 6522 only shifts on the peripheral's CB1
   edges; (b) the Plus keyboard driver does an ACR `$18→$00→$1C` dance
   before each command, and the completion armed by the throwaway `$18`
   (mode 110, φ2) SR state **survived the mode switch** and fired ~16
   clocks into the external-clock command — an early SHIFT #1 that made
   the driver flip to shift-in prematurely, re-issue Inquiry forever,
   and lose every key transition. Fix: arm the auto-completion only for
   internally-clocked modes (001/010/100/101/110, R6522 §2.4) and cancel
   it whenever SR is written in — or ACR switches to — disabled/external
   modes. The Mac II NuBus card-clocked path keeps its explicit
   soft-flag-gated `armShiftComplete()` in `MacIIMemory`.
2. **`q605_nofpu_boot_etalon`** — reached a perfect 8bpp Finder but
   failed its `SCSI > 5000` floor at 4954; eased to 4000, the same
   variance allowance the main `q605_boot_etalon` got under the SPConfig
   clamp (entry below).

## 2026-07-21 — Finder matrix Phase A complete (all four machines)

`finder_boot_matrix` (ROM CRC × System image) is fully green on the four
supported profiles — this is the Phase A/B result table the matrix plan
called for (details in the dated entries below):

| Cell | Result |
|---|---|
| Plus v1/v2/v3 (`4D1EEEE1`/`4D1EEAE1`/`4D1F8172`) × Sys 5.1 / 6.0 / 6.0.8 + HD20SC | PASS |
| Mac II v1/v2 (`97851DB6`/`9779D2C4`) × Sys 6.0 / 6.0.8 + HD20SC | PASS |
| Mac II v2 × Sys 7.0 (SCSI≈1207) / 7.1 (SCSI≈1342) | PASS — SPConfig `$22` + EvQ Return dismiss (`macii_sys7_boot_etalon`) |
| LC II (`35C28F5F`) × boot.vhd / Sys 7.1 (SCSI≈746) / 7.5 / 7.5.5 (SCSI≈3231) | PASS — SPConfig `$22` clamp, **no** EvQ dismiss |
| Q605 (`FF7439EE`) × OS 8.1 / Sys 7.5 / 7.5.5 / 7.6 / GISTPERSO | PASS — 8.1 @8bpp; 7.x often 1bpp until Monitors; 53C96 polled-WRITE fix |

No FAIL cell remains, so Phase B ("fix emulator bugs before adding
machines") is closed too; the matrix's next phase is new machine profiles
(TODO Phase C). Remaining optional cell: Plus floppy System 4.1.

<a id="2026-07-21-q605-sys755-76-finder"></a>
## 2026-07-21 — Q605 Sys 7.5.5 / 7.6 → Finder (53C96 polled WRITE)

System 7.5.5 / 7.6 hung pre-Finder on the Quadra 605 (SCSI≈1838,
`MBarHeight=0`) in the SCSI Manager async wait (`$0C0C` device-record
`$BE`/`$C0` gate). Last CDB was WRITE(10); `lastCmd=$10` (non-DMA
Transfer Info). OS 8.1 was fine because it drains DATA OUT through the
pseudo-DMA window (`dmaWrite`); the 7.5.5 SCSI Manager 4.3 HAL feeds
writes via R_FIFO instead.

Root cause: `Ncr53c96::transferInfo` DATA OUT only called `updateDrq()`,
and `fifoPush` never gathered payload into `dataOut_` — so polled
WRITE never raised `I_BUS`. Fix: arm `dataXfer_` + reload `tcounter_`
from `tcount_` (or remaining payload) on CI_XFER `$10`, and route
DATA OUT FIFO writes through the same gather/complete path as
`dmaWrite` (`acceptDataOutByte_`).

Gates: `finder_boot_matrix q605` × 7.5.5 / 7.6 / 7.5 / 8.1 PASS;
`ncr53c96_test` adds a polled WRITE(10) case; `q605_boot_etalon` green.

## 2026-07-21 — Q605 Sys 7.5 / GISTPERSO Finder at 1bpp; 7.5.5/7.6 hang

`finder_boot_matrix` Q605 cells: System 7.5 and `GISTPERSO-boot` reach a
real 640×480×1 Finder (light menu bar). Infinite Mac **7.5.5** and **7.6**
still fail — not a depth-gate false negative: SCSI freezes ~1800 cmds with
`MBarHeight=0` / empty `MenuList` while `WaitNextEvent` spins (full-screen
~50% dither, menu luminance ≈127). SPConfig is already `$22`. Same 7.5.5
image boots on LC II. Open follow-up: what 7.5.5 starts on Q605 that 7.5
does not (likely OT/extension path). Matrix 1bpp metrics accept dark
custom desktops (GISTPERSO); `q605_boot_etalon` SCSI floor eased 5000→4000
(variance under SPConfig clamp).

<a id="2026-07-20-lcii-sys71-755-finder"></a>
## 2026-07-20 — LC II Sys 7.1 / 7.5.5 → Finder (SPConfig clamp)

Infinite Mac LC II images self-heal SysParam SPConfig to `$01` (AppleTalk
active) when XPRAM `$13` is cold-zero — boot stalls at SCSI≈277 in an
EtherTalk CautionAlert (`CurActivate=$FFFFFFFF`, menu≈0.50). Fix: call
`Egret::factoryDefaults()` from `V8Memory::reset` and keep XPRAM `$13` +
SysParam `$1FB` at `$22` (AppleTalk inactive), same policy as Mac II RTC /
Egret O6.11. Soft EvQ Return dismiss (needed on Mac II) **must not** run
on LC II — it double-faults System 7.5.5 at ROM `$40A09A32`.

Gates: `finder_boot_matrix lcii` × 7.1 (SCSI≈746) / 7.5.5 (SCSI≈3231)
PASS; Sys 7.5 + `lcii_boot_etalon` still green. `Egret::factoryDefaults`
now always reseeds SPConfig even when `'NuMc'` is present.

<a id="2026-07-20-macii-sys7-finder"></a>
## 2026-07-20 — Mac II Sys7 → Finder (AppleTalk alert dismiss)

Infinite Mac System 7 selects EtherTalk with no NuBus ethernet, so boot
stopped in two CautionAlerts after SCSI ~274 (not a 5380 hang). ADB cannot
click OK (VIA1 modem often stuck ST=EVEN). Fix: keep SysParam SPConfig
`$1FB=$22` (AppleTalk inactive, same as LC II XPRAM `$13`), and once per
frame soft-post Return into EvQ while a modal `CurActivate` bit31 is set
and SCSI has stalled — clears both alerts without touching Sys6 Finder
(`CurActivate=0` at desktop). RTC factory defaults always reseed SPConfig
`$22` even when `'NuMc'` is already present.

Gate: `macii_sys7_boot_etalon` (soft-skip without `hdv/System 7.{0,1} HD.dsk`);
matrix confirms 7.0 SCSI≈1207 and 7.1 SCSI≈1342, menu≈0.12 / desk≈0.46.
`macii_boot_etalon` (Sys6) still green.

Earlier on the same path: after Welcome, ASC `mode=$18` / sticky half-empty
IRQ — Sound Manager writes MODE words whose high bits are **ignored on
silicon** (`data &= 3`, MAME/QEMU). Classic path now matches ASCTester/MAME:
MODE bits 0–1 only, status `$804` read clears IRQ+bits, half-empty is an
edge at cap `$1FF`, and **empty-cycle** re-IRQs once per 1 KB drain while
FIFO mode runs dry (QEMU). V8 (`$E8`) level semantics unchanged.

## 2026-07-20 — Classic ASC idle IRQ (Mac II)

Mac II's discrete ASC (`AscV8` version `$00`) was asserting the half-empty
IRQ whenever the FIFO sat below `$200` bytes — including a never-started
idle chip. That pins VIA2 CB1 once the ROM enables it and starves boot.
Superseded by the MODE-mask + empty-cycle fix above for Sys7; the idle
quiet rule remains. V8 (`$E8`) unchanged.

## 2026-07-20 — SCSI flat-HFS façade

`ScsiDisk::open` detects bare HFS volumes (`'LK'` boot blocks at LBA 0 —
Infinite Mac / Basilisk `.dsk`) and prepends an in-memory DDM + partition
map + `Apple_Driver43` template (same 96-block layout as
`tools/wrap_hfs.py`), including a ddType `$6A` driver entry for LC II
StartBoot. Write-back maps LBAs ≥ 96 onto the original flat file; the
synthetic prefix stays memory-only. Template search:
`$POM68K_SCSI_DDM_TEMPLATE`, then `HD20SC.vhd` / `boot.vhd` beside the
image or under `hdv/`. Gate: `scsi_hfs_facade_test`.

## 2026-07-20 — Mac II boots System 6 to the Finder

Post-Welcome stall at ~235 SCSI cmds was two bugs:

1. **NCR 5380 sticky CSR.REQ** during DATA_IN. Mac II SCSIMgr uses TIB
   `scInc` 512 × `scLoop` N; between chunks `$1E088` waits for REQ clear.
   REQ stayed asserted → hang mid-READ (512 of 1536). Fix: one-shot REQ
   gap after each DACK (`reqGap_`), re-arm on the next CSR read (Plus still
   polls REQ between PDMA bytes). Also: clear stale DMA IRQ on
   `enterCommand` / Start-DMA-Initiator-Receive; one IRQ edge on DATA→STATUS
   only (not again on MSG/BusFree).

2. **VIA2 CA1 with empty `$D04` slot queue → SysError(51)** then IPL2
   livelock / wild PC. Raise CA1 only when `via2Ca1SlotTaskArmed()`.

Gate: `macii_boot_etalon` green (menu/desk ≈0.10/0.49, cmds≈1162).

## 2026-07-20 — Mac II: overlay is a one-way latch

After Welcome to Macintosh the System rewrites VIA1 PA with bit4 set;
treating that as overlay-on remapped RAM to open bus and left the CPU
spinning at `$640000` (FFFF). Clear overlay only — never re-arm.

## 2026-07-20 — Mac II: PDMA $50F060xx must decode A0..A1

Boot selected SCSI and issued READ(10) LBA 96 (n=2), but blind `move.l`
PDMA via `$C08=$50F06000` only hit exact `$6060` — the three sibling
bytes of each long were open bus → 256 DMA bytes per 1024-byte xfer and
endless retry. Map `$6000–$7FFF` (and `$12000–$13FFF`) as DACK. 68020
has no `extBusError` path — soft-fail when DRQ is down (no MmuBusError).

## 2026-07-20 — Mac II: prefer SCSI boot over empty floppy

After `wantType=1`, `Apple_Driver43` does `_AddDrive` (DrvQ: floppy + SCSI
drives 8..14), but virgin `GetDefaultStartup` + `$B0E=$80FF` kept `BootDrive=1`
and never read HFS. `loadRom` now: (1) retargets `lea a3` at `$15D6` to
`$40801826`; (2) NOPs the `$B0E` btst at `$17F4`; repairs the checksum.

## 2026-07-20 — Mac II: StartBoot wantType=$FF was skipping Apple_HFS

Mac II POST is green; SCSI PDMA (`$50F12000–$13FFF`) delivers LBA 0 correctly,
but StartBoot never loaded a driver: virgin PRAM makes `GetTimeout` return
`$FFFF`, and `$40807B14` packs that low byte as the DDM `ddType` to seek.
Type `$FF` misses stock `ddType $0001`, then the non-1 path JSRs the driver
with A0 on the wrong PM block — endless READ(6) LBA 0, empty `DrvQHdr`,
gray floppy icon.

`MacIIMemory::loadRom` forces `wantType=1` (`moveq #1,d0` at `$7B12`) and
repairs the ROM checksum at `$0`. Boot walks PM and `_AddDrive`s; Finder was
still blocked by the floppy-first matcher (entry above).

## 2026-07-20 — O6.13: SCC word fast path + LC II NOFPU diagnosis

V8 `read16`/`write16` now handle the SCC window (`$F04000`) with a single
ctl/data side-effect and a mirrored data lane, so a speculative word access
cannot double-advance `Scc8530`'s register pointer. Gated by an extra check
in `scc_ext_test`. Mac drivers still use `move.b`; this is defensive.

**NOFPU / SANE (still open for a Finder gate):** `rominfo` confirms two
`PACK 4` resources and a Mac LC UniversalInfo at `$003BA6` with
`hwCfgWord $DC00` (FPU). `POM68K_NOFPU=1` (`FPUModel::NONE`) reaches
StartInit's `$F200` probe with `D3=$DC00`, takes vector 11 twice, then
spins the SysError dialog at `$40A02A38` (`$172`). Forcing the no-FPU
shape (`$CC00`, productKind 31, `rom85 $7FFF`) avoids the F-line but hangs
earlier at `$A499F6`/`$A49A7A`: `btst #16,D7` fails because bit 16 is only
`bset` on the successful FPU/AAAA path (`$48D30` / `$49BA0` after
`$F010`). Soft 68882 + `$CC00` hybrid still hangs — StartInit never reaches
the FPU probe. Default remains 68882 attached; true PACK 4 selection needs
a VIA-ID→UniversalInfo path that takes the FD/`$CC00` entry without
entering the bit-16 gate.

## 2026-07-20 — Q8.8: CACHE_BOOST calibration (default stays 1)

> **Superseded** by [2026-07-25 — Quadra 800 (26th machine), the 040 boost
> ceiling lifted…](#2026-07-25--quadra-800-26th-machine-the-040-boost-ceiling-lifted-and-the-pic-co-step-un-boosted):
> the "boost 2+ fails SCSI bring-up" note below was a stale symptom.
> `Cpu040`/`CentrisCpu` default to `cacheBoost_ = 4` since then.

Measured `POM68K_Q605_CACHE_BOOST` against `q605_boot_etalon` with the
FF7439EE + Mac OS 8.1 assets. Boost **1** remains the only value that
reaches the Finder (640×480×8, menu/desktop 204.4/141.2, SCSI≈5004,
~19 s). Boost **2/3/4** all exhaust the 2.5 G-cycle budget with
`SCSI=0`, DAFB still reset, and PC near `$408BA0EE` — the throughput
overlay races Cuda/SWIM/SCSI bring-up even when wait-state and SWIM
clocking are boost-invariant.

Made those paths boost-correct without changing the default: `Cpu040::stall`
scales machine-cycle wait states by `cacheBoost_`, `viaSync` aligns in
machine-cycle space, and `syncSwimFromCpu` converts Moira deltas through
`cacheBoost` before the C15M ratio. Raising the default above 1 is a
separate relative-timing milestone (not copyback/snooping).

## 2026-07-20 — Q8.6: SWIM2 SuperDrive media (MFM 1.44 + GCR)

`SonyDrive` accepts 1.44 MB HD images (80×2×18×512 @ 300 RPM) alongside
800K/400K GCR, with SuperDrive sense bits, MFM IBM System 34 track encoding
(MARK on A1 syncs), `nextByte` for SWIM2, sector R/W and `eject()`. `Swim2`
attaches two drives, maps phases→CA/LSTRB, mode motor/devsel/HDSEL/ACTION and
setup GCR/MFM, and clocks FIFO bytes from/to the selected drive. `Q605Memory`
owns `drive0_`/`drive1_`, `insertDisk`/`ejectDisk`, and ticks both spindles;
the Quadra GUI can insert/eject floppies under `disks35/`. Gates:
`swim2_media_test` (MFM/GCR/write-back) and `q605_floppy_boot_etalon`
(synthetic HD sector0 via the SWIM2 stack; ROM floppy boot still optional /
soft-skip). Plus/LC II IWM + `gcr_test` unchanged.

## 2026-07-20 — Q8.7: 040 I/D ATC + Cpu040 throughput overlay

Moira gains a 32-entry I/D ATC for 68040 translation (flush on PFLUSH*/TC/
URP/SRP; `POM68K_MMU040_WALK=1` forces walk-per-access). `Cpu040` arms the
030-style i-cache overlay with `POM68K_Q605_CACHE_BOOST` (default **1** —
boost 2+ fails SCSI bring-up; see Q8.8) and `POM68K_Q605_ICACHE_MISS`.
`sst68040` and `q605_boot_etalon` remain the non-regression gates.

## 2026-07-20 — Q8.5: 68LC040 NOFPU path (soft FPU; bare NONE = dsNoFPU 90)

`POM68K_Q605_NOFPU=1` selects Moira `M68LC040` with the 68882 kept attached as
a SoftwareFPU-equivalent so Mac OS 8.1 reaches the Finder under the LC 475 CPU
identity. Bare `FPUModel::NONE` still fails: GetHardwareInfo correctly clears
the FPU bit, System installs PACK 4's F-line glue (`v11` → RAM), then a raw
040 FPU opcode raises **SysError 90 (dsNoFPU)** and the ROM spins on MBState
at `$40802A38`. PACK 4 is not FPSP; format-$4 F-line (architectural LC040)
and format-$0 SANE glue both end there without Apple's FPSP. The UniversalInfo
bit-28 read mask remains available for a future true-NONE + FPSP attempt.
`q605_nofpu_boot_etalon` gates the soft-FPU LC040 Finder path; default boots
stay M68040+68882.

## 2026-07-20 — Q8.4: SWIM2 register/FIFO core replaces the zero stub

PrimeTime's `$5001E000` floppy window now uses a dedicated `Swim2` device
instead of returning zero. This first media-independent slice implements the
MAME reset state, mode set/clear, setup/phases, four rotating timing
parameters, the two-entry data/mark/CRC FIFO, clear-on-read errors and
read/write handshake levels. IOSB's register spacing and 16-bit lane behavior
are modeled explicitly so a word access has exactly one FIFO side effect. A
C15M-clocked no-media shifter supplies idle `$FF` read bytes and drains write
bytes; this is required by the ROM's SWIM speed calibration before SCSI boot.
Each SWIM bus transaction also applies IOSB's documented five-cycle wait state.

New `swim2_test` pins the chip register contract, PrimeTime bus wrapper and wait
state.
The real `q605_boot_etalon` still reaches the 256-color Finder. Drive selection,
flux timing and GCR/MFM media land in Q8.6.

## 2026-07-20 — Q8.3: Quadra 605 whole-machine boot gate

Added `q605_boot_etalon`, a soft-skipping whole-machine gate for the
user-provided FF7439EE ROM and Mac OS 8.1 disk image. It runs the 68040 machine
for up to 2.5 billion cycles, decodes the live main-GDevice framebuffer through
the DAFB CLUT, and requires the 640×480×8 mode, DAFB mode 3, Finder-like
menu/desktop luminance separation, and more than 5,000 SCSI commands. Missing
assets remain a clean pass, matching the existing LC II etalon discipline.
The real assets pass with DAFB base `$1000`, stride 1024, mode 3, menu/desktop
luminance 204/141 and 5,002 SCSI commands.

## 2026-07-20 — Q8.2: Quadra 605 PrimeTime/IOSB ASC stereo

The Quadra sound device is now the hardware-appropriate `AscIosb`, identified
as version `$BB` by ASCTester on a real LC 475, instead of the mono V8 `$E8`
stopgap. It implements separate 1 KB FIFO A/B channels, the four-bit stereo
FIFO status, writable A/B interrupt enables, PLAYRECA behavior, lockstep
22.257 kHz draining, and distinct left/right output. The discrete `$B0` EASC's
44.1 kHz SRC/CD-XA path is intentionally not conflated with this IOSB cell.

PrimeTime's pseudo-VIA2 now re-latches bit 4 when software acknowledges IFR
while the ASC interrupt line remains high. The host audio ring carries stereo
frames; existing Mac Plus and LC II mono samples are duplicated to both host
channels. `q605_trace` reports FIFO A/B traffic and the `$BB` identity.

New gate `q605_asc_test` pins version/register defaults, independent FIFO
feeding, stereo drain and samples, interrupt enable/level semantics, and
pseudo-VIA2 re-latching. The original `asc_test` remains unchanged and guards
the LC II V8 variant.

## 2026-07-20 — Q8.1: DAFB stride/depth model and 256-color host rendering

The Quadra 605 DAFB HLE now tracks the split `$000/$004` framebuffer base,
`$008` stride, `$010` configuration (including the fixed 1024-byte convolution
pitch), and Antelope RAMDAC PCBR0/PCBR1 pixel modes. The GUI and `q605_trace`
render using the live hardware depth/stride instead of relying on the main
GDevice PixMap while Mac OS is changing depth. DAFB reset now also clears the
CLUT and all mode/interrupt state. `q605_trace` emits a CLUT-decoded PPM for
1/2/4/8 bpp, reports the hardware geometry, and offers `--dafb-io N` to trace
DAFB plus MEMCjr holding-port traffic independently of the generic I/O budget.

New `q605_dafb_test` pins the MEMCjr 6+6-bit register protocol, stride
conversion/config override, all indexed depths, Antelope x555 selection, and
reset defaults. This raises the suite to 27 CTest gates.

The original Q8 diagnosis was too strong: the previous raw DAFB register file
already echoed basic stride/config writes to the guest. This change fixes the
host-side B&W/distorted rendering, but the reported guest crash still requires
a real Finder `SetDepth(8)` reproduction. That integration run was unavailable
in this checkout because the user-provided FF7439EE ROM and Mac OS 8.1 disk
image are absent.

## 2026-07-20 — Q7: Quadra 605 GUI profile, audio and ROM discovery

The Quadra 605 is now a third interactive GUI machine beside the Mac Plus and
LC II. A 1 MB ROM dispatches to `runQuadra`; the Machine menu can switch among
all three profiles. The machine thread exposes ADB input, reset, SCSI disks,
turbo/pacing and live DAFB framebuffer decoding. ROM fallbacks no longer depend
on stale hard-coded paths: `findRomBySignature()` scans `roms/` for the LC II
`35C28F5F` and Quadra `FF7439EE` signatures.

The IOSB EASC window at `$50014000` is connected to the existing `AscV8`
engine as a mono FIFO-A stopgap, clocked at C15M=15.6672 MHz from the 25 MHz
CPU and interrupting through pseudo-VIA2 bit 4. Boot traces consumed 31 690
non-silent FIFO bytes without regressing the Finder boot. A faithful stereo
EASC/version/FIFO-B implementation remains open.

Framebuffer base handling now accepts the 32-bit MMU alias published by
QuickDraw: because the VRAM aperture is 1 MB aligned, `baseAddr & 0xFFFFF`
selects the physical offset. This removes the false white band previously
rendered from VRAM scratch row zero. Development-only trace/disassembly tools
are also `EXCLUDE_FROM_ALL`, avoiding unnecessary LTO relinks during a normal
build.

## 2026-07-20 — Q6.6 RESOLVED: Mac OS 8.1 boots the Quadra 605 (68LC040) to the Finder desktop — two blockers, the FPU trap and a DMA-final-chunk STATUS race

The Quadra 605 now boots Mac OS 8.1 all the way to the Finder: `q605_trace`
renders the full desktop (🍎/File/Edit/View/Special/Help menu bar, the
`Mac-8.1-US` boot volume icon, Browse-the-Internet/Mail/Trash, gray-dither
background) at clk ~1.8 G, then idles (SCSI commands plateau at 5973 while the
Cuda one-second ticks keep running). All 26 CTest gates stay green.

**1. `$40802A38` "MBState" dialog spin = SysError(10) dsLineFErr — an FPU trap.**
The boot parked in a ROM modal-alert loop (`tst.b $172.w; bne`) waiting for the
mouse-button low-mem to clear. The real cause was upstream: the ROM
unconditionally runs FPU init (`fmove.l fpcr,D0` @ `$408E9AC0`); our
68LC040-without-FPU trapped that F-line to the System's vector-11 "no FP
package" stub (`$0002747E`), which raised `SysError(10)` = dsLineFErr and drew
the alert. MAME's golden oracle `macqd605` is a **full 68040 with an FPU**
(`macquadra605.cpp:158 M68040(...)`; only its lc475/lc575 variants use
M68LC040). Fix (`src/Cpu040.cpp`): `setModel(M68040)` + `setFPUModel(M68882)`;
`POM68K_Q605_NOFPU` restores the bare 68LC040/no-FPU config (accurate to real
Quadra-605 hardware, but this System ships no SANE FP emulator so it cannot
reach the Finder that way). This advanced the boot 3705 → 5363 SCSI commands.

**2. SCSI SIM stall at 5363 commands = a whole-block DMA read whose final chunk
never flipped the bus to STATUS.** After the FPU fix the boot wedged in the OS
8.1 SCSI Manager's SIM sequencer (`$00122A78`, state `($88,A4)=$d`), spinning on
the data-transfer-complete flag `($5e,A3)` bit 4, which never set.

- **Symptom chain (all traced):** the failing transaction is a multi-block
  READ(10) at LBA `$49E21`, 22 blocks = 11264 B. The Manager drains it in a
  per-512-byte-block pattern of **three PIO bytes (CI_XFER `$10`, one reg2 read
  each) + one TC=509 DMA chunk (CI_XFER `$90`)**. Because 11264 is an exact
  multiple of 512, the *last* Transfer Info of the whole transaction is always
  that DMA chunk. On this PIO-mode device (non-DMA select `$42`, no pseudo-DMA
  window) even the `$90` chunk is drained through the **register FIFO port**
  (reg2 / `read(R_FIFO)`), never `dmaRead()`.
- **Root cause:** the SIM's per-CI_XFER completion service (`$0011E686`) reads
  reg4's phase bits right after each Transfer Info and sets `($5e,A3)` bit 4
  only if it sees STATUS. Our model advanced DATA_IN→STATUS lazily — on the last
  byte *read* — and the Q6.6-fix-#3 pre-advance was gated `!dmaCommand_`, so for
  the final `$90` chunk the phase was still DATA_IN when the service ran; the
  R_FIFO drain flipped it to STATUS only afterwards. Bit 4 never set → the
  sequencer spun forever. (`dmaRead()`-drained transactions were unaffected —
  `dmaRead()` drives its own STATUS transition inline, before the service.)
- **Fix (`src/Ncr53c96.cpp`):** pre-advance to STATUS at the last Transfer Info
  **regardless of the DMA flag** (bytes are considered moved off the bus at the
  Transfer Info, as on real hardware), so the completion service sees STATUS.
  To keep that from breaking the ROM's pseudo-DMA read loop (`$408D1FAC
  btst #4,($70,A3)`, "≥16 bytes ready") which drains *in* DATA_IN, `dmaRead()`,
  `updateDrq()` and `R_FLAGS` now key on `dataInPos_` (bytes still buffered)
  rather than `phase_==DATA_IN`, so a window drain survives the pre-advance.
  Advanced the boot 5363 → 5973 SCSI commands → the Finder. All 26 gates green,
  deterministic; the ncr53c96 / scsi_pdma unit tests still pass.

`tests/q605_trace.cpp` now reads the true screen geometry from the main GDevice
PixMap (baseAddr/rowBytes/bounds) and emits a correct 640×480 1bpp screenshot
(`q605_boot_1bpp.pbm`).

## 2026-07-19 — Q6.5d RESOLVED: dsBadPatch(99) was a 53C96 FIFO-count lie that sent the OS SCSI Manager's resource read into its DISCARD engine

The Quadra 605 boot reached the ROM-patch stage then drew a `dsBadPatch`
(System error 99) alert — the last step before the Finder. The whole cause was
a single wrong register value in our 53C96 model.

- **Symptom chain (all traced):** `_GetResource('scod',-16470)` → NULL →
  `_SysError(99)`. GetResource returned NULL with ResErr = memFullErr(-108)
  though the system heap had 23.7 MB free, because a `_ReallocHandle` was asked
  for a garbage size — the resource's 4-byte length prefix read back as garbage.
  The File Manager kept re-reading the SAME disk block ($51F08, READ(10) 1 block)
  16× and never advanced. Each SCSI read actually SUCCEEDED (512/512 bytes,
  correct data, status $00, clean CI_COMPLETE/MSG/DISCONNECT) — yet the data
  never reached the resource-manager's buffer.
- **Root cause:** the OS 8.1 SCSI Manager, right after the DMA-SELECT completes,
  reads reg7 (FIFO byte count) at `$0011ADD4` (`moveq #$1f,D1; and.b reg7,D1;
  cmpi.b #$1,D1`) to sanity-check the FIFO. On the real 53CF96 the FIFO is EMPTY
  there (the CDB already streamed out), so the count is 0 (or 1). Our `R_FLAGS`
  returned `min(dataIn_.size(),16)=16` because we short-circuit the whole payload
  through `dataIn_` instead of the physical FIFO. Seeing "16 stray bytes" the
  Manager flagged the transfer abnormal (`($a,A4)=5`) and routed the read to its
  DISCARD engine (`$0011AEEA`: read the FIFO byte into D0, then `$0011AEB4`
  immediately overwrites D0 with the state — thrown away), never the STORE engine
  (`$0011B34C`/`$0011B358 move.b ($20,A3),(A2)+`). Buffer empty → garbage length
  → memFullErr → dsBadPatch.
- **Fix (src/Ncr53c96.h + .cpp):** a `dataXfer_` flag — cleared when a DATA-IN
  phase is entered (`runTarget`), set when a data-phase Transfer-Info actually
  fetches bytes (`transferInfo`). `R_FLAGS` in DATA_IN now reports 0 until
  `dataXfer_`, then `min(remaining,16)` as before (so the ROM driver's DMA
  16-byte-ready gate is unchanged). Now the Manager sees FIFO=0 after the select,
  takes the normal path, and DMAs the read (`lastCmd=$90`, dmaBytes climbs).
  dsBadPatch is gone, the boot runs far past it, 26/26 CTest gates stay green
  (the working DMA path and the LC II 5380 are untouched).
- **Method note:** the win was the `Q605_PCLOG="<from> <to>"` window PC-tracer
  added to q605_trace (plus Q605_NOPRAM for a deterministic cold boot — the tool
  auto-load/saved q605_trace.pram which made runs diverge). It showed the data
  loop never reaches the STORE engine. Eliminated on the way: it is NOT an
  ISR/async-completion problem (the polled wait masks IRQ and reads istatus
  inline), NOT a bad-status rejection (all interrupt causes were "good"), and
  NOT a fixed-period watchdog (retries were back-to-back). Remaining: the boot
  now spins in the SCSI Manager completion loop (`$00123BA8`/`$0011CD2C`, IPL +
  device-record `$0C0C` check) — the new frontier Q6.6, Finder not yet reached.

## 2026-07-19 — Q6.5b/c: the async SCSI SIM crash + the SCC/reselection spin are BOTH fixed — the boot loads System, applies patches, stops at dsBadPatch

Two MAME-source-driven 53C96 fixes (ncr53c90.cpp FSM, read via scrapling +
local clone) took the boot from the "illegal instruction" crash all the way to
the System's ROM-patch-application stage:

- **DMA SELECT raises DRQ, not an interrupt (Q6.5b).** MAME's CD_SELECT with a
  DMA command does `dma_set(DMA_OUT)`; in DISC_SEL_ARBITRATION with an empty
  FIFO it runs `seq=1; check_drq(); break;` — DRQ goes HIGH and NO interrupt
  fires. The Mac OS 8 async SCSI SIM polls that DRQ (pseudo-VIA2 IFR bit0, via
  `$0011E5A6`) to detect an async transaction and install its completion
  continuation (trampoline `$0011EC80`, `move.l a1,$f0(a0)`). Our selectTarget()
  raised I_BUS|I_FUNCTION immediately on the empty-CDB select and never raised
  DRQ → the SIM took the SYNC path, skipped the trampoline, and its later "$02
  service" message jumped through the never-installed NULL continuation → jump
  to `$2` → vector table executed → vec-4 crash alert. Oracle proof: the module
  is loaded at +$10B0 on MAME; at the isr the continuation reads `$0011FA54`
  (VALID) because the trampoline ran first; ours was NULL. Fix: incomplete-CDB
  select → phase=COMMAND, seq=1, selCdbWait_, DRQ high, no IRQ; the completion
  IRQ (I_BUS|I_FUNCTION) fires when the streamed CDB completes.
- **CD_ENABLE_SEL raises no interrupt (Q6.5c).** MAME just
  `command_pop_and_chain()` (ncr53c90.cpp:841); only CD_DISABLE_SEL does
  function_complete(). Our spurious I_FUNCTION mis-sequenced the SIM's async
  completion wait (the `$001B7860` loop polling I/O status `$171D02==$8001`,
  which also polls the SCC `$1D8`/`$1DC`).

Result: crash GONE (0 vec-4), boot advances **1583 → 2882 SCSI commands**,
launches and runs the loaded System, and reaches the **ROM-patch application
stage** — where it stops at a System caution+Restart alert. Read the screen
(new raw-VRAM dump) + DSErrCode `$AF0` = **99 = dsBadPatch, "Can't load patch
resource"**, raised by a System patch stub at `$0004C966` that validates ROM
return addresses (`$4080F244`/`$4080F300`) and errors on mismatch. This is the
FINAL startup stage before the Finder launches. 26/26 CTest green throughout;
ncr53c96_test unchanged. New frontier (Q6.5d): why the patch validation fails —
trace the patch-resource load / the `$4080F244` ROM return-address flow vs MAME.

## 2026-07-19 — Q6.5: the boot restart loop is ACTUALLY resolved — the ROM's POST XPRAM validity read uses a THIRD Cuda framing (direct-driver GetPram)

The previous entry's `_ReadXPRam $76` echo fix let the System *load* but did
NOT stop the restart loop: instrumenting the ROvr boot flag (`q605_trace`
`--pcring` + a new `Egret::onXPramWrite` hook) showed the machine still jumped
to the ROM reset re-entry `$4080000A` ~every 121 M cycles (66 restarts over
8 G), re-reading a bit more of the System each time but never launching it —
exactly the cadence the Q6.4 notes described. The MAME macqd605 oracle
confirmed real hardware **never** restarts (`$4080008C` never hit, blank NVRAM).

Root cause, localized end-to-end: the ROvr flag XPRAM `$8A` (set by Mac OS 8's
`_WriteXPRam $8A|=$05` then `_ShutDown ShutDwnStart`) was being **cleared to
`$00` by the ROM's full-XPRAM re-init every boot**, so ROvr re-applied and
restarted forever. The re-init is gated in `$4080B280` by two signature checks
on a stack buffer filled by a Cuda XPRAM read at `$4080B286`: `cmpi.b
#$A8,($10,A7)` (SysParam) and `cmp.l #'NuMc',($C,A7)`. That buffer came back
**all zero** (D3=`$0` at the `$4080B2C8` compare) even though `pram[$0C..$0F]`
holds `'NuMc'` and `pram[$10]=$A8`.

The read that fills the buffer goes through the ROM's **direct Cuda driver**
(`$408B34xx` byte-lane receiver — NOT the device-manager ISR of the previous
fix), and it reads XPRAM one byte at a time via **GetPram** (`$01 $07 $00
addr`), framing each reply as `[sync, status, DATA]` — it takes the data as
the byte right after ONE status byte. Our `kGetPram` reply carried the full
`[sync, status0, status1, cmdEcho]` 4-byte header (correct for the
device-manager ISR), so the direct driver read `status1` (`$00`) as the data
and saw an all-zero XPRAM (verified on the Cuda wire: GetPram `$0C` → reply
`$01 $00 $00`, 3rd byte `$00` instead of `$4E`='N').

**Fix** (`src/Egret.cpp`, `kGetPram`, Quadra-only `cudaPolarity_`): drop
`status1`+`cmdEcho` from the GetPram reply so the data lands as the 3rd byte
(`[sync, status, data…]`). After the fix the `$4080B2C8` compare reads
D3=`$4E754D63`='NuMc' → the ROM skips the re-init → `$8A` survives → **zero
restarts** over a 6 G-cycle boot (was 66), the full System loads (1583 SCSI
commands) and runs. LC II is untouched (`cudaPolarity_=false` keeps its 4-byte
header, `lcii_boot_etalon` + `egret_test` green; all 26 CTest gates pass).

New frontier (Q6.5b): the boot now **launches and runs the loaded System**
(RAM code at `$0002xxxx`/`$0006xxxx` driving ROM Toolbox), but wedges in a
**modal mouse-click tracking loop** at `$40802A38` (`tst.b $172.w; bne` —
`$0172` = **MBState**, the mouse-button low-mem global, `$80`=up). The routine
`$408028C0`/`$408029EC` draws two boxed shapes (an alert icon + dialog frame,
per the VRAM), waits for the button DOWN (`$172`==0), tracks the click, then
waits for button UP — a classic `_ModalDialog`/menu-track loop. Headless there
is no click, so it spins. MAME macqd605 **never executes `$408028C0`** — so our
running System uniquely puts up this dialog. **Read the dialog text** (new
`Q605_STRTAP` Dialog-Manager string tap): it is the System Error crash alert
**"illegal instruction … Restart"** — so the running System takes a **vector-4
illegal-instruction exception** during startup (a `willExecute` vec-4 log at
`pc=$00000064`, clk 560 M). Traced it: a **wild `jmp (A1)`** at `$0011E996`
(`movea.l ($f0,A0),A1; jmp (A1)`, A0=`$0000F180`) reads a handler pointer from
`$0000F270` that holds `$00000002`, jumps to `$2`, and executes the low-memory
vector table as code up to `$64` → illegal. A RAM write-watch shows `$F270` is
written by the **SCSI driver** (`pc=$408D231A`, `A1=$50F50100` = the 53C96
pseudo-DMA data port, `A3=$50F10000`) — i.e. `$F270` is inside a SCSI read
buffer, and its bytes later get used as an interrupt/driver handler pointer.
So the crash is a **handler pointer clobbered by SCSI-buffer data** — most
likely a 53C96 pseudo-DMA overrun / wrong-address / structure-vs-buffer
aliasing (ties back to the Q6.1/Q6.3 SCSI path).

**ROOT CAUSE pinned (2026-07-19): a deferred-task context clobbered by SCSI
buffer reuse (use-after-free), NOT a live interrupt.** MAME never reaches
`$0011E996`. It is reached on our machine via the **event/deferred-task
processor** `$0011A6D2` (handler=table[D0], context A0=*(A4+4)=`$F180`), NOT
from a hardware IRQ: measured at ISR entry `via2IFR=$00`, `via2IER=$12` (SCSI
bit 3 **not** even enabled), `scsi.irq()=0`. So an earlier SCSI event was
**queued** as a deferred task referencing context `$F180`; before the task ran,
`$F180`'s region (`$F270 = $F180+$F0`, the task's handler field) was **reused as
a SCSI read buffer** (write-watch: `pc=$408D231A`, 53C96 pseudo-DMA port
`$50F50100`) → the handler field holds stale disk bytes (`$0/$2`) → the deferred
task jumps to `$2` → runs the low-mem vector table → illegal instruction.
This is a **heap/allocation + deferred-task ordering divergence** (kin to the
LC II SimCity timing race): on real hardware the SCSI buffer does not overlap
the live task context, or the task runs before the buffer is reused. Next: find
why our SCSI driver's read buffer lands on the deferred-task context `$F180`
(an upstream heap-allocation divergence) or why the task is deferred past the
buffer reuse — diff the allocation/queue ordering against MAME. A speculative
53C96 tweak is the WRONG move (the crash is not an IRQ); this is above the chip. Tooling this pass: `Egret::onXPramWrite`;
`q605_trace` `$8A` write tracer, `Q605_CKPT` checkpoints, `Q605_CUDA_FROM`
Cuda byte log, `Q605_STRTAP` dialog-text tap, vec-4/11 exception logging,
`q605_boot.ppm` screen dump.

## 2026-07-19 — Q6.4 + Q6.2 BOTH RESOLVED: the boot restart loop AND the block-0 loop were one coupled Cuda-reply-framing bug; the System now loads

Unified fix for two loops that a single blunt setting could not satisfy. The
Quadra's `_ReadXPRam` replies are consumed by TWO different ROM readers with
DIFFERENT header conventions, so the reply cannot be statically echo/no-echo:

- The **device-manager receive ISR** at `$408A9BBE` (SysParam validity,
  boot-flag `$8A`, the ADB-autopoll block) consumes a fixed 4-byte header
  `[sync, status0, status1, cmdEcho]` — it NEEDS the echo. Q6.2 had dropped
  it, so the SysParam validity read at `$4080C5CC` (`cmpi.b #$A8,($1F8)`)
  landed one byte short (`$1F8`=`$00` not `$A8`) → the ROM re-initialised the
  whole XPRAM every boot → wiped the 32-bit boot flag `$8A` that Mac OS 8's
  `'ROvr'` patch sets (`_WriteXPRam $8A|=$05; _ShutDown ShutDwnStart`) →
  restart loop (**Q6.4**).
- The **`_GetOSDefault` reader** at XPRAM `$76` is the ONE read serviced by a
  simpler reader that skips only 2 header bytes after sync — it must NOT get
  the echo, or it captures the echo as data (`$0200` not `$0001`), the Start
  Manager's DDM ddType scan at `$40807264` matches no descriptor
  (`$0001`/`$006A`), and it re-reads block 0 forever (**Q6.2**).

**Fix** (`src/Egret.cpp`, `kReadXPram`, Quadra-only `cudaPolarity_`): keep the
full 4-byte header (echo) for every ReadXPram EXCEPT `$76`, where the echo is
popped — `$76` is the single address whose reader skips it. This is the
observed framing of the two readers; both loops clear at once. LC II is
untouched (default `cudaPolarity_=false`, never pops). Proven with the new
`$408A9BE2` HDRDONE tap (device-manager reads show `hdrbuf 01 00 00 02`, data
correct only with the echo) and the `01 02 01 76` reply
(`01 00 00 02 00 01` → `_GetOSDefault`=`$0001` only when the echo is popped).

**Result.** No restart loop, no block-0 loop. The System now genuinely
**loads and runs**: varied `READ(10)`/`WRITE(10)` across the disk (2 200+ SCSI
commands and climbing, 3.4 MB+), 24-bit `'ROvr'` code executing at
`$A0000000`, and the boot advances through the ROM startup-chime / ASC phase
(`$408070F8` sample-copy + calibrated delay loops) into later init
(`pc=$40847BA2` at 2.5 G cycles) — dramatically past both loops. **26/26
ctest gates green** (incl. `egret_test`, `lcii_boot_etalon` "booted to the
Finder", `scsi_pdma_test`).

**Next (Q6.5):** carry the launch through to the Finder desktop. The boot is
slow (calibrated delay loops run in real cycles; ASC sound is still a stub)
but progressing, not looping — SCSI/PC counters keep advancing. Watch the ASC
chime path and diff the sustained boot against the MAME macqd605 golden run.
The proper long-term cure for the `$76` special-case is to make the
`_GetOSDefault` reader consume the echo like the ISR does (a byte-delivery
detail), rather than address-gating the reply.

Tooling: `tests/q605_trace.cpp` gained `--dumpat ADDR` — with `--stop-at` it
dumps 128 bytes + a 48-instruction disassembly at the stop; standalone it
does a static ROM/RAM disassembly right after reset (no breakpoint needed),
which is how the `$4080C5CC` validity routine, the `$A001F010` ROvr patch and
the `$40807224` DDM scan were read — plus a `$408A9BE2` HDRDONE tap (Cuda
receive header count + buffer + dest per read). `src/Egret.cpp`'s
`EGRET_CMD_LOG` env logger was the microscope (it showed `_WriteXPRam $8A=$05`
never surviving the next boot's `_SetPram $8A=$00`, and the `$76`
read/reader mismatch).

## 2026-07-19 — [superseded within the day] Q6.4 root-caused, un-masking Q6.2

Intermediate step, kept as a pointer: found that the Q6.2 echo-pop broke the
device-manager XPRAM SysParam validity read (→ Q6.4 restart loop), fixed it by
keeping the echo, and discovered that doing so un-masked the Q6.2 block-0 loop
(_GetOSDefault=$0200). Both were resolved together the same day by the unified
fix above (keep the echo for all reads except the $76 OSDefault read). See the
top entry for the full story.

## 2026-07-19 — Q6.4 re-localized: it is a System-launch HANDOFF failure, NOT a Cuda reply-framing bug (the prior "completion ISR buffer-smash" lead is disproven; no fix landed yet)

> **Superseded, same day**, by [2026-07-19 — Q6.4 + Q6.2 BOTH RESOLVED…](#2026-07-19--q64--q62-both-resolved-the-boot-restart-loop-and-the-block-0-loop-were-one-coupled-cuda-reply-framing-bug-the-system-now-loads):
> it *was* a Cuda reply-framing bug after all — the "NOT a Cuda reply-framing
> bug" ruling below is the wrong turn, kept because the elimination work in it
> is what localised the real one.

Followed the Q6.4 "diff the Cuda completion reply framing against MAME"
lead to its conclusion and DISPROVED it as the fix, then re-localized the
blocker one level up. Established (MAME macqd605 oracle via the working
`save`-marker method, `roms/mame/oracleQ_*`; our side via new `q605_trace`
taps):

- **`$4080ED7E` is the ROM's Shutdown Manager selector dispatcher**
  (selector 2 = `ShutDwnStart` = restart → `$4080EE06` → `jsr $4084C148` →
  `$4080EE12` → `jmp $4080000A`). **MAME reaches `$4080ED7E` ONLY with
  selector 0 (init)** across its full 20 s boot to the Finder and NEVER
  hits `$4080EE06`/`$4080000A`/the `$4080EE94` task-walk. **Our machine
  hits it with selector 2** (identical regs every restart:
  `D1=$B0D70002 D2=$B0D7EF88 A0=$003FF974 A1=$40800000`, clk 434067092).
  So the entire restart/task-walk machinery is code MAME never runs.
- **The completion-ISR buffer-smash lead is disproven:** at `$408A9CFC`
  the received count `w@(a2+$10)` is **always exactly 4** on our machine
  (no `dbra` wrap, no 64KB copy). Two experiments that should have fixed a
  framing bug do NOT stop the restart: (a) swapping the Cuda pseudo-reply
  header to `00 01 00 <echo>` (matching an SR-wire measurement) only shifts
  the restart 434M→443M; (b) suppressing the periodic Cuda TIMER packets
  leaves it at ~437M. The Q6.2 ReadXPram-echo class is not the Q6.4 cause.
- **Root shape: the System loads off SCSI (1281 cmds) but NEVER LAUNCHES.**
  PC coverage over one boot attempt: `$40840000` (ROM POST/SANE) = 38.9M
  hits; the loaded System code in RAM (`$008xxxxx`) = only ~700 hits total.
  On MAME the ROM's Cuda completion ISR `$408A9CFC` stops firing after
  ~20M cycles (control handed to the System's own ADB driver); on ours it
  keeps firing to 434M — we stay in the ROM's boot code and restart
  ~every 118M cycles (~4.7 s @ 25 MHz), each attempt loading slightly more
  of the System (SCSI 995→1281). Q6.4 is therefore a **boot-block /
  System-launch handoff failure**, and the next step is to diff MAME's
  ~15-20M-cycle handoff window (where it reads and `jmp`s the boot blocks)
  against ours to find why our machine never launches the loaded System.
- Non-fix findings kept for the record: the shared Egret `kCpuHz` is the
  LC II 15.67 MHz (Q605 is 25 MHz) so the Cuda RTC/TIMER cadence runs ~1.6×
  fast — harmless to the restart, make it per-machine when polishing; and
  MAME's `00 01 00` pseudo-reply header was an SR-wire measurement, NOT the
  `$408A9CFC` framing (that ISR frames `00 00 00 <byte>`) — don't diff the
  HLE against those bytes for XPRAM.

Tooling: `tests/q605_trace.cpp` gained `--complog HEXPC` (dump the Cuda
completion-ISR reply count + bytes + DCE/ioCompletion at a PC) plus
always-on low-volume taps at `$4080ED7E` (SELDISP: selector + regs, and a
raw stack dump when selector==2) and `$4080EEB2` (TASK: the walked task
element + handler). `src/Egret.cpp` gained an env-gated (`EGRET_CMD_LOG`)
command/reply logger — off by default, no behaviour change. `egret_test`
and `lcii_boot_etalon` stay green (shared Egret path untouched).

## 2026-07-19 — Q6.4 deeply localized: the console divert is a periodic boot-RESTART loop, not a fault — several candidates ruled out (no fix yet)

The Q6.4 blocker (System loads off SCSI, then the boot diverts to the ROM
serial-console loop at $408B9928) was traced end-to-end and materially
narrowed, though the single fix is not yet landed. Established (all
MAME-oracle-confirmed via the `save`-marker method — see TODO § Q6.4 for
the full write-up and the `agentQ_*` evidence under roms/mame/):

- The boot **restarts ~every 118M cycles** (jumps to the ROM reset re-entry
  $4080000A) via the Device/Slot Manager's VBL/slot-task walk ($4080EE94)
  → $4080EE12 → $4080EE1E. MAME never restarts and boots to the Finder in
  ~20M of its own cycles. Each of our restarts makes FORWARD SCSI progress
  (995→1281 commands), so it is the ROM re-attempting the boot, not a hard
  reboot. Terminally (clk ~953M) a late POST re-test finds D7 bit 26 clear
  at $4084AA58 (its OK-setter $408473B6 is never reached) and drops to the
  serial console.
- **Ruled out (do not re-investigate):** (1) interrupt delivery — IPL-1
  (VBL) and IPL-2 (slot) fire thousands of times (the earlier "zero
  interrupts" was a measurement artifact: the vec histogram counted only
  `willExecute`, missing `willInterrupt`); (2) 24-bit MMU addressing — the
  System legitimately runs `'ROvr'` ROM-patch code through a flagged 24-bit
  master pointer ($A00031F0, $A0 = MemMgr lock/resource flags) while
  MMU32Bit ($CB2)=0, and our 040 24-bit page table correctly aliases
  $A00031F0→phys $000061F0 (no bus error); (3) no unhandled fault of our
  own (only benign $FnFFFFFF slot-probe vec-2s + A-line traps).

Tooling added to `tests/q605_trace.cpp` (only file changed; no source/Moira
edits, ctest 26/26 green incl. egret_test + lcii_boot_etalon): `--firstpc
HEXPC` logs the first control-flow edge INTO a PC from a non-adjacent
caller (regs+clk), and the trace now hooks `willInterrupt` for a real
IRQ-by-level histogram. Next: instruction-diff the $4080EE94 task-walk /
$408B7716 restart-check against MAME to find why our path takes the restart
selector where MAME returns normally.

## 2026-07-19 — Q6.3 RESOLVED: SCSI multi-block read — the polled ($10) Transfer Info needed the DATA IN bus-service interrupt

With Q6.2 letting the boot load the driver/partition-map/System, it then
spun forever at `$40899704` (`btst #7,($40,A3)`, R_STATUS bit 7 =
S_INTERRUPT) after a large read. The Mac OS 8.1 SCSI driver's read
($408D2280) uses the pseudo-DMA window two ways: a DMA-variant burst
(`CI_XFER|DMA` = $90, drained through $50F50100 then wait S_INTERRUPT)
and a **polled byte tail** (`CI_XFER` = $10, TC=1, wait S_INTERRUPT, then
`move.b ($20,A3),(A2)+` — one FIFO byte at $408D2388). Our
`Ncr53c96::transferInfo` DATA_IN branch only raised the bus-service
interrupt for the DMA variant, so the $10 tail never completed. Fix:
raise `S_TC0 | I_BUS` for any DATA_IN Transfer Info with data pending
(DMA or polled), and make `dmaRead` raise I_BUS per-chunk at TC=0 (not
only at full-payload drain) for multi-block DMA reads. `scsi_pdma_test`
and `ncr53c96_test` stay green. The boot now reads 1 281 SCSI commands
(43 775 writes), loads the System, and advances to a new blocker (Q6.4:
it diverts to the POST serial console during System startup —
$408B9928/$408BA0EA — instead of continuing to the Finder).

## 2026-07-19 — Q6.2 RESOLVED: the block-0 re-read loop was a Cuda ReadXPram reply-framing divergence — the boot now loads the driver, partition map and System (progresses to a new SCSI blocker)

The Start Manager's boot-driver scan (`$40807224`) matches a DDM driver
descriptor by a **wanted ddType** = low byte of the `_GetOSDefault`
result. MAME macqd605 (golden, boots our exact disk to the Finder) gets
$0001 → wanted $01 → matches our disk's DDM entry0 → driver at block $40
→ partition map → System. Ours got $0200 → wanted $00 → matches nothing
→ the scan re-ran the whole ID 6→0 poll → block 0 re-read ~48 700×.

`_GetOSDefault` reads XPRAM $76:$77 via a Cuda ReadXPram (`01 02 01 76`);
PRAM $76:$77 already held the correct $0001. The divergence was the
**reply framing**: the Quadra Cuda's ReadXPram reply is a **3-byte
header `[sync, status0, status1]` followed by data directly — with NO
command-echo byte**, but our Egret HLE (matching the LC II Egret driver,
which does echo) appended a 4th cmdEcho header byte. The ROM's
device-manager receive ISR (`$408A9BC0-$408A9C30`) reads its fixed
header count then copies the data, so the cmdEcho `$02` landed as
data[0] and _GetOSDefault returned `$0200`. Fix: drop the cmdEcho from
the Cuda ReadXPram reply (`Egret::process`, gated on `cudaPolarity_`) →
D3=$0001FFFF at the scan, byte-identical to MAME.

Also fixed a latent **ghost Cuda session**: between transactions the ROM
toggles TIP/BYTEACK to poll the idle bus without writing the SR; the HLE
grabbed the stale SR (our last `$AA` sync ack) as byte 0, built a bogus
`AA AA AA AA AA` command and answered with the unknown-type error report
`{01,02,00,AA}` (its `$02` status a second contaminator). Fix: a Cuda
command session only begins when the host actually loaded a command byte
— new `Via6522::srHostWritten()` (set on a host SR write, cleared on
device `loadSR`), gated into the SYS_SESSION-rise HOST_CMD entry in
`Egret::portBChanged`.

Both fixes are gated on `cudaPolarity_`; the LC II Egret path is
untouched — `egret_test` and `lcii_boot_etalon` stay green, full ctest
26/26. The two earlier reverted probes (blanket 3-byte Cuda header /
byte-swapped header) failed because they targeted the pseudo-reply
*header status bytes*; the real error was one extra *header byte* on the
ReadXPram reply specifically. Next blocker (Q6.3): the boot now reads
977 progressive blocks then spins at `$40899704` polling 53C96 Status
bit 7 after a pseudo-DMA READ10 — a controller completion-flag gap, far
past block 0. Tooling note (MAME imgui debugger under Xvfb): `tracelog`
in a bp/wp action WORKS; `save`-expressions and bp/wp *conditions* are
unreliable — drive everything through `trace <file>,maincpu,,{ tracelog
"" }` + taps.

## 2026-07-18 — Q6.1: 53C96 pseudo-DMA reads work — the Mac OS 8.1 SCSI driver now transfers full 512-byte blocks off the disk

With the Q5 Slot-Manager fix letting the boot reach SCSI target
selection, three gaps in the `Ncr53c96` model blocked the driver's
polled-DMA read loop ($408D1F40). Each was pinned by tracing the driver
against the MAME `ncr53c90` FSM (references in comments):
- **CDB streaming.** The driver SELECTs (no ATN, cmd $C1) with a flushed
  FIFO, THEN pushes the CDB into the FIFO and polls for the phase to
  leave COMMAND ($408D1A84). Our select consumed nothing and parked in
  COMMAND forever. Fix: `fifoPush` while in COMMAND phase accumulates the
  CDB and runs the target once complete (models the real
  DISC_SEL_WAIT_REQ/SEND_BYTE streaming, ncr53c90.cpp:544-570), advancing
  the phase out of COMMAND.
- **Chunked DATA IN.** The driver reads 16 bytes per iteration: set
  TC=16, Transfer Info DMA ($90), wait S_TC0 (chunk landed) + VIA2 DRQ +
  FIFO-full (reg7 bit4), burst 4 longwords from the $50F50100 window,
  then wait on Status bit7 (bus-service IRQ) and read R_ISTAT. Fix:
  `transferInfo` sets S_TC0 and raises I_BUS per DMA chunk; `R_FLAGS`
  reports the DATA IN remaining count (not the physical FIFO); DRQ is
  decoupled from S_TC0 (it tracks CPU-side availability).
- **VIA2 DRQ line.** The driver polls the 53C96 DRQ through pseudo-VIA2
  IFR bit0 ($50F03A00 = via2 reg 13, pseudovia.cpp:162 scsi_drq_w), a
  line we never reflected. Fix: via2 IFR reads OR in the live
  `scsi_.drq()`.

Verified end to end: block 0's Driver Descriptor Map ('ER' 02 00 00 09
60 60 …) lands byte-perfect at the driver buffer $003FF980 via the
pseudo-DMA window; the transaction completes with GOOD status ($00), a
COMMAND COMPLETE message, and a clean CI_COMPLETE/MSG_ACCEPT. `ctest`
26/26. `q605_trace` gained SCSI chunk-loop log filters and an A2-buffer
dump (commit 04f1b40).

Known next blocker (Q6.2): the boot re-reads block 0 in a loop and never
advances to the partition map or the SCSI driver — a boot-logic issue
above the SCSI A-trap, not a controller fault (see TODO § Q6.2).

## 2026-07-18 — Q5.1d: Q5 Slot-Manager blocker RESOLVED — the missing MEMCjr DAFB bus-holding split; boot now drives the SCSI bus

Root-caused via a newly-unblocked full-machine MAME oracle, then fixed
in `Q605Memory`. The MEMCjr accesses the DAFB register file at
$F9800000-$F98001FF as a **12-bit port split into two 6-bit halves**
(`djmemc.cpp:149-198`): a read returns `reg & 0x3f` and latches
`(reg>>6)&0x3f`; a write ORs that latch back in; the high-6 half is
carried through `$50F0E07C` (mirror $5000E07C). Our HLE returned/wrote
the full register in one $F9800000 access, so the ROM reconstructed
wrong DAFB config/timing values, which steered the video sResource
directory walk to the WRONG per-DrHW sRsrcType list ($408F1CC8 instead
of $408F1C90) → `_sReadStruct` selector 5 read the raw `00030001`
sRsrcType header as a block size → spSize=$2FFFD → the byte-lane copy
overran to end-of-ROM $40900000 → the boot fell to the POST console.
- **Fix** (`Q605Memory.cpp`): `dafbRegRead` now wraps `dafbRegReadRaw`
  and applies the low-6/latch-high-6 split for offsets < $200;
  `dafbRegWrite` ORs `dafbHolding_` into writes < $200 then clears it;
  `$50F0E07C` (`memcjr_r/w` $7C) reads `dafbHolding_>>6` and writes
  `(v&0x3f)<<6`. BOTH the read and write halves were required — the
  write-side alone was inert.
- **Result**: no more $2FFFD overrun, no POST-console park at 4 MB or
  32 MB, and the machine now issues real SCSI CDBs (68-103 commands,
  273-410 selections with `--disk MacOS-8.1-boot.vhd`). VEC2 count 44→13
  (only benign empty-slot probes remain). ctest 26/26.
- **New frontier (Q6/Q7)**: the SCSI pseudo-DMA data path — the driver
  spins at $408D1A84 polling the 53C96 phase register with dmaBytes=0;
  READ CAPACITY/READ blocks don't yet deliver a payload.

### Oracle harness recipe (record it — several dead ends)

`-debugger none` SILENTLY DISCARDS debugger console/printf/trace output.
`-debugger gdbstub` refuses m68040. The WORKING recipe is the imgui
debugger under Xvfb+bgfx: `xvfb-run -a flatpak run org.mamedev.MAME
-rompath "$PWD" macqd605 -ramsize <bytes> -video bgfx -sound none
-nothrottle -seconds_to_run N -debug -debugger imgui -debugscript <f>`.
Gotchas: (1) breakpoints only arm after `focus maincpu` in the script;
(2) capture state via `save <absfile>,<expr>,<len>` inside a breakpoint
ACTION (`bpset a,cond,{ save f.bin,a0,64 ; bpclear ; g }`) — printf /
tracelog / trace-action all produce nothing; (3) the flatpak HOME is
`~/.var/app/org.mamedev.MAME`, so save/trace files need an absolute path
under `/home/...` (or a relative path, which lands in the rompath dir).
Expr syntax: `l@(a0+4)`, `w@(a1+4)`, `b@(a0+0x32)`, `sp`, `pc`, C ops.



The Cuda-ROM blocker on `macqd605` was cleared (romset under
`roms/mame/`, zeroed NVRAM placeholder → MAME warns but runs and
factory-inits XPRAM like our Egret HLE). Built a working headless MAME
debug harness and co-simulated our exact ROM against it. Results:
- **It is genuinely OUR bug, not a RAM-size artefact.** MAME boots PAST
  the Slot Manager (never reaches the POST console $408B9928) at BOTH
  4 MB and 32 MB, with the SAME MemTop as us. Our machine reaches the
  console at both sizes. The 32 MB fatal `_sReadStruct` overrun
  (SR=$2000 → $40900000) DISAPPEARS at 4 MB but a *different* POST test
  still parks us in the console — same upstream cause.
- **Root cause pinned to video sResource-LIST selection.** The ROM holds
  a sequence of per-DrHW sRsrcType lists; MAME's Slot walk selects list
  **$408F1C90**, ours selects the NEXT one **$408F1CC8** (off by one
  $38-byte list block). MAME's video sReadStruct(spID=1) then runs on the
  board directory (spsPointer=$408FFFDC, spOffset=−$2C, spSize=$10 — a
  correct small copy); ours lands sel5's spsPointer on the raw
  `00030001` sRsrcType header and reads it as a size → spSize=$2FFFD →
  the byte-lane copy overruns to end-of-ROM. MAME NEVER calls sel5 with
  spsPointer=$408F1CC8, and NEVER executes the RAM sExec at $000094E0.
- **Prime suspect: the MEMCjr DAFB *holding-register* access model our
  HLE omits.** For MEMCjr the DAFB regs are read through a 6-bit split
  (`djmemc.cpp:143-198`): `dafb_holding_r` at $F9800000-$F98001FF returns
  only `reg & 0x3f` and latches `(reg>>6)&0x3f`, read back via
  `memcjr_r` at $5000E07C ($50F0E07C mirror). Our `dafbRegRead`
  (`Q605Memory.cpp:163-190`) returns the full register in one $F9800000
  access with no split. The monitor-sense reg $1C survives (value 1 < 
  $40, DrHW $1C pick stays correct), but wider regs (version/config/
  timing/RAMDAC id) come back wrong and one steers the off-by-one list
  pick. Our boot does 85 840 $F9800000 + 900 $50F0E0 reads — the protocol
  is heavily exercised. Full recipe + fix plan in TODO § Q5.1d.
- **No speculative patch applied** (project discipline): the fix is a
  real access-model rewrite (mask+latch every DAFB read, honour the
  $50F0E07C holding read/write) that must be iterated against the oracle
  to avoid regressing the passing DAFB init / Swatch VBL path.
- **Tooling**: `q605_trace --stop-at` now also dumps the PC RING
  (caller chain) and the LOWMEM globals (MemTop $108 / BufPtr $10C), used
  to pin the caller of $000094E0 to the $40809A0A sExec trampoline and to
  confirm MemTop matches MAME.

## 2026-07-18 — Q5.1c: the fatal `_sReadStruct` fully anatomised; DrHW pick proven correct; full-machine oracle blocked (round 2)

Chased the Q5 Slot-Manager overrun end to end with per-field `--wwatch`
and `--stop-at` on the RAM orchestrator. New, proven facts:
- **The fatal call originates in RAM.** At `$000094E0` (the ROM's own
  dispatch code copied to low RAM at boot) the orchestrator sets
  spID := $01 (sRsrcType) and invokes internal `_sReadStruct` (selector
  5, `$A06E`) WITHOUT pre-setting spSize.
- **`sFindStruct`/`sOffsetData` land correctly** on the DrHW sRsrcType
  body $408F27B4 = `0003 0001 0001 001C` (DrHW $1C, Hi-Res 13").
- **DrHW $1C is the CORRECT selection**, not a sense artefact: MAME
  `dafb.cpp:204` defaults the Quadra 605 monitor to 6 (Hi-Res 640×480)
  and `dafb.cpp:389-416` returns sense 6^7 = 1 — identical to our HLE
  (`Q605Memory.cpp:166` `6u^7u`). The DAFB-sense theory is dead for good.
- **The size blow-up is inside `_sReadStruct` selector 5 ($40806BE0):**
  the sRsrcType-vs-sBlock discriminator `move.w $4(a1),d0; bmi` sees
  DrSW=$0001 (positive) so it does NOT skip, then reads the long
  $00030001 at $408F27B4 as a block size → spSize = $30001−4 = $2FFFD →
  $D84C-byte overrun to end-of-ROM $40900000 → ATC miss → VEC2 #14
  (SR=$2000, a real UNHANDLED fault, unlike the SR=$2700 empty-slot
  probes) → boot ends in the POST serial console.
- **Full-machine co-simulation is BLOCKED.** MAME `macqd605` accepts our
  exact ROM (SHA1 1d833125…) but also demands the Cuda 341S0788 firmware
  ROM + NVRAM (`cuda.cpp:344`), which we do not have; the UAE oracle is
  instruction-level only. So the single upstream machine value that a
  real 605 reports differently (making the copy never reach $40900000)
  cannot be localized here. It is NOT the CPU (sst68040 7200/7200), the
  DAFB sense, or Slot-Manager arithmetic — full detail + the exact
  re-attack plan in TODO § Q5.1c. No speculative size-clamp applied (it
  would mask the divergence and risks the passing POST).

## 2026-07-18 — Q6: NCR 53C96 wired into the Quadra 605 + the sReadWord producer chain pinned (boot-integration round 1)

Merged the standalone `Ncr53c96` controller (MAME `ncr53c90.cpp`
reference: command-driven 16-byte FIFO, 24-bit transfer counter,
pseudo-DMA data path) and its `ncr53c96_test` gate, then WIRED it into
`Q605Memory`:
- register file at PrimeTime+$10000 (absolute $50010000, reg =
  (addr>>4)&$F per iosb.cpp:58-59 turboscsi_r);
- pseudo-DMA port at $50010100 → dmaRead/dmaWrite, with the same
  DRQ-gated /BERR holdoff the LC II V8 SCSI path already uses (the SCSI
  Manager's blind transfers catch the bus error to terminate; a live
  53C96 transfer count keeps DRQ asserted — macquadra605.cpp:206);
- level-sensitive IRQ into the Quadra pseudo-VIA2 SCSI line, re-sampled
  on every register/DMA access, IntStatus-read clears it
  (macquadra605.cpp:204-206 → pseudovia.cpp:148);
- `Q605Memory::attachScsi(path)` mounts the boot drive at SCSI ID 0.
`q605_trace` grew `--disk/--scsi IMG` (attach at ID 0), Cuda PRAM
persistence (q605_trace.pram + factory XPRAM), `--scsi-log FROM TO`
(register/CDB trace window), and `--wwatch ADDR` (a RAM write-watch that
dumps PC + A-registers on any store touching ADDR). ctest 26/26 (adds
ncr53c96_test); the ROM reaches and exercises the 53C96 (1682 register
reads, a bus RESET) but stops at the Q5 Slot-Manager overrun before it
selects the target.

**Blocker localization advanced (Q5.1b).** With `--wwatch` the exact
producer of the junk spSize=$0002FFFD is now pinned. The fatal copy runs
on spBlock $003FF99E; spSize is built by: `$40806C50` (sReadWord) reads
four byte-lane bytes from spsPointer into d1 → `$40806C88 move.l d1,(a0)`
stores that long into **spResult** (spBlock+0) → `$40806C26 move.l
(a0),$8(a0)` copies spResult into spSize → `$40805990 move.l $8(a0),d0;
subq.l #4,d0; $4080599E move.l d0,$8(a0)` makes spSize = spResult − 4.
In the fatal call spsPointer=$408F27B4 (the ROM's own video DrHW
sRsrcType record table), so spResult = long there = $00030001 → spSize =
$0002FFFD. The TRUE upstream bug is that the `[$db8]` Slot-Manager
sResource-list walk set spsPointer to the raw DrHW table instead of to a
genuine sBlock header. Also corrected an O6-round claim: the DAFB at
$F9800000 **is** on the boot path (the ROM zero-fills the whole register
file at pc=$00006C5E and does 93k+ reads during video/Slot init); whether
a DAFB read steers the DrHW pick ($1C = High-Res 13" was selected) is the
next thing to instrument, then WinUAE-co-simulate the walk window.

## 2026-07-18 — Q5.1a: the Slot-Manager blocker re-localized — the DAFB-sense theory is disproven, the fault is a decl-ROM parse

Chased the Q5 boot blocker (VEC2 #14 sReadStruct overrun at $40900000 →
POST serial console) instead of guessing. The prior working hypothesis —
that our DAFB monitor-sense/version answers steer the ROM to a wrong
video DrHW and so build a junk spBlock — is **DISPROVEN**: a grouped
I/O trace shows the ROM never reads the DAFB sense ($1C) or version
($2C) register at all during POST/Slot-Manager; the DAFB file sees only
8 timing-config WRITES. Moreover this ROM does not touch our DAFB HLE
address ($F9800000) at all — MEMCjr exposes the DAFB behind a 6-bit
holding-register window ($5000E000/$50F0E0xx mirror + a video block at
$50F18xxx), per MAME `djmemc.cpp:142-178`. So the sense HLE is dead code
on the boot path (logged for Q8 when video paints).

Re-localized: the faulting byte-lane copy is shared code entered via the
low-mem `[$db8]` dispatch (both sReadStruct $408059E0 and the video
builder $40806036 route through it, always on the Slot Manager's working
block $0017FFC0). The copy that overruns runs on a *separate* decl-ROM
sResource-insertion buffer at $003FF99E whose spSize=$0002FFFD was
computed as `1 − (long at the ROM's video sRsrcType table $408F27B4)` =
`1 − $30001` (see $40806036: `move.l $4(a4),d2; moveq #1,d1; sub.l
d2,d1`). The ROM reads its own DrHW record table as if the first record
header were an sBlock size. DrHW=$1C (High-Res 13") was picked.

- **Why non-obvious / what this means**: $40900000 is emergent (grep of
  Basilisk+MAME: zero hits) and a real Quadra 605 faults there too — so
  the machine-visible DIVERGENCE that makes spSize *huge* has to be an
  UPSTREAM value fed into the decl-ROM directory walk that plants
  spsPointer=$408F27B4/spSize=$2FFFD into the $003FF99E buffer. That
  producer is not yet caught (it is neither sReadStruct nor $40806036).
  A real 605's writer plants a SMALL spSize. Next front is a
  memory-write watch on spBlock+4/+8 + WinUAE co-sim of that window
  (the O1-O5 method). The serial console at $408B9928 is entered
  unconditionally by ONE POST-executive table entry ($4084AAA2 `bset
  #$10,d7`), reached via the computed dispatch at $4084AA70; it is
  downstream of the POST D7 state, which the sReadStruct fault poisons.
- Tooling: `q605_trace --stop-skip N` (ignore the first N hits of
  `--stop-at`, so a specific call in a hot routine can be isolated —
  used to prove every sReadStruct/$40806036 call uses spBlock $0017FFC0,
  ruling those two out as the producer). No machine/CPU code changed;
  CTest 25/25 unchanged. Full analysis in `TODO.md § Q5.1a`.

## 2026-07-18 — Q3: the 68040 MMU translates in Moira — full grid 7 200/7 200 pinned, the LC 475 CPU side is complete

Bus-level 040 translation (WinUAE cpummu.c model): ITT/DTT transparent
windows (WP faults even with TC.E off), URP/SRP 3-level walk with U/M
maintenance and one indirection, page-boundary access splitting
(SSW.MA), PTEST → MMUSR040, locked-RMW (SSW.LK) and MOVES (SFC/DFC),
and the format $7 access-error frame with gencpu's **last-write
dichotomy** — a fault on the instruction's final store stacks the NEXT
instruction's PC with no state restore (the OS completes the write from
WB3), everything else restarts with the pre-instruction CCR and (An)±
fixups undone. MOVEM restarts through the SSW.CM/CT latch. The 040 now
runs the mode-5-style no-prefetch-queue loop head (shared pattern with
the 030). Full details in `extern/moira/POM68K_VENDOR.md § Q3`.

- **Why non-obvious**: the WinUAE CATCH's restore block *looks*
  unconditional but is dead on last-write faults (gencpu emits
  `mmu_restart = false` + fixup disarm before the final put) — probed
  empirically before implementing; and a third oracle-glue state leak
  was found (stale `mmu_effective_addr`/`mmu040_move16[]` stacked the
  previous vector's values in format-$7 frames — glue.c zeroes them).
  Also arbitration by fresh seeds: the D6-remainder user-mode
  cpSAVE/cpRESTORE privilege rule is 020/030-only, and FGen with an An
  EA on a FPU-less 040 takes format $4, not Line-F.
- Gate: **sst68040 = 7 200/7 200** pinned across the full family×mmu
  grid (11 cells incl. fault/identity+tt) + 6 400/6 400 fresh-seed
  re-verify (301-308); sst68030 3 082, sst68000, both boot etalons —
  CTest 25/25. Not fuzzed: 8K pages (TC.P; Mac OS uses 4K).

## 2026-07-18 — Q2+Q4: the 68LC040 integer core executes in Moira, WinUAE-differential (5 400/5 400), no-FPU F-line included

Phase 3 (Mac OS 8.1 on an LC 475 / Quadra 605) CPU side, first two
milestones, converged by the established loop (fuzz040.py WinUAE-solo →
sst68040 replay → fix → fresh-seed re-verify). `Model::M68LC040` now
executes on Moira's shared C68020 core; every change is runtime-gated on
`cpuModel >= M68EC040`, so the 3 082-vector sst68030 gate and the
1 000 058-vector sst68000 gate are byte-identical. Full change list in
`extern/moira/POM68K_VENDOR.md § Q2/Q4`; highlights:

- MOVE16/CINV/CPUSH/MOVEC-040 execute; odd instruction-flow targets take
  the 040's format $2 address error with WinUAE's per-instruction A7/PC
  conventions (RTS re-pushes, RTD/RTR restore, Bcc/DBcc pre-condition
  checks); RTE handles formats $3/$4/$7 (SSW.CT continuation copy); the
  040 trace one-shot ("an SR write with old Tx set traces once") and
  post-instruction staged trace; 040 undefined-CCR rows for
  DIV/DIVL/CHK/ABCD.
- Q4 folded in: F2xx with no FPU → vector 11, format $4 frame with
  per-shape word consumption and EA (fpp.c `fault_if_no_fpu` call-site
  conventions), FBcc pseudo-conditions, FMOVEM invalid-EA Line-F. The
  sst68040 harness never attaches an FPU — an attached 68882 would mask
  the format-$4 path.
- **Why non-obvious**: two corpus-poisoning ORACLE-GLUE state leaks were
  unmasked (not Moira bugs, both sequence-order-dependent): stale
  `regs.t1/t0` at state load armed WinUAE's MakeFromSR one-shot trace
  (phantom vector-9 frames carrying the previous vector's `trace_pc` —
  an untraced RTS "taking a trace"), and the `mmu040_movem` restart
  latch made a MOVEM reuse the previous vector's faulted EA. Fixed in
  `oracle/uae/glue.c` (VENDOR.md); the corpora regenerate clean.
- Gate: **`sst68040`** (new CMake gate, tests/data/sst68040, 2 400
  pinned vectors core/random/mmu × off, soft-skip when absent) +
  3 000/3 000 fresh-seed re-verify; full CTest 25/25 including both
  boot etalons.

## 2026-07-18 — GISTPERSO (7.5) boot hang: heap corruption racing an app launch at Finder startup — NOT the pending changes, NOT the disk

User report after the host-machine crash: on the LC II, GISTPERSO-boot
(System 7.5) draws the Finder menu bar + desktop pattern, the clock
ticks, but the icons never come and "the menu is dead". Other volumes
(7.1/7.6/8.1) fine. Differential investigation (full detail in TODO
§ app-compat):

- The CPU spins forever in the ROM Memory Manager heap-walk/coalesce
  loop ($40A0E148) — a garbage block header, i.e. guest **heap
  corruption during Finder startup**, while VBL keeps the clock alive.
- **Exonerated: the uncommitted work.** HEAD and the working tree hang
  byte-identically (same screenshot md5, same hot-PC histogram).
  **Exonerated: PRAM** (known-good PRAM hangs the same) and **HFS
  structure** (catalog walks; SC2K + city play fine launched by hand).
- Startup-key probes (new `LCII_HOLD_KEYS` in lcii_trace, hex ADB codes
  held through boot): Option or Shift → desktop with icons (and no
  SC2K auto-launch); Cmd → same hang. A normal boot auto-launches
  SimCity 2000 while the Finder is still building the desktop, and one
  GUI boot survived exactly that → **timing-dependent race around the
  boot-time app launch**, deterministic loss headless, occasional win
  under GUI timing.

Workaround: boot holding Shift/Option, remove SC2K from the startup
launch; manual launch is unaffected. The deterministic headless repro
is pinned in TODO for the differential hunt for the real corruption
site. `hdv/GISTPERSO-boot.vhd.avant-reparation` backs up the image.

## 2026-07-17 — LC II runs on a dedicated machine thread; boot & secondary SCSI volumes selectable from a "Disques" menu

Interrupted by the host crash, finished and verified 07-18 (24/24
gates, long GUI sessions). Two changes from the perf/UX queue:

- **Machine thread** (`LcMachine`, main.cpp): the emulation loop +
  audio-clocked pacing move off the vsync'd ImGui thread, so a slow GPU
  frame or compositor stall no longer steals emulation time. Contract:
  input/reset cross as queued commands applied between frame slices
  (ADB/CPU objects are touched only by the machine thread), the
  framebuffer crosses as a decoded copy under a mutex (publish throttled
  to ~60 Hz when idle), the status line is relaxed atomics, and the ASC
  ring keeps its SPSC discipline (producer moved threads). Emscripten
  keeps the single-thread path — same `stepTick()`, two drivers. The
  destructor joins the thread so a stray `exit()` (Xlib's default error
  handler) can't destroy a joinable thread → std::terminate.
- **Multi-volume SCSI** (`Ncr5380` targets by ID 0-6, `V8Memory`
  `attachScsi(path, wb, id)`, CLI `argv[3..]` → IDs 1-6): the System's
  boot-time bus scan mounts the secondary volumes. The GUI "Disques"
  menu picks the boot volume and toggles secondaries from the images
  found next to the current one; any change relaunches the emulator
  (the ROM only scans the SCSI bus at boot), same execv mechanism as
  the machine switch. "Redémarrer" = one-click power cycle.

## 2026-07-17 — i-cache overlay folded into Moira's fetch path (-15%)

Second perf step from the 0.40×→1.91× pass's queue: the 68030 i-cache
timing overlay fired a **virtual** `willFetchInstr` on every instruction
word — an indirect call plus an out-of-line model per fetch, ~11% of the
whole emulator. The MC68030UM §6 model (16×4-LW logical direct-mapped,
CACR-gated, miss penalty) now lives inline in `mmuFetchWord` as
`Moira::PomIcache` behind an `armed` flag (default off; `Cpu030` arms it
and keeps the knobs/stats — POM68K_VENDOR.md § willFetchInstr). Same
model, same numbers: lcii_boot_etalon metrics byte-identical
(0.09/0.48/9583 SCSI commands), wall time **143 s → 122 s**, 24/24
green. Next in the queue: the dedicated machine thread.

## 2026-07-17 — Lode Runner "dead arrow keys": not a bug — the game binds the numeric keypad by default

User report: in Lode Runner (LC II) the arrows do nothing although the
game is otherwise perfect. Two-sided verification concluded the input
chain is correct and the behaviour matches real hardware:

- **Emulator side** — a KeyMap probe against the booted System (boot
  etalon + `AdbBus::keyEvent`, the exact entry point the GUI uses)
  shows every injected ADB raw code sets the right virtual-key bit in
  the System's KeyMap ($174): arrows $3B-$3E → virtual **$7B-$7E**,
  keypad $52-$5C → virtual $52-$5C, letters identity. That raw→virtual
  arrow remap is Apple's own KMAP doing its job — on ADB keyboards the
  old M0110A arrow codes $3B-$3E were reassigned to modifiers, and
  arrows moved to $7B-$7E.
- **Game side** — extracted the app's CODE resources off the HFS image
  (it is *Lode Runner: The Legend Returns*, Presage 1994) and
  disassembled its input path: gameplay polls a `_GetKeys` snapshot
  against a per-action **key-binding table stored in its `pref 200`
  resource**. The defaults are `56 58 5B 57 59 5C 53 54 55` — keypad
  4/6/8/5/7/9/1/2/3. No $7B-$7E (arrow) code appears in either the
  factory or the user's saved bindings: **the game simply doesn't bind
  the arrows**, on a real LC II either.

Conclusion: play with the numeric keypad (4← 6→ 8↑ 5↓, 7/9 = dig) or
rebind inside the game's options. No emulator change.

<a id="2026-07-17-performance-pass-realtime"></a>
## 2026-07-17 — Performance pass: 0.40× → 1.91× realtime at the Finder (the sound stutter was the emulator falling behind real time)

The audio-clocked pacing needs ≥1× realtime to hold tempo; a gprof
profile (i7-10700F, headless Finder workload at cache-boost 4) showed
0.40× with **38% of all time inside two 22-entry ATC scans run on every
memory access** (`mmuAtcLookup` 25% + `mmuAtcTouch`'s second scan), and
every 16-bit bus access splitting into two full `read8` decode cascades
(1.8G calls/10s). Four fixes, semantics strictly preserved:

- **Moira ATC, O(1) pseudo-LRU** (`mmuAtcTouch`): a counter mirrors the
  number of set history bits, replacing the per-access 22-entry "any
  clear bit left?" walk. Byte-identical behaviour (every mru transition
  goes through touch or the resets).
- **Moira ATC, last-hit probe** (`mmuAtcLookup`): remembers the line
  that satisfied the previous lookup per (fc, direction) and probes it
  first — page-local streams check one entry instead of scanning 22.
  Same checks, same write-upgrade invalidation, same LRU touch; a stale
  remembered line just falls through to the full scan.
- **V8Memory word fast paths**: `read16`/`write16` service RAM, ROM and
  VRAM as single word accesses (side-effect-free regions only — I/O and
  the overlay keep the sequenced two-byte path); `ramIndex` inlined into
  the header.
- **Build: `-march=native` + LTO** by default (`POM68K_NATIVE=ON`; the
  emulator is built from source on the machine that runs it — pass
  `-DPOM68K_NATIVE=OFF` for a portable binary).

Result: **4.8× faster** (0.40× → 1.91× realtime, boost 4 included), so
the DAC-paced sound now has ~2× headroom at the Finder. The full CTest
suite drops from 316 s to 182 s; 24/24 green including sst68030's 3082
vectors (the ATC changes are semantics-exact by construction and the
MMU/PTEST corpus agrees); SC2K and Lode Runner repros unregressed.

Not done (next steps if a heavier workload still starves, in order of
bang-for-buck): a dedicated machine thread (decouple emulation from the
vsync'd ImGui loop — the 16-core host runs everything on one core
today), trimming the per-fetch i-cache overlay cost (~11%), and only
then a 68k→x86 JIT (weeks of work, and it would obsolete the fuzzed
Moira interpreter's exactness guarantees — last resort).

## 2026-07-17 — Lode Runner launch freeze: odd-SP interrupt frames were corrupt (vendored Moira fix) + sound tempo locked to the host DAC

**Lode Runner froze the machine at launch** (hard halt, no bomb). Chain,
established with a headless keyboard-nav repro (`scratchpad/lrtest*`):
the game's launch drawing runs QuickDraw's conversion blit whose 3-byte-
per-pixel stack temps leave **SP odd** (legal on the 68030); a level-1
interrupt accepted there pushed its frame through
`writeStackFrame0000`'s 010/020 branch, which still applied the 68000's
`& ~1` A0 masking — the whole frame landed one byte low while RTE reads
at true addresses → spurious FORMAT ERROR (vector 14) on the RTE → ROM
system error path → its bomb renderer died on a second fault → bus-error
cascade until the SSP went odd → double fault → frozen Mac. Fixed in the
vendored Moira (frames written byte-exact at the true SP; also fixed a
double ×4 of the stacked vector offset — $190 instead of $64 — passed by
`execInterrupt<C68020>`); details in POM68K_VENDOR.md § Odd-SP interrupt
frames, minimal repro `scratchpad/oddframe.cpp`. 24/24 CTest (sst68000
and sst68030 corpora unaffected by construction — verified green), Lode
Runner now reaches its title screen, SC2K repro still crashes=0.

**Sound tempo wobble fixed by audio-clocked pacing** (GUI): while the
guest streams sound, the emulation speed IS the musical tempo, so the
frame loop now paces itself against the host DAC instead of the host
CPU: when sound was heard recently, each GUI tick emulates only enough
60.15 Hz frames to keep `MacAudioHost`'s ring near ~100 ms
(`buffered()`), pushing silence too (`pushRaw` — inter-note gaps are
part of the timeline); with no sound, the old time-budgeted turbo runs
(fast boot/Finder). The DAC's fixed 22 254 Hz consumption locks the
tempo to real time and absorbs the vsync-60.00 vs frame-60.15 Hz drift
with no resampler; a starvation guard keeps the machine alive if the
audio device disappears. `src/main.cpp` LC II frame lambda +
`MacAudioHost::{buffered,pushRaw,started}`.

## 2026-07-17 — SC2K "coprocesseur absent" ROOT-CAUSED AND FIXED: Egret mid-flight packet retraction manufactured ghost ADB sessions

The crash that survived every timing fix (the ★ TODO item) was never the
VBL/A5 phase race — measurement killed that theory (`racecheck.cpp`:
over 83K interrupts, ZERO were accepted with A5=$4FA8; the O6.12
`irqDelay=2` guard protects the $A4B414-$A4B418 window exactly as
designed; every in-window acceptance lands at $A4B41C, after the movem).

The real chain, established with deterministic single-step harnesses
(`stackwatch2` → `dispatchtrace` → `jumptrace` → `coptrace` → `d2trace`
in the session scratchpad):

1. `Egret::tick` retracted an initiated packet the host hadn't acked for
   150K cycles (`kAbortDelay`). But initiation had already loaded the
   sync byte into the VIA1 shift register — the host's level-1 interrupt
   was in flight. Under SC2K's per-VBL redraw load the ROM's byte-read
   loop is legitimately preempted for 300K+ cycles, so the retraction
   fired routinely during play with the mouse moving.
2. The late host then consumed the stale sync, found XCVR_SESSION
   already low, and recorded a **1-byte session**.
3. The ROM Egret driver computes the ADB record's data length as
   `received - 4` (header) with no guard — the real Egret can never
   deliver a short initiated packet — giving **D2 = -3 = $FFFD**
   ($A14ACE/$A14AD2 `move.w ($10,A2),D0; subq.w #4` → $A151F4
   `move.w D0,D2`).
4. The ADB response dispatcher ($A0A494/$A0A4B4-$A0A4C0) then runs
   `move.b (A2)+,(A0)+ / dbra D2` — 64K iterations — **copying 64KB
   over the stack** starting at its own frame (A0=$91ACCA = entry SP),
   wiping the saved-SP cell, the caller frames, and the application's
   A5 world up to the jump table.
5. The epilogue's `movea.l (A7),A7; rts` then pops a smashed cell and
   returns into a QuickDraw dither pattern ($6D $B6 $DB repeating —
   decodes as `blt.s -74` chains, the -$48-stride PC walk in the crash
   rings); the wander takes an address error, the System Error handler
   calls _ExitToShell, whose patch dispatches through the (also smashed)
   jump table entry `$1400094E` → Line-F → the ROM's generic
   "coprocesseur arithmétique absent" bomb. All red herrings.

FIX: an initiated packet is COMMITTED once its sync byte is on the wire
— the retraction is removed entirely (`Egret::tick`; the transfer is
synchronous and host-clocked on the real part, and collisions are
host-handled — the ROM's senders check XCVR first). The boot-era
"bus-quiet deadlock" the retraction had been added for does not return:
24/24 CTest green including `egret_test` and both boot etalons. The
crash repro (loadcity640 navigation + 6000-frame mouse wiggle, formerly
2 Line-F bombs) now runs clean: **crashes=0**; a 30000-frame endurance
run is clean too. The abort machinery (`kAbortDelay`, `abortTimer_`)
is deleted outright. The i-cache overlay + irqDelay remain as-is — they
were correct; they just weren't this bug.

## 2026-07-17 — 68030 instruction-cache timing overlay (replaces the flat boost)

Replaced the constant boost with a real (if small) model of the 68030
instruction cache. New vendored Moira hook `willFetchInstr(addr, super)` fires
on every instruction-word fetch (`mmuFetchWord`, the mode-5 030 fetch choke
point); `Cpu030` models the on-chip 256-byte i-cache (16 lines × 4 longwords,
logical, direct-mapped, flushed on the CACR clear strobes — needed because the
blit is self-modifying) and charges a fetch-bus penalty (`icacheMiss_`) only on
a MISS, while the core runs at a resident-code ceiling (`cacheBoost_`). Net:
cache-resident code runs near the ceiling, miss-heavy cold code is throttled
toward real speed — the per-code-path behaviour of the real cache instead of a
flat fudge that can't tell them apart. Both knobs live-tunable
(POM68K_CACHE_BOOST ceiling, POM68K_ICACHE_MISS penalty). 24/24 CTest green
(incl. sst68030 3082 vectors — the hook is state-neutral, and lcii_boot_etalon).

**Validated by measurement** (`scratchpad/icachestat.cpp`, `Cpu030::
icacheStats`): the guest runs CACR-I=1 (i-cache enabled); boot is 80% hit; and
the SimCity **redraw hot path is 95% cache-resident** over 453 M fetches —
confirming the overlay boosts exactly the code that livelocks.

**Honest limit — the overlay does NOT cure the black-forest crash, and that's
now diagnosed.** A headless sweep shows the crash is *non-monotonic* in the
boost (boost 4 crashes more than 2): if it were a throughput problem more boost
would always help. It doesn't — so it's a **VBL/redraw phase race** (the VBL is
taken at $A4B416, between SC2K lowering IPL at $A4B414 and restoring A5 at
$A4B418; whether it lands there shifts with the boost). Clearing it by brute
throughput needs ~24×, which no realistic cache (~2-4×) can produce. So the
overlay is the correct throughput model and gives uniform tempo, but black
forest needs a separate structural fix to the interrupt timing (TODO §
App-compat). Workaround: raise POM68K_CACHE_BOOST.

## 2026-07-17 — retire the adaptive cache boost for a constant ratio

The adaptive cache boost (base 2, spiking to maxBoost 24 during heavy per-VBL
redraws) gave a *varying* CPU/peripheral ratio. Once app sound worked
(pseudo-VIA fix below) that varying ratio made the sound tempo wobble audibly
(the emulated sound production isn't locked to the host audio clock, so the
2↔24 flips over/under-ran the ASC out ring), and it still only *deferred* the
SimCity livelock. Replaced it with a SINGLE CONSTANT ratio (`Cpu030
cacheBoost_`, default 2, tunable live via POM68K_CACHE_BOOST, range widened to
1-64), like real hardware which has one fixed CPU/peripheral ratio → uniform
tempo. Removed `maxBoost_`, the IRQ-rate hysteresis, and the `willInterrupt`
counter; `flushTicks` now scales by the constant. 24/24 CTest green.

**What the boost physically is** (verified while doing this): Moira emulates
the 68030 on its `Core::C68020` execution core (MoiraTypes.h:66) — 68020
per-instruction cycle counts (advisory placeholders, POM68K_VENDOR.md), **no
i-cache, no d-cache**, less pipeline overlap than a real 030. The real LC II
68030 has a 256 B I-cache + 256 B D-cache (the System enables them via CACR),
so tight loops run from cache with no fetch bus cycles and it executes far
more instructions per unit machine-time than Moira charges. The boost is the
scalar that compensates for that whole gap.

**Why a constant can't fully win** (headless sweep on the biggest SimCity city
"black forest monstre"): boost 2 (near real-time) still crashes it, boost 4 is
worse (non-monotonic — it's a VBL/redraw *phase race*, not pure throughput),
and clearing it needs ~16-24 which runs ~10× slow. Root reason: the blit is a
*tight cached loop* that gains ~5×+ from the real cache while other code gains
~1-2× — a single scalar can't say "tight loops fast, rest normal". Only a real
68030 i-cache timing model captures that. Intended long-term fix: a cache
overlay on Moira's prefetch path keyed on `cpuModel >= M68030` + CACR (NOT a
new `Core::C68030` — the 020/030 share the execution core by design; the
overlay is the right granularity). Tracked in TODO § App-compat.

## 2026-07-17 — app sound reaches the ASC (pseudo-VIA ASC IRQ was edge-only)

Apps on the LC II were silent — only the ROM boot chime ever played. Traced
with new ASC/trap diagnostics (`AscV8::onWrite`/`onRead` taps,
`PseudoVia::reg()` peek, `scratchpad/{ascprobe,sndtrace,sndtrace2,sndtrace3}
.cpp`): SimCity 2000 *does* call the Sound Manager (`SndNewChannel`,
`SndPlay`, `SndDoCommand`, 281×`SoundDispatch`) and the Sound Manager *does*
init the ASC (reads version $E8, sets FIFO mode) and *does* enable the ASC
interrupt (pseudo-VIA IER bit 4) — but **IFR bit 4 never latched**, so the
level-2 handler never serviced the ASC and never refilled the FIFO → silence.

Root cause: the ASC IRQ is **level-triggered**, but `PseudoVia` only updated
IFR bit 4 on line *transitions* (the edge-driven `onIrq` callback), and
`recalcIrqs()` erases any non-enabled pending bit. After the boot chime the
FIFO empties and the ASC half-empty line asserts and **stays asserted**;
while the ASC interrupt is disabled that pending bit is masked away. When the
Sound Manager later enables the interrupt, the line is *already* high — no new
transition — so IFR bit 4 was never re-latched. The slot IRQs already
re-derive their level every `recalcIrqs()`; the ASC path uniquely relied on
edges.

Fix (`src/PseudoVia.*`): store the raw ASC line level (`ascLine_`) and
re-sample it into IFR bit 4 on every `recalcIrqs()`, exactly like the slot
lines — so the level survives an enable/disable and re-latches when the
System enables the ASC interrupt with the line already high. Verified: SC2K
now writes 3996 non-silent samples to the FIFO on city-load (was 0); 24/24
CTest green (incl. pseudovia_test, asc_test, lcii_boot_etalon). Known
follow-ups (TODO § App-compat): the emulated sound *production* isn't locked
to the host audio clock and the adaptive cache boost's varying CPU/peripheral
ratio makes the tempo wobble, and the SC2K livelock crash still recurs — both
are the same open timing project.

## 2026-07-17 — adaptive cache boost (fixes big-city SimCity crash)

The user's biggest SimCity 2000 city ("black forest monstre") still crashed
on load at 640×480 — reproduced headless (`scratchpad/loadcity640.cpp`:
navigates SC2K → Charger ville → SIM VILLES → the save, then wiggles the
mouse during the redraw). At 512×384 it was fine; the 640×480 redraw is
~1.6× heavier and overran the fixed boost=2. A fixed boost=6 fixed it but
slowed the whole emulator (6× more instructions/frame); boost=4 was worse
than 2 (crash count is non-monotonic — the boost shifts *when* the livelock
trips).

Fix: `Cpu030` boost is now ADAPTIVE. A normal frame takes few interrupts; a
heavy per-VBL redraw takes many dozen (the redraw handler re-enters). When
last frame's IRQ count crosses a threshold, the CPU runs at maxBoost_ (24)
with ~0.75 s hysteresis, then falls back to the base boost (2). So normal
play stays fast and only heavy redraws briefly slow down — no crash.
Verified: black forest monstre loads clean at 640×480; 24/24 CTest green.
POM68K_CACHE_BOOST still sets the base floor.

(maxBoost_ was raised 16→24 once flushTicks was corrected to scale by the
*active* boost — see the second audit below. Before that fix flushTicks
divided by the base boost while the CPU ran at 16×, so peripheral time ran
8× fast and accidentally out-ran the livelock; scaling correctly restored
real VBL cadence, which needed a higher ceiling to clear the redraw.)

## 2026-07-17 — adversarial subsystem audit #2: 9 correctness fixes

A second adversarial multi-agent audit (LC II subsystems vs MAME/Basilisk/
Z8530 & 6522 & SCSI-1 manuals, findings attacked by skeptics) surfaced 17
candidates; after maintainer verification 9 applied, the rest rejected as
false positives or deferred as defensive fixes for access patterns the
guest never performs. 24/24 CTest green.

- **flushTicks scaled by the base boost, not the active one** (`Cpu030`):
  during an adaptive-boosted redraw the CPU ran at maxBoost× but flushTicks
  divided elapsed cycles by cacheBoost_, so VBL/VIA/ASC time ran up to 8×
  fast. Added `activeBoost_` (the boost in force this frame) and scale by
  it. This restored real peripheral cadence (correct sound/timer pitch);
  maxBoost_ raised 16→24 to keep black forest crash-free at true speed.
- **VIA CA2/CB2 flag over-clear** (`src/Via6522.cpp`): an ORA/ORB access
  cleared CA2/CB2 unconditionally, but in the ROM's independent-interrupt
  PCR mode ($22) the RTC 1-second flag must survive a racing port access
  (R6522 §3.2.3). Added `clearCaFlags`/`clearCbFlags` that honour the PCR
  mode.
- **VIA T2 low-byte latch** (`src/Via6522.cpp`): T2CL wrote the counter
  directly; on the 6522 it only stages a low latch that T2CH commits
  (§5.6). Added `t2ll_` staging.
- **ASC FIFO overrun wrote past FULL** (`src/Asc.cpp`): a push at cap=0x400
  still stored and wrapped wr_, overwriting the oldest unread byte. Guarded
  the store by `cap_ < 0x400` (MAME stalls on full).
- **V8Video 16bpp fetch could read one byte past VRAM** (`src/V8Video.h`):
  bounds-guarded the `vram[off+1]` high-byte read.
- **RTC PRAM write underflow** (`src/Rtc.cpp`): addr 14/15 fell through to
  `pram_[addr-16]` = `pram_[-2]/[-1]`. Added an `addr < 16` guard.
- **Egret second-accumulator not reset** (`src/Egret.cpp`): `secAcc_`
  survived reset; zeroed it.
- **SCC RR2 vector priority Ext-over-Tx** (`src/Scc8530.cpp`): within a
  channel the Z8530 ranks Tx above Ext/Status; the status-low code checked
  extPending first. Swapped to Tx-first.
- **SCSI MODE SENSE / REQUEST SENSE conformance** (`src/ScsiDisk.cpp`):
  MODE SENSE(6) now fills the mode-data-length (byte 0 = n−1) and the
  block-descriptor block length (512); REQUEST SENSE clamps the
  additional-length to `min(10, alloc−8)` instead of a fixed 10.

Deferred (not bugs in practice): a defensive SCC word-access fast-path —
a 16-bit access to SCC space double-advances the read pointer, but the
Mac SCC driver only ever uses `move.b`, so it never triggers. Noted in
TODO rather than adding an unverifiable fast-path.

## 2026-07-17 — adversarial subsystem audit: 3 correctness fixes

An adversarial multi-agent audit (11 LC II subsystems vs MAME/Basilisk/
manuals, each finding attacked by two skeptics) surfaced 6 candidates;
after maintainer verification, 3 applied, 3 rejected. 24/24 CTest green.

- **irqDelay not cleared on reset** (`extern/moira/Moira/Moira.cpp`): the
  O6.12 SR-write IRQ-recognition delay survived /RESET (reset zeroes ipl/
  fcl/mmu/fpu but not this new field). Added `irqDelay = 0;`. State
  hygiene; post-reset IPL=7 masks any observable effect.
- **SCC IRQ line was latch-high only** (`src/V8Memory.cpp` tick): was
  `if (scc_.irqAsserted()) sccIrq_ = true;` — one-directional, so a
  de-asserted SCC could leave a stuck IPL 4. Now `sccIrq_ =
  scc_.irqAsserted();` (bidirectional; updateIrq() applies it just after).
  Behaviour-preserving in every currently-reachable state, removes the
  fragility.
- **SCC Break/Abort ext-int latch was order-dependent** (`src/Scc8530.cpp`):
  it latched only when WR15 bit 7 was written after WR1 bit 0, not the
  reverse. Per the Z8530 UM the request must fire when the last required
  enable bit is set, order-independent. Added the symmetric WR1-last block.

Rejected: ADB Talk-reg-3 "missing length byte" (FALSE POSITIVE — Basilisk's
`data[0]=2` is the ADBOp buffer format, not the ADB wire the Egret HLE
speaks; reg 0 already works without it, so adding it would break ADB init);
WRITE(10) length-0 → 65536 (FALSE POSITIVE — 16-bit CDB length 0 = 0 blocks
per SBC, not 65536); MMU hardcoded page mask (harmless — re-masked
downstream at :782 and oracle-matched, not worth risking the 3082-vector
sst68030 gate).

<a id="2026-07-17-lcii-gui-640x480"></a>
## 2026-07-17 — LC II GUI defaults to 640×480

The GUI now boots the LC II at 640×480 (13"/14" RGB) instead of 512×384:
it's the roomiest built-in mode and some software needs ≥640×400 (Lode
Runner errors out at 512×384 with "requires a monitor of 640×400 or
greater", Error #713). `POM68K_MONITOR=512` forces the old mode; the CPU
window buttons still switch live. Tests keep V8Memory's own 512×384
default (only the GUI path picks 640). Note: a freshly-selected monitor
comes up B&W (depth is per-monitor); pick 256 colors in Tableaux de bord
→ Moniteurs and restart once — the choice then persists via SCSI
write-back.

## 2026-07-16 — LC II keyboard: arrow keys + numeric keypad

The LC II ADB key table (`main.cpp`) was missing the arrow keys and the
numeric keypad, so games that steer with the arrows (Lode Runner) got no
character movement. Added them with the correct ADB raw codes (arrows
$3B-$3E, keypad $52-$5C; the table stores `code<<1` and sends `code>>1`).

## 2026-07-16 — SimCity 2000 crash fixed: 68030 i-cache throughput model

SC2K crashed in-game with "coprocesseur arithmétique absent" (Line-F).
Full diagnosis (memory `pom68k-simcity-crash`, TODO § O6): NOT an FPU bug
and NOT a Moira bug — a WinUAE co-simulation (`scratchpad/cosim.cpp` +
`dumpstate.cpp` against `oracle_uae`) matched Moira instruction-for-
instruction over 2M+ steps, and the interrupt exception frame was correct
too. It's an interrupt-timing LIVELOCK: SC2K's per-VBL screen redraw is a
QuickDraw blit that lowers IPL just before restoring A5 ($A4B416→$A4B418);
an interrupt fires in that 1-instruction window, its redraw handler runs
~a frame, and by the time it returns the next VBL is already pending →
taken again before A5 is restored → the redraw re-enters with A5 still the
blit working value ($4FA8) → `jsr (A5+$14AA)` into garbage. Measured: from
that IRQ, 2M instrs never reach the A5 restore.

Root cause: the real 68030 runs tight loops from its 256-byte on-chip
instruction cache at ~1 cycle/word, executing far more instructions per
frame than Moira (no i-cache model) accounts for, so its redraw finishes
within the frame. We model that throughput: `Cpu030` runs the core `cacheBoost_`× more
instructions per unit of peripheral time, and `flushTicks()` scales
elapsed Moira cycles back down so the VBL / VIA timer / ASC cadences stay
at their real rate (the sound/clock are unaffected — only the CPU does
more work per frame). Default 2, overridable with **POM68K_CACHE_BOOST**.

The real trigger was found (2026-07-16): the crash needs the MOUSE MOVING
during the initial map redraw. Moving the mouse makes the System redraw
the cursor every VBL, which tips the already-heavy redraw over its frame
budget → the livelock returns (headless repro `scratchpad/navtest.cpp`
with a mouse "wiggle": 12427 crashes). The throughput model alone wasn't
enough. The real fix is **Moira's IRQ-recognition delay after a mask-
lowering SR write** (POM68K_VENDOR.md § O6.12): the 68k doesn't sample
interrupts until after the instruction following a mask change, which
guarantees the blit reaches its A5 restore between IRQs. Delay depth 2 +
cache boost 2 together take the wiggle repro to **0 crashes**; 24/24 CTest
green (incl. sst68030's 3082 vectors, cpu_smoke, lcii_boot_etalon). Verified: SC2K boots to gameplay and advances jan 1900 → jan
1901 with no crash (headless nav, `scratchpad/navtest.cpp`), and
`lcii_boot_etalon` still passes.

A stricter fix — Moira's one-instruction interrupt-recognition delay after
a mask-lowering SR write (M68000 PRM, which guarantees forward progress) —
was tried and REVERTED: it perturbed IRQ timing elsewhere and actually
reintroduced the crash. The throughput model is the shipping fix.

<a id="2026-07-16-selectable-resolution"></a>
## 2026-07-16 — Selectable resolution (512×384 / 640×480) + per-monitor depth

The LC II built-in V8 video drives two color modes; which one is picked
by the *monitor sense code* (`montype_`), the ID resistors a real Mac
reads off its video connector at reset. Exposed it two ways: env
`POM68K_MONITOR=640` at launch, and live buttons in the CPU window
(512×384 / 640×480). Switching does a Mac reset (the ROM reads the sense
at boot only). Window grew to 1320×1040 so 640×480 at 2× fits.

Only these two — 640×870 portrait needs a framebuffer wider than the V8
provides (870×1024 > the 512 KB VRAM), and nothing larger existed on the
LC II's built-in video. Larger modes would mean emulating a different
machine (NuBus video card, later Mac).

Per-monitor depth: a real Mac keeps each display's bit-depth separately,
but our sPRAM models one shared video block ($58-$5A: depth + mode), so
booting a second monitor rewrote the first's depth — alternating 512↔640
turned the 512 back to B&W (found immediately). `setMonitorSense` now
parks the outgoing monitor's $58-$5A and restores the incoming one's, so
each resolution keeps its own color choice within a session. Known limit:
the parked sets aren't persisted to the `.pram` (which holds one block),
so quitting in 640 B&W means the next launch starts 512 in B&W until you
re-pick 256 colors — recoverable, and 512 (the default) normally stays
color across restarts.

## 2026-07-16 — SCSI write-back (persist guest disk writes)

`ScsiDisk::open(path, writeBack)`: with write-back on, every WRITE(6)/
WRITE(10) is written through to the backing file immediately and flushed
(no exit-time step to miss if the process is killed) in addition to the
in-memory image. The GUI (`main.cpp`, both the Plus and LC II loops)
attaches with write-back ON — the emulated Mac is a daily driver, saves
made inside it must survive. Tests keep the default (`writeBack=false`,
read-only) so the reference images (`hdv/boot.vhd`, `GISTPERSO-boot.vhd`,
`HD20SC.vhd`) are never modified — verified: their mtimes are unchanged
after a full 24/24 CTest run. If the file can't be opened read-write (or
a write later fails, e.g. disk full) it warns on stderr and falls back to
in-memory-only rather than aborting. Direct `image()` pokes (the etalon's
$6A DDM fixup) still bypass the file, as before. Verified end to end: a
writable copy's bytes change across a boot and it re-boots cleanly from
its own modified state.

## 2026-07-16 — LC II color (8 bpp by default) + peripheral-tick batching

- **The Finder ran black & white**: the machine was fine — the ROM
  video driver exposes 1/2/4/8/16 bpp through Monitors (verified by
  scripting the ADB mouse through Tableaux de bord → Moniteurs headless)
  — but the depth defaults to B&W until the user picks a mode, and the
  choice is only committed to XPRAM at the next Mac restart. XPRAM $58
  is the built-in-video sPRAM byte: $80 = ROM cold-boot flag (mode 0 =
  1 bpp), low bits = mode index ($83 = 8 bpp). `Egret::factoryDefaults`
  now seeds $83, so a fresh PRAM boots straight into 256 colors; an
  existing `.pram` keeps whatever Monitors last committed.
- **GUI felt slow**: three causes. (1) The fixed turbo ×8 asked for 8×
  real time when the core sustains ~1.4× on a free host core — every
  GUI frame blocked ~100 ms of emulation and the UI (and mouse) dropped
  to ~10 fps. The LC II loop now time-budgets turbo: one frame slice
  always (real time at vsync), more only while <10 ms of the frame
  budget is spent. (2) `Cpu030::sync` ran the full `V8Memory::tick`
  sweep (VIA + Egret + ASC + SWIM + SCC + IRQ resolve) on every bus
  access; peripherals only need to be current at a device-space access,
  so ticks now batch up to 128 cycles (8 µs) and `V8Memory` flushes
  before any I/O register touch — +17 % core throughput (18.4 →
  21.5 MHz emulated, CPU time). (3) Not the emulator: the host was
  saturated by stale test processes from earlier sessions.
- NB: SCSI writes remain in-memory only (`ScsiDisk`) — Finder/desktop
  changes and the Monitors 'scrn' resource are lost when the emulator
  exits; only XPRAM (the `.pram` file) persists. Write-back is backlog.

## 2026-07-16 — LC II GUI showed a black screen (texture alpha)

The V8 machine booted fine (etalon green, headless framebuffer decode
normal) but the GUI stayed black: `V8Video::decode()` packs `00RRGGBB`
(alpha byte 0), and ImGui renders textures with alpha blending enabled,
so the whole screen texture drew fully transparent over the dark window
background. The Plus path never hit this because `MacVideo` emits
`FF000000`/`FFFFFFFF`. Fix in `main.cpp` only (the decode contract and
its pinned tests keep `00RRGGBB`): force `A=$FF` on each pixel before
the `GL_BGRA` upload.

## 2026-07-16 — review fixes (8-angle bug hunt) + UI: mouse capture, drag fix, machine menu

Confirmed findings from the multi-angle review of the pending O6 work,
all fixed and re-gated (24/24 CTest):

- **`read16` byte order was compiler-dependent** (`V8Memory.cpp`,
  `MacMemory.cpp`): the two side-effecting `read8` calls were unsequenced
  operands of `|` — a right-first compiler would byte-swap every 16-bit
  SCSI pseudo-DMA blind transfer (silent disk corruption on e.g. the
  planned WASM/clang target). Now two sequenced statements.
- **VIA1 port-B input register**: `setInB` replaced the whole register
  with only the XCVR bit, so every other input pin read 0 instead of the
  6522 pull-up 1 (incl. the ROM's legacy RTC-probe lines PB0-PB2). Now
  `$C7 | xcvr<<3`. NB pull-ups must NOT extend to PB4/PB5: those are
  host-driven Egret handshake lines and the HLE is edge-triggered —
  modeling them pulled-up while DDRB is still 0 at reset reads as a
  phantom session rise and wedges the transport (found the hard way:
  black-screen etalon).
- **SCC TxIP was latch-on-enable**: a real 8530 sets Tx-Int-Pending when
  the buffer BECOMES empty, not because Tx IE is enabled over a
  never-filled buffer — the old model fired a spurious level-2 on the
  Mac Plus for any app arming WR1 bit 1. Now edge-triggered via a
  became-empty event, consumed by Reset Tx Int Pending.
- **SCC Break/Abort re-latch was a free-running ~7.8 kHz timer**: any
  LC II software arming WR15 bit 7 + MIE would have received a perpetual
  interrupt storm. Now event-driven — each Reset Ext/Status the driver
  issues re-arms a ~130 µs countdown (ties the abort stream to actual
  servicing, which is all the LAP retry rundown needs); an
  armed-but-unserviced channel latches exactly once. Both abort paths
  now also require WR1 bit 0 (per-channel Ext Int Enable), like the DCD
  path. `scc_ext_test` extended to pin all of this (18 checks).
- **Audio silence gate was DC-blind** (`MacAudioHost.h`): an underrun
  ASC FIFO repeats its stale byte (MAME-faithful — the core is correct),
  a full-scale DC stream the old `peak < 0.01` gate happily pushed into
  the ring from power-on. Gate is now on min/max span (AC amplitude).
- **`localTalkWatchdog` fired silently**: it pokes guest RAM on a
  fingerprint pinned to one AppleTalk version's globals — now logs to
  stderr when it releases the mutex.
- **`lcii_trace` "stopped" flag was `(SR & 0)`** — constant false; the
  one diagnostic separating a STOP-parked CPU from a spin loop never
  reported. Added `Moira::isStopped()` (vendored, POM68K_VENDOR.md).
- CLAUDE.md gate count corrected (24, not 22).

UI (user request): the **Delete key toggles hard mouse capture** (GLFW
disabled cursor, raw deltas, ImGui mouse off); the emulated screen is
now an InvisibleButton, so a **drag started on the Mac screen never
moves the ImGui window** (title bar still does) and keeps feeding the
Mac after the pointer leaves the item — Finder drag-and-drop works; a
**main-menu-bar "Machine" menu switches between the Mac Plus and
Mac LC II profiles** (relaunches the process on the other ROM, PRAM
saved first; entries grey out when the ROM is absent). Shared
`ScreenInput` helper + one key table remain a TODO cleanup.

## 2026-07-16 — O6.11 RESOLVED: GISTPERSO boots to the Finder — Egret XPRAM protocol fix makes AppleTalk genuinely inactive

The clean fix option (a) landed: AppleTalk is now **inactive at boot**, so
`.MPP` never brings up LocalTalk and GISTPERSO's System 7.5 boots to the
Finder desktop (menu bar + mounted volume, screenshot-verified; the
downstream $8009372A wedge is gone with it — it was fallout of the
watchdog's crude half-initialised give-up, which no longer triggers).

**Root cause — an Egret HLE protocol divergence, not AppleTalk itself.**
Where classic Mac OS keeps the flag (primary sources: Apple's leaked
System 7.1 "supermario" tree; verified against this System's own `lmgr`
disassembly):
- "AppleTalk active" = classic-PRAM **SPConfig** byte, low nibble =
  port B use: 1 = useATalk, 2 = useAsync (Patches Release Notes radar
  #1032330; `BeforePatches.a` sets `emAppleTalkInactiveOnBoot` from it).
  On Egret machines SysParam bytes 0-15 live at **XPRAM $10-$1F** (so
  SPConfig = XPRAM $13, Basilisk's default $22 = inactive), bytes 16-19
  at $08-$0B.
- XPRAM `$E0-$E3` (`LAPMgrEqu.a: ATalkPRAM`) is only the **connection
  selector** (low byte = 'atlk' resource id, 0 = built-in LocalTalk); a
  bad id **falls back to built-in** (`NetBootlmgr.a InstallE`), so
  Basilisk's `$00F1000A` never disabled anything — dropped, zeros now.
- AppleTalk 57.x self-heals SPConfig 0/$F → 1 (= active!). That is what
  fired here: `lmgr` found $1FB=$FF and wrote $F1 → PortBUse=1 → the LAP.

Why $1FB was $FF: the ROM's SysParam restore never completed, because
**three Egret XPRAM wire-protocol behaviours were wrong** (pinned from
the ROM's own drivers: sender $40A0C5CC/$40A1557A, transaction engine
$40A14912, 24-bit reader $A4A33C):
- **ReadXPram $02 and GetPram $07 are byte STREAMS with no length on the
  wire** — [1,2,1,addr] / [1,7,addrHi,addrLo]; the ROM's 'NuMc' check
  reads 4 bytes, the SysParam restore reads 16 (at $10 → $1F8!) then 4
  through ONE GetPram each, the boot-flag read takes a single byte, all
  with identical commands. Egret now streams 32 bytes.
- **The HOST terminates a stream** by dropping SYS_SESSION after its
  count, then waits for XCVR_SESSION release ($40A149C4, $A4A3B4-BC).
  `Egret::portBChanged` now aborts the reply and releases XCVR on a
  session drop during RESP_SEND. (Old 1-byte replies only worked because
  XCVR happened to drop with the single byte.)
- **WriteXPram $08 existed only on the wire** ([1,8,1,addr,data…], length
  = the data) — unhandled, it was ack-swallowed, so the ROM's 'NuMc'
  signature write never stuck and **every boot re-ran the cold-PRAM
  XPRAM re-init, twice** (~160M cycles: WarmStart now at 170M vs 330M).

Chain after the fix: 'NuMc' validates → warm-PRAM path → SysParam block
restored ($1F8 = A8 00 00 **22** CC 0A CC 0A …) → SPConfig port B nibble
= 2 (async) → `lmgr` LInit sees AppleTalk inactive → `.MPP` never opens
LocalTalk (0 SCC WR15 arming, 0 level-4 IRQs the whole boot). The
Chooser can turn AppleTalk back on (writes SPConfig=1 to XPRAM $13,
battery-persisted) — that path re-enters the O6.10/O6.11 SCC + watchdog
machinery, which stays as the fallback.

Correction to the 2026-07-16 entry below: GISTPERSO's System (F1-7.5,
French System 7.5) **does** carry `ltlk` 0-7 + `atlk` 1/3 + `lmgr` 0 —
the LAP code at $A5F4C-$A73A4 is `ltlk` 0 itself (resource-offset match
on the `$63e` mutex spin at +$5F4). The earlier "no ltlk exists" claim
came from runtime `GetResource` probes that ran before the System's
resource map was current.

Gates: `egret_test` extended (stream reads, host-terminated session,
WriteXPram round-trip); full suite green incl. `lcii_boot_etalon`.
Dev tooling: `lcii_trace` gained `TRAJ_AT` (retargetable jump-trajectory
dump), `WATCH_SP` (SysParam/PortBUse write watch), `WATCH_XPTRAP`
(_ReadXPRam/_WriteXPRam trap log).

## 2026-07-16 — O6.11: LocalTalk LAP — SCC abort stream + HLE watchdog

GISTPERSO's AppleTalk-active System runs the built-in `.MPP` LocalTalk LAP
(no `ltlk` ADEV exists in it — verified by disk scan and by every runtime
`GetResource('ltlk',0)` returning null; Basilisk's ltlk-resource patch is
therefore inapplicable, and its XPRAM `$E0-$E3` disable had no effect
because this System does not gate LocalTalk on those bytes). The LAP arms
an SDLC transaction on the SCC and its caller busy-waits at RAM `$A6540`
on a driver mutex (`$63e`) for a completion that never arrives. Two
changes, the first a real hardware fix, the second an HLE aid:

- **`Scc8530` streams the standing Break/Abort** (`Scc8530::tick`, wired
  into `V8Memory::tick`). Traced state: WR9 MIE set, WR15(B) Break/Abort
  IE armed, but a single latch — the driver services one abort, resets
  ext/status (clearing it), and waits for the next. On an open LocalTalk
  line the SDLC receiver keeps detecting aborts, so once IE+MIE are armed
  the ext/status interrupt must RE-present; the tick re-latches it every
  ~2000 cycles. This is correct 8530 behaviour and now delivers a stream
  of level-4 SCC interrupts (verified: 20+ taken vs 4-then-silent
  before); `scc_ext_test` green, Plus mouse path (`input_etalon`) green.
- **`V8Memory::localTalkWatchdog`** (HLE, `POM68K_NO_LTALK_WD` disables).
  The abort stream is necessary but not sufficient: the LAP completion is
  woven through the SCC ISR (which only resets the channel, ROM $A6C8E)
  and a Time-Manager timeout — three subsystems. Rather than emulate all
  of it, the watchdog recognises the wedged transaction (the LAP mutex,
  at the AppleTalk globals `*(*(ExpandMem)+$70)+$63e`, held ~0.5 s while
  the abort stream runs) and releases the mutex byte the caller spins on,
  so its retry loop runs down and `.MPP` moves on. This clears `$A6540`
  (dominant hot PC 100M→ gone; the LAP now only retries ~19×).

**Status: this advances GISTPERSO past the LocalTalk LAP but NOT to the
Finder.** Boot proceeds into 32-bit-mode System code and wedges again at
a new spot ($8009372A, a tight loop scanning unmapped memory — a
different subsystem, A2 base $B11B8 vs the LAP's $96AC8). The crude LAP
"give up" likely leaves AppleTalk half-initialised, so a downstream scan
runs off the end of a structure. Reaching the Finder needs either a
clean "AppleTalk inactive" (its System resists the XPRAM route) or real
LocalTalk/Time-Manager completion — tracked in TODO § O6.11. The
24 CTest gates stay green; boot.vhd (no AppleTalk) is unaffected (the
watchdog never arms without Break/Abort IE).

## 2026-07-15 — O6.9 resolved: GISTPERSO's vector-2 storm — RTE honors a cleared SSW.DF

- **The 6.8M-deep vector-2 storm was NOT the pipe-stage words.** The
  RAM routine at $1313E/$1315E is Mac OS's slot-probe recovery; its
  reads at ($e,A7)/($a,A7) sit above a pushed D0, so they are the
  **SSW** and the **format nibble**, not pipe stage B. Protocol
  (decoded with the new `lcii_trace --dasm`): probe `move.b (A1),D0`
  on $FCFFFFFF; on the $B bus-fault frame, RTE **with DF set** = retry
  the data cycle (budget 64); then `bclr #0` on the stacked SSW high
  byte (= clear DF) + RTE = "cycle done, complete the instruction with
  the frame's data input buffer". Moira's $B RTE discarded the frame
  and re-ran the instruction unconditionally → re-fault forever.
- **Fix (vendored Moira, POM68K_VENDOR.md § RTE $B honors a
  software-cleared SSW.DF)**: the RTE pops now capture SSW / fault
  address / data input buffer; the bit-9 "frame carried DF" marker
  (WinUAE encoding, already stacked by `mmuPageFault`) with bit 8
  cleared arms a one-shot latch that `mmuRead`/`mmuWrite` consume when
  the restarted instruction re-issues the exact faulted access: reads
  return the data input buffer, writes are skipped. The mode-5 WinUAE
  oracle zero-fills the pipe and retries *inside* its RTE step, so it
  cannot testify byte-for-byte — pinned instead by `berr030_test` § 5
  (the exact Mac probe pattern: retry once with DF set, then DF-cleared
  continuation). Result: 520 bus errors (4 probes × 64 retries × 2
  passes — the designed cost) instead of millions; boot proceeds to
  the French System 7 « Bienvenue » screen.
- **Storm gone, boot now reaches the graphical « Bienvenue » (Welcome)
  splash with progress bar** (screenshot; ~170 s of Mac time, VBL and
  Time Manager ticking, 856 SCSI selections). This is the O6.9 fix: the
  reported bug — GISTPERSO dying in a vector-2 storm — is resolved.
- **Beyond the storm: AppleTalk/LocalTalk (new, O6.11).** GISTPERSO's
  System has AppleTalk active, so it opens `.MPP` and drives the SCC at
  $50F04000. Two things were done here, and the exact state is worth
  recording precisely:
  - The V8 `Scc8530` replaces the earlier read-only SCC stub, and models
    an **open (peer-less) LocalTalk line**: RR0 carries a standing
    Break/Abort (bit 7), the correct hardware state. This is what moved
    the hang forward from the carrier-sense wait ($A5B28) to the
    transmit wait ($A6540) — the driver reads RR0 in *polled* mode and
    branches on the new value.
  - XPRAM $E0-$E3 = $00/$F1/$00/$0A ("network ≠ LocalTalk", Basilisk II
    `emul_op.cpp:129-144`) in `Egret::factoryDefaults()`.
  - SCC ext/status + Tx-Buffer-Empty interrupts were added and gated
    (`scc_ext_test`) — correct 8530 behaviour — **but the LC II drives
    the SCC purely polled: WR9 master-int-enable is never set, so no SCC
    interrupt is delivered.** The remaining hang ($A6540) is a `.MPP`
    transaction whose completion (`jmp ($634)` → clear the $63E mutex)
    is never reached; it is a Time-Manager-timeout / LocalTalk SDLC
    frame-completion path, not an interrupt. Fully modelling it needs
    LocalTalk SDLC transmit emulation (or Basilisk-style `.MPP` HLE) —
    there is no differential oracle for LocalTalk, so it is scoped as
    O6.11 rather than guessed. See TODO § O6.11.

## 2026-07-15 — Basilisk II knowledge applied: rominfo, XPRAM defaults

- **`tools/rominfo`** (new dev tool, no emulator core): Mac ROM
  introspection with the parsers pinned in the Basilisk II study —
  header + verified checksum, resource map, the compressed A-trap →
  ROM-offset table (`--trap A053` = breakpoint fodder for lcii_trace),
  UniversalInfo records, and the ROM+$94A decoder→low-mem pair table.
  Run on the real LC II ROM it settled two questions (pinned in
  `docs/BASILISK_ROM_NOTES.md` §8): the ROM **does carry a no-FPU SANE**
  (two `PACK 4` resources) — the O6 "error 10 without 68882" polish item
  is a selection problem, not a missing SANE; and the DecoderInfo
  hardware bases confirm the V8 map byte-for-byte (VIA $50F00000,
  pseudo-VIA $50F26000 → $CEC with real-VIA2 decoder[11] empty, SCSI
  triplet, ASC, SWIM).
- **`Egret::factoryDefaults()`**: when no battery file carries the
  system's `'NuMc'` XPRAM signature, seed Basilisk II's known-good
  defaults (DynWait, standard PRAM block, OSDefault=MacOS) instead of
  an all-zero PRAM; $8A intentionally not forced (V8 handles the real
  24-bit startup). Wired into the GUI's PRAM load; verified equivalent
  boot to 800M cycles (overlay, WarmStart, SCSI scan identical).
- `lcii_trace` now logs the **WarmStart `'WLSC'` milestone at $CFC**
  (the ROM's own "low memory is valid" marker, what Basilisk gates all
  host callbacks on) — brackets any fault as before/after low-mem
  validity; fires at ~332M cycles on the boot.vhd path.

## 2026-07-15 — O6: **Mac LC II boots to the Finder desktop**

- **POM68K boots a System disk (`hdv/boot.vhd`, volume "MacPack") all
  the way to the Finder** on the emulated V8 machine: menu bar, an open
  window of folders, desktop icons and Trash, "1.1 GB in disk / 223.3 MB
  available" — the whole classic Mac OS Toolbox running on the O6
  hardware (68030+PMMU, Egret, V8 RAM/video, ASC, SCSI). O6.8 gated by
  `tests/lcii_boot_etalon.cpp` (Finder signature + SCSI command count;
  soft-skips without the ROM/image; adds the $6A DDM entry in memory).
- The user's own disk `hdv/GISTPERSO.vhd` (wrapped bootable) *starts*
  booting — driver, partition map, blessed System Folder, System files
  loaded — but stalls in an exception storm at RAM $13160 (its System
  trips something boot.vhd's doesn't). Tracked as O6.9.
- **Two final gaps closed after the ?-icon stage:**
  1. **SCSI driver load** — the LC II ROM's boot scan ($A07264) only
     loads the driver whose DDM entry `ddType` is **$6A**; a bare-HFS or
     type-$0001-only image is silently rejected and the ROM keeps
     blinking the ?. Added `tools/wrap_hfs.py` to wrap a bare HFS volume
     into an Apple-partitioned disk with a DDM $6A driver entry, and
     modeled the **5380 IRQ latch** (BSR bit 4 sets on a DMA-phase change
     — enterStatus/MsgIn/BusFree; cleared by the RPI register) that the
     SCSI Manager polls to end a blind transfer. The Plus path is
     unchanged (its ROM ignores the latch), all 22 gates stay green.
  2. **68882 default-on** — the target software issues FPU instructions
     and a bare LC II faults with "system error 10" (Line-F). The LC II's
     68882 is a PDS option; POM68K attaches it by default (the O5 core),
     `POM68K_NOFPU` models a bare machine. This was the last thing
     between a rendered system-error dialog and the live Finder.

## 2026-07-15 — O6: the LC II ROM boots to the blinking-? screen

- **Milestone**: the real, unpatched LC II ROM ($35C28F5F) now completes
  its entire ROM phase on the emulated V8 machine — POST, boot chime,
  32-bit switch through the PMMU, low-memory + trap-table setup,
  QuickDraw init — and paints the gray desktop with the mouse cursor
  and the blinking-? boot icon while scanning SCSI (hundreds of
  selections, READ(6) traffic against the attached .vhd).
- **The blocker was Egret command $02 = ReadXPram(count, offset)** — an
  Egret-specific command absent from the Cuda documentation. The ROM's
  `_ReadXPRam` ($A0CC64) loads the boot-mode flag byte from XPRAM $8A
  into low-mem $1EFC; with no reply data, $1EFC kept the $FFFFFFFF
  low-memory fill, its bit 4 selected _InitZone on a diagnostic
  zone-at-zero pblock ($A00518), the zone-header clear wiped exception
  vectors 0-12, and the very next OS trap ($A02D) dispatched through a
  zeroed vector 10 into address 0. Found by walking the chain backwards
  with lcii_trace (vector watch → InitZone pblock → $1EFC → _ReadXPRam
  → the unanswered Egret exchange). Independently confirmed by the
  Basilisk II study (docs/BASILISK_ROM_NOTES.md): Basilisk stubs the
  same service (`_ClkNoMem`) and forces XPRAM $8A itself.
- lcii_trace grew the debugging instruments that made this tractable:
  --ring-from instruction rings with register columns, --probe register
  dumps at a pc, --hot-from tail histograms, automatic MMU table walks
  and root-table dumps on unexpected faults, low-mem watches, PPM
  screenshots, PRAM persistence.

## 2026-07-15 — O6 (LC II machine): first six slices

- **New machine core**: `V8Memory` (address map masked $80FFFFFF, RAM
  config register with the fixed $800000 2 MB alias, overlay cleared by
  any $A00000 read, bus errors on unmapped I/O — the ROM's AddrMapFlags
  probe reproduces the real-hardware $773F), `PseudoVia`, `Cpu030`,
  `Egret` + `AdbBus`, `AscV8`, `V8Video` + `Ariel`, SCSI pseudo-DMA
  windows over the Plus `Ncr5380`, SWIM1 shim over the Plus `Iwm`.
  Six new CTest gates (20 total green).
- **Moira vendored patches driven by the real ROM** (all documented in
  POM68K_VENDOR.md): external `extBusError()` producing exact $A/$B
  frames; RTE of format $A; **prefetch-pipe carry across PMOVE to
  TC/CRP/SRP** — the ROM enables the MMU with `pmove tc; nop; bne;
  jmp (A5)` and those three pipe words must execute under the
  pre-switch mapping (start-of-instruction refetch read RAM garbage and
  sank the boot into the POST debug console); **POLL_IPL at mode-5
  instruction boundaries** — the ROM's TimeDBRA calibration parks in a
  data-access-free `dbra` loop waiting for a VIA1 T2 level-1 interrupt,
  and without per-instruction IPL sampling the interrupt landed after
  the loop timed out, double-storing a result and derailing the table-
  driven dispatch. Neither divergence is visible to single-instruction
  differential fuzzing — machine-level etalons are the only net that
  catches this class.
- **Egret wire protocol decoded from the ROM itself** (no public doc):
  replies are `[sync, status, status, cmdEcho, data…]`, XCVR_SESSION
  drops WITH the last byte, next-byte trigger is the VIA_FULL falling
  edge, PRAM addresses are 16-bit, periodic TIMER packets (first one
  after reset is a 10-byte boot heartbeat, later ticks 3 bytes), and an
  unacknowledged Egret-initiated packet must be retracted or the ROM's
  bus-quiet waits deadlock. Pinned in `src/Egret.cpp` comments +
  `egret_test`.

## 2026-07-15 — O5 follow-ups: 68882 timing + FRESTORE frame acceptance

- **FPU instruction timing** replaces the `CYCLES_68020(6/20)`
  placeholders in `extern/moira/Moira/MoiraExecFPU_cpp.h` with the
  MC68881/MC68882UM Section 8 figures (68882 column), kept in a single
  table/section: Table 8-3 per-opmode totals + per-format spread +
  FMOVE-out row + FMOVECR, Table 8-6 control/FMOVEM (cache case, +2
  MC68882 footnote), Table 8-7 conditionals, Table 8-8 FSAVE/FRESTORE
  per frame. EA cycles reuse the integer `cp` mechanism unchanged
  (`computeEA` accumulates, `CYCLES_68020` adds); 68000/68010 timing
  untouched. **Why**: cycles are advisory in Phase 2 (SST030 `length`
  is not compared) but `emuCycles` drives event ordering — a 570-cycle
  FTWOTOX billed as 20 would skew every interrupt/VBL interleave on the
  future LC II. Two exact timing smokes in `fpu_sanity` (FADD.X 56,
  FMOVECR 32).
- **FSAVE BUSY frames: decided, not implemented** — WinUAE's 6888x
  support never generates them (no mid-instruction save window in its
  coprocessor model) and the oracle is the convergence target, so FSAVE
  stays NULL/IDLE-only (documented in `execFSave` +
  POM68K_VENDOR.md § FPU).
- **FRESTORE accepts every documented frame exactly like WinUAE**
  (`fpuop_restore`, fpp.c:2593-2812): NULL $00, 68881 IDLE $1F18,
  68882 IDLE $1F38 (BIU bit 27 clear re-arms a pending exception),
  BUSY $1FB4/$1FD4 skipped, **and the $41 68040 version-hack frames**
  (fpp.c:2799: active because the oracle runs
  `fpu_no_unimplemented = false` — $41 idle/unimp/busy are accepted or
  skipped, $40 and everything else is a format error, vector 14).
  **Why the hack is replicated**: oracle wins over spec; MC68882UM § 6
  knows nothing of it, but fuzzed $41 frames hit it in WinUAE.
- `oracle/fuzz/gen030.py` plants a well-formed frame image at ~60 % of
  FRESTORE operand addresses (all matrix rows, plausible IDLE
  internals, CU_SAVEPC kept off $FE so WinUAE's unimplemented
  68040-busy resume never triggers). Fresh-seed verify (29/31, n=200,
  fpu × off/identity, WinUAE-solo): **800/800 at first replay**,
  93 FRESTOREs covering the whole matrix. Gates unchanged: ctest
  15/15, sst68030 2 082/2 082, sst68000 1 000 058/1 000 058.
  Still open from the O5 list: FMOVEM indirect-EA read order.
- **Post-retirement repin — gate now 3 082/3 082** (12 corpus files):
  the duo/solo split merged into the standard names (`fpu_off` 1 099,
  `fpu_identity` 732 — duo seeds 1/7 + solo 11/17 + frame-planted
  29/31), and `fpu_tt` grew 11 → **211** with the first full
  FPU-through-translation solo cell (seed 41, 200/200 on first
  replay — the O5 fault-class fixes hold under `tt` too).

## 2026-07-15 — Musashi oracle retired: the loop is WinUAE-solo

- The second oracle (`oracle/musashi/`, MAME m68kmmu on kstenerud
  Musashi 4.60) is **deleted**, along with its build dir and every
  loop/fuzzer reference. **Why**: across all arbitrations D1-D22 it won
  **zero** rulings — WinUAE (+ manual) took every contested call; its
  fault model is architecturally divergent (D8/D9: faulted instructions
  run to completion, zero-filled $A/$B frames) and blind to 68030
  address errors (D11); its 6888x manages ~13 % agreement (D18-D20)
  and cannot testify on FSAVE/FRESTORE, packed decimal, FDBcc/FTRAPcc
  or FMOVEM-fp (D22). Patching it to each ruling cost more than its
  testimony was worth — it was no longer up to standard.
- The differential loop becomes **WinUAE-solo + manual arbitration**:
  solo cells mean "the oracle's word is law"; Moira-vs-oracle disputes
  are settled by MC68030UM / MC68881-882UM readings under the standing
  oracle-wins-over-spec policy (real-hardware traces welcome). Ruling
  appended as `oracle/fuzz/disputes/NOTES.md § Musashi retired`
  (D1-D22 history untouched).
- `loop.sh` now builds one oracle and fuzzes a single grid
  (`{core,mmu,random,fpu} × {off,identity,tt}` + `fault ×
  {identity,tt}`) into the standard `${family}_${mmu}.json` names; the
  duo/solo corpus split (`fpusolo_*`) merges away on the next repin.
  `fuzz030.py` keeps the generic `--b` slot for a future second oracle
  but drops `ruled_for_a` (standing-ruling auto-arbitration only made
  sense against a known-deficient B). Pinned corpora untouched: ctest
  15/15, sst68030 2 082/2 082, sst68000 1 000 058/1 000 058.

## 2026-07-15 — O5 slice 2: 68882 FPU execution in Moira

- **Moira executes the full MC68882 instruction set** (the LC II PDS
  FPU), backed by a new vendored `extern/softfloat/` (SoftFloat-2a +
  Previous/WinUAE FPSP transcendentals, copied from the oracle vendor
  tree — GPLv2+, linked as a separate static lib so Moira stays MIT).
  **Why this softfloat**: it is the exact arithmetic the primary oracle
  (WinUAE) runs, and the oracle wins disputes by project policy — so
  numerical convergence with the differential corpus holds by
  construction. Semantics ported from WinUAE `fpp.c`/`fpp_softfloat.c`
  6888x branches with line citations (constant-ROM garbage entries,
  alias opmodes, packed-decimal k-factor quirks, the FPIAR
  only-when-enabled economy, the 68882 NULL-frame word `$00380000`, the
  68030-build FSAVE that *skips* the internal longs — all oracle-exact
  on purpose).
- Attach/detach follows the CPU-model mechanism: `setFPUModel()`
  rebuilds the jump table; `FPUModel::NONE` (default) keeps it
  byte-identical to stock Moira. **Nothing moved on the gates**:
  sst68030 1 040/1 040 (FPU-less corpus), sst68000
  1 000 058/1 000 058, Mac Plus etalons green.
- `tests/sst68030.cpp` learned the SST030 FP fields (`fp0`-`fp7` as
  3×u32 raw words — fixed contract with the fuzzer — plus
  `fpcr/fpsr/fpiar`) and attaches the 68882 per vector. New gate
  `fpu_sanity` (hand-computed FMOVECR pi, 2+2, 1/0=+inf DZ,
  sqrt(-1)=NaN OPERR, FCMP/FScc orderings, FMOVEM raw image, packed
  1.0, FPCR roundtrip, detached F-line) — ctest now 15/15. The first
  parallel-generated FPU corpus replays 41/41.
- Known-incomplete for the loop (documented in POM68K_VENDOR.md § FPU):
  FSAVE BUSY frames, FPU timing, FMOVEM indirect-EA read order.
- **Same-day solo-corpus convergence** (WinUAE-solo FPU corpus, 700
  vectors, 617→700 after three fixes; duo corpora 41/41 + 90/90 and all
  hard gates unchanged): (1) state-restore convention — loading FPU
  state through the setters leaves the FPU non-null, so a subsequent
  FSAVE emits the IDLE frame like the oracle glue's forced
  `fpu_state = 1`; (2) ruling **D21** — FRESTORE bad-frame format error
  (vector 14) stacks the PC past all consumed words (WinUAE
  `m68k_getpc()`), not Moira's generic `reg.pc - 2`; (3)
  post-instruction FP traps stack the **format $3** floating-point
  frame (next PC + operand fp_ea, newcpu_common.c:1616) instead of a
  format $0 stub.
- **Fresh-seed re-verify (seeds 17/19) closed two FPU-through-MMU fault
  classes**: FMOVEM FP-block transfers are *unlogged* with integer-MOVEM
  bookkeeping (MOVEM1/FMOVEM flags, state[0] long counter, fmovem_store
  in the $B frame's padding slots — WinUAE uses the non-state accessors
  there, fpp.c:2810/2875), and FPU (An)± operands arm *plain* mmufixups
  (register restored on a fault, status byte 0 — bit 7 of
  `mmuFixupReg[]`). Solo seeds 11/17/19/23: 700/700, 211/211, 100/100,
  100/100; full ctest 15/15.
- **O5 closed — gate re-pinned at 2 082/2 082** (14 corpus files): duo
  `fpu_off` 99 (seeds 1/7), `fpu_identity` 32 (1/7), `fpu_tt` 11 (13),
  plus WinUAE-solo `fpusolo_off` 600 / `fpusolo_identity` 300 (seeds
  11/17) under **ruling D22** — the slice-1 solo proposals
  (FSAVE/FRESTORE frames, packed decimal, FDBcc/FTRAPcc, FMOVEM-fp)
  promoted to a pinned solo class, same status as the D9 fault family.
  **Why solo**: Musashi's 6888x cannot testify on those classes (~13 %
  duo agreement, D18-D20); every future Musashi convergence patch grows
  the duo side and shrinks the solo residue. `loop.sh` now fuzzes fpu
  in the duo grid (off/identity/tt) + solo cells; the pending dir is
  retired.

## 2026-07-15 — O4 slice 4: integer-family arbitration (O4 complete)

- The last integer disagreements between the two 68030 oracles were
  swept (3 000 duo states → 88 disputes), categorized by opcode, probed
  case-by-case, and arbitrated: **WinUAE won every ruling** (D11-D17 +
  the D6-remainder, `oracle/fuzz/disputes/NOTES.md` § slice 4) — its
  undefined-flag tables in `newcpu_common.c` are hardware-verified.
  Musashi was patched to the rulings (12 BCD bodies, DIV/CHK/CHK2
  tables, PACK/UNPK byte order, reserved I/IS=100, F-line priv windows,
  format-$2 next-PC, `oracle/musashi/VENDOR.md`); Moira's 020 paths now
  route to its existing UAE-derived helpers instead of the SST-68000
  rules — gated `if constexpr (C68020)` like D10, so the 68000 core is
  bit-identical (sst68000 still 1 000 058/1 000 058).
- **Why two non-obvious fixes existed at all**: Moira's imported copy
  of WinUAE's CHK table had silently dropped `SET_NFLG(dst < 0)` —
  only fresh-seed replay caught it; and Musashi needed **`-fwrapv`**
  because the (deliberately overflowing) CHK bound-subtractions were
  being folded by GCC into `bound < val` while the WinUAE oracle
  already built with `-fwrapv` — same C code, different .so truth.
- **D11 — the Musashi address-error gap became a standing ruling**:
  odd control-flow targets raise vector 3 with a real format $B frame
  on a 68030 (SSW $0066, per-instruction stacked-PC conventions,
  access log in the internal words — all probed); Musashi cannot model
  it, so `fuzz030.py` now auto-arbitrates that signature to WinUAE
  (`ruled_for_a`, D9 precedent) and the vectors enter the corpora.
  Moira replays them byte-for-byte: odd-target checks in nine handlers
  + `execAddressError030`, with WinUAE's quirks kept (BSR decrements A7
  without writing, DBcc faults even on an expired counter, JSR defers
  the fault to the next fetch). The M68030 read/write funnel is now
  taken with TC.E off too — WinUAE's `_state` accessors always log,
  and those logs are what the $B frames stack.
- The Musashi 68881 is **disabled on the 030** (FPU-less LC II, like
  the WinUAE `fpu_model = 0` build): the whole $F2xx/$F3xx space is
  duo-agreed as Line-F/priv until the O5 FPU slice re-enables both
  sides; `move16` and 68040-PFLUSH no longer leak onto the 030.
- Result: **the `--mmu off` grid is fully converged** (core/mmu/random
  100 % over fresh seeds; random was ≈94 % before the slice);
  identity/tt at 96-100 % with the residue fully explained (D8/D9
  Musashi fault-model gap, covered by the WinUAE-solo fault corpora).
  Moira replays 100 % of every agreed vector (2 672/2 672 grid +
  2 000/2 000 sweep). Gates re-pinned: `random_off.json` 250,
  `random_identity.json` 121 (fresh seeds 81/91, D11 vectors included)
  → `ctest -R sst68030` = **1 040/1 040**; full ctest 14/14. **O4 is
  complete** — next: O5 (FPU) per TODO § Phase 2.

## 2026-07-15 — O4 slice 3: the 68030 MMU bus layer (Moira translates)

- Moira now translates **every bus access** when `Model::M68030` and
  TC.E=1 (MC68030UM § 9.5, modeled on the primary oracle WinUAE
  `cpummu030.c`): transparent-translation match, 22-entry ATC with
  pseudo-LRU replacement and the *write-to-unmodified-page invalidates*
  rule, a full table search (FCL, short/long descriptors, limits, early
  termination, indirection, U/M history writes), and 68030 bus
  splitting for unaligned accesses — each sub-access translated
  separately, so a long can straddle a good and a bad page and commit
  its first half. PFLUSH/PFLUSHA/PLOAD/PMOVE-with-FD-clear now flush a
  REAL ATC; PTEST level 0 searches it. Hook cost outside the LC II
  path: `if constexpr` — the 68000/68010 template instantiations are
  bit-identical in behaviour (sst68000 still 1 000 058/1 000 058
  cycle-exact, all Mac Plus boot etalons green).
- Translation faults raise vector-2 bus errors with **byte-for-byte
  WinUAE format $A/$B frames** (the fuzzer compares raw RAM): $A on a
  fault at the instruction's last write (next-instruction PC, updated
  CCR, (An)± kept), $B otherwise (instruction PC, CCR + pending-fixup
  restore, access-value log, wb2/wb3 fixup encodings, MOVEM counter,
  SUBACCESS flags, disp-store words; prefetch-phase faults flag bit 31
  of the pipeline-status long). Double fault (odd vector 2 / fault
  while stacking) → HALT.
- **Why the fault corpus is WinUAE-solo (ruling D9)**: Musashi runs
  faulted instructions to completion and pushes zero-filled frames —
  architecturally incapable without a rewrite, so every fault vector
  self-quarantines in the duo differ. Majority rule (WinUAE + manual):
  the new `--family fault` corpora (`fault_{identity,tt}.json`, aimed
  at invalid/WP/remapped pages, unaligned page-straddles, MOVEM, MOVES,
  TAS/CAS locked-RMW) are generated from WinUAE alone. Two oracle
  determinism bugs were fixed on the way (VENDOR patches 5-6): the
  setjmp-clobbered CCR capture (`volatile`) and stale restart globals
  leaking history into frames (`oracle_set_state` zeroes them).
- Also arbitrated: **D10** — the SST-68000 "ASR past width clears C/X"
  rule is 68000/68010-only (both 68030 oracles keep the sign as C/X);
  MOVES joined the `core` fuzz family (SFC/DFC-driven translation).
- Gates: `ctest -R sst68030` = **875/875** across 9 corpora (mmu-off
  520, duo-agreed identity/tt 250, fault-solo 105); the harness replays
  translation-enabled vectors first-class (`--skip-translation` kept
  for debugging). Scratch sweep: 2 342/2 342 duo-agreed vectors across
  3 families × identity/tt × 3 seeds. Full ctest: 14/14.

## 2026-07-15 — Phase 2 live: two 68030 oracles + arbitration turn 1

- The differential loop of TODO § Phase 2 exists end-to-end: WinUAE
  (Hatari e77819f7, `oracle/uae/`) and MAME-Musashi (m68kmmu 0.276 on
  kstenerud 4.60, `oracle/musashi/`) behind one C ABI
  (`oracle/oracle_api.h`), fuzzed by `oracle/fuzz/` with real MMU
  translation trees in RAM, exchanged as SST030 JSON, replayed against
  Moira by `tests/sst68030` (gate 14). `oracle/fuzz/loop.sh` = one turn.
- Arbitration turn 1 (D1-D5 + bonuses): **WinUAE won every ruling** —
  MMU ops are privileged with the S-check *before* the extension fetch;
  PMOVE MMUSR,Dn is an invalid EA → Line-F (the D2 replace-vs-merge
  question was unreachable on real hardware); long-indirect descriptors
  read the second long at +4 (Musashi also had an unmasked-shift bug that
  killed indirection except at TID); DT=0 walks keep accumulated MMUSR
  bits; the vector-56 frame is format $0 with next-PC; MOVEM list,-(An)
  with the base register in the list stores initial−size (020+ PRM).
  Musashi oracle and Moira both fixed; the losing behaviours are
  catalogued in `oracle/musashi/VENDOR.md` and
  `extern/moira/POM68K_VENDOR.md`, rulings in
  `oracle/fuzz/disputes/NOTES.md § Arbitrated`.
- State: oracle agreement 200/200 (core and mmu families, MMU off);
  Moira replays 100 % of every duo-agreed corpus (520 gated vectors +
  fresh-seed 300/300 spot-check). Translation-enabled vectors stay
  mmu-skipped until the bus/ATC slice.

## 2026-07-15 — O4 slice 1: Moira executes the 68030 MMU instructions

- PMOVE/PTEST/PFLUSH/PFLUSHA/PLOAD execute for real (Model::M68030):
  crp/srp/tc/tt0/tt1/mmusr live in `Registers` with getVBR-style
  accessors, PMOVE moves all of them (TC validation → vector-56 MMU
  configuration error, format-2 frame), PTEST performs the § 9.5.3
  translation-table walk into MMUSR (+ descriptor address → An), PLOAD
  walks with U/M history writes to RAM, PFLUSH* are no-ops (no ATC yet).
  Verified by the O3 differential loop against the Musashi oracle:
  100 % on `family=mmu --mmu off` (2 900+ vectors, 8 seeds); gate
  corpus `tests/data/sst68030/mmu_off.json` (200 vectors) wired into
  `ctest -R sst68030`, which now loads/compares the MMU registers and
  only skips vectors whose *initial* TC has E set (bus translation is
  the next slice). Harness parser fix: `"length":-1` (oracle_step < 0
  in translation-enabled corpora) used to livelock the hand-rolled JSON
  scanner; it now accepts negative literals.
- The non-obvious part: the oracle contradicts MC68030UM in several
  places and, per the project rule, wins — most notably **Musashi never
  privilege-checks MMU instructions** (user-mode PMOVE executes; ~30 %
  of the corpus), MMUSR→Dn replaces the whole register, and an
  invalid-descriptor walk erases previously accumulated MMUSR bits. All
  logged as D1-D7 in `oracle/fuzz/disputes/NOTES.md` for re-arbitration
  when the WinUAE oracle lands. Vendor catalogue updated
  (`extern/moira/POM68K_VENDOR.md` § 68030 MMU-instruction convergence).

## 2026-07-15 — M6: the startup chime plays

- Sound: `MacAudio` pulls the 370-sample/frame PWM buffer (ramTop−$300,
  even byte = 8-bit sample, PA3 buffer-select, PB7 enable, PA2-0 volume);
  `MacAudioHost` plays it through miniaudio on a lock-free ring at
  22254 Hz. The turbo-vs-audio pitch problem is solved by pushing only
  non-silent frames — the ring drains while the machine races through the
  silent RAM test, so the chime and beeps keep the right pitch.
- The Mac Plus power-on chime — a ~601 Hz (≈D5) tone held ~0.7 s then
  muted — is audible. Verified headless by capturing it to a WAV and
  checking the fundamental + decay (`sound_test`).

## 2026-07-15 — M7: System 6 boots from a SCSI hard disk

- NCR 5380 controller + SCSI-1 target boot System 6 from HD20SC.vhd; the
  HD20SC volume mounts on the Finder desktop.
- The day-long blocker was NOT the controller (proven correct in isolation
  by ncr5380_test from the start) but the ROM never running its SCSI scan.
  Diagnosis chain: PRAM (ruled out — Plus ignores the default-boot-device,
  that's a 256K-ROM feature) → floppy presence (ruled out) → drive queue
  had only the floppy → SCSI Manager select/read primitives never executed
  → the gate is `HWCfgFlags` ($0B22) bit 7, set by `E_SoftReset`'s
  $420000-vs-$440000 ROM-mirror probe. We mirrored the ROM across the whole
  window so the probe saw no difference and declared "no SCSI". Fixed by
  returning address-dependent open bus above the true 128 KB ROM. (Nailed
  via the bit-exact Plus v3 ROM disassembly, jonathanschilling/mac_rom.)
- Second blocker: WRITE(6/10) is mandatory — the driver writes to the
  volume during mount; a read-only target hung the boot in a VIA interrupt
  storm right after the driver loaded. Added a DATA OUT phase to the
  controller and in-memory writes to the target.
- Also: GUI windows move only from the title bar (Finder drag-and-drop no
  longer drags the host window); floppy/SCSI paths resolve relative to the
  executable; SCSI disk auto-attaches from hdv/.

## 2026-07-14 — M5.5: the Finder is drivable (keyboard + mouse)

- Minimal SCC Z8530 (DCD ext/status interrupts, RR2B modified vector —
  the ROM's actual dispatch mechanism; it never reads RR3), quadrature
  mouse with the exact polarity table, M0110A keyboard with the
  two-SR-interrupt transaction (~3 ms per phase).
- The vicious one: with keyboard AND mouse alive, a naive IPL OR yields
  level 3, whose ROM vector is a bare RTE → instant livelock. The real
  glue disconnects the VIA /IPL0 while the SCC interrupts (GttMFH); the
  suppression formula fixed it in one line. Diagnosed by single-stepping
  at storm onset: the "handler" was just `rte` + the interrupted
  instruction alternating.
- Gate `input_etalon` verifies against System 6's own understanding:
  RawMouse deltas (±2 for inherent quadrature reversal loss), MBState,
  KeyMap bits. Bonus verification: headless click on the File menu —
  it drops with all items rendered.
- GUI: mouse captured over the Mac screen (2x scale compensated),
  55-key M0110 map, Cmd = Super, Option = Alt.

## 2026-07-14 — M5: System 6.0.5 boots to the Finder from floppy

- IWM + Sony 800K drive from the cross-verified research spec (MAME, pce,
  Snow). GCR checksum ported verbatim from MAME and cross-validated
  against pce's independent formulation before use.
- Three bugs found by tracing the ROM's Sony driver instruction by
  instruction (`sony_trace`, new dev tool):
  1. TACH must run on motor time, not data position — the ROM times
     spindle speed against VIA T2 before ever reading data.
  2. The IWM data register clears ~14 clocks AFTER a read, not
     immediately — the ROM's `tst.b`/`move.b` pairs read it twice.
  3. Boot blocks need the bbVersion word ($4418) at +6 and a BRA at
     bbEntry (+2); 'LK' alone is not enough.
- Verification chain, cheapest first: gcr_test (encoder roundtrip vs an
  independently-ported decoder) → disk_boot_etalon (synthetic boot block
  executes our 68000 code through the whole floppy path, no Apple bits
  needed) → system_boot_etalon (real System 6.0.5 to the Finder desktop,
  2.7 s headless).
- GUI probes disks35/ for a floppy image; boot_trace grew --disk.

## 2026-07-14 — M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060

- `sst68000` harness (POM2/POMIIGS JSON-scanner pattern) runs the full
  1M-vector corpus in 3.7 s; `--dump` prints complete state diffs (the
  workhorse of the convergence loop); `--only/--skip/--max` for triage.
- Starting point was 81.1% (all non-address-error vectors already passed —
  Moira's core semantics were sound). The 19% gap was almost entirely the
  fine detail of 68000 address-error behavior: exception idle cycles,
  stacked-PC values per instruction class, An update ordering around
  faults, frame FC/IN bits, and MOVE's interim flags. Full patch catalogue
  in `extern/moira/POM68K_VENDOR.md § convergence`.
- Notable finds: branches/jumps/returns all stack PC = target−4 with the
  I/N bit set; BSR pushes its return address before faulting but JSR does
  not; ADDX/SUBX leave a faulting -(An).l at init−2 while plain reads keep
  the full −4; DIV/CHK undefined flags follow the preserve rules verified
  empirically by a research agent against the whole corpus (0 mismatches).
- Two ASL.b vectors are upstream-documented bad data (SST issue #4 — our
  computed values match the proposed corrections); skipped with citation.
- Two SST rules conflict with hardware literature (DIVS late-overflow
  timing vs ijor's paper; ASR over-shift C/X) — applied per the
  oracle-wins rule and flagged for oracle #2 in the LC II phase.
- Methodology note: this was the phase-2 oracle loop run manually — dump
  divergence, derive the rule from the corpus (python analysis over the
  JSON), patch, re-run. The 68030 phase will automate exactly this.

## 2026-07-14 — M4 complete: cycle-accurate boot hardware

- VIA timers T1/T2 count at φ2 = CPU/10, driven from `Cpu68k::sync()` via a
  peripheral catch-up (`lastPeriphClock_`) so contention wait states tick
  them too. One-shot/free-run semantics; ±1-cycle 6522 reload latency
  deferred (TODO M4.1).
- RTC 343-0042 bit-banged serial protocol (PB2 /enable, PB1 clock, PB0
  data), 20-byte PRAM, write-protect register, seconds counter on the CA2
  one-second tick. **Fixed the "2 MB" mystery**: with a working RTC the ROM
  now stores MemTop = $400000 — the earlier $1FFBE0 SSP was a mid-boot
  stack, not a sizing result.
- RAM/video contention (`Cpu68k::contentionDelay`): video owns alternate
  4-cycle slots during the 512 visible dots of lines 0-341, sound/PWM fetch
  steals the last 4 cycles of every line. The delay iterates across
  adjacent busy slots (a wait can land in the next line's video slot — the
  first version under-counted by 4 cycles × 341 lines). Gate reproduces
  GttMFH Table 5-3: 2.56 MB/s average RAM bandwidth, 21 246 accesses/frame.
- cpu_smoke hardened: with contention the 1M-cycle run stops mid-repaint,
  so the diagonal invariant is checked on 4 row pairs (3 must hold) instead
  of assuming a completed frame.
- `MacFrame.h`: shared frame clock — VBL (CA1) at line 342 = cycle 120 384,
  one-second every 60 frames; GUI, boot_trace, etalon all agree on phase.
- New gates: `contention_test` (budget math), `rom_boot_etalon` (real ROM →
  gray desktop 50% ± icon patch white ratio; soft-skips without a ROM).
- GUI: Turbo ×8 checkbox (default on with a real ROM — the 4 MB RAM test
  takes 45 s of machine time).

<a id="2026-07-14-m0-m35-first-rom-boot"></a>
## 2026-07-14 — M0–M3.5 + first real-ROM boot

- Project scaffolded on the POMIIGS blueprint; Moira vendored from NeoST
  (`extern/moira/POM68K_VENDOR.md`).
- Built-in demo ROM gate: caught that the 6522 port A reads inputs with
  pull-ups (`ora | ~ddra`) — code must set DDRA before ORA to clear the
  overlay, exactly like the real ROM (DDRA=$7F, then ORA with PA4=0).
- Web-research report (MAME/pce/Mini vMac/GttMFH cross-checked) pinned in
  `DEV.md`; fixed SCC read/write split ($8/$9 even vs $A/$B odd), IWM range
  $C-$D with stub reads of `$1F` (required to reach the blinking-?), VIA PA6
  screen-buffer select.
- Level-sensitive IPL: recomputed after **every** VIA access (reads clear
  IFR flags too) — without this the first serviced VBL re-interrupts forever.
- **Mac Plus ROM v3 boots to the blinking-? floppy icon**: RAM test runs
  ~45 s of machine time on 4 MB (real hardware does the same — don't
  mistake the `movem.l`/`eor` loop at `$400E82` for a hang), then gray
  desktop + mouse pointer + ?-icon; VBL IRQ drives the blink counter wait
  at `$402420`. No VIA timers or RTC needed to get here, confirming the
  BMOW Plus Too minimal-hardware list.
