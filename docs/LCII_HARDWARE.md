# LCII_HARDWARE.md — Macintosh LC II machine blueprint (V8 gate array)

**What this file is.** The pinned hardware reference the LC II implementation
was built from, kept as an **index**: each section states the hardware fact,
cites the MAME line that pins it, and names the **class that implements it**.
The code is the detailed documentation — this file gets you to the right file
fast, and flags where POM68K deliberately differs.

- Where the implementation is a *simplification*, the section says so and
  points at **`docs/LLE_VS_HLE.md`** (the deviation inventory — live gaps
  first). Deviations are collected in [§ Deviations](#deviations--what-the-code-does-differently).
- The machine as-built (variants, boot contract, gates) lives in
  **`DEV.md` § 2.3 "V8 gate array"** — not repeated here.
- The Sonora/AIO family that inherited this design: `DEV.md` § 2.5 and, for a
  worked from-scratch bring-up, **`docs/LC520_BRINGUP.md`**.

> **Cite `§ Section name`, never a line number.** Most source comments already
> do (`V8Memory.h:14`, `V8Video.h:11`, `PseudoVia.h:26`, `Ariel.h:10`,
> `Asc.h:14`, `Egret.h:38`). Two cites are still by line and are wrong after
> any edit here — resolve them by section, not by counting:
> `V8Memory.cpp:242` says "LCII_HARDWARE.md:44" (the C7M/VIA clock — **§
> Clocking**) and `V8Memory.cpp:527` says "LCII_HARDWARE.md:78" (the
> `AddrMapFlags $773F` BERR note — **§ Address map**).

Mined 2026-07-15 in source-of-truth rank order:

1. **MAME master** (`src/mame/apple/maclc.cpp`, `v8.cpp`, `egret.cpp`,
   `macscsi.cpp`, `dfac.cpp`, `macadb.cpp`; `src/devices/machine/pseudovia.cpp`,
   `swim1.cpp`; `src/devices/sound/asc.cpp`; `src/devices/video/ariel.cpp`).
   Line numbers below are the master copies fetched 2026-07-15.
2. **Guide to the Macintosh Family Hardware 2e** (GttMFH) — the LC chapter;
   LC II ≈ LC with a 68030 (Apple published no standalone LC II Dev Note;
   the "Macintosh LC II" section rides the LC one).
3. Cross-checks: EveryMac LC II spec sheet, Linux `via-cuda.c` (Egret
   handshake), community ROM lists, and the **real LC II ROM**, verified with
   `tools/rominfo` (`docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM` —
   user-provided; `docs/*ROMs/` is gitignored, nothing here is committed).

On conflict the oracle (MAME behaviour) wins, per `CLAUDE.md`.

## Where the code is

| This doc's § | Implemented in | Gate |
|---|---|---|
| Address map, RAM controller, Reset & boot flow, Interrupts | `src/V8Memory.h/.cpp` | `v8_ramsize` |
| CPU (68030 + PMMU + 68882) | `src/Cpu030.h/.cpp`, `extern/moira` | `sst68030`, `v8_ramsize` (BERR) |
| VIA1 | `src/Via6522.*` inside `V8Memory` (`viaAccess8`, `viaSync`) | boot etalons |
| Pseudo-VIA | `src/PseudoVia.h/.cpp` (`Flavour::Level`) | `pseudovia_test` |
| Video + Ariel | `src/V8Video.h`, `src/Ariel.h`, `src/VideoBeam.h` | `v8_video_test`, `v8_raster_test` |
| Sound | `src/Asc.h/.cpp` (`AscV8`; `AscSonora` for Spice/Tinker Bell) | `asc_test` |
| SCSI | `src/Ncr5380.*`, `src/ScsiDisk.*` + `V8Memory::scsiDma_/scsiDmaW_` | `scsi_pdma_test` |
| Floppy | `src/Swim1.*` (+ `src/Iwm.*`, `src/SonyDrive.*`) | `swim1_test`, `lcii_floppy_etalon` |
| SCC | `src/Scc8530.*` | `scc_baud_test` |
| Egret | `src/CudaLle.*` + `src/M68hc05.*` (firmware LLE, default); `src/Egret.*` (HLE fallback) | `egret_lle_test`, `egret_test` |
| ADB devices | `src/AdbBus.*`, `src/AdbLine.*`, `src/MacInput.*` | `lcii_boot_etalon` (mouse) |
| Save states | `visit<Ar>()` in each class above, `src/SaveState.*` | `savestate_v8_test`, `lcii_savestate_etalon` |
| Whole machine | `lcii_boot_etalon`, `lcii_sys7_boot_etalon`, `lcii_soak/persist/launch/floppy_etalon`, `jit_lcii_boot_etalon` (second execution engine) | |

`V8Memory::Model {LcII, Lc, ClassicII, ColorClassic, MacTv}` makes this one
class the whole V8/Eagle/Spice/Tinker Bell family. The **LC II is the
reference profile documented here**; the per-variant deltas are in
`DEV.md § 2.3`.

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

Constants as built: `V8Memory::kRomSize` `$80000`, `kVramSize` `$80000`,
`kMbRamSize` `$400000`, `kCpuHz` 15 667 200, `kViaHz` 783 360
(`V8Memory.h:42-47`).

## Clocking

| Clock | Value | Derivation / use | Source |
|---|---|---|---|
| C32M | 31.3344 MHz | master crystal | maclc.cpp:56 |
| CPU | 15.6672 MHz | C32M/2 (`V8Memory::kCpuHz`) | maclc.cpp:57,460 |
| SCC PCLK | 7.8336 MHz | C32M/4; channel RTxC 3.6864 MHz | maclc.cpp:58,378-379 |
| VIA1 | 783.36 kHz | CPU/20 = C7M/10 — **same E-clock as the Plus** (`kViaHz`, `viaDiv_`) | v8.cpp:111 |
| "VBL" tick | 60.15 Hz | free-running timer → VIA1 CA1 (not the real video VBL) | v8.cpp:198-199,243-247 |
| Real video VBL | 60.0 Hz (VGA) / monitor-dependent | screen vblank → pseudo-VIA slot bit `$40` | v8.cpp:106-108 |
| Dot clock 640×480 | 25.175 MHz | 800×525 total (VGA timing) | v8.cpp:106 |
| Dot clock 512×384 | 15.6672 MHz | 640×407 total (Spice values; V8 12" RGB assumed same family) | v8.cpp:717 |
| ASC sample rate | 22 257 Hz | = 15.6672 MHz/704, the Plus horizontal rate | v8.cpp:177; asc.cpp:31 |
| Egret MCU | ≈ 4.19 MHz | 32.768 kHz crystal ×128 PLL (MAME note: ADB timings run 2× spec) | egret.cpp:83; maclc.cpp:418 |

→ `V8Memory::reset` derives `frameCycles_`/`vblStart_` from the modeline, and
`V8Memory::tick` runs the VIA φ2 divider + the 60.15 Hz Bresenham tick.
Sibling clocks (Mac TV C32M) rescale via the `cpuHz` ctor param.

## Address map

**V8 decodes A23-A0 and A31 only** — MAME masks the whole map with
`0x80ffffff` (maclc.cpp:181), mirrored by `V8Memory::addrMask_`. Consequence:
the classic 32-bit Mac addresses fold onto the 24-bit map (ROM `$40A00000` →
`$A00000`, I/O `$5xF00000` → `$F00000`); RAM never exceeds `$9FFFFF`; PDS
slot $E space uses A31.

→ `V8Memory::read8`/`write8` (`V8Memory.cpp` from the `I/O space $F00000+`
comment down) is the decode, in this order.

| Range (A23-A0) | Device | Notes | Source |
|---|---|---|---|
| `$000000-$7FFFFF` | RAM (SIMM bank A first, then motherboard bank B) | layout set by pseudo-VIA config reg, see § RAM controller | v8.cpp:354-422 |
| `$800000-$9FFFFF` | RAM — **first 2 MB of motherboard RAM, always here** | fixed alias regardless of config (`V8Memory::ramIndex`) | v8.cpp:33-35,373-374 |
| `$A00000-$AFFFFF` | ROM 512 KB, mirrored ×2 | any read clears overlay (`rom_switch_r`) | v8.cpp:87-89,225-235 |
| `$B00000-$EFFFFF` | — | open bus, reads `$FF` | — |
| `$F00000-$F01FFF` | **VIA1** | regs every `$200` (A9-A12), 16-bit lanes (byte mirrored on both) | v8.cpp:91,434-460 |
| `$F04000-$F05FFF` | **SCC** 85C30 | **A1 = channel, A2 = ctl/data**; read mirrors byte on D0-7 and D8-15, write uses D8-15 | maclc.cpp:114-122,186 |
| `$F06000-$F07FFF` | SCSI **pseudo-DMA window** | DRQ-handshaked, 8/16/32-bit | maclc.cpp:187,222-266 |
| `$F10000-$F11FFF` | SCSI **53C80 registers** | reg = A4-A6 (`$10` stride); pdma read = reg 6 @ `+$260`, pdma write = reg 0 @ `+$200` | maclc.cpp:188,206-220 |
| `$F12000-$F13FFF` | SCSI pseudo-DMA window | alias | maclc.cpp:189 |
| `$F14000-$F15FFF` | **ASC** (V8 audio) | byte regs (`$800`-reg model, see § Sound) | v8.cpp:92 |
| `$F16000-$F17FFF` | **SWIM1** | reg = A9-A12 (`$200` stride), data on either byte lane; +5 CPU cycles per access | maclc.cpp:190,268-287 |
| `$F24000-$F25FFF` | **Ariel RAMDAC** | +0 address, +1 palette (RGB auto-inc), +2 control, +3 key color | v8.cpp:93; ariel.cpp:62-93 |
| `$F26000-$F27FFF` | **pseudo-VIA** ("VIA2") | decodes A0, A1, A4 only (regs 0,1,2,3,`$10`,`$12`,`$13`); port A write at `(offset>>9)==1` | v8.cpp:94; pseudovia.cpp:15-20,329-335 |
| `$F40000-$FBFFFF` | **VRAM 512 KB** | 32-bit access OK; physical path is 16-bit | v8.cpp:96,175 |
| slot `$E` (A31 set) | LC PDS pseudo-slot | **BERR (no card)**; IRQ would land on pseudo-VIA slot bit `$20` | maclc.cpp:408-414 |

Unmapped I/O in `$F00000+` **bus-errors** — the ROM's address-map probe relies
on BERR to build `AddrMapFlags` (ASCTester on a real LC reports
`AddrMapFlags $0000773F`, asc.cpp:766-770). `V8Memory::busError()` raises it
through `Cpu030::extBusError`. **Exception:** the Classic II's Eagle bus is
forgiving — unmapped I/O returns `$FF` instead, because that ROM dereferences
wild pointers (`$50F18038` among them) with no BERR catcher installed, so any
BERR there lands on a zero vector → DS 1. The `$FF` is a **knob, not a fact**:
MAME answers 0 there but that is its `address_space` default, not a modelled
decision, so `POM68K_V8_HOLEVAL=<hex>` picks the byte until an observable
separates them, and `POM68K_V8_IOHOLE=<n>` logs the accesses with their PC
(`V8Memory.cpp:525-554`).

### 24/32-bit story

- V8 has **no overlay bit in VIA1** (unlike the Plus) and no 24-bit remap
  logic beyond ignoring A24-A30.
- On the original LC (68020, no MMU) the **pseudo-VIA port B bit 3 drives an
  "HMMU enable"** that folds the 32-bit map into 24-bit (maclc.cpp:155-158;
  v8.cpp:349-352 `via2_pb_w` bit 3). → `V8Memory::addrMask_`, rewritten from
  `PseudoVia::onPortB`; reset value `$80FFFFFF` (HMMU disabled).
- On the **LC II the 68030's PMMU implements 24-bit mode**: the ROM/System 7
  build MMU tables that emulate the 24-bit map, and **the machine boots in
  24-bit mode by default** (Memory control panel switches to 32-bit).
  ⇒ the fuzzed MMU core is a **boot prerequisite**, not an optional extra.
  (MAME still wires the hmmu callback on the LC II but Musashi only honours
  it on the 020; the 030 path goes through its MMU.)

## RAM controller — the 10 MB story

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

→ `V8Memory::applyRamConfig` (the `simmPhys_/simmOff_/mbLoc_/mbSize_` banks)
+ `V8Memory::ramIndex` (the `$800000` alias). The pseudo-VIA can rewrite the
mapping at any time, so every config write also calls `jitMapChanged()` — a
bank remap is exactly the "address map moved" case the JIT window cannot see
from a write. Every config value is pinned by `v8_ramsize`.

## Reset & boot flow

1. **Egret holds the 68030 in reset/halt** at power-on; V8 asserts HALT at
   machine reset until Egret's port C bit 3 releases it
   (v8.cpp:204-205; egret.cpp:237-272; maclc.cpp:149-153). Egret loads PRAM
   + RTC into its internal RAM before the falling edge (egret.cpp:246-267).
   → `CudaLle` (firmware) or `Egret` (HLE) drives the release; `V8Memory::reset`
   stages the battery PRAM into the MCU before it runs.
2. Overlay on: **ROM mirrored from `$000000`** (mirror period = ROM size,
   v8.cpp:207-217). CPU fetches initial SP/PC from ROM at 0.
3. First read anywhere in `$A00000-$AFFFFF` clears the overlay and installs
   RAM per current config (v8.cpp:225-235). No VIA bit involved —
   **address-triggered, like SE and later**. → `V8Memory::read8` ROM arm.
4. Early ROM: probes V8 (pseudo-VIA config/video regs), sizes RAM (writes
   config reg permutations), syncs with Egret (gets PRAM/RTC, boot beep via
   ASC + DFAC), probes SCSI/SWIM for boot volume.
5. ROM verified (user-provided copy, `tools/rominfo`)
   — 512 KB (`$80000`), stored checksum `$35C28F5F` = computed big-endian
   word sum of bytes 4…end, ROM version `$067C`, header
   `35C28F5F 0000002A 067C…`; SHA-1
   `d5786182b62a8ffeeb9fd3f80b5511dba70318a0`. MAME's four byte-lane dumps
   (341-0473…0476, maclc.cpp:599-605) interleave to the same image.

**Minimum device set to reach the Finder** (validated by the gates, not by
theory): 68030 + PMMU (24-bit map), V8 RAM controller + overlay, ROM, VIA1 +
the 60.15 Hz tick, pseudo-VIA IRQ core, video + Ariel palette, the MCU (reset
release, RTC, PRAM, ADB keyboard/mouse), SCSI *or* SWIM, an ASC that swallows
the boot beep, and BERR generation for unmapped space and the SCSI DRQ window.

## VIA1 (inside V8)

- `$F00000`, reg = A9-A12, `$200` stride (same as every Mac; Linux
  `via-cuda.c:38-55`), 783.36 kHz, R65NC22 model (v8.cpp:111).
- Access sync: CPU is stalled to the VIA E-clock on every access
  (`via_sync`, v8.cpp:462-483) — same contention idea as the Plus M4 model,
  ~10-20 CPU cycles per access. → `V8Memory::viaSync`, which works in
  **machine cycles** (`Cpu030::machineClock`), never the i-cache-boosted core
  clock: aligning to a boosted phantom E-clock shrinks every VIA-paced pulse
  by `cacheBoost_` (CHANGELOG 2026-07-25).
- **CA1** = 60.15 Hz tick timer (v8.cpp:243-247). **CB1** = Egret byte clock
  in, **CB2** = Egret bidirectional data (shift register, external clock
  mode) (maclc.cpp:425-426,433).

| Port bit | Dir | Function | Source |
|---|---|---|---|
| PA (in) | — | machine ID \| diag bit: V8 `$D4` (LC/LC II) | v8.cpp:249-252 |
| PA5 (out) | O | floppy **HDSEL** (head select) | v8.cpp:264-267 |
| PB3 (in) | I | Egret **XCVR_SESSION** (active low) | v8.cpp:254-257; via-cuda.c:57-72 |
| PB4 (out) | O | Egret **VIA_FULL** (byte ack, active high) | v8.cpp:269-273 |
| PB5 (out) | O | Egret **SYS_SESSION** (host session, active high) | v8.cpp:269-273 |

No RTC bits, no overlay bit, no sound bits — all moved to Egret/ASC/V8.

**As built** (`V8Memory::reset`, the `setInA`/`setInB` block — read it, the
reasoning is there): PA is driven with the diag bit **set** (`$D5` on
LC/LC II, `$93` Eagle, `$83` Spice, `$84` Tinker Bell — Tinker Bell has no
diag OR). PB idles `$C7 | XCVR_SESSION<<3`: PB0-2 (legacy RTC lines) and
PB6-7 keep the 6522 pull-up 1s, but **PB4/PB5 must idle LOW** — they are
host-driven and the HLE transport is edge-triggered, so pull-ups there read
as a phantom session rise and wedge the boot.

## Pseudo-VIA ("VIA2", inside V8)

2-port GPIO + interrupt controller with a 6522-ish layout; **no timers, no
shift register, no DDRs** (pseudovia.cpp:9-11). V8 flavour decodes A0, A1,
A4; register addresses are `$F26000 + reg*$200`-style like a VIA but only
regs 0-3 and $10-$13 exist; a write with `(offset>>9)==1` hits "port A"
(pseudovia.cpp:329-335). IER/IFR bit 7 reads back 0 (pseudovia.cpp:20,241-247).

→ `PseudoVia` (`Flavour::Level` here; `Flavour::Base` for the IIsi/IIci and
IIvx/IIvi — the header block comment explains why the split exists and why
pairing the wrong flavour with a V8-class ASC makes the interrupt
unacknowledgeable). Gate `pseudovia_test`.

| Reg | Name | Semantics | Source |
|---|---|---|---|
| 0 | Port B | in: PB3 state etc.; out: **bit 3 = HMMU enable** (LC) | pseudovia.cpp:225-228,255-257; v8.cpp:349-352 |
| 1 | RAM config | see § RAM controller; reads `config \| 0x04` | pseudovia.cpp:230-233; v8.cpp:328-337 |
| 2 | Slot IFR (active-low latches) | bit 6 = internal video VBL, bit 5 = PDS slot $E, bit 4 = slot $C (unused on LC II); write 1 to bit 6 to arm/ack VBL | pseudovia.cpp:99-134,263-266; v8.cpp:106-108,323-326; maclc.cpp:412-414 |
| 3 | IFR | bit 7 = any, bit 4 = ASC, bit 3 = SCSI IRQ, bit 1 = any-slot, bit 0 = SCSI DRQ; write-1-to-clear except ASC (level) | pseudovia.cpp:136-174,190-218,353-357 |
| $10 | video config | write: bits 0-2 = pixel depth (0=1bpp…4=16bpp); read: **bits 3-5 = monitor sense** (`montype << 3`, 3 bits) | pseudovia.cpp:235-238,273-276; v8.cpp:339-347,519 |
| $12 | slot IER | bit-7-selector write (1 bits set/clear); mask over reg 2 bits 4-6 | pseudovia.cpp:193-194,278-288 |
| $13 | IER | bit-7-selector write; enabled mask `& $1B` | pseudovia.cpp:205,290-305,376-386 |

Reset values: reg 2 = `$7F`, reg 3 = `$1B` (pseudovia.cpp:93-97).
V8 quirk vs base RBV: ASC IRQ is **level-triggered** — writing 1 to IFR bit
4 is a NOP; it clears only when the FIFO refills past half (pseudovia.cpp:
309-327,353-357). Base-RBV's `IER write $FF → $1F` quirk is IIci-only, not
V8 (pseudovia.cpp:290-299 vs 376-386).

## Interrupts

Priority resolver in V8 (v8.cpp:287-315), autovectored. → `V8Memory::iplLevel`
(three lines; `updateIrq` pushes it to `Cpu030::updateIpl`).

| IPL | Source | Cascade |
|---|---|---|
| 1 | **VIA1** IRQ | CA1 = 60.15 Hz tick; SR = Egret byte complete; CB1 edges |
| 2 | **pseudo-VIA** IRQ | ASC (bit 4, level), SCSI IRQ (bit 3), SCSI DRQ (bit 0), slots: video VBL ($40), PDS $E ($20) via any-slot bit 1 |
| 4 | **SCC** | wired straight to IPL, *not* through a VIA (maclc.cpp:380) |
| 7 | NMI (programmer's switch) | — |

Highest pending wins; V8 clears/reasserts on each change (v8.cpp:287-315).
The 60.15 Hz "VBL" heartbeat and the one-second interrupt both live on VIA1
(one-second comes from the MCU's timer packets in System 7, see § Egret).

## Video (V8 + Ariel)

→ `V8Video.h` (one `switch` over the depth, driven either whole-frame by
`decode()` or row-by-row by `raster()` off `VideoBeam`) + `Ariel.h` (the
256-entry CLUT). Gates `v8_video_test`, `v8_raster_test`.

- **VRAM window `$F40000-$FBFFFF`**, 512 KB, framebuffer starts at +0.
- **Row pitch is fixed at 1024 bytes** for 1/2/4/8 bpp regardless of width;
  16 bpp is packed at `hres*2` (v8.cpp:530,554,575,594,610).
- Depth = pseudo-VIA reg $10 bits 0-2: 0=1bpp, 1=2, 2=4, 3=8, 4=16bpp
  (v8.cpp:519-616). Pixels MSB-first within a byte; palette index padded
  with low 1s (e.g. 1bpp uses pens `$7F`/`$FF`) (v8.cpp:532-539).
- Monitor sense (reg $10 read, bits 3-5): 1 = 640×870 portrait, 2 = 512×384
  12" RGB, 6 = 640×480 13" RGB (v8.cpp:61-66,495-515). `V8Memory`'s own
  default is sense **2** (what the tests get); the GUI path forces sense **6**
  unless `POM68K_MONITOR=512`, and offers only those two (`main.cpp` — 640×870
  needs a wider framebuffer than the V8 provides).
- Real-machine mode limits (EveryMac; GttMFH LC ch.): 256 KB VRAM →
  512×384@8bpp or 640×480@4bpp; 512 KB → 512×384@16bpp or 640×480@8bpp;
  640×870@1bpp (4bpp w/512 KB).
- **Ariel** RAMDAC (343S1045/343S1069): Brooktree-style; regs at `$F24000`:
  +0 address (resets RGB phase), +1 palette data (R,G,B auto-increment),
  +2 control (bits 0-2 depth, bit 3 master/slave), +3 key color
  (ariel.cpp:3-27,62-93). 256 entries.
- VBL: screen vblank sets pseudo-VIA slot bit `$40` (v8.cpp:108).
- The Classic II's Eagle bypasses all of this: fixed 512×342 1 bpp scanned
  out of **main RAM** at device offset `$1F9A80`, 64-byte pitch, no Ariel
  (`V8Memory::eagleFrame`, v8.cpp:667-691).

## Sound (ASC-V8 + DFAC)

→ `Asc.h/.cpp` — `AscV8` for the V8/Eagle, `AscSonora` (the EASC at `$BC`) for
Spice/Tinker Bell. Gate `asc_test`.

- ASC registers at `$F14000`, classic ASC layout (`asc.cpp:23-46`): FIFO A =
  `+$000-$3FF`, FIFO B = `+$400-$7FF`, regs at `+$800`.
- **V8 variant** (asc.cpp:843-905): version reg = `$E8`; mode forced 1 (FIFO),
  **mono, FIFO A only** (writes to FIFO B ignored); wavetable/clock/control
  regs read as constants; **1 KB FIFO**, sample rate fixed **22 257 Hz**.
- FIFO status `+$804`: bit 0 = A half-empty (< $200 bytes, asserts IRQ,
  **level-triggered**), bit 1 = A empty/full. Reading `$804` clears the IRQ
  only if not still half-empty (asc.cpp:802-841,858-866).
  **The Sonora/Spice EASC differs**: its `$804` read must clear the IRQ
  *unconditionally* — MAME's `HALF_B` gate freezes the CC / LC III boot at
  "Bienvenue." inside the autovector (`LLE_VS_HLE.md` § 1.7).
- IRQ → pseudo-VIA IFR bit 4 (v8.cpp:119-122).
- **DFAC** (Digitally Filtered Audio Chip) sits after the ASC DAC: volume/
  filter/mic input stage, programmed by the MCU **over I2C** (bit-bang
  SCL/SDA + latch, maclc.cpp:421-423; dfac.cpp). Not modelled as an audio
  stage — but the **I2C ACK is not optional** on the Cuda-flavour siblings
  (`CudaLle::setI2cDfac`): its absence was the long-standing Color Classic
  "0417 wedge". `TODO.md § LC II / V8` still lists DFAC/sound-out polish.
- No PWM/alternate sound buffer: the Plus's sound-buffer scanout is gone.

## SCSI (NCR 53C80 + pseudo-DMA)

→ `Ncr5380.*` + `ScsiDisk.*` (both reused from the Plus) with the window
decode and DRQ gate in `V8Memory::scsiDma_`/`scsiDmaW_`. Gate `scsi_pdma_test`.

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

**Boot findings from tracing the real ROM** (the "why" behind two odd bits of
code):

- The blind-transfer loop (`$A08D5A`) reads the **5380 IRQ latch** (BSR
  bit 4 `$50`) to detect end-of-transfer: the latch sets when the target
  changes phase during a `MODE_DMA` transfer (data→status→message→bus
  free) and clears on a read of the Reset-Parity/Interrupt register.
  Modeled in `Ncr5380` (`enterStatus`/MsgIn/BusFree set `irq_`, `R_RPI`
  clears it); the DRQ line itself stays permissive (any REQ under DMA) so
  the Plus ROM path is unchanged.
- **The boot scan only accepts a driver whose DDM `ddType` is `$6A`**
  (`$A07264` compares the driver-descriptor entry type against `#$6A` and
  keeps rescanning on mismatch). A bare-HFS image (`LK` at block 0) or a
  disk whose only driver entry is type `$0001` blinks the ? forever.
  `tools/wrap_hfs.py` builds a bootable image: Apple DDM + partition map
  + `Apple_Driver43` partition (template from an existing disk) + a
  driver entry with `ddType = $6A`, then the HFS volume.
- The stock LC II has no FPU, but much LC II-era software (and the test
  disk) issues 68882 instructions; without an FPU they F-line-fault to
  **system error 10**. `Cpu030`'s socket is **empty by default**
  (`withFpu = false`, matching maclc.cpp:325-330); `main.cpp` and every LC II
  gate pass `withFpu = true`, and `POM68K_NOFPU` models the bare machine.
  Whether the 030 path still needs the UniversalInfo/`defaultRSRCs` selection
  that fixed the 040 side is **untested** — `TODO.md § LC II / V8`.

## Floppy (SWIM1 + SuperDrive)

→ `Swim1.*` — **both personalities are implemented**: the IWM-compatible GCR
mode delegates to the proven `Iwm`, and the ISM half is the SWIM2-lineage
register file with SWIM1's 16-entry parameter RAM and MFM CRC-CCITT cell
engine (entered by the four 1-0-1-1 mode writes). Gate `swim1_test`; the
missing piece is a *guest-level* 1.44 MB mount/boot gate (`TODO.md`).

- Regs at `$F16000`, stride `$200` like the IWM; MAME charges 5 extra CPU
  cycles per access (maclc.cpp:268-287) — applied via `Cpu030::stall(5)`.
- HDSEL (side select) comes from **VIA1 PA5** (maclc.cpp:309-319; v8.cpp:264)
  — `swim_.setSel((via_.portA() & 0x20) != 0)` on every access.
- Spice/Tinker Bell swap in the gate array's integrated **SWIM2** at the same
  window (`Swim2.*`).

## SCC (Z85C30)

→ `Scc8530.*` (POMIIGS reuse); only the mapping shim lives in `V8Memory`.

- `$F04000`, PCLK 7.8336 MHz, RTxC 3.6864 MHz both channels; single
  read/write region (no Plus-style read-even/write-odd split); word access,
  byte mirrored on both lanes on read (maclc.cpp:114-122,186,378-392).
- Decode as built: **A1 = channel, A2 = data/ctl** (MAME's `dc_ab`).
- IRQ direct to IPL4 (maclc.cpp:380); ext/status interrupts carry the LAP
  manager's carrier sense. Mouse is **ADB now** — no DCD hack.
- LocalTalk / AppleTalk on this wire: `docs/APPLETALK.md`.

## Egret (ADB + RTC + PRAM + power)

68HC05EG, 32.768 kHz×128; on the real board it is *always powered*.
MAME runs the dumped firmware (`egret.cpp` ROM_START): **341S0851**
(default bios) or earlier **341S0850** for LC/LC II
(`341s0850.bin` CRC `4906ecd0` / SHA1 `95e08ba0…fd87c`,
`341s0851.bin` CRC `ea9ea6e4` / SHA1 `8b0dae3e…eb571`, 0x1100 bytes).

→ **POM68K runs the real firmware by default** (since 2026-07-23) when
`roms/egret/341s0850.bin` is present: `CudaLle` with `Flavor::Egret` on the
shared `M68hc05` 68HC05E1 core, instruction-slaved ADB wire; the falling PC3
edge releases the CPU and installs the staged PRAM (`V8Memory.cpp` firmware
search, `V8Memory::reset`). `POM68K_EGRET_LLE=0` or a missing dump falls back
to the HLE `Egret.*` — see `docs/LLE_VS_HLE.md` § 2 and § 1.9. Gates
`egret_lle_test` (firmware) and `egret_test` (HLE wire).

**Transport** — VIA1 shift register in external-clock mode; Egret clocks
CB1 and drives/reads CB2 (maclc.cpp:425-426,433; via-cuda.c:80-82):

| VIA1 pin | Egret name | Dir (host view) | Cuda equivalent |
|---|---|---|---|
| PB3 | XCVR_SESSION (active low) | in | TREQ |
| PB4 | VIA_FULL (active high) | out | TACK/byte-ack |
| PB5 | SYS_SESSION (active high) | out | TIP |
| CB1 | byte clock | in | same |
| CB2 | serial data | bidir | same |

The Cuda's TIP/BYTEACK are **active low** where the Egret's are active high;
the wire is otherwise identical, which is why one class covers both
(`Egret`'s `cudaPolarity`, `CudaLle::Flavor`).

Handshake (via-cuda.c:57-95, delays ≈ 300-450 µs; MAME ADB bit times
egret.cpp:83 comment): host raises SYS_SESSION to start a command, feeds
bytes through the SR, pulses VIA_FULL per byte; Egret raises XCVR_SESSION
and clocks response bytes back, SR IRQ per byte; session ends when both
sessions drop. Packet format is Cuda-compatible: `[type, cmd, data…]` with
type 0 = ADB, 1 = pseudo (RTC/PRAM/power), 2 = error, 3 = timer. Pseudo
commands (Linux `cuda.h`): GET_TIME=3, SET_TIME=9, GET_PRAM=7, SET_PRAM=$C,
AUTOPOLL=1, POWERDOWN=$A, RESET_SYSTEM=$11, SEND_DFAC=$E…

The **exact framing/pacing model** (attention byte as a wire event, the
61/71/88/13/30 µs Cuda schedule, `$02`/`$08` READ/WRITE_MCU_MEM with PRAM at
MCU `$0100-$01FF`, one-second modes on `$1B`) is documented where it is
implemented — `Egret.h`'s header block. Do not duplicate it here.

**XPRAM contents the ROM/System care about** (Basilisk II study,
`BASILISK_ROM_NOTES.md` §5/§7.5): `'NuMc'` validity signature at $0C-$0F
(without it the boot takes PRAM-init detours), $01 = InternalWaitFlags
(DynWait $80 = don't stall on SCSI spin-up), $13 = SPConfig (port B use —
`$22` = both ports async, which is what keeps AppleTalk inactive),
$58 = built-in video sPRAM (`$80` flag | mode index), $76-$77 = OSDefault,
$78-$7B = boot volume/driver, **$8A = startup 24/32-bit mode byte** (read
via the Egret-specific `ReadXPram` $02 into low-mem $1EFC — the original O6
boot blocker, CHANGELOG 2026-07-15). `Egret::factoryDefaults()` seeds
Basilisk's known-good defaults when no battery file carries the signature —
read that function, every byte carries its reason; **$8A is deliberately
left 0** because the real 24-bit startup path works on V8.

## Known pitfalls (MAME comments = gold)

Each of these cost real debugging time and is now encoded in the source; most
are held by a boot etalon somewhere in the family rather than by a unit gate.

- **CPU must start halted** until the MCU releases it (v8.cpp:204-205) — a
  core that starts executing at reset runs into an unmapped world.
- **ASC IRQ is level-triggered on V8** (ack-by-write is a NOP); edge
  modelling makes System 7 sound hang (pseudovia.cpp:309-327,353-357).
- **RAM remap on config write**: System 7's 24↔32-bit switch re-runs sizing;
  the `$800000` 2 MB alias must stay put in every config (v8.cpp:354-422).
- 68030 + 24-bit default ⇒ **MMU tables live before the Finder**; the fuzzed
  MMU is on the boot path.
- Classic II ROM (same family) has a genuine table-overrun bug patched in
  MAME (maclc.cpp:614-631) — LC II ROM `$35C28F5F` is not affected, but it
  shows this ROM generation exercises odd 68030 corner cases.
- VIA E-clock sync and the SWIM +5 cycles are **machine-cycle** costs, not
  core-clock costs: computing them on the i-cache-boosted clock shrinks every
  VIA-paced pulse by the boost factor (this is what wedged the IIsi and
  black-screened the LC III / IIvx).
- SCSI blind transfers **require bus-error timeout support**, so `/BERR`
  plumbing (`Cpu030::extBusError`, Moira `MmuBusError`) is a boot dependency.
- 16-bit V8 data path: don't promise cycle accuracy; the LC II is a
  **functional-accuracy** machine by design (`CLAUDE.md`).

## Deviations — what the code does differently

Full inventory and priorities in **`docs/LLE_VS_HLE.md`**; this is the
V8-specific slice.

| Deviation | Where | Why / status |
|---|---|---|
| **No per-pixel beam**, but decode *is* row-granular since 2026-08-02: `V8Video::raster()` renders each visible row once, when `VideoBeam` says the beam scans it (`decode()` stays for stills) | `V8Video.h`, `VideoBeam.h` | `LLE_VS_HLE` § 1.1; gates `v8_raster_test`, `raster_equiv_test` |
| **VBL geometry is the 12" RGB modeline for every V8 sense** (640×407 @ C15M ⇒ 60.15 Hz); only Tinker Bell switches to the 800×525 VGA timing | `V8Memory::reset` (`frameCycles_`, `vblStart_`) | No guest observed depending on the 640×480 modeline's 59.94 Hz. Not gated. |
| **VRAM is always 512 KB** (`kVramSize`), never the 256 KB base config | `V8Memory::kVramSize` | The 256 KB machine's mode limits are therefore not enforced |
| **SCSI DRQ timeout is not timed** — no DRQ raises `/BERR` immediately instead of after ~16 µs | `V8Memory::scsiDma_` | Functionally what the blind-transfer loops need; `LLE_VS_HLE` § 1.5 |
| **ASC drain is a fixed 22 257 Hz**, not derived from the programmed rate | `Asc.*` | `LLE_VS_HLE` § 1.7 |
| **DFAC is not an audio stage** — writes accepted and dropped (but the I2C ACK is modelled) | `CudaLle::setI2cDfac`, `V8Memory` Spice brightness/contrast DAC | `TODO.md § LC II / V8` |
| **SWIM1 read engine has no flux jitter** — MAME's LS-pair cell state machine reduces to the SWIM2 shifter; DAT1BYTE not wired | `Swim1.h` | Our cells are ideal; `LLE_VS_HLE` § 1.3 / § 3 |
| **68882 populated by default** although the stock LC II has none | `main.cpp`, LC II gates pass `withFpu = true` | Era software F-line-faults otherwise; `POM68K_NOFPU` = bare machine |
| **No CPU/bus contention model** beyond VIA E-clock sync + SWIM wait states; an i-cache *throughput* overlay, not a cache model | `Cpu030.h` | Functional accuracy by design; `LLE_VS_HLE` § 1.2 |
| **LC PDS slot: no card, BERR** | `V8Memory::read8` | Nothing to emulate yet |

## Open questions

Resolved since the blueprint was written (kept so nobody re-opens them):
SCC A1/A2 decode and the pseudo-VIA port-A write decode are now settled by a
booting ROM; the SCSI BERR timeout is modelled as immediate (above); the Egret
firmware runs for real, so its HLE quirks only matter on the fallback path.

Still open — these need hardware traces or a guest that exercises them:

1. Exact early-boot probe order and which V8 registers gate the diagnostic
   path (VIA1 PA `$D4` + diag bit, v8.cpp:249-252). Empirically the diag bit
   must read **1** (diagnostics off) — the same lesson the IIci re-taught.
2. Real BERR timeout constant for the SCSI DRQ window on LC II (MAME uses
   16 µs, "system-dependent" per macscsi.cpp:19-23).
3. 512×384 dot-clock/porch numbers for V8 proper (pinned from Spice,
   v8.cpp:717 — verify against GttMFH's video chapter or hardware).
4. Whether any LC II software depends on pseudo-VIA slot bit $10 (slot $C)
   or the 16 bpp mode with only 512 KB VRAM (likely no — 512×384@16 needs
   384 KB, fits; monitor sense interaction unclear).
5. Egret firmware ADB timing vs System 7.1's ADB manager (MAME note: real
   Egret runs ADB at 2× spec timings, egret.cpp:83).
6. Interrupt/VBL/VIA/memory timing vs real hardware, and the idle screen dim
   seen after very long runs (`TODO.md § LC II / V8`).
