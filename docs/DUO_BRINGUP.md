# PowerBook Duo / PB150 bring-up — the MSC platform (platform #11)

**Goal.** The last 68k Macs with real power management: the PowerBook Duo
family (MSC/MSC II ASIC + PG&E power manager), and eventually the PowerBook
150 (same MSC lineage + IDE, the last 68030 PowerBook, 1994-07). This is the
`docs/LCII_HARDWARE.md`-style blueprint: everything below is read from MAME
`macpwrbkmsc.cpp` / `msc.cpp` / `gsc.cpp` / `m68hc05pge.cpp` (R. Belmont,
BSD-3-Clause) — cite file:line when porting.

Status 2026-07-31 (evening): **milestone 2 essentially working.** The full
PG&E LLE is in (`src/M68hc05Pge.*` chip clone + `src/PgePmu.*` board
integration, `PseudoVia::Flavour::Msc`), and the boot now runs the REAL
power-management sequence end to end:

1. PG&E boot stub executes from `pge_boot.bin`, powers the board, releases
   the 68030 (~0.7 s in, port E bit 2) — the CPU is held until then.
2. The system ROM uploads the PMU firmware over SPI through the VIA1
   shifter (≈32 K transfers; the blob is tagged **"BORG"** in the ROM at
   `$AA9C4`, upload verified byte-perfect against the ROM), the stub jumps
   into it at `$8122`, the firmware banks the boot ROM out at `$8131`.
3. The firmware serves the PMU protocol (its vector page lands at SRAM
   `$FFFx`; command dialog observed: host `$3A` reads with counter-ramp
   replies, steady `$D9`-family traffic after boot).
4. The host boots on: **GSC VRAM fully painted + SCSI bus probing** at
   2.5 G cycles (interrupt-driven, stack in high RAM).

**The one bug of the day, for the record**: SRAM writes to $FE00-$FFFF were
gated off while the boot ROM overlay was in — but MAME's view semantics
only override READS, so the firmware upload must be able to fill its
vector page under the overlay. With the gate in place every post-bank-out
interrupt vectored to $0000 through a zero vector page and the PMU crashed
into page-zero garbage (`M68hc05Pge.cpp` write8 comment).

**Three more findings, each a real divergence closed:**

- **The PMU interrupt rides the SPI clock.** MAME wires the PG&E's SCK to
  `msc_device::cb1_w` → stock `via6522::write_cb1`, which does BOTH halves:
  shift the SR *and* raise `IFR.CB1` on the PCR-selected edge
  (`6522via.cpp:1177-1203`). Our `extShiftCB1` (written for the Mac II PIC
  wire) only ever shifts, so the Duo's PMU driver never got an interrupt
  and the ROM spun forever on `ADBBase+$15D` bit 5 — the PMU-busy flag its
  ISR clears. `Via6522::extCb1Int()` adds the interrupt half, opt-in per
  machine. Result: 54 k IPL-1 interrupts served and the host walks on.
- **SR mode 000 shifts IN under an external clock** — stock 6522 behaviour
  (`SR_DISABLED(m_acr)` → `shift_in()`, `:1199`), not an MSC invention, and
  the mode the PMU reply path actually uses. Opt-in (`setMscShiftQuirk`)
  so the Mac II decode is untouched.
- **MISO is driven only while the VIA shifts OUT** (MAME hangs
  `spi_miso_w` off the CB2 *output* callback, whose only caller is
  `shift_out`). Feeding the SR MSB back in shift-IN mode made the PMU read
  its own bytes one exchange later.

DS2400 battery serial is now a real 1-Wire slave (`PgePmu::Ds2400`, MAME
`ds2401.cpp` state machine on the MCU cycle clock) fed from
`duobatid.bin`.

Next: reach the Finder, then GSC decode / input / sleep-wake gates.

## Why this order

| Profile | CPU | Screen | MAME support | POM68K order |
|---|---|---|---|---|
| **Duo 230** | 030 @ 33 MHz | GSC 640×400×4bpp gray | **full** (`macpd230`) | **1st — oracle-backed** |
| Duo 210 | 030 @ 25 MHz | same | full | 2nd (clock variant) |
| Duo 250 | 030 @ 33 | same, active matrix | full | cheap variant |
| Duo 270c | 030+FPU | CSC 640×480×16bpp color | full | after CSC port |
| Duo 280/280c | **040** @ 33 | CSC | full | 040 seam reuse |
| **PowerBook 150** | 030 @ 33 | GSC-class 640×480 gray | **"Future" — none** | LC520-method (ROM as only oracle) |

ROMs already in `roms/1MB ROMs/`: Duo 210/230/250 `ECFA989B`, Duo 270c
`0024D346`, Duo 280/280c `015621D7`, PB150 `FDA22562`.

**PG&E dumps (local, never committed — same policy as `roms/cuda/`):**

- `roms/pge/pge_boot.bin` — 512 B PG&E 68HC05 boot mask ROM, CRC32
  `62d4dfed` / SHA1 `79dc721651bf47aec53f57885779c84c4781761d`
  (MAME `m68hc05pge.cpp`). The PG&E's *main* firmware is **uploaded by the
  system ROM over SPI at every boot** — only this tiny bootloader is silicon.
- `roms/pge/duobatid.bin` — 8 B Dallas DS2400 battery serial, CRC32
  `7545c341` / SHA1 `61b094ee5b398077f70eaa1887921c8366f7abfe`
  (`macpwrbkmsc.cpp` ROM_START). Read over the PG&E's 1-Wire (port E bit 7).

## Address map (Duo 2xx; PB150 expected close — verify against its ROM)

CPU sees (from `macpwrbkmsc.cpp:588-627` + `msc.cpp:30-38` + `gsc.cpp` map,
MSC device mapped at $40000000, GSC at $50000000):

| Range | Device |
|---|---|
| `$40000000-$400FFFFF` (mirror ×16 through $4FF...) | ROM, `rom_switch_r` overlay behaviour like djMEMC |
| `$50F00000-$50F01FFF` | **VIA1** (MSC-internal `mscvia_device` — CB1 interrupt quirk, see below) |
| `$50F04000-$50F05FFF` | SCC 8530 (`scc_r/scc_w`) |
| `$50F06000-$50F07FFF` | SCSI pseudo-DMA (`scsi_drq`) |
| `$50F10000-$50F11FFF` | NCR 5380 registers |
| `$50F12000-$50F13FFF` | SCSI DRQ mirror |
| `$50F14000-$50F15FFF` | **ASC-MSC flavour** (`asc_msc_device` — a 4th ASC variant next to V8/Sonora/IOSB) |
| `$50F20000-$50F21FFF` | **GSC registers** (GSC maps at $50000000 base: internal `$00F20000`) |
| `$50F26000-$50F27FFF` | **pseudo-VIA2** (PMU handshake lives on its port B) |
| `$50FA0000-$50FA0003` | `power_cycle_w` |
| `$5FFFFFFC` | box ID: Duo 210 `$A55A1004`, 230 `$A55A1005`, 250 `$A55A1006`, 270c `$A55A1002`, 280 `$A55A1000` |
| `$60000000-$6001FFFF` (mirror `$0FFE0000`) | GSC VRAM, 128 KB |

## The PG&E power manager (the "powersave" brick)

68HC05-family MCU ("PG&E"), MAME device `m68hc05pge` (971 lines):

- **Host comms = SPI slave**, not bit-banged GPIO: the MSC pseudo-VIA2 port B
  bit 1 = `/PMU_ACK`, bit 2 = `/PMU_REQ` (`macpwrbkmsc.cpp:23-26`), data
  through the VIA1 shifter (our `Via6522::extShiftCB1` machinery is the
  matching half). This is the Cuda transport shape with a hardware shifter on
  the MCU end — **less phase-fragile than the Cuda bit-bang** in principle,
  but treat `pom68k-mactv-gate-broken` as applicable until proven otherwise.
- On-chip: 11 GPIO ports (A-L), ADC (battery voltage + the non-linear
  temperature sensor — lookup table excerpted at `macpwrbkmsc.cpp:36-66`),
  PLM timers (backlight brightness: PLM1 $7F→$26, PLM2 $01→$5A, PLM1+PLM2
  always $80), PWM (A0 charge current, B0 contrast $33-$C5), 1-Wire master
  (DS2400 battery ID), seconds/RTC (`macseconds_interface`), NVRAM
  (`device_nvram_interface` — PRAM lives in the PMU here, like Cuda).
- Trackball X/Y/button counters read by the PMU (`read_tbX/Y/B`) — input
  reaches the guest THROUGH the PMU (it is also the ADB controller on Duos).
- **Boot flow**: 512 B mask ROM waits for the host to upload the real
  firmware over SPI, then jumps in. So our `M68hc05` runs the boot stub +
  host-uploaded code — the big firmware ships inside the (user-provided)
  system ROM, which sidesteps the usual dump-availability question for
  everything but the 512 B stub.

Port wiring (from `macpwrbkmsc.cpp:773-789`): pullups port C `$FF`, port E
`$80` (bit 7 = 1-Wire bus).

## MSC ASIC notes

- VIA1 is MSC-internal with a **customized CB1 interrupt**: `msc.h:19-26` —
  shifter-related CB1 raises INT_CB1 in a way stock 6522 does not; MAME
  matches the leaked System 7.1 source. Port both the quirk and the comment.
- VIA2 is a pseudo-VIA (the `PseudoVia` pattern from IOSB applies).
- MSC embeds the sound (`asc_msc_device`) and wants an "audio active" signal
  into power management (`msc.cpp:129` — sleep must not trigger while sound
  plays).
- `rom_switch_r` overlay + `power_cycle_w`: the machine can power-cycle
  itself; reset semantics via MSC, not a global line.

## Milestones (house rule: each gated before the next)

1. **`duo_trace` boots the ROM to first PMU contact** — no PG&E dumps
   needed: `MscMemory` skeleton (RAM sizing, ROM overlay, VIA1/VIA2 decode,
   GSC stub, box ID), `Cpu030` wrapper, tracer on the `q605_trace` pattern.
   Expected stall: PMU handshake timeout (LC520 memory: handshake timeout →
   error chime + SCC monitor — that IS the checkpoint).
2. **PG&E LLE** (`M68hc05` extension) + `pge_boot.bin` → firmware upload
   handshake completes, RTC/PRAM served, boot proceeds.
3. **GSC video decode** (fixed-mode 640×400×4bpp, Valkyrie pattern) →
   happy-Mac / Finder frames; `duo230_boot_etalon`.
4. Input through the PMU (trackball + matrix keyboard) → `duo230_input_etalon`.
5. Variants: Duo 210/250 (trivial), 270c (CSC), 280 (040), then **PB150**
   (GSC-480 + IDE `$A55A` probing from its own ROM, no oracle).
6. **The actual point — power management observable**: sleep/wake cycle as a
   gate (`duo230_sleep_etalon`): guest sleeps via PMU, wakes on event, Ticks
   resume. No other machine in the tree can test this path.

## Open questions (answer from ROM traces, not guesses)

- PB150: exact GSC flavour, IDE decode window, box ID, and whether its PMU
  wants a different firmware image from the Duo one (`FDA22562` will say).
- MSC II vs MSC differences for the 040 Duos (280/280c).
- DFAC presence on the Duo audio path (`dfac.h` is included by the driver —
  check whether boot needs its I2C ACK the way Cuda 2.35 does,
  `pom68k-cuda-0417-wedge`).
