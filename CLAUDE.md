# CLAUDE.md

Always-loaded **orientation index**: it routes, it does not explain. The code
is the documentation; this file is the table of contents. Keep it scannable in
under a minute — anything that grows a paragraph belongs in `DEV.md` (how) or
`CHANGELOG.md` (why, dated).

POM68K is a **Macintosh 68k emulator**: **36 machine profiles**, from the Mac
Plus (68000, cycle-exact) to the Quadra 950 tower (68040, functional), **every
one booting the Finder**. 68k sibling of [POMIIGS](../POMIIGS/) — same
architecture, conventions and milestone discipline; the CPU integration
pattern (Moira wrapper) comes from [NeoST](../neost/).

The profile list lives in **one** place: the `kProfiles` table in
`src/main.cpp` (the GUI **Machine** menu), grouped by platform board.
36 profiles → 20 `MachineKind` values → **12 platform implementations**
(the machine table below; the 12th, MSC, has no GUI profile yet).
`SnapMachine` in `src/SaveStateMachines.h` carries the matching 36 tags.

## Where to look

| Need | Go to |
|---|---|
| Build, ROM placement, keys, CLI, machine roster | `README.md` |
| How a subsystem works; per-platform address maps; **the complete env-knob list** | `DEV.md` |
| Active backlog, known-red gates, next machines | `TODO.md` |
| What changed and **why** — dated entries, the project's real design record | `CHANGELOG.md` |
| Moira provenance + every local patch (incl. the JIT seam) | `extern/moira/POM68K_VENDOR.md` |
| JIT design, invariants, measurements | `src/jit/POM68K_JIT.md` |
| Host-side tools (`dir2hfs.py`, `rominfo`, `pgo_train.sh`, netatalk/macip bridges) | `tools/` |

`docs/` — research notes, one topic each:

| File | Topic |
|---|---|
| `LCII_HARDWARE.md` | LC II machine blueprint |
| `BASILISK_ROM_NOTES.md` | `$067C` ROM-behaviour oracle; §8 = facts verified on the real LC II ROM (`tools/rominfo`) |
| `LC520_BRINGUP.md` | How the EDE66CBD all-in-one family was cracked with the ROM as the only oracle |
| `68K_FAMILY_SCOPE.md` | Which other 68K Macs are reachable, and at what effort |
| `LLE_VS_HLE.md` | **Inventory** of every HLE shortcut vs pure-LLE code + migration plan |
| `HLE_OVERLAY.md` | Design study: opt-in HLE accelerator over the LLE core (non-conformant mode) |
| `APPLETALK.md` | AppleTalk / LocalTalk / LLAP reference + netatalk/CUPS bridge — read before touching `Scc8530`/`LtoUdp` |
| `IOP_BRINGUP.md` | Mac IIfx / Quadra 900-950 blueprint — the Apple PIC IOP (R65C02) + OSS brick, milestone plan |
| `DUO_BRINGUP.md` | PowerBook Duo 230 blueprint — the MSC + PG&E (68HC05 Power Manager) brick, milestone plan |
| `CACHE_040.md` | 68040 copyback/snooping blueprint — no oracle, no DMA client yet, M0 CM-bit numbers, milestones M1-M3 |

## Status (2026-08-04)

- All **36 profiles boot the Finder**, each covered by a boot-etalon gate
  (named per row in the machine table below). The OS-version sweep (System 4.1 → Mac OS 8.1)
  is `tests/finder_boot_matrix.cpp` — an on-demand harness, `EXCLUDE_FROM_ALL`
  and **not** a registered CTest. Bring-up history: `CHANGELOG.md`, by date.
- **143 CTest gates, 143/143 green** — full run 2026-08-03 on a **fully
  rebuilt tree** (`make` first, 150 objects/binaries relinked), 3 h 03.
  `ctest -N`: 66 `unit`, 8 `smoke`, 16 `jit`, 32 `m040`, 73 `etalon`.
  **The `make` is part of the claim, not a detail.** An earlier run the
  same day returned 143/143 over binaries linked at *different times* —
  102 of ~110 were older than `libpom68k_core.a` — and proved nothing:
  `ctest` does not build. A phantom failure gets investigated; a phantom
  pass gets quoted. **A green `ctest` is only worth the freshness of its
  binaries.**
- **The IOP brick is finished.** Platform #12 (OSS + dual Apple PIC IOPs)
  carries the Mac IIfx, and the same IOP front end on the Quadra 700 board
  carries the **Eclipse towers** — Quadra 900 and 950, the 35th and 36th
  profiles (2026-08-02, `docs/IOP_BRINGUP.md` § M7).
- **`docs/LLE_VS_HLE.md` lists no live BUG any more** (2026-08-02): the one
  entry that claimed to be one — the "Quadra Cmd-N modifier symptom" — was
  retracted by experiment. Everything remaining in § 1 is a *simplification*,
  each with its reason and its reopening condition.
- **Save states**: all 11 machine families, GUI-wired; the Eclipse tail
  (two `ApplePic` + `AdbLine` + `Egret` + 2nd 53C96) is gated in
  `savestate_040_test`.
- **JIT**: second engine, off by default (see the table below).
- The **PowerBook Duo 230** (MSC + PG&E) boots the Finder under
  `duo230_boot_etalon` but is **not** a `kProfiles` row yet — house rule: a
  GUI profile is earned by a Finder cell *plus* the GUI/save-state wiring
  (`docs/DUO_BRINGUP.md`, `TODO.md` § 7).
- The peripheral **deadline** mechanism covers eight platforms (Q605 + V8
  2026-08-03; Sonora, VASP, RBV, Centris, Q700/Eclipse, Q630 2026-08-04 —
  bound = min(MCU LLE, historical batch), 27 serial gates green). Compacts,
  Mac II, IIfx and MSC stay on fixed batches, each for a stated reason
  (`TODO.md` § 4).
- **Next: the 040 copyback/snooping chantier, M1** — blueprint + M0 landed
  2026-08-04 (`docs/CACHE_040.md`: no oracle, no DMA client, Mac OS maps
  98.9 % of data copyback). Then `TODO.md` § *Test & validation depth*.
- **Release CI** ships four artifacts on tag (Linux x86_64/aarch64
  AppImage glibc-2.27 — the aarch64 one is the Pi 400 package —, macOS
  Universal 2 dmg, Windows x64 zip); `--version` is the headless smoke.
  Bootstrap: run *Build bionic builder image* once, pin its digests in
  `release.yml`.

## Conventions (inherited from POMIIGS/POM2)

- **One concern per file** — each `.cpp/.h` pair owns one subsystem.
- **Every milestone is gated** by a CTest under `tests/` before the next
  depends on it.
- **`emuCycles` everywhere** — events carry CPU-cycle stamps, never wall-clock.
- **Docs in English**; conversation with the user may be French.
- **C++20** whole-tree (Moira requires it — divergence from POMIIGS's C++17).
- **License GPLv3**; ROMs are **user-provided, never committed**.

### Source of truth (ranked; cite file + line range in comments)

1. **MAME `mac.cpp` + `m68000` family** — primary hardware reference.
2. **Guide to the Macintosh Family Hardware** + *Inside Macintosh III* — docs.
3. **Hatari/WinUAE 68k timing** (via NeoST's convergence docs) — CPU timing.
4. **pce-macplus / minivmac / Basilisk II** — behavioural cross-checks.

## Build & run

```bash
./setup_imgui.sh             # one-time: fetches Dear ImGui + creates build/
cd build && cmake .. && make -j   # → build/POM68K + tests
ctest                        # 143 gates, ~3h (asset-dependent ones soft-skip)
ctest -L unit                # 66 gates — no ROM or disk image needed
ctest -L smoke               # 8 gates — one machine, both CPU engines
ctest -L jit                 # 16 gates;  -L m040 = 32, the 68040 family
make -j4 jitdev && ctest -L smoke   # the JIT working loop
./POM68K [ROM] [media...]    # profile picked by ROM size + checksum; the
                             # mapping is `kProfiles` in src/main.cpp
```

Never iterate on a full `ctest` or a full `make` — labels are derived from
test names at the end of `CMakeLists.txt`; pick the narrowest tier.

Clocks/video per machine live in the machine's `*Memory` ctor (`cpuHz`), not
here. Anchors worth remembering: Mac Plus **7.8336 MHz**, 60.15 Hz
(130 240 cycles/frame), 512×342×1 bpp (MSB = leftmost, 1 = black); LC II
**15.6672 MHz**; Quadra 605 **25 MHz**, 640×480×8. Quadra CPU default is
68040 + soft 68882; `POM68K_Q605_NOFPU=1` → 68LC040 + soft 68882, `=2` → true
bare `FPUModel::NONE` — both reach the Finder (integer PACK 4 via the XPRAM
`$AE` ROM-resource combo).

Mac Plus 24-bit address map, boot overlay, framebuffer/sound-buffer offsets
and the contention model: `DEV.md` § *Mac Plus platform*. Every other map is
in its `*Memory.h`.

## Machines — 12 platform implementations

Each row is one memory-map + I/O implementation; the profiles differ only by
clock / CPU / model ID / MCU. Per-platform deep-dive: `DEV.md`. The last row
carries no `kProfiles` entry yet — it is gated, not GUI-wired.

| Platform | Profiles (36) | Files | Gates | Source |
|---|---|---|---|---|
| **68000 compacts** | Plus, SE, SE FDHD, Classic | `MacMemory.*` (`Model`), `Cpu68k.*`, `MacVideo.h`, `MacFrame.h`, `MacAudio.h`/`MacAudioHost.h`, `MacInput.*` (M0110; SE+ = ADB via `Pic1654s`) | `rom_/disk_/system_boot_etalon`, `se_/sefdhd_/classic_boot_etalon` | MAME `mac128.cpp` |
| **GLUE + NuBus** | Mac II, IIx, IIcx, SE/30 (compact IIx, `Se30Video.h`) | `MacIIMemory.*` (`Model`), `Cpu020.*` (`is030`), `AdbVia.*`, `Pic1654s.*`, `AdbLine.*`, `NuBus.*`, `DeclRom.*`, `TobyVideo.*` | `macii_post/boot/sys7_boot/mouse_etalon`, `iix_/iicx_/se30_boot_etalon` | MAME `macii.cpp` |
| **OSS + 2 Apple PIC IOPs** (no VIA2, no built-in video) | Mac IIfx (68030 @ 40 MHz) | `IIfxMemory.*`, `IIfxCpu.*`, `ApplePic.*` (IOP), `R65c02.*` (its CPU); video = `TobyVideo` on slot 9, ADB = IOP firmware ↔ `AdbLine` | `iifx_post/boot/input_etalon`, `applepic_test`, `r65c02_test` | MAME `maciifx.cpp`/`applepic.cpp`; `docs/IOP_BRINGUP.md` |
| **RBV** (RAM-based video — ancestor of V8/VASP/Sonora) | IIsi (Egret LLE), IIci (PIC ADB modem + discrete RTC) | `RbvMemory.*` (`iici`), `RbvCpu.*`, `RbvVideo.h` | `iisi_/iici_boot_etalon`, `iisi_input_etalon` | MAME `rbv.cpp`/`maciici.cpp`/`adbmodem.cpp` |
| **V8 / Eagle / Spice / Tinker Bell** | LC (68020+HMMU), LC II, Classic II, Color Classic, Mac TV | `V8Memory.*` (`Model`, `spiceClass()`, `cpuHz`), `Cpu030.*`, `V8Video.h` | `lc_/lcii_/classic2_/cclassic_/mactv_boot_etalon`, `lcii_sys7_/soak/persist/launch/floppy_etalon` | MAME `maclc.cpp`/`v8.cpp` |
| **Sonora** | LC III, LC III+, LC 520, LC 550, Color Classic II | `SonoraMemory.*` (`cpuHz`/`machineId`/`cudaAdb`), `SonoraCpu.*`, `SonoraVideo.h` | `lc3_/lc3plus_/lc520_/lc550_/cclassic2_boot_etalon`, `lc3_/lc520_input_etalon` | MAME `sonora.cpp`/`maclc3.cpp`/`mv_sonora.cpp`; `docs/LC520_BRINGUP.md` |
| **VASP** (V8 peripherals on Sonora addressing) | IIvx, IIvi | `VaspMemory.*`, `VaspCpu.*`, `VaspVideo.h` | `iivx_/iivi_boot_etalon`, `iivx_input_etalon` | MAME `vasp.cpp`/`maciivx.cpp` |
| **MEMCjr + PrimeTime** | LC 475, LC 575, Quadra 605 | `Q605Memory.*`, `Cpu040.*` | `q605_boot/floppy_boot/nofpu_boot/barefpu_boot/ot_bind_etalon`, `lc475_/lc575_boot_etalon` | MAME `macquadra605.cpp` |
| **djMEMC + IOSB** | Centris 610/650, Quadra 610/650/800 | `CentrisMemory.*`, `CentrisCpu.*` (reuse `Dafb`/`Ncr53c96`/`Swim2`/`AscIosb`/`PseudoVia` + `Rtc` + `AdbVia`) | `centris610/650_`, `quadra610/650/800_boot_etalon` | MAME `macquadra800.cpp`/`djmemc.cpp`/`iosb.cpp` |
| **Discrete 040** (Mac II front end + Quadra back end) | Quadra 700; **Quadra 900 / 950** = the same board with the IIfx's front end (two `ApplePic` IOPs, `Egret` instead of the discrete RTC, a 2nd 53C96) | `Q700Memory.*` (`Model {Spike,Q900,Q950}`), `Q700Cpu.*`; SCSI through DAFB's own TurboSCSI cell | `q700_/q900_/q950_boot_etalon` | `DEV.md` § *Discrete-040 platform*; `docs/IOP_BRINGUP.md` § M7 |
| **F108 + PrimeTime II + Valkyrie** | Quadra 630, LC 580 | `Q630Memory.*`, `Q630Cpu.*`, `Valkyrie.*` (fixed-mode framebuffer) | `q630_/lc580_boot_etalon` | MAME `valkyrie.cpp` |
| **MSC + PG&E** (PowerBook Duo — **no `kProfiles` row yet**) | Duo 230 (68030 @ 33 MHz, LCD via `MscMemory::decodeScreen`) | `MscMemory.*`, `MscCpu.*`, `PgePmu.*`, `M68hc05Pge.*` (the PMU's own 68HC05, BORG v2 uploaded mid-boot) | `duo230_boot_etalon` | MAME `macpwrbkmsc.cpp`/`msc.cpp`/`m68hc05pge.cpp`; `docs/DUO_BRINGUP.md` |

## Subsystems

| Subsystem | Files | Status | Source |
|---|---|---|---|
| **68000 CPU** (Moira wrapper + contention) | `Cpu68k.h/.cpp` | M1/M4 ✓ | NeoST pattern; GttMFH timing |
| **VIA 6522** (ports, timers, IFR/IER) | `Via6522.h/.cpp` | M4 ✓ (±1-cycle latency deferred) | MAME `via6522.cpp` |
| **RTC 343-0042** (clock + PRAM serial) | `Rtc.h/.cpp` | M4 ✓; PRAM file persistence belongs to the owning machine (`loadPram`/`savePram` on `*Memory`) — **absent on `MacMemory`, `MacIIMemory`, `IIfxMemory` and `MscMemory`**, i.e. the compacts, the Mac II family, the IIfx and the Duo | Mini vMac RTC.c |
| Built-in demo ROM | `DemoRom.h` | ✓ (gate vehicle) | — |
| UI (ImGui/GLFW, turbo, Machine/Disques/CPU/Réseau menus) | `main.cpp` | ✓ | POMIIGS main.cpp |
| **SST 68000 harness** | `tests/sst68000.cpp` | M4.5 ✓; 1 000 058 vectors | SingleStepTests/680x0 |
| **IWM + Sony 3.5" 800K GCR** | `Iwm.h/.cpp`, `SonyDrive.h/.cpp` | M5 ✓; write engine + GCR write-back (`iwm_write_test`) | MAME `iwm.cpp`/`ap_dsk35.cpp` |
| **SWIM1** (IWM + ISM, 1.44 MB MFM) | `Swim1.h/.cpp` | ✓ `swim1_test` | MAME SWIM |
| **SWIM2 + SuperDrive** | `Swim2.*`, `SonyDrive.*` | ✓; LLE cell engines (MFM CRC, rotation) gated | MAME SWIM2 |
| **Keyboard (M0110) + mouse** | `MacInput.h/.cpp`, `Scc8530.h/.cpp` | M5.5 ✓ | MAME/Mini vMac/Snow |
| **ADB** (bus + line + LLE transceivers) | `AdbBus.*`, `AdbLine.*`, `AdbVia.*`, `Pic1654s.*` | ✓ `adbline_test`, `pic1654s_test` | MAME |
| **MCU firmware LLE** (Egret / Cuda on a real 6805) | `M68hc05.*`, `CudaLle.*`, `Egret.*` | ✓ default where `roms/` has the dump; HLE fallback (`POM68K_EGRET_LLE=0`) — `m68hc05_test`, `cuda_lle_test`, `egret_lle_test`, `q605_cudalle_*` | MAME + factory firmware |
| **Apple PIC IOP** (IIfx + the Eclipse towers) | `R65c02.*` (core), `ApplePic.*` (device), `IIfxMemory.*`/`IIfxCpu.*` (the platform) | M1-M3, M5-M6 ✓ — **the IIfx is the 34th profile**: `iifx_boot_etalon` (Finder on 7.6), `iifx_input_etalon` (mouse + KeyMap through the IOP firmware), `iifx_post_etalon`, `applepic_test`, `r65c02_test`, save states in `savestate_030_test`. **M7 closed 2026-08-02**: the Quadra 900/950 are the 35th/36th profiles — the same IOP brick on the Quadra 700 board (`q900_/q950_boot_etalon`, Eclipse save states in `savestate_040_test`) — `docs/IOP_BRINGUP.md` | POM2 `M6502` vendored; MAME `applepic.cpp`/`maciifx.cpp` |
| **PG&E Power Manager + MSC** (PowerBook Duo) | `M68hc05Pge.*` (the PMU's own 68HC05 variant), `PgePmu.*` (SPI/REQ-ACK wire + /PMU_INT level), `MscMemory.*`/`MscCpu.*` (the platform) | M1-M3 ✓ — **the Duo 230 boots the Finder** (`duo230_boot_etalon`, System 7.5.5) but is **not** a GUI profile: no `kProfiles` row, no save states. Next: input through the PMU, then sleep/wake — `docs/DUO_BRINGUP.md` | MAME `macpwrbkmsc.cpp`/`msc.cpp`/`m68hc05pge.cpp` |
| **SCSI NCR 5380 + disks** | `Ncr5380.h/.cpp`, `ScsiDisk.h/.cpp` | M7 ✓; + pseudo-DMA (`scsi_pdma_test`) | MAME `ncr5380.cpp`, pce |
| **NCR 53C96 TurboSCSI** | `Ncr53c96.*` | Q6 ✓; PIO + pseudo-DMA | MAME `ncr53c90.cpp` |
| **CD-ROM target** (`ScsiDisk::Kind::Cdrom`, `.cue`/`.bin`, 2048-B blocks) | `ScsiDisk.*` | ✓ `scsi_cdrom_test`, `q605_cdrom_etalon`, `q605_cdboot_etalon` | MAME cdrom |
| **Sound** — Plus PWM + chime; ASC flavours `AscV8` / `AscSonora` ($BC) / `AscIosb` ($BB) | `MacAudio.h`, `MacAudioHost.h`, `Asc.*` | ✓ `sound_test`, `asc_test`, `q605_asc_test` | GttMFH; MAME ASC/IOSB |
| **Drive sounds** (floppy + HDD FX) | `FloppySound.*`, `FloppySoundSink.h` | ✓ `floppy_sound_test` | MAME floppy_sound_device via POM2 |
| **Video decoders** + **raster beam** | `VideoBeam.h` (scan position + row schedule; owns no clock — it adopts each platform's own `framePos_`), `MacVideo.h`, `TobyVideo.*`, `V8Video.h`, `RbvVideo.h`, `SonoraVideo.h`, `VaspVideo.h`, `Se30Video.h`, `Dafb.*`, `Valkyrie.*`, `Ariel.h` | ✓ per platform; **row-granular decode on all 9** since 2026-08-02 — the beam owns no clock, it adopts each platform's own `framePos_`. `video_beam_test`, `v8_raster_test`, `raster_equiv_test` | per-platform MAME; `docs/LLE_VS_HLE.md` § 1.1 |
| **DAFB/Antelope** (Swatch CRTC, three clock generators, CLUT, sense) | `Dafb.*` | Q8.1 ✓ + MAME-parity pass | MAME `dafb.cpp` |
| **Pseudo-VIA2** | `PseudoVia.*` | ✓ `pseudovia_test` | MAME IOSB VIA2 layout |
| **SCC 8530 serial** (mouse DCD, LAP ext ints, SDLC LLAP wire, derived baud, paced Tx/FCS) + LToUDP cable (`POM68K_LTOUDP=1`) | `Scc8530.*`, `LtoUdp.*` | M7.1 / O6.10 / LLAP-1 ✓ | Zilog UM + MAME z80scc; Mini vMac LToUDP |
| **AppleTalk stack (in-process)** — node/router + AppleShare + LaserWriter + MacIP; GUI **Réseau → AppleTalk**; on by default (`POM68K_APPLETALK=0` off, `POM68K_SHARE_DIR`) | `AtalkStack.*` (DDP/RTMP/ZIP/NBP/AEP/ATP), `AfpServer.*` (ASP+AFP 2.1, `.AppleDouble`), `PapServer.*` (PAP→CUPS/`.ps`), `MacIpGateway.*` (DDP-22 + user-mode NAT), `AtalkHub.h` | ✓; 8 gates — `atalk_stack_test`, `afp_server_test`, `pap_server_test`, `macip_gw_test`, `llap_loop_test`, `ltoudp_test`, `llap_two_system_etalon`, `q605_ot_bind_etalon` | Inside AppleTalk; netatalk 2.4.9 + macipgw wire; `docs/APPLETALK.md` §6.5 |
| **68030 oracle + fuzz loop** | `oracle/` (api, uae, fuzz) | O1-O3 ✓; Musashi retired 2026-07-15 (0 arbitrations won) — WinUAE-solo with manual arbitration (`oracle/fuzz/disputes/NOTES.md`) | WinUAE (Hatari) |
| **68030 core + PMMU** | `extern/moira` extension, `tests/sst68030.cpp` | O4 ✓; **3 082 pinned vectors**, rulings D1-D22 | MC68030UM + WinUAE oracle |
| **68882 FPU** (softfloat, `setFPUModel`) | `extern/moira` FPU + `extern/softfloat/` | O5 ✓ `fpu_sanity` | MC68881/882UM; WinUAE fpp.c |
| **68040 core + MMU** | `extern/moira`, `tests/sst68040.cpp` | Q1-Q4 ✓; **7 200/7 200 pinned vectors** | MC68040UM + WinUAE oracle |
| **Save states** — one `visit<Ar>()` per class drives save AND load; chunked container + zero-run codec. Callbacks/pointers **re-bound, not serialized**; caches **flushed**; disk images stay on the host (`ScsiDisk` copy-on-first-write log) | `SaveState.*`, `SaveStateMachines.*` (11 save/load pairs, 36 `SnapMachine` tags), `MoiraSnapshot.h`, `visit()` in each device | ✓ all 11 families; GUI menu Machine « Sauver / Restaurer l'état » (`main.cpp` `SaveStateSlot`, machine-thread apply, `<image>.<profile>.pomss`); `savestate_test`, `savestate_v8_test`, `savestate_030/040/68k_test`, `lcii_savestate_etalon`, `q605_savestate_etalon` | — |
| **JIT — second execution engine** (host-agnostic `jit::Engine` + `jit::Backend`; `threaded` portable floor, **x86-64** and **AArch64** code generators — A64 full-boot-gated, `auto` on arm64 since 2026-08-04). **Off by default everywhere** — the interpreter is what every accuracy claim rests on | `src/jit/` (`JitEngine`, `JitBackend`, `JitIr`, `JitCodeBuffer`, `JitGuard`, `backends/`), seam in `extern/moira` | J0-J2 ✓. Engine wired in 9 machine loops: `Cpu030` (V8, incl. the 68020 LC), `SonoraCpu`, `VaspCpu`, `RbvCpu`, `Cpu040`, `CentrisCpu`, `Q700Cpu`, `Q630Cpu`, `MscCpu` — **`Cpu020` (Mac II family), `Cpu68k` (compacts) and `IIfxCpu` have none**. x64 codegen is **68040-only by declared capability** (`BackendCaps::guestFamilies`), so `auto` gives the 030s `threaded`. `q605_boot_etalon` 61.3 s interp → 22.9 s (×2.68), 89.6 % native | `src/jit/POM68K_JIT.md`; WinUAE JIT as reference, not imported |

## CPU core: Moira (vendored)

`extern/moira/` is vendored from NeoST (itself Dirk Hoffmann's Moira, MIT),
NeoST's patches included — provenance and every local change in
`extern/moira/POM68K_VENDOR.md`. Config: `MOIRA_PRECISE_TIMING`,
`MOIRA_EMULATE_ADDRESS_ERROR`, no Musashi mimicry. Executes 68000/68010
cycle-exact, and 68020 / **68030 + PMMU** / **68040 + 68LC040 + 040 MMU**
functionally.

The 030/040 extensions were built from the Motorola manuals with AI +
differential fuzzing against the vendored **WinUAE/Hatari oracle**. **On
spec/oracle conflict, the oracle wins.** The 040 path has an ATC fast path and
a throughput/i-cache overlay (`POM68K_Q605_CACHE_BOOST`, default **4**; the
old boost-1 pin was a stale SCSI symptom); no architectural copyback/snooping
yet. Bus timing is counted on **machine** cycles, never on the boosted clock.
