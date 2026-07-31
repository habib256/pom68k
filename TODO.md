# TODO

**Active work only.** Resolved work, investigation trails and design rationale
live in `CHANGELOG.md` (implementation detail in `DEV.md`, vendor notes in
`extern/*/POM68K_VENDOR.md`, LLE inventory in `docs/LLE_VS_HLE.md`, JIT design
in `src/jit/POM68K_JIT.md`).

**Counts verified 2026-07-31** — re-verify before quoting them anywhere:
- **129 CTest gates** (`ctest -N`): 59 `unit`, 8 `smoke`, 16 `jit`, 30 `m040`.
- **32 machine profiles** = 32 tags in `SnapMachine`, `src/SaveStateMachines.h:47-68`.

House rule for this file: an item earns its place by saying **what to do next**,
concretely. When it lands, it moves to `CHANGELOG.md` and leaves at most one
line here. **Every unchecked box below was re-verified against the code on
2026-07-31.**

---

## 1. Red now

### `q605_cudalle_key_etalon` — RED, real bug, not a regression

Not in `-L smoke` nor `-L jit`, which is why the working loop never caught it.
Fails identically at `cda7bc2` **and** at `b472a7b` (the commit that introduced
the gate): `Ticks 8905 -> 11031, KeyMap SILENT`.

Ruled out, each measured: not a wedge (Ticks advance, SCSI plateaus at the same
5236 commands as the passing gates); not the Cuda wire (silent on firmware LLE
*and* on `POM68K_CUDA_LLE=0`); not the `peek8`-is-physical trap (`TC = 0`, so
the physical read *is* the guest view); not a missing device
(`AdbLine::keyboardAddr() == 2`, `ADBBase` `$0CF8` = `$00006DD0`); not a dirty
volume (`drVolAtrb = $0100`); not the too-wide window (all 16 bytes read `00`).

ADB side traced CLEAN (`POM68K_ADB_LLE_TRACE=1`, 397 k lines): 2906 Talk R0 to
address 2, 829 keyboard reports emitted, every one followed by TREQ fall →
session → `via: SR byte`. R0 byte order, Talk R3 identity and Listen R3 rules
all match `macadb.cpp:656-777`. Guest side: `KeyTime` ($0186) advances in
lockstep with the keystrokes, but `KeyLast` ($0184) stays 0 and KeyMap stays
zero. **The break is inside the guest, between the ADB keyboard driver and the
Event Manager.**

2×2 probe (`tests/adb_key_probe.cpp`, `POM68K_PROBE_MACHINE/_IMG/_FRAMES`):

| cell | KeyMap | Cmd-N repaints |
|---|---|---|
| LC II + System 7.5 (control) | live | yes |
| Quadra 605 + System 7.6 | live | no |
| Quadra 605 + Mac OS 8.1 | dead | no |

So the **Quadra hardware path is exonerated** — same machine, same Cuda, same
`AdbLine`, keys land under 7.6 and not under 8.1. The 8.1 failure is
System-coupled.

- [ ] **Next: trace whether the guest reads keyboard R2 at all on the Quadra**
  (`cmd=2A` in the ADB trace). Talk R2 modifiers are now implemented
  (`AdbLine.cpp:37,45-58,313` — live bitmap, active low, in the save-state
  chunk), closing a genuine oracle divergence against `macadb.cpp:694-700`, but
  Cmd-N on the Quadra still does not repaint. If R2 is never polled, the
  modifier state comes from the R0 key stream and the bug is in how we report
  the Command keydown itself.
- [ ] **Fix the gate's own defects** once the bug is understood: narrow the
  KeyMap window from 16 bytes to 8 (`pom68k-false-green-wide-assert`), consider
  asserting on pixels instead, and **register it in a tier that runs** — a gate
  nobody executes is not a gate.
- [ ] **Second, independent suspicion**: on the GUI Quadra, typing in a Netscape
  text field beeps per letter (a beep per *rejected* key — consistent with the
  above) but the beeps sound wrong and differ per letter. Suspect the ASC
  rendering from stale FIFO residue; `q605_asc_test` covers registers/IRQ, not
  audible output.

Methodology notes, both paid for twice: `KeyLast` ($0184) moves in **no** cell,
including working ones — it is not a usable observable; and a VRAM-hash probe
reported "keys lost" on a cell where KeyMap proves they arrive, because letters
select nothing on a bare desktop. **Believe an observable only after it has
demonstrated sensitivity.**

### The Cuda↔VIA bit-bang transport is phase-fragile

- [ ] `M68hc05::serviceInterrupts` still returns **0** where hardware charges 11
  cycles (`m6805.cpp:570`) — a deliberate, gated inaccuracy documented at
  `src/M68hc05.cpp:160-177`. Charging the 11 costs the MCU ~2 % of its
  instruction throughput against machine time; that phase shift deadlocks the
  **Mac TV** (31.3344 MHz, tightest MCU:CPU ratio in the tree): the Cuda stops
  after 7 of a byte's 8 CB1 edges waiting for BYTEACK while the ROM spins on
  VIA1 IFR.SHIFT at `$40AB3BC8`.
  **The real fix is to make the transport survive a phase shift**, then re-land
  the 11 (a one-line change: `return 11`). `CudaLle.cpp:20-28` records two
  earlier attempts to move the same phase (uniform 2 µs interleave, busy-gated)
  crashing the guest — treat the MCU/VIA lockstep as the thing under test, not
  a free parameter. Already ruled out: the one-second-timer clock model, and a
  stale external bit counter across an ACR shift-mode change in `Via6522`.
  Always run `mactv_boot_etalon` after any MCU timing change.

---

## 2. Test & validation depth — the single biggest gap

The gates prove **boot**, not **use**, and the machine fan-out made the ratio
worse. Of the **32 profiles** covered by the **129 gates**, only **7** have any
gate past the Finder signature: LC II (`lcii_soak/persist/launch/floppy_etalon`
+ `lcii_savestate_etalon`), Quadra 605 (`q605_cudalle_mouse/key_etalon`,
`q605_cdrom/cdboot_etalon`, `q605_savestate_etalon`, `q605_ot_bind_etalon`),
Mac II (`macii_mouse_etalon`), **Mac Plus** (`input_etalon` — mouse
quadrature + M0110 keys), and the four input gates from 2026-07-29
(`lc3_`, `lc520_`, `iivx_`, `iisi_input_etalon`). That is EIGHT profiles
with any gate past the Finder signature; enumerate them with
`ctest -N | grep -E 'input_etalon|beyond|mouse_etalon|key_etalon|savestate_etalon'`
rather than trusting this sentence. **The other 24 profiles are
boot-to-Finder signature only.** A machine can pass its etalon and still be
useless for real work.

Highest-ROI closers, in order:

- [ ] **Soak + persist on a second machine (Quadra 605 / Mac OS 8.1).** The LC
  II template is `tests/lcii_beyond_etalon.cpp` (`CMakeLists.txt:875-878`, one
  gate per scenario). A Q605 persist run also exercises the 53C96 **WRITE**
  path end to end, which nothing else does. Blocked on nothing but the work —
  the old "once save states land" blocker is gone (save states shipped
  2026-07-30).
- [ ] **Floppy: a guest-INITIATED write.** `lcii_floppy_etalon` proves
  mount+read but asserts nothing about writing, because no gesture has yet made
  the guest write to the medium. Evidence 2026-07-29: the volume mounts
  read-write (`isWriteProtected() == 0`) and its window auto-opens (changed
  region x 3..494 / y 2..240), but a Cmd-N in that state modifies **neither**
  the floppy nor the hard disk — while the identical Cmd-N in the `persist`
  scenario works on the HD. So the keystroke is dropped in the post-insert
  state, not landing on the wrong volume. Next: vary the settle time before
  Cmd-N (stuck modifier vs Finder still busy); if the UI route stays flaky,
  drive the write from a script/app on the boot volume. Device-side
  write→eject→flush is already gated by `floppy_persist_test`.
- [ ] **Widen per-machine System coverage.** Each profile's etalon pins one
  reference image (`GISTPERSO`, Infinite Mac 8.1…). `finder_boot_matrix`
  currently accepts only four machines (`tests/finder_boot_matrix.cpp:328-333`
  — `plus`/`macii`/`lcii`/`q605`); every profile added since Phase C has no
  matrix cell. This is the calcify-around-one-image trap `LLE_VS_HLE.md` warns
  about. Add cells as images are validated — Classic II, LC, Color Classic,
  LC III and the AIO family are the oldest debts.
- [ ] **Plus floppy System 4.1 cell.** `bootPlus`
  (`tests/finder_boot_matrix.cpp:136-155`) only does `attachScsi`; the 4.1 cell
  needs an `insertDisk` path. All HD cells PASS.

**Per-machine LLE-completeness estimates** (±5 pts, re-scored 2026-07-25):
Plus ~85, Q605 ~80, LC II ~80, LC 475/575 ~79, Mac II ~78, LC / LC III/III+
~75, Centris/Quadra 610/650 ~74, Classic II / LC 520 family ~73, IIx/IIcx ~73,
IIvx/vi ~72, IIsi / IIci ~70, Color Classic ~70, Mac TV ~70. Common ceilings:
whole-frame video everywhere, cycle-exact CPU only on the Plus, and the
beyond-boot gap above. Lowest scores are **freshness** (booted-once, not
hardened — RBV / Tinker Bell / VASP / AIO).

---

## 3. JIT — second execution engine

Landed and documented: J0/J1 (engine seam, backends, fetch window, block cache),
J2 (x86-64 code generator), J3 (inline DTLB), block linking, 030 + 020 seams,
GUI engine switch on every family, PGO training per CPU family. Design and
measurements: `src/jit/POM68K_JIT.md`; per-item history in `CHANGELOG.md`
(2026-07-27 → 2026-07-31).

Current state: engine available on every 020/030/040 machine; the **x86-64
backend is declared 68040-only** (`BackendCaps::guestFamilies`, `JitBackend.h §
GuestFamily`) so `auto` gives the 030s `threaded`. Best measured figure: Q605
boot etalon **61.3 s interpreter → 22.9 s JIT (×2.68)**.

Open, in ROI order:

- [ ] **Widen the x86-64 backend to the 68030 family.** The divergence is
  **localized**: with `POM68K_JIT_ACCESS_THUNK=0` — which hands every
  memory-touching instruction back to the interpreter — x64 boots the LC II to
  the Finder, so what breaks is the natively-compiled memory-access path, not
  the emitters. Work items, in order:
  - **an `lcii`/x64 lockstep gate FIRST.** `jit_lockstep_test` and friends run
    two **Quadra 605** machines, so an 030 code generator has no differential
    coverage at all — and this bug is exactly what that costs.
  - a 68030 branch in `pomJitProbeData` (it returns false below `M68EC040`
    today, so the inline DTLB never fills on an 030 at all);
  - model-correct access thunks: `pomJitReadData`/`pomJitWriteData` call
    `mmu040Read`/`mmu040Write` unconditionally;
  - the 030's `(An)+` update order (before the access, not after —
    `MoiraDataflow_cpp.h:326-332`), its restartable last write (`:355-361`), and
    the end-of-instruction prefetch refill that makes `queue.irc` mean something
    different at a block exit.
- [ ] **Coverage tail** (after MOVEM/DBcc/JMP): line $E shifts/rotates (0.9 %),
  Scc (`JitBackendX64.cpp:1819` refuses it), PEA; **the 68020 indexed modes are
  the big block** (a brief extension-word decoder — QuickDraw's blitters;
  currently refused at `JitBackendX64.cpp:36,171,175`). MULU/DIVU stay fallback
  (data-dependent cycles — the cross-check refuses them honestly).
- [ ] **Compact `mmu040InstrStart`.** Eight per-instruction field resets + a
  `getCCR()` pack; adjacent fields could collapse into one or two wide stores.
  Small, but it sits on every single 040 instruction.
- [ ] **aarch64 backend.** Porting note already written and validated against
  the IR: `src/jit/backends/JitBackendA64.md`.
- [ ] **Generated-code density** — deprioritized 2026-07-30. The stale
  150 B/instr figure predates boundary-deferral + cold emission; the re-baseline
  shows x64 already beating threaded on both regimes (−10 %). The residual
  idle-Finder gap is dominated by the ATC-eviction exactness contract, which
  density cannot touch. Micro-wins (local rel8, shared stubs) are third-order.

### Measured and DROPPED — do not re-open without new data

These cost real time to measure. The numbers, not the conclusions, are the
value here.

- **Lazy condition codes in x64: ceiling ≈0.8 %.** Method: duplicate the flag
  emission (storing the same byte twice is a semantic no-op, so the delta is the
  marginal cost of one full materialisation set). Q605 boot on the x64 backend
  26.13/26.15 s → 26.37/26.30 s. Lazy CC can only remove the *dead* subset of
  that. **Measurement trap**: the first published figure (2.5 %) used
  `POM68K_CPU_ENGINE=jit` alone, which selects **threaded** — the modified x64
  code never ran. Any x64 measurement must set `POM68K_JIT_BACKEND=x64`.
- **Page-granular dispatch tables for the memory maps: premise was wrong.**
  Accesses by destination over an LC II boot (1 475 M): RAM 69.6 %, ROM 29.3 %,
  I/O 0.33 %, other 0.80 %. 99 % land on RAM/ROM, already 2-4 perfectly
  predicted compares; a 4 KB table would put a **dependent load** in front of
  the 99 % case to save branches that cost nothing. Re-open only with a profile
  showing decode as a real share; the honest lever for I/O-heavy code is
  per-device caching.
- **O(1) ATC lookup for `mmu040Translate`: flat.** callgrind put it at 38.6 % of
  the interpreter, so a 256-entry direct-mapped hint table went in front of the
  32-entry scan (bit-identical by construction). Interpreter 213.2 vs 213.2 s
  (5 G), 906.7 vs 903.9 s (20 G), x64 98.9 vs 97.6 s. The single-entry
  `last[write]` memo already catches the hot case. **The 38.6 % is real but it
  is the WALK and per-access bookkeeping, not the lookup** — re-open only with a
  profile that separates them.
- **The interpreter's data window (J1c) is opt-in, not open work.** It exists
  (`JitEngine.cpp:50`, `POM68K_DATA_WINDOW=1`, `POM68K_JIT.md §8`); capping it
  at ATC coverage for bit-exactness made it a net loss on the interpreter. The
  x64 keeps its inline TLB.

Invariant worth restating: **derived state dies with the ATC entry it came
from** (`pomJitAtcEvict`). A window hit must imply the interpreter would have
ATC-hit too, or the engines walk different subsets of the page tables — and
walks write the descriptor U bit, which Mac OS VM reads for page aging. That
class of divergence mimics memory corruption; see CHANGELOG 2026-07-28.

---

## 4. LLE fidelity — replace HLE shortcuts

Inventory and migration plan: `docs/LLE_VS_HLE.md`. Policy settled 2026-07-29
(§2): HLE fallbacks are **kept but LOUD** (stderr NON-CONFORMANT notice at every
HLE ADB entry) because MCU dumps are non-distributable; deletion would be a
deliberate "POM68K requires MCU dumps" product decision, not a cleanup.

- [ ] **`AdbLine` device model gaps**: second mouse button, extended-keyboard
  handler IDs, and Listen R2 (LEDs). *(Talk R2 modifiers landed 2026-07-31 —
  see §1.)*
- [ ] **Quadra 605 / LC 475**: expand Cuda commands only from ROM/driver traces;
  accurate 040 timing, cache copyback/snooping and on-chip-FPU/FPSP behaviour as
  separate oracle-gated milestones.
- [ ] **SCC LLE, Low tier** (2026-07-22 MAME `z80scc.cpp` audit,
  `docs/LLE_VS_HLE.md` §3): true bit-serial Tx/Rx sampling (only worth it with a
  real async transport to talk to); WR5 RTS output tracking (auto-RTS deassert
  on all-sent, `tra_complete :1100`); SDLC Rx residue codes (RR1 bits 3-1);
  chip-variant gating (NMOS 8530 / 85C30 / ESCC — FIFO depth 3/8, WR7', status
  FIFO `:1363`, needed the day a machine wants the ESCC); WR9 VIS/NV options
  (hardcoded VIS=1, correct for every Mac target); DPLL (MAME stubs it too,
  `:305-318`).
  **Caveat that applies to the whole SCC backlog**: MAME's SDLC side is partial
  (Send Abort / CRC resets `:1602/:1635` "not implemented", no EOM latch, no
  hunt/sync) — for LLAP behaviours **we are the more complete model**. Use MAME
  as oracle for the ASYNC side only; do not regress LLAP chasing parity.
- [ ] **Classic II Eagle `$F18000`**: identify the real block. The ROM writes
  `$1E` then reads a pointer at +$38 — served as open bus today, gated on
  UnivROMFlags bit 7.
- [ ] **ADB report rate.** The 2026-07-25 bus-time correction made reports
  coalesce enough for System 7's mouse scaling to amplify a sustained stream
  ~1.6× (`lcii_launch_etalon` now steers closed-loop instead of assuming 1:1).
  Single moves land within one frame, 1:1 — but measure the actual
  autopoll-to-guest report rate against the Egret's ~90 Hz and check we are not
  servicing fewer polls than real hardware.

---

## 5. Per-machine backlogs

### LC II / V8

- [ ] **1.44 MB guest-level mount/boot etalon.** The SWIM1 controller is done
  (IWM+ISM personalities, 1-0-1-1 switch, param RAM, MFM read/write through the
  cell engines — CHANGELOG 2026-07-23, gate `swim1_test`); what is missing is a
  guest-level gate. Asset on hand: `disks35/Stuffit_Expander_5.5.dsk`.
- [ ] Finish DFAC / sound-out behaviour and host-clock resampling; verify
  long-running audio tempo under GUI load.
- [ ] Bus and timing gaps: compare interrupt, VBL, VIA and memory timings with
  real hardware; diagnose the idle screen dim seen after very long runs.
- [ ] **Confirm the GISTPERSO / SimCity 2000 startup race is closed.** CHANGELOG
  2026-07-18 records only the root-cause analysis (CPU spins in the ROM Memory
  Manager heap-walk at `$40A0E148` = guest heap corruption during Finder
  startup; PRAM and HFS structure exonerated) and the Shift/Option workaround.
  Re-run the deterministic headless repro (`LCII_HOLD_KEYS` in `lcii_trace`) and
  either land a fix entry in CHANGELOG or reopen the differential hunt. Backup
  image: `hdv/GISTPERSO-boot.vhd.avant-reparation`.
- [ ] **No-FPU SANE.** Solved on the 040 side (CHANGELOG 2026-07-21 "Bare no-FPU
  solved: _FP68K binds the integer PACK 4"; gate `q605_barefpu_boot_etalon`
  reaches the Finder under a true `FPUModel::NONE`). **Unverified whether the
  030/LC II path still needs the same UniversalInfo / defaultRSRCs selection**
  (`POM68K_NOFPU` at `main.cpp:1657,1670`, `Cpu030.cpp:38`) — re-test before
  spending effort here; the original O6.13 diagnosis may already be obsolete.

### 68030 / MMU / FPU oracle gaps

- [ ] RTE format `$A/$B` instruction restart.
- [ ] PMOVE-through-translation fault frames.
- [ ] Instruction-stream fetches across page boundaries.
- [ ] FMOVEM indirect-EA read order.

### Mac Plus

- [ ] **VIA/RTC accuracy**: model 6522 T1/T2 ±1-cycle reload/IFR latency; VIA
  E-clock access alignment and IACK E-cycles; persist PRAM and seed the GUI RTC
  from the host while keeping tests deterministic. *(PRAM file persistence
  exists on the later machines — `main.cpp:1735-1738,1993` — but `MacMemory` has
  no `loadPram`/`savePram`, so the Plus/compact family is the gap.)*
- [ ] **Floppy**: external-drive selection (`Iwm.h:23,63` takes a second
  `SonyDrive*`; `MacMemory.h:147` still passes only the internal one). *(Write
  support, GCR write-back and host file persistence are DONE — CHANGELOG
  2026-07-23 / 2026-07-24, `SonyDrive::setWriteBack`/`flushToFile`, gates
  `iwm_write_test`, `floppy_persist_test`; the eject/insert GUI exists,
  `main.cpp:3422,3667`.)*
- [ ] Keypad/arrow `$79`-prefix handling where required by M0110 input.
- [ ] **Sound accuracy**: fetch the sound buffer per scanline instead of once
  per frame; model the disk-PWM byte and the analog volume curve.
- [ ] **SCSI/serial**: multiple targets/LUNs and correct REQUEST SENSE after
  CHECK CONDITION; a host-side serial transport (PTY/TCP) to make the SCC ports
  *usable*, not just correctly timed. *(WR4 clock mode, WR12/13 BRG, WR11
  routing, WR5 Tx gating, parity/framing, Rx CRC are all DONE — CHANGELOG
  2026-07-23, gates `scc_baud_test`, `scc_engine_test`.)*
- [ ] Pixel-accurate etalons and a WASM build: a screenshot regression runner for
  the Plus boot/Finder paths, keeping asset-dependent tests soft-skippable.

### CD-ROM

Base support DONE 2026-07-29 (`ScsiDisk::openCdrom` — INQUIRY type $05 +
removable, 2048-byte blocks, READ TOC, START/STOP eject, read-only, the Apple
magic MODE SENSE page $30; `attachCdrom(path, id=3)` on all nine multi-target
machines; CLI routes `.iso`/`.cdr`/`.toast`). Gates `scsi_cdrom_test`,
`q605_cdrom_etalon` (8.1 boots from HD, an 8.6 CD mounts as data),
`q605_cdboot_etalon` (no HD → the ROM's 6→0 scan boots the disc, 3913 blocks).
**Layout matters**: the ROM scans 6→0, so the boot volume goes to ID 6 and the
CD to 3, or a bootable disc wins the scan. **8.5/8.6 are PowerPC-only** (8.1 is
the last 68k release) — a black screen on them is correct behaviour.

- [ ] **Install from CD**: the disc boots and the Installer is reachable, but
  driving it to completion (mouse through the Installer UI, then a reboot onto
  the freshly written volume) is not automated. The natural next "real work"
  gate.
- [ ] **Only 2048-byte-DDM discs mount.** `MacOS_86.iso` (`sbBlkSize = 2048`)
  mounts; the hybrid `TIM_3.iso` (`sbBlkSize = 512`) and bare-HFS `.toast`
  images are read (4 blocks of probes) then ignored. Observed, cause not
  established — **it may well be correct** (a real Apple CD driver may require
  its own `Apple_Driver43_CD` partition, which only the 8.6 disc has). Verify
  against MAME or a real drive before "fixing" anything.
- [ ] **CD audio**: READ TOC already reports the data track; CDDA playback,
  PLAY AUDIO/PAUSE and the audio-through-ASC path are absent — no consumer yet.
- [ ] 2352-byte raw rips + `.cue`/`.bin` multi-track (refused today rather than
  mis-read).

---

## 6. Networking

### AppleTalk in-process stack — DONE 2026-07-24, polish backlog

`AtalkStack` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer` (ASP + AFP 2.1 +
`.AppleDouble`), `PapServer` (PAP → CUPS/`.ps`), `MacIpGateway` (DDP-22 +
user-mode NAT), tied by `AtalkHub` with a **Réseau → AppleTalk** window; GUI
default. Gates `atalk_stack_test`, `afp_server_test`, `pap_server_test`,
`macip_gw_test`. Reference: `docs/APPLETALK.md` §6.5.

- [ ] Persist CNIDs across runs (per-session today; a `.AppleDB` or an id in the
  `.AppleDouble` sidecar would survive a reboot, matching netatalk's catalog).
- [ ] AFP corners the subset skips: a proper Desktop database (icons / comments /
  APPL mapping, not the current "no item" stubs), CopyFile, CatSearch,
  DID-relative variable paths, AFP ≥ 3.0 / UTF-8 names.
- [ ] More UAMs than guest/cleartext (DHX / random-number) if a guest refuses
  cleartext.
- [ ] PAP status polling + per-queue `papd.conf`-style config; expose the CUPS
  queue choice in the GUI.
- [ ] MacIP: outbound ICMP (needs raw sockets or a ping helper), IP fragment
  reassembly guest→host, TCP window scaling for big transfers. In-order-only,
  MSS 536 today.
- [ ] GUI: editable fields (share dir / server + printer name / gateway subnet +
  DNS) instead of env-only; a "reveal spool folder" button.
- [ ] A live-boot etalon that mounts the internal AppleShare from a real guest
  Chooser. `q605_ot_bind_etalon` already proves the OT `.MPP` bind under 8.1;
  what is missing is the scripted Chooser drive (same need as the external
  bridge below, and a Sys 7 image).

### LocalTalk between two POM68K instances

Milestone 1 DONE 2026-07-22: bidirectional SCC LLAP wire (`Scc8530` SDLC Tx
capture, paced 3-deep Rx FIFO, Hunt carrier sense, WR1 modes, WR3/WR6 address
search + `$FF` broadcast, EOF + FCS in RR1) and the `LtoUdp` cable (Mini vMac /
TashRouter format, 239.192.76.84:1954). Gates `llap_loop_test`,
`ltoudp_test`, `llap_two_system_etalon`. TashRouter interop verified. Note:
System 6 only opens `.MPP` lazily from the Chooser — headless LLAP tests need
Sys 7.

- [~] **Full AppleShare session over the real bridge**: infrastructure DONE
  (netatalk **2.4.9** + TashRouter vendored, `tools/netatalk2/build_netatalk2.sh`
  builds hermetically, `appleshare_bridge.sh` + `router.py` serve `input/` as
  "Input" in zone "POM68K"). **Remaining: run the bridge with a GUI guest and
  mount the volume from the Chooser.**
- [ ] Interop check against Mini vMac's LToUDP (same multicast group).

---

## 7. New machine profiles

Phase A/B/C are done — 32 profiles, all booting the Finder (per-machine detail
in `CLAUDE.md` § Status and `CHANGELOG.md` 2026-07-21 → 2026-07-25). Effort
tiers and the full family map: `docs/68K_FAMILY_SCOPE.md`. Rule kept: **each new
profile gets at least one Finder cell before the next.**

Explicitly **out of scope** for now: PowerBook PMU, AV DSP, all 4 MB PPC ROMs.

### Independent majors — the only things left that are not just a ROM dump

- [ ] **AppleP IC IOP + OSS** → unlocks **IIfx** *and* **Quadra 900/950**. The
  IOP (Apple 343S1021, MAME `machine/applepic.cpp`) is a 65C02 core + 2 KB
  shared RAM + 2 DMA channels + host/peripheral mailboxes + a timer. POM68K has
  no 6502-class core; POMIIGS's `CPU65816` (825 lines, emulation mode = 65C02)
  is the candidate to vendor. The IIfx also has **no built-in video** — it boots
  on a NuBus card, so `TobyVideo`/`DeclRom` has to carry it.
- [ ] **Power Manager** → Portable / PowerBook 1xx / Duo. The 68HC05 half
  already ships (`M68hc05`), so the **PB150 is the cheapest entry**.
- [ ] **AV DSP (DSP3210)** → 660AV/840AV. Not planned.
- [ ] **NuBus + slot video** beyond Mac II Toby: IIx / IIcx / IIci and the NuBus
  Quadras. VASP/IIvx currently reads its three slots as empty; real cards would
  reuse the Mac II NuBus/DeclRom port.
- [ ] **ATA/IDE target** on the Quadra 630 / LC 580. The port is mapped but has
  no drive, so boot goes through SCSI — the remaining gap on that board.

### Remaining machines with the ROM already in `roms/`

| Machine | ROM on hand | New brick |
|---|---|---|
| **SE/30** | *(needs a dump)* | compact 030 + compact video; otherwise a IIx on a compact board |
| **Quadra 900 / 950** | `420DBFF3` / `3DC27823` | two **AppleP IC** IOPs (6502-class, SCC + SWIM) + Egret — the same brick the IIfx needs |
| **Mac IIfx** | `4147DD77` | OSS + two 6502-class IOPs + NuBus-only video |
| **PowerBook 150** | `FDA22562` | LCD framebuffer + 68HC05 PM — the `M68hc05` core already ships |
| **PowerBook 140-180 / Duo** | `E33B2724` / `0024D346` / `015621D7` | LCD framebuffer + Power Manager (68HC05 on the Duos) |
| **Portable / PowerBook 100** | `96CA3846` / `96645F9C` | LCD framebuffer + M50753 (740/6502) Power Manager |

### Assets for new profiles

Local, never committed (`hdv/` is gitignored): Infinite Mac copies of System 4.1
(floppy), 5.1 / 6.0 / 6.0.8 / 7.0 / 7.1 / 7.5 / 7.5.5 HD `.dsk`, plus
`HD20SC.vhd`, `boot.vhd` / `GISTPERSO-boot.vhd`, `MacOS-7.6-boot.vhd`,
`MacOS-8.1-boot.vhd`. Full tree also at `../refs/infinite-mac/Images`. Missing
files: fetch with **Scrapling** (not raw `curl` through the sandbox proxy) —
`Fetcher.get` / `scrapling extract get` on
`https://raw.githubusercontent.com/mihaip/infinite-mac/main/Images/…`.
Flat HFS → SCSI façade: `ScsiDisk::open` (gate `scsi_hfs_facade_test`; offline
bake `tools/wrap_hfs.py`).

---

## 8. Cross-machine architecture

- [ ] **Save states — one residual.** The feature shipped 2026-07-30 across all
  10 machine families and 32 profiles (archive core `src/SaveState.h/.cpp`,
  container `SaveStateMachines.h/.cpp`, `MoiraSnapshot.h`, GUI/CLI wiring in
  `main.cpp` `SaveStateSlot`). Gates: `savestate_test`, `savestate_v8_test`,
  `savestate_030/040/68k_test` (all `unit`), plus the real-OS
  `lcii_savestate_etalon` and `q605_savestate_etalon`.
  **Remaining: a hands-on GUI pass** — click « Sauver l'état » / « Restaurer
  l'état » on a booted machine. The machine-level save/load is gated; the GUI
  layer is compile-verified only.
  Conventions the chunks follow, worth keeping: callbacks and cross-device
  pointers are **re-bound, never serialized** (a pointer becomes an index —
  `Ncr5380::disk_`); pure caches are **flushed** on restore (ATC via the one
  vendored line `Moira::pomFlushAtcs()`, the 030 i-cache, the JIT guard) rather
  than carried; host-backed bulk data stays on the host (`ScsiDisk` ships a
  copy-on-first-write log of what the guest changed, not the image).
- [ ] **Optional HLE acceleration overlay** (`docs/HLE_OVERLAY.md`, after the
  `docs/LLE_VS_HLE.md` cleanup pass). Start with one hidden `boot.checksum`
  address hook and an HLE-forbidden accuracy-test mode; then signature-matched
  modules, per-module A/B gates and a visible non-conformant-mode indicator.
  **Prioritize disk HLE**; defer timing-loop elision until its overlap with the
  JIT is understood. **The premise moved 2026-07-31**: the conformant JIT
  now measures **×2.68** on `q605_boot_etalon` (61.3 s → 22.9 s), i.e.
  THROUGH the "~×2.5-3 conformant ceiling" this item used to invoke as its
  justification — so the overlay can no longer be sold as the way to make
  POM68K fast. Its surviving argument is narrower and sharper: the residual
  idle-Finder cost is the ATC-exactness contract itself (794 M window-lost
  exits over 12.2 G instructions), and a non-conformant mode is exactly
  where the five relaxations the JIT refuses become legal. `docs/
  HLE_OVERLAY.md` § 0 dates every premise; read it before building anything.
  Note also that the JIT reaches only eight CPU wrappers — on the Plus/SE/
  Classic and the Mac II family, HLE is the ONLY accelerator that exists.
- [ ] **Retro68 as a guest-level differential oracle**: build small Toolbox /
  Device Manager / XPRAM probes, run identical binaries under MAME and POM68K,
  compare. Known friction: no `Lists.h`/`AppleTalk.h` shims in multiversal
  (hardcoded list in `cincludes.rb`); the prober's `compat/` carries the
  PBControl glue; a 0-byte `.APPL` is normal.
- [ ] **Refactor the remaining GUI globals**: move compile-unit state such as
  `demoMode` (5 sites in `main.cpp`) into a machine/UI status object; keep
  machine threads, command queues and Emscripten's single-thread path
  behaviourally aligned.

---

## 9. Closed this cycle — index only

One line each; the story is in `CHANGELOG.md` under the date.

- **i-cache boost vs bus time** (2026-07-25) — `viaSync` read the *boosted*
  clock and `stall()` charged E-clock waits in boosted cycles, so every VIA-paced
  pulse was `cacheBoost_`× too short in machine time and host-paced Egret
  transports wedged above ~20 MHz. **Bus time is charged in machine cycles on
  all four 030 CPUs** now; `AdbVia::syncTo` was the one remaining `getClock()`
  reader. `POM68K_Q605_CACHE_BOOST` default is back to **4**; the IIsi's
  `cacheBoost_ = 1` pin is retired.
- **Floppy write persistence** (2026-07-24) — gate `floppy_persist_test`.
- **Beyond-boot gates on the LC II** (2026-07-24 / 2026-07-29) — soak, persist,
  launch, floppy; caught the ~37 % MCU overclock (`CudaLle mcuDebt_`).
- **Save states** (2026-07-30) — see §8.
- **Egret / Cuda firmware LLE** (2026-07-23 → 2026-07-29) — `M68hc05` core, the
  instruction-slaved ADB wire (`M68hc05::onCycles`), the Cuda 341S0417 "wedge"
  that was a missing DFAC2 I2C ACK, factory firmware as the default everywhere.
- **SCC**: async baud machinery + Tx/Rx engine fidelity (2026-07-23); the OS 8.1
  / OpenTransport standing-abort hang (2026-07-28, gate `q605_ot_bind_etalon`) —
  a *virgin* line reads clean, the standing abort begins with the first frame the
  line carries.
- **CD-ROM** base + mount + boot-from-CD (2026-07-29) — see §5.
- **Input-delivery gates** for four boot-only machines (2026-07-29).
- **The IIsi "ADB Manager never initializes" bug — RETRACTED** (2026-07-29):
  there was never a bug. Three rounds of coherent findings were one artifact —
  `peek8()` is **physical**, and on a RAM-based-video machine physical low RAM
  *is* the framebuffer while the ROM's PMMU (`TC=$80F84500`) moves the System's
  logical low memory elsewhere. "ADBBase" was screen pixels. Lessons kept:
  **check `TC` bit 31 before trusting any low-memory read**, assert on something
  the MMU cannot move (pixels, wire traffic, device state), and **corroborate
  with the observation closest to the user's experience** — "does the cursor
  move?" (idle diff 0 px, after motion 46 px) would have killed all three
  rounds at the start. A real gate bug was found en route: a 16-byte scan of the
  8-byte KeyMap read `$017D=$41` as a keystroke — **a positive assertion over a
  too-wide window is a false green.**
- **Block linking** (commit `b2c4e19`, `POM68K_JIT.md §9`) — **this entry had
  gone stale and misdirected a whole planning round on 2026-07-30.** It is a
  link *table* (O(1) slot invalidation, no patched jumps); loading phase block
  entries −53 %, 268 → 566 instr/entry, 18.4 → 16.75 s. At the idle Finder it
  helps less because the hot exits are `JSR`/`RTS` (still `Unsafe`). *Keep this
  line as the reminder of why §0's house rule exists.*
- **JIT window survival under VM page-aging** (2026-07-31) — the culprit was not
  eviction selectivity but an unconditional `pomJitDtlbFlush()` (a 2 KB memset)
  on every window re-arm: ≈1.3 TB of memset traffic per long run, paid by both
  backends. Deleting it is conformance-neutral (every invalidation it stood in
  for has an exact owner). threaded −22.8/−26.7 %, x64 −28.5/−33.0 %; DTLB fills
  942 M → 7.8 M; boot etalon 61.3 s interp → **22.9 s JIT**.
- **`selectBackend("auto")` could never pick x64** (2026-07-30) — and the fix
  needed a second half: with the filter lifted, `auto` handed x64 the **68030**
  machines it was never written for and `jit_lcii_boot_etalon` wedged for an
  hour. **Backend validity is per GUEST family**, not just per host
  (`BackendCaps::guestFamilies`).
- **PGO training extended to the 030/020 machines** (2026-07-29,
  `tools/pgo_train.sh`) — LC II boot −26 %, LC 68020 boot −12 %, on the
  *default* engine.
