# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K ships, and what each remaining one costs.
Written 2026-07-17; the Phase C fan-out (2026-07-24/25) obsoleted most of it,
and every line was re-derived from the code on **2026-08-12**.

The target is **every 68k Macintosh**. Counts in this document describe the
current implementation and must never be read as a ceiling on that target.

**POM68K ships 37 machine profiles across 12 platform implementations. Every
one boots to the Finder and every one has a boot etalon that proves it.**
Source of truth for that count: `kMachineProfiles` in
`src/MachineCatalog.h`. Every row carries its stable `SnapMachine` id and is
consumed by the **Machine** menu; compile-time checks keep ids unique and
dense. A platform = one `*Memory`/`*Cpu` pair in `src/` and one save/load
overload in `src/SaveStateMachines.h`.

There is no longer any platform in `src/` without a profile row: the last two
paid the house rule (a catalogue row is earned by a Finder cell *plus* GUI
and save-state wiring) — the **Quadra 900/950** on 2026-08-02 and the
**PowerBook Duo 230** on 2026-08-06.

**The headline for scope: all CPU cores exist** — 68000 cycle-exact,
68020/68030+PMMU/68040+MMU functional and oracle-fuzzed against WinUAE, plus
the 68882 and the integrated 68040 FPU. Nothing left in the family is blocked
by the CPU. **Every remaining machine is a co-processor problem**: one of the
bricks in § 3, or a ROM dump.

Per-subsystem status lives in `CLAUDE.md`; the as-built per-platform
descriptions in `DEV.md` § 2; the LLE-vs-HLE deviation inventory in
`docs/LLE_VS_HLE.md`; the backlog in `TODO.md` § 7.

---

## 1. Done — the 37 profiles and the gate that proves each

Every gate below is a Finder-signature boot etalon unless noted.

### 68000 — `MacMemory` / `Cpu68k` (cycle-exact)

The compacts turned out to be a `MacMemory::Model` enum, not a machine: the SE
map is the Plus map with a bigger ROM, the overlay clearing on the first ROM
access, and **ADB on the same PIC1654S firmware LLE the Mac II uses**
(VIA PB5/PB4 = ST, PB3 = /ADB IRQ — `MacMemory.h:46-48`) in place of the M0110.

| Profile | ROM | Gate |
|---|---|---|
| Macintosh Plus | `macplus.rom` | `system_boot_etalon`, `disk_boot_etalon`, `scsi_boot_etalon`, `rom_boot_etalon`, `input_etalon` |
| Macintosh SE | `B2E362A8` | `se_boot_etalon` |
| Macintosh SE FDHD | `B306E171` | `sefdhd_boot_etalon` |
| Macintosh Classic | `A49F9914` | `classic_boot_etalon`, `jit_classic_boot_etalon` |

### GLUE + Toby NuBus — `MacIIMemory` / `Cpu020`

`MacIIMemory::Model` + `Cpu020`'s `is030` flag. The IIx/IIcx wall was the 030
PMMU double-translating against the GLUE 24-bit remap — skip `physAddr` when
the PMMU is on (`MacIIMemory.h:66-71`). All four run at 15.6672 MHz:
`kCpuHz` is fixed and the ctor takes no clock (`MacIIMemory.h:34,45`).

| Profile | CPU | ROM | Gate |
|---|---|---|---|
| Macintosh II | 68020 | `9779D2C4` | `macii_boot_etalon`, `macii_sys7_boot_etalon`, `macii_post_etalon`, `macii_mouse_etalon`, `jit_macii_boot_etalon` |
| Macintosh IIx | 68030 + PMMU | `97221136` | `iix_boot_etalon` |
| Macintosh IIcx | 68030 + PMMU | `97221136` | `iicx_boot_etalon` |
| Macintosh SE/30 | 68030 + PMMU, compact 512×342 mono (`Se30Video.h`) | `97221136` + `se30vrom.uk6` | `se30_boot_etalon`, `jit_se30_boot_etalon` |

### OSS + two Apple PIC IOPs — `IIfxMemory` / `IIfxCpu`

The one platform with **no VIA2 and no built-in video**: the OSS interrupt
controller replaces VIA2, and two Apple PIC (343S1021) IOPs — each an R65C02
running firmware the host ROM downloads at boot — front the SCC and the SWIM.
ADB is bit-banged by the SWIM IOP's own firmware against `AdbLine`; video is
`TobyVideo` on slot 9. As-built: `DEV.md` § 2.10. Blueprint:
`docs/IOP_BRINGUP.md`.

| Profile | ROM | Gate |
|---|---|---|
| Macintosh IIfx (68030 @ 40 MHz) | `4147DD77` | `iifx_boot_etalon` (System 7.6), `iifx_post_etalon`, `iifx_input_etalon`, `jit_iifx_boot_etalon`; device gates `applepic_test`, `r65c02_test` |

### RBV (RAM-Based Video) — `RbvMemory` / `RbvCpu` / `RbvVideo`

The ancestor of the whole V8/VASP/Sonora line: framebuffer at RAM start,
Bt478 CLUT, SWIM1, discrete ASC. It runs at the **shared default i-cache
boost of 4** like every other 030 — the 2026-07-25 fix was to charge VIA/bus
time in machine cycles, not to disable the boost (`RbvCpu.cpp:35-40`).

| Profile | Distinguishing | ROM | Gate |
|---|---|---|---|
| Macintosh IIci | 25 MHz, PIC1654S ADB modem LLE + discrete 343-0042 RTC + empty NuBus | `368CADFE` | `iici_boot_etalon` |
| Macintosh IIsi | 20 MHz, Egret 344S0100 LLE | `36B7FB6C` | `iisi_boot_etalon`, `iisi_input_etalon`, `jit_iisi_boot_etalon` |

### V8 / Eagle / Spice / Tinker Bell — `V8Memory` / `Cpu030`

One gate array with a `Model` enum, a `spiceClass()` predicate and a `cpuHz`
ctor parameter. Machine blueprint: `docs/LCII_HARDWARE.md`.

| Profile | CPU / ASIC | ROM | Gate |
|---|---|---|---|
| Macintosh LC | 68020 + HMMU, V8 | `350EACF0` | `lc_boot_etalon`, `jit_lc_boot_etalon` |
| Macintosh LC II | 68030 + MMU + 68882, V8 | `35C28F5F` | `lcii_boot_etalon`, `lcii_sys7_boot_etalon`, `lcii_soak/persist/launch/floppy_etalon`, `lcii_savestate_etalon`, `jit_lcii_boot_etalon` |
| Macintosh Classic II | 68030, Eagle (mono 512×342 from RAM) | `3193670E` | `classic2_boot_etalon` |
| Macintosh Color Classic | 68030, Spice + Cuda 341S0417 LLE | `ECD99DC0` | `cclassic_boot_etalon` |
| Macintosh TV | 68030 @ 31.3 MHz, Tinker Bell + Cuda LLE | `EAF1678D` | `mactv_boot_etalon`, `jit_mactv_boot_etalon` |

### Sonora — `SonoraMemory` / `SonoraCpu` / `SonoraVideo`

One machine, two MCU flavours selected by the `cudaAdb` flag: Egret 341S0851
on the LC III boards, **Cuda 341S0060** on the EDE66CBD all-in-ones (2.40 —
2.37 livelocks on pseudo-cmd `$0E`). Bring-up story: `docs/LC520_BRINGUP.md`.

| Profile | Distinguishing | ROM | Gate |
|---|---|---|---|
| Macintosh LC III | 25 MHz, Egret | `ECBBC41C` | `lc3_boot_etalon`, `lc3_input_etalon`, `jit_lc3_boot_etalon` |
| Macintosh LC III+ | 33.33 MHz (`kCpuHzPlus`), Egret | `ECBBC41C` | `lc3plus_boot_etalon` |
| Macintosh LC 520 | 25 MHz, Cuda, sense 6 → 640×480×8 | `EDE66CBD` | `lc520_boot_etalon`, `lc520_input_etalon` |
| Macintosh LC 550 | 33.33 MHz, Cuda | `EDE66CBD` | `lc550_boot_etalon` |
| Color Classic II | same board and box ID as the 550, sense 2 → 512×384 | `EDE66CBD` | `cclassic2_boot_etalon` |

### VASP — `VaspMemory` / `VaspCpu` / `VaspVideo`

"V8 video on Sonora addressing": Sonora shell + V8 peripherals (AscV8, SWIM1,
Ariel, 2048-byte pitch), Egret 341S0851. Empty NuBus reads MAME-unmapped 0.

| Profile | Clock | ROM | Gate |
|---|---|---|---|
| Macintosh IIvx | 31.3344 MHz | `4957EB49` | `iivx_boot_etalon`, `iivx_input_etalon`, `iivx_soak_etalon`, `iivx_persist_etalon`, `jit_iivx_boot_etalon` |
| Macintosh IIvi | 15.6672 MHz (menu says "16") | `4957EB49` | `iivi_boot_etalon` |

### MEMCjr + PrimeTime + DAFB — `Q605Memory` / `Cpu040`

68040 / 68LC040 + 040 MMU, Cuda firmware LLE, NCR 53C96, SWIM2. **All three
profiles run at 25 MHz**: `Q605Memory::kCpuHz` is fixed and the ctor takes no
clock parameter (`Q605Memory.h:73,76`), so the LC 575's "33 MHz" is a menu
label, not the emulated clock. The identity is the `POM68K_Q605_ID` longword
only.

| Profile | CPU | ROM | Gate |
|---|---|---|---|
| Macintosh LC 475 | 68LC040 | `FF7439EE` | `lc475_boot_etalon` |
| Macintosh LC 575 | 68LC040 | `FF7439EE` | `lc575_boot_etalon` |
| Quadra 605 | 68040 + integrated FPU | `FF7439EE` | `q605_boot_etalon`, `q605_nofpu_/barefpu_/floppy_/cudalle_boot_etalon`, `q605_cudalle_mouse_/key_etalon`, `q605_ot_bind_etalon`, `q605_cdrom_/cdboot_/cdhot_etalon`, `q605_soak_/persist_/savestate_etalon`, `jit_q605_boot_etalon`, `interp_q605_boot_etalon` |

### djMEMC + IOSB — `CentrisMemory` / `CentrisCpu`

Q605 devices (DAFB, 53C96, SWIM2, AscIosb, PseudoVia) + discrete `Rtc` +
PIC1654S ADB LLE. The one wall was djMEMC's 2 MB VRAM window vs MEMCjr's 1 MB.
The Quadra 800 needed only its ID pins (`$12`) and the Ethernet address ROM at
`$50008000`; SONIC and its NuBus slots stay unmapped-0 and the boot path never
binds them. Clocks and ID pins are per profile (`CentrisMemory.h:56-67`).

| Profile | Clock / ID pins | Gate |
|---|---|---|
| Macintosh Centris 610 | 20 MHz / `$40` | `centris610_boot_etalon` |
| Macintosh Centris 650 | 25 MHz / `$46` | `centris650_boot_etalon`, `jit_centris650_boot_etalon`, `interp_centris650_boot_etalon` |
| Macintosh Quadra 610 | 25 MHz / `$44` | `quadra610_boot_etalon` |
| Macintosh Quadra 650 | 33.33 MHz / `$52` | `quadra650_boot_etalon` |
| Macintosh Quadra 800 | 33.33 MHz / `$12` | `quadra800_boot_etalon` |

All five share ROM `F1A6F343`. The 610/650/800 get a full 68040 + integrated FPU
(`POM68K_CENTRIS_FPU`, set by the runner); the Centrises get a 68LC040.

### Discrete 040 — `Q700Memory` / `Q700Cpu` (Spike, Eclipse, Zydeco)

The first *discrete* 040 machines: Mac II VIA1/VIA2 + RTC + PIC ADB in front,
Quadra DAFB/53C96/SWIM1/EASC behind, SCSI through **DAFB's own TurboSCSI cell**.
`Model {Spike, Q900, Q950}` — the two towers replace the Spike's front end
with the **IIfx's**: two `ApplePic` IOPs, `AdbLine`, an `Egret` on VIA1
CB1/CB2 instead of the discrete RTC, and a second 53C96 bus
(`docs/IOP_BRINGUP.md` § 5).

| Profile | Clock | ROM | Gate |
|---|---|---|---|
| Macintosh Quadra 700 | 25 MHz | `420DBFF3` | `q700_boot_etalon`, `jit_q700_boot_etalon`, `interp_q700_boot_etalon` |
| Macintosh Quadra 900 | 25 MHz | `420DBFF3` (shared with the 700) | `q900_boot_etalon` |
| Macintosh Quadra 950 | 33.33 MHz | `3DC27823` | `q950_boot_etalon` |

### F108 + PrimeTime II + Valkyrie — `Q630Memory` / `Q630Cpu` / `Valkyrie`

The last 68k desktop board. Fixed-mode Valkyrie framebuffer, Cuda 341S0060,
68040 @ 33 MHz (both profiles — `Q630Memory.h:66`). The ATA/IDE port is mapped
but has no drive, so boot goes over SCSI (§ 4).

| Profile | ROM | Gate |
|---|---|---|
| Macintosh Quadra 630 | `06684214` | `q630_boot_etalon`, `jit_q630_boot_etalon`, `interp_q630_boot_etalon` |
| Macintosh LC / Performa 580 | `06684214` | `lc580_boot_etalon` |

### MSC + PG&E — `MscMemory` / `MscCpu` (the only laptop)

No Egret, no Cuda, no floppy: power, clock, PRAM, keyboard, trackball and the
1-Wire battery ID all live behind the **PG&E** 68HC05 power manager, whose
main firmware the system ROM uploads over SPI at every boot. LCD is a fixed
640×400 grayscale GSC framebuffer. As-built: `DEV.md` § 2.11. Blueprint and
open milestones: `docs/DUO_BRINGUP.md`.

| Profile | ROM | Gate |
|---|---|---|
| PowerBook Duo 230 (68030 @ 33 MHz) | `ECFA989B` | `duo230_boot_etalon` (System 7.5.5), `msc_parity_test` |

---

## 2. The leverage — why what remains is cheap or expensive

The heavy lifting is done; remaining machines mostly re-wire existing parts.

1. **Moira: 68000 (cycle-exact) + 68020 + 68030/PMMU + 68882 + 68040/040 MMU/FPU.**
   Covers the CPU of the *entire* 68k line. 030/040 oracle-fuzzed against
   WinUAE (`oracle/`, `tests/sst68030`, `tests/sst68040`).
2. **Firmware-LLE MCUs (`M68hc05` + `CudaLle`)** running real **Egret** and
   **Cuda** 68HC05 images — the default on every ADB machine. Built once,
   reused everywhere. **This is also the Power Manager brick, already paid for**
   (`M68hc05Pge` is a clone of that core, not a subclass — different address
   width, stack window, vectors and peripherals).
3. **Twelve platforms as templates** (§ 1). New profiles inside a platform are
   identity + glue variations — the LC 475 / LC III+ / CC II / Quadra 800 / LC
   580 / Quadra 900 precedent, hours not weeks.

Reusable as-is: **VIA 6522**, **SCC 8530** (real SDLC/LLAP + baud), **NCR
5380** + pseudo-DMA, **NCR 53C96** + TurboSCSI wait-states, **IWM/SWIM1**
(real write engine) and **SWIM2** (real MFM/GCR cell engines), **ASC**
(`AscV8`/`AscSonora`/`AscIosb` stereo), **MEMCjr/PrimeTime**, **F108/PrimeTime
II**, **DAFB/Antelope**, **Valkyrie**, **Ariel** RAMDAC, **pseudo-VIA**,
**RTC**, **PIC1654S** ADB modem, **Apple PIC IOP** (`ApplePic` + `R65c02`),
**Toby NuBus + DeclRom**, the **GSC** LCD decode, and 1/2/4/8/16-bit video.

What remains outside the shipped tree is the **M50753** (a different 6502-class
MCU, for the Portable / PB 100 / 140-180) and the **AV I/O complex**.

---

## 3. Remaining — the bricks, and what each unlocks

Every remaining 68k Mac needs a **new co-processor core** (or a dump), not a
new bus. Backlog entries: `TODO.md` § 7 *Independent majors*.

| Brick | Unlocks | ROM on hand | Status |
|---|---|---|---|
| **Apple PIC IOP** (343S1021: R65C02 + 32 KB shared RAM + 2 DMA channels + host/peripheral mailboxes + timer) **+ the OSS** interrupt controller | Mac IIfx (2026-08-01), Quadra 900 / 950 (2026-08-02). `R65c02.*` + `ApplePic.*` serve the IIfx front end and both Eclipse IOPs; all three Finder gates pass | IIfx `4147DD77`; Q900 shares `420DBFF3` with the Q700; Q950 `3DC27823` | ✅ **landed** |
| **Power Manager + LCD framebuffer** — 68HC05 flavour (PB 150 / 190 / Duo / 500) or **M50753** (Mitsubishi 740 = 6502 superset; Portable, PB 100 / 140-180) | the whole portable line. **68HC05 side landed**: `M68hc05Pge.*` + `PgePmu.*` + `MscMemory.*` boot the Duo 230 to the Finder and it is the 37th profile (2026-08-06). Still open on that platform: input through the PMU's matrix/trackball path, and sleep/wake. **M50753 side untouched** | PB150 `FDA22562`; PB160-180 `E33B2724`; Duos `ECFA989B` / `0024D346` / `015621D7`; PB520/540 `B6909089`; PB190 `4D27039C`; Portable `96CA3846`; PB100 `96645F9C` | 🟢 (68HC05) / 🟡 (M50753) |
| **AV I/O complex** — advanced video + S-Video digitizer, **Curio** combo SCC/SCSI/Ethernet, **MACE**, sound codec, AV memory/DMA controller | Quadra/Centris **660AV**, **840AV** — boot **without** the DSP | `5BF10FD1` | 🟠 |
| **AT&T DSP3210 core + VCOS/ARTA** | full 660AV / 840AV DSP fidelity | — | 🔴 |

Two things worth knowing about that table:

- **The IOP was one brick that unlocked three machines, and all three are in.**
  The 65C02 core landed 2026-08-01 as `src/R65c02.*` — vendored from **POM2's
  `M6502`** (CMOS mode + full Rockwell RMB/SMB/BBR/BBS), *not* the POMIIGS
  `CPU65816` this doc first designated: 65816 emulation mode lacks the Rockwell
  bit ops MAME's R65C02 PIC core has (`applepic.h:9`).
- **The AV DSP is not the AV boot blocker.** The DSP3210 is an *offload*
  engine (audio, speech, telecom/GeoPort, video accel) — per the AV DSP FAQ it
  is not required to boot Mac OS or run the Finder. Stub it, as POM68K already
  stubs an empty NuBus, and the OS comes up; only software that explicitly
  drives the DSP is lost. The real wall is the AV I/O complex — all new gate
  arrays with no current POM68K equivalent, unlike the "plain" Quadra 610/650
  that reuse MEMCjr/DAFB/53C96. **Full DSP fidelity is deferred indefinitely**:
  a 32-bit float DSP core with its own OS, and **MAME itself does not run it
  yet** (the 660AV/840AV were the last holdout desktop 68k, still WIP in 2025).

Portables and AV are feasibility-proven, not speculative: MAME emulates the
**entire** 68k portable line — the Portable and PB100 are *fully working*
(battery/charger simulated), the 140-180 and the Duos boot.

---

## 4. Cheap, unblocked, and still not done

Not bricks — just work nobody has done. Each is hours-to-days on shipped
platforms.

| Item | Why cheap | Note |
|---|---|---|
| **128K / 512K / 512Ke** | A subset of the Plus: 64K ROM, no SCSI, less RAM. Memory/ROM config on `MacMemory`. | 128K `28BA61CE` and 512K `28BA4E50` are on hand |
| **Performa rebadges** of shipped machines | Model-ID longword only — the LC 475 / LC III+ / CC II / LC 580 precedent | `kMachineProfiles` row + a variant value |
| **Duo 210 / 250** | `MscMemory` already carries `kCpuHz210` and all three box IDs (`kIdDuo210/230/250`); they share the `ECFA989B` ROM, so they need an env selector like the Mac II group's | `MscMemory.h:53-59`; the `main.cpp:5488-5492` comment says the same |
| **Generalized NuBus + slot video** | The Mac II Toby/DeclRom port made reusable | Real cards on IIx/IIcx/IIci/IIsi/VASP and the NuBus Quadras. The IIfx, which has no built-in video, already boots on `TobyVideo` in slot 9 |
| **ATA/IDE target on the Quadra 630 / LC 580** | The port is mapped (`Q630Memory.h:27`), it just has no drive | The remaining gap on that board; boot currently goes over SCSI |

**Orthogonal, blocks no boot: pin-level 68040 timing** — the integrated FPU
implements the hardware subset and delegates unsupported operations
(`FSIN`/`FTAN`, etc.) to the guest **FPSP**. The 4 KB I/D caches now carry
data, implement writethrough/copyback/NC modes, dirty replacement and
CPUSH/CINV, expose alternate-master snooping, and charge configurable
hit/fill/push transaction costs (`POM68K_040_DCACHE=1`). Cache-aware native
line reads are conformant and measured (-21.2 % on the fixed-budget Q605
control), but the mode remains opt-in because the cache-on real-ROM gate still
misses its Finder budget and costs far more than the calibrated cacheless
path. What is
not modelled is an electrical BCLK/TA waveform; board wait states remain in
the memory callbacks. See `docs/CACHE_040.md`.

---

## 5. The caveat on "done" — depth, not breadth

Booting to the Finder is the *entry* criterion, not the finish line.
Re-derived from `CMakeLists.txt` on 2026-08-12:

- **37 of 37** profiles have a Finder boot gate.
- **9 of 37** have any gate *past* the boot signature: Plus (`input_etalon`),
  Mac II (`macii_mouse_etalon`), LC II (soak / persist / launch / floppy /
  savestate), LC III, LC 520, IIsi (`*_input_etalon`), IIvx (input + soak +
  persist), Quadra 605 (OT bind, CD-ROM ×3, floppy, mouse, key, soak, persist,
  savestate), IIfx (`iifx_input_etalon`). **28 profiles are proven only to the
  point where the Finder appears** — including every 2026-08 arrival.
- **15 of 37** are additionally gated on the **second execution engine**
  (`jit_*_boot_etalon`: q605, centris650, q630, q700, lcii, mactv, lc3, iivx,
  iisi, lc, macii, se30, system — the Plus — and iifx from the `foreach` at
  `CMakeLists.txt:1513-1554`, plus `jit_classic_boot_etalon` registered on its
  own at `:1657` because it shares the compact binary); four of those also
  carry an explicit `interp_*_boot_etalon` interpreter reference (q605,
  centris650, q630, q700).

Adding a 38th machine is cheaper than hardening the 37 that exist. Read the
roadmap below against that trade — and against `TODO.md` § 2, which calls test
depth the single biggest gap in the project.

---

## 6. Roadmap (cheapest unlock first)

Phase A/B/C — Plus → Mac II → the V8/Sonora/VASP/RBV/040 fan-out — are **done**,
and so are the two platforms this section used to call unfinished (§ 1). What
is left, in return-on-effort order:

1. **Depth over breadth** — beyond-boot gates on the 28 profiles that have
   none (§ 5). Not a new machine, and the highest-value item on this page.
2. **Finish the Duo platform**: input through the PMU's matrix keyboard and
   trackball counters (`duo230_input_etalon`), then the **sleep/wake gate no
   other machine in the tree can run** (`docs/DUO_BRINGUP.md` milestones 4
   and 6). The brick is paid for; this is the product work.
3. **Generalize NuBus** → real slot cards on IIx/IIcx/IIci/IIsi/VASP and the
   NuBus Quadras. (No longer a prerequisite for the IIfx: it ships on
   `TobyVideo` in slot 9.)
4. **More 68HC05 portables** (PB150, the other Duos, the 500 series) — reuses
   the shipped `M68hc05`/`PgePmu` stack, the 030/040 CPUs and the GSC decode.
5. **AV boot without the DSP** — the AV I/O complex (video / Curio / MACE /
   codec) at functional accuracy, DSP stubbed.
6. **Deferred / separate frontiers**: M50753 portables (need a 740/6502 core,
   borrowable from the POMIIGS/POM2 siblings), full AV DSP fidelity (no
   reusable core; MAME itself doesn't run it), and full 68040 cache/FPSP
   accuracy.

Free (ROM already on hand): the **128K/512K** configs, the Duo 210/250 clock
variants and any Performa rebadge (§ 4). The **SE/30** cashed this tier in on
2026-07-31 — wiring only, Finder on the first run.

---

## Sources

- [List of Mac models grouped by CPU type — Wikipedia](https://en.wikipedia.org/wiki/List_of_Mac_models_grouped_by_CPU_type)
- [Timeline of 680x0 Computers — Low End Mac](https://lowendmac.com/2015/timeline-of-680x0-computers/)
- [Macintosh II Family Technical Overview](https://www.angelfire.com/ca2/tech68k/macii.html)
- [Entry Level Family Technical Overview (LC / Egret / V8)](https://www.angelfire.com/ca2/tech68k/entry.html)
- [Apple I/O Notes — chip families (Egret/Cuda/Sonora/SWIM)](https://mcosre.sourceforge.net/docs/apple_io.html)
- [Apple Computer Custom IC Definitions — Higher Intellect Wiki](https://wiki.preterhuman.net/Apple_Computer_Custom_IC_Definitions)
- [Macintosh IIfx (OSS + IOPs) — Higher Intellect Wiki](https://wiki.preterhuman.net/Macintosh_IIfx)
- [Moira 68k emulator — Dirk W. Hoffmann (upstream cores: 68000/010/EC020/020)](https://dirkwhoffmann.github.io/Moira/)

Portables / AV feasibility research (2026-07-24):

- [MAME 2023 Mac emulation updates — E-Maculation](https://www.emaculation.com/forum/viewtopic.php?t=12050) — Portable + PB100 *fully working* via the M50753 (m5074x) core; Duo Power Manager run as real 68HC05 code.
- [MAME 2024/2025 Mac emulation updates — E-Maculation](https://www.emaculation.com/forum/viewtopic.php?t=12370) — 660AV/840AV WIP, the last holdout desktop 68k.
- [Driver:Mac 68K — MAMEdev Wiki](https://wiki.mamedev.org/index.php/Driver:Mac_68K) — per-model boot status (PowerBooks, Duos, IIfx A/UX-only, SE/30, IIci).
- [AV DSP Mini-FAQ (funet)](https://ftp.funet.fi/pub/mac/info-mac/info/hdwr/av-dsp-faq-101.txt) — the DSP3210 drives optional audio/speech/telecom/video, not the boot path.
- [DSP3210 programming — 68kMLA](https://68kmla.org/bb/threads/dsp3210-programming.37581/); [Motorola 68HC05 — Wikipedia](https://en.wikipedia.org/wiki/Motorola_68HC05)
