# TODO

## Milestone roadmap

### Phase 1 — Mac Plus (68000, cycle-exact)

- [x] **M0 — Foundation**: CMake, docs, Moira vendored from NeoST
  (`extern/moira/POM68K_VENDOR.md`), ImGui frontend shell.
- [x] **M1 — CPU**: `Cpu68k` Moira wrapper (M68000, PRECISE_TIMING, address
  errors). Gate: `cpu_smoke`.
- [x] **M2 — Memory map**: RAM/ROM/overlay/VIA decode, SCC/IWM/SCSI stubs.
  Gate: `cpu_smoke` (overlay path).
- [x] **M3 — Video**: 512×342 framebuffer → ImGui texture; built-in 68000
  demo animates diagonal stripes. Gate: `demo_screenshot`.
- [x] **M3.5 — Map pinning**: web-research report applied (SCC read/write
  split $8/$9 vs $A/$B, IWM $C-$D returning $1F, VIA PA6 screen select,
  DDRA=$7F) and persisted in `DEV.md` with citations.
- [ ] **M3.6 — boot_trace dev tool** (POMIIGS pattern): trace PC, detect
  hangs, dump the framebuffer as text — the microscope for M4.
- [x] **M4 — Real ROM boot** (2026-07-14): blinking-? with ROM v3
  `$4D1F8172`, **MemTop = $400000 (4 MB correctly sized once the RTC
  existed)**. VIA timers T1/T2 @ 783.36 kHz ticked from `sync()`; RTC/PRAM
  serial protocol (PB0-2) + CA2 one-second; RAM/video contention with exact
  GttMFH budget (2.56 MB/s — gate `contention_test`); PB6 hblank derived
  from the beam; VBL raised at line 342 phase (`MacFrame.h`); GUI turbo ×8.
  Gates: `contention_test`, `rom_boot_etalon` (soft-skips without ROM).
- [ ] **M4.1 — deferred accuracy items**: 6522 T1/T2 ±1-cycle reload
  latency; VIA E-clock (/VPA) access synchronization + IACK E-cycles
  (NeoST `iackSyncBefore/After` pattern); PRAM file persistence; seed RTC
  seconds from host clock in the GUI (kept deterministic in tests).
- [x] **M4.5 — SingleStepTests 68000** (2026-07-14): `sst68000` harness
  (bare Moira, flat 16 MB bus) — registers, SR, USP/SSP, PC + prefetch
  queue, RAM, exact cycles. **1 000 058 / 1 000 060 pass** (2 = upstream
  bad data, SST issue #4, skipped with citation). ~25 Moira patches were
  needed, all catalogued in `extern/moira/POM68K_VENDOR.md § convergence`;
  two SST rules are flagged for cross-checking against oracle #2 in phase 2
  (DIVS late-overflow timing, ASR over-shift C/X). Gate: `sst68000` (3.7 s,
  soft-skips without the 1 GB corpus — `tests/fetch_sst_68000.sh`).
  **This JSON format is the exchange format for the 68030 oracle phase.**
- [x] **M5 — IWM + Sony 3.5" GCR drive** (2026-07-14): **System 6.0.5
  boots to the Finder** through the full chain — IWM register model,
  800K drive (sense/commands, time-based TACH, instant steps), GCR
  encoder (MAME-ported, pce-cross-validated), raw .dsk + DiskCopy 4.2.
  Gates: `gcr_test` (roundtrip), `disk_boot_etalon` (self-contained
  synthetic boot block), `system_boot_etalon` (real System 6 → Finder,
  soft-skips). Read-only for now.
- [x] **M5.5 — Input** (2026-07-14): M0110A keyboard (two-interrupt SR
  transaction) + quadrature mouse (SCC DCD + RR2B modified vector) +
  button; correct IPL suppression (level 3 never occurs). The Finder is
  drivable: menus open, cursor tracks 1:1. Gate: `input_etalon` (verified
  against System 6's RawMouse/MBState/KeyMap globals).
- [ ] **M5.6 — leftovers**: floppy write support, external drive,
  eject/insert UI, keypad/arrow two-byte codes ($79 prefix).
- [x] **M6 — Sound** (2026-07-15): PWM sample buffer → `MacAudio` extractor
  → `MacAudioHost` (miniaudio, non-silent-frame push so turbo survives).
  **The iconic startup chime plays** — a clean ~601 Hz tone captured to
  `chime.wav`. Gate: `sound_test`. Remaining: sound-buffer cycle accuracy
  (per-scanline fetch vs whole-frame), the disk-PWM byte, volume-curve
  fidelity.
- [x] **M7 — SCSI NCR 5380 + hard disk** (2026-07-15): **System 6 boots
  from HD20SC.vhd to the Finder** (the HD20SC volume icon appears on the
  desktop). Controller (arbitration-free selection, all phases, pseudo-DMA)
  + SCSI-1 target (read + in-memory write). The blocker was the ROM's
  $420000/$440000 SCSI-presence probe defeated by full-window ROM
  mirroring — fixed with address-dependent open bus above the 128 KB ROM.
  Gates: `scsi_disk_test`, `ncr5380_test`, `scsi_boot_etalon`.
- [ ] **M7.1 — SCSI polish**: ~~persist writes to the backing file~~
  (done 2026-07-16: write-through, GUI-only, `ScsiDisk::open(path,
  writeBack)`), multiple targets/LUNs, REQUEST SENSE after CHECK, the
  SCC serial ports (port POMIIGS `Scc8530`) for real.
- [ ] **M8 — Etalons**: pixel-perfect screenshot regression suite
  (NeoST `run_etalons.py` pattern); WASM build.

### Phase 2 — Mac LC II (68030 + MMU, functional accuracy)

The 68030 execution core does not exist in Moira (disassembler only). Build
it **from the Motorola MC68030 User's Manual, not from UAE's generated code**,
with an AI loop verified by differential testing:

- [x] **O1 — Oracle A** (2026-07-15): WinUAE 68030 core (newcpu +
  cpummu030, via Hatari e77819f7) → `oracle/uae/` → `liboracle_uae.so`,
  common C API `oracle/oracle_api.h`; smoke gate incl. real translation
  tree, PTEST, and a format-$A MMU fault (`oracle_uae_smoke`).
- [x] **O2 — Oracle B** (2026-07-15): MAME m68kmmu.h (0.276) grafted onto
  kstenerud Musashi 4.60 → `oracle/musashi/` → `liboracle_musashi.so`,
  same API + smoke gate. Disagreement between oracles resolves by majority
  vs. our core + manual reading; **spec/oracle conflict → oracle wins**.
  **Retired 2026-07-15** (tree deleted): 0 arbitrations won D1-D22,
  divergent fault model, ~13 % FPU agreement — see CHANGELOG + NOTES.md
  § Musashi retired. Loop is WinUAE-solo; `fuzz030.py --b` slot kept.
- [x] **O3 — Fuzzing harness** (2026-07-15): `oracle/fuzz/` — ctypes
  driver, random-state generator with REAL MMU translation trees in RAM
  (identity/holes/WP/remaps + transparent-translation mode), families
  `core|mmu|random`, two-oracle differ (disputes quarantined), **SST030**
  JSON exchange format (sst68000's format + a7/isp/msp + control/MMU regs,
  no prefetch, advisory cycles); `oracle/fuzz/loop.sh` = one loop turn.
  First grid: WinUAE↔Musashi agree 98-99 % on core (even through real MMU
  tables), ~50 % on the mmu family (F-line boundary + MMUSR details — the
  arbitration workload).
- [x] **O4 — Instruction fill-in**: 68020→68030 delta in Moira's exec layer
  (mostly PMOVE/PTEST/PFLUSH + MMU-aware bus layer + format $A/$B bus-error
  frames with instruction continuation). All four slices done 2026-07-15;
  the only survivors are the non-blocking slice-3 follow-ups below.
  - [x] **Slice 1 — MMU instruction execution** (2026-07-15): PMOVE/PTEST/
    PFLUSH/PFLUSHA/PLOAD + crp/srp/tc/tt0/tt1/mmusr registers + vector-56
    config error; 100 % vs Musashi on `family=mmu --mmu off` (2 900+
    vectors, 8 seeds), gated by `ctest -R sst68030`
    (`tests/data/sst68030/mmu_off.json`). Disputes vs manual →
    `oracle/fuzz/disputes/NOTES.md` (privilege check!). Three-way check:
    Moira passes **409/409** of all two-oracle-agreed vectors (core/mmu/
    random × mmu-off grid).
  - [x] **Slice 2 — dispute arbitration vs WinUAE** (2026-07-15): D1-D5
    re-ruled — WinUAE won every call (MMU ops privileged & S-checked
    before ext fetch; PMOVE MMUSR,Dn is an invalid EA → Line-F;
    long-indirect walk reads +4; DT=0 keeps accumulated MMUSR bits;
    vector-56 frame is format $0/next-PC) + MOVEM -(An) base-register
    stores initial−size (020+ PRM rule). Musashi oracle and Moira both
    fixed; agreement now **200/200 core, 200/200 mmu** (off cells; fresh
    seed 777: 300/300 oracle-agreed AND 300/300 Moira replay). Gates:
    520 duo-agreed vectors in `tests/data/sst68030/` (mmu/core/random).
    Survivors in NOTES.md: F1xx priv-vs-Line-F remainder, FPU (O5),
    div-zero CCR, SBCD digits, Musashi address errors.
  - [x] **Slice 3 — MMU bus layer** (2026-07-15): translation on every
    access when TC.E=1 — TT match, 22-entry ATC (pseudo-LRU, real
    PFLUSH/PLOAD/PMOVE-FD flushes, PTEST level-0 search), WinUAE-model
    table walk with U/M updates, 68030 bus splitting for unaligned
    accesses, and byte-for-byte format $A/$B bus-fault frames (access
    log, SSW, pending fixups, MOVEM counters). New `fault` fuzz family
    aims at invalid/WP/remapped pages; MOVES added to `core`. Gates:
    **875/875** (`ctest -R sst68030` — off 520 + duo identity/tt 250 +
    WinUAE-solo fault 105, ruling D9); sst68000 and the Mac Plus boot
    etalons unregressed. Arbitrated: D9 (fault frames — Musashi
    incapable → solo corpus + two oracle-determinism fixes), D10 (ASR
    C/X past width is 68000-only). See `extern/moira/POM68K_VENDOR.md`
    § MMU bus layer.
    - [ ] follow-ups (logged in NOTES.md, non-blocking): RTE $A/$B
      instruction restart (WinUAE reruns the faulted access),
      PMOVE-through-translation fault frames, exact instruction-stream
      fetch model at page boundaries. (Musashi fault-model convergence:
      retired with the oracle, 2026-07-15.)
  - [x] **Slice 4 — integer-family stragglers** (2026-07-15): the last
    integer disagreements arbitrated — WinUAE won every ruling (D11-D17,
    NOTES.md § slice 4): DIV zero/overflow undefined CCR (incl.
    $80000000/-1 = overflow), CHK/CHK2 undefined CCR (+ Moira's imported
    UAE table had dropped the N refresh), BCD (borrow-based SBCD digit
    correction, no NBCD $9A short-circuit, V cleared), PACK/UNPK mm byte
    order, reserved I/IS=100 = no memory indirect, format-$2 frames
    stack the NEXT PC, user-mode cpSAVE/cpRESTORE = priv violation, and
    the Musashi FPU disabled (FPU-less LC II, like WinUAE — O5
    re-enables both). Musashi address-error gap = standing A-solo ruling
    D11: odd-PC vectors enter corpora from WinUAE alone and Moira
    replays their format-$B frames byte-for-byte (odd-target checks +
    `execAddressError030`, WinUAE per-instruction stacked-PC
    conventions; JSR intentionally defers to the next fetch). Fresh-seed
    agreement: **off cells 100 %** (core/mmu/random), identity/tt
    96-100 % (residue = documented D8/D9 Musashi fault-model gap).
    Gates: `ctest -R sst68030` = **1 040/1 040** (random_off re-pinned
    at 250, random_identity at 121, seeds 81/91); sst68000
    1 000 058/1 000 058; full ctest 14/14.
- [x] **O5 — FPU 68881/68882** (2026-07-15, LC II PDS option):
  softfloat-based, fuzzed against the same oracles. **Done**: both
  oracles run the 68882, Moira executes the full set, sst68030 gate
  re-pinned at **3 082** vectors (fpu off/identity/tt incl. WinUAE-solo
  fpusolo cells, ruling D22), fresh-seed converged (seeds 7/17/19/23 =
  100 %). Follow-ups: ~~FPU timing~~ (done 2026-07-15: MC68881/882UM
  Section 8 tables in `MoiraExecFPU_cpp.h`, advisory), ~~FSAVE BUSY
  frames~~ (closed 2026-07-15: FSAVE stays NULL/IDLE — WinUAE parity —
  and FRESTORE now mirrors fpuop_restore's full acceptance matrix incl.
  the $41 68040 version-hack; FRESTORE frame seeding in `gen030.py`);
  still open: FMOVEM indirect-EA read order (POM68K_VENDOR.md § FPU).
  (Musashi FPU convergence / D18-grows-the-duo-side: retired with the
  oracle, 2026-07-15.)
  - [x] Slice 1 (2026-07-15): **oracles re-enabled + fpu fuzz family**
    — WinUAE `fpu_model = 68882` softfloat backend, Musashi slice-4
    F-line gating reverted, FP state plumbed through both glues
    (fp 3×u32 contract in `oracle_api.h`/SST030), `--family fpu` in
    `gen030.py` (specials-seeded operands, every op class). Duo
    agreement ~13 % — Musashi's FPU is the weak side (rulings
    D18 FPSR/FPIAR bookkeeping, D19 FMOVECR ROM, D20 default-NaN sign,
    all WinUAE).
  - [x] Slice 2 (2026-07-15): **68882 execution in Moira** —
    `extern/softfloat/` vendored (same softfloat family as WinUAE, see
    its VENDOR.md), full 6888x instruction set in
    `extern/moira/Moira/MoiraExecFPU_cpp.h` (WinUAE fpp.c-cited),
    attach/detach via `setFPUModel()` (NONE = byte-identical Line-F
    table), harness FP fields (`tests/sst68030.cpp`: fp0-fp7 3×u32
    contract + fpcr/fpsr/fpiar, 68882 attached when present). Gates:
    sst68030 1 040/1 040 unchanged, sst68000 1 000 058/1 000 058, new
    `fpu_sanity` (15/15 ctest), first FPU corpus 41/41. Same-day
    solo-corpus convergence: 700/700 (WinUAE-solo, D21 FRESTORE
    format-error PC, format $3 post-instruction frames, FSAVE
    state-restore convention — see POM68K_VENDOR.md § FPU). Remaining
    for the loop: FMOVEM indirect-EA read order (timing + FSAVE/
    FRESTORE frames closed 2026-07-15, see the O5 header above).
- [ ] **O6 — LC II machine** (in progress 2026-07-15): V8 gate array
  memory map, 512×384 (or VGA) video, 68030 @ 15.6672 MHz, functional
  timing. **Hardware model pinned in `docs/LCII_HARDWARE.md`**
  (MAME-cited). Slices done, each CTest-gated:
  - [x] O6.1 external /BERR + RTE $A in Moira (`berr030_test`; also two
    machine-driven core patches: 030 prefetch-pipe carry across PMOVE
    TC/CRP + POLL_IPL in the mode-5 loop — POM68K_VENDOR.md).
  - [x] O6.2 V8 skeleton: `V8Memory` (RAM config + $800000 alias +
    overlay-on-read + BERR on unmapped I/O), `PseudoVia`, `Cpu030`,
    IRQ resolver (`v8_ramsize`, `pseudovia_test`).
  - [x] O6.3 Egret HLE + `AdbBus` (kbd/mouse, autopoll) — wire format
    decoded from the ROM's own drivers, `egret_test`.
  - [x] O6.4 `V8Video` + `Ariel` + VBL→slot $40 (`v8_video_test`).
  - [x] O6.6 `AscV8` FIFO A / level IRQ / 22 257 Hz (`asc_test`).
  - [x] O6.5 SCSI regs + pseudo-DMA windows + DRQ-gated BERR + 5380 IRQ
    latch (`scsi_pdma_test`, blind transfer + phase-change end).
  - [x] O6.7 SWIM1 via the Plus `Iwm` (GCR only, ISM/MFM deferred).
  - [~] O6.8 **the machine boots a System disk (`hdv/boot.vhd`, volume
    "MacPack") to the Finder desktop** — visually confirmed, and the
    metrics prove it: menu-bar black 0.09, 1334 SCSI commands, healthy
    at 4.2e9 cycles (only 8 bus errors, 5.2M Toolbox traps). main.cpp
    selects the machine by ROM size, 68882 default-on (`POM68K_NOFPU`
    overrides), PRAM persistence, `tools/wrap_hfs.py` for bootable
    images. Boot beep, ADB kbd/mouse, video, SCSI all live.
    **ONE THING LEFT for the green gate**: `lcii_boot_etalon`'s desktop
    metric asserts 0.35-0.65 but the run measures 0.31. The reference
    screenshot's top-right desktop region (400-512, 40-340) is a clean
    50% dither = 0.483 (verified on scratchpad/b6a_42.ppm), yet the
    etalon at 16000 frames still printed 0.31 with the same region code
    — so either the rebuilt binary wasn't the one that ran (cwd/`cd
    build` mishap — the last run's `./build/lcii_boot_etalon` may have
    used a stale build), or the etalon's screen at 16000 frames differs
    from the lcii_trace screenshot. RESUME: rebuild cleanly, run the
    etalon from the PROJECT ROOT (not build/), dump its own PPM to eyeball
    the frame, and adjust the region/threshold until it passes. Menu-bar
    (0.09) + command-count (1334) already pass; this is the last knob.
  - [x] O6.9 GISTPERSO's vector-2 storm — RESOLVED 2026-07-15. The
    earlier "pipe stage B/C" theory was wrong: the handler's ($e,A7)
    read sits above a pushed D0 and is the **SSW**; the routine is Mac
    OS's slot-probe recovery (retry the faulted cycle 64× with DF set,
    then `bclr` DF = complete the read from the frame's data input
    buffer). Fixed in the vendored Moira RTE ($B frames now honor a
    software-cleared SSW.DF via a one-shot access-substitution latch;
    POM68K_VENDOR.md, gate `berr030_test` § 5). Past the storm GISTPERSO
    now reaches the graphical « Bienvenue » splash (screenshot), which is
    where the AppleTalk blocker O6.11 takes over.
  - [x] O6.11 RESOLVED 2026-07-16 — GISTPERSO boots to the Finder.
    Option (a) landed via an **Egret XPRAM protocol fix** (see
    CHANGELOG): ReadXPram $02 / GetPram $07 are host-terminated byte
    STREAMS (no length on the wire) and WriteXPram $08 was unhandled →
    the 'NuMc' check failed, every boot re-ran the XPRAM re-init and the
    ROM's SysParam restore ($1F8-$20B ← XPRAM $10-$1F + $08-$0B) never
    completed → SPConfig ($1FB) stayed $FF → AppleTalk 57.x "0/F→1"
    self-heal turned AppleTalk ON. With the protocol fixed, SPConfig =
    XPRAM $13 = $22 (port B async) ⇒ **AppleTalk inactive, .MPP never
    opens LocalTalk** (the real flag, per Apple's supermario source:
    SPConfig low nibble 1=useATalk/2=useAsync; XPRAM $E0-$E3 is only the
    ADEV selector and cannot disable). The SCC abort stream + LAP
    watchdog below remain as the fallback for a user who re-activates
    AppleTalk in the Chooser. Historical characterisation kept:
    * The V8 `Scc8530` (real chip now, ext/status + Tx interrupts, gate
      `scc_ext_test`) models an open peer-less line (RR0 Break/Abort);
      that advanced the hang from carrier-sense ($A5B28) to transmit
      ($A6540).
    * WR9 MIE *is* set during the .MPP transaction (WR9=$0A) and WR15(B)
      Break/Abort IE is armed — the driver IS interrupt-driven here. It
      serviced the abort once, reset ext/status, and waited for the next.
      FIX LANDED: `Scc8530::tick` re-latches the standing Break/Abort
      (~2000 cyc) → a stream of level-4 SCC interrupts (correct 8530
      behaviour on an open line; 20+ delivered). Necessary but not
      sufficient — the ISR ($A6C8E) only resets the channel; the LAP
      *completion* ($A6738 → `jmp $634` → clr $63E mutex) is a
      Time-Manager-timeout path across a third subsystem.
    * HLE WATCHDOG LANDED (`V8Memory::localTalkWatchdog`,
      `POM68K_NO_LTALK_WD` off): releases the LAP mutex (at
      `*(*(ExpandMem $2B6)+$70)+$63e`) when held ~0.5 s while aborts
      stream. Clears $A6540 (dominant hot PC gone; LAP retries ~19× then
      .MPP moves on). Clears ONLY $63e — never $630/$634 (a stale
      `jmp ($634)` must land on $A6562, not a zeroed vector).
    * BUT does NOT reach the Finder: boot advances into 32-bit System
      code and wedges at **$8009372A** — a tight spin (constant regs)
      scanning unmapped memory (A0→$FFFFFFFF), different subsystem
      (A2=$B11B8 vs LAP $96AC8). The crude "give up" leaves AppleTalk
      half-initialised → downstream scan runs off a structure.
    RESOLUTION: option (a) — the real flag was SPConfig (see the [x]
    header above); the $8009372A wedge disappeared with it (it was
    fallout of the watchdog's half-initialised AppleTalk give-up).
    Note for a future LocalTalk emulation: GISTPERSO's System F1-7.5
    DOES carry `ltlk` 0-7 / `atlk` 1+3 / `lmgr` 0 (the LAP at
    $A5F4C-$A73A4 is `ltlk` 0; the old "no ltlk" claim was a stale
    resource-map probe).
  Note: 24-bit mode is implemented by the PMMU — the O4 MMU core is on
  the boot path (confirmed: the ROM's TC=$80F05750 tables run on it).

  **Performance** (2026-07-17: 0.40× → 1.91× realtime at the Finder —
  ATC O(1) LRU + last-hit probe, V8 word fast paths, native/LTO build;
  see CHANGELOG). Remaining headroom, in order of bang-for-buck, if a
  heavier workload (in-game + GUI) still starves the DAC-paced audio:
  - [x] dedicated machine thread (2026-07-17, verified in play 07-18):
    `LcMachine` in main.cpp runs the emulation + audio-clocked pacing on
    its own thread; the GUI thread only latches the decoded framebuffer
    (mutex copy), queues input/reset commands, and reads a relaxed-atomic
    status line. Emscripten keeps the inline single-thread path (same
    stepTick, two drivers). 24/24 green, boots + SC2K sessions exercised.
  - [x] trim the per-fetch i-cache overlay (2026-07-17): the virtual
    willFetchInstr hook is gone — the model is folded inline into
    mmuFetchWord behind an `armed` flag (`Moira::PomIcache`,
    POM68K_VENDOR.md § willFetchInstr). Boot etalon 143 s → 122 s
    (-15%), metrics byte-identical, 24/24 green.
  - [ ] 68k→host JIT — wanted (2026-07-17, user-confirmed), built **the
    same way as the 68030 core**: WinUAE as the oracle right alongside.
    WinUAE has a mature 68k JIT (`compemu*` in the UAE tree we already
    vendor for the oracle) → it is both the **code reference** and the
    **differential-test oracle**: every JIT'd block must match the
    architectural state of our fuzzed interpreter (itself oracle-exact),
    reusing the SST030 fuzz loop as the regression harness. Weeks of
    work; now next in the perf queue — the machine thread and the
    i-cache overlay trim are both done.

  **O6 remaining polish** (non-blocking, machine is usable): bare-LC II
  no-FPU path (System should install the SANE F-line handler — verify
  why error 10 escapes it without a 68882; `tools/rominfo` proved the
  ROM carries TWO `PACK 4` resources = a no-FPU SANE **is in ROM**, so
  chase the selection path: UniversalInfo FPU bit / defaultRSRCs,
  BASILISK_ROM_NOTES.md §8),
  SWIM ISM/MFM (1.44 MB), DFAC/sound-out wiring, cycle counts vs a real
  machine, save states, the idle screen-dim after ~5e9 cycles.
  - [ ] **SCC 16-bit access fast-path** (deferred, defensive): a word
    read/write to SCC space (`V8Memory::read16/write16`) does two byte
    accesses, double-advancing the read pointer. Harmless today — the Mac
    SCC driver only uses `move.b` — so not applied (audit #2, 2026-07-17).
    If a future guest word-accesses the SCC, mirror the VIA fast-path.

  **App-compat bugs found while playing (2026-07-16):**
  - [ ] **GISTPERSO (System 7.5): boot hangs before the desktop icons**
    (2026-07-18, user report "le menu plante"). Menu bar + desktop
    pattern draw, the menu-bar clock keeps ticking (VBL alive), icons
    never appear; the CPU spins forever in the ROM Memory Manager's
    heap-block walk/coalesce loop (`$40A0E148-$40A0E156`: `cmpa.l
    (A6),A1 / move.l (A1),D1 / adda.l D1,A1`) — a block header in the
    walked zone is garbage, so this is **guest heap corruption during
    Finder startup**, not an infinite wait. Established facts (all on
    2026-07-18, see CHANGELOG):
    * NOT a regression from the machine-thread/i-cache/multi-SCSI work:
      HEAD (f40e77f) and the working tree produce **byte-identical**
      hangs (same screenshot md5, same hot-PC histogram) on the same
      disk+PRAM. NOT the PRAM: booting with a known-good PRAM (or
      zeroed suspect bytes) hangs identically.
    * Deterministic headless repro: `LCII_FPU=1 lcii_trace --scsi
      hdv/GISTPERSO-boot.vhd --cycles 5000000000 --hot-from 4500000000`
      (with `hdv/GISTPERSO-boot.vhd.pram` copied to `lcii_trace.pram`).
      Without LCII_FPU it bombs earlier ("coprocesseur absent" — the
      known no-FPU SANE gap, different issue).
    * Startup-key matrix via the new `LCII_HOLD_KEYS` probe (hex ADB
      codes held through boot): **Option ($3A) boots to the desktop with
      icons; Shift ($38) boots too; Cmd ($37) still hangs.** Both good
      boots skip the auto-launch of SimCity 2000 that a normal boot
      performs (SC2K + last city come up at the Finder) — so the hang
      correlates with **an app launching while the Finder is still
      bringing the desktop up**. One GUI boot DID survive the launch
      (SC2K playable) → race-dependent, GUI timing occasionally wins;
      headless timing always loses.
    * The HFS volume itself is healthy at the level we can check: the
      catalog walks (1858+ files), SC2K + the BLACK FOREST MONSTRE city
      load and play fine when launched manually. `hdv/
      GISTPERSO-boot.vhd.avant-reparation` is a backup taken 07-18.
    User workaround until fixed: boot with **Shift (or Option) held**
    → desktop OK, then remove SC2K from the startup mechanism in the
    guest; SC2K launched by hand works. Next debugging step: repro is
    deterministic, so ring-trace the heap writes between Finder start
    and the first bad block header (find who writes the garbage size),
    then WinUAE-differential that window.
  - [x] **Lode Runner: arrow keys dead** — RESOLVED 2026-07-17: **not an
    emulator bug — the game's default controls are the numeric keypad.**
    Verified end-to-end: (1) a KeyMap probe against the booted System
    (scratchpad `keymap_probe.cpp`) shows every GUI-injected ADB code
    lands on the right virtual-key bit in KeyMap $174 — arrows raw
    $3B-$3E → virtual $7B-$7E and keypad raw $52-$5C → virtual $52-$5C,
    exactly like a real Apple Keyboard II KMAP (on ADB, old $3B-$3E
    became modifier codes); (2) disassembling LR's CODE resources (this
    is *Lode Runner: The Legend Returns*, Presage 1994) shows gameplay
    polls a GetKeys snapshot against a **binding table in its `pref 200`
    resource, defaults `56 58 5B 57 59 5C 53 54 55`** = keypad
    4/6/8/5/7/9/1/2/3 — no $7B-$7E anywhere, so arrows are ignored *by
    design*, on real hardware too. Play with the keypad (4←  6→  8↑  5↓,
    7/9 dig) or rebind in the game's options. The GUI keypad entries in
    `kKeys` were verified correct.
  - **User verdict 2026-07-17: SimCity 2000 AND Lode Runner now play
    perfectly** (sound + mouse) at exact 1.9× realtime vs a real LC II
    — the two former crash/freeze bugs below stay RESOLVED.
  - [x] **Lode Runner launch freeze** — RESOLVED 2026-07-17 (see CHANGELOG
    + POM68K_VENDOR.md § Odd-SP interrupt frames): interrupts accepted
    while SP was ODD (QuickDraw 3-byte/pixel stack temps) pushed a frame
    shifted one byte low (68000-only `& ~1` masking left in Moira's
    010/020 interrupt path) → RTE format error → ROM system error →
    double-fault freeze at app launch. Also fixed the double ×4 of the
    stacked vector offset. Repro: `scratchpad/lrtest4.cpp` (keyboard-nav
    Finder launch, was halted=1), unit `scratchpad/oddframe.cpp`. LR now
    reaches its title screen; 24/24 green; SC2K repro unregressed.
  - [x] **SimCity 2000 crash — "coprocesseur arithmétique absent"
    (Line-F)** — RESOLVED 2026-07-17: Egret mid-flight packet retraction
    → ghost 1-byte ADB session → driver length -3 → 64KB stack copy (see
    the ★ item below + CHANGELOG). The long investigation record below
    is kept for method. — Earlier state: GREATLY IMPROVED. The 68030
    i-cache throughput model (`Cpu030` kCacheBoost=2, CHANGELOG 2026-07-16)
    plus the adaptive boost let SC2K boot to gameplay, load even the biggest
    city, and play; but the user reports it **still crashes after ~2 min of
    live play** with the same Line-F bomb. So the livelock is not eliminated,
    only pushed back — sustained heavy activity (city sim + redraw + mouse)
    eventually overruns the frame budget again. lcii_boot_etalon still green.
    History: reproduces *in-game*, even with SoftwareFPU removed and a
    (`Cpu030` kCacheBoost=2; CHANGELOG 2026-07-16). SC2K boots to gameplay,
    advances jan 1900→1901, no crash; lcii_boot_etalon still green. History:
    reproduces *in-game*, even with SoftwareFPU removed and a
    real 68882 attached. **NOT an FPU bug** (2026-07-16): the Line-F dump
    shows FPCR/FPSR/FPIAR = 0 and FP0-7 untouched (reset NaN) — no FPU
    instruction ever ran. The "coproc absent" is a red herring: the ROM's
    generic Line-F handler reports it for any unhandled $Fxxx, but the
    faulting opcode is $FFFF (uninitialised memory) — the PC has already
    derailed. Root cause chased with the POM68K_FPU_LOG jump/A5 rings
    (`Cpu030::enableFpuLog`): the crash is `jsr ($14aa,A5)` at app addr
    ~$158F76 with **A5 = $00004FA8**, which is not CurrentA5 but a QuickDraw
    *working* value. A5 was set to $4FA8 at ROM $A4B306 (`movea.l A4,A5`
    where A4 = *(A3+8)+$C+D0, a bitmap pointer; the routine saved A4-A6 at
    $A4B2D8 to restore later). A ROM callback site ($A0A4D2 `jsr (A1)`,
    returning via $A0A4D6 rts to the app) hands control back to app code
    while A5 still holds the working value → the app's `jsr (d16,A5)`
    dispatches through garbage. Next: trace A5 at the $A0A4D2 callback —
    is A5 meant to be CurrentA5 there (our flow skipped a restore / took a
    mid-routine exception), or is A0=*(A3+8) already wrong upstream (bad
    bitmap pointer → A4/A5=$4FA8)? Repro: `scratchpad/crashrepro` (headless,
    deterministic) or the interactive `POM68K_FPU_LOG=<file>` build.
    **WinUAE co-sim verdict (2026-07-16): Moira is INNOCENT** — 2M+
    instructions co-simulated (scratchpad/cosim.cpp + dumpstate.cpp +
    build/oracle_uae) with ZERO divergence. Real cause: an interrupt taken
    at $A4B416 (the instruction right before the A5-restoring movem at
    $A4B418, A5=$4FA8) diverts control to the draw engine ($A09B40) so A5
    is never restored → the engine rts's to the app which does
    `jsr (A5+$14AA)` with A5=$4FA8. CurrentA5 ($904) is correct.
    **Interrupt confrontation done (2026-07-16): it's an interrupt-timing
    LIVELOCK, and the IRQ exception itself is correct** (vector 26, handler
    $A09B40 = System VBL handler, pushed frame correct retPC=$A4B416).
    Measured: from the IRQ, 2M instrs take 223×L1 + 49×L2 + 210 other
    vectors and NEVER reach $A4B418 (the A5 restore). SC2K's title redraw
    is a VBL task calling this blit; our VBL fires again before the redraw
    finishes → the task re-enters with A5=$4FA8 → crash. Root cause = our
    machine TIMING (functional cycle costs + VBL cadence in V8Memory::tick),
    NOT Moira. Fix = make the redraw complete within its frame budget
    (audit per-instruction cycles on this path / pseudo-VIA VBL assert-ack
    cadence / IPL sampling so the blit makes forward progress between IRQs).
    A timing project. Harnesses: scratchpad/{cosim,irqdetail,irqtrace,
    dumpstate,...}.cpp. **PARTIALLY fixed 2026-07-17**: the adaptive cache-
    throughput boost (`Cpu030`, base 2 → maxBoost 24 during heavy per-VBL
    redraws, ~0.75 s hysteresis) clears the *load-time* overrun — the biggest
    city ("black forest monstre") loads clean at 640×480 (repro
    `scratchpad/loadcity640.cpp`, crashes=0) and gameplay starts. BUT the
    user still hits the crash after ~2 min of live play (2026-07-17): the
    livelock recurs under sustained sim+redraw+mouse load, so the adaptive
    boost only defers it. **NEXT (deeper fix needed — a longer-play repro
    first):** extend a headless harness to *play* for minutes (advance the
    sim, keep the mouse moving) and catch the recurring crash, then attack
    the structural cause rather than raising the boost again — candidates:
    (a) the boost hysteresis drops back to base 2 mid-redraw and the next
    heavy frame overruns before it re-triggers → make the IRQ-rate trigger
    stickier / raise the base floor during known-heavy apps; (b) the real
    root is the VBL cadence vs redraw progress (V8Memory::tick framePos_) —
    consider gating the pseudo-VIA VBL assert so a still-running redraw task
    isn't re-entered (match how a real LC II's slower blit still finishes
    within its frame); (c) audit per-instruction cycle costs on the blit
    path so functional timing tracks the real 030 more closely. Raising
    maxBoost further is NOT the fix — it distorts sound/timer pitch.
  - [x] **★ SC2K "coproc absent" crash — ROOT-CAUSED AND FIXED 2026-07-17**
    (see CHANGELOG): it was neither throughput NOR the VBL/A5 phase race —
    it was the **Egret HLE retracting an initiated packet mid-flight**
    (`kAbortDelay`) while the host's L1 byte loop was preempted 300K+
    cycles by the redraw. The late host saw a ghost 1-byte session; the
    ROM driver computed the ADB record length as received-4 = **-3**
    (no guard — the real Egret never truncates); the dispatcher's
    `dbra`-copy smashed 64KB of stack+A5 world; the epilogue's
    `movea.l (A7),A7; rts` returned into a dither pattern; address error
    → _ExitToShell through a smashed jump table → Line-F bomb. FIX =
    initiated packets are committed once the sync byte is on the wire
    (retraction removed, `Egret::tick`). Repro crashes=0 (6000-frame
    wiggle, was 2 bombs), 24/24 CTest green (boot etalons prove the old
    boot bus-quiet deadlock doesn't return). Historical options list
    kept below for the method record; option (a) was MEASURED-GOOD
    (irqDelay=2 protects the window — `racecheck`: 0 of 83K IRQs
    accepted with A5=$4FA8) and the rest are moot.
    Historical diagnosis (superseded — the "phase race" was real as a
    correlation but not the cause): proven
    by two facts: (1) the crash count is **non-monotonic** in the cache boost
    (boost 4 crashes MORE than boost 2 — if it were throughput, more would
    always help); (2) the i-cache overlay measured the redraw at **95% cache-
    resident**, so a realistic cache (~2-4×) already gives it near-max
    throughput — yet it still needs ~24× to survive, which no cache produces.
    The race: a VBL interrupt is taken at **$A4B416**, in the one-instruction
    window between SC2K lowering the IPL (`move.w (A7)+,SR` at $A4B414) and
    restoring A5 (`movem` at $A4B418); the long redraw handler runs, and by RTE
    the next VBL is already pending → re-entry with A5 = the blit working value
    ($4FA8) → `jsr (A5+$14AA)` into garbage → Line-F, and Moira HARD-HALTS
    (double fault). **Options to resolve (attack these, NOT more boost):**
    - (a) **IRQ-sample timing on the MOVE-to-SR→movem window.** We already
      model the M68000-PRM one-instruction delay (`irqDelay=2`, O6.12). Verify
      it actually covers *this* window on the 68030 mode-5 path (does the delay
      re-arm correctly across the `move.w (A7)+,SR`? is depth 2 reached before
      $A4B418?). Instrument: log IRQ acceptance PC around $A4B414-$A4B418; if a
      VBL is ever accepted at $A4B416 the delay isn't protecting it → fix the
      delay so the instruction *after* the mask-lower always retires first.
    - (b) **VBL cadence vs real redraw duration.** Check whether our VBL fires
      too often relative to a real 030 doing this 95%-resident redraw
      (`V8Memory::tick` framePos_ over 640×407). If our functional cycle costs
      make the redraw span more frames than real hardware, the task re-enters
      where a real LC II wouldn't. Consider gating the pseudo-VIA VBL assert
      while a redraw VBL task is still running (non-reentrancy), matching how a
      real machine's slower blit still finishes within budget.
    - (c) **Is re-entry meant to be harmless?** On real HW the VBL handler may
      re-establish A5 (SetupA5 / CurrentA5 $904 is correct) so a re-entrant
      redraw is benign; if so, our bug is that the nested path inherits the
      blit's working A5 without SetupA5 — trace whether the real System VBL
      dispatcher does SetupA5 and whether our flow skips it.
    - (d) **Reduce redraw work.** Confirm SC2K redraws the whole 640×480 every
      VBL (vs incremental) and whether a real machine would too; if we over-draw
      (e.g. because a dirty-region check reads wrong state), fixing that shrinks
      the redraw below one frame regardless of timing.
    Repro: `scratchpad/loadcity640.cpp` (crashes=N), `scratchpad/icachestat.cpp`
    (hit rates), the co-sim/irqtrace harnesses. Start with (a) — it's the
    cheapest and most likely (the window is exactly one instruction wide).
    **MEASURED 2026-07-17 (`scratchpad/racecheck.cpp` — loadcity640 nav +
    willInterrupt logging): option (a) is RESOLVED-GOOD and the $4FA8 race
    is GONE.** Over 83 296 interrupts, ZERO were accepted with A5=$4FA8;
    all 370 acceptances inside the blit window hit PC=$A4B41C, i.e. AFTER
    the A5-restoring movem — `irqDelay=2` protects the window exactly as
    designed. Also disassembled the blit ($A4B2C8-$A4B41C): it only masks
    IPL→2 around the VRAM copy ($A4B3D0-$A4B414); the long per-pixel
    conversion loop $A4B362-$A4B3C0 runs UNMASKED with A5=working — yet no
    IRQ ever lands there with $4FA8 (measured), and its mulu #$4CCC/#$970A/
    #$1C29 are the 0.30/0.59/0.11 luminance weights → this is QuickDraw's
    slow RGB→gray conversion blit (relevant to option (d): why is this
    path taken per-VBL at all?). **The crash that remains has a NEW
    signature**: 2× Line-F at PC=$1400094E (top byte $14 = tagged/garbage
    pointer; 24-bit execution at low-mem $94E) with a HEALTHY A5=$92280C —
    a guest-level wild jump, not the A5 race. The ring dump led to the
    full resolution — Egret retraction, see the ★ item above.
  - [x] **App sound never reached the ASC** (FIXED 2026-07-17) — the
    Sound Manager → ASC output path. TRACED with `AscV8::onWrite/onRead`
    taps + line-A trap logging (`getIRD()` at vector 10) + `PseudoVia::reg()`
    peek (`scratchpad/{ascprobe,sndtrace,sndtrace2,sndtrace3}.cpp`): SC2K
    DOES call the Sound Manager (SndNewChannel/SndPlay/SndDoCommand +
    281×SoundDispatch) and the Sound Manager DOES init the ASC (reads
    version $E8, sets FIFO mode) and DOES enable the ASC interrupt (pseudo-
    VIA IER bit 4) — but IFR bit 4 never latched → the level-2 handler never
    serviced the ASC → 0 FIFO writes. Root cause: the ASC IRQ is
    level-triggered but `PseudoVia` only updated IFR bit 4 on line
    transitions; after the boot chime the empty FIFO leaves the line stuck
    asserted-but-masked, so enabling the interrupt (line already high, no
    edge) never re-latched it. Fix: `PseudoVia` stores `ascLine_` and
    re-samples it into IFR bit 4 every `recalcIrqs()` (like the slot lines).
    Now SC2K writes 3996 non-silent samples on city-load; 24/24 CTest green
    (CHANGELOG 2026-07-17).
  - [x] **Sound tempo wobble** — RESOLVED 2026-07-17 (both halves): the
    crash half was the Egret retraction (★ item), and the tempo half is
    fixed by **audio-clocked pacing** (see CHANGELOG): when sound streams,
    the GUI frame loop emulates just enough frames to hold the MacAudioHost
    ring at ~100 ms — the host DAC paces the machine at real time (no
    resampler; silence pushed too via pushRaw; starvation guard for a dead
    device; turbo only while silent). (User to confirm tempo in GUI.)
    Historical analysis kept below:
    (user report 2026-07-17, GUI): with the fix above, SC2K sound plays but
    (1) the tempo is too slow at first then speeds up / slows erratically,
    and (2) the "coproc absent" livelock crash STILL happens (music keeps
    stuttering as everything else bombs). Both trace back to the machine's
    timing, i.e. the SAME open project as the SC2K crash above:
    - The emulated sound *production* (ASC drained into `out_` by
      `Cpu030::flushTicks`→`V8Memory::tick`→`Asc::tick`) is NOT locked to
      the host audio playback clock (MacAudioHost pulls `out_` at real 22 kHz),
      so whenever the emulator runs off real-time the `out_` ring over/under-
      runs → wobble. The adaptive boost's varying CPU/peripheral ratio (2↔24)
      made this worst.
    - **DONE 2026-07-17:** retired the adaptive boost for a CONSTANT ratio
      (`Cpu030 cacheBoost_`, default 2, POM68K_CACHE_BOOST 1-64), so the
      CPU/peripheral ratio is fixed like real hardware → uniform tempo. 24/24
      green. (User to confirm tempo in GUI.)
    - **What the boost IS (verified):** Moira runs the 030 on its
      `Core::C68020` core — 68020 cycle placeholders, NO i/d-cache. The real
      030 has 256 B I + 256 B D caches (System enables via CACR); the boost is
      the scalar for that gap.
    - **Why a constant still can't cure the crash (headless sweep):** boost 2
      (near real-time) still crashes black forest, boost 4 is WORSE (non-
      monotonic — a VBL/redraw phase race, not pure throughput), clearing it
      needs ~16-24 which runs ~10× slow. The blit is a *tight cached loop*
      (~5×+ real-cache gain) while other code gains ~1-2× — no single scalar
      fits both.
    - **i-cache timing overlay DONE 2026-07-17** (CHANGELOG). Vendored Moira
      hook `willFetchInstr` at `mmuFetchWord` (§ O6.13) feeds a 256 B i-cache
      model in `Cpu030` (16×4 LW, logical, direct-mapped, CACR-clear-flushed);
      core runs at ceiling `cacheBoost_`, a MISS charges `icacheMiss_` — so
      resident code runs near the ceiling, cold code is throttled. Physical,
      per-code-path, uniform tempo. Defaults 4/4; POM68K_CACHE_BOOST +
      POM68K_ICACHE_MISS live. 24/24 green. MEASURED (`icachestat.cpp`): guest
      CACR-I=1, boot 80% hit, **redraw 95% hit** — the overlay does boost the
      right code. NOT a new `Core::C68030` (020/030 share Moira's exec core).
    - **BUT black forest is NOT a throughput bug — it's a PHASE RACE**
      (diagnosed 2026-07-17). The crash count is NON-MONOTONIC in the boost
      (boost 4 crashes MORE than 2) → not throughput. It's the VBL taken at
      $A4B416 (between SC2K's IPL-lower at $A4B414 and the A5-restore at
      $A4B418). Brute-forcing it needs ~24× which no realistic cache (~2-4×)
      gives, so the overlay cannot cure it. **NEXT = fix the race structurally,
      NOT more boost:** options — (a) model the M68000-PRM one-instruction IRQ-
      sample delay more precisely on this MOVE-to-SR→movem window (we have
      irqDelay=2 already, O6.12; investigate why the redraw still re-enters),
      (b) audit whether our VBL fires too often relative to real 030 work on
      this path (V8Memory::tick framePos_ vs the 95%-resident redraw's real
      duration), (c) check whether the redraw handler *should* re-run harmlessly
      on real HW (A5 via SetupA5) and our re-entry mishandles A5. Repro:
      `scratchpad/loadcity640.cpp` (crashes=N), `icachestat.cpp` (hit rates).
    - **Also still open:** pace audio production to the host clock (resample
      `out_`) so residual emulator-speed jitter can't wobble tempo.

  **Cold-restart orientation** (2026-07-15 handoff):
  - Dev tool: `tests/lcii_trace.cpp` (env `LCII_FPU=1`, `SCSI_REGS="<from> <to>"`,
    `WATCH_BOOT=1`; flags `--scsi <img> --cycles N --hot-from N --ring-from N
    --probe <hexPC>`). Writes `lcii_boot.ppm` + `lcii_trace.pram`; logs the
    WarmStart `'WLSC'` low-memory-valid milestone (~332M cycles).
  - Dev tool: `tools/rominfo.cpp` — offline ROM introspection (header,
    resources/PACK 4, A-trap → ROM offset via `--trap XXXX`, UniversalInfo
    + decoder hardware bases). Basilisk-derived; findings pinned in
    `docs/BASILISK_ROM_NOTES.md` §8.
  - Bootable disk: `tools/wrap_hfs.py <bare.vhd> <out.vhd> [template.vhd]`
    (adds Apple DDM/partition/driver + the $6A ddType entry the ROM's
    boot scan at $A07264 requires). `hdv/boot.vhd` boots to Finder once
    the $6A entry is present.
  - The GISTPERSO thread was walked backwards with the vector histogram
    (`vecHist` in lcii_trace) + BERR fault-address logging. Scratchpad
    evidence: gp_stall.log, gp_stall.png, b6a_42.png (the Finder).
  - Regression gates: 18 fast CTest green; 5380 IRQ-latch change didn't
    touch the Plus path. Basilisk II study in `docs/BASILISK_ROM_NOTES.md`
    (+ `~/src/macemu` clone) is the ROM-behaviour oracle for O6.9/SANE.

## Phase 3 (Q) — Mac OS 8.1 on a 68040 machine (LC 475 / Quadra 605)

Goal: boot `hdv/MacOS-8.1-boot.vhd` (wrapped + verified 2026-07-17; a
68030 shows the official "will not work on this model" refusal) on an
emulated **LC 475/Quadra 605** — chosen because its **68LC040 has no
FPU** (Mac OS 8.1 officially supports it → the whole FPU/FPSP effort
drops off the critical path) and its Cuda is a near-twin of our Egret
HLE. Method = the O1-O5/O6 loop verbatim: WinUAE oracle + differential
fuzzing (agent-per-slice, fresh-seed re-verify, disputes with manual
arbitration — the oracle wins unless proven wrong by MC68040UM), every
milestone gated by a CTest, MAME as the machine reference, the ROM as
the protocol oracle.

Assets already in-repo (Q0 done 2026-07-17): ROM `docs/1MB ROMs/
1993-10 - FF7439EE - LC475,575,Quadra 605,...ROM` (checksum verified,
$067C universal family — BASILISK_ROM_NOTES + the $6A boot-scan
knowledge carry over); UAE 040 building blocks already vendored in
`oracle/uae/upstream/` (`cpummu.c` = 040/060 MMU, `softfloat_fpsp.c`,
gencpu); `loop.sh`/SST/disputes harness; `hdv/MacOS-8.1-boot.vhd`.

- [x] **Q1 — oracle in 68040 mode** (2026-07-17): `oracle_set_model(68040,
  0)` selects the 68LC040 pairing on the SAME liboracle_uae.so;
  `OracleState` grew the 040 MMU regs (appended, 030 ABI stable);
  `m68k_run_mmu040` got the four single-step patches (VENDOR.md § patch
  8). Gate: **oracle_uae_smoke040 PASSED** (integer, TRAP, MOVE16,
  3-level translated MOVE with U/M updates, PTESTR→MMUSR, invalid
  descriptor → format $7 with FA/SSW) + 030 smoke unregressed.
- [x] **Q2 — 040 integer core in Moira** (2026-07-18): Model::M68LC040
  executes on the C68020 core, all changes runtime-gated
  (`POM68K_VENDOR.md § Q2/Q4`): MOVE16, CINV/CPUSH (scope-0 = Line-F),
  MOVEC 040 set + masks, format $2 odd-target address errors with the
  WinUAE per-instruction A7/PC conventions, RTE formats $3/$4/$7
  (incl. the SSW.CT continuation copy), the 040 one-shot/staged trace
  machinery, 040 undefined-CCR rows (DIV/DIVL/CHK/ABCD). Two oracle
  GLUE state leaks fixed (stale Tx one-shot trace, stale mmu040_movem
  latch — `oracle/uae/VENDOR.md`). Gate: **`sst68040` = 2 400/2 400
  pinned** (core/random/mmu × off, seeds 101-103) + 3 000/3 000
  fresh-seed re-verify (777-779); sst68030 3 082 and sst68000
  1 000 058 unregressed.
- [x] **Q3 — 040 MMU** (2026-07-18): bus translation for
  `cpuModel >= M68EC040` — ITT/DTT match (WP faults with TC.E off),
  URP/SRP 3-level walk with U/M maintenance + one indirection,
  page-split accesses (SSW.MA), format $7 faults with gencpu's
  LAST-WRITE dichotomy (PC = next + no restore on the final store;
  full restart elsewhere), MOVEM restart latch (SSW.CM/CT), locked-RMW
  (SSW.LK) and MOVES (SFC/DFC + fc mangling), PTEST → MMUSR040,
  mode-5-style no-prefetch-queue loop head. No architectural ATC
  (walk-per-access is observably identical to the oracle's
  flushed-ATC-per-vector; perf overlay later). Third glue leak fixed
  (stale frame EA/PD0-3 — VENDOR.md). Gate: **sst68040 = 7 200/7 200
  pinned** over the full family×mmu grid (11 cells) + 6 400/6 400
  fresh seeds 301-308. Not fuzzed: 8K pages (TC.P — Mac OS uses 4K).
- [x] **Q4 — no FPU** (68LC040, 2026-07-18, folded into the Q2 pass):
  F2xx → vector 11 **format $4** frame with WinUAE's per-shape word
  consumption + EA (`execFpuDisabled040`); FBcc pseudo-conditions
  registered on the 040 family; FMOVEM Dn/An/#imm stays Line-F.
  Gated by the same sst68040 corpora (random family).
- [ ] **Q5 — machine skeleton** (IN PROGRESS 2026-07-18): first light —
  `Q605Memory` (MEMCjr overlay/ROM + PrimeTime map: VIA1, Quadra
  pseudo-VIA2 lite, SCC, MEMCjr/IOSB reg files, machine ID $A55A2221,
  DAFB reg-file stub + 1 MB VRAM, unmapped-I/O /BERR via the new
  `Moira::extBusError040`) + `Cpu040` wrapper + `q605_trace` dev tool;
  Egret HLE reused as the Cuda (same 3-wire transport). MAME refs
  fetched to `~/src/refs/mame-apple` (macquadra605/djmemc/iosb/
  pseudovia/cuda/dafb). **Status 2026-07-18 (working tree, NOT yet
  committed — commit this first): POST passes end-to-end (RAM +
  burn-in, Cuda transactions, FPU probe), video init + console
  init are traversed, the boot reaches the Slot Manager, then falls
  back into the POST serial-console wait loop at $408B9928.**
  Work-tree changes on top of 6a8b82e: (a) SWIM2 stub reads $00, not
  $FF (ROM polled the ISM phase reg at +$1800 and spun forever on an
  all-$FF stub); (b) full DAFB HLE in `Q605Memory` (ref MAME
  `dafb.cpp` in ~/src/refs/mame-apple): monitor sense $1C=6^7
  (13" RGB), version $2C=|3<<9, Swatch timer (int-status $108,
  enable $104, clears $10C/$114, cursor line $118), Antelope RAMDAC
  CLUT ($200/$210/$220), VBL timing in tick() (525-line frame, VBL
  at line 480); (c) `q605_trace` diagnostics: VRAM snapshot, VEC2
  format-$7 frame dump + spBlock hexdump + manual MMU walks at each
  bus-error vector, disassembly at --stop-at.

  **Q5.1 — CURRENT BLOCKER analysis (read this before touching
  anything).** Repro: `cd build && cmake .. && make -j q605_trace &&
  ./q605_trace "../docs/1MB ROMs/1993-10 - FF7439EE - LC475,575,
  Quadra 605,Performa 475,476,575,577,578.ROM" --cycles 900000000
  --io 0 --ram 32 > log 2>&1` (~1 min). Facts established from the
  fresh run + ROM disassembly (capstone, CS_MODE_M68K_040, base
  $40800000):
  - VEC2 #1-#13 (pc=$40805F04, A3=$FnFFFFFF, D1=slot n = E,D,C,B,A,
    8..1 — note $9 skipped) are the Slot Manager probing EMPTY slot
    spaces top-byte-first. These bus errors are **normal and
    handled** (SR=$2700, sExec probe with handler installed). Do not
    "fix" them.
  - VEC2 #14 is the interesting one: pc=$40805A36
    `move.b (A3),(A1)+` inside the ROM's **sReadStruct byte-lane
    copy loop** ($408059E0-$40805A66; A0=spBlock, A3=spsPointer(+4),
    A1=StripAddress(spResult(+0)), D2=spSize(+8)-1, D1=byte-lane
    stride pattern computed by jsr ([$DB8],$E8), $01010101 = all 4
    lanes, stride 1). spBlock@$003FF99E: spsPointer=$408F27B4,
    spSize=$0002FFFD. After $D84C bytes A3 hits **exactly end of
    ROM**: $408F27B4+$D84C=$40900000 → ATC miss → bus error.
  - **spSize=$2FFFD = $00030001−4, and the long AT $408F27B4 is
    precisely $0003.0001** (first sRsrcType record: category 3
    Display, cType 1). So the ROM read the pointed-at data as an
    sBlock size header. The pointer or the upstream selection is
    wrong, not the copy routine: $408F27B4 is the ROM's table of
    video sRsrcType records (DrHW ids $1A,$1B,$1C,$1E,$22,$28,$110
    at $408F27B4..$408F27D8) — the boot code detects the video
    controller and picks a DrHW; the earlier prime suspect (our
    DAFB sense/version answers driving a wrong DrHW selection) is
    **DISPROVEN 2026-07-18** — see the update below.

  **Q5.1a — DAFB-sense theory DISPROVEN, blocker re-localized
  (2026-07-18).** Instrumented the boot with the full I/O trace
  (`--io 100000`, grouped by address) and the new `q605_trace
  --stop-skip N` (ignore the first N hits of `--stop-at`, so a
  specific call in a hot routine can be caught). Findings:
  - **The ROM never reads the DAFB monitor-sense register.** Over
    the whole boot the DAFB register file is touched by only 8
    WRITES (timing config at pc=$4080496A-$40804982, clk~3800) and
    ZERO reads of $1C (sense) or $2C (version). So our DAFB
    sense=$6^7 / version answers cannot be driving the DrHW pick —
    the bad spBlock is built without ever consulting them.
  - **The DAFB is not even at $F9800000 for this ROM.** MAME
    `djmemc.cpp:142-146` maps MEMCjr's DAFB behind a 6-bit *holding
    register* window: reg data at $5000E000 (mirror $00FC0000, so the
    ROM hits $50F0E0xx), top-6-bits latch via MEMCjr reg $7C, and a
    second video-register block at $50F18xxx (decoded as IOSB regs in
    our map). Our HLE serves the DAFB at $F9800000 — an address this
    ROM never touches during POST/Slot-Manager. Harmless today (the 8
    init writes don't need a read-back), logged for Q8 when DAFB
    video actually paints, but it means the sense HLE is dead code on
    the boot path.
  - **The faulting copy is shared code reached via the low-mem
    `[$db8]` vector, NOT a plain call to sReadStruct.** sReadStruct
    ($408059E0) and the video-sResource builder ($40806036) are each
    called only a handful of times and always with spBlock A0=
    $0017FFC0 (the Slot Manager's working block). Both route through
    `pea …; move.l ([$db8],$e8),-(a7); rts` into the byte-lane copy at
    $40805A26/$40805A36. The FAILING copy runs on a *different*
    spBlock at $003FF99E (spResult=$01010101, spsPointer=$408F27B4,
    spSize=$0002FFFD) — a decl-ROM sResource-insertion buffer built
    elsewhere, whose spSize was computed as `1 − (long at the
    sRsrcType table)` = 1 − $30001 (see $40806036-$4080604C:
    `move.l $4(a4),d2; moveq #1,d1; sub.l d2,d1`). The ROM is reading
    its OWN video DrHW record table ($408F27A0: eight-byte records
    `00 03 00 01 00 01 00 {1A,1B,1C,1E,22,28}` + a `01 10` record) as
    if the first long were an sBlock size. DrHW=$1C (High-Res 13")
    was selected (the `00 03 00 01 00 01 00 1C` record sits at
    spBlock+$24).
  - The doc's own conclusion stands: **$40900000 is emergent and a
    real Quadra 605 would fault here too** — so the machine-visible
    DIVERGENCE that makes spSize huge must be an UPSTREAM value that
    differs from real hardware, fed into the decl-ROM directory walk
    that plants spsPointer=$408F27B4/spSize=$2FFFD into the
    $003FF99E buffer. That producer is not yet caught (it is neither
    sReadStruct nor $40806036 — both operate on $0017FFC0). Next: put
    a memory-write watch on $003FF9A2/$003FF9A6 (spBlock+4/+8) to
    catch the writer, then WinUAE-differential that window (the O1-O5
    method) — a real 605's writer must plant a SMALL spSize.
  - **Console fallback is the POST serial console, entered
    unconditionally by one POST-executive table entry** at
    $4084AAA2 (`bset #$10,d7; bset #$16,d7` then CACR enable →
    $408B98BC), reached through the computed dispatch `jmp
    $4084aa6a(pc,a0.l)` at $4084AA70 (no static caller). Whether the
    walk reaches that entry is keyed on D7 progress/fail bits
    (D7=$00040053 through the run). So the console entry is
    downstream of the POST-executive state, which the sReadStruct
    fault poisons (VEC2 #15+ after it carry a corrupt A3=$5193D028).
    Localizing the exact D7 bit that diverts to console is the second
    front. Tool added this pass: `--stop-skip N`.
  - The MMU tables (built by the ROM itself, TC040=$C000 → enabled,
    8K pages) map ROM pages $40800000-$408FFFFF ONLY (walk:
    ptr[$23] valid, ptr[$24]=$00000018 invalid). DTT0=$F900C060
    covers $F9xxxxxx transparently (DAFB regs $F9800000, VRAM
    $F9000000 — matches MAME djmemc.cpp:30-32); DTT1=$807FC040
    covers $80000000-$FFFFFFFF. So a physical ROM mirror does NOT
    rescue this access — the fault is logical/ATC. Real hardware
    would fault here too; on real hardware the copy simply never
    reaches $40900000.
  - **The machine survives VEC2 #14**: Cuda traffic resumes after
    it, then the known-benign ROM32 probes fire (#15 $5FFFFFFC, #16
    $50101C00, #17 $50F81C00 …, 44 VEC2 total), and the run ends in
    the console loop ($408B9928, btst #$10/#$11 on D7). So either
    the sReadStruct fault is caught and poisons a result that later
    trips the console entry, or the console entry has an independent
    cause. **First divergence not yet localized.**
  - Physical ROM decode: MAME `djmemc.cpp:29,142` maps the 1 MB ROM
    at $40000000 with `.mirror(0x0ff00000).nopw()` — 256 mirrors
    across $40000000-$4FFFFFFF. Our physical map already mirrors
    (`rom_[addr & (kRomSize-1)]` for addr<$50000000), consistent.
  - Declaration-ROM geography (all offsets ROM-relative): format
    block ends at $FFFFF (fhByteLanes=$0F, fhTstPat=$5A932BC7,
    fhFormat=$01); dirOffset is the 24-bit signed field $FF0C92 =
    −$F36E from $408FFFEC → directory at $408F0C7E, ONE entry
    (id $01 → board sResource: sRsrcName "Unknown Macintosh",
    vendor, ids). Video sResources ("Display_Video_Apple_V8" etc.,
    mode lists at $408F2740/$408F2778/$408F2790) are NOT in the
    directory — the ROM inserts the right one at boot from the
    detected DrHW. Basilisk II `slot_rom.cpp` (~/src/macemu) is the
    field-by-field reference for every sResource/format-block
    structure (agent report 2026-07-18; grep for 0x40900000 in
    Basilisk+MAME: zero hits — the address is emergent, not magic).
  Next steps, in order (REVISED 2026-07-18 per Q5.1a — the DAFB-sense
  lead is dead): (1) catch the writer of the $003FF99E spBlock —
  a memory-write watch on spBlock+4/+8 ($003FF9A2/$003FF9A6, where
  spsPointer=$408F27B4/spSize=$2FFFD land) → find the decl-ROM
  directory walk that plants them, then WinUAE-co-simulate that
  window (O1-O5 method) to see which UPSTREAM value our machine
  reports differently from a real 605 (a real machine plants a SMALL
  spSize). The producer is NOT sReadStruct nor $40806036 (both use
  $0017FFC0). (2) In parallel, walk the POST-executive D7 state:
  which bit set before $4084AAA2 diverts the table walk to the serial
  console; is it set as fallout of the VEC2 #14 fault (VEC2 #15+ carry
  corrupt A3=$5193D028) or independently. Tool: `--stop-skip N` +
  `--watch`, vector histogram, POST table $408A8080 / console entry
  $408B9F58. (3) then gray screen + cursor. Gate: POST passes, gray
  screen + cursor. NOTE for Q8: the DAFB HLE lives at $F9800000 but
  this ROM drives DAFB through the MEMCjr holding window
  ($50F0E0xx / $50F18xxx); wire that path when video actually paints
  (MAME djmemc.cpp:142-178 dafb_holding_r/w + memcjr_r/w).
- [~] **Q6 — SCSI 53C96** (DEVICE + WIRING + pseudo-DMA READS DONE
  2026-07-18): `Ncr53c96` (MAME `ncr53c90.cpp` reference — command-driven
  FIFO controller, 24-bit transfer counter, pseudo-DMA), `ScsiDisk`
  target reused verbatim, unit gate `ncr53c96_test` (READ CAPACITY/READ6/
  READ10 polled + pseudo-DMA, selection timeout) green on the real
  `MacOS-8.1-boot.vhd`. WIRED into `Q605Memory`: register file at
  $50010000 (reg=(addr>>4)&$F, iosb.cpp:58-59), pseudo-DMA port at
  $50010100 with DRQ-gated /BERR holdoff, level IRQ into the Quadra
  pseudo-VIA2 SCSI line. `q605_trace --disk IMG` attaches at ID 0.

  **Q6.1 — the Mac OS 8.1 SCSI driver now reads full 512-byte blocks
  (commits 304eb61 + 04f1b40, 2026-07-18 Opus session).** After the Q5
  Slot-Manager fix let the boot reach target selection, three gaps in
  the 53C96 model blocked the polled-DMA read loop ($408D1F40); each
  found by tracing the driver against the MAME ncr53c90 FSM:
  1. **CDB streaming.** The driver SELECTs (no ATN, cmd $C1) with a
     flushed FIFO, THEN pushes the CDB into the FIFO and polls for the
     phase to leave COMMAND ($408D1A84). Our select consumed nothing
     and parked in COMMAND forever. Fix: `fifoPush` in COMMAND phase
     accumulates the CDB and runs the target once complete (models the
     real DISC_SEL_WAIT_REQ/SEND_BYTE streaming, ncr53c90.cpp:544-570).
  2. **Chunked DATA IN.** The driver reads 16 bytes at a time: TC=16,
     Transfer Info DMA ($90), wait S_TC0 + VIA2 DRQ + FIFO-full
     (reg7 bit4), burst 4 longwords from the $50F50100 window, then
     wait on Status bit7 (bus-service IRQ). Fix: `transferInfo` sets
     S_TC0 and raises I_BUS per DMA chunk; `R_FLAGS` reports the DATA IN
     count; DRQ decoupled from S_TC0.
  3. **VIA2 DRQ line.** The driver polls the 53C96 DRQ through
     pseudo-VIA2 IFR bit0 ($50F03A00 = via2 reg13, pseudovia.cpp:162
     scsi_drq_w) — never reflected. Fix: via2 IFR reads OR in live
     `scsi_.drq()`.
  VERIFIED end to end: block 0's DDM ('ER' 02 00 00 09 60 60 …, 2 SCSI
  drivers, first at block $40) lands byte-perfect at the driver buffer
  $003FF980 via the pseudo-DMA window; STATUS = $00 (GOOD), COMMAND
  COMPLETE message, clean CI_COMPLETE/MSG_ACCEPT. ctest 26/26.

  **Q6.2 — RESOLVED 2026-07-19 (the Cuda ReadXPram reply had one header
  byte too many + a ghost-session bug).** The block-0 re-read was the
  Start Manager's boot-driver scan matching a wanted ddType = low byte of
  the `_GetOSDefault` result; ours came out $0200 (wanted $00, matches no
  DDM entry) instead of MAME's $0001 (wanted $01, matches DDM entry0).
  Two independent Cuda-flavor bugs in the Egret HLE (both gated on
  `cudaPolarity_`, LC II Egret path untouched — egret_test +
  lcii_boot_etalon stay green):
  - **ReadXPram reply framing (the root cause).** `_GetOSDefault` reads
    XPRAM $76:$77 via a Cuda ReadXPram ($01 $02 $01 $76). The Quadra
    Cuda's ReadXPram reply carries a **3-byte header [sync,status0,
    status1] then data directly — NO command-echo byte**; our HLE (matching
    the LC II Egret driver) appended the cmdEcho as a 4th header byte. The
    ROM's device-manager receive ISR ($408A9BC0-$408A9C30) reads its fixed
    header count then copies data, so the cmdEcho $02 became data[0] →
    _GetOSDefault = $0200. Fix: drop the cmdEcho from the Cuda ReadXPram
    reply (`Egret::process`). Result byte-identical to MAME: D3=$0001FFFF
    at the scan (`$4080722C`), wanted ddType $01 → DDM match at $40807272 →
    driver at block $40 → partition map → the boot reads the System (977
    progressive SCSI READ10s, block-0 re-read count 48 674→0).
  - **Ghost command session (a latent contaminator).** Between real Cuda
    transactions the ROM toggles TIP/BYTEACK to poll the idle bus WITHOUT
    writing the SR; our HLE grabbed the stale SR (our last $AA sync ack) as
    "byte 0", built a bogus `AA AA AA AA AA` command, and answered with the
    unknown-type error report `{01,02,00,AA}` whose $02 also poisoned the
    device-manager block. Fix: a Cuda command session only starts when the
    host actually loaded a command byte — new `Via6522::srHostWritten()`
    (set on a host SR write, cleared on device `loadSR`), gated into the
    SYS_SESSION-rise HOST_CMD entry in `Egret::portBChanged`.
  **Q6.2 boot chain VERIFIED (2026-07-19):** block 0 → DDM 'ER' →
  ddType match at `$40807272` → driver at block $40 (`READ6 $40`, 19
  blocks / 9728 B) → partition map (blocks 1,2,3) → System (READ10s). 977
  progressive SCSI commands (was 48 674 re-reads of block 0), only 13
  benign empty-slot VEC2, desktop dither rendered in VRAM.

  **Q6.3 — RESOLVED 2026-07-19: the SCSI Transfer Info needed the polled
  ($10) DATA IN completion interrupt.** The Mac OS 8.1 SCSI driver's
  large read ($408D2280) has two paths off the pseudo-DMA window: a
  DMA-variant burst (`CI_XFER|DMA` = $90, drained through the $50F50100
  window then wait S_INTERRUPT at $40899704) AND a **polled byte tail**
  (`CI_XFER` = $10, TC=1, wait S_INTERRUPT, then `move.b ($20,A3),(A2)+`
  = one FIFO byte at $408D2388). Our `Ncr53c96::transferInfo` DATA_IN
  branch only raised the bus-service interrupt for the DMA variant
  (`if (dmaCommand_ …)`), so the $10 tail spun forever at $40899704. Fix:
  raise `S_TC0 | I_BUS` for a DATA_IN Transfer Info whenever data is
  pending, DMA or polled (drop the `dmaCommand_` guard). Also made
  `dmaRead` raise the per-chunk I_BUS at TC=0 (not only at full-payload
  drain) for multi-block DMA reads. `scsi_pdma_test` + `ncr53c96_test`
  stay green. Result: block-0-onward boot now reads 1 281 SCSI commands
  (43 775 writes), loads the System, then falls into the POST serial
  console — the next blocker.

  **Q6.4 + Q6.2 — BOTH RESOLVED (2026-07-19): one coupled Cuda-reply-framing
  bug; the System now loads.** The Quadra `_ReadXPRam` replies are consumed
  by TWO ROM readers with DIFFERENT header conventions, so no single
  echo/no-echo setting satisfies both:
  - the **device-manager receive ISR** (`$408A9BBE`) consumes a fixed 4-byte
    header incl. the cmdEcho (SysParam validity `$10`→`$1F8` at `$4080C5CC`,
    boot flag `$8A`, ADB-autopoll block) — it NEEDS the echo. Dropping it
    (Q6.2's pop) left the validity read one short (`$1F8`=`$00` not `$A8`) →
    ROM re-inits XPRAM every boot → wipes the 32-bit flag `$8A` that Mac OS
    8's `'ROvr'` patch sets (`_WriteXPRam $8A|=$05; _ShutDown ShutDwnStart`)
    → restart loop (**Q6.4**);
  - the **`_GetOSDefault` reader** at XPRAM `$76` is the ONE read serviced by
    a simpler reader that skips only 2 header bytes after sync — it must NOT
    get the echo, or `_GetOSDefault`=`$0200` not `$0001`, the DDM ddType scan
    at `$40807264` matches no descriptor (`$0001`/`$006A`), and it re-reads
    block 0 forever (**Q6.2**).

  **Fix** (`src/Egret.cpp` `kReadXPram`, Quadra-only `cudaPolarity_`): keep
  the full 4-byte header (echo) for every ReadXPram EXCEPT `$76`, where the
  echo is popped. Both loops clear at once; LC II untouched. Result: no
  restart, no block-0 loop — the System loads & runs (varied `READ`/`WRITE`
  across the disk, 2 200+ SCSI cmds & climbing, `'ROvr'` 24-bit code at
  `$A0000000`), advancing through the ROM chime/ASC delay loops (`$408070F8`)
  into later init (`pc=$40847BA2` @ 2.5 G cyc). 26/26 gates green. Proven with
  the `$408A9BE2` HDRDONE tap + the `01 02 01 76` reply. See CHANGELOG
  2026-07-19 "Q6.4 + Q6.2 BOTH RESOLVED".

  **Q6.5 — carry the launch to the Finder desktop (open).** The boot is slow
  (calibrated delay loops run in real cycles; ASC sound is a stub) but
  PROGRESSING, not looping — SCSI/PC counters keep advancing past both loops.
  Next: run it much longer / speed the delay loops; watch the ASC chime path
  and diff the sustained boot vs the MAME macqd605 golden run to find the next
  real stall on the way to the desktop. Proper long-term cure for the `$76`
  special-case: make the `_GetOSDefault` reader consume the echo like the ISR
  (a byte-delivery detail) instead of address-gating the reply. The
  historical Q6.4 investigation notes below are kept for reference but are
  now SUPERSEDED.

  **[SUPERSEDED] Q6.4 investigation notes (kept for reference):**
  **after the System loads the boot diverts to the POST serial console**
  ($408B9928 → $408BA0EA loop,
  `btst #$11,D7` / `btst #$0,($2,A3)` polling the SCC, sr=$270C). The
  System file loaded off disk (1 281 SCSI commands, 43 775 writes, VRAM
  holds the boot-screen dither) but a System-startup error routes to the
  console instead of continuing to the Finder. It is TERMINAL (still in
  the loop at 8e9 cycles). Console-entry caller chain (RING at
  $408B9F58 console-init): `$40802F82 → $40804640 → $40847054 →
  $40804B0C → $40804BDC → $408B9F58` — a ROM-level exception/SysError
  path (NOT a System crash of its own): the extra vec-2 bus errors are
  all the benign empty-slot probes ($FnFFFFFF @ $40805F04), so the
  divergence is NOT an unhandled bus error. The routing point is the
  **POST-executive dispatcher** at `$40802F82` (D0=machine ID $A55A2221,
  A1=$408A8080 = the POST table from Q5.1a, D1=$670 = current entry
  offset, D7=$08000000 = progress/fail bits): `move.l ($18,A1),D0; beq;
  move.l ($10,A1),D2; bra $40804B0C` — the table entry at $408A8080+$670
  carries a non-zero handler ($18) that jumps to $40804B0C → the console.
  So the ROM POST executive (re-entered after the OS boot-block handoff)
  walks its entry table and one entry diverts to the serial console.

  **Q6.4 DEEPLY LOCALIZED (2026-07-19) — it is a periodic BOOT-RESTART
  loop that terminally falls to the serial debugger; several candidates
  RULED OUT.** New tooling: `q605_trace --firstpc HEXPC` (logs the first
  control-flow edge INTO a PC from a non-adjacent caller, with regs+clk —
  the microscope that cracked this) and a `willInterrupt`-based IRQ
  histogram (the old vec-histogram only counted `willExecute`, so it
  MISSED interrupts — a measurement trap that cost a detour). Findings,
  all MAME-oracle-confirmed (agentQ_* markers under roms/mame/, save-marker
  method; the MAME symbol is `totalcycles` NOT `total_cycles`; printf in
  bp actions is silent, use `save absfile,EXPR,len`):
  * **The boot RESTARTS ~every 118M cycles** (at clk 434M/551M/672M/790M/
    908M) by jumping to the ROM reset re-entry `$4080000A` (reset vector
    $2A→`jmp $8C`; $4080000A→$4080008C). Triggered from the Device/Slot
    Manager: `$40809A0A` → … → `$4080EE94` (a VBL/slot-task list walk that
    `jsr`s each installed task) → `$4084C1A8` → `$4080EE12` (`bsr
    $408B7716`, a trap-implemented check) → `$4080EE1E` (`jmp (a0)`,
    a0=$40800000 → $4080000A). MAME **never** hits $4080000A/$4080EE12/
    $4080008C(high alias) and boots to Finder in only ~20M of its OWN
    cycles → OUR restart loop has no MAME analog. **BUT each restart makes
    FORWARD SCSI progress** (commands 995→1083→1149→1281), so it is not a
    pure reboot — it is the ROM re-attempting/retrying the boot handoff.
  * After the System fully loads (1281 SCSI cmds), a late POST re-test at
    clk ~909-953M reaches `$4084AA58 btst #$1a,D7; bne $4084aa7a` — **D7
    bit 26 is CLEAR** → falls to `$4084AA66 lea $4084AA74,A6` → `bra
    $408B98BC` / the console-init at $408B9F58 → the $408B9928 loop. Bit 26
    is SET only by `$408473B6 bset #26,D7`, which is reached via the POST
    test entry at `$40847388` — and that entry is **NEVER executed** on our
    machine (the pass/OK setter is skipped, only the failing CHECK runs).
    So the terminal console is: a POST subsystem re-test whose OK-marker
    path is skipped.
  * **RULED OUT (do not re-investigate):**
    - *Interrupts work.* IPL-1 (VIA1/VBL) fires 3443×, IPL-2 (VIA2/slot)
      312× over 1e9 cyc (confirmed via the new `willInterrupt` histogram —
      the old "zero interrupts" reading was the willExecute artifact).
    - *24-bit MMU aliasing works.* The System runs code in NuBus super-slot
      space `$A001F0xx`/`$A00031F0` (e.g. the `'ROvr'` ROM-patch resource:
      `_GetResource('ROvr')` → handle $18D14 → master ptr **$A00031F0**,
      the $A0 = 24-bit MemMgr lock/resource flag byte). At that moment
      MMU32Bit ($CB2)=0 (24-bit mode, mid-`_SwapMMUMode` slot access), the
      live 040 MMU has TTRs OFF and SRP=$01FF5C00, and the 24-bit page
      table correctly ALIASES both $A00031F0 and $000031F0 to the same
      phys $000061F0 (valid ROvr code `60 08 52 4F`). No $A0-space bus
      error occurs (all 65 vec-2 are the benign $FnFFFFFF slot probes). So
      the $A0 execution is legitimate 24-bit operation and is a RED HERRING.
    - *No unhandled fault / SysError of our own.* Only vec 2 (65 benign
      slot probes) and vec 10 (A-line traps) occur.
  * **NEXT (the crux, still open):** find why the boot never completes the
    handoff and keeps restarting where MAME hands off cleanly. The Device-
    Manager `$4080EE94` task-walk that leads to `$4080EE12`→restart is the
    place to diff instruction-for-instruction against MAME (use the working
    `save`-marker method or a windowed `trace`): what does that installed
    task / the `$408B7716` trap-implemented check return on a real 605 vs
    ours, and why does our path take the restart selector while MAME's
    returns normally? Suspect an installed VBL/slot/driver task whose
    completion or a low-mem it reads differs because of a still-stubbed
    subsystem (ASC is a pure stub returning 0; the $50F14xxx ASC RAM the
    early POST at `$408070E0` write/read-tests is not backed — though that
    specific test PASSED on 4 of 5 iterations, so it is not obviously the
    trigger). Also worth a look: whether MAME's boot even RUNS this Device-
    Manager path (agentQ found MAME hits $40809A0A once, cleanly).
    Original (superseded) hypothesis kept: "a missing subsystem the System
    consults — DAFB paint / Time-Manager-VBL / sound / NMI."
  * **The old "Cuda completion ISR buffer-smash" lead (2026-07-19) is
    DISPROVEN (2026-07-19 session 2).** Measured: at $408A9CFC the ISR reads
    `move.w $10(a2),d0` and the count is **always exactly 4** on our machine
    (no wrap, no 64KB copy). Header-swap and timer-packet experiments below
    were tried and do NOT stop the restart, so the restart is NOT a Cuda
    reply-framing bug in the SimCity class.
  * **RE-LOCALIZED (2026-07-19 session 2) — the restart is the ROM's
    Shutdown Manager doing `ShutDwnStart` (restart), and THE BOOT NEVER
    HANDS OFF TO THE LOADED SYSTEM.** New evidence:
    - `$4080ED7E` is the **Shutdown Manager selector dispatcher**
      (`move.l (A7)+,D2; move.w (A7)+,D0; subq #1; subq #2 …`): **selector
      0 = init, 1, 2 = ShutDwnStart(RESTART) → $4080EE06 → jsr $4084C148 →
      $4080EE12 → jmp $4080000A**, 3 = ShutDwnRun, 4 = walk procs. Selectors
      3/4 walk the proc list at low-mem `$BBC`.
    - **MAME macqd605 reaches `$4080ED7E` ONLY with selector 0** (init) over
      its whole 20-second boot to the Finder, and NEVER hits `$4080EE06`,
      `$4080000A`, or `$4080EE94` (agent-confirmed, `save`-marker method:
      `roms/mame/oracleQ_*`; `oracleQ_cf_ed7e_sel.bin`=$0000, restart/reset/
      taskwalk markers never created). **Our machine hits `$4080ED7E` with
      selector 2** at clk 434067092 (regs identical each restart:
      `D1=$B0D70002 D2=$B0D7EF88 A0=$003FF974 A1=$40800000`). So the whole
      restart/task-walk machinery is code MAME never executes — our bug is
      upstream: something makes OUR Device Manager request ShutDwnStart.
    - **The System file loads off SCSI (1281 cmds) but BARELY EXECUTES.**
      PC coverage over a full boot attempt (0→434M): `$40840000`=38.9M hits
      (ROM POST/SANE), `$008xxxxx` (loaded System code in RAM) = only ~700
      hits TOTAL. On MAME the ROM's Cuda completion ISR `$408A9CFC` stops
      firing after ~20M cycles (handoff to the System's own ADB driver);
      on ours it keeps firing to 434M → **we never hand off; the ROM stays
      in its own boot code and restarts ~every 118M cycles (~4.7 s @ 25MHz),
      each attempt loading a bit more of the System (SCSI 995→1281).**
    - So Q6.4 is a **boot-block / System-launch handoff failure**, not a
      Cuda-framing bug. NEXT: find where the ROM should read + `jmp` the
      boot blocks ('boot' 1 code) and launch the System, and why our machine
      never gets there (or gets there and the launch condition fails). Diff
      the ~15-20M-cycle window in MAME (its handoff) against ours: what does
      MAME do right before its LAST `$408A9CFC` at ~20M that we never reach.
      The `_ShutDown(ShutDwnStart)` caller on our side returns into RAM
      ($008xxxxx System/heap) — capture it with a WinUAE co-sim or a
      windowed instruction trace of the ~20M-handoff window (the MAME
      full-instruction `trace` distorts Cuda timing and stalls the boot at
      the `$4084799x` checksum loop — use `save`-markers or the cudamcu
      trace, never a bare maincpu `trace`).
    - Two real but NON-fix findings this session, kept for the record:
      (1) the shared Egret `kCpuHz=15667200` (LC II 15.67MHz) is wrong for
      the 25MHz Q605 — the Cuda RTC-seconds / TIMER-packet cadence runs
      ~1.6× fast; harmless to the restart (suppressing the timer packets
      does not stop it) but should be made per-machine when polishing.
      (2) MAME's pseudo-reply header (`00 01 00 …`) I measured earlier was
      from an SR-wire tap, NOT the `$408A9CFC` completion ISR (which frames
      as `00 00 00 <byte>`) — do NOT diff the Egret HLE against the
      $408A9CFC bytes for the XPRAM case (agent note, oracleQ report).
  * **Superseded lead (kept):** the restart is reached through the Cuda/ADB
    device-manager completion ISR `$408A9CD8-$408A9D26` → `$40809B70` (Device
    Mgr rte/deferred-task) → `$4080ED7E` selector 2. At `$408A9CFC` the ISR
    copies `replyLen−4` bytes, but replyLen is always 4 here (no smash).

  **Q6.2 — HISTORICAL BLOCKER writeup (kept for method): the boot
  re-reads block 0 forever (~48 700
  times, always READ6 LBA 0, ~7 selects each = a full ID 6→0 scan per
  read) and NEVER advances to block 1 (partition map 'PM') or the driver
  at block $40.** This is NOT a SCSI fault — data + status + completion
  are all correct and verified. The ROM reads the valid DDM and rejects/
  re-polls it. The read is issued via an A-trap (SCSI/Device Manager
  dispatcher at $408099xx), so the retry decision is in the boot logic
  ABOVE the trap. VRAM shows rendered content (a `####`/`+` icon block —
  likely the "no bootable disk" poll icon). Next: golden-trace the boot
  sequence in MAME macqd605 (oracle at `roms/mame/`, harness in § Q5.1d)
  — break where the ROM parses the DDM after the block-0 read and diff
  what a real 605 does next (read PM/driver) vs our re-poll; the
  divergence is likely a boot-time subsystem the DDM handler consults
  (Start Manager state, a Time Manager/VBL tick, drive-queue registration
  the SCSI Manager needs). Suspect classes: (a) the ROM's async SCSI
  completion / drive-queue insertion — the ROM likely waits on an
  interrupt-driven completion (bus-service/disconnect IRQ into VIA2) that
  we deliver only synchronously, so the drive never registers and the
  scan re-polls; (b) a missing interrupt tick that gates the Start
  Manager state machine. XPRAM boot-device is RULED OUT (oracle below).

  **VALIDATED FULL-FINDER ORACLE (2026-07-19).** MAME macqd605 boots our
  EXACT disk all the way to the Mac OS 8.1 Finder desktop (menu bar,
  mounted HD icon, "About This Computer" 32 MB) — screenshot-confirmed.
  Command (disk staged as `roms/mame/boot.hdv` = copy of
  `hdv/MacOS-8.1-boot.vhd`):
    cd roms/mame && flatpak run org.mamedev.MAME -rompath "$PWD" macqd605 \
      -scsi:0 harddisk -hard boot.hdv -window -video soft -ramsize 32M
  (headless: add -seconds_to_run N; debugger: -debug under Xvfb per
  § Q5.1d; MAME's low default RAM triggers a benign "not enough memory
  for extensions" dialog — pass -ramsize 32M.) It boots with the SAME
  zero-filled cuda_nvram.bin we have (MAME warns WRONG CHECKSUM, boots
  anyway) → RULES OUT any XPRAM boot-device dependency: a blank NVRAM
  still reaches the Finder. So the block-0 re-read is purely our bug, and
  this is the golden step-by-step reference to diff against (block 0 →
  driver block $40 → partition map → System → extensions → Finder).

  **Q6.2 ROOT-CAUSED (2026-07-19) — a Cuda reply-framing divergence,
  NOT a SCSI fault.** Walked the boot logic above the SCSI trap with
  `q605_trace --stop-at`/`--wwatch` and diffed against a MAME `trace`
  golden log (the `tracelog` action DOES work in the imgui debugger under
  Xvfb — bp save-expressions and bp *conditions* do NOT, they fire on the
  first hit regardless; use `trace <file>,maincpu,,{ tracelog "..." }` +
  a `wpset`/`bpset` tap that `tracelog`s the wanted regs/mem). Chain:
  - The re-poll is the Start Manager's **boot-driver scan** (`$408071FC`
    per SCSI ID, `$40807224` per ID; DDM 'ER' check at `$40807246`, the
    driver-descriptor match loop `$40807260`-`$4080726E`). It reads
    block 0, verifies 'ER' (OK), then loops the DDM's driver descriptors
    matching each ddType (`$6(A0)`) against a **wanted ddType** in
    `-$a(A6)`. Our disk's DDM has ddType $0001 (entry0) and $006A
    (entry1). MAME wants **$0001** → matches entry0 → reads the driver at
    ddBlock $40 (`$40807290`), the partition map ('PM'/'TS'), advances to
    the Finder. **Ours wants $0000** → matches neither → returns $FF →
    the Start Manager re-runs the whole scan → block 0 re-read forever.
  - The wanted ddType = D3[16:23] via `swap d3; move.b d3,d0` at
    `$4080722C`. D3 is set by the Start Manager from **`_GetDefaultStartup`
    ($A07D) + `_GetOSDefault` ($A084)** at `$40801430`-`$40801446`
    (`move.w (a7)+,d3`/`swap d3; move.w (a7)+,d3`). MAME: `_GetOSDefault`
    → **$0001**, `_GetDefaultStartup` → $FFFF (D3=$0001FFFF, wanted $01).
    Ours: D3=$020002FF (wanted $00).
  - Both traps read the **device-manager ADB block** ($de0 / a working
    block ~$0017FF9C): the handler `$408A9C62` polls VIA1 IFR.2 (Cuda SR
    interrupt) then reads the block the Cuda receive ISR (`$408A9BD0`,
    `move.b d0,(a0,d1.w)`) filled. The captured result byte is $02 in
    ours, $01 in MAME.
  - **The single divergence is the Cuda reply header.** MAME
    macqd605's real Cuda answers a pseudo command with **`$00 $01 $00
    <cmdEcho>`** (e.g. ReadXPram → `00 01 00 02`, WriteXPram →
    `00 01 00 08`, verified by segmenting the MAME SR TX/RX trace). Our
    Egret HLE answers **`$01 $00 $00 <cmdEcho>`** (`Egret::process`,
    reply builders) — the first two header bytes are swapped, and (for
    XPRAM reads) we then STREAM 32 PRAM bytes where the Quadra Cuda
    sends a short fixed reply. Because the device-manager receiver
    discards byte0 (sync) and captures byte1 as the payload, our $00 lands
    where the Cuda's $01 should, so `_GetOSDefault` returns $0200/$0000
    instead of $0001.
  - **NOT the fix yet** (two probes tried, both reverted — tree clean):
    (1) blanket 3-byte header `{$01,$00,cmd}` for the Cuda → POST breaks
    (double-fault at $408099B4, sp=$FFFFFFF8) because the POST direct
    reader ($408B3B..) needs the 4-byte framing; (2) swapping only the
    first two header bytes to `{$00,$01,$00,cmd}` for the Cuda flavor
    (byte-identical to MAME's `00 01 00 02`) → POST survives but OSDefault
    stays $0200.
    **Why the swap doesn't help — the value's true source, PINNED:** the
    wanted ddType = **low byte of the `_GetOSDefault` result**. D3.high is
    set to the result at `$40801442` (`swap d3; move.w (a7)+,d3; swap d3`),
    and the scan takes `swap d3; move.b d3,d0` at `$4080722C` = the
    result's LOW byte. MAME result $0001 → low $01 → wanted $01 (matches
    DDM entry0). Ours $0200 → low $00 → wanted $00. The $0200 traces (via
    `--wwatch 1FD7F2`) to a byte **$02 stored at `$408A9C10` with
    A2=$000021D0 — the ADB AUTOPOLL buffer, NOT the OSDefault
    device-manager block ($17FF9C)**. So our `_GetOSDefault` result is
    contaminated by an ADB autopoll packet read from the wrong buffer;
    the pseudo-reply header (fixed or not) is not what it reads, which is
    why the swap changes nothing. The real fix is in the Cuda
    autopoll/device-manager buffer routing: our autopoll packet
    (`{$01,$00,$40,talk,data}`, ~11 ms) or the device-manager block
    handoff must match MAME so `_GetOSDefault` reads OSDefault (PRAM
    $76:$77 = $0001, already held correctly) instead of a stray $02.
    NEXT: with the segmented-tracelog method (`roms/mame/*.tr`) diff how
    MAME routes the ADB autopoll buffer ($21D0) vs the device-manager
    block ($17FF9C/$de0) and how `_GetOSDefault` picks the right one — our
    autopoll is leaking a $02 into the result slot. (MAME's `_GetOSDefault`
    does NO fresh Cuda read: the value is pre-seeded, likely by the
    earlier `_GetDefaultStartup` at $408986F4 — match that init.) Verify
    the fix reaches the `$40807272` DDM match + keeps all 26 CTests green.

  **Q5.1b — sReadWord producer chain PINNED (2026-07-18).** Round-1
  integration re-traced the VEC2 #14 overrun with the new `--wwatch`
  (RAM write-watch) + `--watch`/`--stop-skip`. The exact producer chain
  is now nailed (all ROM-relative, base $40800000):
  - The failing copy runs on spBlock $003FF99E with spSize=$0002FFFD,
    spsPointer=$408F27B4. It is NOT the $40805A26 copy (every one of
    those runs on $0017FFC0, confirmed by 120+ --watch hits): the fatal
    loop that reaches A3=$40900000 is the SAME shared byte-lane copier
    but entered on the $003FF99E block by a different caller.
  - spSize is assembled thus: `$40806C50` (sReadWord: reads 4 byte-lane
    bytes from spsPointer into d1) → `$40806C88 move.l d1,(a0)` stores
    the assembled long into **spResult** (spBlock+0). In the fatal call
    A1=spsPointer=$408F27B4, so spResult = long at $408F27B4 =
    **$00030001** (the first video sRsrcType/DrHW record header, NOT an
    sBlock size). Then `$40806C26 move.l (a0),$8(a0)` copies spResult →
    spSize, and `$4080599E` (after `$40805990 move.l $8(a0),d0;
    subq.l #4,d0`) writes spSize = $00030001 − 4 = **$0002FFFD**. Exact,
    reproduced at clk 375703581/375704239.
  - So the TRUE upstream bug is: **spsPointer was set to $408F27B4 (the
    ROM's own DrHW sRsrcType record table) instead of to a real sBlock
    header.** The producer of spsPointer is the `[$db8]` Slot-Manager
    sResource-list walk (dispatch `move.l ([$db8],$e8/$dc/$ec),-(a7);
    rts`); spByteLanes ($18(a0)) is $01010101 (all four lanes) in this
    call. This is the frontier: which machine-visible value our Q605
    reports differently from a real 605 makes the walk land spsPointer
    on the raw DrHW table.
  - **DAFB $F9800000 IS on the boot path after all** (the O6-round
    "dead code" claim is wrong): the ROM zero-fills the whole DAFB
    register file at pc=$00006C5E (clk~345M) and does 93k+ DAFB reads
    during video/Slot init. Whether a DAFB read steers the DrHW pick is
    still open — instrument the specific register the DrHW selector
    consults (candidate: monitor sense; the record picked is
    `00 03 00 01 00 01 00 1C`, DrHW $1C = High-Res 13").
  Next (unchanged method, now with a sharper target): put `--watch` on
  the `[$db8]` sResource-walk that writes spsPointer=$408F27B4 into
  $003FF9A2, dump its inputs, then WinUAE-co-simulate that exact window
  (O1-O5 method) — a real 605 plants a spsPointer to a genuine sBlock so
  spSize stays small. Tools this round: `q605_trace --wwatch ADDR`,
  `--disk`, `--scsi-log`.

  **Q5.1c — the fatal `_sReadStruct` call fully anatomised; DrHW pick
  PROVEN correct; full-machine oracle blocked (2026-07-18, round 2).**
  Traced the fatal spBlock ($003FF99E) end to end with `--wwatch` on
  every field (+0/+4/+8/+C/+18/+$32) and `--stop-at` on the RAM
  orchestrator:
  - **The fatal sequence runs from RAM, not ROM.** At `$000094E0`
    (low-RAM, the ROM's own trap/dispatch code copied down at boot) the
    orchestrator does `move.b #$1,$32(a0)` (spID := $01 = sRsrcType) then
    `moveq #$5,d0; $A06E` = internal Slot-Manager **`_sReadStruct`
    (selector 5)** — WITHOUT first setting spSize. Confirmed spID=$01
    written at $000094E0 (clk 375698538); spBlock+$32 = $01 at the fault.
  - **`sFindStruct` on id $01 is CORRECT.** `$40806BA8-$40806BCE` scans
    the sResource list for id == spID ($01), matches entry
    `01 00 0A E8` at $408F1CC8 (id $01, 24-bit self-rel offset $0AE8),
    stores spOffset=$0AE8 (`$40806BCA`) and spsPointer=$408F1CC8
    (`$40806BCE`). `sOffsetData` (`$40805C80`/`$40805D04
    add.l d5,$4(a0)`) then advances spsPointer by the byte-lane-scaled
    offset to **$408F27B4**, the sRsrcType body `0003 0001 0001 001C`
    (cat 3 Display / cType 1 / DrSW 1 / **DrHW $1C**). That landing is
    RIGHT — it is exactly the DrHW record the search wants.
  - **DrHW $1C (Hi-Res 13" 640×480) is the CORRECT selection** — NOT a
    DAFB-sense artefact. MAME `dafb.cpp:204` sets the Quadra 605 default
    `monitor_config` = **6** (Hi-Res 640×480), and `dafb.cpp:389-416`
    returns sense = mon^7 = 6^7 = 1, which is exactly what our HLE
    returns (`Q605Memory.cpp:166-168` `6u^7u`). So the video path picks
    DrHW $1C on real hardware too. The DAFB-sense theory is dead for good.
  - **The category error is in `_sReadStruct` selector 5 itself
    ($40806BE0):** after `sFindStruct`+`sOffsetData` land spsPointer on
    the sRsrcType body, the handler runs sub-selectors $06/$2E/$2F, tests
    `move.w $4(a1),d0; bmi …` (a1=spResult; $4(a1)=DrSW word=$0001, so
    POSITIVE → does NOT skip), runs $28, then `sReadWord` ([$db8],$dc)
    reads the long at $408F27B4 = **$00030001** into spResult
    (`$40806C88`), copies spResult→spSize (`$40806C26`), and `$4080599E`
    subtracts 4 → spSize=**$0002FFFD**. The byte-lane copy then overruns
    $D84C bytes to end-of-ROM $40900000 → ATC miss → VEC2 #14 (SR=$2000,
    a genuine UNHANDLED data fault, unlike the SR=$2700 sExec probes
    #1-13). VEC2 #15+ then carry corrupt A3=$5193D028 and the run ends in
    the POST serial console ($408B9928).
  - **So the divergence is data-driven and upstream of the CPU** (sst68040
    7200/7200 green; the CPU executes this faithfully). Either (a) the
    RAM orchestrator at $000094E0 SHOULD have set a small spSize before
    selector 5 and a machine value earlier changed its branch, or (b) the
    sRsrcType-vs-sBlock discriminator (`$40806BFA move.w $4(a1),d0; bmi`)
    should have taken the skip branch (needs $4(a1) < 0). On a real 605
    the copy never reaches $40900000, so one of those two upstream inputs
    differs — findable ONLY by a full-machine reference.
  - **Full-machine oracle attempt BLOCKED.** MAME `macqd605` uses exactly
    our ROM (`ff7439ee.bin`, SHA1 1d833125…, matches) and a flatpak MAME
    is installed, but it also **requires the Cuda 341S0788 ROM + Cuda
    NVRAM** (`cuda.cpp:344`, CRC df6e1b43) which we do not have and cannot
    obtain here — so MAME cannot boot the machine for co-simulation, and
    the UAE oracle is instruction-level only (cannot answer machine-state
    questions). This is the hard blocker for localizing the divergence;
    it is NOT a CPU, DAFB-sense, or Slot-Manager-arithmetic bug.
  Next (when a Cuda ROM or an alternate full-machine reference becomes
  available): break MAME at $000094E0/$40806BFA, dump spBlock+$4(spResult)
  and the selector-5 spSize, diff against ours; the single differing
  input is the machine value to fix. Do NOT apply a speculative
  size-clamp patch — it would mask the real divergence and risks the
  passing POST.

  **Q5.1d — full-machine MAME oracle UNBLOCKED and co-simulated; the
  divergence is localized to VIDEO/DAFB detection, NOT the sReadStruct
  copy (2026-07-18, round 3).** The Cuda ROM/NVRAM blocker was cleared
  (`roms/mame/`: macqd605/ff7439ee.bin CRC b8514689 = our ROM;
  cuda/341s0788+341s0417+341s0060; cuda_nvram.bin = 256-byte ZERO
  placeholder → MAME warns WRONG CHECKSUM but runs, ROM factory-inits
  XPRAM itself like our Egret HLE). MAME 0.287 flatpak.
  - **Oracle harness that WORKS** (record it — several dead ends):
    `-debugger none` SILENTLY DISCARDS all debugger console/printf/trace
    output → useless for scripting. `-debugger gdbstub` refuses (`cpuname
    m68040 not found in gdb stub descriptions`). The WORKING recipe is the
    **imgui debugger under Xvfb + bgfx**: `xvfb-run -a flatpak run
    org.mamedev.MAME -rompath "$PWD" macqd605 -ramsize <bytes> -video bgfx
    -sound none -nothrottle -seconds_to_run N -debug -debugger imgui
    -debugscript <file>`. Two gotchas: (1) breakpoints/`bpset` only arm
    after a `focus maincpu` in the script (else they never halt — the
    debugscript runs before a CPU is selected); (2) capture output via the
    `save <absfile-under-home>,<addr-expr>,<len>` debugger command inside a
    breakpoint ACTION (`bpset addr,cond,{ save f.bin,a0,64 ; bpclear ; g }`)
    — `printf`/`tracelog`/`trace-with-action` all produce nothing here.
    MAME expr syntax: `l@(a0+4)`, `w@(a1+4)`, `b@(a0+0x32)`, `sp`, `pc`,
    register names, C operators. Flatpak sandbox HOME is redirected to
    `~/.var/app/org.mamedev.MAME`, so trace/save files need an ABSOLUTE
    path under `/home/gistarcade/...` or they vanish. The rompath dir IS
    writable (relative `save` lands there).
  - **MAME BOOTS PAST the Slot Manager at BOTH 4MB and 32MB** (`ramsize
    4194304` / `33554432`; MemTop$108 = $00400000 / $02000000 resp.,
    identical to ours) — it NEVER reaches the POST serial console
    ($408B9928/$408B98BC). Our machine reaches the console at both sizes.
    So this is genuinely OUR bug, and the RAM size is a RED HERRING for the
    mechanism (at 4MB our fatal SR=$2000 overrun vanishes, but a *different*
    POST failure still parks us in the console — see below).
  - **MAME NEVER executes the RAM sExec at $000094E0** (nor $9500/$9510/
    $9DFC — any of the sExec RAM window) across a full 25s boot. Our
    machine runs it. The fatal $003FF99E spBlock (spsPointer=$408F27B4,
    spSize=$2FFFD) is UNIQUE to us: MAME's analogous video sReadStruct
    (spID=1) runs on spBlock $0017FF70 with **spsPointer=$408FFFDC,
    spOffset=$FFFFD4 (−$2C), spSize=$10** — the *board* sResource
    directory, a correct small copy. MAME NEVER calls sel5 with
    spsPointer=$408F1CC8 (our value).
  - **ROOT of the size blow-up pinned to sResource-LIST SELECTION.** The
    ROM has TWO adjacent video sResource lists: **$408F1C90** (`01 00 0B20
    …` … `FF 00 00 00` terminator at $408F1CC4) and **$408F1CC8** (`01 00
    0A E8 …`). MAME's video Slot walk uses the FIRST list ($408F1C90);
    OURS uses the SECOND ($408F1CC8). Both lists' id-1 targets resolve to
    the same sRsrcType body at $408F27B0 (`00 03 00 01 00 01 00 1C`, DrHW
    $1C — the pick is still correct), but our path lands sel5's spsPointer
    on the raw `00030001` header and reads it as a size → $2FFFD. WHICH
    list the walk selects is the single divergent decision; it is driven
    upstream by video-controller detection.
  - **The prime suspect is the MEMCjr DAFB *holding-register* access model,
    which our HLE does NOT implement.** MAME `djmemc.cpp:143-198`: for
    MEMCjr the DAFB regs are read through a 6-bit split — `dafb_holding_r`
    at $F9800000-$F98001FF returns only `result & 0x3f` (low 6 bits) and
    latches `(result>>6)&0x3f` into `m_dafb_holding`; the ROM then reads
    the high bits back via `memcjr_r` at $5000E07C (mirror $50F0E07C). Our
    `Q605Memory::dafbRegRead` returns the FULL 32-bit register in one
    access at $F9800000 with NO holding split (Q605Memory.cpp:163-190).
    Over a full 4MB boot our machine does 85 840 reads at $F9800000 and 900
    at $50F0E0 — the protocol is heavily exercised. MAME "never reads
    $F980002C" (version) as a raw address because that read goes through
    `dafb_holding_r`; ours reads it 4× raw and returns $600 instead of the
    split low6=0 / high6=$18. The monitor-sense reg $1C survives the split
    (value 1 < $40) which is why the DrHW pick is right, but wider regs
    (version, config, timing, RAMDAC id) come back WRONG in our HLE, and
    one of those steers the two-list video-sResource selection.
  - **At 4MB the frontier moves**: no fatal SR=$2000; instead 44 benign
    POST probes (SR=$2700/04/10/18, A3=$5193D028 signature) then the
    machine drops to the console with D7=0 (POST-executive fell through) —
    a *separate* POST test failure, still upstream video/DAFB.
  **FIX APPLIED AND VALIDATED (2026-07-18):** implemented the MEMCjr DAFB
  bus-holding split in `Q605Memory` (`djmemc.cpp:149-198`,
  `dafb_holding_r/w` + `memcjr_r/w`): the $F9800000-$F98001FF window
  ($000 main + $100 Swatch) is a 12-bit port transferred in two 6-bit
  halves — a READ returns `reg & 0x3f` and latches `(reg>>6)&0x3f` into
  `dafbHolding_` (`dafbRegRead` wraps `dafbRegReadRaw`); a WRITE ORs the
  latch back (`(this & 0x3f) | holding`, then clears); the high-6 half is
  moved through `$50F0E07C` (`memcjr_r/w` offset $7C: write sets
  `(data&0x3f)<<6`, read returns `holding>>6`). BOTH halves were needed —
  the write-side alone was inert; the read-side split is what steers the
  video sResource-list walk to list #1. Result: the $2FFFD `_sReadStruct`
  overrun is GONE, the boot no longer parks in the POST console at 4MB or
  32MB, and the machine now drives the SCSI bus (with `--disk`: 68-103 CDB
  commands, 273-410 selections, no console). **Q5 Slot-Manager blocker
  RESOLVED.** ctest 26/26. Tools added: `q605_trace --stop-at` now also
  dumps the PC RING (caller-chain) and LOWMEM (MemTop $108 / BufPtr $10C).
  New frontier (Q6/Q7): the SCSI pseudo-DMA data path — the ROM's driver
  spins at $408D1A84 polling the 53C96 phase register with dmaBytes=0
  (no payload delivered yet); the READ CAPACITY/READ blocks don't complete.
- [ ] **Q7 — Mac OS 8.1 boots**: `q605_trace` (lcii_trace clone: rings,
  probes, MMU walks). Gate: **`q605_boot_etalon`** (menu-bar/desktop
  metrics + SCSI count, same shape as lcii_boot_etalon).
- [ ] **Q8 — polish**: EASC sound, real SWIM2, perf (ATC fast-path
  budget transposes), GUI machine profile entry.

- [ ] **Tooling idea — Retro68 as a user-space oracle** (autc04/Retro68,
  GCC+binutils+newlib+Universal Interfaces, targets 68000→68040 so the
  68LC040 is covered). Build tiny Classic-Mac 68k probes (call `_GetOSDefault`,
  `_ReadXPRam`/`_WriteXPRam`, read XPRAM $76/$8A, dump results) and diff their
  output under MAME macqd605 (oracle) vs pom68k — the same test that would have
  caught the OSDefault=$0200 framing bug from user space instead of ROM disasm.
  Useful once the machine boots to the Finder (Q7+) for trap/Device-Manager/HFS
  validation; **no leverage on the current boot-handoff hang** (Retro68 code
  runs on top of an already-booted Mac OS). Heavy `build-toolchain.sh` setup.

## Backlog / known simplifications (each must move to a milestone or be
documented in DEV.md when its subsystem lands)

- `MacMemory`: SCC/IWM/SCSI reads return constants; open bus returns $FF;
  no bus-error generation for empty regions yet.
- `Via6522`: timers don't count; shift register inert; IFR edge semantics
  minimal. ORB reads don't model RTC lines.
- `MacVideo`: whole-frame decode at frame end — no beam position, no
  PA6 alternate-buffer switch yet.
- `main.cpp`: no input, no audio, no Ui class split, no save-states.
- GUI `demoMode` flag is compile-unit static — fold into a Ui/status struct
  when the Ui class lands.
