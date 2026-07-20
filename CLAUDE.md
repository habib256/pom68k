# CLAUDE.md

Orientation **always-loaded index** — keep terse, defer detail to other docs.

POM68K is a **Macintosh 68k** emulator: **Mac Plus** (68000, cycle-exact),
**Mac LC II** (68030 + MMU + 68882, functional accuracy), and **Quadra 605 /
LC 475 profile** (68040/68LC040 + 040 MMU, functional accuracy). It is the 68k
sibling of [POMIIGS](../POMIIGS/) and reuses its architecture, conventions and
milestone discipline; the CPU integration pattern comes from
[NeoST](../neost/) (Moira wrapper, see below).

- `README.md` — user walkthrough (build, ROM placement, keys, CLI).
- `DEV.md` — implementation deep-dives (references, internals, pinned tests).
- `TODO.md` — active backlog + milestone roadmap (polish, fidelity gaps,
  future machine profiles).
- `CHANGELOG.md` — resolved items + the **why** behind non-obvious fixes.
- `docs/` — LC II research: `LCII_HARDWARE.md` (machine blueprint),
  `BASILISK_ROM_NOTES.md` ($067C ROM-behaviour oracle; §8 = facts
  verified on the real LC II ROM with `tools/rominfo`);
  `68K_FAMILY_SCOPE.md` (which other 68K Macs POM68K could support
  later and at what effort); `HLE_OVERLAY.md` (design study — opt-in
  HLE accelerator layered on the LLE core, non-conformant mode).

## CPU core: Moira (vendored)

`extern/moira/` is **vendored from NeoST** (itself from Dirk Hoffmann's Moira,
MIT), with NeoST's patches included — provenance and every local change in
`extern/moira/POM68K_VENDOR.md`. Config: `MOIRA_PRECISE_TIMING`,
`MOIRA_EMULATE_ADDRESS_ERROR`, no Musashi mimicry. This copy executes
68000/68010 cycle-exact and 68020, **68030 + PMMU**, and **68040/68LC040 + 040
MMU** functionally. The 030/040 extensions were built from the Motorola manuals
with AI + differential fuzzing against the vendored **WinUAE/Hatari oracle**;
Musashi was retired after losing every 030 arbitration. On spec/oracle conflict,
**the oracle wins**. The 040 path has an ATC fast path and a throughput/i-cache
overlay (`POM68K_Q605_CACHE_BOOST`, default 1 — boost 2+ fails SCSI
bring-up under `q605_boot_etalon`); no architectural
copyback/snooping yet.

## Source of truth (ranked, cite file + line range in comments)

1. **MAME `mac.cpp` + `m68000` family** — primary hardware reference.
2. **Guide to the Macintosh Family Hardware** + *Inside Macintosh III* — docs.
3. **Hatari/WinUAE 68k timing** (via NeoST's convergence docs) — CPU timing.
4. **pce-macplus / minivmac / Basilisk II** — behavioural cross-checks.

## Conventions (inherited from POMIIGS/POM2)

- **One concern per file** — each `.cpp/.h` pair owns one subsystem.
- **Every milestone is gated** by a CTest under `tests/` before the next
  depends on it.
- **`emuCycles` everywhere** — events carry CPU-cycle stamps, never wall-clock.
- **Docs in English**; conversation with the user may be French.
- **C++20** whole-tree (Moira requires it — divergence from POMIIGS's C++17).
- **License GPLv3**; ROMs are **user-provided, never committed**.

## Build & run

```bash
./setup_imgui.sh             # one-time: fetches Dear ImGui + creates build/
cd build && cmake .. && make -j   # → build/POM68K + tests
ctest                        # 33 gates (asset-dependent gates may soft-skip)
./POM68K [ROM] [media...]    # 128K=Plus, 512K=LC II, 1MB=Quadra 605
```

Mac Plus: CPU **7.8336 MHz**, frame **60.15 Hz** (130 240 cycles/frame), video
**512×342 × 1 bpp**, MSB = leftmost, 1 = black. LC II runs at **15.6672 MHz**
(default 640×480 V8 video; `POM68K_MONITOR=512` → 512×384). Quadra profile
runs at **25 MHz** with 640×480 DAFB video. Default Quadra CPU is full
**68040 + soft 68882** (MAME `macqd605`); `POM68K_Q605_NOFPU=1` selects
68LC040 + soft 68882. Bare `FPUModel::NONE` still hits dsNoFPU 90 pending
real FPSP.

## Subsystem map

| Subsystem | Files | Status | Source |
|---|---|---|---|
| **68000 CPU** (Moira wrapper + contention) | `Cpu68k.h/.cpp` | M1/M4 ✓ | NeoST pattern; GttMFH timing |
| **Memory map + overlay** | `MacMemory.h/.cpp` | M2/M4 ✓ | MAME `mac128.cpp` |
| **VIA 6522** (ports, timers, IFR/IER) | `Via6522.h/.cpp` | M4 ✓ (±1-cycle latency deferred) | MAME `via6522.cpp` |
| **RTC 343-0042** (clock + PRAM serial) | `Rtc.h/.cpp` | M4 ✓ (no file persistence) | Mini vMac RTC.c |
| **Video 512×342** | `MacVideo.h` | M3 ✓ (whole-frame decode) | GttMFH |
| **Frame clock** (VBL phase, one-second) | `MacFrame.h` | M4 ✓ | GttMFH |
| **Sound** (PWM buffer + chime) | `MacAudio.h`, `MacAudioHost.h` | M6 ✓ | GttMFH |
| Built-in demo ROM | `DemoRom.h` | ✓ (gate vehicle) | — |
| UI (ImGui/GLFW, turbo, Machine/Disques) | `main.cpp` | M3 shell ✓ | POMIIGS main.cpp |
| **SST 68000 harness** | `tests/sst68000.cpp` | M4.5 ✓; 1 000 058 vectors | SingleStepTests/680x0 |
| **IWM + Sony 3.5" 800K GCR** | `Iwm.h/.cpp`, `SonyDrive.h/.cpp` | M5 ✓ | MAME `iwm.cpp`/`ap_dsk35.cpp` |
| **Keyboard (M0110) + mouse** | `MacInput.h/.cpp`, `Scc8530.h/.cpp` | M5.5 ✓ | MAME/Mini vMac/Snow |
| **SCSI NCR 5380 + hard disk** | `Ncr5380.h/.cpp`, `ScsiDisk.h/.cpp` | M7 ✓ | MAME `ncr5380.cpp`, pce |
| SCC 8530 serial ports | `Scc8530.*` (mouse DCD + LC II LAP ext ints) | M7.1 / O6.10 | POMIIGS `Scc8530` reuse |
| **68030 oracle + fuzz loop** | `oracle/` (api, uae, fuzz) | O1-O3 ✓ (Musashi retired) | WinUAE (Hatari) |
| **68030 core + MMU (LC II)** | `extern/moira` extension | O4 ✓ | MC68030UM + WinUAE oracle |
| **68882 FPU** (softfloat, `setFPUModel`) | `extern/moira` FPU + `extern/softfloat/` | O5 ✓ | MC68881/882UM; WinUAE fpp.c |
| **68040 oracle + core + MMU** | `oracle/`, `extern/moira`, `tests/sst68040.cpp` | Q1-Q4 ✓; 7 200 pinned vectors | MC68040UM + WinUAE oracle |
| **LC II machine** (V8/Egret/ASC/Ariel) | `V8Memory.*`, `Cpu030.*`, `Egret.*`, `AdbBus.*`, `Asc.*`, `V8Video.h` | O6 ✓; Finder | MAME `maclc.cpp` + LC II ROM |
| **Quadra 605 machine** (MEMCjr/PrimeTime/Cuda/DAFB) | `Q605Memory.*`, `Cpu040.*`, Cuda via `Egret` flavor | Q5-Q7 ✓; Mac OS 8.1 Finder | MAME `macquadra605.cpp` |
| **NCR 53C96 TurboSCSI** | `Ncr53c96.*` | Q6 ✓; PIO + pseudo-DMA | MAME `ncr53c90.cpp` + ROM/OS 8 |
| **DAFB/Antelope video** | `Q605Memory.*` (stride/depth/CLUT HLE) | Q8.1 ✓; 640×480×8 Finder gated | MAME `dafb.cpp` |
| **IOSB ASC stereo** | `Asc.*` (`AscIosb`) | Q8 ✓; `$BB` FIFO/IRQ gated | MAME IOSB / ASC |
| **SWIM2 + SuperDrive** | `Swim2.*`, `SonyDrive.*` | ✓; GCR+MFM media gated | MAME SWIM2 |
| **Pseudo-VIA2** | `PseudoVia.*` | ✓ | MAME IOSB VIA2 layout |

## Memory map (Mac Plus, 24-bit)

```
$000000-$3FFFFF  RAM, mirrors modulo size (ROM overlay here while booting)
$400000-$4FFFFF  ROM 128 KB (mirrored)
$580000-$5FFFFF  SCSI NCR 5380 (reg = A4-A6; A0: 0=read 1=write; A9 = DACK)
$600000-$7FFFFF  RAM (while overlay on)
$800000-$9FFFFF  SCC read, even bytes ($9FFFF8; A1 = channel, A2 = ctl/data)
$A00000-$BFFFFF  SCC write, odd bytes ($BFFFF9)
$C00000-$DFFFFF  IWM, odd bytes ($DFE1FF, regs every $200)
$E80000-$EFFFFF  VIA, even bytes ($EFE1FE, regs every $200; PA4 = overlay,
                 PA6 = screen buffer 1=main, PA3 = sound buffer)
Framebuffer: main = ramTop-$5900 ($3FA700 @ 4 MB), alt = ramTop-$D900
Sound buffer: main = ramTop-$300, alt = ramTop-$5F00 (370 words/frame)
```
Full pinned detail + timing/contention model in `DEV.md`. LC II (V8) and
Quadra (MEMCjr/PrimeTime) maps live in `V8Memory.h` / `Q605Memory.h`.

## Status

**Mac Plus is a usable machine (M0–M7 done).** It boots System 6 from a
floppy *and* from a SCSI hard disk to the Finder, the mouse/keyboard drive
it, and the startup chime plays. Moira passes 1 000 058/1 000 058
accepted SingleStepTests 68000 vectors. Remaining Plus polish in
`TODO.md` (floppy write, serial, cycle-accurate sound, PRAM file
persistence, save states, WASM).

**Phase 2 (Mac LC II): the 68030+MMU+FPU CPU side is done (O1-O5).** The
WinUAE/Hatari 68030 oracle runs behind a C API (`oracle/oracle_api.h`),
fuzzed with real MMU tables (`oracle/fuzz/`, SST030 format), replayed
against Moira (`tests/sst68030`, **3 082 pinned vectors**: integer + MMU
instrs + bus/ATC/fault frames + 68882 FPU, rulings D1-D22). Musashi was
retired 2026-07-15 (0 arbitrations won) — the loop is **WinUAE-solo with
manual arbitration** (`oracle/fuzz/disputes/NOTES.md`).
**O6 boots classic Mac OS to the Finder** off real disk images (GISTPERSO /
System 7.5): V8 gate array (`V8Memory`), Egret HLE (`Egret`/`AdbBus`),
ASC-V8 sound (`Asc`), V8 video + Ariel (`V8Video`), SCSI pseudo-DMA over
the reused `Ncr5380`, SWIM1 GCR over the reused `Iwm`, 68030+PMMU+68882
via the O1-O5 core. Remaining LC II gaps in `TODO.md` (no-FPU SANE, 1.44 MB
SWIM, DFAC audio polish, bus/timing).

**Phase 3 (Quadra 605) reaches a usable Finder desktop:** Q1-Q4
68040/040-MMU core drives MEMCjr/PrimeTime, Cuda HLE (Egret flavor),
DAFB/Antelope (Q8.1 stride/depth/CLUT), IOSB ASC stereo (`AscIosb`),
SWIM2 SuperDrive, and NCR 53C96 SCSI; Mac OS 8.1 boots at 640×480×8.
GUI exposes the machine alongside Plus/LC II. **34 CTest gates**, including
`lcii_boot_etalon`, `macii_boot_etalon`, `macii_sys7_boot_etalon`,
`sst68040`, `q605_boot_etalon`, `q605_dafb_test`, `q605_asc_test`,
`swim2_test`, `swim2_media_test`, `q605_floppy_boot_etalon`, and
`q605_nofpu_boot_etalon`.
