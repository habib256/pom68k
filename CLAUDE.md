# CLAUDE.md

Orientation **always-loaded index** — keep terse, defer detail to other docs.

POM68K is a **Macintosh 68k** emulator: **Mac Plus** first (68000,
cycle-exact), **Mac LC II** second (68030 + MMU, functional accuracy). It is
the 68k sibling of [POMIIGS](../POMIIGS/) and reuses its architecture,
conventions and milestone discipline; the CPU integration pattern comes from
[NeoST](../neost/) (Moira wrapper, see below).

- `README.md` — user walkthrough (build, ROM placement, keys, CLI).
- `DEV.md` — implementation deep-dives (references, internals, pinned tests).
- `TODO.md` — active backlog + milestone roadmap (incl. the LC II / 68030
  AI-differential plan).
- `CHANGELOG.md` — resolved items + the **why** behind non-obvious fixes.

## CPU core: Moira (vendored)

`extern/moira/` is **vendored from NeoST** (itself from Dirk Hoffmann's Moira,
MIT), with NeoST's patches included — provenance and every local change in
`extern/moira/POM68K_VENDOR.md`. Config: `MOIRA_PRECISE_TIMING`,
`MOIRA_EMULATE_ADDRESS_ERROR`, no Musashi mimicry. This copy executes
68000/68010 cycle-exact and 68020 functionally; **68030 is disassembler-only**
— its execution core (incl. MMU) is the LC II phase, built from the Motorola
68030 manual with AI + differential fuzzing against **two oracles (WinUAE,
MAME m68k)**; on spec/oracle conflict, **the oracle wins**.

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
ctest                        # gates: cpu_smoke, demo_screenshot
./POM68K [roms/macplus.rom]  # no ROM → built-in 68000 demo pattern
```

CPU **7.8336 MHz**, frame **60.15 Hz** (130 240 cycles/frame), video
**512×342 × 1 bpp**, MSB = leftmost, 1 = black.

## Subsystem map

| Subsystem | Files | Status | Source |
|---|---|---|---|
| **68000 CPU** (Moira wrapper + contention) | `Cpu68k.h/.cpp` | M1/M4 ✓ | NeoST pattern; GttMFH timing |
| **Memory map + overlay** | `MacMemory.h/.cpp` | M2/M4 ✓ (stubs SCC/IWM/SCSI) | MAME `mac128.cpp` |
| **VIA 6522** (ports, timers, IFR/IER) | `Via6522.h/.cpp` | M4 ✓ (±1-cycle latency deferred) | MAME `via6522.cpp` |
| **RTC 343-0042** (clock + PRAM serial) | `Rtc.h/.cpp` | M4 ✓ (no file persistence) | Mini vMac RTC.c |
| **Video 512×342** | `MacVideo.h` | M3 ✓ (whole-frame decode) | GttMFH |
| **Frame clock** (VBL phase, one-second) | `MacFrame.h` | M4 ✓ | GttMFH |
| **Sound** (PWM buffer + chime) | `MacAudio.h`, `MacAudioHost.h` | M6 ✓ | GttMFH |
| Built-in demo ROM | `DemoRom.h` | ✓ (gate vehicle) | — |
| UI (ImGui/GLFW, turbo) | `main.cpp` | M3 shell ✓ | POMIIGS main.cpp |
| **SST 68000 harness** (oracle-format prototype) | `tests/sst68000.cpp` | M4.5 | SingleStepTests/680x0 |
| IWM + Sony 3.5" GCR | — | M5 | POMIIGS `Iwm` reuse |
| Keyboard/mouse (VIA + $C000-less!) | — | M5 | GttMFH |
| Sound (PWM buffer) | — | M6 | GttMFH |
| SCC 8530 | — | M7 | POMIIGS `Scc8530` reuse |
| SCSI NCR 5380 | — | M7 | MAME `ncr5380.cpp` |
| **68030 core + MMU (LC II)** | `extern/moira` extension | phase 2 | Motorola manual + 2 oracles |

## Memory map (Mac Plus, 24-bit)

```
$000000-$3FFFFF  RAM, mirrors modulo size (ROM overlay here while booting)
$400000-$4FFFFF  ROM 128 KB (mirrored)
$580000-$5FFFFF  SCSI NCR 5380 (reg = A4-A6; A0: 0=read 1=write; A9 = DACK)
$600000-$7FFFFF  RAM (while overlay on)
$800000-$9FFFFF  SCC read, even bytes ($9FFFF8; A1 = channel, A2 = ctl/data)
$A00000-$BFFFFF  SCC write, odd bytes ($BFFFF9)
$C00000-$DFFFFF  IWM, odd bytes ($DFE1FF, regs every $200; stub reads $1F)
$E80000-$EFFFFF  VIA, even bytes ($EFE1FE, regs every $200; PA4 = overlay,
                 PA6 = screen buffer 1=main, PA3 = sound buffer)
Framebuffer: main = ramTop-$5900 ($3FA700 @ 4 MB), alt = ramTop-$D900
Sound buffer: main = ramTop-$300, alt = ramTop-$5F00 (370 words/frame)
```
Full pinned detail + timing/contention model in `DEV.md`.

## Status

**M0–M4 done**: the real Mac Plus ROM boots to the blinking-? floppy icon
with 4 MB correctly sized — VIA timers, RTC/PRAM, cycle-exact RAM/video
contention (GttMFH 2.56 MB/s budget), VBL at line-342 phase. **M4.5** (the
SingleStepTests 68000 harness, prototype of the 68030 oracle format) is
running. Next: M5 IWM + Sony drive → boot a System. See `TODO.md`.
