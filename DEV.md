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

## Sound (M6)

- Main **ramTop−$300**, alt **ramTop−$5F00**; 370 words/frame: even byte =
  8-bit sample, odd byte = disk-PWM (ignored by the Plus's 800K drives).
  One word fetched per scan line ⇒ **22 254.55 Hz** (15.6672 MHz / 704).
  Output is 1-bit PWM into an integrator (MAME models this; linear PCM is an
  approximation). VIA PA3 selects buffer (1=main); PB7 enable (0=enabled);
  PA2-0 volume.

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
