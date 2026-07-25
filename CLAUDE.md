# CLAUDE.md

Orientation **always-loaded index** — keep terse, defer detail to other docs.

POM68K is a **Macintosh 68k** emulator: **Mac Plus** (68000, cycle-exact),
**Mac II** (68020 + Toby NuBus, functional accuracy) and its 68030
siblings **Mac IIx / IIcx** (same GLUE board + PMMU), the **V8 family** —
**Mac LC** (68020 + HMMU), **Mac LC II** (68030 + MMU + 68882),
**Classic II** (68030 + Eagle), **Color Classic** (68030 + Spice +
Cuda LLE) and the **Mac TV** (68030 @ 31.3 MHz + Tinker Bell + Cuda LLE,
EAF1678D ROM) — the **Mac IIsi** (68030 @ 20 MHz + RBV + Egret LLE,
36B7FB6C ROM, RAM-based video) and **Mac IIci** (68030 @ 25 MHz + RBV +
PIC1654S ADB modem LLE + discrete RTC, 368CADFE ROM) — the **Mac LC III / LC III+** (68030 @ 25 / 33 MHz + Sonora),
the **AIO family** — **LC 520 / LC 550 / Color Classic II** (Sonora +
Cuda 341S0060 LLE, EDE66CBD ROM) — the **Mac IIvx / IIvi** (VASP =
"V8 video on Sonora addressing", Egret LLE) — the **Centris 610 / 650** and **Quadra 610 / 650 / 800**
(djMEMC + IOSB, discrete RTC + PIC1654S LLE; the 800 adds SONIC + NuBus) — the
**Quadra 605** (68040 + FPU) and **LC 475 / LC 575 / Performa 475-575**
(68LC040) — both 040 + 040 MMU, functional accuracy. **26 machine
profiles, all booting the Finder.** It is the
68k sibling of [POMIIGS](../POMIIGS/) and reuses its architecture,
conventions and milestone discipline; the CPU integration pattern comes
from [NeoST](../neost/) (Moira wrapper, see below).

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
  HLE accelerator layered on the LLE core, non-conformant mode);
  `LLE_VS_HLE.md` (**inventory** of every HLE shortcut vs pure-LLE code,
  with the migration plan toward more LLE); `APPLETALK.md` (AppleTalk /
  LocalTalk / LLAP protocol reference + the netatalk/CUPS bridge, mapped
  back to `Scc8530`/`LtoUdp` — read before touching LocalTalk/AppleShare).

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
overlay (`POM68K_Q605_CACHE_BOOST`, **default 4 since 2026-07-25** — the
old boost-1 pin was a stale SCSI symptom); no architectural
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
ctest                        # 91 gates (asset-dependent gates may soft-skip)
./POM68K [ROM] [media...]    # 128K=Plus, 256K=Mac II, 512K=V8 family
                             # (checksum: LC/LC II/Classic II); 1 MB by
                             # checksum: Color Classic / LC III / AIO
                             # (EDE66CBD: LC 520/550/CC II) / IIvx-IIvi
                             # (4957EB49) / Quadra
```

Mac Plus: CPU **7.8336 MHz**, frame **60.15 Hz** (130 240 cycles/frame), video
**512×342 × 1 bpp**, MSB = leftmost, 1 = black. LC II runs at **15.6672 MHz**
(default 640×480 V8 video; `POM68K_MONITOR=512` → 512×384). Quadra profile
runs at **25 MHz** with 640×480 DAFB video. Default Quadra CPU is full
**68040 + soft 68882** (MAME `macqd605`); `POM68K_Q605_NOFPU=1` selects
68LC040 + soft 68882, `=2` TRUE bare `FPUModel::NONE` — both reach the
Finder (integer PACK 4 via the XPRAM `$AE` ROM-resource combo).

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
| **IWM + Sony 3.5" 800K GCR** | `Iwm.h/.cpp`, `SonyDrive.h/.cpp` | M5 ✓; write engine + GCR write-back (`iwm_write_test`) | MAME `iwm.cpp`/`ap_dsk35.cpp` |
| **Keyboard (M0110) + mouse** | `MacInput.h/.cpp`, `Scc8530.h/.cpp` | M5.5 ✓ | MAME/Mini vMac/Snow |
| **SCSI NCR 5380 + hard disk** | `Ncr5380.h/.cpp`, `ScsiDisk.h/.cpp` | M7 ✓ | MAME `ncr5380.cpp`, pce |
| SCC 8530 serial ports | `Scc8530.*` (mouse DCD, LAP ext ints, SDLC Tx/Rx LLAP wire, derived baud + paced Tx engine/FCS verify) + `LtoUdp.*` (UDP cable, `POM68K_LTOUDP=1`) | M7.1 / O6.10 / LLAP-1 ✓ | POMIIGS reuse; Zilog UM + MAME z80scc; Mini vMac LToUDP |
| **AppleTalk stack (in-process)** — node/router + AppleShare + LaserWriter + MacIP; GUI **Réseau → AppleTalk** window; on by default (`POM68K_APPLETALK=0` off, `POM68K_SHARE_DIR`), coexists with LToUDP | `AtalkStack.*` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer.*` (ASP+AFP 2.1, `.AppleDouble`), `PapServer.*` (PAP→CUPS/`.ps`), `MacIpGateway.*` (DDP-22 + user-mode NAT), `AtalkHub.h` | ✓ 2026-07-24; `atalk_stack_test`/`afp_server_test`/`pap_server_test`/`macip_gw_test` | Inside AppleTalk; netatalk 2.4.9 + macipgw wire; `docs/APPLETALK.md` §6.5 |
| **68030 oracle + fuzz loop** | `oracle/` (api, uae, fuzz) | O1-O3 ✓ (Musashi retired) | WinUAE (Hatari) |
| **68030 core + MMU (LC II)** | `extern/moira` extension | O4 ✓ | MC68030UM + WinUAE oracle |
| **68882 FPU** (softfloat, `setFPUModel`) | `extern/moira` FPU + `extern/softfloat/` | O5 ✓ | MC68881/882UM; WinUAE fpp.c |
| **68040 oracle + core + MMU** | `oracle/`, `extern/moira`, `tests/sst68040.cpp` | Q1-Q4 ✓; 7 200 pinned vectors | MC68040UM + WinUAE oracle |
| **Mac II / IIx / IIcx machine** (GLUE/Toby/ADB modem; IIx/IIcx = 68030 + PMMU on the same board, `MacIIMemory::Model`) | `MacIIMemory.*` (Model enum), `Cpu020.*` (`is030` flag), `AdbVia.*`, `Pic1654s.*`, `AdbLine.*`, `NuBus.*`, `DeclRom.*`, `TobyVideo.*` | ✓; Sys 6 + 7 Finder; LLE ADB (real PIC1654S) default, mouse live; `macii_*`, `iix_*`, `iicx_boot_etalon` | MAME `macii.cpp` + Mac II ROMs |
| **V8 family** (LC II + LC 68020/HMMU + Classic II Eagle + Color Classic Spice/Cuda LLE + **Mac TV** Tinker Bell @ 31.3 MHz) | `V8Memory.*` (Model enum + `spiceClass()` + `cpuHz`), `Cpu030.*`, `Egret.*`, `AdbBus.*`, `Asc.*` (`AscV8` + `AscSonora` $BC), `V8Video.h`, `Swim1.*`, `Swim2.*` | O6 ✓ + Phase C ✓; Finder ×5; SWIM1 IWM+ISM (`swim1_test`); `mactv_boot_etalon` | MAME `maclc.cpp`/`v8.cpp` + ROMs |
| **Mac IIsi / IIci** (RBV = RAM-Based Video, the V8/VASP/Sonora ancestor; 030 @ 20/25 MHz, SWIM1, discrete ASC, Bt478 CLUT, framebuffer at RAM start; IIsi = Egret 344S0100 LLE, IIci = PIC1654S ADB modem LLE + discrete 343-0042 RTC + empty NuBus) | `RbvMemory.*` (`iici` flag), `RbvCpu.*` (no i-cache boost — tight host-paced Egret bit-bang), `RbvVideo.h`; IIci reuses `AdbVia`/`Rtc` | Phase C ✓ 2026-07-25; Sys 7.5 Finder ×2 (`iisi_boot_etalon`, `iici_boot_etalon`) | MAME `rbv.cpp`/`maciici.cpp`/`adbmodem.cpp` |
| **Mac LC III / LC III+ + AIO family** (Sonora, 030 @ 25 / 33 MHz; Egret LLE 341S0851 or — LC 520/550/CC II, EDE66CBD ROM — Cuda LLE 341S0060) | `SonoraMemory.*` (cpuHz/machineId/cudaAdb params), `SonoraCpu.*`, `SonoraVideo.h` (mv_sonora modelines/CLUT/sense) | Phase C ✓; Sys 7.5 Finder ×5 (`lc3_boot_etalon`, `lc3plus_boot_etalon`, `lc520_boot_etalon`, `lc550_boot_etalon`, `cclassic2_boot_etalon`); bring-up story `docs/LC520_BRINGUP.md` | MAME `sonora.cpp`/`maclc3.cpp`/`mv_sonora.cpp` + EDE66CBD ROM disasm |
| **Mac IIvx / IIvi** (VASP = V8 video on Sonora addressing; 030 + 882 @ 31.3344 / 15.6672 MHz, Egret LLE 341S0851, AscV8 + SWIM1 + Ariel, empty NuBus = MAME-unmapped 0) | `VaspMemory.*`, `VaspCpu.*`, `VaspVideo.h` (2048-byte pitch) | Phase C ✓; Sys 7.5 Finder ×2 (`iivx_boot_etalon`, `iivi_boot_etalon`) | MAME `vasp.cpp`/`maciivx.cpp` |
| **Quadra 605 machine** (MEMCjr/PrimeTime/Cuda/DAFB) | `Q605Memory.*`, `Cpu040.*`, Cuda via `Egret` flavor | Q5-Q7 ✓; Mac OS 8.1 Finder | MAME `macquadra605.cpp` |
| **Centris 610/650 machine** (djMEMC + IOSB; discrete RTC + PIC1654S ADB LLE) | `CentrisMemory.*`, `CentrisCpu.*` (reuse Q605 DAFB/53C96/SWIM2/AscIosb/PseudoVia + Rtc + AdbVia) | Phase C ✓; Mac OS 8.1 Finder ×4 (`centris650/610`, `quadra650/610_boot_etalon`) | MAME `macquadra800.cpp`/`djmemc.cpp`/`iosb.cpp` |
| **NCR 53C96 TurboSCSI** | `Ncr53c96.*` | Q6 ✓; PIO + pseudo-DMA | MAME `ncr53c90.cpp` + ROM/OS 8 |
| **DAFB/Antelope video** | `Dafb.*` (Swatch CRTC/Gazelle/CLUT/sense; MEMCjr holding in `Q605Memory`) | Q8.1 ✓ + MAME-parity pass; 640×480×8 Finder gated | MAME `dafb.cpp` |
| **IOSB ASC stereo** | `Asc.*` (`AscIosb`) | Q8 ✓; `$BB` FIFO/IRQ gated | MAME IOSB / ASC |
| **SWIM2 + SuperDrive** | `Swim2.*`, `SonyDrive.*` | ✓; LLE cell engines (MFM CRC, rotation) gated | MAME SWIM2 |
| **Pseudo-VIA2** | `PseudoVia.*` | ✓ | MAME IOSB VIA2 layout |
| **Drive sounds** (floppy+HDD FX) | `FloppySound.*`, `FloppySoundSink.h` | ✓; `floppy_sound_test` | MAME floppy_sound_device via POM2 |

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
System 7.5): V8 gate array (`V8Memory`), Egret **firmware LLE (default
since 2026-07-24**, instruction-slaved ADB wire; `POM68K_EGRET_LLE=0` =
HLE fallback),
ASC-V8 sound (`Asc`), V8 video + Ariel (`V8Video`), SCSI pseudo-DMA over
the reused `Ncr5380`, SWIM1 IWM+ISM (`Swim1`, 1.44 MB MFM), 68030+PMMU+68882
via the O1-O5 core. Remaining LC II gaps in `TODO.md` (no-FPU SANE, 1.44 MB
SWIM, DFAC audio polish, bus/timing).

**Phase 3 (Quadra 605) reaches a usable Finder desktop:** Q1-Q4
68040/040-MMU core drives MEMCjr/PrimeTime, Cuda **firmware LLE**
(`M68hc05`+`CudaLle`, default with `roms/cuda/`; HLE fallback),
DAFB/Antelope (Q8.1 stride/depth/CLUT), IOSB ASC stereo (`AscIosb`),
SWIM2 SuperDrive, and NCR 53C96 SCSI; Mac OS 8.1 boots at 640×480×8 and
System 7.5 / 7.5.5 / 7.6 reach the Finder too (53C96 polled-WRITE path).
GUI exposes the machine alongside the other 24 profiles (Machine menu).
**91 CTest gates**,
including `lcii_boot_etalon`, `lcii_sys7_boot_etalon`, `macii_post_etalon`,
`macii_boot_etalon`, `macii_sys7_boot_etalon`, `macii_mouse_etalon`
(LLE ADB mouse — default path since 2026-07-22), `sst68040`,
`q605_boot_etalon`, `q605_dafb_test`, `q605_asc_test`, `swim2_test`,
`swim2_media_test`, `q605_floppy_boot_etalon`,
`q605_nofpu_boot_etalon`, `q605_barefpu_boot_etalon`, and the
firmware-LLE gates `m68hc05_test`, `cuda_lle_test`,
`q605_cudalle_boot_etalon`, `q605_cudalle_mouse_etalon`,
`egret_lle_test`, `swim1_test`, `iwm_write_test`,
`q605_cudalle_key_etalon` (Cuda firmware default
since 2026-07-23; the LC II Egret flavor default since 2026-07-24 —
instruction-slaved ADB wire, `M68hc05::onCycles`).

**The Finder boot matrix (Phase A/B) is green** on all four machines ×
System 4.1–8.1 era images (`tests/finder_boot_matrix.cpp`; CHANGELOG
2026-07-21). **Phase C (2026-07-24)** added four machines, all on
firmware-LLE MCU paths: the **Macintosh LC** (68020 — plain-020 /BERR
path fixed in Moira, POM68K_VENDOR.md) and **Classic II** (Eagle, mono
512×342 from RAM, forgiving bus) — gates `lc_boot_etalon`,
`classic2_boot_etalon`; then the **Color Classic** (Spice + `AscSonora`
$BC + SWIM2, **Cuda firmware LLE** 341S0788 — the factory 341S0417
wedges the M68hc05, TODO) and the **Mac LC III** (Sonora machine:
`SonoraMemory`/`SonoraCpu`/`SonoraVideo`, Egret LLE 341S0851) — gates
`cclassic_boot_etalon`, `lc3_boot_etalon`. A later round added two more
identity-variant siblings, each reusing a Finder-booting machine unchanged
but for the model longword: the **LC 475 / Performa 475** ($A55A2221,
68LC040 + Cuda LLE, split from the Quadra 605 $A55A2225) and the
**LC III+** (33.33 MHz Sonora, $A55A0003, Egret LLE — the unmapped-Sonora-I/O
readback had to match MAME's 0, not open-bus $FF, to clear its RAM device
poll) — gates `lc475_boot_etalon`, `lc3plus_boot_etalon`. The last round
cracked the **EDE66CBD all-in-one family** from scratch (MAME's stubs don't
boot; the ROM was the oracle — `docs/LC520_BRINGUP.md`): the AIOs carry a
**Cuda 341S0060** (2.40 — 2.37 livelocks on pseudo-cmd $0E) instead of the
Egret, selected by `SonoraMemory`'s `cudaAdb` flag. The **LC 520**
($A55A0100, 25 MHz, sense 6 → 640×480×8 color Finder), **LC 550**
($A55A0101, 33 MHz) and **Color Classic II** (same board, sense 2 →
512×384) all boot System 7.5 — gates `lc520_boot_etalon`,
`lc550_boot_etalon`, `cclassic2_boot_etalon`. The **Mac IIvx / IIvi**
followed on the new **VASP** machine (`VaspMemory`/`VaspCpu`/`VaspVideo` —
Sonora shell + V8 peripherals: AscV8, SWIM1, Ariel, pseudo-VIA video
hooks; Egret 341S0851 LLE; $A55A2015 @ 31.3344 MHz / $A55A2016 @
15.6672 MHz, shared 4957EB49 ROM) — gates `iivx_boot_etalon`,
`iivi_boot_etalon` (73). Then (2026-07-25) two more LLE machines: the
**Mac TV** on the **Tinker Bell** ASIC (its own `$EAF1678D` ROM, not the
EDE66CBD Sonora AIO — added as `V8Memory::Model::MacTv` + a `spiceClass()`
predicate + a `cpuHz` ctor param, Cuda LLE @ 31.3 MHz) and the **Mac IIsi**
on the new **RBV** machine (`RbvMemory`/`RbvCpu`/`RbvVideo`, Egret 344S0100
LLE @ 20 MHz; the RAM-based-video ancestor of the whole V8/VASP/Sonora
line), and the **Mac IIci** as an `iici` flavor of the same RBV machine
(PIC1654S ADB modem LLE + discrete 343-0042 RTC + empty NuBus @ 25 MHz —
the IIsi's near-twin; the wall was `via_in_a` PA0 = 1 for diagnostic-off,
else the ROM burns in on the VIA-T2 loop), and the **Mac IIx / IIcx** —
68030 variants of the Mac II FDHD (`MacIIMemory::Model` + `Cpu020` `is030`,
shared `$97221136` ROM, Toby NuBus reused; the wall was the 030 PMMU
double-translating against the GLUE 24-bit remap — skip `physAddr` when the
PMMU is on) — gates `mactv_boot_etalon`, `iisi_boot_etalon`,
`iici_boot_etalon`, `iix_boot_etalon`, `iicx_boot_etalon`. The 040 side of
Phase C added the **Centris 610 / 650** and their full-040 **Quadra 610 /
650** twins on the new djMEMC + IOSB machine (`CentrisMemory`/`CentrisCpu`,
F1A6F343 ROM) plus the **LC 575** identity ($A55A222E) — gates
`centris610/650_`, `quadra610/650_`, `lc575_boot_etalon`. Plus usability gates
`floppy_persist_test` and the LC II beyond-boot trio
`lcii_soak/persist/launch_etalon`. The **Quadra 800** followed (2026-07-25) as a fifth model of that machine —
ID pins `$12`, full 040 @ 33 MHz, SONIC/NuBus unmapped but the Ethernet
address ROM at `$50008000` modelled — gate `quadra800_boot_etalon`.
**Next (ROMs already on hand):** Quadra 630 / LC 630 (06684214) on the 040
side, SE/SE FDHD/Classic (compact 68000 + ADB transcoder), SE/30 (compact
IIx + built-in video), IIfx (OSS + IOPs) — and the LLE fidelity pass
(`docs/LLE_VS_HLE.md`) + the beyond-boot test depth pass (`TODO.md`).
