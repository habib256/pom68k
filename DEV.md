# DEV.md — implementation deep-dives

Pinned hardware reference for the Mac Plus, cross-checked across MAME
`mac128.cpp`, pce-macplus, Mini vMac, *Guide to the Macintosh Family
Hardware* 2e (GttMFH) and *Inside Macintosh* III (web research, 2026-07-14).
Every subsystem port must cite one of these plus a gate test.

## Address map (24-bit)

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
  hit its own "no network" timeout rather than hang. Two additions to
  the same class, both standard 8530 behaviour, both gated by
  `scc_ext_test` and invisible to the Plus mouse path (`input_etalon`
  green):
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

## CPU integration notes

- Moira precise-timing: `sync()` before every bus access — contention and
  VIA E-clock sync hook there (NeoST pattern: `iackSyncBefore/After` for
  IACK E-clock waits).
- `MOIRA_EMULATE_ADDRESS_ERROR=true`: Mac software (and the oracle phase)
  needs address-error frames.
- Demo ROM (`DemoRom.h`) mimics the real boot: DDRA=`$7F`, then
  ORA=`$40` (overlay off + main screen buffer).

## Sources

MAME `src/mame/apple/mac128.cpp` · pce-macplus (`macplus.c`, `mem.c`,
`scsi.c`, `iwm.c`) · Mini vMac (`GLOBGLUE.c`, `SCRNEMDV.c`, `SNDEMDEV.c`) ·
GttMFH 2e (archive.org) · Inside Macintosh III · retro.co.za Mac PAL
reverse-engineering · bigmessowires.com Plus Too series · mcosre/gryphel ROM
version lists.
