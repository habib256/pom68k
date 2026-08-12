# 68K Macintosh family — support scope & effort

Which classic 68K Macs POM68K ships, and what each remaining one costs.
Written 2026-07-17; the Phase C fan-out (2026-07-24/25) obsoleted most of it,
and this pass (**2026-08-01**) re-derived every line from the code.

**POM68K ships 36 machine profiles across 11 machine families. Every one boots
to the Finder and every one has a gate that proves it.** Sources of truth for
that count: `SnapMachine` (`src/SaveStateMachines.h:49`, 36 tags) and
`kProfiles[]` (`src/main.cpp:792`, one row per profile, which is also the
**Machine** menu).

Two platforms exist in `src/` **without** a profile row, by the house rule
that a `kProfiles` line is earned by a Finder cell *plus* GUI and save-state
wiring: the **PowerBook Duo 230** (MSC + PG&E — boots, `duo230_boot_etalon`)
and the **Quadra 900/950** (Eclipse, inside `Q700Memory` — POSTs, boot still
open). They are counted in § 3, not in the 34.

**The headline for scope: all CPU cores exist** — 68000 cycle-exact,
68020/68030+PMMU/68040+MMU functional and oracle-fuzzed against WinUAE, plus
the 68882. Nothing left in the family is blocked by the CPU. **Every remaining
machine is a co-processor problem**: one of four bricks in § 3, or a ROM dump.

Per-subsystem status lives in `CLAUDE.md`; the LLE-vs-HLE deviation inventory
in `docs/LLE_VS_HLE.md`; the backlog in `TODO.md` § 7.

---

## 1. Done — the 36 profiles and the gate that proves each

Grouped by machine family (= one `*Memory`/`*Cpu` pair in `src/`, and one
save/load overload in `src/SaveStateMachines.h`). Every gate below is a
Finder-signature boot etalon unless noted.

### 68000 — `MacMemory` / `Cpu68k` (cycle-exact)

The compacts turned out to be a `MacMemory::Model` enum, not a machine: the SE
map is the Plus map with a bigger ROM, the overlay clearing on the first ROM
access, and **ADB on the same PIC1654S firmware LLE the Mac II uses** (PB4/PB5
= ST) in place of the M0110.

| Profile | ROM | Gate |
|---|---|---|
| Macintosh Plus | `macplus.rom` | `system_boot_etalon`, `disk_boot_etalon`, `scsi_boot_etalon`, `rom_boot_etalon`, `input_etalon` |
| Macintosh SE | `B2E362A8` | `se_boot_etalon` |
| Macintosh SE FDHD | `B306E171` | `sefdhd_boot_etalon` |
| Macintosh Classic | `A49F9914` | `classic_boot_etalon` |

### GLUE + Toby NuBus — `MacIIMemory` / `Cpu020`

`MacIIMemory::Model` + `Cpu020`'s `is030` flag. The IIx/IIcx wall was the 030
PMMU double-translating against the GLUE 24-bit remap — skip `physAddr` when
the PMMU is on.

| Profile | CPU | ROM | Gate |
|---|---|---|---|
| Macintosh II | 68020 | `9779D2C4` | `macii_boot_etalon`, `macii_sys7_boot_etalon`, `macii_post_etalon`, `macii_mouse_etalon` |
| Macintosh IIx | 68030 + PMMU | `97221136` | `iix_boot_etalon` |
| Macintosh IIcx | 68030 + PMMU | `97221136` | `iicx_boot_etalon` |
| Macintosh SE/30 | 68030 + PMMU, compact 512×342 mono (`Se30Video.h`) | `97221136` + `se30vrom.uk6` | `se30_boot_etalon` |

### OSS + two Apple PIC IOPs — `IIfxMemory` / `IIfxCpu`

The one platform with **no VIA2 and no built-in video**: the OSS interrupt
controller replaces VIA2, and two Apple PIC (343S1021) IOPs — each an R65C02
running firmware the host ROM downloads at boot — front the SCC and the SWIM.
ADB is bit-banged by the SWIM IOP's own firmware against `AdbLine`; video is
`TobyVideo` on slot 9. Blueprint: `docs/IOP_BRINGUP.md`.

| Profile | ROM | Gate |
|---|---|---|
| Macintosh IIfx (68030 @ 40 MHz) | `4147DD77` | `iifx_boot_etalon` (System 7.6), `iifx_post_etalon`, `iifx_input_etalon`; device gates `applepic_test`, `r65c02_test` |

### RBV (RAM-Based Video) — `RbvMemory` / `RbvCpu` / `RbvVideo`

The ancestor of the whole V8/VASP/Sonora line: framebuffer at RAM start,
Bt478 CLUT, SWIM1, discrete ASC. No i-cache boost — the Egret bit-bang is
host-paced.

| Profile | Distinguishing | ROM | Gate |
|---|---|---|---|
| Macintosh IIci | 25 MHz, PIC1654S ADB modem LLE + discrete 343-0042 RTC + empty NuBus | `368CADFE` | `iici_boot_etalon` |
| Macintosh IIsi | 20 MHz, Egret 344S0100 LLE | `36B7FB6C` | `iisi_boot_etalon`, `iisi_input_etalon` |

### V8 / Eagle / Spice / Tinker Bell — `V8Memory` / `Cpu030`

One gate array with a `Model` enum, a `spiceClass()` predicate and a `cpuHz`
ctor parameter.

| Profile | CPU / ASIC | ROM | Gate |
|---|---|---|---|
| Macintosh LC | 68020 + HMMU, V8 | `350EACF0` | `lc_boot_etalon` |
| Macintosh LC II | 68030 + MMU + 68882, V8 | `35C28F5F` | `lcii_boot_etalon`, `lcii_sys7_boot_etalon`, `lcii_soak/persist/launch/floppy_etalon`, `lcii_savestate_etalon` |
| Macintosh Classic II | 68030, Eagle (mono 512×342 from RAM) | `3193670E` | `classic2_boot_etalon` |
| Macintosh Color Classic | 68030, Spice + Cuda 341S0417 LLE | `ECD99DC0` | `cclassic_boot_etalon` |
| Macintosh TV | 68030 @ 31.3 MHz, Tinker Bell + Cuda LLE | `EAF1678D` | `mactv_boot_etalon` |

### Sonora — `SonoraMemory` / `SonoraCpu` / `SonoraVideo`

One machine, two MCU flavours selected by the `cudaAdb` flag: Egret 341S0851
on the LC III boards, **Cuda 341S0060** on the EDE66CBD all-in-ones (2.40 —
2.37 livelocks on pseudo-cmd `$0E`). Bring-up story: `docs/LC520_BRINGUP.md`.

| Profile | Distinguishing | ROM | Gate |
|---|---|---|---|
| Macintosh LC III | 25 MHz, Egret | `ECBBC41C` | `lc3_boot_etalon`, `lc3_input_etalon` |
| Macintosh LC III+ | 33 MHz, Egret | `ECBBC41C` | `lc3plus_boot_etalon` |
| Macintosh LC 520 | 25 MHz, Cuda, sense 6 → 640×480×8 | `EDE66CBD` | `lc520_boot_etalon`, `lc520_input_etalon` |
| Macintosh LC 550 | 33 MHz, Cuda | `EDE66CBD` | `lc550_boot_etalon` |
| Color Classic II | same board, sense 2 → 512×384 | `EDE66CBD` | `cclassic2_boot_etalon` |

### VASP — `VaspMemory` / `VaspCpu` / `VaspVideo`

"V8 video on Sonora addressing": Sonora shell + V8 peripherals (AscV8, SWIM1,
Ariel, 2048-byte pitch), Egret 341S0851. Empty NuBus reads MAME-unmapped 0.

| Profile | Clock | ROM | Gate |
|---|---|---|---|
| Macintosh IIvx | 31.3344 MHz | `4957EB49` | `iivx_boot_etalon`, `iivx_input_etalon` |
| Macintosh IIvi | 15.6672 MHz | `4957EB49` | `iivi_boot_etalon` |

### MEMCjr + PrimeTime + DAFB — `Q605Memory` / `Cpu040`

68040 / 68LC040 + 040 MMU, Cuda firmware LLE, NCR 53C96, SWIM2.

| Profile | CPU | ROM | Gate |
|---|---|---|---|
| Macintosh LC 475 | 68LC040 | `FF7439EE` | `lc475_boot_etalon` |
| Macintosh LC 575 | 68LC040, 33 MHz | `FF7439EE` | `lc575_boot_etalon` |
| Quadra 605 | 68040 + soft 68882 | `FF7439EE` | `q605_boot_etalon`, `q605_nofpu_/barefpu_/floppy_/cudalle_boot_etalon`, `q605_cudalle_mouse_/key_etalon`, `q605_ot_bind_etalon`, `q605_cdrom_/cdboot_etalon`, `q605_savestate_etalon` |

### djMEMC + IOSB — `CentrisMemory` / `CentrisCpu`

Q605 devices (DAFB, 53C96, SWIM2, AscIosb, PseudoVia) + discrete `Rtc` +
PIC1654S ADB LLE. The one wall was djMEMC's 2 MB VRAM window vs MEMCjr's 1 MB.
The Quadra 800 needed only its ID pins (`$12`) and the Ethernet address ROM at
`$50008000`; SONIC and its NuBus slots stay unmapped-0 and the boot path never
binds them.

| Profile | Gate |
|---|---|
| Macintosh Centris 610 (20 MHz) | `centris610_boot_etalon` |
| Macintosh Centris 650 | `centris650_boot_etalon` |
| Macintosh Quadra 610 | `quadra610_boot_etalon` |
| Macintosh Quadra 650 (33 MHz) | `quadra650_boot_etalon` |
| Macintosh Quadra 800 (33 MHz) | `quadra800_boot_etalon` |

All five share ROM `F1A6F343`.

### Discrete 040 ("Spike") — `Q700Memory` / `Q700Cpu`

The first *discrete* 040 machine: Mac II VIA1/VIA2 + RTC + PIC ADB in front,
Quadra DAFB/53C96/SWIM1/EASC behind, SCSI through **DAFB's own TurboSCSI cell**.
NuBus unpopulated.

| Profile | ROM | Gate |
|---|---|---|
| Macintosh Quadra 700 | `420DBFF3` | `q700_boot_etalon` |

### F108 + PrimeTime II + Valkyrie — `Q630Memory` / `Q630Cpu` / `Valkyrie`

The last 68k desktop board. Fixed-mode Valkyrie framebuffer, Cuda 341S0060,
68040 @ 33 MHz. The ATA/IDE port is mapped but has no drive, so boot goes over
SCSI (§ 4).

| Profile | ROM | Gate |
|---|---|---|
| Macintosh Quadra 630 (33 MHz) | `06684214` | `q630_boot_etalon` |
| Macintosh LC / Performa 580 | `06684214` | `lc580_boot_etalon` |

---

## 2. The leverage — why what remains is cheap or expensive

The heavy lifting is done; remaining machines mostly re-wire existing parts.

1. **Moira: 68000 (cycle-exact) + 68020 + 68030/PMMU + 68882 + 68040/040 MMU.**
   Covers the CPU of the *entire* 68k line. 030/040 oracle-fuzzed against
   WinUAE (`oracle/`, `tests/sst68030`, `tests/sst68040`).
2. **Firmware-LLE MCUs (`M68hc05` + `CudaLle`)** running real **Egret** and
   **Cuda** 68HC05 images — the default on every ADB machine. Built once,
   reused everywhere. **This is also the Power Manager brick, already paid for**
   (§ 3).
3. **Eleven machine families as templates** (§ 1). New profiles inside a family are
   identity + glue variations — the LC 475 / LC III+ / CC II / Quadra 800 / LC
   580 precedent, hours not weeks.

Reusable as-is: **VIA 6522**, **SCC 8530** (real SDLC/LLAP + baud), **NCR
5380** + pseudo-DMA, **NCR 53C96** + TurboSCSI wait-states, **IWM/SWIM1**
(real write engine) and **SWIM2** (real MFM/GCR cell engines), **ASC**
(`AscV8`/`AscSonora`/`AscIosb` stereo), **MEMCjr/PrimeTime**, **F108/PrimeTime
II**, **DAFB/Antelope**, **Valkyrie**, **Ariel** RAMDAC, **pseudo-VIA**,
**RTC**, **PIC1654S** ADB modem, **Toby NuBus + DeclRom**, and 1/2/4/8-bit
video.

Both of the things this section listed as missing have since landed: the
**6502-class core** (`R65c02.*`, 2026-08-01, vendored from POM2's `M6502`) and
the **LCD framebuffer** (`MscMemory::decodeScreen`, 2026-07-31). What remains
outside the shipped tree is the **M50753** (a different 6502-class MCU, for the
Portable / PB 100 / 140-180) and the **AV I/O complex**.

---

## 3. Remaining — four bricks, and what each unlocks

Every remaining 68k Mac needs a **new co-processor core** (or a dump), not a
new bus. Backlog entries: `TODO.md` § 7 *Independent majors*.

| Brick | Unlocks | ROM on hand | Difficulty |
|---|---|---|---|
| **Apple PIC IOP** (Apple 343S1021: 65C02 core + 2 KB shared RAM + 2 DMA channels + host/peripheral mailboxes + timer — MAME `machine/applepic.cpp`) **+ the OSS** interrupt controller | **LANDED**: Mac IIfx (2026-08-01), then Quadra 900 / 950 (2026-08-02). `R65c02.*` + `ApplePic.*` serve the IIfx front end and both Eclipse IOPs; all three Finder gates pass. | IIfx `4147DD77`; Q900 shares `420DBFF3` with the Q700; Q950 `3DC27823` | ✅ |
| **Power Manager + LCD framebuffer** — 68HC05 flavour (PB 150 / 190 / Duo / 500) or **M50753** (Mitsubishi 740 = 6502 superset; Portable, PB 100 / 140-180) | the whole portable line. **68HC05 side LANDED 2026-07-31**: `M68hc05Pge.*` + `PgePmu.*` + `MscMemory.*` boot the **Duo 230** to the Finder (`duo230_boot_etalon`); still no GUI profile, no input, no sleep/wake. **M50753 side untouched** | PB150 `FDA22562`; PB160-180 `E33B2724`; Duos `ECFA989B` / `0024D346` / `015621D7`; PB520/540 `B6909089`; PB190 `4D27039C`; Portable `96CA3846`; PB100 `96645F9C` | 🟢 (68HC05) / 🟡 (M50753) |
| **AV I/O complex** — advanced video + S-Video digitizer, **Curio** combo SCC/SCSI/Ethernet, **MACE**, sound codec, AV memory/DMA controller | Quadra/Centris **660AV**, **840AV** — boot **without** the DSP | `5BF10FD1` | 🟠 |
| **AT&T DSP3210 core + VCOS/ARTA** | full 660AV / 840AV DSP fidelity | — | 🔴 |
| **Wiring only** | **SE/30** — compact Mac IIx (`MacIIMemory` + compact video) — **LANDED 2026-07-31** as the 33rd profile (`Se30Video.h`, gate `se30_boot_etalon`; the estimate held: Finder on the first run) | `97221136` + `se30vrom.uk6` | ✅ |

The three things worth knowing about that table:

- **The IOP was one brick that unlocked three machines, and two of them are
  in.** The 65C02 core **landed 2026-08-01** as `src/R65c02.*` — vendored from
  **POM2's `M6502`** (CMOS mode + full Rockwell RMB/SMB/BBR/BBS), *not* the
  POMIIGS `CPU65816` this doc first designated: 65816 emulation mode lacks the
  Rockwell bit ops MAME's R65C02 PIC core has (`applepic.h:9`). The IIfx
  shipped the same day; the Q900/950 stall on one control-flow divergence in
  the SWIM IOP's device model. Remaining milestones: `docs/IOP_BRINGUP.md`.
  Note the IIfx has **no built-in video**: it boots on a NuBus card, so
  `TobyVideo`/`DeclRom` carries it (§ 4).
- **A 68HC05 PowerBook was indeed easier than the IIfx** — the estimate held.
  The PM is the *same 68HC05 core POM68K already ships*, running real firmware
  (uploaded mid-boot as BORG v2, the same "no dump needed" pattern as the IOP),
  and the LCD decode was a flat framebuffer as predicted. The **Duo 230**
  booted 2026-07-31, ahead of the IIfx. What is left there is not the brick but
  the product work: input through the PMU, a GUI profile, save states, and the
  sleep/wake gate no other machine can run (`docs/DUO_BRINGUP.md`). The 500
  series is 68LC040 (CPU done, Q605 path); PB150 is the cheapest remaining
  entry.
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
| **Performa rebadges** of shipped machines | Model-ID longword only — the LC 475 / LC III+ / CC II / LC 580 precedent | `kProfiles[]` row + an env value |
| **Generalized NuBus + slot video** | The Mac II Toby/DeclRom port made reusable | Real cards on IIx/IIcx/IIci/IIsi/VASP and the NuBus Quadras. The IIfx, which has no built-in video, already boots on `TobyVideo` in slot 9 |
| **ATA/IDE target on the Quadra 630 / LC 580** | The port is mapped, it just has no drive | The remaining gap on that board; boot currently goes over SCSI |

**Orthogonal, blocks no boot: full architectural 68040 fidelity** — i/d caches,
copyback/snooping, and the real partial on-chip FPU where transcendentals
(`FSIN`/`FTAN`) trap to the software **FPSP**. The current 040 uses Moira's
68882 model plus an i-cache throughput overlay. A separate CPU project;
software that observes those details needs it, nothing currently blocks on it.

---

## 5. The caveat on "done" — depth, not breadth

Booting to the Finder is the *entry* criterion, not the finish line. Re-derived
from `ctest -N` on 2026-08-01:

- **34 of 34** profiles have a Finder boot gate.
- **9 of 34** have any gate *past* the boot signature: Plus (`input_etalon`),
  Mac II (`macii_mouse_etalon`), LC II (soak / persist / launch / floppy /
  savestate), LC III, LC 520, IIvx, IIsi (`*_input_etalon`), Quadra 605
  (OT bind, CD-ROM, floppy, mouse, key, savestate), IIfx
  (`iifx_input_etalon`). **25 profiles are proven only to the point where the
  Finder appears** — including both 2026-07-31/08-01 arrivals' siblings.
- **10 of 34** are additionally gated on the **second execution engine**
  (`jit_*_boot_etalon`: q605, centris650, q630, q700, lcii, mactv, lc3, iivx,
  iisi, lc).

Adding a 35th machine is cheaper than hardening the 34 that exist. Read the
roadmap below against that trade — and against `TODO.md` § 2, which calls test
depth the single biggest gap in the project.

---

## 6. Roadmap (cheapest unlock first)

Phase A/B/C — Plus → Mac II → the V8/Sonora/VASP/RBV/040 fan-out — are **done**
(§ 1). What is left, in return-on-effort order:

1. **Depth over breadth** — beyond-boot gates on the 25 profiles that have
   none (§ 5). Not a new machine, and the highest-value item on this page.
2. **Finish the two platforms that already boot-or-almost**: the **Duo 230**
   needs input through the PMU, a `kProfiles` row, save states and the
   sleep/wake gate; the **Quadra 900/950** needs one control-flow divergence
   in the SWIM IOP's device model closed (`TODO.md` § 0). Both are cheaper
   than any new brick — the bricks are paid for.
3. **Generalize NuBus** → real slot cards on IIx/IIcx/IIci/IIsi/VASP and the
   NuBus Quadras. (No longer a prerequisite for the IIfx: it ships on
   `TobyVideo` in slot 9.)
4. **More 68HC05 portables** (PB150, the other Duos, the 500 series) — reuses
   the shipped `M68hc05`/`PgePmu` stack and the 030/040 CPUs; the LCD
   framebuffer is done too.
5. **AV boot without the DSP** — the AV I/O complex (video / Curio / MACE /
   codec) at functional accuracy, DSP stubbed.
6. **Deferred / separate frontiers**: M50753 portables (need a 740/6502 core,
   borrowable from the POMIIGS/POM2 siblings), full AV DSP fidelity (no
   reusable core; MAME itself doesn't run it), and full 68040 cache/FPSP
   accuracy.

Free (ROM already on hand): the **128K/512K** configs and any Performa
rebadge (§ 4). The **SE/30** cashed this tier in on 2026-07-31 — wiring only,
Finder on the first run.

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
