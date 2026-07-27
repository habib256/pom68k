# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K supports, and the effort each remaining one
takes, assessed against what the emulator already has. Written 2026-07-17;
updated 2026-07-20 (Quadra 605), 2026-07-24 (Phase C fan-out) and
**2026-07-25 (RBV + Tinker Bell + 68030 Mac II — this pass)**.

POM68K today covers **27 machine profiles** across every 68k generation, and
**every one boots to the Finder**:

- **68000, cycle-exact**: **Mac Plus** ✅
- **68020**: **Mac II** ✅ (Toby NuBus), **Mac LC** ✅ (V8/HMMU)
- **68030 + PMMU (+ 68882)**: **IIx** ✅ / **IIcx** ✅ (GLUE + NuBus),
  **IIci** ✅ / **IIsi** ✅ (RBV), **LC II** ✅, **Classic II** ✅ (Eagle),
  **Color Classic** ✅ (Spice), **Color Classic II** ✅, **LC III** ✅ /
  **LC III+** ✅ (Sonora), **LC 520** ✅ / **LC 550** ✅ (Sonora AIO),
  **IIvx** ✅ / **IIvi** ✅ (VASP), **Mac TV** ✅ (Tinker Bell)
- **68040 / 68LC040 + 040 MMU**: **Quadra 605** ✅, **LC 475** ✅,
  **LC 575** ✅ (MEMCjr/PrimeTime/DAFB), **Centris 610** ✅ /
  **Centris 650** ✅ and **Quadra 610** ✅ / **Quadra 650** ✅ /
  **Quadra 800** ✅ (djMEMC + IOSB), **Quadra 700** ✅ (discrete + DAFB
  TurboSCSI)

Every ADB machine now runs its MCU as **firmware LLE** (real 68HC05 —
Egret / Cuda images) by default. See `CLAUDE.md`, `TODO.md` and
`CHANGELOG.md` for subsystem status, and `docs/LLE_VS_HLE.md` for the
per-subsystem LLE-vs-HLE inventory.

**The headline for scope: all CPU cores exist** (68000 cycle-exact,
68020/030/040 functional, 030/040 oracle-fuzzed). Nothing left in the family
is blocked by the CPU — **every remaining machine is a *platform* problem**:
gate arrays, MCU protocols, video controllers, NuBus, power management.

## The 68K Mac universe (by CPU) — ✅ = done

- **68000** — 128K, 512K/512Ke, **Plus** ✅, **SE** ✅, **SE FDHD** ✅, **Classic** ✅,
  Portable, PowerBook 100.
- **68020** — **II** ✅, **LC** ✅.
- **68030** — IIfx, SE/30, **IIx** ✅, **IIcx** ✅, **IIsi** ✅ (RBV),
  **IIci** ✅ (RBV), **Classic II** ✅, **Color Classic** ✅, **Color Classic II** ✅,
  **LC II** ✅, **LC III** ✅, **LC III+** ✅, **LC 520** ✅, **LC 550** ✅,
  **IIvx** ✅, **IIvi** ✅, **Mac TV** ✅ (Tinker Bell),
  Performa 4xx/6xx rebadges, PowerBook 140–180 / Duo.
- **68040 / 68LC040** — **Quadra 605** ✅, **LC 475** ✅, **LC 575** ✅,
  **Centris 610** ✅ / **650** ✅, **Quadra 610** ✅ / **650** ✅ /
  **800** ✅, **700** ✅, **630 / LC 580** ✅, Quadra 900/950 (IOPs), 660AV/840AV (DSP),
  PowerBook 500 / 190.

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
| ~~**ADB-over-VIA compact MCU**~~ ✅ **DONE — IT IS THE PIC1654S WE ALREADY RUN**: MAME `mac128.cpp` drives the SE's ADB through `adbmodem`, the same transceiver as the Mac II/IIci/Centris. `MacMemory::Model {Plus,SE,SEFDHD,Classic}` wires it and all three boot System 6 to the Finder (`se_`/`sefdhd_`/`classic_boot_etalon`) | SE ✅, SE FDHD ✅, Classic ✅ — SE/30 still needs a ROM dump | 🟢 |
| ~~**RBV** (RAM-Based Video controller)~~ ✅ **DONE** (`RbvMemory` — IIsi + IIci) | — | 🟡 |
| **Generalized NuBus + slot video** (the Mac II Toby/DeclRom port, made reusable) | NuBus Quadras (700/800/900/950); real cards on IIci/IIsi/VASP. *IIx/IIcx no longer need it — they ride the Mac II board itself* | 🟡 |
| ~~**040 I/O-controller variants**~~ ✅ **DONE**: djMEMC + IOSB (`CentrisMemory` — Centris/Quadra 610 + 650 + **800**), discrete "Spike" (`Q700Memory`), F108 + PrimeTime II + Valkyrie (`Q630Memory` — Quadra 630 / LC 580) | still to do: Quadra 900/950 (they need the IOP brick, not an I/O variant) | 🟢 |
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
| **Centris/Quadra 610, 650, 800** ✅ **DONE** | `CentrisMemory`/`CentrisCpu` (djMEMC + IOSB), Q605 devices + discrete RTC + PIC1654S ADB LLE. Gates `centris610/650_`, `quadra610/650/800_boot_etalon`. The **800** needed only its ID pins ($12) + the Ethernet address ROM at $50008000; SONIC and its NuBus slots stay unmapped-0 and the boot path never binds them. |
| **Quadra 630 / LC 580** ✅ **DONE** | Not DAFB after all: **F108** + **PrimeTime II** + the fixed-mode **Valkyrie** framebuffer (`Q630Memory`/`Q630Cpu`/`Valkyrie`), Cuda 341S0060, 68040 @ 33 MHz. Mac OS 8.1 Finder at 640×480×8 on the first run; the ATA/IDE port is mapped but empty, so boot goes over SCSI. Gates `q630_`/`lc580_boot_etalon`. |
| **Quadra 700** ✅ **DONE** | `Q700Memory`/`Q700Cpu` — discrete 040: Mac II VIA1/VIA2 + RTC + PIC ADB, Quadra DAFB/53C96/SWIM1/EASC, SCSI behind DAFB's TurboSCSI cell. NuBus unpopulated. Gate `q700_boot_etalon`. |
| **Quadra 900 / 950** | The Q700 machine plus **two AppleP IC IOPs** (the SCC and SWIM run on 6502-class I/O processors) and an Egret — the same IOP brick the IIfx needs, so one core unlocks three machines. |

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

1. ~~**Mac TV** (Tinker Bell) + **RBV → IIsi + IIci** + **IIx / IIcx** (68030
   on the Mac II board) + **040 I/O variants → Centris/Quadra 610/650**~~
   **DONE 2026-07-24/25.**
2. ~~**Quadra 800**~~ **DONE 2026-07-25** — a fifth model of the djMEMC/IOSB
   machine (ID pins `$12`, Ethernet address ROM, SONIC/NuBus unmapped).
3. ~~**Quadra 630 / LC 630 (+ LC 580)**~~ ✅ **DONE 2026-07-25** — one more 040
   I/O-controller variant on the proven platform.
4. ~~**Compact ADB MCU → SE / SE FDHD / Classic**~~ ✅ **DONE 2026-07-25** (SE/30 blocked on a ROM dump) — one small
   transcoder unlocks the whole compact side (three ROMs already on hand).
5. **Generalize NuBus** → real slot cards on IIci/IIsi/VASP and the NuBus
   Quadras (700/900/950).
6. **IIfx (OSS + IOPs)** — the last desktop-030 architectural jump.
7. **A 68HC05 PowerBook (150, Duo or 500)** — reuses the shipped `M68hc05`
   core + 030/040 CPUs; only the LCD framebuffer is genuinely new. Likely
   *easier* than the IIfx, despite living in the "portables" bucket.
8. **AV boot without the DSP** — the AV I/O complex (video/Curio/MACE/codec)
   at functional accuracy, DSP stubbed.
9. **Deferred / separate frontiers**: M50753 portables (need a 740/6502 core,
   borrowable from POMIIGS/POM2), full AV DSP fidelity, and full 68040
   cache/FPSP accuracy.

**Caveat on "done".** Booting to the Finder is the *entry* criterion, not
the finish line: 22 of the 25 profiles have **no gate past the boot
signature** (only Mac II, Quadra 605 and LC II do — see TODO "Test &
validation depth"). Adding the 26th machine is cheaper than hardening the
25 that exist; the roadmap above should be read against that trade.

**Bottom line:** with all CPU cores and five full machine platforms in hand
(V8, Sonora/VASP, RBV, MEMCjr/PrimeTime, djMEMC/IOSB), the complete
**desktop** family is essentially finished — what remains there is the
**Quadra 800/630/700/900/950** cluster (SONIC + generalized NuBus + one more
I/O variant) and the **IIfx** (OSS + IOPs). The **compact** side is still
gated on a single brick: the **ADB-over-VIA transcoder MCU** (SE / SE FDHD /
Classic / SE/30). The "portables and AV" frontier is **not the
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

## After the Quadra 630 (2026-07-25): what is actually left

Every remaining 68k Mac needs a **new co-processor core**, not a new bus:

| Brick | Unlocks | Notes |
|---|---|---|
| **AppleP IC IOP** (Apple 343S1021: a 65C02 core + 2 KB shared RAM + two DMA channels + host/peripheral mailboxes + timer — MAME `machine/applepic.cpp`, 578 lines) plus the **OSS** interrupt controller | **IIfx**, **Quadra 900 / 950** | The 65C02 core is the only piece POM68K has no equivalent of — the sibling [POMIIGS](../../POMIIGS/) ships a compact `CPU65816` (825 lines) whose emulation mode is a 65C02, the natural candidate to vendor. Note the IIfx has **no built-in video**: it needs the NuBus card path (`TobyVideo`/`DeclRom` already exist). |
| **Power Manager** (M50753 on the Portable/PB100, 68HC05 on the PB150/Duos) | Portable, PowerBook 1xx/Duo | The 68HC05 half is already in the tree (`M68hc05`, running Egret/Cuda firmware) — the PB150 is therefore the cheapest portable. |
| **AV DSP** (AT&T DSP3210) | 660AV / 840AV | Out of scope for the foreseeable future. |
| **ROM dump** | SE/30 | Nothing to build: it is a compact Mac IIx (`MacIIMemory` + compact video). |
