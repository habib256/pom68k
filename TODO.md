# TODO

Active work only. Completed milestones, resolved bugs, investigation trails and
design rationale belong in `CHANGELOG.md` (with implementation detail in
`DEV.md` and the vendor notes). The LLE-vs-HLE inventory and migration plan
live in `docs/LLE_VS_HLE.md`.

## Current priority — Finder matrix Phase C (new machine profiles)

Phase A/B are **done**: Plus / Mac II / LC II / Quadra 605 all reach the
Finder on every compatible System image (result table + fixes:
CHANGELOG 2026-07-21 "Finder matrix Phase A complete"). Goal now: extend
profiles until every **68k** ROM under `roms/` (not PPC 4 MB) has a matching
machine, one Finder cell per new profile before the next.

Harness: `tests/finder_boot_matrix.cpp` (`make finder_boot_matrix`); flat
HFS → SCSI façade in `ScsiDisk::open` (gate `scsi_hfs_facade_test`; offline
bake `tools/wrap_hfs.py`).

Assets (local; do not commit — `hdv/` is gitignored):
- Infinite Mac copies in `hdv/`: System 4.1 (floppy), 5.1 / 6.0 / 6.0.8 /
  7.0 / 7.1 / 7.5 / 7.5.5 HD `.dsk`, plus existing `HD20SC.vhd`,
  `boot.vhd` / `GISTPERSO-boot.vhd`, `MacOS-7.6-boot.vhd`,
  `MacOS-8.1-boot.vhd`.
- Full Infinite Mac tree also at `../refs/infinite-mac/Images` (or
  `/home/gistarcade/src/refs/infinite-mac/Images`). If a file is missing,
  fetch with **Scrapling** (not raw `curl` through the sandbox proxy):
  `Fetcher.get` / `scrapling extract get` on
  `https://raw.githubusercontent.com/mihaip/infinite-mac/main/Images/…`.

Remaining Phase A odds and ends:
- [ ] Plus floppy System 4.1 cell via `insertDisk` (all HD cells PASS).

Phase C — new profiles, order from `docs/68K_FAMILY_SCOPE.md`; each profile
gets at least one Finder cell before the next:
- [ ] Macintosh LC (68020) — V8 reuse; ROM `350EACF0`.
- [ ] Classic II → Color Classic → LC III → IIsi (030 / Egret cluster).
- [ ] SE / Classic (68000 + ADB) for 256/512 KB compact ROMs.
- [ ] Nearby 040 (LC 475 identity, then Centris/Q610…) for other 1 MB ROMs.
- [ ] Explicitly **out of Phase C**: PowerBook PMU, IIfx IOPs, AV DSP, all
  4 MB PPC ROMs.

## LocalTalk between two POM68K instances (virtual LLAP cable)

Feasible, and a natural sequel to LLE step 3 (CHANGELOG 2026-07-21) —
SCC emission/reception is now honest. Scheme: wire one instance's SCC Tx
to the other's Rx over a host transport (local UNIX socket, TCP, or UDP
multicast; two physical machines work the same way, just a network
socket).

- Tx side already serialises SDLC frames (the LLAP ENQ probes prove it);
  encapsulate frame-by-frame — the EOM/Underrun latch gives the frame
  boundaries.
- Rx side is the bulk of the work: the reverse path in `Scc8530` —
  inject a frame into the Rx FIFO, drop Sync/Hunt (carrier present =
  line busy), raise the Rx interrupts and the end-of-frame CRC result.
  Today's carrier sense always says "line free" since nobody transmits
  on the far end.
- Honour the LLAP RTS/CTS dance for directed frames; address-acquisition
  collisions can stay unmodelled as long as the two nodes draw different
  random addresses.

Precedents: Mini vMac does exactly this (LocalTalk over UDP between
instances); Basilisk II/SheepShaver bridge AppleTalk differently.

Payoff: Chooser sees the other Mac, AppleShare file sharing between a
Quadra under OS 8.1 and a Mac Plus under System 6, and a real test bench
for the SCC/LLAP layer.

First milestone: LLAP frame encapsulation over UDP + full Rx path in
`Scc8530`, gated by a two-instance test where each node sees the other's
ENQ probes.

## LLE fidelity — replace HLE shortcuts (see `docs/LLE_VS_HLE.md`)

- [x] **Mac II ADB — real PIC1654S: DONE, now the DEFAULT** (CHANGELOG
  2026-07-22 "Mac II LLE ADB default"). The mouse moves end-to-end over the
  real firmware (`macii_mouse_trace` PASS); `POM68K_ADB_LLE=0` keeps the
  HLE fallback. Follow-ups that remain:
  - [ ] Retire the HLE `AdbVia` byte-model once a few more machines run
    LLE ADB (it is still the no-dump fallback).
  - [ ] `AdbLine` device model: second mouse button / extended-keyboard
    handler IDs, and exercise Listen R2 (LEDs) paths.
- [ ] **Quadra 605 / LC 475** shortcuts where fidelity matters:
  - Expand Cuda commands only from ROM/driver traces.
  - Add accurate 040 timing, cache copyback/snooping and on-chip-FPU/FPSP
    behaviour as separate, oracle-gated milestones.
  - (Follow-up from Q8.8) Make `CACHE_BOOST` > 1 Finder-safe without
    changing etalon metrics.
- [ ] **Cuda wire-model redo** (follow-up to the solved bare no-FPU
  enigma — CHANGELOG 2026-07-21 "Bare no-FPU solved"): replace the
  per-reader reply-framing accommodations in `Egret.cpp` (echo-slot
  data duplication for ReadXPram, the $76 pop, the GetPram erase, the
  long/short tick heuristic) with the real Cuda packet
  `[type, flags, cmdEcho, data…]` + a *clocked* attention byte and
  per-byte SR scheduling. Full blueprint (framing, error packets,
  61/71/88 µs timings, failed-attempt notes) in `docs/LLE_VS_HLE.md`
  §1.6b; migration step 7 there. Needs re-pinning every ROM reader
  (`$408A9BBE` ISR, `$408B3Bxx` direct pollers, LC II `$A14D4E`)
  against that wire.

## Mac LC II

- [ ] **Fix the no-FPU SANE path** (diagnosis done — CHANGELOG 2026-07-20
  O6.13): select the real no-FPU UniversalInfo / defaultRSRCs path so PACK 4
  installs without the D7 bit-16 gate; Finder under bare `FPUModel::NONE`.

- [ ] **Confirm the GISTPERSO/SimCity 2000 startup race is closed.** TODO
  had it checked, but CHANGELOG (2026-07-18) records only the root-cause
  analysis and the Shift/Option workaround — re-run the headless repro and
  either land the fix entry in CHANGELOG or reopen the differential hunt.

- [ ] **Complete LC II storage and sound devices.**
  - Add SWIM ISM/MFM support for 1.44 MB media.
  - Finish DFAC/sound-out behaviour and host-clock resampling.
  - Verify long-running audio tempo under GUI load.

- [ ] **Close deferred LC II bus and timing gaps.**
  - Compare interrupt, VBL, VIA and memory timings with real hardware.
  - Diagnose the idle screen dim seen after very long runs.

- [ ] **Close remaining 68030/MMU/FPU oracle gaps.**
  - RTE format `$A/$B` instruction restart.
  - PMOVE-through-translation fault frames.
  - Instruction-stream fetches across page boundaries.
  - FMOVEM indirect-EA read order.

## Mac Plus

- [ ] **Finish VIA/RTC accuracy.**
  - Model 6522 T1/T2 ±1-cycle reload/IFR latency.
  - Add VIA E-clock access alignment and IACK E-cycles.
  - Persist PRAM and seed GUI RTC time from the host while keeping tests
    deterministic.

- [ ] **Complete floppy support.**
  - Add 800K write support, external-drive selection and eject/insert UI.
  - Implement keypad/arrow `$79`-prefix handling where required by M0110 input.

- [ ] **Improve classic sound accuracy.**
  - Fetch the sound buffer per scanline instead of once per frame.
  - Model the disk-PWM byte and the analog volume curve.

- [ ] **Finish NCR 5380/SCSI and serial support.**
  - Support multiple targets/LUNs and correct REQUEST SENSE after CHECK
    CONDITION.
  - Implement usable SCC serial ports rather than only mouse/LocalTalk paths.

- [ ] **Add pixel-accurate etalons and a WASM build.**
  - Create a screenshot regression runner for the Plus boot/Finder paths.
  - Keep asset-dependent tests soft-skippable.

## Cross-machine architecture

- [ ] **Implement save states.**
  - Version CPU, MMU/FPU, memory and every device state.
  - Include mounted-media identity and reject incompatible machine profiles.
  - Stamp active non-conformant HLE modules in the state.

- [ ] **Build a 68k-to-host JIT.**
  - Use WinUAE's JIT as a code reference, not imported generated code.
  - Differential-test every compiled block against the oracle-converged
    interpreter using SST030/SST040 state formats.
  - Handle MMU faults, interrupts, self-modifying code and cache invalidation
    before enabling it by default.

- [ ] **Build the optional HLE acceleration overlay described in
  `docs/HLE_OVERLAY.md`** (after the `docs/LLE_VS_HLE.md` cleanup pass).
  - Start with one hidden `boot.checksum` address hook and an HLE-forbidden
    accuracy-test mode.
  - Add signature-matched modules, per-module A/B gates and a visible
    non-conformant-mode indicator.
  - Prioritize disk HLE; defer timing-loop elision until its overlap with the
    JIT is understood.

- [ ] **Evaluate Retro68 as a guest-level differential oracle.**
  - Build small Toolbox/Device Manager/XPRAM probes.
  - Run identical binaries under MAME and POM68K and compare results.

- [ ] **Refactor the remaining GUI globals.**
  - Move compile-unit state such as `demoMode` into a machine/UI status object.
  - Keep machine threads, command queues and Emscripten's single-thread path
    behaviourally aligned.

## Future machine profiles

Driven by **Phase C** of the Finder matrix above; detail and effort tiers in
`docs/68K_FAMILY_SCOPE.md`.

- [ ] **Macintosh LC (68020)** — next low-cost profile (V8/Egret/ASC reuse).

- [ ] **Nearby 68030**: Classic II → Color Classic → LC III → IIsi
  (Sonora/RBV/Cuda/SWIM2 as separate gated devices).

- [ ] **Nearby 68040**: LC/Performa 475 identity, LC 575, then
  Quadra/Centris 610/650/800.

- [ ] **NuBus + slot video** beyond Mac II Toby: IIx/IIcx/IIci and NuBus
  Quadras.

- [ ] **Independent majors**: PowerBook PMU, IIfx IOPs, 660AV/840AV DSP.
