# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K supports, and the effort each remaining one
takes, assessed against what the emulator already has. Written 2026-07-17;
updated 2026-07-20 (Quadra 605) and **2026-07-24 (Phase C fan-out — this
pass)**.

POM68K today covers **21 machine profiles** across every 68k generation, and
**every one boots to the Finder**:

- **68000, cycle-exact**: **Mac Plus** ✅
- **68020**: **Mac II** ✅ (Toby NuBus), **Mac LC** ✅ (V8/HMMU)
- **68030 + PMMU (+ 68882)**: **LC II** ✅, **Classic II** ✅ (Eagle),
  **Color Classic** ✅ (Spice), **Color Classic II** ✅, **LC III** ✅ /
  **LC III+** ✅ (Sonora), **LC 520** ✅ / **LC 550** ✅ (Sonora AIO),
  **IIvx** ✅ / **IIvi** ✅ (VASP), **Mac TV** ✅ (Tinker Bell),
  **IIsi** ✅ (RBV)
- **68040 / 68LC040 + 040 MMU**: **Quadra 605** ✅, **LC 475** ✅,
  **LC 575** ✅ (MEMCjr/PrimeTime/DAFB)

Every ADB machine now runs its MCU as **firmware LLE** (real 68HC05 —
Egret / Cuda images) by default. See `CLAUDE.md`, `TODO.md` and
`CHANGELOG.md` for subsystem status, and `docs/LLE_VS_HLE.md` for the
per-subsystem LLE-vs-HLE inventory.

**The headline for scope: all CPU cores exist** (68000 cycle-exact,
68020/030/040 functional, 030/040 oracle-fuzzed). Nothing left in the family
is blocked by the CPU — **every remaining machine is a *platform* problem**:
gate arrays, MCU protocols, video controllers, NuBus, power management.

## The 68K Mac universe (by CPU) — ✅ = done

- **68000** — 128K, 512K/512Ke, **Plus** ✅, SE, SE FDHD, Classic,
  Portable, PowerBook 100.
- **68020** — **II** ✅, **LC** ✅.
- **68030** — IIfx, SE/30, **IIx** ✅, **IIcx** ✅, **IIsi** ✅ (RBV),
  **IIci** ✅ (RBV), **Classic II** ✅, **Color Classic** ✅, **Color Classic II** ✅,
  **LC II** ✅, **LC III** ✅, **LC III+** ✅, **LC 520** ✅, **LC 550** ✅,
  **IIvx** ✅, **IIvi** ✅, **Mac TV** ✅ (Tinker Bell),
  Performa 4xx/6xx rebadges, PowerBook 140–180 / Duo.
- **68040 / 68LC040** — **Quadra 605** ✅, **LC 475** ✅, **LC 575** ✅,
  other Quadra & Centris (610/650/630/700/800/900/950…), Quadra 660AV/840AV
  (with DSP), PowerBook 500 / 190.

## What POM68K already has as leverage

The heavy lifting is done — future machines mostly re-wire existing parts:

1. **Moira: 68000 (cycle-exact) + 68020 + 68030/PMMU + 68882 + 68040/040 MMU.**
   Covers the CPU of the *entire* 68k line. The 030/040 extensions are
   oracle-fuzzed against WinUAE.
2. **Firmware-LLE MCUs (`M68hc05` + `CudaLle`)** running real **Egret** and
   **Cuda** 68HC05 images — shared by every 030/040 ADB machine (LC, LC II,
   Classic II, Color Classic, LC III/III+, LC 520/550/CC II, IIvx/IIvi,
   Quadra 605, LC 475/575). Built once, reused everywhere.
3. **Three complete machine platforms as templates**: the **V8** gate array
   (LC/LC II/Classic II/Color Classic), the **Sonora** gate array (LC III
   family + AIO) and its **VASP** recombination (IIvx/IIvi), and the
   **MEMCjr/PrimeTime + DAFB** 040 platform (Quadra 605 family). New machines
   in each family are largely identity + glue variations.

Reusable as-is: **VIA 6522**, **SCC 8530** (real SDLC/LLAP + baud), **NCR
5380** + pseudo-DMA, **NCR 53C96** + TurboSCSI wait-states, **IWM/SWIM1**
(real write engine) and **SWIM2** (real MFM/GCR cell engines), **ASC**
(`AscV8`/`AscSonora`/`AscIosb` stereo), the **V8 / Sonora / VASP** gate
arrays, **MEMCjr/PrimeTime**, **DAFB/Antelope**, **Ariel** RAMDAC,
**pseudo-VIA**, **RTC**, **Toby NuBus + DeclRom**, and 1/2/4/8-bit video.

## What's NOT built yet — the real cost of the remaining machines

Only a handful of *new* hardware bricks stand between here and the full
desktop/compact family. Each unlocks a cluster:

| Missing brick | Unlocks | Difficulty |
|---|---|---|
| **ADB-over-VIA compact MCU** (not Egret — the SE/Classic shift-register transcoder) | SE, SE FDHD, Classic, SE/30 | 🟢/🟡 |
| ~~**RBV** (RAM-Based Video controller)~~ ✅ **DONE** (`RbvMemory`, IIsi) | IIci (twin) | 🟡 |
| **Generalized NuBus + slot video** (the Mac II Toby/DeclRom port, made reusable) | IIx, IIcx, and every NuBus Quadra | 🟡 |
| **040 I/O-controller variants** (reuse Q605 CPU/DAFB/Cuda/53C96/SWIM2) | Centris/Quadra 610, 650, 630/LC 630, 700, 800, 900, 950 | 🟡 |
| **OSS + two 6502-class IOPs** | IIfx | 🟠 |
| **LCD framebuffer + 68HC05 Power Manager wiring** | PowerBook 150 / 190 / Duo / 500 series | 🟡 (see § Portables) |
| **LCD framebuffer + M50753 (740/6502-family) Power Manager core** | Macintosh Portable, PowerBook 100 / 140–180 | 🟡 |
| **AV I/O complex** (advanced video + S-Video digitizer, Curio combo SCC/SCSI/Ethernet, MACE, sound codec) | Quadra/Centris 660AV, 840AV — **boot without the DSP** | 🟠 |
| **AT&T DSP3210 core + VCOS/ARTA** | full 660AV / 840AV DSP fidelity | 🔴 |

Orthogonal (blocks no boot): **full architectural 68040 fidelity** — i/d
caches, copyback/snooping, and the real partial on-chip FPU where
transcendentals (`FSIN`/`FTAN`) trap to the software **FPSP**. The current
040 uses Moira's 68882 model + an i-cache throughput overlay.

## Effort tiers (remaining machines only)

### 🟢 Easy (hours–days) — config/identity, no new hardware

| Machine | Why cheap |
|---|---|
| **128K / 512K / 512Ke** | Subset of the Plus: 64K ROM, no SCSI, less RAM. Memory/ROM config. |
| **Performa rebadges** (4xx/5xx/6xx of done machines) | Model-ID longword only — the LC 475/LC III+/CC II precedent. |
| **Mac TV** ✅ **DONE** | `V8Memory::Model::MacTv` on the Tinker Bell ASIC (`$EAF1678D`, not EDE66CBD Sonora) — a `spiceClass()` sibling of the Color Classic + a `cpuHz` ctor param. Gate `mactv_boot_etalon`. |

### 🟡 Moderate (days–weeks each) — one new brick, shared everything else

| Machine | New work |
|---|---|
| **SE / SE FDHD / Classic** | Plus + **ADB-over-VIA compact MCU** (shift-register transcoder, not Egret). ADB device logic already exists (`AdbLine`). |
| **SE/30** | The compact ADB MCU + 030 + specific 1-bit video + PDS slot. Internally a compact Mac II. |
| **IIsi** ✅ **DONE** | `RbvMemory`/`RbvCpu`/`RbvVideo` (030 @ 20 MHz + Egret 344S0100 LLE + RBV). Gate `iisi_boot_etalon`. |
| **IIci** ✅ **DONE** | `RbvMemory` `iici` flavor — RBV + `AdbVia` (PIC1654S ADB modem LLE) + discrete `Rtc` + empty NuBus, 030 @ 25 MHz. Gate `iici_boot_etalon`. |
| **IIx / IIcx** ✅ **DONE** | `MacIIMemory::Model {IIx,IIcx}` + `Cpu020` `is030` — 68030 on the Mac II board, Toby NuBus reused, VIA machine-ID pins. Wall: skip the GLUE 24-bit remap once the 030 PMMU is on. Gates `iix/iicx_boot_etalon`. |
| **Centris/Quadra 610, 650; Quadra 630 / LC 630** | Adapt the 040 memory/I/O-controller variant; onboard video reuses DAFB. LC 630 is the last 68k desktop. |
| **Quadra 700 / 800 / 900 / 950** | The 040 platform exists, but these are **NuBus** systems — gated on the generalized NuBus/slot-video brick. |

### 🟠 Hard (weeks–months) — co-processors

| Machine | New work |
|---|---|
| **IIfx** | **OSS** + **two IOPs** (6502-class I/O processors driving SCC and ADB/SWIM) must be emulated. The hardest 030. |

### 🟡/🟠 Portables — *not* uniformly hard (feasibility researched 2026-07-24)

MAME emulates the **entire** 68k portable line — the Portable and PowerBook
100 are *fully working* (battery/charger simulated), the 140–180 and the Duos
boot — so the class is proven feasible, not speculative. The Power Manager
splits into two sub-families, and one lands squarely on POM68K's strengths:

- **68HC05 Power Manager** — PowerBook **150 / 190 / Duo / 500 series**.
  **This is the most accessible remaining machine, not the hardest.** The PM
  is the *same 68HC05 core POM68K already ships* (`M68hc05` + `CudaLle`, real
  Egret/Cuda firmware); MAME runs the Duo PM as its real 68HC05 code, the same
  firmware-LLE pattern. The **500 series is 68LC040** (CPU already done, Q605
  path); the Duos are 030. New work reduces to the **LCD framebuffer** (a flat
  grayscale/color decode — simpler than the CRT beam timing already skipped)
  plus PM wiring and sleep/wake. Battery/charge can be stubbed. 🟡
- **M50753 Power Manager** — Macintosh **Portable, PowerBook 100 / 140–180**.
  The M50753 is a **Mitsubishi 740-family MCU = a 6502 superset**. Leverage is
  *in the sibling projects*: **POMIIGS (65C816) and POM2 (6502)** already have
  a 6502-family core to port a 740 from (6502 + extended ops), rather than
  writing one from scratch. Plus the same LCD/PM work as above. 🟡

### 🟠 AV desktops — the DSP is not the boot blocker

- **Quadra/Centris 660AV, 840AV — boot to the Finder WITHOUT the DSP: 🟠
  moderate-hard.** The AT&T **DSP3210 is an offload engine** (audio, speech,
  telecom/GeoPort, video accel) — per the AV DSP FAQ it is **not required to
  boot Mac OS or run the Finder**. Stub it (as POM68K already stubs an empty
  NuBus) and the OS comes up; only software that explicitly drives the DSP is
  lost. The real wall is the **AV-specific I/O complex** — advanced video with
  S-Video capture, the **Curio** combo SCC/SCSI/Ethernet, **MACE** Ethernet, a
  sound codec, and the AV memory/DMA controller — all new gate arrays with no
  current POM68K equivalent (unlike the "plain" Quadra 610/650 that reuse
  MEMCjr/DAFB/53C96).
- **Full AV fidelity (emulated DSP3210): 🔴 out of practical scope.** The
  DSP3210 is a full 32-bit floating-point DSP core with its own OS
  (VCOS/ARTA); there is no reusable core in POM68K and **MAME itself does not
  yet run it** (the 660AV/840AV were the last holdout desktop 68k, still WIP in
  2025). Deferred indefinitely.

### 🔴 Orthogonal — full architectural 68040 fidelity

- **Full architectural 68040 fidelity** (caches / copyback-snooping / FPSP):
  a separate CPU project; software that observes those details needs it, but
  nothing currently blocks on it.

## Recommended roadmap (best return on effort)

Phase A/B/C (Plus → Mac II → the V8/Sonora/VASP/040 fan-out) are **done**.
The remaining order, cheapest-unlock-first:

1. ~~**Mac TV** (Tinker Bell) + **RBV → IIsi + IIci**~~ **DONE 2026-07-25.**
   Next in these families: Performa rebadges (AIO/Sonora) and, for real
   NuBus cards on the IIci, the generalized NuBus/slot-video brick.
2. **Compact ADB MCU → SE / Classic / SE/30** — one small transcoder unlocks
   the whole compact-68000 side.
4. **Generalize NuBus → IIx / IIcx** (reuse Toby), which also unblocks the
   NuBus Quadras.
5. **040 I/O variants → Centris/Quadra 610/650/630/700/800** — reuse the
   Q605 CPU and devices.
6. **IIfx (OSS + IOPs)** — the last desktop-030 architectural jump.
7. **A 68HC05 PowerBook (Duo or 500)** — reuses the shipped `M68hc05` core +
   030/040 CPUs; only the LCD framebuffer is genuinely new. Likely *easier*
   than the IIfx, despite living in the "portables" bucket.
8. **AV boot without the DSP** — the AV I/O complex (video/Curio/MACE/codec)
   at functional accuracy, DSP stubbed.
9. **Deferred / separate frontiers**: M50753 portables (need a 740/6502 core,
   borrowable from POMIIGS/POM2), full AV DSP fidelity, and full 68040
   cache/FPSP accuracy.

**Bottom line:** with all CPU cores and three full machine platforms in hand,
the complete **desktop + compact** family reduces to **four hardware bricks**
(compact ADB MCU, RBV, IIfx IOPs, 040 I/O variants) plus generalizing the
NuBus that Mac II already has. The "portables and AV" frontier is **not the
uniform wall it looks like** (feasibility research 2026-07-24): a **68HC05
PowerBook (Duo/500)** reuses the shipped `M68hc05` core and is likely *easier*
than the IIfx, and an **AV desktop boots without its DSP** (an offload engine,
not a boot dependency) — its cost is the AV I/O complex, not the DSP. What is
genuinely deferred: the **M50753 portables** (need a 740/6502 core, borrowable
from the POMIIGS/POM2 siblings), **full AV DSP fidelity** (no reusable core;
MAME itself doesn't run it yet), and **full 68040 cache/FPSP** accuracy.

## Sources

- [List of Mac models grouped by CPU type — Wikipedia](https://en.wikipedia.org/wiki/List_of_Mac_models_grouped_by_CPU_type)
- [Timeline of 680x0 Computers — Low End Mac](https://lowendmac.com/2015/timeline-of-680x0-computers/)
- [Macintosh II Family Technical Overview](https://www.angelfire.com/ca2/tech68k/macii.html)
- [Entry Level Family Technical Overview (LC / Egret / V8)](https://www.angelfire.com/ca2/tech68k/entry.html)
- [Apple I/O Notes — chip families (Egret/Cuda/Sonora/SWIM)](https://mcosre.sourceforge.net/docs/apple_io.html)
- [Apple Computer Custom IC Definitions — Higher Intellect Wiki](https://wiki.preterhuman.net/Apple_Computer_Custom_IC_Definitions)
- [Macintosh IIfx (OSS + IOPs) — Higher Intellect Wiki](https://wiki.preterhuman.net/Macintosh_IIfx)
- [Moira 68k emulator — Dirk W. Hoffmann (upstream cores: 68000/010/EC020/020)](https://dirkwhoffmann.github.io/Moira/)

Portables/AV feasibility research (2026-07-24):
- [MAME 2023 Mac emulation updates — E-Maculation](https://www.emaculation.com/forum/viewtopic.php?t=12050) — Portable + PB100 *fully working* via the M50753 (m5074x) core; Duo Power Manager run as real 68HC05 code.
- [MAME 2024/2025 Mac emulation updates — E-Maculation](https://www.emaculation.com/forum/viewtopic.php?t=12370) — 660AV/840AV WIP, the last holdout desktop 68k.
- [Driver:Mac 68K — MAMEdev Wiki](https://wiki.mamedev.org/index.php/Driver:Mac_68K) — per-model boot status (PowerBooks, Duos, IIfx A/UX-only, SE/30, IIci).
- [AV DSP Mini-FAQ (funet)](https://ftp.funet.fi/pub/mac/info-mac/info/hdwr/av-dsp-faq-101.txt) — the DSP3210 drives optional audio/speech/telecom/video, not the boot path.
- [DSP3210 programming — 68kMLA](https://68kmla.org/bb/threads/dsp3210-programming.37581/); [Motorola 68HC05 — Wikipedia](https://en.wikipedia.org/wiki/Motorola_68HC05)
</content>
</invoke>
