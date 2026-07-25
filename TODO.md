# TODO

Active work only. Completed milestones, resolved bugs, investigation trails and
design rationale belong in `CHANGELOG.md` (with implementation detail in
`DEV.md` and the vendor notes). The LLE-vs-HLE inventory and migration plan
live in `docs/LLE_VS_HLE.md`.

## Usability & proof (make the machines we have actually usable)

Rather than adding a machine, prove and harden the ones we have (ROI order:
data-loss holes and false test-confidence first, then convenience).
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
- [ ] **C — Save states**: CPU (Moira has a serialization format) + RAM +
  device state. User-expected; turns any manual setup into an instant
  regression. Next up.
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
  **Follow-up**: the factory 341S0417 (Cuda 2.35) wedges on the
  M68hc05 — releases the host reset, never answers the VIA transport;
  diagnose the 2.35 firmware path and switch the default back.
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
  `quadra650/quadra610_boot_etalon`; GUI entries. Quadra 800 (SONIC + NuBus)
  is the remaining macquadra800.cpp sibling.
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
  7. **SCC: OS 8.1/OT hangs on the standing no-peer abort.** OT's LLAP
     driver (unlike Sys 7's) waits forever for the §1.8 standing
     Break/Abort to clear before binding .MPP (spin at $D1F04; last
     SCC access reads RR0=$D4 with bit 7 set, then silence — SCCDBG
     capture 2026-07-24). Test env `POM68K_SCC_CLEANLINE=1` (idle
     line = clean flags) unblocks it: with it, Netscape resolved and
     contacted www.apple.com through MacIP/NAT — the first real
     internet access from a POM68K guest. Turn the env into the real
     LLE fix: present the abort only while a genuine abort condition
     exists (line state, not machine config), keep the Sys 7 no-peer
     etalons green, add an OT-flavored gate. Then retire the env.
     **Remaining after that**: retire the `Egret.*`/`AdbBus` HLE (and
     the Mac II §1.9 leftover) once the no-dump fallbacks feel
     redundant.
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

## Test & validation depth (audit 2026-07-24)

Finding from the doc-sync + machine-matrix pass: the 73 gates prove **boot**,
not **use**. Of the 15 machine profiles, only **Mac II** (`macii_mouse_etalon`)
and **Quadra 605** (`q605_cudalle_mouse_etalon` / `_key_etalon`) have a gate
that exercises anything past reaching the Finder — the other 13 (LC, LC II,
Classic II, Color Classic, LC III/III+, LC 520/550, CC II, IIvx/IIvi,
LC 475/575) are **boot-to-Finder signature only**. A machine can pass its
etalon and still be broken for real work; green gates read as more coverage
than they give. Highest-ROI closers, in order:

- [ ] **"Real work" functional gates on one reference machine** (LC II or
  Quadra 605): drive the Finder to (a) launch an app, (b) create + save a
  file to the SCSI volume, reboot, assert the file survives (rides the
  `ScsiDisk` writeBack path — already persistent), (c) a floppy
  write→eject→reinsert→read round-trip. Catches the silent-data-loss and
  Toolbox-regression class that framebuffer signatures cannot see.
- [ ] **Stability / soak gate**: run N minutes of emulated idle uptime at the
  Finder on one machine and assert no hang, no runaway (PC/heat-death), no
  IRQ storm. Cheap; catches timing/tick-batch regressions the short boot
  etalons miss.
- [ ] **Input-delivery gates for the boot-only machines** (reuse the
  `macii_mouse` / `q605_cudalle_mouse` harness): at least prove the ADB
  firmware-LLE path actually delivers mouse+key on the Sonora/VASP families,
  not just that it boots. Ties into retiring the HLE ADB fallbacks
  (`docs/LLE_VS_HLE.md` §2).
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
- **Per-machine LLE-completeness estimates** (±5 pts) from the same pass, for
  reference: Plus ~85, Q605 ~80, LC II ~80, LC 475/575 ~79, Mac II ~78, LC /
  LC III/III+ ~75, Classic II / LC 520 family ~73, IIvx/vi ~72, Color Classic
  ~70. Common ceilings: whole-frame video everywhere, cycle-exact CPU only on
  the Plus. Lowest scores are freshness (VASP/AIO booted-once, not hardened)
  and the one known-wrong default (Color Classic runs the substitute Cuda
  341S0788 — factory 341S0417 wedges the M68hc05, tracked under Color Classic
  above).

## Future machine profiles

Driven by **Phase C** of the Finder matrix above; detail and effort tiers in
`docs/68K_FAMILY_SCOPE.md`.

- [x] ~~**Macintosh LC (68020)**~~ **DONE 2026-07-24** (`lc_boot_etalon`).
- [x] ~~**Nearby 68030**: Classic II → Color Classic → LC III~~ **DONE
  2026-07-24** (Sonora/Cuda/SWIM2 gated devices; gates `classic2_`,
  `cclassic_`, `lc3_`, `lc3plus_boot_etalon`). Plus the AIO family
  (LC 520/550/CC II), IIvx/IIvi, **Mac TV** (Tinker Bell), **IIsi** + **IIci**
  (RBV) and **IIx** + **IIcx** (68030 Mac II) — all 2026-07-25. **Remaining
  030:** IIfx (OSS + IOPs) and **SE/30** (compact IIx + built-in video);
  **compact 68000: SE/Classic** (ADB).
- [x] ~~**Nearby 68040**: LC/Performa 475 identity, LC 575~~ **DONE
  2026-07-24** (`lc475_`, `lc575_boot_etalon`). **Remaining 040:**
  Centris/Quadra 610/650/800.

- [ ] **NuBus + slot video** beyond Mac II Toby: IIx/IIcx/IIci and NuBus
  Quadras. (VASP/IIvx currently reads its three slots as empty — real
  cards would reuse the Mac II NuBus/DeclRom port.)

- [ ] **Independent majors**: PowerBook PMU, IIfx IOPs, 660AV/840AV DSP.
- [ ] **Mac TV** — BLOCKED on the **EAF1678D** ROM / Tinker Bell ASIC
  (not EDE66CBD). Dump on hand (`roms/mactv/eaf1678d.bin`); needs a
  Tinker Bell map + `$EAF1678D` dispatch. The EDE66CBD `$2000-$2003`
  probe was the wrong machine (`docs/LC520_BRINGUP.md` § Siblings).
