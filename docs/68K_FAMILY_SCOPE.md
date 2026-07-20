# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K could support later, and the effort each takes,
assessed against what the emulator already has. Written 2026-07-17; updated
2026-07-20 after the Quadra 605 reached the Finder.

POM68K today: **Mac Plus** (68000, cycle-exact), **Mac LC II** (68030 + PMMU +
68882, functional), and **Quadra 605 / LC 475 profile** (68040/68LC040 +
040 MMU, functional). The Quadra boots Mac OS 8.1 to the Finder. See
`CLAUDE.md`, `TODO.md` § Phase 3 and `CHANGELOG.md` for the subsystem status.

## The 68K Mac universe (by CPU)

- **68000** — 128K, 512K/512Ke, **Plus** ✅, SE, SE FDHD, Classic,
  Portable, PowerBook 100.
- **68020** — Macintosh **II**, **LC** (original).
- **68030** — IIx, IIcx, IIci, IIfx, IIsi, SE/30, Classic II, Color Classic,
  **LC II** ✅, LC III, Performa 4xx/6xx, IIvx/IIvi, PowerBook 140–180 / Duo.
- **68040 / 68LC040** — **Quadra 605 / LC 475 profile** ✅, other Quadra &
  Centris (610/650/700/800/900/950…),
  LC 475/575, Performa 575+, Quadra 660AV/840AV (with DSP).

## What POM68K already has as leverage

Three pieces do most of the heavy lifting for future machines:

1. **Moira extended to 68030 + PMMU + 68882** (the O1–O5 work). Upstream Moira
   only does 68000/68010/68EC020/68020 — this fork adds the **68030**, which
   covers the CPU of the *entire* 030 generation. The 68000 path is
   cycle-exact; the 68020 path is functional.
2. **Egret HLE (68HC05 ADB controller)** — the Egret is **shared** by LC, LC II,
   LC III, Classic II, IIsi, IIvx/IIvi. Built once (done), reused by six
   machines.
3. **68040 + 040 MMU execution** (the Q1–Q4 work) and a complete first 040
   machine: **MEMCjr + PrimeTime/pseudo-VIA2 + DAFB HLE + Cuda HLE + NCR
   53C96**. This removes the former CPU-core barrier and provides reusable
   parts for nearby Quadra/Centris models. The core is functional, not yet a
   cache- and cycle-exact 68040.

Reusable as-is: **VIA 6522**, **SCC 8530**, **NCR 5380 SCSI** + hard disk,
**IWM/SWIM1** + Sony 800K GCR, **ASC** (V8 variant), the **V8 gate array**,
**Ariel** RAMDAC, **pseudo-VIA**, RTC, **NCR 53C96**, **MEMCjr/PrimeTime**,
**DAFB/Antelope** and 1/2/4/8-bit video decoding.

Not yet built (the real cost of new machines): **NuBus + slot video**, the
**IIfx IOPs**, a faithful **SWIM2**, stereo **EASC**, the per-machine gate
arrays (**RBV / OSS / MDU / Sonora**), and the PowerBook **Power Manager**.
The 040 side still lacks architectural caches, accurate timing, and the
on-chip-FPU/FPSP split; the current Quadra compatibility configuration uses
Moira's 68882 FPU model. **Cuda, DAFB and MEMCjr exist**, but only to the extent
currently exercised by the Quadra 605 ROM and Mac OS 8.1.

## Effort tiers

### 🟢 Easy (days) — direct variations of machines already done

| Machine | CPU | Why cheap |
|---|---|---|
| **LC (original)** | 68020 | It is an **LC II with a 68020** — same V8, Egret, ASC. Moira already does the 020. Change CPU model + ROM. |
| **128K / 512K / 512Ke** | 68000 | Subset of the Plus: 64K ROM, no SCSI, less RAM. Mostly memory/ROM config. |
| **SE / Classic** | 68000 | Plus + ADB. SE ADB runs over the VIA shift register + a small MCU (not Egret); a small transcoder, but the ADB logic already exists. |
| **LC 475 / Performa 475/476** | 68LC040 | Direct variants of the completed MEMCjr/PrimeTime machine. The remaining work is model identity, ROM/config validation and a usable no-FPU software path. |

### 🟡 Moderate (1–3 weeks each) — shared CPU, new machine glue

| Machine | New work |
|---|---|
| **Classic II / Performa 200** | 030 + Egret, internals close to LC II. Adapt the gate array. |
| **LC III / Performa 450** | **Sonora** gate array (VIA1 + pseudo-VIA2 + **SWIM2** + Egret) — evolved V8. Write Sonora + SWIM2. |
| **Color Classic** | 030 + V8-derivative + built-in color video, but **Cuda** instead of Egret → adapt the existing Quadra Cuda HLE and add the machine-specific video/glue. |
| **IIsi / IIvx / IIvi** | 030 + Egret + **RBV** (RAM-Based Video) → write RBV. |
| **SE/30** | 030 + 68882 + specific 1-bit video + PDS slot, ADB via MCU (no Egret). |
| **LC 575 / Performa 575–578** | 68LC040 | Shares the universal ROM and much of the 605/475 platform, but needs its built-in display geometry and model-specific I/O validated. |
| **Quadra/Centris 610, 650, Quadra 800** | 040 | The CPU, DAFB, Cuda and 53C96 now exist. Adapt the memory/I/O controller variants, machine IDs and ROM-visible details; onboard video can reuse the DAFB path. |

### 🟠 Hard (1–2 months each) — NuBus, portable controllers, or major variants

| Machine | New work |
|---|---|
| **Mac II / IIx / IIcx / IIci** | Two discrete VIAs, **NuBus** + **slot video card** (a whole new subsystem), **OSS/RBV/MDU** depending on model. IIci adds integrated RBV. |
| **IIfx** | Also **two IOPs** (6502-class I/O processors driving SCC and ADB/SWIM) → those co-processors must be emulated. The hardest 030. |
| **PowerBooks (030)** | **Power Manager** MCU, LCD video, power domain → new Power Manager HLE. |
| **Quadra 700 / 900 / 950** | The 040 CPU is done, but these are NuBus systems with different memory/I/O glue and slot-video requirements. |

### 🔴 Very hard (months) — AV Macs and full 68040 fidelity

- **Full architectural 68040 fidelity** is still a separate CPU project:
  instruction/data caches and copyback/snooping behaviour, accurate timing,
  and the real partial on-chip FPU where transcendentals such as
  `FSIN`/`FTAN` trap to the software **FPSP**. None of that blocks the current
  functional Quadra 605 boot, but software that observes those details needs
  it.
- **Quadra 660AV / 840AV** additionally carry an **AT&T 3210 DSP**, plus the
  AV-specific I/O complex. They remain effectively out of scope despite the
  completed 040 core.

## Recommended roadmap (best return on effort)

1. **Finish Quadra 605 polish** — DAFB color/depth switching, faithful EASC and
   SWIM2, no-FPU SANE path, then pin a whole-machine boot gate.
2. **LC (68020)** — nearly free; validates multi-machine parameterization.
3. **Classic II + Color Classic + LC III** — the "Egret/030 cluster" around the
   LC II (Sonora, SWIM2, Cuda, RBV); where POM68K is strongest.
4. **SE / Classic (68000 + ADB)** — widens the compact-68000 side.
5. **Nearby 040 desktops** — LC/Performa 475 variants, LC 575, then
   Quadra/Centris 610/650/800, reusing the Q605 CPU and machine devices.
6. **IIsi then the Mac II family + NuBus** — the remaining architectural jump
   (slot video).

**Bottom line:** POM68K now has executable cores for every classic-Mac
generation from **68000 through 68040** (including 68020), and one supported
machine in each of the 68000, 68030 and 68040 classes boots to a usable desktop.
New models are primarily a machine-platform problem: gate arrays, MCU
protocols, storage/video variants, and NuBus. The sharpest remaining barriers
are **NuBus + slot video**, portable power management, AV DSPs, and fully
faithful 68040 cache/FPU/timing behaviour — no longer basic 040 instruction
execution.

## Sources

- [List of Mac models grouped by CPU type — Wikipedia](https://en.wikipedia.org/wiki/List_of_Mac_models_grouped_by_CPU_type)
- [Timeline of 680x0 Computers — Low End Mac](https://lowendmac.com/2015/timeline-of-680x0-computers/)
- [Macintosh II Family Technical Overview](https://www.angelfire.com/ca2/tech68k/macii.html)
- [Entry Level Family Technical Overview (LC / Egret / V8)](https://www.angelfire.com/ca2/tech68k/entry.html)
- [Apple I/O Notes — chip families (Egret/Cuda/Sonora/SWIM)](https://mcosre.sourceforge.net/docs/apple_io.html)
- [Apple Computer Custom IC Definitions — Higher Intellect Wiki](https://wiki.preterhuman.net/Apple_Computer_Custom_IC_Definitions)
- [Macintosh IIfx (OSS + IOPs) — Higher Intellect Wiki](https://wiki.preterhuman.net/Macintosh_IIfx)
- [Moira 68k emulator — Dirk W. Hoffmann (upstream cores: 68000/010/EC020/020)](https://dirkwhoffmann.github.io/Moira/)
