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
- [ ] **M6 — Sound** (PWM sample buffer at ramTop-$300, miniaudio host —
  POMIIGS `Audio` pattern).
- [ ] **M7 — SCC 8530** (port POMIIGS) + **SCSI NCR 5380** + HD20/SCSI disk
  → boot System from disk image.
- [ ] **M8 — Etalons**: pixel-perfect screenshot regression suite
  (NeoST `run_etalons.py` pattern); WASM build.

### Phase 2 — Mac LC II (68030 + MMU, functional accuracy)

The 68030 execution core does not exist in Moira (disassembler only). Build
it **from the Motorola MC68030 User's Manual, not from UAE's generated code**,
with an AI loop verified by differential testing:

- [ ] **O1 — Oracle A**: WinUAE 68030 core (cpuemu + mmu030) compiled as a
  `.so` with a `set_state / step / get_state` C API.
- [ ] **O2 — Oracle B**: MAME/Musashi 68030 core, same API. Disagreement
  between oracles resolves by majority vs. our core + manual reading;
  **spec/oracle conflict → oracle wins** (undocumented flags etc.).
- [ ] **O3 — Fuzzing harness**: Python generator of random states + MMU
  translation trees (valid/invalid/cascaded descriptors — PTEST/PLOAD need
  real tables in RAM); SingleStepTests JSON as the exchange format.
- [ ] **O4 — Instruction fill-in**: 68020→68030 delta in Moira's exec layer
  (mostly PMOVE/PTEST/PFLUSH + MMU-aware bus layer + format $A/$B bus-error
  frames with instruction continuation).
- [ ] **O5 — FPU 68881/68882** (LC II PDS option): softfloat-based, fuzzed
  against the same oracles.
- [ ] **O6 — LC II machine**: V8 gate array memory map, 512×384 (or VGA)
  video, 68030 @ 15.6672 MHz, functional timing.

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
