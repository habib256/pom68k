# TODO

Active work only. Completed milestones, resolved bugs, investigation trails and
design rationale belong in `CHANGELOG.md` (with implementation detail in
`DEV.md` and the vendor notes). The LLE-vs-HLE inventory and migration plan
live in `docs/LLE_VS_HLE.md`.

## i-cache boost vs bus time — RESOLVED 2026-07-25

The three red gates (`lc3_`, `lc3plus_`, `iivx_boot_etalon`) were **not** an
Egret bug: `viaSync` read the *boosted* core clock and `stall()` charged the
E-clock wait in boosted cycles, so every VIA-paced pulse was `cacheBoost_`×
too short in machine time and the ROM's host-paced Egret transport wedged
above ~20 MHz. Bus time is now charged in machine cycles on all four 030
CPUs (the convention `Cpu040` already used) — CHANGELOG 2026-07-25. The
IIsi's `cacheBoost_ = 1` workaround is retired with it.
- [x] ~~**Try raising `POM68K_Q605_CACHE_BOOST` again**~~ **DONE 2026-07-25**:
  the boost-1 pin was a stale symptom — the whole 040 family is green at 2/4/8,
  so `Cpu040`/`CentrisCpu` now default to 4 (the suspicion that
  `Q605Memory::viaSync` had the 030 bug was wrong: it already divided by the
  boost). Two unit tests that measured wait states on the boosted clock
  (`swim2_test`, `q605_turboscsi_test`) were made boost-invariant.
- [x] ~~Audit the remaining `getClock()` readers~~ **DONE 2026-07-25**: found
  one — `AdbVia::syncTo` fed the PIC1654S co-step the raw core clock, so the
  transceiver firmware ran `cacheBoost_`× fast (live on the IIci once its
  boost came back). All four sites pass `machineClock()` now; `syncSwimFromCpu`
  and the 040 `viaSync`es were already correct.
- [ ] **ADB report rate**: the corrected timing made reports coalesce enough
  for System 7's mouse scaling to amplify a sustained stream ~1.6×
  (`lcii_launch_etalon` now steers closed-loop instead of assuming 1:1).
  Single moves land within one frame, 1:1 — but it is worth measuring the
  actual autopoll-to-guest report rate against the Egret's ~90 Hz and
  checking we are not servicing fewer polls than real hardware.

## Usability & proof (make the machines we have actually usable)

Rather than adding a machine, prove and harden the ones we have (ROI order:
data-loss holes and false test-confidence first, then convenience).
- [ ] **The Cuda↔VIA bit-bang transport is phase-fragile** (found 2026-07-27
  by the `mactv_boot_etalon` regression, since FIXED — see CHANGELOG). The
  6805 charges 11 cycles for its interrupt sequence (MAME `m6805.cpp:570`);
  modelling that costs the MCU ~2 % of its instruction throughput against
  machine time, which shifts the phase between the MCU's instruction stream
  and the host VIA — and that is enough to deadlock the Cuda transport on
  the **Mac TV** (31.3344 MHz, the tightest MCU:CPU ratio in the tree): the
  Cuda stops after 7 of a byte's 8 CB1 edges waiting for BYTEACK while the
  ROM spins forever on VIA1 IFR.SHIFT at `$40AB3BC8`, never reaching the
  SCSI Manager. `M68hc05::serviceInterrupts` therefore still returns 0, a
  deliberate gated inaccuracy documented at the call site.
  **The real fix is to make the transport survive a phase shift**, then
  re-land the 11 (a one-line change). `CudaLle.cpp:20-28` records two
  earlier attempts to move the same phase (uniform 2 µs interleave,
  busy-gated) crashing the guest, so treat the MCU/VIA lockstep as the
  thing under test, not a free parameter. Already ruled out as causes: the
  one-second-timer clock model, and a stale external bit counter across an
  ACR shift-mode change in `Via6522`.
- [x] **A — Floppy write persistence** DONE 2026-07-24: committed sectors
  flush back to the host `.dsk`/DC42 file on eject + GUI exit (temp+rename,
  DC42 checksum regenerated), opt-in write-back (GUI on, tests off). Gate
  `floppy_persist_test`. Closes the "writes stay in memory" data-loss hole.
- [x] **B — Beyond-boot gates on the LC II** DONE 2026-07-24: soak (idle
  uptime, clock tracks), persist (Cmd-N folder survives a reboot), launch
  (mouse double-click opens a folder). Gates `lcii_soak/persist/launch_
  etalon`. **Caught a real bug**: the Egret/Cuda-LLE MCU overclocked ~37 %
  (`M68hc05::run` budget overshoot) → the Mac RTC drifted; fixed with
  `CudaLle mcuDebt_` (CHANGELOG). These catch stability/corruption/drift
  the boot signatures structurally miss.
- [~] **C — Save states**: FOUNDATION LANDED 2026-07-30, fan-out pending.
  (Correction to this item's old wording: Moira has **no** serialization
  format — `StrWriter` is the disassembler's string writer. All of the CPU
  state is `protected`, though, which is what lets `MoiraSnapshot` carry the
  chunk on the POM68K side of the vendor seam.)
  - Done: archive core (`src/SaveState.h/.cpp`) — one `visit<Ar>()` per class
    driving save AND load, chunked container, zero-run codec, bounds-checked
    reader; `visit()` on the whole **LC II** tree (20 classes) and the
    container assembly (`SaveStateMachines.h/.cpp`) with identity refusal
    (version / profile / ROM checksum / RAM size). Gates `savestate_test`
    and `savestate_v8_test`, both `unit` (no assets).
  - **Remaining, in ROI order:**
    - [x] ~~**An LC II boot etalon.**~~ **DONE 2026-07-30**
      (`lcii_savestate_etalon`): boot System 7 to the Finder off the SCSI
      image, snapshot, run 1200 frames of deterministic mouse activity,
      hash; restore, run the same 1200 frames — bit-identical machine
      hash both ways, load→save byte-identical, Finder alive at the end.
      Passed first try, which upgrades the LC II device chunks (SCSI COW
      log, SWIM, ASC, SCC, Egret-LLE MCU mid-transaction) from
      compile-verified to behaviour-verified.
    - [x] ~~Fan out: 9 more CPU wrappers + 9 more machines~~ **DONE
      2026-07-30**: every machine class serializes — Sonora/VASP/RBV
      (030), Q605/Centris/Q700/Q630 (040), Mac II + MacMemory compacts —
      with all their unique devices (Dafb, Valkyrie, Rtc, AdbVia +
      Pic1654s, Ncr53c96, AscIosb, TobyVideo + NuBus, MacKeyboard/
      MacMouse), all 10 CPU wrappers on `MoiraSnapshot`, one `SnapMachine`
      tag per profile (32), and the container genericized (`saveT`/`loadT`
      + one-line forwards). Gates `savestate_030_test`, `savestate_040_
      test`, `savestate_68k_test` (all `unit`, synthetic ROMs): re-save
      byte-identity + determinism across a restore per family, incl. the
      IIci flavor (AdbVia/Rtc wiring) and a Toby-equipped Mac II.
      Traps hit en route: the Mac II enters via Cpu020's hardcoded
      Basilisk vector (PC=ROMBase+$2A, not the ROM header) in 32-bit mode
      (hmmu24_ off at reset), and the Plus overlay clear samples portA(),
      so DDRA must be set first. Real-OS savestate etalons: **LC II and
      Q605 exist** (`lcii_savestate_etalon`, `q605_savestate_etalon` —
      the latter DONE 2026-07-30, first try: Mac OS 8.1 Finder, 1200
      mouse frames, bit-identical restore, covering DAFB/AscIosb/53C96/
      Cuda-LLE under a real OS). The other families follow the same
      template as needed.
    - [x] ~~GUI/CLI wiring~~ **DONE 2026-07-30** (`main.cpp SaveStateSlot`):
      every machine window's **Machine** menu gains « Sauver l'état » /
      « Restaurer l'état » (Mac II: buttons in its CPU panel). The GUI only
      queues a request; the MACHINE thread performs the save/load inside
      `applyCmds()` between two quanta (the `Cmd::CpuEngine` precedent) and
      posts a one-line outcome (also printed to stdout). The state file is
      tagged like the `.pram` (`<image>.<profile>.pomss`), written
      temp+rename; a refused snapshot leaves the machine untouched and the
      menu shows `load()`'s own explanation. The single-threaded
      Plus/compact loop applies inline and `MacFrameClock::resync()`s after
      a restore (frameBase is derived from the CPU clock). All 10 families
      wired, one `SnapMachine` tag per profile at each run site.
      **Pending: a hands-on GUI pass** (click both items on a booted
      machine) — the machine-level save/load is gated, the GUI layer is
      compile-verified only.
  - Conventions the chunks follow, worth keeping: callbacks and cross-device
    pointers are **re-bound, never serialized** (a pointer becomes an index —
    `Ncr5380::disk_`); pure caches are **flushed** on restore (ATC via the
    one vendored line `Moira::pomFlushAtcs()`, the 030 i-cache, the JIT
    guard) rather than carried; host-backed bulk data stays on the host
    (`ScsiDisk` ships a copy-on-first-write log of what the guest changed,
    not the image).
- [ ] Extend the beyond-boot gates to a second machine (Quadra 605 / Mac
  OS 8.1) once save states land — reboot-survival there exercises the
  53C96 WRITE path end to end.

## Finder matrix Phase C (new machine profiles)

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
- [x] ~~Macintosh LC (68020)~~ **DONE 2026-07-24** (CHANGELOG "Phase C"):
  `V8Memory::Model::Lc` (2 MB soldered, HMMU mask on pseudo-VIA PB3),
  Moira plain-020 /BERR path fixed (POM68K_VENDOR.md), Egret firmware
  LLE ADB. Gate `lc_boot_etalon`; System 7.5 Finder.
- [x] ~~Classic II~~ **DONE 2026-07-24** (same entry): `Model::ClassicII`
  Eagle — PA $92, 512×342 mono out of RAM @$1F9A80, MAME ROM patch,
  forgiving bus (open-bus unmapped/PDS). Gate `classic2_boot_etalon`;
  System 7.5 Finder on the Egret firmware LLE.
  **Follow-ups**: add both to `finder_boot_matrix` cells; identify the
  Eagle's real $F18000 block (the ROM writes $1E then reads a pointer
  at +$38 — served as open bus today, gated on UnivROMFlags bit 7).
- [x] ~~Color Classic~~ **DONE 2026-07-24** (CHANGELOG "Spice + Cuda"):
  `V8Memory::Model::ColorClassic` — Spice (PA $82, fixed sense 2,
  SWIM2 in the gate array, 1 MB ROM), `AscSonora` EASC $BC, Cuda
  firmware LLE (341S0788). Gate `cclassic_boot_etalon`.
  ~~**Follow-up**: the factory 341S0417 (Cuda 2.35) wedges on the
  M68hc05~~ **RESOLVED 2026-07-29** (CHANGELOG "The 0417 wedge was a
  missing DFAC2"): not a core bug — the CC carries a DFAC2 on the
  Cuda's I2C (maclc.cpp:505) and the 2.35 requires its ACK; un-ACKed
  it took a DFAC error path that muted the next host VIA session.
  `CudaLle::setI2cDfac` (minimal $6F slave) + factory 0417 is the CC
  default; the Mac TV runs its factory 341S0789 (2.38, dump landed
  the same day). `POM68K_CUDA_FW=<path>` = diag firmware override.
- [x] ~~LC III~~ **DONE 2026-07-24** (same entry): `SonoraMemory` +
  `SonoraCpu` (030 @ 25 MHz) + `SonoraVideo` (mv_sonora modelines,
  CLUT, sense in vctrl), Egret LLE 341S0851. Gate `lc3_boot_etalon`.
  **Follow-ups**: add both new machines to `finder_boot_matrix` cells.
- [x] ~~LC III+ (33 MHz, ID $A55A0003)~~ **DONE 2026-07-24** (CHANGELOG
  "LC 475 + LC III+"): `SonoraMemory` cpuHz/machineId ctor params
  (`kCpuHzPlus`/`kIdLc3Plus`), `POM68K_LC3_PLUS` selects; the unmapped
  Sonora I/O catch-all had to return 0 (MAME parity, not open-bus $FF) to
  clear the LC III+ ProductInfo's RAM device poll at $50F0A000. Gate
  `lc3plus_boot_etalon`; Sys 7.5 Finder on the Egret LLE.
- [x] ~~LC 475 / Performa 475 (68LC040 identity)~~ **DONE 2026-07-24** (same
  entry): split the combined Quadra 605 / LC 475 profile — LC 475 =
  $A55A2221 + 68LC040 (`POM68K_Q605_NOFPU`), Quadra 605 = $A55A2225 + full
  FPU, both via `POM68K_Q605_ID`. Gate `lc475_boot_etalon`; Mac OS 8.1
  640×480×8 Finder on the Cuda LLE.
- [x] ~~LC 575 / Performa 575 (68LC040 @ 33 MHz, ID $A55A222E)~~ **DONE
  2026-07-24**: identity sibling of the Quadra 605 on the SAME FF7439EE ROM.
  The all-in-one ROM path probes un-emulated PrimeTime I/O windows and a /BERR
  there dropped it into the ROM serial debugger, so `Q605Memory` now reads
  **unmapped PrimeTime I/O back as 0** (MAME iosb.cpp:54-65 — no catch-all
  BERR; same rule as the Sonora map). Fine-sampled Finder latch (the 33 MHz
  desktop is briefly clear before the "not shut down properly" startup alert
  covers the menu bar). Gate `lc575_boot_etalon`; Mac OS 8.1 640×480×8 Finder.
  Zero regression across the 13 Q605-family gates.
- [x] **LC 520 / LC 550 — DONE** (2026-07-24): the EDE66CBD AIO family
  boots System 7.5 to the 8-bpp color Finder on the Sonora machine with
  **Cuda 341S0060 firmware LLE** (`SonoraMemory cudaAdb=true`) — the
  from-scratch reverse-engineering (all four walls resolved) is in
  **`docs/LC520_BRINGUP.md`**. Gates `lc520_boot_etalon`,
  `lc550_boot_etalon`; GUI entries + `$EDE66CBD → runLc3(SonoraModel)`
  dispatch (`POM68K_AIO_ID`).
- [x] **Color Classic II / Performa 275 — DONE** (2026-07-24): box `$0101`
  at sense 2 (table vid `$4D`) → 512×384×8 bpp color Finder. Gate
  `cclassic2_boot_etalon`; GUI `POM68K_AIO_ID=CC2`.
- [x] **Mac IIvx / IIvi — DONE** (2026-07-24): new VASP machine
  (`VaspMemory`/`VaspCpu`/`VaspVideo` — "V8 video on Sonora addressing",
  MAME vasp.cpp/maciivx.cpp), Egret 341S0851 LLE, 68030+68882 @ 31.3344 /
  15.6672 MHz, ids $A55A2015/$A55A2016 off the shared 4957EB49 ROM. Both
  boot Sys 7.5 to the 640×480×8 Finder. Gates `iivx_boot_etalon`,
  `iivi_boot_etalon`. Empty NuBus = MAME-unmapped 0; real NuBus cards on
  VASP would need the Mac II NuBus/DeclRom port.
- [x] **Mac TV — DONE** (2026-07-25): the EAF1678D **Tinker Bell** ROM
  (not the EDE66CBD Sonora AIO). Added as `V8Memory::Model::MacTv` + a
  `spiceClass()` predicate (Color Classic ∪ Mac TV share SWIM2 + Sonora
  EASC + Cuda) and a `cpuHz` ctor param: PA id `$84`, fixed 640×480 sense
  `$06`, 8 MB cap, Cuda LLE (341s0060), 68030 @ 31.3344 MHz no-FPU. The
  gate array stays C15M, the CPU ticks rescale (VASP pattern). Boots color
  Sys 7.5 first try — gate `mactv_boot_etalon`; GUI/CLI dispatch on
  `$EAF1678D → runLcII(Model::MacTv)`.
- [x] **Centris 650 / Centris 610 — DONE** (2026-07-24): new djMEMC + IOSB
  machine (`CentrisMemory`/`CentrisCpu`) — Q605 DAFB/53C96/SWIM2/ASC/pseudo-
  VIA2 + discrete 343-0042 RTC + PIC1654S ADB LLE (`AdbVia`), 68LC040 @ 25 /
  20 MHz, model in VIA1 PA pins ($46/$40), F1A6F343/F1ACAD13 ROM. Wall: the
  djMEMC 2 MB VRAM window (vs MEMCjr's 1 MB) — mirror it. Both boot Mac OS
  8.1 to the 640×480×8 Finder. Gates `centris650/centris610_boot_etalon`;
  GUI `POM68K_CENTRIS610`. **Quadra 610/650** (68040, ID $44/$52) are the
  same machine with `CentrisCpu` full-040 (`POM68K_CENTRIS_FPU`).
- [x] **Quadra 650 / Quadra 610 — DONE** (2026-07-24): full 68040 identity
  variants of the Centris (`POM68K_CENTRIS_MODEL=q650/q610`, ID $52 @ 33 MHz
  / $44 @ 25 MHz). Both boot Mac OS 8.1 to the Finder. Gates
  `quadra650/quadra610_boot_etalon`; GUI entries.
- [x] **Quadra 800 — DONE** (2026-07-25): the last `macquadra800.cpp` sibling,
  a fifth model of the same machine (`POM68K_CENTRIS_MODEL=q800`) — full
  68040 @ 33 MHz, VIA1 PA ID pins **$12** (pa1|pa4, the only one with pa6
  clear). SONIC Ethernet and the three NuBus slots stay unmapped-0 (the boot
  path binds neither); only the **Ethernet address ROM** at $50008000 (6 MAC
  bytes + inverted-XOR check at +7) needed modelling. Mac OS 8.1 640×480×8
  Finder on the first run. Gate `quadra800_boot_etalon`; GUI entry.
- [x] **Mac IIsi — DONE** (2026-07-25): new **RBV** machine
  (`RbvMemory`/`RbvCpu`/`RbvVideo`) — the RAM-Based Video ancestor of the
  V8/VASP/Sonora line (MAME `rbv.cpp`/`maciici.cpp maciisi`): 68030 @
  20 MHz, Egret 344S0100 LLE, SWIM1, discrete ASC, Bt478 CLUT, framebuffer
  at the start of system RAM. Wall: the IIsi ROM's host-paced Egret
  bit-bang loses the via_full pulse under the 030 i-cache boost — `RbvCpu`
  defaults to no boost. Boots the French Sys 7.5 Finder (1-bpp B&W dither).
  Gate `iisi_boot_etalon`; GUI/CLI dispatch on `$36B7FB6C → runIIsi`.
- [x] **Mac IIci — DONE** (2026-07-25): the IIsi's near-twin, an `iici`
  flavor of `RbvMemory` — RBV video/SCSI/SWIM1/ASC, but the Egret swapped
  for the **ADB modem** (`AdbVia` PIC1654S LLE) + **discrete 343-0042 RTC**
  (the Centris wiring), no reset-hold, empty NuBus. 68030 @ 25 MHz, ROM
  `$368CADFE`. Wall: `via_in_a` PA0 must read 1 (`$C7`, diagnostic
  disabled), not `$C6`, or the ROM spins in its VIA-T2 burn-in loop. Gate
  `iici_boot_etalon`; dispatch `$368CADFE → runIIsi(iici=true)`.
- [x] **Mac IIx / IIcx — DONE** (2026-07-25): 68030 variants of the Mac II
  FDHD — `MacIIMemory::Model {MacII,IIx,IIcx}` + `Cpu020` `is030` flag
  (M68030 + 68882), Toby NuBus video reused, shared `mac2fdhd` ROM
  `$97221136`, VIA IDs (IIx VIA2 PB `$87`, IIcx VIA1 PA `$C1`). Wall: the
  030's own PMMU double-translated against the GLUE 24-bit remap — skip
  `physAddr()` when TC bit 31 (PMMU on), the 020-HMMU-vs-030-PMMU split.
  Both boot to the Finder (SCSI 1158). Gates `iix/iicx_boot_etalon`;
  dispatch `$97221136 → IIx` (`POM68K_MACII_MODEL=iicx/fdhd`).
- [ ] SE / Classic (68000 + ADB) for 256/512 KB compact ROMs.
- [ ] Explicitly **out of Phase C**: PowerBook PMU, IIfx IOPs, AV DSP, all
  4 MB PPC ROMs.

## AppleTalk in-process stack (2026-07-24, DONE — polish backlog)

The whole service side now runs inside POM68K on the SCC wire, GUI
default (`docs/APPLETALK.md` §6.5, CHANGELOG 2026-07-24): `AtalkStack`
(node/router: DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer` (ASP + AFP 2.1 +
`.AppleDouble`), `PapServer` (PAP → CUPS/`.ps`), `MacIpGateway` (DDP-22
+ user-mode NAT), tied by `AtalkHub` with a **Réseau → AppleTalk**
status/toggle window. Gates `atalk_stack_test`, `afp_server_test`,
`pap_server_test`, `macip_gw_test`. Remaining polish:

- [ ] Persist CNIDs across runs (currently per-session; a `.AppleDB`
  or extending the `.AppleDouble` sidecar with the id would survive a
  reboot, matching netatalk's catalog).
- [ ] AFP corners the subset skips: proper Desktop database (icons/
  comments/APPL mapping, not the current "no item" stubs), CopyFile,
  CatSearch, DID-relative variable paths, AFP ≥ 3.0 / UTF-8 names.
- [ ] More UAMs than guest/cleartext (DHX/random-number) if a guest
  refuses cleartext.
- [ ] PAP status polling + per-queue `papd.conf`-style config; expose
  the CUPS queue choice in the GUI.
- [ ] MacIP: outbound ICMP (needs raw sockets / a ping helper),
  IP fragment reassembly on the guest→host side, TCP window scaling for
  big transfers; today it is in-order-only MSS 536.
- [ ] GUI: editable fields (share dir / server + printer name / gateway
  subnet + DNS) instead of env-only; a "reveal spool folder" button.
- [ ] A live-boot etalon that mounts the internal AppleShare from a real
  guest Chooser (needs a Sys 7 image + scripted Chooser drive, like the
  external-bridge item below).

## LocalTalk between two POM68K instances (virtual LLAP cable)

**Milestone 1 DONE (2026-07-22)** — the SCC LLAP wire is bidirectional
and the LToUDP cable exists (CHANGELOG "LLAP milestone 1"):
`Scc8530` captures SDLC Tx frames (underrun = frame end, Send Abort
discards) and has a full Rx path (paced 3-deep FIFO, Hunt exit/re-entry
carrier sense, WR1 Rx interrupt modes, SDLC address search WR3/WR6 +
$FF broadcast, EOF + FCS tail in RR1); `LtoUdp` speaks the Mini vMac /
TashRouter multicast format (239.192.76.84:1954, 4-byte sender tag).
GUI opt-in `POM68K_LTOUDP=1` on Mac II / LC II / Quadra. Gates:
`llap_loop_test` (two SCCs, ENQ both ways, filter, abort, carrier),
`ltoudp_test` (real multicast cable, soft-skips).

Next milestones:
- [x] **Two-System etalon — DONE (2026-07-22)**: `llap_two_system_etalon`
  boots TWO Mac II Sys 7 machines with `POM68K_APPLETALK=1` (SPConfig
  $21 seed, Rtc + Egret) on a shared cable; both LAP Managers acquire
  distinct node IDs over real ENQ traffic (~650 probes each way) in
  ~12 s emulated. Note: System 6 only opens .MPP lazily from the
  Chooser — headless LLAP tests need Sys 7.
- [x] RTS/CTS directed-frame dialogue — pinned in `llap_loop_test`
  (2026-07-22): CTS answered and received inside the LLAP inter-frame
  window, DATA follows; GUI quanta are sliced ~1 ms while the UDP cable
  is active (`runQuantumWithWire`) so handshakes stay within the
  driver's retry budget.
- [x] Plus machine wiring (2026-07-22): `wireLocalTalk(mem, 272)` +
  per-frame poll in the inline GUI loop.
- [x] TashRouter interop VERIFIED (2026-07-22): its `LtoudpPort` speaks
  our exact wire format (4-byte pid tag + raw LLAP); a live TashRouter's
  own address probe (ENQ 254/254/$81) was received on our socket.
  README documents the minimal router. Remaining:
- [~] Full AppleShare session (2026-07-22: infrastructure DONE, live
  Chooser mount pending manual validation): netatalk **2.4.9** and
  TashRouter are **vendored** (`extern/netatalk2` + `extern/tashrouter`,
  POM68K_VENDOR.md each); `tools/netatalk2/build_netatalk2.sh` builds
  hermetically (static pinned BDB 5.3 + libgcrypt, no system packages —
  afpd/atalkd 2.4.9 verified running); `appleshare_bridge.sh` (sudo:
  appletalk module + veth/macvtap pair) + `router.py` complete the
  chain serving `input/` as "Input" in zone "POM68K". Remaining: run
  the bridge with a GUI guest and mount the volume from the Chooser.
- [ ] Interop check against Mini vMac's LToUDP (same multicast group).

## LLE fidelity — replace HLE shortcuts (see `docs/LLE_VS_HLE.md`)

- [x] **Mac II ADB — real PIC1654S: DONE, now the DEFAULT** (CHANGELOG
  2026-07-22 "Mac II LLE ADB default"). The mouse moves end-to-end over the
  real firmware (`macii_mouse_trace` PASS); `POM68K_ADB_LLE=0` keeps the
  HLE fallback. Follow-ups that remain:
  - [x] ~~Retire the HLE `AdbVia` byte-model~~ **Policy settled
    2026-07-29** (`docs/LLE_VS_HLE.md` §2 "HLE-fallback retirement
    policy"): kept as the no-dump fallback (dumps are non-distributable)
    but LOUD — every HLE ADB entry prints a NON-CONFORMANT notice.
    Actual deletion = a deliberate "POM68K requires MCU dumps" product
    decision, not a cleanup.
  - [ ] `AdbLine` device model: second mouse button / extended-keyboard
    handler IDs, and exercise Listen R2 (LEDs) paths.
- [ ] **Quadra 605 / LC 475** shortcuts where fidelity matters:
  - Expand Cuda commands only from ROM/driver traces.
  - Add accurate 040 timing, cache copyback/snooping and on-chip-FPU/FPSP
    behaviour as separate, oracle-gated milestones.
  - (Follow-up from Q8.8) Make `CACHE_BOOST` > 1 Finder-safe without
    changing etalon metrics.
- [x] **Cuda wire-model redo — DONE 2026-07-22** (CHANGELOG "LLE
  step 7"; `docs/LLE_VS_HLE.md` §1.6b resolved): real framing +
  wire-event attention byte + 61/71/88/13/30 µs pacing + `$1B`
  one-second modes; the $76 pop, GetPram erase, Q8.2 duplication and
  tick heuristic are deleted, 49/49 gates + Finder matrix green.
  Follow-up (step 10 there): Egret/Cuda **firmware** LLE — the real
  dumps are on hand (`roms/cuda/341s0788.bin` etc.), the Mac II
  PIC1654S migration is the template. **Blueprint (oracles fetched
  2026-07-23 to `refs/mame/src/`)**:
  1. ~~`M68hc05` core~~ **DONE 2026-07-23** (CHANGELOG "M68HC05E1
     core"; gate `m68hc05_test`): all three Cuda dumps execute clean
     from reset — PLL/DDR/port-B traffic pinned, zero undefined
     opcodes over 2 M cycles each.
  2. ~~Wire ports behind `POM68K_CUDA_LLE=1`~~ **DONE 2026-07-23**
     (`CudaLle`, gate `cuda_lle_test`): firmware releases the 68040 by
     its own PC3 write (+280.8 ms), PRAM installs at $0100-$01FF,
     /TREQ on VIA1 PB3, via_clock/data on the VIA SR via
     `extShiftCB1`, `AdbLine` on PA7/PA6. Egret HLE stays default.
  3. ~~Host↔Cuda transactions against the ROM/System~~ **DONE
     2026-07-23** (CHANGELOG "Mac OS 8.1 boots to the Finder on the
     REAL Cuda firmware"; gate `q605_cudalle_boot_etalon`): all three
     Q605 boot etalons reach the Finder on the firmware path. The
     blocker was the customized-E1 PFW pin (MAME's "cudapfw" tap,
     now `M68hc05::setForcedInputs`).
  4. ~~Flip the default on the Q605~~ **DONE 2026-07-23** (CHANGELOG
     "The real Cuda firmware is the Quadra's DEFAULT"): input routed
     to `AdbLine`, PRAM persistence re-mirrored, the ADB polarity bug
     fixed (electrical line = ¬PA7), `q605_cudalle_mouse_etalon`
     green. `POM68K_CUDA_LLE=0` keeps the Egret HLE fallback.
  5. ~~LC II Egret flavor~~ **built 2026-07-23, default REVERTED to
     HLE the same day** (CHANGELOG "Egret firmware LLE back to
     OPT-IN"): `CudaLle::Flavor::Egret` boots System 7.5/7.1 to the
     Finder, but the VIA per-byte dance desyncs under autopoll load —
     every host session closes after ONE byte ($00) and the firmware
     clocks the real packet after the close; only the one-second
     packet resyncs (~1.5% of mouse reports land). `POM68K_EGRET_LLE=1`
     opts in; gate `egret_lle_test` still forces + pins the path.
  6. ~~Fix the Egret LLE ADB receive, then re-flip the default~~
     **DONE 2026-07-24** (CHANGELOG "Event-driven ADB wire"): the wire
     is SLAVED to the MCU's instruction stream (`M68hc05::onCycles` →
     `CudaLle` adbAcc_ lambda) — device edges land at instruction
     resolution while the MCU/host-VIA lockstep phase (the thing both
     slicing experiments broke) stays bit-identical. LC II mouse
     delivery went from ~1.5% to HLE parity (`lcii_mouse_trace`
     saturates identically); the Egret firmware LLE is the LC II
     DEFAULT (`POM68K_EGRET_LLE=0` = HLE fallback). The Quadra
     collision face (2026-07-24 field freeze: host command × autopoll
     TREQ wedge at ~$D1F04) is pinned by the new 500-pair keypad
     stress phase in `q605_cudalle_key_etalon` — green on the slaved
     wire.
  7. ~~**SCC: OS 8.1/OT hangs on the standing no-peer abort**~~ **DONE
     2026-07-28** (CHANGELOG "LLE step 7"; `docs/LLE_VS_HLE.md` §1.10
     RESOLVED): the abort is presented only under a genuine abort
     condition — a VIRGIN line (never driven since reset) reads clean
     (FM0: no edge → no recovered clock → no abort), which is what
     OT's .MPP bind spins on; the standing abort begins with the first
     frame the line carries (`Scc8530::lineDriven_`, LLAP trailer
     abort) and the EOM path presents it, so the Sys 7 no-peer stream
     starts at the guest's own first ENQ probe (etalon timings
     unchanged). `POM68K_SCC_CLEANLINE` is deleted from all eight
     memory classes. New gate `q605_ot_bind_etalon` (OS 8.1 + the
     in-process hub, main.cpp wiring — bind proven by post-ENQ DDP);
     `scc_ext_test` / `llap_loop_test` re-pinned. Note: the wedge was
     already un-reproducible on the 2026-07-28 tree (likely collateral
     of the 2026-07-25 bus-time pass); the fix makes the bind
     guaranteed rather than timing-dependent.
     **Remaining**: ~~retire the `Egret.*`/`AdbBus` HLE (and the Mac II
     §1.9 leftover)~~ — policy settled 2026-07-29 (`LLE_VS_HLE.md` §2):
     fallbacks kept but LOUD (stderr NON-CONFORMANT notice at every HLE
     ADB entry); §1.9 lives only inside them; deletion is a deliberate
     product decision.
- [ ] **SCC LLE backlog — 2026-07-22 MAME `z80scc.cpp` audit** (source
  in `refs/mame/src/devices/machine/`; summary in
  `docs/LLE_VS_HLE.md` §3). Caveat everywhere: MAME's own SDLC side is
  partial (Send Abort/CRC resets `:1602/:1635` "not implemented", no
  EOM latch, no hunt/sync) — for LLAP behaviours we are already the
  more complete model; use MAME as oracle for the ASYNC side only.
  - [x] **High — async baud machinery: DONE 2026-07-23** (CHANGELOG
    "SCC async-baud machinery"; gate `scc_baud_test`): WR4 clock
    mode + stop/parity, WR5 data bits, WR12/13+WR14 BRG, WR11
    routing → per-channel derived pace; machines wire
    `setClocks(cpuHz, pclkHz)`; SDLC derives the exact legacy
    272/544/868 LLAP constants.
  - [x] **Medium — Tx/Rx engine fidelity: DONE 2026-07-23** (CHANGELOG
    "SCC Tx/Rx engine"; gate `scc_engine_test`): WR5 bit 3 gates the
    transmitter; one-slot Tx buffer feeds a paced shifter (TxIP on the
    buffer-empty TRANSITION, RR0 TBE + RR1 All Sent live); the SDLC
    tail (CRC+flag) drains in 24 bit times at the programmed pace
    (flat 1200 deleted); the receiver VERIFIES the Rx FCS → RR1 bit 6;
    async `injectRxByte` carries parity/framing error bits, special
    raised at READ time with WR1 bit 2 routing (z80scc data_read
    :2130). True bit-serial sampling stays unmodelled (no async
    transport exists yet; MAME byte-steps via device_serial too) —
    folded into the Low tier below.
  - [ ] **Low — completeness**: true bit-serial engines (per-bit Tx/Rx
    sampling — only worth it with a real async transport to talk to);
    WR5 RTS output tracking (modem
    handshake, auto-RTS deassert on all-sent, `tra_complete` :1100);
    SDLC Rx residue codes (RR1 bits 3-1); chip-variant gating
    (NMOS 8530 vs 85C30 vs ESCC — FIFO depth 3/8, WR7', status FIFO
    `:1363`; needed the day a Quadra-era machine wants the ESCC);
    WR9 VIS/NV vector options (today hardcoded VIS=1, correct for
    every Mac target); DPLL (MAME stubs it too, `:305-318` — only
    relevant if a machine ever clocks SDLC off the DPLL for real).

- [ ] **Fix the no-FPU SANE path** (diagnosis done — CHANGELOG 2026-07-20
  O6.13): select the real no-FPU UniversalInfo / defaultRSRCs path so PACK 4
  installs without the D7 bit-16 gate; Finder under bare `FPUModel::NONE`.

- [ ] **Confirm the GISTPERSO/SimCity 2000 startup race is closed.** TODO
  had it checked, but CHANGELOG (2026-07-18) records only the root-cause
  analysis and the Shift/Option workaround — re-run the headless repro and
  either land the fix entry in CHANGELOG or reopen the differential hunt.

- [ ] **Complete LC II storage and sound devices.**
  - ~~Add SWIM ISM/MFM support for 1.44 MB media~~ **controller DONE
    2026-07-23** (CHANGELOG "SWIM1 ISM"; gate `swim1_test`): IWM+ISM
    personalities, 1-0-1-1 switch, param RAM, MFM read/write through
    the cell engines. Remaining: a guest-level 1.44 MB mount/boot
    etalon (asset: `disks35/Stuffit_Expander_5.5.dsk`).
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
  - ~~Add 800K write support~~ **DONE 2026-07-23** (CHANGELOG "IWM write
    engine + GCR write-back"; gate `iwm_write_test`): the real IWM write
    mode (handshake/underrun) + checksum-verified GCR sector commit, on
    the Plus and the LC II (shared `Iwm`). Writes stay in-memory —
    persisting dirty floppy images back to the host file remains open.
  - Add external-drive selection and eject/insert UI.
  - Implement keypad/arrow `$79`-prefix handling where required by M0110 input.

- [ ] **Improve classic sound accuracy.**
  - Fetch the sound buffer per scanline instead of once per frame.
  - Model the disk-PWM byte and the analog volume curve.

- [ ] **Finish NCR 5380/SCSI and serial support.**
  - Support multiple targets/LUNs and correct REQUEST SENSE after CHECK
    CONDITION.
  - Implement usable SCC serial ports rather than only mouse/LocalTalk
    paths. Blueprint from the 2026-07-22 MAME `z80scc.cpp` audit
    (docs/LLE_VS_HLE.md §3; source fetched to `refs/mame`):
    1. ~~WR4 clock mode + WR12/13 BRG~~ **DONE 2026-07-23**
       (`scc_baud_test`);
    2. ~~WR11 clock-source routing~~ **DONE 2026-07-23** (same pass);
    3. then WR5 Tx-Enable gating, parity/framing generation,
       Rx CRC check (RR1 bit 6), and a host-side serial transport
       (PTY/TCP) to make the ports *usable*, not just timed. Note:
       MAME's own SDLC side is partial (Send Abort/CRC resets/EOM
       latch unimplemented) — for LLAP behaviours our implementation
       is the more complete one; don't regress it chasing MAME parity.

- [ ] **Add pixel-accurate etalons and a WASM build.**
  - Create a screenshot regression runner for the Plus boot/Finder paths.
  - Keep asset-dependent tests soft-skippable.

- [x] ~~**CD-ROM support**~~ **DONE 2026-07-29**: `ScsiDisk::openCdrom`
  adds a CD personality (INQUIRY type $05 + removable, 2048-byte blocks,
  READ TOC, START/STOP eject, read-only) with the **Apple magic MODE
  SENSE page $30** that Apple's CD driver gates on. `attachCdrom(path,
  id = 3)` on all nine multi-target machines; the CLI routes
  `.iso`/`.cdr`/`.toast`. Gate `scsi_cdrom_test` (31 checks).
  Remaining CD work, in rough order of value:
  - [ ] **Boot from CD** (the ROM's SCSI scan already covers ID 3; needs
    a bootable Apple CD image to pin it).
  - [x] ~~**A guest-level mount gate**~~ **DONE 2026-07-29**
    (`q605_cdrom_etalon`): Mac OS 8.1 boots from the hard disk and the
    Mac OS 8.6 CD mounts as data — 640×480×8 Finder plus ~100 blocks of
    catalog traffic served BY THE CD TARGET. Layout matters: the ROM's
    SCSI scan runs 6→0, so the boot volume goes to ID 6 and the CD to 3,
    or a bootable disc wins the scan. Three real bugs were found getting
    there (block descriptor, READ TOC format 1, mode page $0E — see
    CHANGELOG).
  - [ ] **Only 2048-byte-DDM discs mount.** `MacOS_86.iso` (driver
    descriptor map declaring `sbBlkSize = 2048`) mounts; the hybrid
    `TIM_3.iso` (`sbBlkSize = 512`) and the bare-HFS `.toast` images are
    read (4 blocks of probes) and then ignored. Observed, cause not
    established — it may well be correct (a real Apple CD driver may
    require its own `Apple_Driver43_CD` partition, which only the 8.6
    disc has). Verify against MAME or a real drive before "fixing"
    anything.
  - [x] ~~**Boot from CD**~~ **DONE 2026-07-29** (`q605_cdboot_etalon`):
    with no hard disk the ROM's 6→0 scan reaches the CD at ID 3, loads
    its `Apple_Driver43_CD` partition and boots the disc — 3913 blocks
    (7.8 MB) of System read off the CD target, 640×480×8 Finder. Asset:
    a Mac OS 8.1 retail CD. **8.5/8.6 cannot be used**: they are
    PowerPC-only (8.1 is the last 68k release), so a 68k Mac stops at a
    black screen on them however good the emulation — the earlier 8.6
    black screen was correct behaviour, not a bug.
  - [ ] **Install from CD**: the disc boots and the Installer is
    reachable, but driving it to completion (mouse through the Installer
    UI, then a reboot onto the freshly written volume) is not automated.
    That is the natural next "real work" gate.
  - [ ] **CD audio** (READ TOC already reports the data track; CDDA
    playback, PLAY AUDIO/PAUSE and the audio-through-ASC path are
    absent — no consumer yet).
  - [ ] 2352-byte raw rips + .cue/.bin multi-track (refused today rather
    than mis-read).

## Cross-machine architecture

- [ ] **Implement save states.**
  - Version CPU, MMU/FPU, memory and every device state.
  - Include mounted-media identity and reject incompatible machine profiles.
  - Stamp active non-conformant HLE modules in the state.

- [ ] **Build a 68k-to-host JIT.** Staged; **J0 + J1 done 2026-07-27**
  (`src/jit/POM68K_JIT.md`, seam in `extern/moira/POM68K_VENDOR.md`).
  POM68K is multiplatform, so the engine is multi-target by construction:
  a host-agnostic layer over `jit::Backend`, with a `threaded` backend that
  generates no code and is therefore always available.
  - ~~J0 — engine seam, backend interface + registry, portable W^X code
    buffer, GUI **CPU** menu with live switching, gauge window, gates~~
    **DONE**.
  - ~~J1 — instruction-fetch code window, block discovery by tracing, block
    cache, physical-page write guard~~ **DONE**. Measured on
    `q605_boot_etalon`: window alone **−53 %** (58.7 s → 27.6 s, ×2.13); window
    + blocks −18 %; window off +6 %. The block cache is consequently **off by
    default** — blocks average 1.04 instructions on branch-dense 68k code, so
    its bookkeeping costs more than its replay saves. Gates
    `jit_backend_test`, `jit_lockstep_test`, `jit_lockstep_blocks_test` and
    the four `jit_*_boot_etalon` twins. Off by default everywhere.
  - [ ] **J1c — a DATA window, the same trick on the data path.** Likely the
    best remaining return per unit of work, and it is not code generation.
    The arithmetic: the interpreter took 58.7 s, the instruction-fetch window
    removed ~31 s of that, and a good part of the remaining 27.6 s is the
    *same* cost on the other side — every `mmu040Read`/`mmu040Write` still
    does an ATC probe, a virtual `read16()` and the machine's whole address
    decode. The mechanism, the probe, the guard and the gates already exist.
    Harder than the fetch, and honestly so: data accesses fault (protection,
    write-protected pages, page-crossing), writes must maintain the M bit,
    and the fault-frame shape is load-bearing. So the window would have to
    validate per-page permissions and bail to the normal path on any doubt.
    **Measure a prototype before committing to it.**
  - [x] **x64 × PGO divergence — RESOLVED 2026-07-28 (same day).** It was
    never PGO, never the code generator, and never UB (valgrind: 0 errors).
    It was the documented "ATC pseudo-LRU divergence" class turning
    guest-visible: a window/TLB entry could outlive the ATC entry it derived
    from, so each engine skipped a different subset of table walks — and
    walks write the descriptor U bit, which Mac OS VM READS for page aging
    once the System is up. Three engines, three different-but-each-valid
    futures; every binary/config perturbation reshuffled the pattern, which
    mimicked memory corruption. Fix: `pomJitAtcEvict` — derived state dies
    with its ATC entry (both eviction sites), so a window hit now implies
    the interpreter would have ATC-hit too, and all engines are bit-exact
    over 20 G cycles (fp 8f26fcba22986fc6 × interp/interp+window/threaded/
    x64). Fallout: capped at ATC coverage, the INTERPRETER's data window
    stopped paying and is now opt-in (POM68K_DATA_WINDOW=1); the x64 keeps
    its inline TLB. ~~Re-measure engine timings on an idle host~~ **DONE
    2026-07-30** (idle host, 2 runs each): Q605 boot interp 61.3-61.6 s →
    threaded window 32.3 s (×1.90) → x64 25.3-26.0 s (×2.40); `auto`
    25.2 s (picks x64, as designed). **LC II boot: interp 137.8-152.3 s,
    window 141.1-143.8 s — the window is now NEUTRAL-to-slightly-negative
    on the LC II**, retiring the old "×1.6" figure: the LC II boots with
    the PMMU on, and the bit-exactness ATC capping evicts its windows
    exactly as on the LC III (−9 % there). The 030 fleet's next lever is
    the x64 widening + block linking, not the fetch window.
  - [x] **Extend the seam to the 030 machines — ALL FOUR FAMILIES DONE
    2026-07-28** (V8, then Sonora/VASP/RBV in the seventh pass; every 68030
    machine is behind the engine, gates registered one per family).
    Follow-ups that MEASURE, in order: (1) O(1) 030 code probe — the
    22-entry scan runs on every re-arm and the MMU-on machines re-arm
    constantly because ATC evictions kill their windows (the bit-exactness
    contract); an mmuAtcLast-style memo would cut most of it. (2) The 020
    machines (MacIIMemory + LC): different fetch seam — prefetch queue, no
    mode-5 loop — worth doing only after (1) proves the 030 families can
    actually WIN with the MMU on. UPDATE same day: (1) DONE — O(1) memo
    probes + per-space eviction took the LC III from −32 % to −9 % under
    the JIT; the residue is the unified 22-entry ATC evicting code pages,
    which the exactness contract forbids the window to survive. (2) DONE
    as a SEAM (pomJitFetch020 + identity probe + generic pomJitExecOne;
    the LC boots on both engines, gate jit_lc_boot_etalon) and measured
    honestly: −56 % on the LC, because a no-MMU fetch is too cheap for a
    fetch window to beat. Mac II map plumbing (GLUE physAddr, IIx PMMU)
    deferred until a code generator gives the 020 something to win with.
  - [x] (superseded) V8 family first pass
    (LC II/Classic II/CC/Mac TV through the same engine; `mmuFetchWord` is
    the single 030 fetch choke point, so the seam is smaller than the
    040's). Remaining: replicate the V8Memory plumbing (codeSpan/dataSpan/
    guard + wrapper engine member — mechanical, use the V8 diff as the
    template) for SonoraMemory, VaspMemory, RbvMemory; then the 020s
    (MacIIMemory + the LC), which need a different fetch seam (prefetch
    queue, no mode-5 loop).
 — probably better value
    than J2 too: the engine is generic, only the fetch sites are 040-specific,
    and the 030 has its own single choke point (`mmuFetchWord`) which already
    carries `PomIcache`, so the window drops in at the same spot. That is a
    dozen more machines at ~×2 for modest work.
  - [x] **J2 — x86-64 code generation** — DONE 2026-07-28, and it is
    correct but not yet a win. `src/jit/backends/JitBackendX64.cpp` reaches
    70 % native coverage on a Mac OS 8.1 load phase and is bit-exact against
    the interpreter (registers, stacks, clock and low RAM, every boundary),
    but it is 2.3x SLOWER than the fetch window once the Finder is idle, so
    `auto` keeps `threaded` and it ships as `POM68K_JIT_BACKEND=x64`. The
    two follow-ups, in order, are below.
  - [ ] **The LLE-conformant performance backlog** (2026-07-28, ranked by
    yield; the measured conformant ceiling is ~×2.5-3 on the 040s — anything
    beyond that lives in the non-conformant fast mode, docs/HLE_OVERLAY.md):
    1. ~~**Extend PGO training to the 030/020 machines.**~~ **DONE
       2026-07-29** (`tools/pgo_train.sh`): one boot per CPU family —
       Quadra 605 (040+MMU), LC II (030, MMU off), LC III (030, MMU on),
       LC (020+HMMU) — on BOTH engines, instead of the Quadra boot alone.
       **Measured: LC II boot 145.0 / 144.2 s → 107.0 / 107.6 s (−26 %,
       ×1.35); LC 68020 boot 96.2 s → 84.3 s (−12 %, ×1.14)** — one
       sample each on the 020, two on the LC II. Same Release flags on
       both sides, gates passing with identical signatures. The old
       Quadra-only profile had left every 030/020 path cold, and the
       gain lands on the DEFAULT engine, which is the one users run.
    2. **Lazy condition codes in the x64 backend** (own entry below) — the
       one big conformant codegen lever left.
    3. ~~**Page-granular dispatch tables for the memory maps.**~~
       **DROPPED 2026-07-29 — measured, and the premise was wrong.** The
       item assumed a deep range-compare cascade on every access. Counting
       accesses by destination over an LC II boot (1 475 M accesses):
       **RAM 69.6 %, ROM 29.3 %, I/O 0.33 %, other 0.80 %**. So 99 % of
       accesses land on RAM or ROM, whose paths are already 2-4 *perfectly
       predicted* compares (and `V8Memory::ramIndex` was inlined for this
       in 2026-07-17), while the long cascades a table would shorten are
       the I/O ones — a third of one percent of traffic. A 4 KB table
       would put a **dependent load** in front of the 99 % case to save
       branches that cost nothing, and is a near-certain net loss.
       Re-open only with a profile showing decode as a real share; the
       honest lever for I/O-heavy code would be per-device caching, not a
       global table.
    4. **Compact mmu040InstrStart.** Eight per-instruction field resets +
       a getCCR() pack; adjacent fields could collapse into one or two wide
       stores. Small, but it sits on every single 040 instruction.
  - [x] ~~**Wire the GUI engine switch for the 030 machines**~~ **DONE
    2026-07-30**: `Cmd::CpuEngine` + engine/gauge hooks on `LcMachine` and
    `SonoraStyleMachine` (V8 + Sonora/VASP/RBV run sites), the exact
    QuadraMachine pattern; the menu hint now says 68030/68040. Honesty
    note: the motivating "×1.6 on the LC II" turned out STALE — the same
    day's idle-host re-baseline measures the window neutral on the LC II
    boot (PMMU on → ATC evictions kill windows, the bit-exactness cap).
    The switch ships anyway: it is the honest control surface, and the
    day the x64 backend covers the 030s the menu is already there.
  - [x] ~~**Lazy condition codes in the x86-64 backend**~~ **DROPPED
  2026-07-29 — measured ceiling ≈0.8 %.** Method: DUPLICATE the flag
  emission (storing the same byte twice is semantically a no-op, so the
  guest is unaffected and the delta is the marginal cost of one full
  materialisation set). Q605 boot on the **x64 backend**: 26.13 / 26.15 s
  → 26.37 / 26.30 s, i.e. **+0.8 % for a whole extra set**. Lazy CC can
  only remove the DEAD subset of that, so well under 1 % — not worth an
  intricate codegen change that silently breaks bit-exactness when wrong.
  The backlog's "a third off the per-instruction contract" holds for
  contract SIZE, not for time.
  **Correction**: the first published figure (2.5 %) was measured with
  `POM68K_CPU_ENGINE=jit` alone, which selects the **threaded** backend —
  so the modified x64 code never ran and the delta was pure noise. Any
  x64-backend measurement must set `POM68K_JIT_BACKEND=x64` explicitly
  (see the `auto` item below).
- [x] ~~**`selectBackend("auto")` can never pick x64**~~ **DONE 2026-07-30.**
  The `dflt` filter is gone, so `auto` now tries entries in order — the Q605
  gains its measured 32.6 s → 26.1 s, and `jit_*_boot_etalon` finally gives
  the x86-64 generator end-to-end boot coverage.
  **This item's own caution ("flip it only behind boot-etalon coverage")
  earned its keep immediately:** with the filter lifted, `auto` also handed
  x64 the **68030** machines, which it was never written for, and
  `jit_lcii_boot_etalon` wedged in the ROM's Egret handshake loop and timed
  out at an hour. So the flip needed a second half — backend validity is now
  declared per GUEST family (`BackendCaps::guestFamilies`, `JitBackend.h §
  GuestFamily`): `threaded` = every family (it replays Moira's own handlers),
  x64 = 68040 only. Selection tests guest validity before host ranking.
  Full story + the wrong turn in CHANGELOG 2026-07-30.
- [ ] **Widen the x86-64 backend to the 68030 family** (currently
  `kGuest68040`, so every 030 machine runs `threaded` under `auto`). The
  divergence is **localized**: with `POM68K_JIT_ACCESS_THUNK=0` — which hands
  every memory-touching instruction back to the interpreter — x64 boots the
  LC II to the Finder, so what breaks is the natively-compiled memory-access
  path, not the rest of the emitters. Work items, in order:
  - a 68030 branch in `pomJitProbeData` (it returns false below `M68EC040`
    today, so the inline DTLB never fills on an 030 at all);
  - model-correct access thunks: `pomJitReadData`/`pomJitWriteData` call
    `mmu040Read`/`mmu040Write` unconditionally;
  - the 030's `(An)+` update order (before the access, not after —
    `MoiraDataflow_cpp.h:326-332`), its restartable last write (`:355-361`),
    and the end-of-instruction prefetch refill that makes `queue.irc` mean
    something different at a block exit;
  - **an `lcii`/x64 lockstep gate first.** `jit_lockstep_test` and friends run
    two **Quadra 605** machines, so an 030 code generator would have no
    differential coverage at all — and this bug is exactly what that costs.
- [ ] **Block linking — the one thing that would make J2 pay.** Blocks run
    284 instructions per entry while the guest loads the System and **4.9**
    once the Finder is up: Finder-era 68k is branch-dense enough that a
    block entry (hash lookup, frame, prologue, epilogue) is paid every five
    instructions. A branch whose target is another compiled block must jump
    straight into it. Everything it needs already exists — the target is a
    compile-time constant and the block cache is keyed on (pc, super) — what
    is missing is a patchable exit and an unlink-on-evict list.
  - [ ] **Generated-code density.** ~150 bytes of host code per guest
    instruction, most of it the per-instruction contract (budget and flag
    guards, POLL_IPL, pc/pc0, the prefetch queue, the cycle charge) rather
    than the operation. The boundary state is only read when a block exits,
    so it belongs in the cold exit stubs; the guards' two `rel32` jumps
    belong in `rel8`. An interpreter's footprint is bounded by the
    instruction set, a code generator's by the guest program — this is what
    decides whether generated code fits in cache at all.
  - [ ] **aarch64 backend** — porting note already written and validated
    against the IR: `src/jit/backends/JitBackendA64.md`.
  - [x] Fine-grained block eviction — DONE 2026-07-28. The guard went from
    4 KB to 256-byte granularity, `serviceGuard()` evicts only the blocks
    overlapping the written slices, and a `slice -> blocks` index makes that
    O(blocks in the slice). One boot phase went from 5 313 whole-cache
    flushes to 27.

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

## Test & validation depth (audit 2026-07-24, re-counted 2026-07-25)

Finding from the doc-sync + machine-matrix pass: the gates prove **boot**,
not **use** — and the machine fan-out made the ratio *worse*, not better.
Of the **25 machine profiles** covered by the **90 gates**, only **three**
have any gate past the Finder signature: **Mac II** (`macii_mouse_etalon`),
**Quadra 605** (`q605_cudalle_mouse_etalon` / `_key_etalon`) and **LC II**
(`lcii_soak/persist/launch_etalon`, added 2026-07-24). The other **22**
(LC, Classic II, Color Classic, CC II, Mac TV, IIsi, IIci, IIx, IIcx,
LC III/III+, LC 520/550, IIvx/IIvi, Centris 610/650, Quadra 610/650,
LC 475/575) are **boot-to-Finder signature only**. A machine can pass its
etalon and still be broken for real work; green gates read as more coverage
than they give — this is now the single biggest gap in the project.
Highest-ROI closers, in order:

- [x] ~~**Stability / soak gate**~~ **DONE 2026-07-24 on the LC II**
  (`lcii_soak_etalon` — idle uptime + the Mac clock tracking emulated time;
  it is what caught the 37 % MCU overclock). **Still open: a second machine**
  — the Quadra 605 / Mac OS 8.1 soak, which also exercises the 53C96 WRITE
  path, is the next one (blocked on nothing but the work).
- [x] ~~**Input-delivery gates for the boot-only machines**~~ **DONE
  2026-07-29** (`family_input_etalon` — one binary, gates
  `lc3_input_etalon` / `lc520_input_etalon` / `iivx_input_etalon` /
  `iisi_input_etalon`): boot Sys 7.5 blind, inject deltas on the
  bit-serial AdbLine, and require the input to arrive — the whole
  wire→MCU-autopoll→VIA-SR→ADB-Manager→driver chain. Sonora/VASP assert
  the low-memory Mouse ($0830) + a KeyMap ($0174-$017B) bit; the **IIsi
  asserts on SCREEN PIXELS** (idle frames identical, injected motion
  repaints the cursor) because on a RAM-based-video machine physical low
  RAM *is* the framebuffer — see the retraction below. Diag knob:
  `POM68K_INPUT_ANYPATH=1` runs any of them on the HLE path.
- [x] ~~**IIsi: the ADB Manager never initializes**~~ **RETRACTED
  2026-07-29 — there was never a bug. The IIsi mouse works.** Three
  rounds of "findings" (mouse driver not binding → no ADB at all →
  ADBBase never written) were all one measurement artifact: the gate and
  every probe read low memory with `peek8()`, which is PHYSICAL, and the
  IIsi is a **RAM-based-video** machine — physical low RAM *is* the
  framebuffer, and the ROM uses the PMMU (TC=$80F84500, translation on)
  to put the System's logical low memory somewhere else. So "ADBBase"
  was screen pixels all along: the `$55555555` is the 50%-gray desktop
  pattern, `$FFE7C7FF` is dithered content, the zeros are black, and the
  "QuickDraw fill loop writing zeros over the globals" at `$4082D01A`
  was QuickDraw painting the screen — exactly as it should.
  Settled by an MMU-independent check: capture the framebuffer, inject
  motion, capture again. Idle diff **0 px**, after motion **46 px** —
  the cursor moves. `iisi_input_etalon` now asserts that, and is
  registered. Keep the lesson, not the bug:
  - **`peek8()` is physical.** On any machine whose ROM relocates low
    memory behind the MMU, guest-global assertions are meaningless.
    Check `TC` bit 31 before trusting a low-memory read, or assert on
    something the MMU cannot move — pixels, wire traffic, device state.
  - **Corroborate before concluding.** Every round produced a coherent
    story (spin at `$4080A8E6`, 51 relocation passes, "nobody writes
    ADBBase") and each was consistent with the artifact. A single cheap
    end-to-end check — does the cursor move? — would have killed all
    three at the start. Prefer the observation closest to the user's
    experience before the one closest to the code.
  - A real gate bug WAS found en route and fixed: the keyboard check
    scanned 16 bytes at `$0174` when KeyMap is exactly 8, so `$017D=$41`
    read as a keystroke. A positive assertion over a too-wide window is
    a false green.

- [x] ~~**"Real work" functional gates on one reference machine**~~ **DONE
  2026-07-29 on the LC II** — the four `lcii_beyond_etalon` scenarios:
  `soak` (idle uptime + Mac clock tracking), `persist` (Cmd-N creates a
  folder, the SCSI image changes, and a hard reset boots back off the
  modified volume), `launch` (mouse double-click opens a window and pulls
  SCSI reads), and **`floppy`** (new): insert an 800K HFS medium after
  the Finder is up and the System polls the drive, reads ~1.7 M nibbles
  over the real IWM, mounts the volume and opens its window; the medium
  survives eject + re-insert byte-intact. That closes the guest-side
  floppy READ path, which had no gate at all.
- [ ] **Floppy: a guest-INITIATED write.** The `floppy` scenario proves
  mount+read but asserts nothing about writing, because no gesture has
  yet made the guest write to the medium. Evidence gathered 2026-07-29:
  the volume mounts read-write (`isWriteProtected()` = 0) and its window
  auto-opens (the changed screen region is the whole upper screen, x
  3..494 / y 2..240), but a Cmd-N in that state modifies **neither** the
  floppy nor the hard disk — while the identical Cmd-N in `persist`
  works on the HD. So the keystroke is being dropped in the
  post-insert state rather than landing on the wrong volume. Next: check
  whether the modifier is stuck or the Finder is still busy (vary the
  settle time before Cmd-N), and if the UI route stays flaky, drive the
  write from a script/app on the boot volume instead. The device-side
  write→eject→flush plumbing is already gated by `floppy_persist_test`.
- [ ] **Widen per-machine System coverage.** Each profile's etalon pins one
  reference image (`GISTPERSO`, Infinite Mac 8.1…). The `finder_boot_matrix`
  helps, but functional coverage across images is thin — the exact
  calcify-around-one-image trap `LLE_VS_HLE.md` warns about. Add matrix cells
  as images are validated.

Notes / already tracked:
- **Save states** would double as a test accelerator (instant setup, snapshot
  regressions) — see "Implement save states" above.
- **Floppy write persistence** now exists (GUI/Plus, `floppy_persist_test`,
  atomic temp+rename); tests stay in-memory by design.
- **Per-machine LLE-completeness estimates** (±5 pts), re-scored 2026-07-25
  for the five newest machines: Plus ~85, Q605 ~80, LC II ~80, LC 475/575
  ~79, Mac II ~78, LC / LC III/III+ ~75, Centris/Quadra 610/650 ~74,
  Classic II / LC 520 family ~73, IIx/IIcx ~73, IIvx/vi ~72, IIsi / IIci
  ~70, Color Classic ~70, Mac TV ~70. Common ceilings: whole-frame video
  everywhere, cycle-exact CPU only on the Plus, no beyond-boot gate on 22 of
  25 profiles. Lowest scores are **freshness** (booted-once, not hardened —
  RBV/Tinker Bell/VASP/AIO). The last known-wrong MCU default fell
  2026-07-29: the Color Classic runs its factory 341S0417 (the "wedge" was
  the missing DFAC2 I2C ACK) and the Mac TV its factory 341S0789.

## Future machine profiles

Driven by **Phase C** of the Finder matrix above; detail and effort tiers in
`docs/68K_FAMILY_SCOPE.md`.

- [x] ~~**Macintosh LC (68020)**~~ **DONE 2026-07-24** (`lc_boot_etalon`).
- [x] ~~**Nearby 68030**: Classic II → Color Classic → LC III~~ **DONE
  2026-07-24** (Sonora/Cuda/SWIM2 gated devices; gates `classic2_`,
  `cclassic_`, `lc3_`, `lc3plus_boot_etalon`). Plus the AIO family
  (LC 520/550/CC II), IIvx/IIvi, **Mac TV** (Tinker Bell), **IIsi** + **IIci**
  (RBV) and **IIx** + **IIcx** (68030 Mac II) — all 2026-07-25. **Remaining
  030:** IIfx (OSS + IOPs) and **SE/30** (compact IIx + built-in video, no
  ROM dump on hand). **Compact 68000: SE / SE FDHD / Classic DONE
  2026-07-25** (`se_`, `sefdhd_`, `classic_boot_etalon` — a `MacMemory::
  Model` enum + the PIC1654S ADB LLE, no new machine).
- [x] ~~**Nearby 68040**: LC/Performa 475 identity, LC 575~~ **DONE
  2026-07-24** (`lc475_`, `lc575_boot_etalon`), plus the **Centris 610/650
  + Quadra 610/650** djMEMC+IOSB machine the same day. **Remaining 040:**
  Quadra 900/950 (IOPs), 660AV/840AV. **Quadra 700 DONE 2026-07-25**
  (`q700_boot_etalon` — discrete 040 + DAFB TurboSCSI cell); **Quadra 630 /
  LC 580 DONE 2026-07-25** (`q630_`/`lc580_boot_etalon` — F108 + PrimeTime II
  + Valkyrie; the ATA/IDE port is mapped but has no drive, so boot is SCSI —
  modelling an ATA target is the remaining gap on that machine).

- [ ] **NuBus + slot video** beyond Mac II Toby: IIx/IIcx/IIci and NuBus
  Quadras. (VASP/IIvx currently reads its three slots as empty — real
  cards would reuse the Mac II NuBus/DeclRom port.)

- [ ] **Independent majors — the only things left that are not a ROM dump**
  (see `docs/68K_FAMILY_SCOPE.md` § "After the Quadra 630"):
  - [ ] **AppleP IC IOP + OSS** → unlocks **IIfx** *and* **Quadra 900/950**.
        The IOP (Apple 343S1021, MAME `machine/applepic.cpp`) is a 65C02 core
        + 2 KB shared RAM + 2 DMA channels + host/peripheral mailboxes + a
        timer. POM68K has no 6502-class core; POMIIGS's `CPU65816` (825
        lines, emulation mode = 65C02) is the candidate to vendor. The IIfx
        also has **no built-in video** — it boots on a NuBus card, so the
        existing `TobyVideo`/`DeclRom` path has to carry it.
  - [ ] **Power Manager** → Portable / PowerBook 1xx / Duo. The 68HC05 half
        already ships (`M68hc05`), so the PB150 is the cheapest entry.
  - [ ] **AV DSP (DSP3210)** → 660AV/840AV. Not planned.
- [x] ~~**Mac TV** (EAF1678D / Tinker Bell)~~ **DONE 2026-07-25** —
  `V8Memory::Model::MacTv`, gate `mactv_boot_etalon` (see the Phase C
  entry above). The EDE66CBD `$2000-$2003` probe had been the wrong
  machine (`docs/LC520_BRINGUP.md` § Siblings).

### Remaining machines, with the ROM already in `roms/` (cheapest first)

| Machine | ROM on hand | New brick |
|---|---|---|
| **SE / SE FDHD / Classic** — *started 2026-07-25* | `B2E362A8` / `B306E171` / `A49F9914` | **the "compact ADB MCU" brick does not exist**: MAME drives the SE's ADB through the same `adbmodem` PIC1654S we already run (`mac128.cpp` `set_via_state`). `MacMemory::Model {Plus,SE,SEFDHD,Classic}` is in (256/512 KB ROM, overlay auto-clear on first ROM access, ADB on PB3-5 + CB1/CB2 replacing the M0110 + quadrature). **State: the SE ROM executes and the PIC LLE attaches, but the POST spins at `$402A94`/`$402752` and never reaches SCSI.** Next: trace what that loop polls (VIA? RTC? sound? the SE's PB6 SCSI-IRQ mask?) with the scratchpad tracer. |
| **Quadra 630 / LC 630** | `06684214` | the 630's I/O controller variant (last 68k desktop) |
| **LC 580 / Performa 580** | `064DC91D` | AIO sibling of the 630 board |
| **SE / SE FDHD / Classic** | `B2E362A8` / `B306E171` / `A49F9914` | compact ADB-over-VIA transcoder MCU (not Egret) |
| **SE/30** | (needs a dump) | the same compact MCU + 030 + compact video |
| **Quadra 900 / 950** | `420DBFF3` / `3DC27823` | two **AppleP IC** IOPs (6502-class, SCC + SWIM) + Egret — the same brick the IIfx needs |
| **Mac IIfx** | `4147DD77` | OSS + two 6502-class IOPs |
| **Portable / PowerBook 100** | `96CA3846` / `96645F9C` | LCD framebuffer + M50753 (740/6502) Power Manager |
| **PowerBook 140-180 / Duo** | `E33B2724` / `0024D346` / `015621D7` | LCD framebuffer + Power Manager (68HC05 on the Duos) |
| **PowerBook 150** | `FDA22562` | LCD + 68HC05 PM — the `M68hc05` core already ships |

Effort tiers and the full family map: `docs/68K_FAMILY_SCOPE.md`.
