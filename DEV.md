# DEV.md — implementation deep-dives

Pinned implementation reference — **one section per machine platform**,
each with its own reference machine; the other members of a family are
identity/clock variants of it and are listed inside that section:

| Platform | Reference machine | Variants in the same section |
|---|---|---|
| 68000 + PAL glue | **Mac Plus** | (128K/512K later) |
| GLUE + NuBus | **Mac II** | IIx, IIcx (68030 on the same board) |
| V8 gate array | **Mac LC II** | LC, Classic II (Eagle), Color Classic (Spice), Mac TV (Tinker Bell) |
| RBV (RAM-based video) | **Mac IIsi** | IIci (PIC ADB modem + discrete RTC) |
| Sonora gate array | **Mac LC III** | LC III+, LC 520/550, Color Classic II, and the VASP recombination (IIvx / IIvi) |
| MEMCjr + PrimeTime | **Quadra 605** | LC 475, LC 575 |
| djMEMC + IOSB | **Centris 650** | Centris 610, Quadra 610, Quadra 650, Quadra 800 |
| Discrete 040 + DAFB | **Quadra 700** | (Quadra 900/950 once the IOPs exist) |

The per-machine bring-up narratives (what broke and why) stay in
`CHANGELOG.md`; the machine roster is in `CLAUDE.md`.
The Plus material is cross-checked across MAME `mac128.cpp`, pce-macplus,
Mini vMac, *Guide to the Macintosh Family Hardware* 2e (GttMFH) and *Inside
Macintosh* III (web research, 2026-07-14). LC II and Quadra details use MAME's
Apple machine/devices plus the real ROM and System drivers as protocol oracles.
Every subsystem port must cite one of these plus a gate test.

## Mac Plus platform (M0-M7, cycle-exact) — address map (24-bit)

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

## Boot overlay

Reset ⇒ OVERLAY=1 (VIA PA4): ROM mirrored over `$000000-$5FFFFF`, RAM at
`$600000-$7FFFFF`. ROM tests RAM at `$600000`, then clears PA4 → normal map.
Writes to the RAM window are discarded while overlay is on (MAME). SE and
later auto-clear on first `$400000` access — Plus does **not**.

## Video

- Main buffer **ramTop−$5900**, alt **ramTop−$D900** (main−$8000). 4 MB:
  `$3FA700`/`$3F2700`. `ScrnBase` global = `$0824`.
- 512×342×1 bpp = 21 888 bytes, 64 bytes/row contiguous; **MSB = leftmost,
  1 = black**. VIA **PA6: 1 = main, 0 = alternate**.

## Sound (M6 ✓)

- Main **ramTop−$300**, alt **ramTop−$5F00**; 370 words/frame: even byte =
  8-bit sample, odd byte = disk-PWM (ignored by the Plus's 800K drives).
  One word fetched per scan line ⇒ **22 254.55 Hz** (15.6672 MHz / 704).
  Output is 1-bit PWM into an integrator; we take the byte as unsigned
  linear PCM `(byte-128)/128` (the standard approximation). VIA PA3 selects
  buffer (1=main); PB7 enable (0=enabled); PA2-0 volume (0-7).
- `MacAudio` extracts the 370 samples/frame; `MacAudioHost` (miniaudio,
  GUI-only) plays them through a lock-free SPSC ring at 22254 Hz. Only
  **non-silent frames** are pushed, so the ring stays drained while the
  machine turbos through the silent RAM test — the startup chime and system
  beeps still play at the right pitch, just slightly delayed.
- The **startup chime** is a clean ~601 Hz (≈D5) tone for ~0.7 s at power-on
  (before the RAM test), then PB7 mutes it. `sound_test` captures it to
  `chime.wav` and checks it's an audible decaying tone in the beep band.

## VIA 6522

- **Port A** (`$EFFFFE`, DDRA=`$7F`): 7 in vSCCWrReq · 6 out vPage2 (screen)
  · 5 out vHeadSel (floppy) · 4 out vOverlay · 3 out vSndPg2 · 2-0 out volume.
- **Port B** (`$EFE1FE`, DDRB=`$87`): 7 out vSndEnb (0=on) · 6 in **H4 =
  horizontal blanking** (1 = in hblank; MAME returns constant `$40` — derive
  from beam counters for cycle accuracy) · 5/4 in mouse Y2/X2 · 3 in button
  (0=pressed) · 2 out rTCEnb (0=on) · 1 out rTCClk · 0 i/o rTCData.
  Mouse X1/Y1 quadrature → SCC DCD (level-2 IRQs).
- **Clock: φ2 = E = 7.8336/10 = 783.36 kHz** (T1/T2 tick 1.2766 µs).
- IFR bits: 7 IRQ · 6 **T1=Sound Driver** · 5 **T2=Disk Driver** · 4 CB1 kbd
  clk · 3 CB2 kbd data · 2 SR · 1 **CA1=VBL 60.1475 Hz** · 0 CA2=one-second.
- **IPL: 1=VIA, 2=SCC, 4=programmer's switch** (additive; Mini vMac:
  `IPL = (VIA & ~SCC) | (SCC<<1) | (button<<2)`). All autovectored. No SCSI
  IRQ on the Plus (polled).

## Timing (M4 cycle accuracy)

- Dot clock 15.6672 MHz; CPU C7M = 7.8336 MHz; SCC PCLK = 3.9168 MHz (MAME).
- Line: 704 dots = 512 visible + 192 hblank = **352 CPU cycles**,
  22 254.5 Hz. Frame: **370 lines** (342 visible + 28 vblank), **60.1474 Hz**
  = **130 240 CPU cycles** (hard-coded in Mini vMac and pce; ours matches).
- **RAM contention**: CPU and video alternate 4-cycle slots during the 512
  visible dots — CPU loses **128 of the first 256 cycles of each visible
  line**, plus **4 cycles per line (all 370)** for the sound-word fetch. ROM
  and I/O are never contended. Validation target: average CPU RAM bandwidth
  **2.56 MB/s** (GttMFH Table 5-3). MAME lump-sums per line; we should model
  it in `Cpu68k::sync()` once beam counters exist.

## ROM

128 KB (two 64 KB byte-lane ROMs, A0 undecoded). Versions by 4-byte Apple
checksum: v1 `$4D1EEEE1` "Lonely Hearts" (SCSI boot bug), v2 `$4D1EEAE1`
"Lonely Heifers" (most common), v3 `$4D1F8172` "Loud Harmonicas". Early
boot: ROM checksum (Sad Mac 01) → RAM tests at `$600000` (Sad Mac 02-05) →
overlay clear → VIA/IWM/SCC init → SCSI probe → beep → blinking-?.
**Minimum to reach the blinking-?** (BMOW Plus Too): CPU + ROM + RAM +
framebuffer + partial VIA (overlay, IER/IFR, CA1 VBL, CA2 one-sec) + IWM
stub reading `$1F`; SCC/SCSI/RTC reads must merely terminate.

## RAM sizing

Configs: 1/2/2.5/4 MB (two SIMM rows). The ROM sizes RAM itself via
mirror/address-uniqueness tests and stores top+1 in `MemTop` (`$0108`) —
emulators just mirror via a mask and let the ROM discover it.

## Implementation status vs this reference (M4)

- Contention: implemented exactly as above in `Cpu68k::contentionDelay`
  (slot-accurate, iterative across busy slots); gate `contention_test`
  reproduces the 2.56 MB/s figure. Applied to RAM only, before each bus
  access (Moira precise-timing `sync` has already run).
- VIA timers: φ2 ticks batched through `MacMemory::tick` from the CPU's
  peripheral catch-up; the 6522's ±1-cycle reload/IFR latency is NOT yet
  modeled, nor is E-clock (/VPA) alignment of VIA accesses — TODO M4.1.
- RTC: full command/read/write serial protocol, 20-byte PRAM, in-memory
  only (no file persistence yet); seconds start at 0 (deterministic tests).
- PB6 H4 is derived from the true beam position (`clock % 352 < 256` →
  display portion), unlike MAME's constant.

## IWM + Sony 800K (M5, research-pinned + trace-verified)

Full spec tables live in the M5 research report (MAME iwm.cpp/floppy.cpp/
flopimg.cpp/ap_dsk35.cpp, pce, Snow — cross-verified). Key facts our
implementation depends on, several found the hard way with `sony_trace`:

- **8 state lines** at `$DFE1FF + reg*$200`, reg = A9-A12: CA0-CA2/LSTRB,
  ENABLE, SELECT, Q6, Q7; ANY access (read or write) toggles the line —
  the ROM strobes EJECT with a `tst.b $DFEFFF` READ.
- **(Q7,Q6) select** data/status/handshake/mode; the ROM reads data through
  the q6L/q7L addresses (each read also clears that line). Mode = `$1F`.
- **Data register**: nibble MSB-set when ready; clears **~14 IWM clocks
  AFTER a read**, not immediately — the ROM does `tst.b` (poll) then
  `move.b` (capture) and both must see the same nibble. Modeled with a
  14-cycle countdown re-armed only on first read.
- **TACH (sense 7) must be time-based** (spin_ counter × zone RPM
  394/429/472/525/590, 120 edges/rev): the ROM measures spindle speed by
  timing tach edges against VIA T2 *before* reading data; a
  position-derived tach freezes when the IWM isn't streaming and the ROM
  ejects the disk.
- **Sense/command tables** per the MFD-51W (sense F "new interface" = 1,
  SIDES = 1, READY = 0 immediately, STEP completes instantly — matches
  MAME). Commands: CA2 = value, (CA1,CA0,SEL) = address, LSTRB rising edge.
- **GCR**: byte-level nibble stream (no 10-bit sync framing needed), one
  nibble per 128 CPU cycles; 2:1 interleave; the MAME `build_mac_track_gcr`
  rolling checksum ported verbatim and cross-validated against pce's
  independent formulation (200 random sectors, identical output).
- **Boot blocks**: the ROM validates bbID 'LK' AND the bbVersion word at
  +6 (`$4418`), then jumps to bbEntry at +2 (a BRA.W in real blocks).
  Code placed directly at +2 gets rejected (version check) → eject.
- Debug tooling: `sony_trace` (instruction-level driver trace with idle-
  loop filtering), IWM per-reg/sense counters, consumed-nibble ring.

## Input: M0110 keyboard + mouse (M5.5, research-pinned + System-verified)

- **IPL wiring (the bug that cost the session)**: the glue DISCONNECTS the
  VIA's /IPL0 while the SCC interrupts — level 3 never occurs on a Plus.
  Its ROM vector is a bare RTE, so a naive `via|scc<<1` OR livelocks the
  machine the moment both are pending (keyboard SR + mouse DCD). Formula:
  `IPL = (VIA & ~SCC) | (SCC << 1)` (Mini vMac / GttMFH Table 3-1).
- **Mouse**: X1 → SCC DCD-A, Y1 → DCD-B; X2/Y2 → VIA PB4/PB5; button →
  PB3 (0 = down). Per step: set the phase bit FIRST, then toggle DCD.
  Direction: PB4 ≠ DCD-A → right; PB5 = DCD-B → down (X and Y sense are
  opposite). Max ~1 step/axis per 350-450 µs (faster starves Y — B-ext has
  lower priority than A-ext). Quadrature loses ±1 count at direction
  changes (real hardware does too).
- **SCC minimal model**: pointer reg (auto-reset), WR1.0/WR9.3/WR15.3
  enables, WR0 cmd $10 (reset ext status), and crucially **RR2 on channel
  B = the WR2 vector with bits 3:1 replaced by the highest pending source**
  (A-ext = 101, B-ext = 001, idle = 011) — the ROM's level-2 handler
  dispatches Lvl2DT on those bits; it never reads RR3.
- **LC II AppleTalk path (O6.10, the `Scc8530` at V8 $50F04000, IPL 4)**:
  the LC II has no LocalTalk peer, so the System's `.MPP`/LAP layer must
  hit its own "no network" timeout rather than hang. O6.13: V8 word
  accesses on `$F04000` take one ctl/data side-effect (mirrored lanes)
  so they cannot double-advance `ptr_`. Two additions to the same class,
  both standard 8530 behaviour, both gated by `scc_ext_test` and
  invisible to the Plus mouse path (`input_etalon` green):
  1. **Break/Abort on the open line** — `setAbortIdle(true)` (set by
     V8Memory) makes RR0 bit 7 stand and arming WR15 bit 7 latch the
     external/status interrupt: AppleTalk's carrier-sense wait ($A5B28
     spin on the driver mutex $63E) unblocks.
  2. **Tx Buffer Empty interrupt** — the transmit buffer accepts a byte
     instantly (no shift register), so enabling Tx ints (WR1 bit 1) or
     writing data latches Tx-empty (RR3 D4/D1, RR2 status codes
     100/000, cleared by WR0 Reset Tx Int Pending $28). The LAP driver
     arms a serial transaction and sleeps on this completion interrupt
     ($A6540 mutex spin); without it the ISR never runs and boot hangs
     at the « Bienvenue » bar. WR2/WR9 are mirrored to both channels
     (chip-global on a real 8530). RR0 bit 2 (Tx empty) stays asserted.
  Since O6.11 (2026-07-16) this whole path only runs when the user turns
  AppleTalk ON in the Chooser: the factory default is **AppleTalk
  inactive** via classic-PRAM SPConfig (XPRAM $13 = $22, port B nibble
  2 = useAsync; 1 = useATalk — Apple supermario `BeforePatches.a` /
  Patches note #1032330). `.MPP` then never opens LocalTalk. XPRAM
  $E0-$E3 is only the LAP connection selector ('atlk' id, 0 = built-in)
  and cannot disable AppleTalk (bad id → fall back to built-in).
- **Egret XPRAM wire protocol (O6.11, pinned from the ROM's drivers)**:
  ReadXPram `[1,2,1,addr]` and GetPram `[1,7,hi,lo]` are **byte streams
  with no length on the wire** — Egret keeps supplying successive bytes;
  the HOST takes its count, drops SYS_SESSION and waits for the
  XCVR_SESSION release (32-bit engine $40A149C4, 24-bit reader
  $A4A3B4-BC). WriteXPram is `[1,8,1,addr,data…]` (length = data). The
  ROM's SysParam restore is TWO GetPram streams: 16 bytes at XPRAM $10
  → $1F8-$207, then 4 at $08 → $208-$20B — that is where low-mem
  SPConfig ($1FB) comes from. Getting this wrong fails the 'NuMc'
  validity read at $0C and re-runs the cold-PRAM XPRAM re-init on every
  boot. Gate: `egret_test` (stream + host-terminated session +
  WriteXPram round-trip).
- **Keyboard (M0110A)**: commands Inquiry $10 / Instant $14 / Model $16
  (→ $0B) / Test $36 (→ $7D); Null = $7B; transition = keycode*2+1,
  bit 7 = release. Transaction = TWO VIA SR interrupts: one when the
  command finishes shifting out (ACR mode 111), one ~3 ms later when the
  response lands after the driver flips ACR to shift-in (mode 011).
  Verified against System 6's own state: RawMouse ($82C), MBState ($172),
  KeyMap ($174) — `input_etalon`.

## SCSI: NCR 5380 + hard disk (M7, research-pinned + ROM-verified)

Boots System 6 from a raw Apple SCSI image (`hdv/*.vhd`, 512-byte blocks,
'ER' DDM at block 0, 'PM' partition map, Apple_Driver43 + Apple_HFS).

- **Controller** (`Ncr5380`): register-write-driven phase engine at
  `$580000`, reg = A4-A6, A0 = byte lane, A9 = pseudo-DMA/DACK. Polled — no
  CPU interrupt. Selection **without arbitration** (the Plus way): triggers
  on ICR SEL asserted + BSY released with the target-ID bit on ODR,
  independent of the Mode ARBITRATE bit. Phases COMMAND→DATA(IN/OUT)→
  STATUS→MSG IN with REQ/ACK per byte; pseudo-DMA auto-handshakes one byte
  per A9 access. One target at SCSI ID 0. Bit layouts from MAME
  `ncr5380.cpp`; sequence from pce `macplus/scsi.c` and the bit-exact ROM
  disassembly (`SCSI_DO_SELECT`).
- **Target** (`ScsiDisk`): SCSI-1 direct-access — TEST UNIT READY, REQUEST
  SENSE, INQUIRY (byte 0 = 0x00 direct-access is all the ROM keys on),
  READ CAPACITY, READ(6/10), **WRITE(6/10)** (in-memory only — the driver
  writes during mount), MODE SENSE. Raw 512-byte-block image.
- **THE GATE (why it took a day): the ROM's SCSI-presence probe.**
  `E_SoftReset` does `MOVE.L ($420000),D0; CMP.L ($440000),D0; BEQ
  no-scsi`. On real hardware the 128 KB ROM does NOT mirror across the
  whole $400000-$4FFFFF window, so those two longwords differ → SCSI
  present → `HWCfgFlags` ($0B22) bit 7 set → `CheckSCSI` ($407D40) runs the
  6→0 scan, reads block 0, loads the Apple_Driver43, JSRs its init (which
  registers the drive in `DrvQHdr` $0308), and the boot dispatcher boots it.
  Our `MacMemory` originally mirrored the ROM everywhere → $420000 ==
  $440000 → the ROM concluded "no SCSI" and never scanned. Fix: ROM answers
  only at $400000-$41FFFF; above it returns address-dependent open bus
  (`addr >> 16`) so the probe sees a difference. **The Plus does NOT consult
  PRAM for the boot device** (that's a 256K-ROM/SE+ feature) — the scan is
  automatic and unconditional once HWCfgFlags is set.
- **WRITE support is mandatory**: the disk driver writes to the volume
  during mount; a read-only target hangs the boot in a VIA interrupt storm
  after the driver loads. Writes go to the in-memory image only (backing
  file untouched — persistence is a later milestone).

## Mac II platform (functional)

- **CPU:** `Cpu020` drives Moira's 68020 at 15.6672 MHz (functional, not
  cycle-exact); no PMMU (HMMU address translation only where the ROM needs
  it), no FPU requirement for Sys 6/7 boot.
- **Memory/GLUE:** `MacIIMemory` owns the 32-bit GLUE map, ROM overlay
  (**one-way latch** — the System rewrites VIA1 PA with bit 4 set after
  Welcome; re-arming the overlay opens the bus, see CHANGELOG 2026-07-20),
  VIA1 + VIA2, classic ASC (`AscV8` version `$00`, MODE bits 0–1 only,
  edge half-empty + empty-cycle re-IRQ), SCC, IWM, RTC (`Rtc` with
  `factoryDefaults` SPConfig `$22` seeding) and NCR 5380 with pseudo-DMA
  at `$50F060xx` (A0..A1 decoded across the `$6000–$7FFF` window).
- **NuBus:** `NuBus` + `DeclRom` model 32-bit slot windows and declaration
  ROMs; `TobyVideo` is the slot-9 640×480 card with Bt453 CLUT
  (whole-frame decode). VIA2 CA1 slot IRQ only fires when the `$D04` slot
  task queue is armed (empty-queue CA1 → SysError 51).
- **ADB:** default is the **LLE PIC1654S** path when
  `roms/adbmodem/342s0440-b.bin` is present — the real transceiver
  firmware drives the VIA shifter and the bit-serial bus; the mouse only
  moves on this path (see "Mac II ADB: PIC1654S LLE" below).
  `POM68K_ADB_LLE=0` (or a missing dump) falls back to the `AdbVia` HLE
  byte-model (NEW/EVEN/ODD/IDLE over the VIA SR feeding `AdbBus`), which
  drops fast `ADBReInit` traffic — phantom mouse address, frozen cursor.
- **RTC / XPRAM:** the ROM runs **unmodified** since 2026-07-21 — the
  `Rtc` speaks the full 343-0042 protocol (falling-edge shift, 256-byte
  extended XPRAM, MAME macrtc mapping: classic regs 8-11/16-31 = XPRAM
  `$08-$0B`/`$10-$1F`), so the ROM cold-inits its own PRAM and boots
  SCSI via its own `$78-$7B` defaults (driver refNum -33 = SCSI ID 0).
- **Boot HLE: none left** (LLE steps 1-4, 2026-07-21). Sys 7 EtherTalk
  CautionAlerts are dismissed by the TESTS pressing Return over real ADB
  (`keyEvent $24`); SPConfig `$22` (AppleTalk inactive) is only a
  reset-time factory seed.
- **Gates:** `macii_post_etalon`, `macii_boot_etalon` (Sys 6),
  `macii_sys7_boot_etalon` (Sys 7.0/7.1), `declrom_test`, `nubus_test`,
  `toby_test`.

### Variants: Mac IIx / IIcx (68030 on the same board)

`MacIIMemory::Model {MacII, IIx, IIcx}` + a `Cpu020` `is030` flag
(M68030 + M68882) on the shared `mac2fdhd` ROM `$97221136`; same GLUE,
same Toby NuBus. Identity is VIA machine-ID pins only (IIx VIA2 PB `$87`,
IIcx VIA1 PA `$C1`). **The wall**: once the 030's own PMMU is enabled
(TC bit 31), Moira hands the bus a *physical* address, so
`MacIIMemory::physAddr`'s GLUE 24-bit remap must be skipped — otherwise it
translates twice and the boot wedges mid-System (SCSI freezes ~351
commands in). This is the same 020-HMMU-vs-030-PMMU split `V8Memory`
already had. Gates: `iix_boot_etalon`, `iicx_boot_etalon`.

## Mac II ADB: PIC1654S LLE (default; `POM68K_ADB_LLE=0` → HLE)

The HLE `AdbVia` byte-model drops ~99.99 % of the ROM's fast `ADBReInit`
traffic (its fixed timer misses the rapid ST transitions), so the ROM's
device map diverges from `AdbBus` and the mouse ends up polled at a
phantom address (frozen). The LLE path runs the *real* transceiver, the
way MAME does — the whole NEW/EVEN/ODD/IDLE handshake and the ADB
timeouts live in the firmware, so they are exact by construction.

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
  co-stepping — bit cell 1564 cyc ≈ 99.8 µs and attention 12410 ≈ 792 µs,
  i.e. the ADB spec values, since the firmware's delay loops ARE the
  reference. Gate: `adbline_test`.
- **`Via6522::extShiftCB1`** — external-clock (CB1) shift-register support
  for the ADB modes (011 shift-in, 111 shift-out), rising-edge clocked to
  match the firmware's shift routines (`0x065` send / `0x077` receive).
- **`AdbVia`** — wires the PIC ports to the VIA (RA0/RA1←PB4/PB5 ST,
  RB2/RB3→CB1/CB2 shifter, RB4→PB3 IRQ, RA2/RA3→ADB line) and steps the
  PIC at every VIA1 access (`MacIIMemory::viaAccess` → `AdbVia::syncTo`).

Two fixes made it run end-to-end (PIC receives ROM commands, drives the
ADB bus, `AdbLine` decodes them): **ST-idle pull-up** — PB4/PB5 read high
when the 68k leaves them as inputs, so idle = ST=IDLE(3) not a spurious
NEW that RESET-looped the PIC (`readA` does `portB | ~ddrb`); and the
line-timing recalibration above.

**Default since 2026-07-22.** The "self-test misroute" blocker was three
stacked bugs (full detail: CHANGELOG "Mac II LLE ADB default"): PIC
instruction cost ignored (`tickLle` now charges `run(1)`'s real 1–3-cycle
cost — the firmware's wire timing then lands on the ADB spec); a phantom
IFR.SHIFT from the Slot-Manager ORB `armShiftComplete()` hack whose
`$15D(A3)` guard is ADBBase→flags (gated off in LLE — the firmware's
idle-timeout byte serves the $7100 POST wait for real); and the VIA
mode-111 ext shift-out advancing the SR on the *first* CB1 falling edge,
delivering every ROM byte `<<1` to the PIC. Note for future co-stepping
work: `syncTo`'s burst-at-VIA-access interleaving is temporally *exact* —
VIA state only changes at VIA accesses and the CPU only sees PIC effects
through VIA reads, both of which sync first; only the cost accounting was
wrong. Firmware map (from disassembly): ST-change dispatch @0x022 with
index = (last-cmd-op<<2)|newST; 0x065 = shift byte PIC→VIA, 0x077 =
VIA→PIC (8 CB1 cells); 0x0C5 ladder = wire bit-width measurement; 0x1A0 =
execute command on the wire; 0x1F3 = IRQ (RB4/PB3) decision.
Diagnostics: `POM68K_ADB_LLE_TRACE=1` (wire edges + `adbtalk` decode),
`POM68K_ADB_PIC_TRACE=1` (ST samples, PIC port-B writes, VIA1 traffic).
Repro/gate: `macii_mouse_trace` (mouse must move; also exercises the
`via2Ca1SlotTaskArmed` slot-VBL → jCrsrTask fix, same CHANGELOG entry).

## V8 platform — Mac LC II (O6, functional)

- **CPU:** `Cpu030` drives Moira's 68030 + PMMU + 68882 at 15.6672 MHz.
  `sst68030` pins 3 082 integer/MMU/bus-fault/FPU vectors generated by the
  WinUAE/Hatari oracle. The machine adds a functional instruction-cache timing
  overlay; it is not a cycle-exact 68030 bus/cache model.
- **V8 map:** `V8Memory` owns RAM/ROM overlay, V8/pseudo-VIA registers,
  VIA1, SCC, SWIM1, ASC-V8 and NCR 5380 pseudo-DMA. `Egret` + `AdbBus`
  implement the 68HC05 transport, ADB keyboard/mouse, RTC and XPRAM stream
  commands. `V8Video` decodes the built-in framebuffer through the Ariel CLUT.
- **Boot contract:** the real 512 KB LC II ROM boots System 7.x from SCSI to
  the Finder. `lcii_boot_etalon` is the whole-machine gate; `egret_test`,
  `pseudovia_test`, `v8_ramsize`, `v8_video_test`, `asc_test` and
  `scsi_pdma_test` pin the reusable devices. AppleTalk defaults inactive via
  SPConfig XPRAM; enabling it uses the SCC no-peer timeout path described
  above.

### Variants: LC, Classic II, Color Classic, Mac TV

`V8Memory::Model` covers the whole V8 / Eagle / Spice / Tinker Bell spread
with a `spiceClass()` predicate (Color Classic ∪ Mac TV: SWIM2 + Sonora
EASC `$BC` + Cuda) and a `cpuHz` ctor param — the gate array stays in the
C15M domain while the CPU ticks rescale (the VASP pattern). The **LC** is
68020 + HMMU (2 MB soldered, HMMU mask on pseudo-VIA PB3); **Classic II**
is the Eagle (PA `$92`, mono 512×342 out of RAM @ `$1F9A80`, forgiving
bus); the **Color Classic** is Spice (PA `$82`, fixed sense 2, SWIM2 in
the gate array, Cuda 341S0788 — the factory 341S0417 still wedges the
M68hc05); the **Mac TV** runs its own `$EAF1678D` Tinker Bell ROM (PA id
`$84`, fixed 640×480 sense `$06`, 8 MB cap, 68030 @ 31.3344 MHz, no FPU)
— *not* the EDE66CBD Sonora AIO ROM, despite the archive filename.
Gates: `lc_`, `classic2_`, `cclassic_`, `mactv_boot_etalon`.

## MEMCjr + PrimeTime platform — Quadra 605 / LC 475 / LC 575 (Q1-Q8, functional)

- **CPU/core:** `Cpu040` drives the Q1-Q4 Moira 040 core at 25 MHz. The core
  implements MOVE16, 040 control registers, 040 exception frames, TTRs,
  three-level URP/SRP translation, MMUSR/PTEST and restartable MMU faults.
  `sst68040` pins **7 200/7 200** vectors across integer, random and MMU
  families. Q8 adds a functional I/D ATC and a Cpu040 i-cache/throughput
  overlay; architectural copyback/snooping and cycle-accurate 040 timing
  are not modeled.
- **FPU compatibility:** a real Quadra 605 has a full 68040+FPU; LC 475 is
  68LC040. The MAME `macqd605` oracle is a full 68040, and POM68K defaults to
  M68040 + Moira's 68882. `POM68K_Q605_NOFPU=1` selects **M68LC040 + soft
  68882** (SoftwareFPU-equivalent); `=2` selects TRUE bare `FPUModel::NONE`,
  and Mac OS 8.1 installs the ROM's **integer PACK 4** for it — the selector
  is the ROM-resource combo in XPRAM `$AE`, validated against UniversalInfo
  defaultRSRCs and HWCfgFlags bit 12 (CHANGELOG 2026-07-21 "Bare no-FPU
  solved"). `q605_nofpu_boot_etalon` gates the soft path,
  `q605_barefpu_boot_etalon` the bare one.
  Q8 also adds a separate I/D ATC (32 entries) and a Cpu040 i-cache/throughput
  overlay (`POM68K_Q605_CACHE_BOOST`, default **4** since 2026-07-25;
  `POM68K_MMU040_WALK` disables the ATC). The old default of 1 carried the
  note "boost 2+ fails SCSI bring-up" — re-measured, that was a stale
  symptom: the whole 040 family is green at 4 (CHANGELOG "The Quadra's
  boost-1 pin was stale"). Stall / VIA E-clock / SWIM C15M sync are
  boost-invariant, and the two unit tests that were reading the boosted
  clock now read `machineClock()`.
- **Memory/I/O:** `Q605Memory` models MEMCjr ROM overlay/RAM sizing and the
  PrimeTime window: VIA1, Quadra pseudo-VIA2, SCC, IOSB/MEMCjr registers,
  Cuda-flavoured `Egret`, IOSB ASC `$BB` stereo, SWIM2 (MFM/GCR SuperDrive) and
  `Ncr53c96` TurboSCSI.
- **SWIM2 cell engine** (2026-07-23, `LLE_VS_HLE.md` step 13): `Swim2`
  runs MAME `swim2.cpp`'s bit engines — the MFM sync-hunting shifter with
  serial CRC-CCITT (`$CDB4` seed, `M_CRC0` on handshake bit 1), the GCR
  high-bit framer, and the TSS write serializer in half-cycles.
  `SonyDrive` stores each track as one revolution of raw cells (MFM 16 /
  GCR 31 C15M clocks per cell) padded to the spindle geometry; ACTION
  start lands the head at the spin-counter angle (real rotational
  latency; `setSpinClockHz` declares the spin tick unit — Q605 25 MHz).
  MFM writes are rebuilt per-gap (PLL-style, drift-proof), decoded by an
  offline replica of the read machine, and commit only CRC-valid
  sectors. The Iwm/SWIM1 nibble path is unchanged. Gates: `swim2_test`,
  `swim2_media_test`, `q605_floppy_boot_etalon`.
  The 53C96 supports streamed CDBs, PIO Transfer Info, DRQ-gated pseudo-DMA,
  STATUS/MSG completion and the OS 8.1 SCSI Manager's mixed PIO/DMA chunking.
- **DAFB/Antelope** (`Dafb.h/.cpp` since 2026-07-21 — one concern per
  file; the MEMCjr 6+6-bit holding split and the VRAM stay with the bus
  decoder in `Q605Memory`): 1 MB VRAM at `$F9000000`; DAFB registers at
  `$F9800000`. MEMCjr transfers DAFB values through the real 6+6-bit holding
  protocol. MAME-parity pass (2026-07-21): Swatch CRTC timing registers
  drive `recalc_mode()`-derived geometry (`dafbHres/Vres`), the Gazelle
  clock generator's bit-banged 20-bit M/N/P word sets the pixel clock,
  and the frame/VBL timing follows the guest's programmed
  `htotal×vtotal/pclk` (OS 8.1 programs 896×525 at 30.25 MHz). Extended
  monitor sense (drive pins + ext-code read-back), Swatch display-disable
  bit, VBL/cursor interrupts, CLUT and RAMDAC-selected 1/2/4/8/16/24-bit
  modes are all dafb.cpp semantics. **The clock generator is per-flavour**
  (`Dafb::Clockgen` ctor variant, 2026-07-27): Gazelle on MEMCjr
  (`dafb.cpp:1322`), **DP8534** on djMEMC (`:1197` — MSB-first bitstream
  into `$303`, committed by any write to `$313`, decoded as five
  bit-reversed bytes → P/RCNT/NCNT), **DP8531** on the Quadra 700's
  discrete DAFB (`:884` — nibble registers at `$3n3`, latched by register
  15 → R/P and the A/B swallow-counter split of the N modulus). One
  decoder for all three was not merely incomplete: the DP8531's register-12
  nibbles land on `$3C3`, the Gazelle's own serial port, so the Q700 used
  to latch a pixel clock out of unrelated data. Real ROM values, visible
  with `POM68K_DAFB_CLOCK_TRACE=1`: Centris 650 → 30.26 MHz, Quadra 700 →
  25.175 then 30.24 MHz. `q605_dafb_test` pins
  register/depth/reset/CRTC/sense and all three clock generators. GUI and `q605_trace` render
  indexed modes from live hardware state; the 256-color Finder is proven
  live (`q605_boot_etalon`: mode 3, base `$1000`, stride 1024).
  `q605_trace --dafb-io N` gives DAFB and MEMCjr holding-port traffic its
  own trace budget. Remaining gaps: no VRAM arbitration, VBL line
  hard-coded at 480 (as in MAME).
- **Boot/UI:** the FF7439EE 1 MB ROM boots Mac OS 8.1 (640×480×8) and
  System 7.5 / 7.5.5 / 7.6 (often 1bpp until Monitors) to the Finder, and
  is selectable beside Plus/Mac II/LC II in the GUI. `q605_trace` is the
  diagnostic whole-machine runner; `q605_boot_etalon` is the whole-machine
  gate (soft-skips when the user-provided ROM/disk assets are absent).

## RBV platform — Mac IIsi / IIci (`RbvMemory`/`RbvCpu`/`RbvVideo`)

The **ancestor** of the V8/VASP/Sonora line (MAME `rbv.cpp:6-9`): RBV
originated the pseudo-VIA and video-out-of-system-RAM those gate arrays
inherited. 68030 @ 20 MHz (IIsi) / 25 MHz (IIci), 512 KB ROM at
`$40000000` (any read clears the overlay), VIA1 at `+$00000`/`+$40000`,
SCC `+$04000`, 5380 `+$10000` (pseudo-DMA `+$06000`/`+$12000`), discrete
ASC `+$14000`, SWIM1 `+$16000`, RBV `+$24000` = Bt478 DAC (MSB lane, the
`Ariel` register model) and `+$26000` = pseudo-VIA. There is **no machine-ID
longword** — the ROM identifies the box from VIA1 PA and the monitor sense
(`montype << 3`) through the pseudo-VIA video-config hook. Framebuffer =
the **start of system RAM**.

- **IIsi**: Egret **344S0100** firmware LLE, VIA1 PA reads `$97`.
- **IIci**: same map, different front end — PIC1654S **ADB modem**
  (`AdbVia` + `342s0440-b.bin`) on VIA1 CB1/CB2 + PB4/PB5 and a discrete
  **343-0042 RTC** on PB0-2/CA2 (the Mac II/Centris wiring), no MCU
  reset-hold, three empty NuBus slots. VIA1 PA must read **`$C7`**, not
  `$C6`: MAME's `via_in_a` is `0xC6 | BIT(config,1)` and diagnostic mode is
  disabled by default, so PA0 = 1. With PA0 = 0 the ROM takes the
  diagnostic path and spins forever in its VIA-T2 calibration loop.
- **The IIsi is the machine that exposed the bus-time bug.** Its ROM's
  Egret transport is a *host-paced* bit-bang (`bclr`/`bset` of VIA1
  PB4/via_full back to back), and until 2026-07-25 `viaSync` aligned to the
  E-clock in the **boosted** core-clock domain — so the pulse came out
  `cacheBoost_`× too short and the firmware missed it. Bus time is now
  charged in machine cycles on all four 030 CPUs (`machineClock()`,
  `stall()` scaled by the boost) and the machine runs at the shared default
  boost. See CHANGELOG 2026-07-25.
- **The pseudo-VIA here is the *base* device, not the V8 one.** MAME's
  `rbv.cpp:66` (and `vasp.cpp:90`) instantiates `APPLE_PSEUDOVIA`, where
  IFR bit 4 (ASC) latches only the 0→1 edge and the guest's write-1-to-ack
  sticks — the level flavour with its `~$10` ack mask belongs to
  `v8_pseudovia_device` / `sonora_pseudovia_device` alone. Selected by
  `PseudoVia::Flavour` since 2026-07-27; see CHANGELOG for why VASP in
  particular could not survive stacking two level behaviours.
- Gates: `iisi_boot_etalon`, `iici_boot_etalon`.

## Sonora platform — LC III / LC III+ / AIO family, + the VASP recombination

`SonoraMemory`/`SonoraCpu`/`SonoraVideo` model the Sonora gate array:
machine-ID longword at `$5FFFFFFC`, 1 MB VRAM at `$60000000`, video =
the `mv_sonora` cell (5 modelines, CLUT, monitor sense in `vctrl`).
Parameterised by ctor: `cpuHz` (25 MHz LC III / 33.33 MHz LC III+ /
LC 550) + `machineId` (`$A55A0001`, `$A55A0003`, `$A55A0100`,
`$A55A0101`) + `cudaAdb` (the all-in-ones carry a **Cuda 341S0060**
instead of the Egret 341S0851 — 2.37 livelocks on pseudo-cmd `$0E`).
`VaspMemory` is the same shell with **V8 peripherals** hung off it
(AscV8, SWIM1, Ariel, pseudo-VIA video hooks) and a **fixed 2048-byte row
pitch**; `$A55A2015` @ 31.3344 MHz = IIvx, `$A55A2016` @ 15.6672 MHz =
IIvi. **Unmapped I/O on both maps reads back 0** (MAME `iosb.cpp:54-65`
parity) — an open-bus `$FF` hard-wedges the LC III+ ProductInfo RAM-device
poll at `$50F0A000`. The from-scratch AIO bring-up (MAME's stubs do not
boot; the ROM was the oracle) is written up in `docs/LC520_BRINGUP.md`.
Gates: `lc3_`, `lc3plus_`, `lc520_`, `lc550_`, `cclassic2_`, `iivx_`,
`iivi_boot_etalon`.

## djMEMC + IOSB platform — Centris 610/650, Quadra 610/650 (`CentrisMemory`/`CentrisCpu`)

The two-ASIC I/O generation (MAME `macquadra800.cpp` + `djmemc.cpp` +
`iosb.cpp`), recombining Q605 parts: DAFB video, 53C96 TurboSCSI, SWIM2,
IOSB ASC, Quadra pseudo-VIA2 — plus a **discrete 343-0042 RTC** and the
**PIC1654S ADB transceiver** (no reset-holding MCU, so the CPU runs from
power-on). The model is strapped in VIA1 port A: `$46` Centris 650, `$40`
Centris 610, `$44` Quadra 610, `$52` Quadra 650; the `$5FFFxxxx` longword
is the fixed IOSB `$A55A2BAD`. Centris = 68LC040 @ 25/20 MHz, Quadra =
full 68040 @ 33/25 MHz (`POM68K_CENTRIS_FPU`).
**The one map delta that matters**: djMEMC maps a **2 MB** VRAM window
(`$F9000000-$F91FFFFF`) where MEMCjr mapped 1 MB — the 1 MB VRAM must be
mirrored across it or the ROM's VRAM sizer bus-errors at `$F91FFFFC` and
drops into the ROM serial monitor. Gates: `centris610/650_`,
`quadra610/650_boot_etalon`.

## Discrete-040 platform — Quadra 700 (`Q700Memory`/`Q700Cpu`)

The first Quadra, and the machine that shows the family seam: a full 68040
@ 25 MHz whose front end is the **Mac II's** (VIA1 + a real VIA2 6522,
discrete 343-0042 RTC, PIC1654S ADB transceiver — firmware LLE) and whose
back end is the **Quadra's** (DAFB video, 53C96, SWIM1, EASC, SCC, SONIC),
at the `$5000xxxx` layout the Centris/Q800 share. ROM `$420DBFF3`; VIA1 PA
reads `$C1` (diagnostic disabled — the IIci lesson); 2 MB VRAM; no
machine-ID longword.

- **SCSI hangs off DAFB, not IOSB.** DAFB register `$24` latches four
  wait-state selections (`dafb.cpp:480-530`) and reads back the **live DRQ
  in bit 9**; registers at `+$0F000`, pseudo-DMA at `+$0F100` with the
  bit-18 waitstated alias. This is the cell `docs/LLE_VS_HLE.md` §3 had
  parked as "only matters for a future Q700/Q950-class profile".
- **VIA2 is a real 6522** with the Mac II interrupt fan-in: slot/DAFB IRQs
  on CA1 + port A (active low, DAFB = slot $F → bit 6), ASC on CB1, SCSI on
  CB2, SONIC on PA0.
- *Simplification*: the 60.15 Hz tick is generated directly into VIA1 CA1;
  real hardware routes VIA2's T1 out of PB7 into VIA1 CA1
  (`via2_out_b` "chain 60.15 Hz to VIA1").
- Gate: `q700_boot_etalon` (Mac OS 8.1, 640×480 DAFB).

## JIT — the second execution engine (`src/jit/`, J0/J1)

Full design, invariants and environment surface in **`src/jit/POM68K_JIT.md`**;
the four-point extension it needs inside the vendored core is documented in
`extern/moira/POM68K_VENDOR.md` § *JIT seam*. What matters here:

- **It is a second engine, not a replacement.** Off by default in the GUI,
  headless and CTest. The GUI **CPU** menu switches it live (through the
  machine thread's command queue, so the swap lands between two
  instructions); `POM68K_CPU_ENGINE=jit` selects it at startup. Wired on the
  four 68040 profiles only: `Cpu040`, `CentrisCpu`, `Q630Cpu`, `Q700Cpu`.
- **Multi-target by construction**, because POM68K is multiplatform. Layer 1
  (`jit::Engine`) knows nothing about the host; layer 2 (`jit::Backend`)
  is where an architecture lives. The `threaded` backend generates no code
  and is always compiled and always usable, so `POM68K_JIT_BACKEND=auto`
  never fails to produce a working engine — Emscripten and hardened kernels
  included. `jit::CodeBuffer` already implements portable W^X memory
  (mmap/VirtualAlloc, `MAP_JIT` + per-thread write-protect on Apple silicon,
  explicit i-cache invalidation off x86) for the code generators to come.
- **Measured** on `q605_boot_etalon` (boot to the 256-colour Finder, same
  host, same load): interpreter **58.7 s**, JIT with the fetch window alone
  **27.6 s** (−53 %, ×2.13), JIT with window + block cache **47.9 s** (−18 %),
  JIT with the window off **62.1 s** (+6 %, the engine's own dispatch overhead
  and the honest zero point). The block cache therefore ships **off**
  (`POM68K_JIT_BLOCKS=1` enables it): real 68k code is branch-dense, recorded
  blocks average 1.04 instructions, and the per-branch hash lookup plus trace
  attempt costs more than a one-instruction replay saves. It stays in the
  tree because it is exactly the structure a code generator needs, and
  `jit_lockstep_blocks_test` keeps that path gated.
- **Where the win comes from.** On the 040 Moira has no prefetch queue:
  `mmu040InstrStart` fetches the opcode and the `pc+2` lookahead *through
  the MMU* on every instruction, and `readExt` does the same per extension
  word — each one an ATC probe plus a virtual `read16()` plus the machine's
  address decode. The **code window** replaces that, for instruction fetches
  only, with a bounds check and a load from a host pointer into the guest
  RAM/ROM buffer. On this path `SYNC(x)` is a no-op (`Core::C68020`), so the
  window changes no cycle accounting whatsoever.
- **Blocks are recorded by executing them**, not by decoding: the engine
  runs instructions through `Moira::pomJitExecOne()` and notes where each
  one started and how far the pc moved. No second 68k decoder to keep in
  sync, and a recorded block cannot describe something that did not happen.
  Replay re-verifies the clock budget, `flags == 0`, and that the pc is
  where the trace said — that last check is the catch-all for an unpredicted
  trap or a fault redirect.
- **Three things kill a cached window/block**: a write into a physical page
  holding translated code (`jit::CodeGuard`'s page map, hooked into the four
  memory maps' `write8`/`write16`), a change of the address map itself
  (boot-overlay flip — which is a *read* side effect, so it calls
  `jitMapChanged()` directly), and a change of translation
  (`Moira::pomJitMmuGen`, bumped by every ATC flush and TTR write). Blocks
  additionally stop *before* any opcode that could touch the MMU, a cache or
  the supervisor bit (`jit::classify`).
- **Working loop.** A bare `ctest` runs 118 gates in ~2h30 and a bare `make`
  relinks ~90 binaries under tree-wide LTO — useless to iterate against, and
  `ctest -j` is out because the etalons are contention-sensitive. So gates
  carry **labels derived from their names** (`CMakeLists.txt`, end of the test
  block) and there is a `jitdev` build target:
  `make -j4 jitdev && ctest -L smoke` = **~2.5 min** (56 s to relink 4
  binaries, 98 s for 5 gates) instead of ~3 h. Tiers: `smoke` (one machine,
  both engines — the JIT loop), `unit` (50 gates, 9 s, no assets), `jit` (all
  7 JIT gates), `m040` (26 gates, the 68040 family = the JIT's blast radius),
  bare `ctest` (the release gate). Labels are derived, not declared, so a gate
  added tomorrow is classified when it is registered.
- **Gates**: `jit_backend_test` (backend registry, W^X buffer, classifier
  safety rules — no assets), `jit_lockstep_test` (two Quadra 605 machines
  from one ROM, interpreter vs JIT, compared register by register at every
  instruction boundary, and **failing if the JIT never ran a block**), plus
  the four `jit_*_boot_etalon` twins — the same etalon executables re-run
  with `POM68K_CPU_ENGINE=jit`.

## CPU integration notes

- Moira precise-timing: `sync()` before every bus access — contention and
  VIA E-clock sync hook there (NeoST pattern: `iackSyncBefore/After` for
  IACK E-clock waits).
- `MOIRA_EMULATE_ADDRESS_ERROR=true`: Mac software (and the oracle phase)
  needs address-error frames.
- Demo ROM (`DemoRom.h`) mimics the real boot: DDRA=`$7F`, then
  ORA=`$40` (overlay off + main screen buffer).

## Sources

MAME `src/mame/apple/mac128.cpp`, `macii.cpp`, `maclc.cpp`, `maclc3.cpp`,
`maciivx.cpp`, `maciici.cpp`, `macquadra605.cpp`, `macquadra800.cpp`,
`machine/djmemc.cpp`, `machine/iosb.cpp`, `machine/rbv.cpp`,
`machine/v8.cpp`, `machine/vasp.cpp`, `machine/sonora.cpp`,
`machine/adbmodem.cpp`, `video/dafb.cpp`, `video/mv_sonora.cpp`,
`machine/ncr53c90.cpp` · WinUAE/Hatari m68k + MMU/FPU cores · Motorola
MC68030UM/MC68040UM/MC68881UM · pce-macplus (`macplus.c`, `mem.c`, `scsi.c`,
`iwm.c`) · Mini vMac (`GLOBGLUE.c`, `SCRNEMDV.c`, `SNDEMDEV.c`) · GttMFH 2e
(archive.org) · Inside Macintosh III · retro.co.za Mac PAL reverse-engineering
· bigmessowires.com Plus Too series · mcosre/gryphel ROM version lists.
