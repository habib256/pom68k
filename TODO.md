# TODO

Active work only. Completed milestones, resolved bugs, investigation trails and
design rationale belong in `CHANGELOG.md` (with implementation detail in
`DEV.md` and the vendor notes).

## Current priority — Quadra 605 / LC 475

- [x] **Reproduce and close the Finder 256-color issue.**
  - Boot the FF7439EE ROM with `hdv/MacOS-8.1-boot.vhd`.
  - Select Monitors & Sound → 256 colors and capture
  `q605_trace --dafb-io N`.
  - Verify the live DAFB base, stride, mode and CLUT through the transition.
  - If the guest still crashes, trace the first exception and offending DAFB
  access; do not infer a guest fix from correct host rendering alone.

- [x] **Finish and validate the IOSB ASC stereo implementation.**
  - [x] Land the `$BB` IOSB variant with FIFO A/B, 22.257 kHz drain, level IRQ and
  stereo host output.
  - [x] Gate register/FIFO/IRQ semantics with `q605_asc_test`.
  - [x] Finder boot reaches 640×480×8 with ASC `$BB` / stereo FIFOs armed;
  audible host validation remains a manual GUI check.

- [x] **Implement a real SWIM2.**
  - [x] Replace the all-zero stub with the MAME-compatible reset/register
  file, rotating timing parameters, two-entry data/mark/CRC FIFO,
  error/handshake semantics and PrimeTime D15-D8 word mapping
  (`swim2_test`). The no-media read/write shifter is clocked at C15M so the
  ROM speed calibration completes; IOSB's five-cycle SWIM wait state is also
  modeled and gated. `q605_boot_etalon` proves no boot regression.
  - [x] Connect the register core to drive selection, CA phases/LSTRB, HDSEL,
  GCR/MFM setup bits and 1.44 MB MFM media (`SonyDrive` SuperDrive path +
  `Swim2::attachDrive`); two drives wired into `Q605Memory`.
  - [x] Preserve 800K GCR compatibility; media gates `swim2_media_test` and
  `q605_floppy_boot_etalon` (unit-level sector0 read; full ROM floppy boot
  still open — SCSI remains the default Quadra path).

- [x] **Make the real 68LC040 no-FPU configuration usable.**
  - [x] Reproduce with `POM68K_Q605_NOFPU=1`: bare `FPUModel::NONE` reaches
  SysError 90 (dsNoFPU) at `$40802A38` (MBState) — PACK 4 F-line glue is not
  FPSP; format $4 and format $0 both fail without Apple's FPSP.
  - [x] Confirm with `rominfo` that FF7439EE contains two `PACK 4` resources;
    their presence alone therefore does not prove that the System installs FPSP.
  - [x] `POM68K_Q605_NOFPU=1` selects M68LC040 + soft 68882 (SoftwareFPU-
  equivalent) so Finder stays reachable under the LC CPU identity; gated by
  `q605_nofpu_boot_etalon`.
  - [ ] Follow-up: bare `FPUModel::NONE` + real FPSP (or host F-line emulator
  that still fails the ROM `fnop` probe) without the soft 68882.

- [x] **Validate** `q605_boot_etalon` **with the user assets.**
  - [x] Add the soft-skipping gate with menu/desktop metrics, 640×480×8 DAFB
  geometry and a minimum SCSI command
  count, following `lcii_boot_etalon`.
  - [x] Soft-skip cleanly when the user-provided ROM or disk image is absent.
  - [x] Run it with FF7439EE + `MacOS-8.1-boot.vhd`: Finder reached at
  640×480×8, DAFB mode 3, base `$1000`, stride 1024, menu luminance 204,
  desktop 141 and 5,002 SCSI commands.

- [x] **Improve Quadra performance without changing architectural results.**
  - [x] Add an ATC/translation fast path (separate I/D, 32 entries) measured
  against walk-per-access (`POM68K_MMU040_WALK=1` disables it); `sst68040` and
  `q605_boot_etalon` stay green.
  - [x] Model enough 040 i-cache behaviour for a throughput overlay on
  `Cpu040` (`POM68K_Q605_CACHE_BOOST`, default 1 — boost 4 broke SCSI bring-up).
  - [x] Calibrate `CACHE_BOOST` > 1 against `q605_boot_etalon`: boost 2/3/4
  all fail with SCSI=0 (hang ~`$408BA0EE`). Default stays **1**. Stall /
  viaSync / syncSwimFromCpu are now boost-invariant; a future milestone
  may raise the default once Cuda/SCSI relative timing tolerates it.

- [ ] **Replace remaining Quadra HLE shortcuts where fidelity matters.**
  - Remove the LocalTalk LAP watchdog by modeling the required SCC/timeout
  behaviour.
  - Expand Cuda commands only from ROM/driver traces.
  - Add accurate 040 timing, cache copyback/snooping and on-chip-FPU/FPSP
  behaviour as separate, oracle-gated milestones.
  - (Follow-up from Q8.8) Make `CACHE_BOOST` > 1 Finder-safe without
  changing etalon metrics.



## Mac LC II

- [ ] **Fix the no-FPU SANE path.**
  - [x] Boot with `POM68K_NOFPU=1` and capture the failure: stock Mac LC
  UniversalInfo `$DC00` → `$F200` Line-F → SysError dialog `$40A02A38`.
  - [x] Confirm `rominfo`: two `PACK 4` resources; FD/`$CC00` LC II-shaped
  entry exists; Mac LC record is what VIA PA `$D4` currently selects.
  - [x] Prove `$CC00` overlay (and soft-FPU+`$CC00`) hangs StartInit at
  `$A49A7A` (D7 bit 16 clear; bit 16 set only after FPU success path).
  - [ ] Select the real no-FPU UniversalInfo / defaultRSRCs path so PACK 4
  installs without the bit-16 gate; Finder under bare `FPUModel::NONE`.

- [x] **Fix the GISTPERSO/SimCity 2000 startup race.**
  - Reproduce the normal boot heap corruption with
  `LCII_FPU=1 lcii_trace --scsi hdv/GISTPERSO-boot.vhd`.
  - Trace heap writes from Finder startup to the first corrupt block header.
  - Differential-check the responsible CPU/MMU window against WinUAE.
  - Keep Shift/Option boot only as a temporary user workaround.

- [ ] **Complete LC II storage and sound devices.**
  - Add SWIM ISM/MFM support for 1.44 MB media.
  - Finish DFAC/sound-out behaviour and host-clock resampling.
  - Verify long-running audio tempo under GUI load.

- [ ] **Close deferred LC II bus and timing gaps.**
  - [x] Add an SCC 16-bit fast path so word accesses do not double-advance the
  register pointer (`scc_ext_test`).
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
  `docs/HLE_OVERLAY.md`.**
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

- [ ] **Macintosh II → Finder** (in progress). POST green (`macii_post_etalon`).
  HMMU + SCSI PDMA window fixed; StartBoot `wantType` forced to 1 so the
  Apple_HFS path runs. Still open: `Apple_Driver43` on `HD20SC` JSRs but
  never fills `DrvQHdr` (Plus boots the same image) — gate
  `macii_boot_etalon` stays red (gray floppy / menu≈0.50).

- [ ] **Add the original Macintosh LC (68020)** as the next low-cost profile,
  reusing V8/Egret/ASC and validating multi-machine parameterization.

- [ ] **Expand the nearby 68030 family** in this order: Classic II, Color
  Classic, LC III, then IIsi. Implement Sonora/RBV/Cuda/SWIM2 variants as
  separate tested devices.

- [ ] **Expand the nearby 68040 family** after Quadra polish: LC/Performa 475,
  LC 575, then Quadra/Centris 610/650/800.

- [ ] **Implement NuBus and slot video** before Mac II, IIx/IIcx/IIci or the
  NuBus Quadras.

- [ ] **Treat PowerBook power management, IIfx IOPs and 660AV/840AV DSPs as
  independent major projects.** Scope and effort are tracked in
  `docs/68K_FAMILY_SCOPE.md`.
