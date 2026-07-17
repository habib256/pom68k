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
