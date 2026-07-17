# LCII_HARDWARE.md — Macintosh LC II machine blueprint (Phase 2, milestone O6)

Pinned hardware reference for the **Mac LC II** (68030 @ 15.6672 MHz, V8 gate
array, Egret ADB/RTC/PRAM MCU), mirroring the role `DEV.md` plays for the
Plus. Mined 2026-07-15 in source-of-truth rank order:

1. **MAME master** (`src/mame/apple/maclc.cpp`, `v8.cpp`, `egret.cpp`,
   `macscsi.cpp`, `dfac.cpp`, `macadb.cpp`; `src/devices/machine/pseudovia.cpp`,
   `swim1.cpp`; `src/devices/sound/asc.cpp`; `src/devices/video/ariel.cpp`).
   Line numbers below are the master copies fetched 2026-07-15.
2. **Guide to the Macintosh Family Hardware 2e** (GttMFH) — the LC chapter;
   LC II ≈ LC with a 68030 (Apple published no standalone LC II Dev Note;
   the "Macintosh LC II" section rides the LC one).
3. Cross-checks: EveryMac LC II spec sheet, Linux `via-cuda.c` (Egret
   handshake), community ROM lists, and the **real LC II ROM already in this
   repo** (`docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM`, verified).

On conflict the oracle (MAME behaviour) wins, per `CLAUDE.md`.

## Machine summary

| Item | Value | Source |
|---|---|---|
| CPU | MC68030 @ 15.6672 MHz (C32M 31.3344/2), **no FPU** (optional 68882) | maclc.cpp:56-58,460; EveryMac |
| System ASIC | **V8** (343S0116 / 343-0155): memory ctrl, video ctrl, VIA1, pseudo-VIA, ASC-like sound | v8.cpp:7-13 |
| Data path | **16-bit** bus from V8 to RAM/VRAM (the famous LC-family bottleneck) | v8.cpp:16; EveryMac |
| RAM | 4 MB soldered + 2×30-pin SIMM (pairs), **10 MB hard limit** | maclc.cpp:9-10,464-466 |
| ROM | 512 KB (4 × 27C010: 341-0473…0476), checksum `$35C28F5F`, version `$067C` | maclc.cpp:599-605; ROM verified below |
| VRAM | 256 KB soldered, +256 KB SIMM → 512 KB max; V8 window is 512 KB | EveryMac; v8.cpp:96,175 |
| Video | 512×384 or 640×480 or 640×870, 1/2/4/8 bpp (16 bpp mode exists in V8) via **Ariel** RAMDAC | v8.cpp:61-66,495-619 |
| Sound | ASC-V8 variant (FIFO A only, mono, 22 257 Hz) → **DFAC** output stage | asc.cpp:843-905; maclc.cpp:396-398 |
| Floppy | **SWIM1** + 1.44 MB SuperDrive (GCR + MFM) | maclc.cpp:435-440 |
| SCSI | NCR **53C80**, pseudo-DMA with DRQ-gated /DSACK wait states | maclc.cpp:362-372; macscsi.cpp:5-52 |
| Serial | Z**85C30** SCC, PCLK = C7M 7.8336 MHz | maclc.cpp:378-379 |
| ADB/RTC/PRAM | **Egret** MCU (68HC05EG, firmware 341S0850 on LC/LC II) | egret.cpp:42-44; maclc.cpp:418-419 |
| Expansion | 1 × LC PDS (pseudo-NuBus slot $E) | maclc.cpp:408-414 |

## Clocking

| Clock | Value | Derivation / use | Source |
|---|---|---|---|
| C32M | 31.3344 MHz | master crystal | maclc.cpp:56 |
| CPU | 15.6672 MHz | C32M/2 | maclc.cpp:57,460 |
| SCC PCLK | 7.8336 MHz | C32M/4; channel RTxC 3.6864 MHz | maclc.cpp:58,378-379 |
| VIA1 | 783.36 kHz | 15.6672/20 — **same E-clock as the Plus** | v8.cpp:111 |
| "VBL" tick | 60.15 Hz | free-running timer → VIA1 CA1 (not the real video VBL) | v8.cpp:198-199,243-247 |
| Real video VBL | 60.0 Hz (VGA) / monitor-dependent | screen vblank → pseudo-VIA slot bit `$40` | v8.cpp:106-108 |
| Dot clock 640×480 | 25.175 MHz, 800×525 total (VGA timing) | v8.cpp:106 |
| Dot clock 512×384 | 15.6672 MHz, 640×407 total (Spice values; V8 12" RGB assumed same family) | v8.cpp:717 |
| ASC sample rate | 22 257 Hz (= 15.6672 MHz/704, the Plus horizontal rate) | v8.cpp:177; asc.cpp:31 |
| Egret MCU | 32.768 kHz crystal ×128 PLL ≈ 4.19 MHz (MAME note: ADB timings run 2× spec) | egret.cpp:83; maclc.cpp:418 |

## Address map (32-bit, but only A31 + A23-A0 decoded)

**V8 decodes A23-A0 and A31 only** — MAME masks the whole map with
`0x80ffffff` (maclc.cpp:181). Consequence: the classic 32-bit Mac addresses
fold onto the 24-bit map (ROM `$40A00000` → `$A00000`, I/O `$5xF00000` →
`$F00000`); RAM never exceeds `$9FFFFF`; PDS slot $E space uses A31.

| Range (A23-A0) | Device | Notes | Source |
|---|---|---|---|
| `$000000-$7FFFFF` | RAM (SIMM bank A first, then motherboard bank B) | layout set by pseudo-VIA config reg, see § RAM controller | v8.cpp:354-422 |
| `$800000-$9FFFFF` | RAM — **first 2 MB of motherboard RAM, always here** | fixed alias regardless of config | v8.cpp:33-35,373-374 |
| `$A00000-$AFFFFF` | ROM 512 KB, mirrored ×2 | any read clears overlay (`rom_switch_r`) | v8.cpp:87-89,225-235 |
| `$F00000-$F01FFF` | **VIA1**, regs every `$200` (A9-A12), 16-bit lanes (byte mirrored on both) | v8.cpp:91,434-460 |
| `$F04000-$F05FFF` | **SCC** 85C30, word stride: A1=ctl/data?, A2=channel per z80scc `dc_ab`; read mirrors byte on D0-7 and D8-15, write uses D8-15 | maclc.cpp:114-122,186 |
| `$F06000-$F07FFF` | SCSI **pseudo-DMA window** (DRQ-handshaked, 8/16/32-bit) | maclc.cpp:187,222-266 |
| `$F10000-$F11FFF` | SCSI **53C80 registers**, reg = A4-A6 (`$10` stride); pdma read = reg 6 @ `+$260`, pdma write = reg 0 @ `+$200` | maclc.cpp:188,206-220 |
| `$F12000-$F13FFF` | SCSI pseudo-DMA window (alias) | maclc.cpp:189 |
| `$F14000-$F15FFF` | **ASC** (V8 audio), byte regs, 4 KB window (`$800`-reg model, see § Sound) | v8.cpp:92 |
| `$F16000-$F17FFF` | **SWIM1**, reg = A9-A12 (`$200` stride), data on either byte lane; +5 CPU cycles per access in MAME | maclc.cpp:190,268-287 |
| `$F24000-$F25FFF` | **Ariel RAMDAC**: +0 address, +1 palette (RGB auto-inc), +2 control, +3 key color | v8.cpp:93; ariel.cpp:62-93 |
| `$F26000-$F27FFF` | **pseudo-VIA** ("VIA2"): decodes A0, A1, A4 only (regs 0,1,2,3,$10,$12,$13); port A write at `+$200`-decoded `(offset>>9)==1` | v8.cpp:94; pseudovia.cpp:15-20,329-335 |
| `$F40000-$FBFFFF` | **VRAM 512 KB** (32-bit access OK; physical path is 16-bit) | v8.cpp:96,175 |
| slot `$E` (A31 set) | LC PDS pseudo-slot; IRQ → pseudo-VIA slot bit `$20` | maclc.cpp:408-414 |

Unmapped I/O in `$F00000+` should bus-error (the ROM's address-map probe
relies on BERR to build `AddrMapFlags`; ASCTester on a real LC reports
`AddrMapFlags $0000773F`, asc.cpp:766-770).

### 24/32-bit story

- V8 has **no overlay bit in VIA1** (unlike the Plus) and no 24-bit remap
  logic beyond ignoring A24-A30.
- On the original LC (68020, no MMU) the **pseudo-VIA port B bit 3 drives an
  "HMMU enable"** that makes the 020 fold the 32-bit map into 24-bit
  (maclc.cpp:155-158; v8.cpp:349-352 `via2_pb_w` bit 3).
- On the **LC II the 68030's PMMU implements 24-bit mode**: the ROM/System 7
  build MMU tables that emulate the 24-bit map, and **the machine boots in
  24-bit mode by default** (Memory control panel switches to 32-bit).
  ⇒ **O4's MMU core is a boot prerequisite for O6**, not an optional extra.
  (MAME still wires the hmmu callback on the LC II but Musashi only honours
  it on the 020; the 030 path goes through its MMU.)

## RAM controller (V8) — the 10 MB story

Config written by the ROM to pseudo-VIA reg 1 (`via2_config_w` →
`ram_size`), read back as `config | 0x04` (v8.cpp:328-337). Bits
(v8.cpp:375-382):

| Bits | Meaning |
|---|---|
| 5 | motherboard (bank B) size: 0 = 4 MB, 1 = 2 MB |
| 7-6 | SIMM (bank A) size: 0 = none, 1 = 2 MB, 2 = 4 MB, 3 = 8 MB |

Rules (v8.cpp:354-422, MAME comment v8.cpp:33-35):

- **SIMM bank always maps at `$000000`**; motherboard RAM maps *after* it.
- First 2 MB of motherboard RAM **always aliased at `$800000-$9FFFFF`**.
- SIMM = 8 MB (2×4 MB) ⇒ SIMMs fill `$000000-$7FFFFF`; the only motherboard
  RAM visible is the 2 MB alias at `$800000` ⇒ **12 MB installed, 10 MB
  usable, 2 MB of the soldered 4 MB wasted** (v8.cpp:363-369,417-420).
- LC II: `baseIs4M = true`, RAM options 4/6/8/10 MB (maclc.cpp:464-466).
- On overlay release the ROM-side default is `ram_size(0xc0)` = "8 MB SIMM +
  full motherboard" until the ROM probes and writes the real config
  (v8.cpp:225-235). The ROM sizes memory by writing configs and testing.

POM68K note: contiguity of RAM at 0 is config-dependent — model `ram_size`
exactly; System 7 uses `MemTop` from the ROM's sizing.

## Reset & boot flow

1. **Egret holds the 68030 in reset/halt** at power-on; V8 asserts HALT at
   machine reset until Egret's port C bit 3 releases it
   (v8.cpp:204-205; egret.cpp:237-272; maclc.cpp:149-153). Egret loads PRAM
   + RTC into its internal RAM before the falling edge (egret.cpp:246-267).
2. Overlay on: **ROM mirrored from `$000000`** (mirror period = ROM size,
   v8.cpp:207-217). CPU fetches initial SP/PC from ROM at 0.
3. First read anywhere in `$A00000-$AFFFFF` (via `rom_switch_r`) clears the
   overlay and installs RAM per current config (v8.cpp:225-235). No VIA bit
   involved — **address-triggered, like SE and later**.
4. Early ROM: probes V8 (pseudo-VIA config/video regs), sizes RAM (writes
   config reg permutations), syncs with Egret (gets PRAM/RTC, boot beep via
   ASC + DFAC), probes SCSI/SWIM for boot volume.
5. ROM verified in-repo: `docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM`
   — 512 KB (`$80000`), stored checksum `$35C28F5F` = computed big-endian
   word sum of bytes 4…end, ROM version `$067C`, header
   `35C28F5F 0000002A 067C…`; SHA-1
   `d5786182b62a8ffeeb9fd3f80b5511dba70318a0`. MAME's four byte-lane dumps
   (341-0473…0476, maclc.cpp:599-605) interleave to the same image.

## VIA1 (inside V8)

- `$F00000`, reg = A9-A12, `$200` stride (same as every Mac; Linux
  `via-cuda.c:38-55`), 783.36 kHz, R65NC22 model (v8.cpp:111).
- Access sync: CPU is stalled to the VIA E-clock on every access
  (`via_sync`, v8.cpp:462-483) — same contention idea as the Plus M4 model,
  ~10-20 CPU cycles per access.
- **CA1** = 60.15 Hz tick timer (v8.cpp:243-247). **CB1** = Egret byte clock
  in, **CB2** = Egret bidirectional data (shift register, external clock
  mode) (maclc.cpp:425-426,433).

| Port bit | Dir | Function | Source |
|---|---|---|---|
| PA (in) | — | reads `$D4 | diag` on V8/LC-family (ID bits) | v8.cpp:249-252 |
| PA5 (out) | O | floppy **HDSEL** (head select) | v8.cpp:264-267 |
| PB3 (in) | I | Egret **XCVR_SESSION** (active low) | v8.cpp:254-257; via-cuda.c:57-72 |
| PB4 (out) | O | Egret **VIA_FULL** (byte ack, active high) | v8.cpp:269-273 |
| PB5 (out) | O | Egret **SYS_SESSION** (host session, active high) | v8.cpp:269-273 |

No RTC bits, no overlay bit, no sound bits — all moved to Egret/ASC/V8.

## Pseudo-VIA ("VIA2", inside V8)

2-port GPIO + interrupt controller with a 6522-ish layout; **no timers, no
shift register, no DDRs** (pseudovia.cpp:9-11). V8 flavour decodes A0, A1,
A4; register addresses are `$F26000 + reg*$200`-style like a VIA but only
regs 0-3 and $10-$13 exist; a write with `(offset>>9)==1` hits "port A"
(pseudovia.cpp:329-335). IER/IFR bit 7 reads back 0 (pseudovia.cpp:20,241-247).

| Reg | Name | Semantics | Source |
|---|---|---|---|
| 0 | Port B | in: PB3 state etc.; out: **bit 3 = HMMU enable** (LC) | pseudovia.cpp:225-228,255-257; v8.cpp:349-352 |
| 1 | RAM config | see § RAM controller; reads `config | 0x04` | pseudovia.cpp:230-233; v8.cpp:328-337 |
| 2 | Slot IFR (active-low latches) | bit 6 = internal video VBL, bit 5 = PDS slot $E, bit 4 = slot $C (unused on LC II); write 1 to bit 6 to arm/ack VBL | pseudovia.cpp:99-134,263-266; v8.cpp:106-108,323-326; maclc.cpp:412-414 |
| 3 | IFR | bit 7 = any, bit 4 = ASC, bit 3 = SCSI IRQ, bit 1 = any-slot, bit 0 = SCSI DRQ; write-1-to-clear except ASC (level) | pseudovia.cpp:136-174,190-218,353-357 |
| $10 | video config | write: bits 0-2 = pixel depth (0=1bpp…4=16bpp); read: bits 3-6 = monitor sense (montype) | pseudovia.cpp:235-238,273-276; v8.cpp:339-347,519 |
| $12 | slot IER | bit-7-selector write (1 bits set/clear); mask over reg 2 bits 3-6 | pseudovia.cpp:193-194,278-288 |
| $13 | IER | bit-7-selector write; enabled mask `& $1B` | pseudovia.cpp:205,290-305,376-386 |

Reset values: reg 2 = `$7F`, reg 3 = `$1B` (pseudovia.cpp:93-97).
V8 quirk vs base RBV: ASC IRQ is **level-triggered** — writing 1 to IFR bit
4 is a NOP; it clears only when the FIFO refills past half (pseudovia.cpp:
309-327,353-357). Base-RBV's `IER write $FF → $1F` quirk is IIci-only, not
V8 (pseudovia.cpp:290-299 vs 376-386).

## Interrupts

Priority resolver in V8 (v8.cpp:287-315), autovectored:

| IPL | Source | Cascade |
|---|---|---|
| 1 | **VIA1** IRQ | CA1 = 60.15 Hz tick; SR = Egret byte complete; CB1 edges |
| 2 | **pseudo-VIA** IRQ | ASC (bit 4, level), SCSI IRQ (bit 3), SCSI DRQ (bit 0), slots: video VBL ($40), PDS $E ($20) via any-slot bit 1 |
| 4 | **SCC** | wired straight to IPL, *not* through a VIA (maclc.cpp:380) |
| 7 | NMI (programmer's switch) | — |

Highest pending wins; V8 clears/reasserts on each change (v8.cpp:287-315).
The 60.15 Hz "VBL" heartbeat and the one-second interrupt both live on VIA1
(one-second comes from Egret's timer packets in System 7, see § Egret).

## Video (V8 + Ariel)

- **VRAM window `$F40000-$FBFFFF`**, 512 KB, framebuffer starts at +0.
- **Row pitch is fixed at 1024 bytes** for 1/2/4/8 bpp regardless of width;
  16 bpp is packed at `hres*2` (v8.cpp:530,554,575,594,610).
- Depth = pseudo-VIA reg $10 bits 0-2: 0=1bpp, 1=2, 2=4, 3=8, 4=16bpp
  (v8.cpp:519-616). Pixels MSB-first within a byte; palette index padded
  with low 1s (e.g. 1bpp uses pens `$7F`/`$FF`) (v8.cpp:532-539).
- Monitor sense (reg $10 read, bits 3-6): 1 = 640×870 portrait, 2 = 512×384
  12" RGB, 6 = 640×480 13" RGB (v8.cpp:61-66,495-515).
- Real-machine mode limits (EveryMac; GttMFH LC ch.): 256 KB VRAM →
  512×384@8bpp or 640×480@4bpp; 512 KB → 512×384@16bpp or 640×480@8bpp;
  640×870@1bpp (4bpp w/512 KB).
- **Ariel** RAMDAC (343S1045/343S1069): Brooktree-style; regs at `$F24000`:
  +0 address (resets RGB phase), +1 palette data (R,G,B auto-increment),
  +2 control (bits 0-2 depth, bit 3 master/slave), +3 key color
  (ariel.cpp:3-27,62-93). 256 entries.
- VBL: screen vblank sets pseudo-VIA slot bit `$40` (v8.cpp:108).

## Sound (ASC-V8 + DFAC)

- ASC registers at `$F14000`, classic ASC layout (`asc.cpp:23-46`): FIFO A =
  `+$000-$3FF`, FIFO B = `+$400-$7FF`, regs at `+$800`.
- **V8 variant** (asc.cpp:843-905): version reg = `$E8`; mode forced 1 (FIFO),
  **mono, FIFO A only** (writes to FIFO B ignored); wavetable/clock/control
  regs read as constants; **1 KB FIFO**, sample rate fixed **22 257 Hz**.
- FIFO status `+$804`: bit 0 = A half-empty (< $200 bytes, asserts IRQ,
  **level-triggered**), bit 1 = A empty/full. Reading `$804` clears the IRQ
  only if not still half-empty (asc.cpp:802-841,858-866).
- IRQ → pseudo-VIA IFR bit 4 (v8.cpp:119-122).
- **DFAC** (Digitally Filtered Audio Chip) sits after the ASC DAC: volume/
  filter/mic input stage, programmed by **Egret over I2C** (bit-bang SCL/SDA
  + latch, maclc.cpp:421-423; dfac.cpp). For emulation: pass-through at unit
  gain is enough to boot; the boot beep plays through ASC FIFO A.
- No PWM/alternate sound buffer: the Plus's sound-buffer scanout is gone.

## SCSI (NCR 53C80 + pseudo-DMA)

- Registers `$F10000+`, stride $10, reg = A4-A6 (maclc.cpp:206-220).
- Pseudo-DMA: read reg 6 at `+$260`, write reg 0 at `+$200`, or the wide
  handshaked window at `$F06000`/`$F12000` accepting 1/2/4-byte accesses
  (maclc.cpp:222-266). The SCSI Manager's *blind* transfers use `MOVE.L`
  loops; hardware gates **DRQ onto /DSACK** to insert wait states, and a
  hung DRQ ends in a **bus error** (~16 µs timeout in MAME) that the SCSI
  Manager catches (macscsi.cpp:5-52; maclc.cpp:129-132,372).
- 5380 IRQ → pseudo-VIA IFR bit 3, DRQ → bit 0 (pseudovia.cpp:148-174) —
  but the LC II ROM/System mostly polls; MAME doesn't even wire the 5380 IRQ
  on maclc and boots fine.
- **Plus-phase `Ncr5380.cpp`/`ScsiDisk.cpp` are reusable**; new work = the
  32-bit DRQ window + BERR timeout path (POM68K needs bus-error plumbing).

**O6 boot findings (POM68K, from tracing the real ROM):**
- The blind-transfer loop ($A08D5A) reads the **5380 IRQ latch** (BSR
  bit 4 `$50`) to detect end-of-transfer: the latch sets when the target
  changes phase during a `MODE_DMA` transfer (data→status→message→bus
  free) and clears on a read of the Reset-Parity/Interrupt register.
  Modeled in `Ncr5380` (enterStatus/MsgIn/BusFree set `irq_`, RPI clears
  it); the DRQ line itself stays permissive (any REQ under DMA) so the
  Plus ROM path is unchanged.
- **The boot scan only accepts a driver whose DDM `ddType` is `$6A`**
  ($A07264 compares the driver-descriptor entry type against `#$6A` and
  keeps rescanning on mismatch). A bare-HFS image (`LK` at block 0) or a
  disk whose only driver entry is type `$0001` blinks the ? forever.
  `tools/wrap_hfs.py` builds a bootable image: Apple DDM + partition map
  + `Apple_Driver43` partition (template from an existing disk) + a
  driver entry with `ddType = $6A`, then the HFS volume.
- The stock LC II has no FPU, but much LC II-era software (and the test
  disk) issues 68882 instructions; without an FPU they F-line-fault to
  **system error 10**. POM68K attaches the O5 68882 by default
  (`Cpu030(mem, withFpu=true)`); `POM68K_NOFPU` models a bare machine.
  (Open: a real no-FPU LC II routes F-line through SANE — verify why
  that path doesn't catch it here.)

## Floppy (SWIM1 + SuperDrive)

- SWIM1 = IWM superset: comes up in **IWM-compatible GCR mode**, switches to
  ISM mode for MFM (1.44 MB) (swim1.cpp; devices at maclc.cpp:435-440).
- Regs at `$F16000`, stride `$200` like the IWM; MAME charges 5 extra CPU
  cycles per access (maclc.cpp:268-287).
- HDSEL (side select) comes from **VIA1 PA5** (maclc.cpp:309-319; v8.cpp:264).
- Plus-phase `Iwm.cpp`/`SonyDrive.cpp` cover the 800K GCR path; **new**:
  ISM register file + MFM cell engine, or stub MFM and boot from 800K
  images/SCSI first.

## SCC (Z85C30)

- `$F04000`, PCLK 7.8336 MHz, RTxC 3.6864 MHz both channels; single
  read/write region (no Plus-style read-even/write-odd split); word access,
  byte mirrored on both lanes on read (maclc.cpp:114-122,186,378-392).
- IRQ direct to IPL4 (maclc.cpp:380). Mouse is **ADB now** — no DCD hack.
- POM68K `Scc8530.cpp` (POMIIGS reuse) fits; only the mapping shim is new.

## Egret (ADB + RTC + PRAM + power)

68HC05EG, 32.768 kHz×128; on the real board it is *always powered*.
MAME runs the dumped firmware (341S0850 for LC/LC II, egret.cpp:42-44,74-75);
POM68K should **HLE** it (one concern: `Egret.cpp`).

**Transport** — VIA1 shift register in external-clock mode; Egret clocks
CB1 and drives/reads CB2 (maclc.cpp:425-426,433; via-cuda.c:80-82):

| VIA1 pin | Egret name | Dir (host view) | Cuda equivalent |
|---|---|---|---|
| PB3 | XCVR_SESSION (active low) | in | TREQ |
| PB4 | VIA_FULL (active high) | out | TACK/byte-ack |
| PB5 | SYS_SESSION (active high) | out | TIP |
| CB1 | byte clock | in | same |
| CB2 | serial data | bidir | same |

Handshake (via-cuda.c:57-95, delays ≈ 300-450 µs; MAME ADB bit times
egret.cpp:83 comment): host raises SYS_SESSION to start a command, feeds
bytes through the SR, pulses VIA_FULL per byte; Egret raises XCVR_SESSION
and clocks response bytes back, SR IRQ per byte; session ends when both
sessions drop. Packet format is Cuda-compatible: `[type, cmd, data…]` with
type 0 = ADB, 1 = pseudo (RTC/PRAM/power), 2 = error, 3 = timer. Pseudo
commands (Linux `cuda.h`): GET_TIME=3, SET_TIME=9, GET_PRAM=7, SET_PRAM=$C,
AUTOPOLL=1, POWERDOWN=$A, RESET_SYSTEM=$11, SEND_DFAC=$E…

**Minimum for System 7.1 boot**: release 68030 reset at power-on; answer
GET_TIME/SET_TIME (RTC seconds, Mac epoch); 256-byte PRAM GET/SET; ADB
autopoll with HLE keyboard (reg 0) + mouse (reg 0) talk data and SRQ; DFAC
writes may be swallowed. MAME's `macadb.cpp` (bit-serial HLE keyboard+mouse)
is the behavioural reference; POM68K's `MacInput` event plumbing is reusable
behind a new `AdbBus.cpp`.

**XPRAM contents the ROM/System care about** (Basilisk II study,
`BASILISK_ROM_NOTES.md` §5/§7.5): `'NuMc'` validity signature at $0C-$0F
(without it the boot takes PRAM-init detours), $01 = InternalWaitFlags
(DynWait $80 = don't stall on SCSI spin-up), $76-$77 = OSDefault,
$78-$7B = boot volume/driver, **$8A = startup 24/32-bit mode byte** (read
via the Egret-specific `ReadXPram` $02 into low-mem $1EFC — the O6 boot
blocker, see CHANGELOG 2026-07-15). `Egret::factoryDefaults()` seeds
Basilisk's known-good defaults when no battery file carries the signature
($8A deliberately left 0: the real 24-bit startup path works on V8).

## Known pitfalls (MAME comments = gold)

- **CPU must start halted** until Egret releases it (v8.cpp:204-205) — a
  core that starts executing at reset will run into an unmapped world.
- **ASC IRQ is level-triggered on V8** (ack-by-write is a NOP); edge
  modelling makes System 7 sound hang (pseudovia.cpp:309-327,353-357).
- **RAM remap on config write**: System 7's 24↔32-bit switch re-runs sizing;
  the `$800000` 2 MB alias must stay put in every config (v8.cpp:354-422).
- 68030 + 24-bit default ⇒ **MMU tables live before the Finder**; fuzzed O4
  MMU is on the boot path.
- Classic II ROM (same family) has a genuine table-overrun bug patched in
  MAME (maclc.cpp:614-631) — LC II ROM `$35C28F5F` is not affected, but it
  shows this ROM generation exercises odd 68030 corner cases.
- VIA access needs E-clock sync (v8.cpp:462-483) and SWIM accesses eat ~5
  CPU cycles (maclc.cpp:268-287) — "functional timing" should keep these.
- SCSI blind transfers **require bus-error timeout support** (macscsi.cpp).
- 16-bit V8 data path: don't promise cycle accuracy; LC II is the
  functional-accuracy machine by design (CLAUDE.md).

## Subsystem → POM68K implementation map (O6 slices)

| Subsystem | File (one concern each) | Reuse from Plus? | Suggested gate test |
|---|---|---|---|
| 68030 CPU + MMU | `extern/moira` (O4) | Moira wrapper `Cpu68k` pattern | `sst68030` (exists) + 24-bit-map MMU boot etalon |
| V8 memory ctrl + overlay | `V8Memory.cpp/.h` | `MacMemory` shape only | `v8_ramsize`: all config values vs pinned table; overlay clear on `$A00000` read |
| ROM loader | extend `main.cpp` ROM path | yes | checksum `$35C28F5F` accepted, byte-lane variant interleaved |
| VIA1 | `Via6522` (existing) + `V8Via1` glue | **yes** (R65NC22 = same core) | CA1 60.15 Hz tick count over N frames |
| Pseudo-VIA | `PseudoVia.cpp/.h` | no (new, small) | IFR/IER/slot semantics vs table above |
| Interrupt resolver | inside `V8Memory` or `V8Irq.h` | Plus `field_interrupts` pattern | scc>via2>via1 priority unit test |
| Video + Ariel | `V8Video.cpp/.h`, `Ariel.h` | `MacVideo` whole-frame decode pattern | screenshot gate: boot screen 512×384@1bpp then 8bpp gray ramp |
| ASC + DFAC | `Asc.cpp/.h` (V8 variant flag) | `MacAudioHost` | boot-beep FIFO drain rate = 22 257 Hz; half-empty IRQ level test |
| SWIM1 + SuperDrive | `Swim.cpp/.h` + `SonyDrive` ext | `Iwm`/`SonyDrive` (GCR path) | 800K GCR boot; MFM deferred |
| SCSI | `Ncr5380` + new `ScsiPdma.cpp` | **yes** (5380, `ScsiDisk`) | blind-read window test incl. BERR timeout |
| SCC | `Scc8530` (existing) | **yes** | map shim smoke |
| Egret HLE | `Egret.cpp/.h` | `Rtc` seconds/PRAM logic ideas | GET_TIME/PRAM round-trip; reset-release sequencing |
| ADB devices | `AdbBus.cpp/.h` + `MacInput` ext | `MacInput` events | keyboard/mouse talk-reg0 + SRQ under autopoll |
| LC PDS | none (stub) | — | absent slot reads bus-error |

## System 7.1: minimal device set vs stubs

**Must work to reach the Finder**: 68030+MMU (24-bit map), V8 RAM
controller + overlay, ROM, VIA1 + 60.15 Hz tick, pseudo-VIA IRQ core,
video 512×384@1-8bpp + Ariel palette, Egret (reset release, RTC, PRAM, ADB
keyboard/mouse), SCSI (boot volume) *or* SWIM GCR floppy, ASC enough to
swallow the boot beep (audio out optional), bus-error generation for
unmapped space and SCSI timeout.

**Can stub**: SCC (return sane status, no data), DFAC (ignore writes), MFM
floppy mode, PDS slot (bus error), 16 bpp video, sound input, Egret power
management beyond reset, slot $C interrupts.

## Open questions (need the real ROM / hardware traces)

1. Exact early-boot probe order and which V8 registers gate the diagnostic
   path (VIA1 PA reads `$D4` + diag bit, v8.cpp:249-252) — trace the ROM.
2. SCC register decode parity (A1/A2 roles) — confirm against ROM serial
   driver rather than MAME's `dc_ab` convention.
3. Pseudo-VIA port-A write decode (`(offset>>9)==1`) — which address the ROM
   actually uses; confirm from ROM disassembly.
4. Real BERR timeout constant for the SCSI DRQ window on LC II (MAME uses
   16 µs, "system-dependent" per macscsi.cpp:19-23).
5. 512×384 dot-clock/porch numbers for V8 proper (pinned from Spice,
   v8.cpp:717 — verify against GttMFH video chapter or hardware).
6. Whether any LC II software depends on pseudo-VIA slot bit $10 (slot $C)
   or the 16 bpp mode with only 512 KB VRAM (likely no — 512×384@16 needs
   384 KB, fits; monitor sense interaction unclear).
7. Egret firmware quirks vs HLE: SRQ timing tolerated by System 7.1's ADB
   manager (MAME note: real Egret runs ADB at 2× spec timings,
   egret.cpp:83).
