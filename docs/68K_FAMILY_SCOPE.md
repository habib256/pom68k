# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K could support later, and the effort each takes,
assessed against what the emulator already has. Written 2026-07-17.

POM68K today: **Mac Plus** (68000, cycle-exact) and **Mac LC II** (68030 +
PMMU + 68882, functional). See `CLAUDE.md` for the subsystem map.

## The 68K Mac universe (by CPU)

- **68000** — 128K, 512K/512Ke, **Plus** ✅, SE, SE FDHD, Classic,
  Portable, PowerBook 100.
- **68020** — Macintosh **II**, **LC** (original).
- **68030** — IIx, IIcx, IIci, IIfx, IIsi, SE/30, Classic II, Color Classic,
  **LC II** ✅, LC III, Performa 4xx/6xx, IIvx/IIvi, PowerBook 140–180 / Duo.
- **68040 / 68LC040** — Quadra & Centris (605/610/650/700/800/900/950…),
  LC 475/575, Performa 575+, Quadra 660AV/840AV (with DSP).

## What POM68K already has as leverage

Two pieces do most of the heavy lifting for future machines:

1. **Moira extended to 68030 + PMMU + 68882** (the O1–O5 work). Upstream Moira
   only does 68000/68010/68EC020/68020 — this fork adds the **68030**, which
   covers the CPU of the *entire* 030 generation. The 68000 path is
   cycle-exact; the 68020 path is functional.
2. **Egret HLE (68HC05 ADB controller)** — the Egret is **shared** by LC, LC II,
   LC III, Classic II, IIsi, IIvx/IIvi. Built once (done), reused by six
   machines.

Reusable as-is: **VIA 6522**, **SCC 8530**, **NCR 5380 SCSI** + hard disk,
**IWM/SWIM1** + Sony 800K GCR, **ASC** (V8 variant), the **V8 gate array**,
**Ariel** RAMDAC, **pseudo-VIA**, RTC, 1-bit + 8-bit video.

Not yet built (the real cost of new machines): **68040 execution core**,
**NuBus + slot video**, the **IIfx IOPs**, **Cuda**, the per-machine gate
arrays (**RBV / OSS / MDU / Sonora / DAFB**), and the PowerBook **Power
Manager**.

## Effort tiers

### 🟢 Easy (days) — direct variations of machines already done

| Machine | CPU | Why cheap |
|---|---|---|
| **LC (original)** | 68020 | It is an **LC II with a 68020** — same V8, Egret, ASC. Moira already does the 020. Change CPU model + ROM. |
| **128K / 512K / 512Ke** | 68000 | Subset of the Plus: 64K ROM, no SCSI, less RAM. Mostly memory/ROM config. |
| **SE / Classic** | 68000 | Plus + ADB. SE ADB runs over the VIA shift register + a small MCU (not Egret); a small transcoder, but the ADB logic already exists. |

### 🟡 Moderate (1–3 weeks each) — LC-II philosophy, new gate array

| Machine | New work |
|---|---|
| **Classic II / Performa 200** | 030 + Egret, internals close to LC II. Adapt the gate array. |
| **LC III / Performa 450** | **Sonora** gate array (VIA1 + pseudo-VIA2 + **SWIM2** + Egret) — evolved V8. Write Sonora + SWIM2. |
| **Color Classic** | 030 + V8-derivative + built-in color video, but **Cuda** instead of Egret → small Cuda HLE (same 68HC05 family as Egret). |
| **IIsi / IIvx / IIvi** | 030 + Egret + **RBV** (RAM-Based Video) → write RBV. |
| **SE/30** | 030 + 68882 + specific 1-bit video + PDS slot, ADB via MCU (no Egret). |

### 🟠 Hard (1–2 months each) — the "big" Mac II family + NuBus

| Machine | New work |
|---|---|
| **Mac II / IIx / IIcx / IIci** | Two discrete VIAs, **NuBus** + **slot video card** (a whole new subsystem), **OSS/RBV/MDU** depending on model. IIci adds integrated RBV. |
| **IIfx** | Also **two IOPs** (6502-class I/O processors driving SCC and ADB/SWIM) → those co-processors must be emulated. The hardest 030. |
| **PowerBooks (030)** | **Power Manager** MCU, LCD video, power domain → new Power Manager HLE. |

### 🔴 Very hard (months) — the 68040 generation

- **Quadra / Centris** need a **68040 execution core in Moira** (exists neither
  upstream nor in this fork). The 040 is not a faster 030: **different MMU**
  (fixed table format, no `PMOVE`), a **partial on-chip FPU** (transcendentals
  FSIN/FTAN/… are *not* in hardware — they trap to the software **FPSP**),
  snooping caches, `MOVE16`. Plus new gate arrays (DAFB video, DjMEMC/MEMCjr
  memory, IOSB/Curio I/O). Comparable in scope to the whole 68030 effort
  already done.
- **660AV / 840AV** additionally carry an **AT&T 3210 DSP** → effectively out
  of scope.

## Recommended roadmap (best return on effort)

1. **LC (68020)** — nearly free; validates multi-machine parameterization.
2. **Classic II + Color Classic + LC III** — the "Egret/030 cluster" around the
   LC II (Sonora, SWIM2, Cuda, RBV); where POM68K is strongest.
3. **SE / Classic (68000 + ADB)** — widens the compact-68000 side.
4. **IIsi then the Mac II family + NuBus** — the real architectural jump (slot
   video).
5. **68040 / Quadra** — a project in itself (new CPU core); only if the ambition
   goes that far.

**Bottom line:** the entire **68000 and 68030 generation is within reach** (the
CPU core and the Egret already exist) — most remaining work is the per-machine
gate arrays. The hard, sharp barrier is the **68040**: it needs a new execution
core, comparable in magnitude to the 68030 work already completed.

## Sources

- [List of Mac models grouped by CPU type — Wikipedia](https://en.wikipedia.org/wiki/List_of_Mac_models_grouped_by_CPU_type)
- [Timeline of 680x0 Computers — Low End Mac](https://lowendmac.com/2015/timeline-of-680x0-computers/)
- [Macintosh II Family Technical Overview](https://www.angelfire.com/ca2/tech68k/macii.html)
- [Entry Level Family Technical Overview (LC / Egret / V8)](https://www.angelfire.com/ca2/tech68k/entry.html)
- [Apple I/O Notes — chip families (Egret/Cuda/Sonora/SWIM)](https://mcosre.sourceforge.net/docs/apple_io.html)
- [Apple Computer Custom IC Definitions — Higher Intellect Wiki](https://wiki.preterhuman.net/Apple_Computer_Custom_IC_Definitions)
- [Macintosh IIfx (OSS + IOPs) — Higher Intellect Wiki](https://wiki.preterhuman.net/Macintosh_IIfx)
- [Moira 68k emulator — Dirk W. Hoffmann (upstream cores: 68000/010/EC020/020)](https://dirkwhoffmann.github.io/Moira/)
