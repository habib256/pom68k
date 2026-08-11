# DEV.md — implementation deep-dives

**What this file is.** The index to the non-obvious internals: the facts a
port depends on, and the *reason* each one is the way it is. The code is
the documentation — every entry here points at a class, a file or a MAME
line and stops. When a section and the code disagree, **the code wins**;
fix the section.

**What lives elsewhere.** Per-machine bring-up narratives (what broke, in
what order) → `CHANGELOG.md`. Machine roster and one-line status →
`CLAUDE.md`. Backlog → `TODO.md`. User-facing walkthrough → `README.md`.
JIT design → `src/jit/POM68K_JIT.md`. Moira's local changes →
`extern/moira/POM68K_VENDOR.md`. LC II research, AppleTalk, HLE inventory
and the LC 520 bring-up → `docs/`.

---

## Index

| Looking for | Go to |
|---|---|
| Why a change to the CPU clock broke a *device* | [§1.2 Bus time is machine cycles](#12-family-wide-invariants) |
| Why unmapped I/O must read 0, not `$FF` | [§1.2](#12-family-wide-invariants) |
| Which pseudo-VIA flavour a machine gets | [§1.2](#12-family-wide-invariants) |
| How the boot overlay clears (it differs per family) | [§1.2](#12-family-wide-invariants) |
| Where POM68K hooks into Moira | [§1.3 CPU integration seam](#13-cpu-integration-seam) |
| How save/load cannot drift apart | [§1.4 Save states](#14-save-states-the-visit-contract) |
| A machine's address map, ID longword, straps, gates | [§2 Platforms](#2-platforms) |
| Floppy, SCSI, ADB, MCU, SCC, DAFB internals | [§3 Shared devices](#3-devices-shared-across-platforms) |
| The JIT | [§4](#4-jit--the-second-execution-engine) → `src/jit/POM68K_JIT.md` |
| Every environment variable the code reads | [§5 Environment knobs](#5-environment-knobs--the-complete-list) |
| Which `ctest` tier to run | [§6 Test tiers](#6-test-tiers-and-gates) |

Platform sections are **one per board generation**, each with a reference
machine; the other members of a family are identity/clock variants listed
inside that section. The last row has no § here yet: the Duo boots
(`duo230_boot_etalon`) but is still a bring-up, so its blueprint lives in
`docs/DUO_BRINGUP.md` until the platform earns a GUI profile.

| Platform | Reference machine | Variants in the same section | § |
|---|---|---|---|
| 68000 + PAL glue | **Mac Plus** | SE, SE FDHD, Classic (`MacMemory::Model`) | [2.1](#21-68000--pal-glue--mac-plus-se-se-fdhd-classic) |
| GLUE + NuBus | **Mac II** | IIx, IIcx, SE/30 (68030 on the same board; the SE/30 is the compact IIx) | [2.2](#22-glue--nubus--mac-ii-iix-iicx-se30) |
| V8 gate array | **Mac LC II** | LC, Classic II (Eagle), Color Classic (Spice), Mac TV (Tinker Bell) | [2.3](#23-v8-gate-array--mac-lc-ii-lc-classic-ii-color-classic-mac-tv) |
| RBV (RAM-based video) | **Mac IIsi** | IIci (PIC ADB modem + discrete RTC) | [2.4](#24-rbv-ram-based-video--mac-iisi-iici) |
| Sonora gate array | **Mac LC III** | LC III+, LC 520/550, Color Classic II; **VASP** = IIvx / IIvi | [2.5](#25-sonora-gate-array--lc-iii-lc-iii-aio-family--the-vasp-recombination) |
| MEMCjr + PrimeTime | **Quadra 605** | LC 475, LC 575 | [2.6](#26-memcjr--primetime--quadra-605-lc-475-lc-575) |
| djMEMC + IOSB | **Centris 650** | Centris 610, Quadra 610/650/800 | [2.7](#27-djmemc--iosb--centris-650-centris-610-quadra-610650800) |
| Discrete 040 + DAFB | **Quadra 700** | Quadra 900 / 950 (the same board + the IIfx's IOP front end) | [2.8](#28-discrete-040--dafb--quadra-700) |
| F108 + PrimeTime II + Valkyrie | **Quadra 630** | LC 580 | [2.9](#29-f108--primetime-ii--valkyrie--quadra-630-lc-580) |
| OSS + 2 Apple PIC IOPs | **Mac IIfx** | (Quadra 900/950 reuse the IOPs on the Q700 board) | [2.10](#210-oss--two-apple-pic-iops--mac-iifx) |
| MSC + PG&E Power Manager | **PowerBook Duo 230** | (210/250/270c/280, then PB150 — none wired) | `MscMemory`/`MscCpu`/`PgePmu`/`M68hc05Pge`; blueprint `docs/DUO_BRINGUP.md` |

---

## 1. Ground rules

### 1.1 Sources, ranked

Every subsystem port must cite one of these **plus a gate test**:

1. **MAME** `src/mame/apple/*` + the `m68000` family — primary hardware
   reference.
2. **Guide to the Macintosh Family Hardware** 2e (GttMFH) + *Inside
   Macintosh III*.
3. **Hatari/WinUAE** 68k timing and MMU/FPU cores — the differential oracle
   (`oracle/`). On spec/oracle conflict, the oracle wins.
4. **pce-macplus / Mini vMac / Basilisk II** — behavioural cross-checks.

The Plus material is cross-checked across MAME `mac128.cpp`, pce-macplus,
Mini vMac, GttMFH and *Inside Macintosh* III (web research, 2026-07-14).
LC II and Quadra details use MAME's Apple machine/device models plus the
real ROM and System drivers as protocol oracles. Full citation list: [§7](#7-sources).

### 1.2 Family-wide invariants

The four rules below were each learned by breaking one machine and then
finding they applied to five. They are here, not in a platform section,
because that is the mistake they encode.

**Bus time is charged in machine cycles, never in boosted core cycles.**
The 030/040 CPUs carry an i-cache/throughput overlay (`POM68K_*_CACHE_BOOST`),
so the *core* clock is a multiple of the real one. Anything a device can
observe — VIA E-clock alignment (`viaSync`), `stall()`, SWIM C15M sync —
must run off `machineClock()` and be scaled by the boost. The **Mac IIsi**
is the machine that exposed this: its ROM's Egret transport is a *host-paced*
bit-bang (`bclr`/`bset` of VIA1 PB4 / `via_full` back to back), and until
2026-07-25 `viaSync` aligned in the boosted domain, so every pulse came out
`cacheBoost_`× too short and the firmware missed it. Same root cause as the
LC III / IIvx black screens. Fixed on all four 030 CPUs; the whole 040
family is likewise boost-invariant, which is what let the default rise from
1 to 4 (CHANGELOG 2026-07-25).

**Unmapped I/O reads back 0, not open-bus `$FF`.** MAME parity
(`iosb.cpp:54-65`) for the whole V8 / VASP / Sonora / RBV / IOSB line. An
open-bus `$FF` hard-wedges the LC III+ ProductInfo RAM-device poll at
`$50F0A000`, and misleads every ROM device probe that tests for
"nothing there". *Exception*: the Plus deliberately returns
address-dependent open bus above the real ROM window — see the SCSI-probe
story in [§3.3](#33-scsi-ncr-5380).

**There are two pseudo-VIA flavours, and they are not interchangeable.**
`PseudoVia::Flavour` (since 2026-07-27). MAME's `rbv.cpp:66` and
`vasp.cpp:90` instantiate the **base** `APPLE_PSEUDOVIA`, where IFR bit 4
(ASC) latches only the 0→1 *edge* and the guest's write-1-to-ack sticks.
The **level** flavour, with its `~$10` ack mask, belongs to
`v8_pseudovia_device` / `sonora_pseudovia_device` alone. VASP in particular
cannot survive stacking two level behaviours (CHANGELOG).

**The boot overlay clears three different ways.** Plus: only when the ROM
explicitly clears VIA PA4 ([§2.1](#21-68000--pal-glue--mac-plus-se-se-fdhd-classic)).
SE and later 68000/68020 compacts, and every `$40000000`-ROM machine: the
**first read anywhere in the ROM window** clears it (`rom_switch_r`).
Mac II: a **one-way latch** — the System rewrites VIA1 PA with bit 4 set
after Welcome, and re-arming the overlay there opens the bus (CHANGELOG
2026-07-20). Because clearing is a *read* side effect it also invalidates
JIT translations directly, via `jitMapChanged()` ([§4](#4-jit--the-second-execution-engine)).

### 1.3 CPU integration seam

- **Moira precise timing**: `sync()` runs before every bus access —
  RAM contention and VIA E-clock alignment hook there (NeoST pattern:
  `iackSyncBefore/After` for IACK E-clock waits).
- `MOIRA_EMULATE_ADDRESS_ERROR=true`: Mac software (and the oracle phase)
  needs address-error frames.
- **030 PMMU vs 020 HMMU**: once the 030's own PMMU is on (TC bit 31),
  Moira hands the bus a *physical* address, so a machine's own 24-bit
  remap must be **skipped**. `V8Memory` had this first; `MacIIMemory::physAddr`
  needed it for the IIx/IIcx ([§2.2](#22-glue--nubus--mac-ii-iix-iicx-se30)).
- `DemoRom.h` mimics the real boot for gate purposes: DDRA=`$7F`, then
  ORA=`$40` (overlay off + main screen buffer).
- JIT seam inside the vendored core: `extern/moira/POM68K_VENDOR.md`
  § *JIT seam*.

### 1.4 Save states: the `visit()` contract

`SaveState.h` / `SaveState.cpp` / `SaveStateMachines.h/.cpp`, plus one
`template <class Ar> void visit(Ar& ar)` per serializable class.

- **One method drives both directions.** `Writer` and `Reader` instantiate
  the *same* body, so a field added to `visit()` is immediately live on
  save and load. Hand-written `save()`/`load()` pairs are the classic
  emulator save-state corruption source — a field added to one and
  forgotten in the other restores as garbage months later. The visitor
  removes the failure mode by construction rather than by review.
- **What must NOT go through `visit()`** (`SaveState.h:20-30`):
  `std::function` callbacks and inter-device pointers (**re-bound** by the
  machine after restore, never serialized); pure caches (Moira's ATC, JIT
  blocks — **flushed**, re-derivable); host-backed bulk data (ROM, disk
  images — the snapshot carries an identity checksum plus whatever the
  guest has modified since, `ScsiDisk`'s copy-on-first-write log).
- **Refusal is early and total.** The loader validates magic, format
  version, machine profile, ROM checksum and RAM size *before* touching a
  byte of state: a half-applied snapshot is worse than none. Unknown
  chunks are skipped and counted as a warning, not a failure.
- **`SnapMachine`** is one tag per **profile**, not per class — identity
  twins share a ROM (LC III / LC III+, Q605 / LC 475) so the header
  checksum cannot tell them apart. Values are part of the file format:
  append, never renumber. All **34** profiles are enumerated
  (`SaveStateMachines.h`) and each of the **11** machine families has a
  `save`/`load` pair.
- Gates: `savestate_test`, `savestate_v8_test`, `savestate_030_test`,
  `savestate_040_test`, `savestate_68k_test` (all `unit`), plus the
  whole-machine `lcii_savestate_etalon` and `q605_savestate_etalon`.
  The GUI hook shipped 2026-07-30; the remaining item (a hands-on GUI
  pass) is in `TODO.md` § 8.

---

## 2. Platforms

### 2.1 68000 + PAL glue — Mac Plus (SE, SE FDHD, Classic)

`MacMemory` (`Model {Plus, SE, SEFDHD, Classic}`) + `Cpu68k`. **Cycle-exact**
(M0-M7). The compacts are the same map with a bigger ROM, the SE-style
overlay clear ([§1.2](#12-family-wide-invariants)) and **ADB on the
PIC1654S firmware LLE** (PB4/PB5 = ST) in place of the M0110 — see
[§3.6](#36-input-adb--pic1654s-transceiver-lle). `setModel()` exists because
`main()` builds the machine before it has read the ROM, and the compacts are
told apart by its checksum. Gates: `rom_boot_etalon`, `disk_boot_etalon`,
`system_boot_etalon`, `scsi_boot_etalon`, `se_boot_etalon`,
`sefdhd_boot_etalon`, `classic_boot_etalon`.

#### Address map (24-bit)

| Range | Device | Notes |
|---|---|---|
| `$000000-$3FFFFF` | RAM | mirrors modulo RAM size (MAME `offset & ram_mask`) |
| `$400000-$4FFFFF` | ROM 128 KB | mirrored; pce mirrors to `$57FFFF` |
| `$580000-$5FFFFF` | SCSI NCR 5380 | reg = A4-A6 (×16); A0: 0=read 1=write; A9=DACK (pseudo-DMA `$580201`/`$580260`) |
| `$600000-$7FFFFF` | RAM overlay window | RAM lives here while overlay on |
| `$800000-$9FFFFF` | SCC **read** (even, D8-D15) | `sccRBase=$9FFFF8`; A1=channel (0=B), A2=ctl/data; **odd read resets the SCC** (Mini vMac) |
| `$A00000-$BFFFFF` | SCC **write** (odd, D0-D7) | `sccWBase=$BFFFF9` |
| `$C00000-$DFFFFF` | IWM (odd, D0-D7) | reg = A9-A12 (`$200` spacing), `dBase=$DFE1FF`; stub read `$1F` suffices to reach the blinking-? |
| `$E80000-$EFFFFF` | VIA (even, D8-D15) | reg = A9-A12, `vBase=$EFE1FE`=ORB; `$EFFFFE`=ORA_NH (reg 1 never used); E-clock sync via /VPA |
| `$FFFFF0-$FFFFFF` | autovector space | glue asserts /VPA on IACK |

#### Boot overlay

Reset ⇒ OVERLAY=1 (VIA PA4): ROM mirrored over `$000000-$5FFFFF`, RAM at
`$600000-$7FFFFF`. ROM tests RAM at `$600000`, then clears PA4 → normal map.
Writes to the RAM window are discarded while overlay is on (MAME). **SE and
later auto-clear on first `$400000` access — the Plus does not.**

#### Video

- Main buffer **ramTop−$5900**, alt **ramTop−$D900** (main−$8000). 4 MB:
  `$3FA700`/`$3F2700`. `ScrnBase` global = `$0824`.
- 512×342×1 bpp = 21 888 bytes, 64 bytes/row contiguous; **MSB = leftmost,
  1 = black**. VIA **PA6: 1 = main, 0 = alternate**.

#### Sound (M6)

- Main **ramTop−$300**, alt **ramTop−$5F00**; 370 words/frame: even byte =
  8-bit sample, odd byte = disk-PWM (ignored by the Plus's 800K drives).
  One word fetched per scan line ⇒ **22 254.55 Hz** (15.6672 MHz / 704).
  Output is 1-bit PWM into an integrator; we take the byte as unsigned
  linear PCM `(byte-128)/128` (the standard approximation). VIA PA3 selects
  the buffer (1=main); PB7 enable (0=enabled); PA2-0 volume (0-7).
- `MacAudio` extracts the 370 samples/frame; `MacAudioHost` (miniaudio,
  GUI-only) plays them through a lock-free SPSC ring at 22254 Hz. Only
  **non-silent frames** are pushed, so the ring stays drained while the
  machine turbos through the silent RAM test — the startup chime and system
  beeps still play at the right pitch, just slightly delayed.
- The **startup chime** is a clean ~601 Hz (≈D5) tone for ~0.7 s at power-on
  (before the RAM test), then PB7 mutes it. `sound_test` captures it to
  `chime.wav` and checks it is an audible decaying tone in the beep band.

#### VIA 6522 wiring

- **Port A** (`$EFFFFE`, DDRA=`$7F`): 7 in vSCCWrReq · 6 out vPage2 (screen)
  · 5 out vHeadSel (floppy) · 4 out vOverlay · 3 out vSndPg2 · 2-0 out volume.
- **Port B** (`$EFE1FE`, DDRB=`$87`): 7 out vSndEnb (0=on) · 6 in **H4 =
  horizontal blanking** (1 = in hblank; MAME returns a constant `$40` — we
  derive it from the beam counters) · 5/4 in mouse Y2/X2 · 3 in button
  (0=pressed) · 2 out rTCEnb (0=on) · 1 out rTCClk · 0 i/o rTCData.
  Mouse X1/Y1 quadrature → SCC DCD (level-2 IRQs).
- **Clock: φ2 = E = 7.8336/10 = 783.36 kHz** (T1/T2 tick 1.2766 µs).
- IFR bits: 7 IRQ · 6 **T1=Sound Driver** · 5 **T2=Disk Driver** · 4 CB1 kbd
  clk · 3 CB2 kbd data · 2 SR · 1 **CA1=VBL 60.1475 Hz** · 0 CA2=one-second.
- **IPL: 1=VIA, 2=SCC, 4=programmer's switch**, and they are *not* simply
  ORed — see the glue-disconnect trap in [§3.5](#35-input-m0110-keyboard--quadrature-mouse).
  All autovectored. No SCSI IRQ on the Plus (polled).

#### Timing (M4 cycle accuracy)

- Dot clock 15.6672 MHz; CPU C7M = 7.8336 MHz; SCC PCLK = 3.9168 MHz (MAME).
- Line: 704 dots = 512 visible + 192 hblank = **352 CPU cycles**,
  22 254.5 Hz. Frame: **370 lines** (342 visible + 28 vblank), **60.1474 Hz**
  = **130 240 CPU cycles** (hard-coded in Mini vMac and pce; ours matches).
- **RAM contention**: CPU and video alternate 4-cycle slots during the 512
  visible dots — the CPU loses **128 of the first 256 cycles of each visible
  line**, plus **4 cycles per line (all 370)** for the sound-word fetch. ROM
  and I/O are never contended. Validation target: average CPU RAM bandwidth
  **2.56 MB/s** (GttMFH Table 5-3). MAME lump-sums per line; we model it in
  `Cpu68k::contentionDelay`.

#### ROM and RAM sizing

128 KB (two 64 KB byte-lane ROMs, A0 undecoded). Versions by 4-byte Apple
checksum: v1 `$4D1EEEE1` "Lonely Hearts" (SCSI boot bug), v2 `$4D1EEAE1`
"Lonely Heifers" (most common), v3 `$4D1F8172` "Loud Harmonicas". Early
boot: ROM checksum (Sad Mac 01) → RAM tests at `$600000` (Sad Mac 02-05) →
overlay clear → VIA/IWM/SCC init → SCSI probe → beep → blinking-?.
**Minimum to reach the blinking-?** (BMOW Plus Too): CPU + ROM + RAM +
framebuffer + partial VIA (overlay, IER/IFR, CA1 VBL, CA2 one-sec) + an IWM
stub reading `$1F`; SCC/SCSI/RTC reads must merely terminate.

RAM configs: 1/2/2.5/4 MB (two SIMM rows). The ROM sizes RAM itself via
mirror/address-uniqueness tests and stores top+1 in `MemTop` (`$0108`) —
emulators just mirror via a mask and let the ROM discover it.

#### What is and is not modelled (M4)

- Contention: implemented exactly as above (slot-accurate, iterative across
  busy slots); `contention_test` reproduces the 2.56 MB/s figure. RAM only,
  before each bus access (Moira precise-timing `sync` has already run).
- VIA timers: φ2 ticks batched through `MacMemory::tick` from the CPU's
  peripheral catch-up. The 6522's ±1-cycle reload/IFR latency is **not**
  modelled, nor is E-clock (/VPA) alignment of VIA accesses — TODO M4.1.
- RTC: full command/read/write serial protocol, 20-byte PRAM, in-memory
  only (no file persistence); seconds start at 0 (deterministic tests).
- PB6 H4 is derived from the true beam position (`clock % 352 < 256`),
  unlike MAME's constant.

### 2.2 GLUE + NuBus — Mac II (IIx, IIcx, SE/30)

`MacIIMemory` (`Model {MacII, IIx, IIcx, SE30}`) + `Cpu020` (`is030` flag).
Functional accuracy.

- **CPU:** Moira's 68020 at 15.6672 MHz; no PMMU (HMMU translation only
  where the ROM needs it), no FPU requirement for Sys 6/7 boot
  (`POM68K_NOFPU`).
- **Memory/GLUE:** the 32-bit GLUE map, ROM overlay (**one-way latch**,
  [§1.2](#12-family-wide-invariants)), VIA1 + VIA2, classic ASC
  (`AscV8` version `$00`, MODE bits 0-1 only, edge half-empty + empty-cycle
  re-IRQ), SCC, IWM, `Rtc` (with `factoryDefaults` SPConfig `$22` seeding)
  and NCR 5380 pseudo-DMA at `$50F060xx` (A0..A1 decoded across the
  `$6000-$7FFF` window).
- **NuBus:** `NuBus` + `DeclRom` model the 32-bit slot windows and
  declaration ROMs; `TobyVideo` is the slot-9 640×480 card with a Bt453
  CLUT (whole-frame decode). VIA2 CA1 slot IRQ fires **only** when the
  `$D04` slot task queue is armed — an empty-queue CA1 gives SysError 51.
- **ADB:** LLE `Pic1654s` by default when `roms/adbmodem/342s0440-b.bin`
  is present; the mouse only moves on that path
  ([§3.6](#36-input-adb--pic1654s-transceiver-lle)).
- **RTC / XPRAM:** the ROM runs **unmodified** since 2026-07-21 — `Rtc`
  speaks the full 343-0042 protocol (falling-edge shift, 256-byte extended
  XPRAM, MAME `macrtc` mapping: classic regs 8-11/16-31 = XPRAM
  `$08-$0B`/`$10-$1F`), so the ROM cold-inits its own PRAM and boots SCSI
  from its own `$78-$7B` defaults (driver refNum −33 = SCSI ID 0).
- **Boot HLE: none left** (LLE steps 1-4, 2026-07-21). Sys 7 EtherTalk
  CautionAlerts are dismissed by the *tests* pressing Return over real ADB
  (`keyEvent $24`); SPConfig `$22` (AppleTalk inactive) is only a
  reset-time factory seed.
- **Variants IIx / IIcx**: 68030 + 68882 on the shared `mac2fdhd` ROM
  `$97221136`, same GLUE, same Toby NuBus. Identity is VIA machine-ID pins
  only (IIx VIA2 PB `$87`, IIcx VIA1 PA `$C1`). **The wall** was the PMMU
  double-translation described in [§1.3](#13-cpu-integration-seam) — the
  boot wedged mid-System, SCSI freezing ~351 commands in.
- **Variant SE/30** (2026-07-31): the compact IIx — MAME `macse30` is
  "IIx with no slots and built-in video". Identity is the *combination*
  of both siblings' pins (VIA1 PA `$C1` + VIA2 PB `$87`). The internal
  512×342×1 video is `Se30Video.h`, an ordinary `NuBusDevice` on
  pseudo-slot $E: 64 KB VRAM served across the slot (MAME `$FE000000` +
  the `$FEE00000` 24-bit mirror), the linear 8 KB `se30vrom.uk6` dump as
  its declaration ROM at the top of the $FE window, page select on VIA1
  PA6 (from the written byte — PA6-as-input is the machine-ID pull-up).
  VBL is a slot-$E interrupt gated by VIA1 PB6=0 (`se30_via_out_b`),
  kept at MAME's every-other-frame phase toggle; flipping PB6 is the
  driver's ISR ack. No new brick — Finder on the gate's first run.
- **Gates:** `macii_post_etalon`, `macii_boot_etalon` (Sys 6),
  `macii_sys7_boot_etalon`, `macii_mouse_etalon`, `iix_boot_etalon`,
  `iicx_boot_etalon`, `se30_boot_etalon`, `declrom_test`, `nubus_test`,
  `toby_test`.

### 2.3 V8 gate array — Mac LC II (LC, Classic II, Color Classic, Mac TV)

`V8Memory` (`Model {LcII, Lc, ClassicII, ColorClassic, MacTv}`) + `Cpu030`.
Functional accuracy (O6).

- **CPU:** Moira's 68030 + PMMU + 68882 at 15.6672 MHz. `sst68030` pins
  **3 082** integer/MMU/bus-fault/FPU vectors generated by the WinUAE/Hatari
  oracle. The machine adds a functional instruction-cache *throughput*
  overlay — it is not a cycle-exact 68030 bus/cache model.
- **Map:** RAM/ROM overlay, V8/pseudo-VIA registers, VIA1, SCC, SWIM1,
  ASC-V8 and NCR 5380 pseudo-DMA. `Egret` + `AdbBus` implement the 68HC05
  transport, ADB keyboard/mouse, RTC and XPRAM stream commands
  ([§3.7](#37-mcus-egret--cuda-68hc05-firmware-lle)). `V8Video` decodes the
  built-in framebuffer through the Ariel CLUT.
- **RAM:** 4/6/8/10 MB (motherboard + SIMM pair); 10 MB is the V8 hard
  limit (12 MB installed, 2 MB wasted).
- **Boot contract:** the real 512 KB LC II ROM boots System 7.x from SCSI
  to the Finder. AppleTalk defaults inactive via SPConfig XPRAM
  ([§3.8](#38-scc-8530--appletalk)).
- **Variants** — one enum covers the whole V8 / Eagle / Spice / Tinker Bell
  spread, with a `spiceClass()` predicate (Color Classic ∪ Mac TV: SWIM2 +
  Sonora EASC `$BC` + Cuda) and a `cpuHz` ctor param: the gate array stays
  in the C15M domain while the CPU ticks rescale (the VASP pattern).
  - **LC** — 68020 + HMMU, 2 MB soldered, HMMU mask on pseudo-VIA PB3.
  - **Classic II** — Eagle, VIA1 PA `$92`, mono 512×342 out of RAM at
    `$1F9A80`, forgiving bus.
  - **Color Classic** — Spice, PA `$82`, fixed sense 2, SWIM2 in the gate
    array, **factory Cuda 341S0417 (2.35) firmware LLE, default since
    2026-07-29**; the long-standing "0417 wedge" was the missing DFAC2 I2C
    ACK, not an M68hc05 bug (`CudaLle::setI2cDfac`, `V8Memory.cpp:58-66`).
    341S0788 (2.37) stays as the no-0417 fallback.
  - **Mac TV** — its own `$EAF1678D` Tinker Bell ROM (PA id `$84`, fixed
    640×480 sense 6, 8 MB cap, 68030 @ 31.3344 MHz), *not* the EDE66CBD
    Sonora AIO ROM despite the archive filename.
- **Gates:** `lcii_boot_etalon`, `lcii_sys7_boot_etalon`,
  `lcii_soak/persist/launch/floppy_etalon`, `lc_`, `classic2_`,
  `cclassic_`, `mactv_boot_etalon`; devices via `egret_test`,
  `pseudovia_test`, `v8_ramsize`, `v8_video_test`, `asc_test`,
  `scsi_pdma_test`.

### 2.4 RBV (RAM-based video) — Mac IIsi (IIci)

`RbvMemory` (`iici` flag) / `RbvCpu` / `RbvVideo`. The **ancestor** of the
whole V8/VASP/Sonora line (MAME `rbv.cpp:6-9`): RBV originated the
pseudo-VIA and the video-out-of-system-RAM those gate arrays inherited.

68030 @ 20 MHz (IIsi) / 25 MHz (IIci), 512 KB ROM at `$40000000` (any read
clears the overlay), all windows mirroring `$00F00000`:

| Offset | Device |
|---|---|
| `+$00000` **and** `+$40000` | VIA1 |
| `+$04000` | SCC |
| `+$10000` | SCSI 5380 (pseudo-DMA `+$06000` / `+$12000`) |
| `+$14000` | discrete ASC (version `$00`, the Mac II cell) |
| `+$16000` | SWIM1 |
| `+$24000` | RBV = Bt478 DAC, MSB lane (the `Ariel` register model) |
| `+$26000` | pseudo-VIA (**base** flavour, [§1.2](#12-family-wide-invariants)) |

**There is no machine-ID longword.** The ROM identifies the box from VIA1
PA and the monitor sense (`montype << 3`) through the pseudo-VIA
video-config hook. The framebuffer is the **start of system RAM**
(`rbv.cpp set_ram_info`) — which is why guest-global asserts in tests read
desktop pixels unless they account for the PMMU.

- **IIsi**: Egret **344S0100** firmware LLE; VIA1 PA reads `$97`
  (`via_in_a_iisi` `$96` | diag-disabled PA0).
- **IIci**: same map, different front end — PIC1654S **ADB modem**
  (`AdbVia` + `342s0440-b.bin`) on VIA1 CB1/CB2 + PB4/PB5, and a discrete
  **343-0042 RTC** on PB0-2/CA2 (the Mac II / Centris wiring). No MCU
  reset-hold, so the 68030 runs from power-on. Three empty NuBus slots
  read 0. ROM `$368CADFE`, 25 MHz. **VIA1 PA must read `$C7`, not `$C6`**:
  MAME's `via_in_a` is `0xC6 | BIT(config,1)` and diagnostic mode is
  disabled by default, so PA0 = 1 (`RbvMemory.cpp:163-168`). With PA0 = 0
  the ROM takes the diagnostic path and spins forever in its VIA-T2
  calibration loop.
- **The IIsi is the machine that exposed the bus-time bug** —
  [§1.2](#12-family-wide-invariants).
- Gates: `iisi_boot_etalon`, `iici_boot_etalon`, `iisi_input_etalon`.

### 2.5 Sonora gate array — LC III, LC III+, AIO family + the VASP recombination

`SonoraMemory` / `SonoraCpu` / `SonoraVideo`: machine-ID longword at
`$5FFFFFFC`, 1 MB VRAM at `$60000000`, video = the `mv_sonora` cell
(5 modelines, CLUT, monitor sense in `vctrl`). Parameterised by ctor —
`cpuHz` × `machineId` × `cudaAdb`:

| Machine | ID | Clock | MCU |
|---|---|---|---|
| LC III | `$A55A0001` | 25 MHz | Egret 341S0851 |
| LC III+ | `$A55A0003` | 33.33 MHz | Egret 341S0851 |
| LC 520 | `$A55A0100` | 25 MHz | **Cuda 341S0060** |
| LC 550 / Color Classic II | `$A55A0101` | 33 MHz | **Cuda 341S0060** |

The all-in-ones carry a Cuda 341S0060 (2.40) instead of the Egret — 2.37
livelocks on pseudo-cmd `$0E`. Color Classic II is the LC 550 board with
sense 2 (512×384) instead of sense 6 (640×480×8). MAME's stubs for this
family do not boot, so the ROM itself was the oracle: the from-scratch
bring-up is written up in **`docs/LC520_BRINGUP.md`**.

**VASP** (`VaspMemory` / `VaspCpu` / `VaspVideo`) is the same Sonora shell
with **V8 peripherals** hung off it — `AscV8`, SWIM1, `Ariel`, pseudo-VIA
video hooks — and a **fixed 2048-byte row pitch**. `$A55A2015` @
31.3344 MHz = **IIvx**, `$A55A2016` @ 15.6672 MHz = **IIvi**; shared
`4957EB49` ROM, Egret 341S0851, empty NuBus. It booted first try: recombine
proven pieces per the MAME map before reverse-engineering.

Unmapped I/O on both maps reads back 0 — the LC III+ lesson,
[§1.2](#12-family-wide-invariants).

Gates: `lc3_`, `lc3plus_`, `lc520_`, `lc550_`, `cclassic2_`, `iivx_`,
`iivi_boot_etalon`, plus `lc3_input_etalon`, `lc520_input_etalon`,
`iivx_input_etalon`.

### 2.6 MEMCjr + PrimeTime — Quadra 605 (LC 475, LC 575)

`Q605Memory` + `Cpu040`. Functional accuracy (Q1-Q8). Machine ID at
`$5FFFFFFC`: LC 475 `$A55A2221`, Quadra 605 `$A55A2225`, LC 575
`$A55A222E` (`POM68K_Q605_ID` overrides).

- **Core:** the Q1-Q4 Moira 040 at 25 MHz — MOVE16, 040 control registers,
  040 exception frames, TTRs, three-level URP/SRP translation, MMUSR/PTEST
  and restartable MMU faults. `sst68040` pins **7 200/7 200** vectors
  across integer, random and MMU families. Q8 adds a separate I/D ATC (32
  entries, `POM68K_MMU040_WALK` disables it) and an i-cache/throughput
  overlay; **architectural copyback/snooping and cycle-accurate 040 timing
  are not modelled**.
- **`POM68K_Q605_CACHE_BOOST` default is 4** (since 2026-07-25). The old
  default of 1 carried the note "boost 2+ fails SCSI bring-up" — re-measured,
  that was a stale symptom of the machine-cycle bug in
  [§1.2](#12-family-wide-invariants); the whole 040 family is green at 4.
- **FPU compatibility:** a real Quadra 605 has a full 68040+FPU, an LC 475
  a 68LC040. The MAME `macqd605` oracle is a full 68040 and POM68K defaults
  to M68040 + Moira's soft 68882. `POM68K_Q605_NOFPU=1` selects
  **M68LC040 + soft 68882** (SoftwareFPU-equivalent); `=2` selects a TRUE
  bare `FPUModel::NONE`, and Mac OS 8.1 then installs the ROM's **integer
  PACK 4** — the selector is the ROM-resource combo in XPRAM `$AE`,
  validated against UniversalInfo `defaultRSRCs` and HWCfgFlags bit 12
  (CHANGELOG 2026-07-21). Gates `q605_nofpu_boot_etalon` /
  `q605_barefpu_boot_etalon`.
- **Memory/I/O:** MEMCjr ROM overlay and RAM sizing, plus the PrimeTime
  window: VIA1, Quadra pseudo-VIA2 (`PseudoVia`), SCC, IOSB/MEMCjr
  registers, Cuda-flavoured `Egret` (+ `CudaLle` firmware LLE), IOSB ASC
  `$BB` stereo (`AscIosb`), SWIM2 and `Ncr53c96`. VRAM 1 MB at `$F9000000`;
  DAFB registers at `$F9800000`, with MEMCjr transferring DAFB values
  through the real 6+6-bit holding protocol (the split stays with the bus
  decoder in `Q605Memory`; the cell itself is
  [§3.9](#39-dafb--antelope)).
- **Boot/UI:** the FF7439EE 1 MB ROM boots Mac OS 8.1 at 640×480×8 and
  System 7.5 / 7.5.5 / 7.6 (often 1 bpp until Monitors) to the Finder.
  `q605_trace` is the diagnostic whole-machine runner (`EXCLUDE_FROM_ALL`,
  not a gate); `q605_boot_etalon` is the whole-machine gate (soft-skips
  when the user-provided assets are absent).
- Gates also: `q605_dafb_test`, `q605_asc_test`, `q605_turboscsi_test`,
  `q605_floppy_boot_etalon`, `q605_cudalle_*`, `q605_cdrom/cdboot_etalon`,
  `q605_ot_bind_etalon`, `lc475_boot_etalon`, `lc575_boot_etalon`.

### 2.7 djMEMC + IOSB — Centris 650 (Centris 610, Quadra 610/650/800)

`CentrisMemory` / `CentrisCpu`. The two-ASIC I/O generation (MAME
`macquadra800.cpp` + `djmemc.cpp` + `iosb.cpp`), recombining Q605 parts:
DAFB video, 53C96 TurboSCSI, SWIM2, IOSB ASC, Quadra pseudo-VIA2 — **plus**
a discrete 343-0042 `Rtc` and the PIC1654S ADB transceiver (`AdbVia`). No
reset-holding MCU, so the CPU runs from power-on.

The model is strapped in **VIA1 port A**, and the `$5FFFxxxx` longword is
the fixed IOSB `$A55A2BAD` for all of them:

| Machine | PA | CPU |
|---|---|---|
| Centris 610 | `$40` | 68LC040 @ 20 MHz |
| Centris 650 | `$46` | 68LC040 @ 25 MHz |
| Quadra 610 | `$44` | 68040 @ 25 MHz |
| Quadra 650 | `$52` | 68040 @ 33.33 MHz |
| Quadra 800 | `$12` | 68040 @ 33 MHz; adds SONIC + NuBus |

`POM68K_CENTRIS_FPU` / `_BAREFPU` pick the FPU; `POM68K_CENTRIS_MODEL`
(or the legacy `POM68K_CENTRIS610`) picks the strap. The Quadra 800 leaves
SONIC/NuBus unmapped but models the Ethernet address ROM at `$50008000`.

**The one map delta that matters:** djMEMC maps a **2 MB** VRAM window
(`$F9000000-$F91FFFFF`) where MEMCjr mapped 1 MB — the 1 MB of VRAM must be
*mirrored* across it, or the ROM's VRAM sizer bus-errors at `$F91FFFFC` and
drops into the ROM serial monitor. (Related decode trap: decode the
framebuffer at the PixMap `rowBytes`, not the DAFB stride.)

Gates: `centris610/650_boot_etalon`, `quadra610/650_boot_etalon`,
`quadra800_boot_etalon`, `lc575_boot_etalon` (the `$A55A222E` identity on
the Q605 board).

### 2.8 Discrete 040 + DAFB — Quadra 700 / 900 / 950

`Q700Memory` / `Q700Cpu`. The **first** Quadra, and the machine that shows
the family seam: a full 68040 @ 25 MHz (50 MHz XTAL / 2) whose front end is
the **Mac II's** (VIA1 + a *real* VIA2 6522, discrete 343-0042 RTC,
PIC1654S ADB transceiver on firmware LLE) and whose back end is the
**Quadra's** (DAFB, 53C96, SWIM1, EASC, SCC, SONIC), at the `$5000xxxx`
layout the Centris/Q800 share. ROM `$420DBFF3`; VIA1 PA reads `$C1`
(diagnostic disabled — the IIci lesson); 2 MB VRAM; no machine-ID longword.

- **SCSI hangs off DAFB, not IOSB.** DAFB register `$24` latches four
  wait-state selections (`dafb.cpp:480-530`) and reads back the **live DRQ
  in bit 9**; registers at `+$0F000`, pseudo-DMA at `+$0F100` with the
  bit-18 waitstated alias. This is the cell `docs/LLE_VS_HLE.md` §3 had
  parked as "only matters for a future Q700/Q950-class profile".
- **VIA2 is a real 6522** with the Mac II interrupt fan-in: slot/DAFB IRQs
  on CA1 + port A (active low, DAFB = slot `$F` → bit 6), ASC on CB1, SCSI
  on CB2, SONIC on PA0.
- *Simplification*: the 60.15 Hz tick is generated directly into VIA1 CA1;
  real hardware routes VIA2's T1 out of PB7 into VIA1 CA1
  (`via2_out_b`, "chain 60.15 Hz to VIA1").
- Gate: `q700_boot_etalon` (Mac OS 8.1, 640×480 DAFB).
- **The Quadra 900/950 grow out of this same class.** `Q700Memory::Model
  {Spike, Q900, Q950}` carries the Eclipse front end — two `ApplePic` IOPs
  (§ 2.10's device), `AdbLine`, `Egret` on VIA1, a second 53C96, the VIA1 PA
  identities and the `$1000`-wide IOP windows — selected by
  `./q700_boot_etalon <q700|q900|q950>`. The Q900 POSTs, paints 640×480 and
  releases both IOPs; the SWIM IOP's byte-perfect firmware then falls into
  its own BRK handler. That is the one open thread — `TODO.md` § 0 and
  `docs/IOP_BRINGUP.md` § M7 — and profiles 35/36 stay unregistered until it
  closes.

**The "Eclipse" towers — Quadra 900 and 950** (`Model {Spike, Q900, Q950}`,
`eclipse()` is the one predicate the code branches on; full blueprint and
bring-up history in `docs/IOP_BRINGUP.md` § M7). Same board, with the **Mac
IIfx's front end** grafted on:

- The SCC and SWIM decodes are replaced by two **Apple PIC IOP** host
  windows (`$5000C000` and `$5001E000`, `$1000` wide, register offset
  `(addr >> 1) & $1F` on **both** byte lanes). Each IOP runs a 65C02 on
  firmware the host ROM uploads at boot — there is no dump.
- **ADB is bit-banged by the SWIM IOP's firmware** through its GPIO onto
  `AdbLine`; the **Egret** on VIA1 CB1/CB2 replaces the discrete RTC (clock,
  PRAM, and it holds the 68040 in reset until its own firmware releases it).
  A **second 53C96** bus sits at `+$0F400`.
- VIA1 PA identity `$D0` (Q900) / `$90` (Q950); the Q950 runs at
  **33.333 MHz** on its own `$3DC27823` ROM, the Q900 shares the Quadra
  700's `$420DBFF3` (so `POM68K_Q700_MODEL` is the only thing that tells
  those two apart).
- **The rule that cost a day**: a Quadra 700 quirk is not automatically an
  Eclipse quirk. The Spike's SWIM1 hangs off the odd byte lane, so `read16`
  and `write16` truncate a word there — on the towers that same window is an
  IOP host window answering on both lanes, and applying the Spike rule made
  the ROM write over the IOP's stack. Both directions now carry
  `!eclipse()`. Four M7 bugs were this same shape; `docs/IOP_BRINGUP.md`
  § 5b lists them.
- Gates: `q900_boot_etalon` (640×480×1), `q950_boot_etalon` (640×480×8),
  Eclipse save states in `savestate_040_test` (`Q900Rig` — the only rig that
  reaches the `eclipse()` tail of `Q700Memory::visit`).

### 2.9 F108 + PrimeTime II + Valkyrie — Quadra 630 (LC 580)

`Q630Memory` / `Q630Cpu` / `Valkyrie`. "Show and Tell": the **last 68k
desktop board**, the Quadra 605 cost-reduced twice over — DAFB replaced by
the fixed-mode **Valkyrie** framebuffer, the SCSI hard disk by an **ATA/IDE**
port wedged into the chipset, MEMCjr by the **F108** memory controller
(ROM/RAM switch + ATA + SCC + a "just like a 53C96" SCSI cell). The I/O
block is otherwise the Q605's PrimeTime, so the machine is `Q605Memory`
with the video cell swapped — the VASP recombination pattern again.
68040 @ 33 MHz, Cuda 341S0060 replacing Egret+RTC (VIA1 PB3=TREQ,
PB4=BYTEACK, PB5=TIP, CB1/CB2 = clock/data). Machine ID at `$5FFFFFFC`:
Quadra 630 `$A55A2252`, LC/Performa 580 `$A55A225A` (`POM68K_Q630_ID`).

**The ATA port is mapped but empty** (`+$1A000` cs0/cs1, no drive
modelled), so boot goes through SCSI. `+$1A100` is the PrimeTime II special
interrupt status (VBL/ATA). Anything else in I/O space bus-errors — the
ROM's address-map probe relies on it, the same discipline as the LC II V8.

**Valkyrie** (`Valkyrie.h`, MAME `valkyrie.cpp`) has **no CRTC to program**:
just a *video timing number* selecting one of a handful of hardwired modes
(`$00`, bit 7 = blank), a depth register (`$04`: 0=1, 1=2, 2=4, 3=8,
4=16 bpp), a 3-byte RAMDAC at `$50F24000` (payload in the **top** byte of
each longword) and 1 MB of VRAM whose frame buffer starts at **+$1000**.
Row stride is `strideForMode << depthIndex` (`valkyrie.cpp:154`). The pixel
clock is programmed over I2C by the Cuda (Valkyrie is slave `$28`); POM68K
does not model that bus, so the clock stays at the 31.3344 MHz default —
which only affects the refresh rate the frame clock derives.

Gates: `q630_boot_etalon`, `lc580_boot_etalon` (640×480×8 Finder).

### 2.10 OSS + two Apple PIC IOPs — Mac IIfx

`IIfxMemory` + `IIfxCpu` (Moira 68030 @ **40 MHz**, functional). Blueprint
and milestone history: `docs/IOP_BRINGUP.md`. What makes this board unlike
every other one in the tree: **three processors**, and the other two run
firmware the host uploads at boot.

- **No VIA2.** The **OSS** (`$5001A000`) replaces it: a flat `$400`-byte
  register file where `regs[0..15]` hold each input's requested IPL and
  `regs[$202]/[$203]` are the pending flags; the highest pending priority
  is driven into the CPU. Inputs: 0-5 = NuBus slots 9-E, 6 = SWIM IOP,
  7 = SCC IOP, 8 = ASC, 9 = SCSIDMA, 10 = the 60.15 Hz tick (also pulses
  VIA1 CA1; acked by writing OSS `$207`), 11 = VIA1.
- **No built-in video.** The IIfx boots on a NuBus card — `TobyVideo` on
  slot 9, the same port the Mac II uses ([§2.2](#22-glue--nubus--mac-ii-iix-iicx-se30)).
- **Two Apple PIC IOPs** (`ApplePic`, one per SCC and SWIM), each an
  `R65c02` at C15M/8 with 32 KB of RAM, a timer, two DMA channels and a
  host mailbox window. **There is no IOP ROM**: the internal map is all
  RAM + registers, so the system ROM downloads the firmware through the
  shared-RAM window and then releases `/RSTPIC` — the Duo BORG pattern,
  and `iifx_post_etalon` verifies both images byte-for-byte against the
  ROM (SCC vector `$040E` at ROM+`$5F471`, SWIM vector `$5000` at
  ROM+`$5A7EE`).
- **ADB is bit-banged by the SWIM IOP.** Its GPIO pair drives the wire
  (gpout0 **inverted** on the board) and `AdbLine`'s LLE keyboard+mouse
  answer — no HLE anywhere on that path, unlike every MCU-based family.
- **SCSIDMA** (`$50008000`): an NCR 53C80 cell plus Apple handshake logic
  over `Ncr5380`. **`$00-$03` is both the handshake data port and 5380
  reg 0** — with no DRQ active it must fall through to the register
  (`scsidma.cpp:262-310`); swallowing it means selection never puts the
  target ID on the bus. True DMA + the restartable handshake stall are
  A/UX-only and deliberately absent.
- **The FMC probe**: `$50024000-$50027FFF` must **bus error** — that is
  how the ROM recognises the board (`maciifx.cpp:204`).
- **Clocks**: the CPU is 40 MHz, every peripheral is on C15M (15.6672 MHz)
  or C15M/20 for the VIA; `tick()` converts with integer accumulators and
  carries the remainder ([§1.2](#12-family-wide-invariants)).
- **System ceiling**: the ROM is 32-bit dirty, so 7.6 is the practical
  top (8.x needs a 32-bit-clean ROM).

Gates: `r65c02_test`, `applepic_test`, `iifx_post_etalon`,
`iifx_boot_etalon` (Finder on 7.6), `iifx_input_etalon` (mouse repaint +
KeyMap through the IOP firmware), save states in `savestate_030_test`.

---

## 3. Devices shared across platforms

### 3.1 Storage: IWM + Sony 800K GCR

`Iwm.h/.cpp` + `SonyDrive.h/.cpp` (M5, research-pinned + trace-verified).
Full spec tables in the M5 research report (MAME `iwm.cpp` / `floppy.cpp` /
`flopimg.cpp` / `ap_dsk35.cpp`, pce, Snow — cross-verified). What the
implementation actually depends on, several found the hard way with
`sony_trace`:

- **8 state lines** at `$DFE1FF + reg*$200`, reg = A9-A12: CA0-CA2/LSTRB,
  ENABLE, SELECT, Q6, Q7. **Any** access — read *or* write — toggles the
  line: the ROM strobes EJECT with a `tst.b $DFEFFF` READ.
- **(Q7,Q6) select** data/status/handshake/mode; the ROM reads data through
  the q6L/q7L addresses (each read also clears that line). Mode = `$1F`.
- **Data register**: nibble MSB-set when ready; it clears **~14 IWM clocks
  AFTER a read**, not immediately — the ROM does `tst.b` (poll) then
  `move.b` (capture) and both must see the same nibble. Modelled with a
  14-cycle countdown re-armed only on the first read. The flip side: a
  poll landing back inside the hold after a CONSUME re-reads the SAME
  nibble (silicon does too — MAME `iwm.cpp:284`), and Apple's denibble
  inner path is hand-timed to JUST clear the window at real speed — which
  is why the 030 i-cache boost must freeze to 1 while the motor runs (the
  floppy boost gate, `Cpu030::pollBoostGate`; CHANGELOG 2026-08-05
  (eighth)/(ninth)).
- **TACH (sense 7) must be time-based** (spin counter × zone RPM
  394/429/472/525/590, 120 edges/rev): the ROM measures spindle speed by
  timing tach edges against VIA T2 *before* reading data. A
  position-derived tach freezes when the IWM is not streaming, and the ROM
  ejects the disk.
- **Sense/command tables** per the MFD-51W (sense F "new interface" = 1,
  SIDES = 1, READY = 0 immediately, STEP completes instantly — matches
  MAME). Commands: CA2 = value, (CA1,CA0,SEL) = address, LSTRB rising edge.
- **GCR**: byte-level nibble stream (no 10-bit sync framing needed), one
  nibble per 128 CPU cycles, 2:1 interleave. MAME's `build_mac_track_gcr`
  rolling checksum is ported verbatim and cross-validated against pce's
  independent formulation (200 random sectors, identical output).
- **Boot blocks**: the ROM validates bbID 'LK' AND the bbVersion word at
  +6 (`$4418`), then jumps to bbEntry at +2 (a `BRA.W` in real blocks).
  Code placed directly at +2 is rejected by the version check → eject.
- Debug tooling: `sony_trace` (instruction-level driver trace with
  idle-loop filtering), `lcii_sony_trace` (the V8/030 sibling: Prime/
  Control journal off the unit table, `_MountVol` at trap level, per-PC
  loss/duplicate attribution — the tool that located the boost-vs-hold
  mechanism, CHANGELOG 2026-08-05 (eighth)), IWM per-reg/sense counters,
  consumed-nibble ring.
- Gates: `gcr_test`, `iwm_write_test`, `floppy_persist_test`,
  `floppy_sound_test`.

### 3.2 Storage: SWIM1 and the SWIM2 cell engine

- **`Swim1`** — IWM + ISM modes, 1.44 MB MFM. Used by V8, RBV, VASP and the
  Quadra 700. Gate `swim1_test`.
- **`Swim2`** (2026-07-23, `docs/LLE_VS_HLE.md` step 13) runs MAME
  `swim2.cpp`'s **bit engines**: the MFM sync-hunting shifter with serial
  CRC-CCITT (`$CDB4` seed, `M_CRC0` on handshake bit 1), the GCR high-bit
  framer, and the TSS write serializer in half-cycles. `SonyDrive` stores
  each track as one revolution of raw cells (MFM 16 / GCR 31 C15M clocks
  per cell) padded to the spindle geometry; ACTION start lands the head at
  the spin-counter angle, i.e. **real rotational latency**
  (`setSpinClockHz` declares the spin tick unit — Q605 25 MHz). MFM writes
  are rebuilt per gap (PLL-style, drift-proof), decoded by an offline
  replica of the read machine, and **only CRC-valid sectors commit**. The
  IWM/SWIM1 nibble path is unchanged. Gates: `swim2_test`,
  `swim2_media_test`, `q605_floppy_boot_etalon`.

### 3.3 SCSI: NCR 5380

`Ncr5380.h/.cpp` + `ScsiDisk.h/.cpp` (M7, research-pinned + ROM-verified).
Boots System 6 from a raw Apple SCSI image (`hdv/*.vhd`, 512-byte blocks,
'ER' DDM at block 0, 'PM' partition map, Apple_Driver43 + Apple_HFS).

- **Controller**: register-write-driven phase engine at `$580000`,
  reg = A4-A6, A0 = byte lane, A9 = pseudo-DMA/DACK. Polled — no CPU
  interrupt on the Plus. Selection **without arbitration** (the Plus way):
  it triggers on ICR SEL asserted + BSY released with the target-ID bit on
  ODR, independent of the Mode ARBITRATE bit. Phases
  COMMAND→DATA(IN/OUT)→STATUS→MSG IN with REQ/ACK per byte; pseudo-DMA
  auto-handshakes one byte per A9 access. One target at SCSI ID 0. Bit
  layouts from MAME `ncr5380.cpp`; sequence from pce `macplus/scsi.c` and a
  bit-exact ROM disassembly (`SCSI_DO_SELECT`).
- **Target** (`ScsiDisk`, one implementation of `ScsiTarget` —
  [§3.3bis](#33bis-what-else-can-live-on-the-bus-scsitarget--daynaport)):
  SCSI-1 direct-access — TEST UNIT READY, REQUEST SENSE, INQUIRY (byte 0 =
  `$00`, direct-access, is all the ROM keys on), READ CAPACITY, READ(6/10),
  **WRITE(6/10)**, MODE SENSE. Also CD-ROM (`Kind::Cdrom`,
  `POM68K_CD_TRACE`).
- **The guest-side half of the target, added 2026-08-07.** Everything above
  is what the *ROM* needs to read a volume, and it is all the boot etalons
  can prove. Nothing running INSIDE the guest could work on a disk: MODE
  SENSE answered a bare header with no pages at all, so HD SC Setup, Drive
  Setup, Silverlining and FWB each refused the drive — which is why the tree
  grew a host-side `tools/wrap_hfs.py` + DDM-template façade instead. Now:
  - **mode pages** 1 (error recovery), 2 (disconnect/reconnect), 3 (format
    device), 4 (rigid geometry — 8 heads × 25 sectors, cylinders derived),
    8 (caching) and **`$30`, the Apple signature** (`"APPLE COMPUTER,
    INC."` at page offset `$0A`) — the hard-disk twin of the page `$30` the
    CD-ROM personality already carried. `$3F` returns the set; an
    unsupported page is CHECK CONDITION / `$24`, not an empty reply. PC=1
    (changeable) reports only what MODE SELECT could actually alter.
  - **INQUIRY** answers as the Apple-branded Seagate an internal Mac drive
    reports (`" SEAGATE"` / `"          ST225N"`); `POM68K_SCSI_INQUIRY=pom68k`
    restores the emulator's own identity.
  - **commands** REZERO, REASSIGN BLOCKS, SEEK(6/10), SEND DIAGNOSTIC,
    START/STOP, PREVENT/ALLOW, WRITE AND VERIFY(10), VERIFY(10) (BytChk
    compares for real and answers MISCOMPARE / `$1D`), SYNCHRONIZE CACHE,
    READ DEFECT DATA(10), MODE SELECT(10), MODE SENSE(10).
  - Oracle: **RaSCSI** (`RASCSI-X68k src/raspberrypi/disk.cpp:1473-1616` +
    `:2857-2918`, BSD-3-Clause). MAME's `nscsi_hd` models none of it, so
    for this one area RaSCSI outranks the usual source order.
  - Gate: `scsi_target_test` (builds its own image — no asset, never skips).
- **DATA OUT sizing belongs to the target**, not the controller
  (`ScsiTarget::writeByteCount` / `::extendDataOut`). Both engines used to
  carry their own partial copy of that table and they disagreed: the 5380
  knew MODE SELECT(6) and the FORMAT UNIT defect-list header, the 53C96
  knew only WRITE(6)/(10). A parameter-list command on any 53C96 machine
  therefore reached STATUS while the driver still had its list to send.

### 3.3bis What else can live on the bus: `ScsiTarget` + `DaynaPort`

`ScsiTarget.h` is the four-method interface both controllers hold
(`present`, `command`, `writeByteCount`, `extendDataOut`) — deliberately
without block size, media or geometry, because a controller that knows
those has grown a device model. `ScsiDisk` is one implementation;
`DaynaPort` is the other.

**`DaynaPort`** (`DaynaPort.h/.cpp`) is the DaynaPort SCSI/Link: an
Ethernet card that answers SCSI commands. READ(6) with control byte
`$C0`/`$80` pulls one received frame behind a 6-byte header (2-byte length
including the FCS, 4-byte flags — `$10` = more queued); WRITE(6) pushes one
out in either the raw or the `$80` header format; `$09` retrieve stats,
`$0C` set mode / set MAC, `$0D` set multicast (accepted, discarded), `$0E`
enable/disable. INQUIRY is a processor device (`$03`) in **37 bytes** — the
Mac driver rejects the usual 36. Received frames are padded to the 60-byte
Ethernet minimum and given a real CRC-32 FCS; the 6 KB Rx ring drops the
arriving frame when full and counts it.

**`EtherLink`** (`EtherLink.h/.cpp`) is the wire between that card and the
NAT already inside `MacIpGateway`: Ethernet framing plus an ARP responder
that **proxies for the whole subnet** (the gateway is the only thing on the
segment) while never answering for the guest's own address — a reply there
reads as a duplicate address and MacTCP refuses to initialise.
`MacIpGateway` leases now record which link they were learned on, and
`sendIpToGuest` routes DDP or Ethernet accordingly.

Wiring: `POM68K_DAYNAPORT=<id>` puts a card on the **Quadra 605**'s bus
(`=1` → the default ID 3, where MAME parks the CD-ROM). `AtalkHub::attach`
detects `mem.daynaPort()` with a `requires` clause, so the eleven machines
without one compile unchanged and adding a card elsewhere is a member plus
an accessor. The card is on the bus regardless of AppleTalk; its *uplink*
is the hub's NAT, so `POM68K_APPLETALK=0` leaves the guest a card with
nothing behind it.

Measured: `q605_boot_etalon` reaches the same Mac OS 8.1 desktop with the
card at ID 4 — identical screen signature, 4558 SCSI commands against 4551
without it, which is one bus probe of one extra target.

Not done, and each is a real gap: no guest-side SCSI/Link **driver** has
been run against this (the command set is gated, the driver's opinion of it
is not); no GUI menu entry; not in save states (a restore comes back with
an empty Rx ring); EtherTalk is not bridged — the card carries IPv4 and ARP,
so AppleTalk still goes over the SCC. Gate: `daynaport_test`.
- **THE GATE (why it took a day): the ROM's SCSI-presence probe.**
  `E_SoftReset` does `MOVE.L ($420000),D0; CMP.L ($440000),D0; BEQ no-scsi`.
  On real hardware the 128 KB ROM does **not** mirror across the whole
  `$400000-$4FFFFF` window, so those two longwords differ → SCSI present →
  `HWCfgFlags` (`$0B22`) bit 7 set → `CheckSCSI` (`$407D40`) runs the 6→0
  scan, reads block 0, loads Apple_Driver43, JSRs its init (which registers
  the drive in `DrvQHdr` `$0308`), and the boot dispatcher boots it. Our
  `MacMemory` originally mirrored the ROM everywhere → the probe saw
  equality → "no SCSI", no scan. Fix: the ROM answers only at
  `$400000-$41FFFF`; above that it returns address-dependent open bus
  (`addr >> 16`). **The Plus does not consult PRAM for the boot device**
  (that is a 256K-ROM/SE+ feature) — the scan is automatic and
  unconditional once HWCfgFlags is set.
- **WRITE support is mandatory**: the disk driver writes to the volume
  during mount, and a read-only target hangs the boot in a VIA interrupt
  storm right after the driver loads. Writes land in the in-memory image;
  persistence is `ScsiDisk`'s copy-on-first-write log
  ([§1.4](#14-save-states-the-visit-contract)).
- Gates: `ncr5380_test`, `scsi_disk_test`, `scsi_boot_etalon`,
  `scsi_pdma_test`, `scsi_cdrom_test`, `scsi_hfs_facade_test`.

### 3.4 SCSI: NCR 53C96 TurboSCSI

`Ncr53c96.h/.cpp` (Q6). Streamed CDBs, PIO Transfer Info, DRQ-gated
pseudo-DMA, STATUS/MSG completion, and the OS 8.1 SCSI Manager's mixed
PIO/DMA chunking. On the Quadra 700 the surrounding cell is DAFB's, not
IOSB's ([§2.8](#28-discrete-040--dafb--quadra-700)). Gates:
`ncr53c96_test`, `q605_turboscsi_test`.

### 3.5 Input: M0110 keyboard + quadrature mouse

`MacInput.h/.cpp` + `Scc8530` + VIA (M5.5, research-pinned and
System-verified). Plus-family only; everything later is ADB.

- **IPL wiring — the bug that cost a session.** The glue **disconnects**
  the VIA's /IPL0 while the SCC interrupts, so level 3 never occurs on a
  Plus. Its ROM vector is a bare RTE, so a naive `via | scc<<1` OR
  livelocks the machine the moment both are pending (keyboard SR + mouse
  DCD). The formula is `IPL = (VIA & ~SCC) | (SCC << 1)` (Mini vMac /
  GttMFH Table 3-1).
- **Mouse**: X1 → SCC DCD-A, Y1 → DCD-B; X2/Y2 → VIA PB4/PB5; button → PB3
  (0 = down). Per step: set the phase bit **first**, then toggle DCD.
  Direction: PB4 ≠ DCD-A → right; PB5 = DCD-B → down (X and Y senses are
  opposite). Max ~1 step/axis per 350-450 µs — faster starves Y, because
  B-ext has lower priority than A-ext. Quadrature loses ±1 count at
  direction changes; so does real hardware.
- **Keyboard (M0110A)**: commands Inquiry `$10` / Instant `$14` / Model
  `$16` (→ `$0B`) / Test `$36` (→ `$7D`); Null = `$7B`; transition =
  keycode*2+1, bit 7 = release. A transaction is **two** VIA SR interrupts:
  one when the command finishes shifting out (ACR mode 111), one ~3 ms
  later when the response lands after the driver flips ACR to shift-in
  (mode 011). Verified against System 6's own state: RawMouse (`$82C`),
  MBState (`$172`), KeyMap (`$174`, **8 bytes** — asserting over 16 made a
  dead ADB stack look half-alive once). Gate `input_etalon`.

### 3.6 Input: ADB — PIC1654S transceiver LLE

Default on Mac II / IIx / IIcx, IIci, Centris/Quadra, Quadra 700 and the
SE/Classic compacts; `POM68K_ADB_LLE=0` or a missing dump falls back to
HLE.

**Why LLE.** The HLE `AdbVia` byte-model (NEW/EVEN/ODD/IDLE over the VIA SR
feeding `AdbBus`) drops ~99.99 % of the ROM's fast `ADBReInit` traffic — its
fixed timer misses the rapid ST transitions — so the ROM's device map
diverges from `AdbBus` and the mouse ends up polled at a phantom address,
frozen. The LLE path runs the *real* transceiver, the way MAME does: the
whole NEW/EVEN/ODD/IDLE handshake and the ADB timeouts live in the
firmware, so they are exact by construction.

- **`Pic1654s`** — PIC16C5x-family 12-bit core, the GI/NMOS PIC1654S
  variant Apple used as the ADB Modem **342S0440-B**. Ported from MAME
  `pic16c5x.cpp` with the `0x1654` quirks: OPTION/SLEEP/CLRWDT/TRIS decode
  as NOP, port read = external pins AND the output latch, STATUS NMOS
  masking, `/8` clock (3.6864 MHz → 460.8 kHz), TMR0/RTCC counts external
  edges. Runs `roms/adbmodem/342s0440-b.bin` (1024 B, CRC `cffb33eb`,
  user-provided — never committed). Gate: `pic1654s_test`.
- **`AdbLine`** — bit-serial ADB keyboard+mouse device model (wired-AND
  open-collector line, attention/sync/8-bit/stop framing, SRQ, Listen-R3
  reassignment, change-detected Talk R0), ported from MAME `macadb.cpp`.
  Timing is calibrated to the PIC's *own* wire rate under cycle-exact
  co-stepping — bit cell 1564 cyc ≈ 99.8 µs, attention 12410 ≈ 792 µs, i.e.
  the ADB spec values, because the firmware's delay loops **are** the
  reference. Gate: `adbline_test`.
  The **device registers** are completed against DingusPPC's
  `adbkeyboard.cpp`/`adbmouse.cpp` (2026-08-02), MAME modelling none of
  them: the handler ID is a real register selected by a Listen R3 activator
  (keyboard 1/2/3 — and a standard keyboard **refuses** 3, which is the
  probe that identifies an Extended Keyboard II; mouse 1/2/4, where 4 is the
  Extended Mouse Protocol that puts the **second button** in bit 7 of the
  second report byte and answers R1 with the 8-byte identifier block),
  Listen R2 latches the keyboard LEDs (active low, read back via
  `keyboardLeds()`), and SendReset restores the default handler as well as
  the default address. Reset ID stays 1; `POM68K_ADB_KBD_ID` moves it.
- **`Via6522::extShiftCB1`** — external-clock (CB1) shift-register support
  for the ADB modes (011 shift-in, 111 shift-out), rising-edge clocked to
  match the firmware's shift routines (`0x065` send, `0x077` receive).
- **`AdbVia`** wires the PIC ports to the VIA (RA0/RA1←PB4/PB5 ST,
  RB2/RB3→CB1/CB2 shifter, RB4→PB3 IRQ, RA2/RA3→ADB line) and steps the PIC
  at every VIA1 access (`MacIIMemory::viaAccess` → `AdbVia::syncTo`).

**Two fixes made it run end to end**: an **ST-idle pull-up** — PB4/PB5 read
high when the 68k leaves them as inputs, so idle = ST=IDLE(3) and not a
spurious NEW that RESET-looped the PIC (`readA` does `portB | ~ddrb`) — and
the line-timing recalibration above.

**Default since 2026-07-22.** The "self-test misroute" blocker was three
stacked bugs (full detail: CHANGELOG "Mac II LLE ADB default"): PIC
instruction cost ignored (`tickLle` now charges `run(1)`'s real 1-3-cycle
cost, after which the firmware's wire timing lands on the ADB spec); a
phantom IFR.SHIFT from the Slot-Manager ORB `armShiftComplete()` hack whose
`$15D(A3)` guard is ADBBase→flags (gated off in LLE — the firmware's
idle-timeout byte serves the `$7100` POST wait for real); and the VIA
mode-111 ext shift-out advancing the SR on the *first* CB1 falling edge,
delivering every ROM byte `<<1` to the PIC. **Note for future co-stepping
work:** `syncTo`'s burst-at-VIA-access interleaving is temporally *exact* —
VIA state only changes at VIA accesses and the CPU only sees PIC effects
through VIA reads, both of which sync first; only the cost accounting was
wrong.

Firmware map (from disassembly): ST-change dispatch @`0x022` with index =
(last-cmd-op<<2)|newST; `0x065` = shift byte PIC→VIA; `0x077` = VIA→PIC
(8 CB1 cells); `0x0C5` ladder = wire bit-width measurement; `0x1A0` =
execute command on the wire; `0x1F3` = IRQ (RB4/PB3) decision.
Diagnostics: `POM68K_ADB_LLE_TRACE=1` (wire edges + `adbtalk` decode),
`POM68K_ADB_PIC_TRACE=1` (ST samples, PIC port-B writes, VIA1 traffic).
Gate: **`macii_mouse_etalon`** (binary `tests/macii_mouse_trace.cpp`) — the
mouse must move; it also exercises the `via2Ca1SlotTaskArmed` slot-VBL →
jCrsrTask fix (same CHANGELOG entry).

### 3.7 MCUs: Egret / Cuda (68HC05 firmware LLE)

`M68hc05` + `CudaLle` + `Egret` (transport) + `AdbBus`. Real firmware dumps
under `roms/cuda/`, `0x1100` bytes each mapped at `$0F00`: **341S0417** =
Cuda 2.35, **341S0788** = 2.37, **341S0060** = 2.40; Egret flavours
341S0851 (LC III / VASP), 344S0100 (IIsi). `POM68K_EGRET_LLE=0` /
`POM68K_CUDA_LLE=0` fall back to the HLE byte model;
`POM68K_CUDA_FW=<path>` forces a specific dump.

- **Firmware LLE is the default** — Cuda since 2026-07-23, the LC II Egret
  flavour since 2026-07-24 (instruction-slaved ADB wire,
  `M68hc05::onCycles`).
- **The transport is phase-fragile.** A 2 % shift in the MCU instruction
  rate (the 6805's 11-cycle interrupt charge) deadlocks the Mac TV. Keep
  `serviceInterrupts` at 0, and always run `mactv_boot_etalon` after any
  MCU timing change.
- **Clock drift**: `CudaLle::tick` must carry `M68hc05::run` overshoot as
  `mcuDebt_`, or the MCU overclocks ~37 % and the guest's RTC drifts. Boot
  gates do not see this; `lcii_soak_etalon` does.
- **The DFAC2 I2C slave** (`CudaLle::setI2cDfac`) is not optional on the
  boards that have it: without the ACK, Cuda 2.35 takes its DFAC-error path
  after one aborted probe and never completes the next host VIA session —
  that was the whole "0417 wedge". Enabled for the Color Classic, the
  Sonora AIOs and the Quadra 630.
- **Egret XPRAM wire protocol** (O6.11, pinned from the ROM's own drivers):
  ReadXPram `[1,2,1,addr]` and GetPram `[1,7,hi,lo]` are **byte streams
  with no length on the wire** — Egret keeps supplying successive bytes;
  the HOST takes its count, drops SYS_SESSION and waits for the
  XCVR_SESSION release (32-bit engine `$40A149C4`, 24-bit reader
  `$A4A3B4-BC`). WriteXPram is `[1,8,1,addr,data…]` (length = data). The
  ROM's SysParam restore is **two** GetPram streams: 16 bytes at XPRAM
  `$10` → `$1F8-$207`, then 4 at `$08` → `$208-$20B` — that is where
  low-mem SPConfig (`$1FB`) comes from. Getting this wrong fails the 'NuMc'
  validity read at `$0C` and re-runs the cold-PRAM XPRAM re-init on every
  boot.
- Gates: `m68hc05_test`, `cuda_lle_test`, `egret_test`, `egret_lle_test`,
  `q605_cudalle_boot_etalon`, `q605_cudalle_mouse_etalon`,
  `q605_cudalle_key_etalon`.

### 3.8 SCC 8530 + AppleTalk

`Scc8530.h/.cpp`; the network stack is `AtalkStack` / `AfpServer` /
`PapServer` / `MacIpGateway` / `AtalkHub`, and **its reference is
`docs/APPLETALK.md`** — read that before touching LocalTalk/AppleShare.
Only the SCC-side facts live here.

- **Minimal model** (what the Plus ROM actually uses): pointer reg
  (auto-reset), WR1.0 / WR9.3 / WR15.3 enables, WR0 cmd `$10` (reset ext
  status), and crucially **RR2 on channel B = the WR2 vector with bits 3:1
  replaced by the highest pending source** (A-ext = 101, B-ext = 001,
  idle = 011) — the ROM's level-2 handler dispatches Lvl2DT on those bits
  and never reads RR3.
- **LC II AppleTalk path** (O6.10; the `Scc8530` at V8 `$50F04000`, IPL 4).
  The LC II has no LocalTalk peer, so the System's `.MPP`/LAP layer must
  hit its own "no network" timeout rather than hang. Two additions, both
  standard 8530 behaviour, both gated by `scc_ext_test` and invisible to
  the Plus mouse path:
  1. **Break/Abort on the open line** — `setAbortIdle(true)` (set by
     `V8Memory`) makes RR0 bit 7 stand, and arming WR15 bit 7 latches the
     external/status interrupt: AppleTalk's carrier-sense wait (`$A5B28`
     spin on the driver mutex `$63E`) unblocks.
  2. **Tx Buffer Empty interrupt** — the transmit buffer accepts a byte
     instantly (no shift register), so enabling Tx ints (WR1 bit 1) or
     writing data latches Tx-empty (RR3 D4/D1, RR2 status codes 100/000,
     cleared by WR0 Reset Tx Int Pending `$28`). The LAP driver arms a
     serial transaction and sleeps on this completion interrupt (`$A6540`
     mutex spin); without it the ISR never runs and boot hangs at the
     « Bienvenue » bar. WR2/WR9 are mirrored to both channels (chip-global
     on a real 8530); RR0 bit 2 (Tx empty) stays asserted.
- **O6.13**: V8 *word* accesses on `$F04000` take one ctl/data side effect
  (mirrored lanes) so they cannot double-advance `ptr_`.
- **The `/RTS` and `/DTR` pins** (2026-08-02, `updateRts`). `/RTS` is *not*
  a view of WR5 bit 1: with Auto Enables (WR3 bit 5) the chip holds it
  asserted after the bit is cleared until the transmitter is completely
  empty, so its release lives in `tick()` as the shifter drains, not at the
  register write (MAME `tra_complete:1090`). `/DTR` follows WR5 bit 7 unless
  WR14 bit 2 repurposes the pin for DMA request. Read them as *asserted*
  (`rtsAsserted`/`dtrAsserted`) — the package pins are active low, and no
  caller should have to remember that. **No consumer yet.**
- **SDLC residue codes** (RR1 bits 3-1): a byte-granular wire only produces
  the byte-aligned code **011**, which is also the chip's reset value — hence
  `rr1Rd = $07`. Frame bytes used to report `000` (a *partial* character),
  so the idle register and a live frame byte disagreed. Both read 011 now.
- **AppleTalk is off in the factory PRAM.** Since O6.11 the path above only
  runs when the user turns AppleTalk on in the Chooser: the default is
  **inactive** via classic-PRAM SPConfig (XPRAM `$13` = `$22`, port B
  nibble 2 = useAsync; 1 = useATalk — Apple supermario `BeforePatches.a` /
  Patches note #1032330). `.MPP` then never opens LocalTalk. XPRAM
  `$E0-$E3` is only the LAP connection selector ('atlk' id, 0 = built-in)
  and **cannot** disable AppleTalk (a bad id falls back to built-in).
- Gates: `scc_ext_test`, `scc_baud_test`, `scc_engine_test`,
  `llap_loop_test`, `ltoudp_test`, `llap_two_system_etalon`,
  `atalk_stack_test`, `afp_server_test`, `pap_server_test`,
  `macip_gw_test`.

### 3.8bis The raster beam (`VideoBeam.h`)

The scan position and the row schedule every converted decoder renders
against (`docs/LLE_VS_HLE.md` § 1.1, landed 2026-08-02).

**It owns no clock.** Each platform already accumulates CPU cycles into the
current frame to generate its VBL — `framePos_` in `V8Memory`,
`SonoraMemory`, `VaspMemory`, `RbvMemory`, `MacIIMemory`, `TobyVideo`,
`Dafb`, `Valkyrie`. `VideoBeam::setPos()` adopts that accumulator, so there
is exactly one source of frame time and the VBL edges stay untouched. Adding
a second clock here would be the easy version and the wrong one.

Geometry is `(frameCycles, activeCycles, totalLines, visibleLines)`.
`line()` is derived from `totalLines` (blanking included, for registers that
expose a position); the **row schedule** is derived from `activeCycles`
instead — deliberately independent, so a decode height that does not match
the modeline's line count (a 640×480 surface on the V8's 407-line modeline)
still emits every row exactly once per frame.

Decoder contract: `decodeRows(out, y0, y1)` renders half-open row runs into
a persistent surface; `raster(out)` sets the geometry, calls `setPos()`, and
pumps. `decode()` (whole frame, state as of now) stays for stills and tests.

**`frameCount_` is required, not decorative.** A caller that samples once per
frame at a fixed phase — which the machine loops are — cannot be served by
the position alone: it is modulo, so a whole elapsed frame reads identically
to no time at all and the row cursor never advances. Every memory class that
owns a frame accumulator therefore carries a completed-frame counter, and it
is real machine state (serialized). The beam treats a position decrease *or*
a sequence change as a wrap.

The row cursor inside `VideoBeam` is **not** serialized — a pure cache,
re-derivable from the accumulator and the surface (`SaveState.h`'s rule); a
restore self-heals on the first sequence mismatch.

Gates: `video_beam_test` (schedule), `v8_raster_test` (behaviour).

### 3.9 DAFB / Antelope

`Dafb.h/.cpp` (split out 2026-07-21 — one concern per file; each machine's
VRAM window and holding-port protocol stay with its bus decoder). Used by
Q605/MEMCjr, Centris/djMEMC and the Quadra 700's discrete cell.

MAME-parity pass (2026-07-21): the Swatch CRTC timing registers drive
`recalc_mode()`-derived geometry (`dafbHres/Vres`), the clock generator's
bit-banged word sets the pixel clock, and frame/VBL timing follows the
guest's programmed `htotal×vtotal/pclk` (OS 8.1 programs 896×525 at
30.25 MHz). Extended monitor sense (drive pins + ext-code readback), the
Swatch display-disable bit, VBL/cursor interrupts, CLUT and RAMDAC-selected
1/2/4/8/16/24-bit modes are all `dafb.cpp` semantics.

**The clock generator is per flavour** (`Dafb::Clockgen` ctor variant,
2026-07-27) — and one decoder for all three was not merely incomplete:

| Flavour | Machine | `dafb.cpp` | Wire |
|---|---|---|---|
| **Gazelle** | MEMCjr (Q605) | `:1322` | serial word at `$3C3` |
| **DP8534** | djMEMC (Centris/Quadra) | `:1197` | MSB-first bitstream into `$303`, committed by any write to `$313`, decoded as five bit-reversed bytes → P/RCNT/NCNT |
| **DP8531** | discrete DAFB (Q700) | `:884` | nibble registers at `$3n3`, latched by register 15 → R/P and the A/B swallow-counter split of the N modulus |

The DP8531's register-12 nibbles land on `$3C3`, which **is** the Gazelle's
own serial port — so the shared decoder used to latch a Quadra 700 pixel
clock out of unrelated data, and silently froze the Centris/Quadra pclk.
Real ROM values, visible with `POM68K_DAFB_CLOCK_TRACE=1`: Centris 650 →
30.26 MHz, Quadra 700 → 25.175 then 30.24 MHz.

`q605_dafb_test` pins register/depth/reset/CRTC/sense **and all three clock
generators**; the 256-colour Finder is proven live by `q605_boot_etalon`
(mode 3, base `$1000`, stride 1024). `q605_trace --dafb-io N` gives DAFB and
MEMCjr holding-port traffic its own trace budget. Remaining gaps: no VRAM
arbitration (audited 2026-08-03: no oracle — MAME's four video devices
model none either; `docs/LLE_VS_HLE.md` § 1.1), VBL line hard-coded at 480
(as in MAME).

---

## 4. JIT — the second execution engine

`src/jit/` (`JitEngine`, `JitBackend`, `JitIr`, `JitCodeBuffer`, `JitGuard`,
`backends/`), J0-J2.

**Design, invariants, measurements, the block-cache attribution, the
x86-64 backend, the inline data TLB, block linking and the full environment
table are in `src/jit/POM68K_JIT.md`** — that file is authoritative and this
section deliberately does not restate it. The four-point extension the
engine needs inside the vendored core is in `extern/moira/POM68K_VENDOR.md`
§ *JIT seam*. What belongs *here*, because it is about the rest of the tree:

- **It is a second engine, not a replacement.** It is the conformant default
  on validated 68040 guests; every other family starts on the interpreter.
  The GUI **CPU** menu switches it live (through the machine thread's command
  queue, so the swap lands between two instructions), and
  `POM68K_CPU_ENGINE=interp|jit` overrides the family default. The
  interpreter remains an explicit etalon tier and the accuracy oracle.
- **Where it is wired.** **Twelve** CPU wrappers carry a `jit::Engine`: the
  030s `Cpu030`, `SonoraCpu`, `VaspCpu`, `RbvCpu`, `MscCpu`, the 040s
  `Cpu040`, `CentrisCpu`, `Q700Cpu`, `Q630Cpu`, and — since 2026-08-06 —
  `Cpu020` (Mac II / IIx / IIcx / SE-30) and `Cpu68k` (Plus / SE / SE FDHD
  / Classic) and `IIfxCpu` (the IIfx). **Every CPU wrapper in the tree now
  carries one.** The GUI menu binds on every one of them. The **x86-64 code generator** is **68040-only by declared
  capability** (`BackendCaps::guestFamilies`), so `auto` gives everything
  else the `threaded` backend. That is a guest-family constraint, not a
  host one: the 030's `(An)+` timing, restartable-write/format-$A framing
  and prefetch refill differ semantically from the 040's.
- **…and what it is worth there**, because "wired" and "worth switching on"
  are not the same claim. The window's job is to skip an ATC walk, so the
  gain tracks the MMU: 68040 ×2.1-2.7, 68030 ×1.4-1.7, **68020 ×1.0-1.2,
  68000 ×1.03-1.08**. On the last two there is no walk to skip and only the
  machine's address decode is saved.
- **The compacts are the one family where the window is not free.** Moira's
  `SYNC(x)` macro is a no-op on `Core::C68020` only — which covers the
  68020, 030 and 040. On `Core::C68000` it is real, `MOIRA_PRECISE_TIMING`
  is on, and the Mac Plus carries the tree's only cycle-exact timing claim
  (`sst68000`, 1 000 058 vectors *with cycles*). So `pomJitFetch000`
  replaces the bus read and nothing else, and the video/RAM contention
  `Cpu68k::applyContention` charges from inside `read16()` is handed back
  to the fetch path through `Moira::pomJitSetBusStall`. Gates:
  `jit_system_boot_etalon`, `jit_classic_boot_etalon`, and the decisive
  `jit_lockstep_68000_test` pair — two Mac Plus machines stepped together
  with **`clock`** compared at every checkpoint (2.5 M of them over ~646 M
  guest cycles, one per instruction for the last 100 000). Deleting the
  contention charge makes it red at step 982 402; deleting a `SYNC(2)` at
  step 0.
- **A harness on this family must raise the VBL itself.** `MacMemory::tick()`
  does not generate it — `MacFrameClock::runFrame()` raises VIA CA1 between
  two `runUntil()` calls (`MacFrame.h`). A stepping loop built on
  `runCycles()` alone delivers no vertical blank, and the machine spins in
  early boot with the overlay still up: hundreds of millions of cycles that
  look like a boot and execute nothing but ROM. `jit_lockstep_68000_test`
  shipped that way for one round and passed its own negative controls; it
  now reproduces `runFrame()`'s shape and asserts its reach (overlay down,
  IWM polled) before reporting success.
- **Check that it is doing something, not just switched on.**
  `POM68K_JIT_VERBOSE=1` prints a retired / window-covered / arms / failed
  line at teardown. It exists because the compacts' first wiring reported
  itself ON while retiring **zero** instructions: their main loop is
  `runUntil()` (`MacFrameClock` subdivides a frame into 16 absolute
  targets), not the `runCycles()` every other family uses.
- **The `threaded` backend generates no code and is always compiled and
  always usable**, so `POM68K_JIT_BACKEND=auto` never fails to produce a
  working engine — Emscripten and hardened kernels included.
- **What the rest of the tree owes the JIT.** Three things invalidate a
  cached window or block, and all three are hooks *outside* `src/jit/`:
  a write into a physical page holding translated code (`jit::CodeGuard`'s
  page map, hooked into the memory maps' `write8`/`write16` — hence the
  `#include "jit/JitGuard.h"` at the top of every `*Memory.h`); a change of
  the address map itself (boot-overlay flip, a *read* side effect, so it
  calls `jitMapChanged()` directly, [§1.2](#12-family-wide-invariants));
  and a change of translation (`Moira::pomJitMmuGen`, bumped by every ATC
  flush and TTR write).
- **Gates (16, label `jit`)**: `jit_backend_test` (backend registry, W^X
  buffer, classifier safety rules — no assets), **five** flavours of
  `jit_lockstep_test` (`_blocks`, `_x64`, `_x64_fine`, `_noaccess` — see
  `POM68K_JIT.md` §5 for what each one covers), and **ten**
  `jit_*_boot_etalon` twins (q605, centris650, q630, q700, lcii, mactv,
  lc3, iivx, iisi, lc) — the same etalon executables re-run with
  `POM68K_CPU_ENGINE=jit`.

---

## 5. Environment knobs — the complete list

Derived from the code — re-derived 2026-08-01 with

```bash
grep -rhoP '(?:getenv|env|envBool|envInt)\(\s*"\KPOM68K_[A-Z0-9_]+' \
     src/ tests/ oracle/ extern/moira/ | sort -u
```

after a cross-check found **22 knobs the code reads that no document
mentioned**. Note the `getenv`-only form misses the JIT knobs, which go
through `jit::detail::env()` (`src/jit/JitConfig.h:22`). **Re-derive rather
than extend by hand**: the check is one command, and it is how this list was
found to be missing in the first place.

**Machine selection** (README documents these in prose):
`POM68K_MACII_MODEL`, `POM68K_LC3_PLUS`, `POM68K_AIO_ID`, `POM68K_IIVI`,
`POM68K_Q605_ID`, `POM68K_CENTRIS_MODEL`, `POM68K_CENTRIS610` (legacy
alias), `POM68K_Q630_ID`, `POM68K_MONITOR`.

**CPU configuration** — one family per line. Only the Q605 pair was
documented before 2026-07-31:

| knob | effect |
|---|---|
| `POM68K_Q605_NOFPU` | 1 = 68LC040 + soft 68882, 2 = bare `FPUModel::NONE` |
| `POM68K_Q605_CACHE_BOOST` | i-cache throughput overlay, default **4** |
| `POM68K_Q605_ICACHE_MISS` | i-cache miss cost for that overlay |
| `POM68K_CENTRIS_FPU` / `_BAREFPU` / `_CACHE_BOOST` | the same three for `CentrisCpu` |
| `POM68K_Q700_LC040` / `_BAREFPU` / `_CACHE_BOOST` | …and for `Q700Cpu` |
| `POM68K_Q630_LC040` / `_BAREFPU` / `_CACHE_BOOST` | …and for `Q630Cpu` |
| `POM68K_CACHE_BOOST` / `POM68K_ICACHE_MISS` | the 030 CPUs (`Cpu030`, `SonoraCpu`, `VaspCpu`, `RbvCpu`) |
| `POM68K_FLOPPY_BOOST_GATE` | `=0` disables the floppy boost gate on the `Swim1` 030s (`Cpu030`, `RbvCpu`, `VaspCpu`) — the boost then compresses the Sony denibble path below the IWM's 14-tick hold and GCR mounts fail with badDCksum (the pre-2026-08-05 defect, kept reproducible for `lcii_sony_trace`) |
| `POM68K_MMU040_WALK` | disable the 040 ATC (walk per access) |
| `POM68K_040_DCACHE` | arm the M1 architectural cache-TAG model on the 040s (`docs/CACHE_040.md` § M1) — tags, dirty bits and CINV/CPUSH scopes become real state; **data is still served by the bus**, default off |
| `POM68K_PERIPH_STATS` | count the peripheral catch-up path (Cpu040 only): catchUp/flushTicks/mem.tick calls + cycles per call, printed at exit. The old `POM68K_PERIPH_BATCH` knob is GONE — fixed batching was replaced by event deadlines on eight platforms (2026-08-03/04, `CHANGELOG.md` § *Event deadlines*); the remaining fixed-batch machines (compacts, Mac II, IIfx, MSC) have no knob |
| `POM68K_NOFPU` | Mac II **and IIfx**: no 68881/68882 |

**Devices and subsystems**: `POM68K_EGRET_LLE`, `POM68K_CUDA_LLE`,
`POM68K_CUDA_FW`, `POM68K_ADB_LLE`, `POM68K_ADB_KBD_ID` (`AdbLine`'s
power-on keyboard handler ID: 1 = Apple Standard, the default, 2 = Extended
Keyboard II, 3 = the extended protocol with distinct right-hand modifier
codes — a guest can select any of them itself with a Listen R3, this only
moves the reset value), `POM68K_APPLETALK`,
`POM68K_SHARE_DIR`, `POM68K_ATALK_WIRE_BOOST`, `POM68K_LTOUDP`,
`POM68K_FLOPPY` (image path), `POM68K_FLOPPY_RO`, `POM68K_DRIVE_SFX`
(`0` = silence the drive FX), `POM68K_SCSI_DDM_TEMPLATE`,
`POM68K_SCSI_INQUIRY` (`pom68k` = report the emulator's own INQUIRY strings
instead of the Apple-branded Seagate the guest's own disk tools expect —
[§3.3](#33-scsi-ncr-5380)), `POM68K_DAYNAPORT` (`<id>` = put a DaynaPort
SCSI/Link at that SCSI ID on the Quadra 605; `1` = the default ID 3 —
[§3.3bis](#33bis-what-else-can-live-on-the-bus-scsitarget--daynaport)).

**Duo / PG&E (platform #11, in bring-up)** — behavioural:
`POM68K_PGE_ADB` (`0` = detach the ADB bus from the modem cell),
`POM68K_PGE_SPINUS` (host stall per /PMU_REQ edge, µs, default 80, `0` =
off), `POM68K_PGE_CB1INT` / `POM68K_PGE_CB1BYTE` (alternate SPI-clock →
CB1 wirings, MAME-literal A/B), `POM68K_PGE_ADBRX` (disproved RDRF
experiment, kept as signpost), `POM68K_PGE_CHARGER` (`0` = unplug the
charger — boots WORSE, see `docs/DUO_BRINGUP.md`). Diagnostics:
`POM68K_PGE_TRACE`, `POM68K_PGE_HSHAKE` (REQ/ACK + INT-line
transitions), `POM68K_PGE_SPIBYTES`, `POM68K_PGE_TRAP=<hexbyte>`,
`POM68K_PGE_ADBTRACE`, `POM68K_PGE_PCCOUNT="hex,…"` /
`POM68K_PGE_PCWIN="lo,hi[;…]"` / `POM68K_PGE_PCHIST="lo,hi"` (MCU-side
PC counters / windowed hit log / 256-byte-bucket histogram). The
`duo_trace` harness adds `DUO_CKPT`, `DUO_PMLOG`, `DUO_PCCOUNT`,
`DUO_DUMPAT`, `DUO_KEYAT`, `DUO_PGEWATCH`, `DUO_SRAMDUMP` (its header
documents them).

**JIT**: `POM68K_CPU_ENGINE`, `POM68K_DATA_WINDOW` and the whole
`POM68K_JIT_*` family — see `src/jit/POM68K_JIT.md` § 6. **That table is
authoritative** and was itself corrected on 2026-07-31.

**Quadra 900/950 ("Eclipse", in bring-up on the Q700 board)**:
`POM68K_Q900_ADB` (`iop` forces the IIfx-style IOP-only ADB wire),
`POM68K_Q900_IOPBRK` (dump the SWIM IOP's last 256 PCs at its first `$00`
— the open M7 lead, `TODO.md` § 0), `POM68K_Q900_IOP_TRACE=<max lines>`
(every host-window touch), and the test-side `POM68K_Q900_IOPDUMP`
(`q700_boot_etalon`).

**Diagnostics** (stderr traces, no behavioural change unless stated):
`POM68K_ADB_LLE_TRACE`, `POM68K_ADB_PIC_TRACE`, `POM68K_KEY_TRACE`
(machine-thread heartbeat + a one-shot spin dump), `POM68K_FPU_LOG`,
`POM68K_FREEZE_PROBE`, `POM68K_DAFB_CLOCK_TRACE`, `POM68K_SCSI_LAT`,
`POM68K_CD_TRACE`, `POM68K_SE_VIA_TRACE`, `POM68K_ATALK_DEBUG`,
`POM68K_040_CM_STATS` (68040 ATC fills by descriptor cache-mode —
the `docs/CACHE_040.md` M0 probe),
`POM68K_MACIP_DEBUG`, and the IIfx trio `POM68K_IIFX_IO_TRACE` (unknown
I/O touches), `POM68K_IIFX_SCSI_TRACE` (5380/SCSIDMA registers with PC),
`POM68K_IIFX_ADB_TRACE` (ADB line-state transitions + decoded commands).

**Test-only knobs** — read by a gate, never by the emulator; listed so the
next person greps once instead of twice: `POM68K_AIO_EGRET`,
`POM68K_IICX`, `POM68K_MACII_020`, `POM68K_COMPACT_MODEL`,
`POM68K_Q630_ROM`, `POM68K_BOXID`, `POM68K_SENSE`, `POM68K_DIAG`.
Purely test-local ones (`POM68K_MX`/`_MY`, `POM68K_TRAIL`, `POM68K_BERR`,
`POM68K_CD_BOOT`, `POM68K_BEYOND`, `POM68K_HALT`, `POM68K_DUMP`,
`POM68K_FRAMES`, `POM68K_BENCH_*`, `POM68K_PROBE*`,
`POM68K_INPUT_ANYPATH`, `POM68K_JIT_LOCKSTEP_*`, `POM68K_IIFX_SHOT`
(debug PPM out of `iifx_boot_etalon`), `POM68K_IIFX_POST_CYCLES`,
`POM68K_CD_HOT` (`q605_cdrom_etalon`: insert the disc AFTER the Finder is
up instead of at power-on), `POM68K_FLOPPY_IMG` / `POM68K_FLOPPY_SETTLE`
(`lcii_beyond_etalon`'s floppy scenario: which 800K image, and how long to
settle before judging the mount)) are documented in their own file headers.

**Bring-up probes** — added to this list 2026-08-09, when `config_test`
first ran and found twelve knobs the code reads that no document mentioned.
Each is a *chantier* knob: it belongs to an open investigation and goes when
that investigation closes.

| knob | chantier | what it does |
|---|---|---|
| `POM68K_V8_IOHOLE` | V8 map holes (`TODO` § 4) | `=<n>` logs the first n reads in an unmapped V8 I/O hole with the PC. The Classic II ROM dereferences `$50F18038` and the block behind it has never been identified; the access pattern is the only way to name it |
| `POM68K_V8_HOLEVAL` | same | `=<hex>` what a hole reads back on the Eagle's forgiving bus. MAME answers 0, but that is its `address_space` default and not a modelled decision — so this stays a knob, not a fact, until an observable separates them |
| `POM68K_Q900_IOPWATCH` | Eclipse IOP (`docs/IOP_BRINGUP.md` § M7) | `=<hex>` names every writer of one IOP RAM byte (65C02 / host window / DMA). Written for a SWIM IOP that returned through a corrupted stack frame |
| `POM68K_Q900_IOP_TRACE` | same | `=<n>` IOP port traffic, capped (a bare `=1` is useless past the first firmware upload) |
| `POM68K_PGE_TRAP` | Duo PMU (`docs/DUO_BRINGUP.md`) | `=<hexbyte>` break when the PG&E RECEIVES that byte over SPI |
| `POM68K_PGE_PCCOUNT` | same | `="hex,hex,…"` execution counts + first hits for named PMU firmware addresses |
| `POM68K_PGE_PCHIST` / `POM68K_PGE_PCWIN` | same | PMU PC histogram and its window — where the 68HC05 actually spends its time |

`POM68K_Q700_MODEL` belongs with **Machine selection** above: it picks
`q700` / `q900` / `q950` on the discrete-040 board (the GUI menu sets it
before relaunching, and a `$3DC27823` ROM overrides it — that dump IS a
Quadra 950 whatever the environment inherited).

**Retired** — names that must NOT come back without a new decision. Listed
because a knob that vanishes silently leaves its documentation looking
current:

- `POM68K_PERIPH_BATCH` — fixed peripheral batching, replaced by event
  deadlines on eight platforms (2026-08-03/04). The remaining fixed-batch
  machines (compacts, Mac II, IIfx, MSC) have no knob at all.

### How this list stays true

`config_test` (`unit`, asset-free) checks it in both directions: every
`POM68K_*` name the code reads as a string literal must appear here or in
`src/jit/POM68K_JIT.md`, and every name that appears here must exist in the
code unless it is under **Retired**. It understands the three notations this
section already uses — a full name, a `` `POM68K_X*` `` wildcard, and a
`` `POM68K_CENTRIS_FPU` / `_BAREFPU` `` suffix continuation — so nothing has
to be rewritten to be checkable. **Still re-derive rather than extend by
hand**; the gate tells you when you forgot, it does not write the entry.

**Not yet enforced**: an expiry per knob. The seven probes above declare
their chantier; the other ~120 entries do not yet say whether they are a
permanent product option (which earns a gate) or a chantier leftover. That
classification is a decision per knob, not a mechanical one — `TODO.md` § 8.

---

## 6. Test tiers and gates

**Never iterate against a bare `ctest` or a bare `make`.** A full run is
180 gates / ~4h, and `ctest -j` is unsafe because the boot etalons are
contention-sensitive; a bare `make` relinks ~90 binaries under tree-wide
LTO. Labels are **derived from test names** at registration time
(`CMakeLists.txt`, end of the test block), so a gate added tomorrow is
classified the moment it exists.

```bash
make -j4 jitdev && ctest -L smoke     # ~2.5 min end to end
```

`jitdev` (`CMakeLists.txt:1110`) builds exactly the **three** binaries
`-L smoke` needs — `jit_backend_test`, `jit_lockstep_test`,
`q605_boot_etalon`; the other five smoke registrations re-run those same
binaries under different environments.

| command | gates | when |
|---|---|---|
| `ctest -L smoke` | 9 | the working loop — one machine, both engines |
| `ctest -L unit` | 89 (~1 min) | anything touching non-machine code; no ROM or disk image needed |
| `ctest -L jit` | 28 | before proposing a JIT change |
| `ctest -L m040` | 40 | the 68040 family on the default engine plus explicit interpreter references |
| `ctest` | 180 (~4h) | the release gate, once |

Counts verified against `ctest -N` on 2026-08-10; they drift every time a
gate lands, so **re-derive rather than trust them**. (`src/jit/POM68K_JIT.md`
§ 5 carries the same tiers with the per-lockstep-flavour breakdown and the
timings — it is the authority on the JIT side of this loop.) Asset-dependent gates
soft-skip when the user-provided ROM/disk images are absent. The
whole-machine `*_trace` binaries (`sony_trace`, `lcii_sony_trace`,
`q605_trace`, `macii_mouse_trace`, …) are `EXCLUDE_FROM_ALL` dev tools, not gates — the
gate is the `add_test` NAME, which is not always the binary name
(`macii_mouse_trace` registers as `macii_mouse_etalon`).

### The GUI ↔ machine-thread contract (`src/MachineHost.h`)

Every machine runs on one CRTP host: `MachineHost<Derived, Mem, Cpu, Audio>`.
It owns the thread, the command queue, the framebuffer double buffer, the
status atomics, the `SaveStateSlot` and the JIT gauge snapshot. A platform
supplies only its own half:

| hook | what it does |
|---|---|
| `int64_t frameCycles() const` | cycles in one emulated frame |
| `void emulateQuantum()` | run one frame; bump `framesRun_` |
| `bool drainAudio()` | pull samples into `samp_`; true = audible |
| `void renderFrame(out, w, h)` | fill the publish buffer |
| `void publishStatus()` | store this platform's status atomics |
| `static constexpr bool kStereo` | which `MacAudioHost` entry points to use |

Two rules the header enforces rather than documents. **`fb_` is the platform's
private raster surface** — on the row-granular decoders it persists across
frames and only the scanned rows are repainted, so `renderFrame()` fills a
separate `out` and publication is a swap; the host never takes `fb_`.
And **`cpuEngine()` follows the machine, not the click**: the menu tick reports
the engine actually running, and a swap lands one queue round-trip later.

Per-platform variations that are not worth a hook go to
`if constexpr (requires { … })` in `applyCmds()` — the CD bay
(`insertBayMedia`), the monitor sense (`setMonitorSense`), the floppy (the Duo
230 has no drive at all), and `mouseButton(bool)` vs `(bool, int)`.

CRTP and not a base class **on purpose**: every `self()->` resolves at compile
time, so the no-virtual-in-the-bus-path rule stays mechanical.

Gated by `machinehost_test` (`unit`, asset-free) — queue ordering, the double
buffer, both pacing branches, the thread teardown. That gate exists because
`main.cpp` is the only translation unit outside `pom68k_core`: nothing that
stays in it can be tested, which is why the contract was moved out before it
was unified (`CHANGELOG.md` 2026-08-09 (third)). **The GUI layer above it is
still compile-verified only.**

### What a gate prints about its own assets

Every gate that opens a user-provided file prints one `ASSET` line per file
before it boots — path, size, SHA-256, and for a disk image the HFS volume name
and `drVolAtrb` with bit 8 decoded:

```
ASSET disk   "hdv/MacOS-8.1-boot.vhd" 314621952 B sha256 ea4f068a…
             HFS "Mac-8.1-US" drVolAtrb $0100 clean
```

One call, `testasset::report({ rom, img });` from `tests/AssetFingerprint.h`
(header-only; `tests/` is on the include path for every gate). The role label is
deduced from the path, so there is nothing per-gate to keep consistent. The
volume probe walks the Apple Partition Map when block 0 is `'ER'` and falls back
to a bare MDB at offset 1024 for the flat `.dsk` images.

**Why it exists**: `roms/` and `hdv/` are user-provided and gitignored, so a
gate's fixture can change under it with nothing in the record. On 2026-08-06
that cost two wrong "code regression" diagnoses — `CHANGELOG.md` 2026-08-09.
The digest is also what an `assets.lock` would pin, if one lands.

**Bit 8 clear is a tell, not a verdict.** `hdv/HD20SC.vhd` reads `$0000` and
four green gates boot from it. It says where to look first when a boot gate goes
red. Cost is ~265 MB/s, 1.4 % of `q605_boot_etalon` — no cache, no opt-out knob.
The mechanism is itself gated by `asset_fingerprint_test` (`unit`, asset-free).

Pinned CPU vector suites, which are gates in their own right:
`sst68000` (**1 000 058** accepted SingleStepTests 68000 vectors),
`sst68030` (**3 082** pinned integer/MMU/bus-fault/FPU vectors from the
WinUAE/Hatari oracle; rulings D1-D22 in `oracle/fuzz/disputes/NOTES.md`),
`sst68040` (**7 200/7 200** integer/random/MMU vectors).

---

## 7. Sources

MAME `src/mame/apple/mac128.cpp`, `macii.cpp`, `maclc.cpp`, `maclc3.cpp`,
`maciivx.cpp`, `maciici.cpp`, `macquadra605.cpp`, `macquadra630.cpp`,
`macquadra700.cpp`, `macquadra800.cpp`, `maciifx.cpp`,
`macpwrbkmsc.cpp`, `machine/applepic.cpp`, `machine/msc.cpp`,
`machine/m68hc05pge.cpp`, `machine/djmemc.cpp`,
`machine/iosb.cpp`, `machine/f108.cpp`, `machine/rbv.cpp`,
`machine/v8.cpp`, `machine/vasp.cpp`, `machine/sonora.cpp`,
`machine/adbmodem.cpp`, `machine/macadb.cpp`, `machine/swim2.cpp`,
`video/dafb.cpp`, `video/mv_sonora.cpp`, `video/valkyrie.cpp`,
`machine/ncr5380.cpp`, `machine/ncr53c90.cpp`, and `cpu/pic16c5x/pic16c5x.cpp`
· WinUAE/Hatari m68k + MMU/FPU cores · Motorola MC68030UM / MC68040UM /
MC68881-882UM · pce-macplus (`macplus.c`, `mem.c`, `scsi.c`, `iwm.c`)
· Mini vMac (`GLOBGLUE.c`, `SCRNEMDV.c`, `SNDEMDEV.c`, `RTC.c`)
· GttMFH 2e (archive.org) · *Inside Macintosh* III · *Inside AppleTalk*
· netatalk 2.4.9 + macipgw · retro.co.za Mac PAL reverse-engineering
· bigmessowires.com "Plus Too" series · mcosre/gryphel ROM version lists.

(MAME sources are mirrored locally at `~/src/refs/` — the docs' "refs/mame"
paths are not repo-relative.)
