# PowerBook Duo bring-up — the MSC + PG&E platform

(`MscMemory.h:4` calls this "platform #11" and `IIfxMemory.h:4` calls OSS+IOP
"#12": those are **creation order**, not row numbers in CLAUDE.md's machine
table, and nothing derives from them.)

**Status: the Duo 230 is the 37th profile** (2026-08-06). It boots System 7.5.5
to the Finder — menu bar, battery icon, Control Strip, mounted volume — under
the real power-management sequence, with the PG&E running its own uploaded
firmware. Gates: `duo230_boot_etalon` (Finder, and the `etalon-core`
representative for this platform), `msc_parity_test` (asset-free MAME parity),
save states in `savestate_030_test`.

**The machine as-built is `DEV.md` § 2.11 and the header of `MscMemory.h`;
this file is the porting reference and the record of what the bring-up
established** — the MAME cites to port from (`macpwrbkmsc.cpp` / `msc.cpp` /
`gsc.cpp` / `m68hc05pge.cpp`, R. Belmont, BSD-3-Clause; cite `file:line`),
the findings that are not derivable from the code, and the things that were
*disproved* and must not be re-tried.

Still open, in milestone order: the **trackball quadrature counters** (the
mouse still rides the PG&E's ADB modem cell; the KEYBOARD matrix landed
2026-08-13) and then **sleep/wake**, the one path no other machine in the tree
can exercise — of which the CPU-power-down half is now done, see milestone 6.
Then the PB150, whose ROM is the only oracle.

---

## The boot sequence, end to end

1. The PG&E boot stub executes from `pge_boot.bin`, powers the board and
   releases the 68030 (~0.7 s in, port E bit 2 — `msc.cpp:151`). **The CPU is
   held until then**, so without the stub the ROM stalls loudly at its first
   PMU exchange (that stall was milestone 1's checkpoint).
2. The system ROM uploads the PMU firmware over SPI through the VIA1 shifter
   (≈32 K transfers; the blob is tagged **"BORG"** in the ROM at `$AA9C4`,
   upload verified byte-perfect), the stub jumps into it at `$8122`, and the
   firmware banks the boot ROM out at `$8131`.
3. The firmware serves the PMU protocol (its vector page lands at SRAM
   `$FFFx`; host `$3A` reads answer with counter-ramp replies, steady
   `$D9`-family traffic after boot).
4. **Mid-boot, System 7.5.5 replaces that firmware.** PmgrOp `$E1` streams a
   complete second ~32 KB BORG image over SPI (magic `"BORG"` on the wire, the
   PMU acking every byte with `$8D`, ~32 782 exchanges) and the PG&E switches
   to **v2** at ≈MCU cycle 54 M. Everything after that point runs v2, **with
   different addresses** — a whole debugging detour was spent watching v1 PCs
   in v2 code. Maps and shared zero-page variables:
   `pom68k-duo-borg-firmware-map`.
5. GSC VRAM paints, SCSI probes, the Finder comes up.

## The findings that cost time (each one a real divergence closed)

- **/PMU_INT is a LEVEL, not an edge.** This is the fix that turned "third
  ADBReInit hangs forever" into a booting machine. MAME's `mscvia::pmu_int`
  (`msc.h:19-33`, "fits what we see in the leaked System 7.1 source") asserts
  AND clears INT_CB1. `Via6522::pmuIntLevel()` holds IFR.CB1 set while the line
  is low (surviving IFR writes and ORB accesses; deassert clears it), driven
  from PG&E port F bit 2, and **only the Duo uses it**. Why an edge cannot
  work: the ADB→PMU bridge masks IER.CB1 (`move.b #$10,($1C00,A1)`) around
  every send and acks stale flags, so one assert landing inside that window is
  lost — and since the PMU never deasserts before a readINT drain, no further
  edge can ever come. The line stays low forever and every later assert is a
  write of 0 over 0. Result of the fix: 430 frozen interrupt edges → 1326 and
  climbing, SCSI 281 → 3448 commands, three ADBReInits complete, Finder.
- **The PMU interrupt must NOT ride the SPI clock** — a deliberate, measured
  departure from MAME's literal wiring, and the other half of the same story.
  MAME binds the PG&E's SCK to `msc_device::cb1_w` → the *interrupting*
  `write_cb1`; with that, every shift edge sets IFR.CB1, the driver re-enables
  IER with the flag still pending, its ISR issues readINT (`$78`) again,
  forever. Same build, 5 G cycles: **interrupt half ON → 176 back-to-back
  `$78`, 0 SCSI selects; OFF → 1122 selects, 555 commands, the System loads off
  disk.** `msc.h` models `write_cb1_noint` plus a separate `pmu_int` for
  exactly this reason. `POM68K_PGE_CB1INT=1` restores the MAME-literal wiring
  for A/B; `POM68K_PGE_CB1BYTE=1` gives one CB1 IRQ per completed byte.
- **SRAM writes to `$FE00-$FFFF` must go through while the boot-ROM overlay is
  up.** MAME's view semantics only override READS, so the firmware upload has
  to be able to fill its vector page under the overlay. Gate those writes off
  and every post-bank-out interrupt vectors to `$0000` through a zero vector
  page, and the PMU crashes into page-zero garbage (`M68hc05Pge.cpp` `write8`).
- **SR mode 000 shifts IN under an external clock** — stock 6522 behaviour
  (`SR_DISABLED(m_acr)` → `shift_in()`, `:1199`), not an MSC invention, and the
  mode the PMU reply path actually uses. Opt-in (`Via6522::setMscShiftQuirk`)
  so the Mac II decode is untouched.
- **MISO is driven only while the VIA shifts OUT** (MAME hangs `spi_miso_w`
  off the CB2 *output* callback, whose only caller is `shift_out`). Feeding the
  SR MSB back in shift-IN mode made the PMU read its own bytes one exchange
  later.
- **The 80 µs host stall is load-bearing, and the window is sharp.** MAME's
  `via2_out_b` spins the 68030 80 µs on a /PMU_REQ edge; it looked like
  optional pacing, so it was bisected. The MCU/host interleave is not a free
  parameter (same lesson as the Cuda transport,
  `pom68k-mactv-gate-broken`). `POM68K_PGE_SPINUS=<µs>` is left in for phase
  experiments:

  | host stall | ADBReInit clears | SCSI commands |
  |---|---|---|
  | 0 µs | 0 | 0 |
  | 40 µs | 2 | 895 |
  | **80 µs (MAME, default)** | **2** | **895** |
  | 160 µs | 0 | 0 |

- **The PG&E's ADB modem cell had to answer, and framing it took three fixes**
  (`ADBCR/ADBSR/ADBDR` at `$18-$1A`). MAME's `m68hc05pge` models TDRE and TC on
  timers but **never RDRF** (`m_adbsr |=` only ever sets TDRE and TC) — a
  strong hint that MAME's `macpd230` does not reach the Finder either, whatever
  its lack of a NOT_WORKING flag suggests. The three, each found with
  `POM68K_PGE_ADBTRACE=1`:
  1. **A reply must be its own event.** Folding RDRF into the same timer expiry
     that raises TDRE clobbered ADBDR while the firmware still held the
     transmitted byte there — 1122 SCSI selects → 0. TX-done and RX-arrived are
     separate.
  2. **The TC timer overwrote the scheduled reply.** The firmware acks TDRE by
     writing ADBCR, which arms the 50 µs TC timer, wiping a reply scheduled off
     the TDRE expiry. Both expiries chain into the reply now.
  3. **Listen data bytes must reach the bus.** A Listen R3 with an empty
     payload means the relocation never happens, so the firmware re-finds the
     device at its old address and relocates it again — forever. The cell
     buffers a Listen's two data bytes and only then drives the command.

  Result: enumeration converges (Talk R3 → `$62 $05` keyboard, `$63 $01`
  mouse) and settles into steady-state autopoll (`$2C` → `$FF $FF`).
- **v2 firmware facts worth keeping**: an ADB watchdog at `$01F3` (8 ticks per
  command, expiry retransmits a `$31` resync byte to the cell); the readINT
  dispatcher `$86D6` serves causes by bit — 7 = the once-per-second
  battery/environment report (what the steady `$D9` traffic fetches), 4 = ADB
  reply; `$A0` = host-programmed interrupt-enable mask; delivery re-asserts per
  remaining cause.
- **Capstone's 6805 mode prints `bset`/`bclr` inverted** (`brset`/`brclr` are
  correct) — arbitrate against `src/M68hc05.cpp`
  (`pom68k-capstone-hc05-bset-inverted`).

## Disproved — kept so nobody re-tries them

- **Port A bit 5 as the power key.** `macpwrbkmsc.cpp` returns the literal
  `$DF` (bit 5 clear) for "not pressed" when no matrix row is selected.
  Implementing that hangs the boot dead at `$408B98F2` with Ticks frozen at 0 —
  the firmware reads it as "power key held". Bit 5 must read **1** for
  released, so a blanket `$FF` is correct.
- **The power key as a way to end a session.** Holding `$7F` for 150 frames
  on a running desktop raises **no dialog at all** — screen dump, 2026-08-13
  and again 2026-08-14. On a Duo that key is the PMU's, and whatever the
  firmware does with it does not reach the System through the path we model.
  The gate uses the Finder's `Special` menu instead.
- **"The System holds its writes behind the hard-disk spin-down"** (the
  2026-08-13 working hypothesis for the persist leg). Disproved by its own
  evidence: the Finder issues **eleven READ commands between Cmd and N**, so
  the drive is awake at the moment of the folder creation. What is actually
  happening is that the volume is never flushed at all — its VCB keeps the
  File Manager's dirty bit for minutes — which is PowerBook system software
  doing its job. **Do not model spin-down for this.**
- **Raising RDRF unconditionally on transmit-done** (`POM68K_PGE_ADBRX=1`):
  0 SCSI selects.
- **Unplugging the charger** (`POM68K_PGE_CHARGER=0`): ADBReInit #1 never
  completes, 0 SCSI selects. The battery state machine is load-bearing — leave
  MAME's "charger present".
- **System 7.1 does not boot Duos** (no Enabler). The machine reaches a
  fully-drawn "This startup disk will not work on this Macintosh model" alert —
  desktop pattern, arrow cursor, waiting for a click on Restart (`MBState $172`
  poll). An unplanned proof that the ROM boot, one complete ADBReInit,
  QuickDraw dialog rendering and GSC 4 bpp are all solid. **Use the 7.5.5 image
  for Duo work.**
- **Honouring the Listen R3 activator byte in `AdbBus`** (a device only moves
  on `$00`/`$FE`) did not fix the third-ADBReInit hang. Kept anyway — it is a
  real fidelity gap.
- **"ADBBase goes stale under the waiter."** A3 read `$5894` at one stop and
  `$57BC` at another, which looked like re-entrancy. It was an artefact of
  comparing two different stop points: `--stop-at` with a small skip catches
  the *early* ROM-era ADBReInit, which legitimately runs at the older ADBBase.
  Measured at the real hang, `ADBBase ($0CF8) == A3`. `duo_trace` prints both
  so the question cannot be fudged again — **a pointer compared across two
  stop points is not a measurement.**

## Diagnostics (all in-tree, all env-gated)

`duo_trace` (`EXCLUDE_FROM_ALL`) is the runner. Host side: `DUO_PMLOG`
(command stream, e.g. `DUO_PMLOG=408898CA`, with PMCMD `$20` payload deref),
`DUO_PGEWATCH` (windowed zero-page / cell-register sampling), `DUO_SRAMDUMP`
(dump the LIVE firmware image — the only way to read v2), `DUO_KEYAT` (inject
a key/mouse event at a clock), `DUO_PCCOUNT`, `DUO_CKPT`, `DUO_DUMPAT`.
MCU side: `POM68K_PGE_PCCOUNT` / `_PCWIN` / `_PCHIST` (PC counters, windowed
full logging, bucket histogram), `POM68K_PGE_TRAP=<hexbyte>` (dump firmware
state the instant a byte arrives over SPI), `POM68K_PGE_SPIBYTES` (every
completed exchange), `POM68K_PGE_HSHAKE` (REQ/ACK and /PMU_INT transitions),
`POM68K_PGE_ADBTRACE`, `POM68K_PGE_TRACE`, `POM68K_PGE_TBTRACE` (what the
firmware reads out of the trackball counters, and how often). Full list with
defaults: `DEV.md` § 5.

## Why this order

| Profile | CPU | Screen | MAME support | POM68K order |
|---|---|---|---|---|
| **Duo 230** | 030 @ 33 MHz | GSC 640×400×4bpp gray | **full** (`macpd230`) | **shipped — oracle-backed** |
| Duo 210 | 030 @ 25 MHz (`kCpuHz210`) | same | full | next (clock + box-ID variant) |
| Duo 250 | 030 @ 33 | same, active matrix | full | cheap variant |
| Duo 270c | 030+FPU | CSC 640×480×16bpp color | full | after a CSC port |
| Duo 280/280c | **040** @ 33 | CSC | full | 040 seam reuse |
| **PowerBook 150** | 030 @ 33 | GSC-class 640×480 gray | **"Future" — none** | LC520 method (ROM as the only oracle) |

`MscMemory` already carries `kCpuHz210` and all three Duo 2x0 box IDs
(`MscMemory.h:53-59`); the 210/250 share the `ECFA989B` ROM, so they need an
env selector: today the clock and the box ID are written straight into
`runDuo`'s memory construction (`main.cpp:4363`).

ROMs in `roms/1MB ROMs/` (user-provided, gitignored): Duo 210/230/250
`ECFA989B`, Duo 270c `0024D346`, Duo 280/280c `015621D7`, PB150 `FDA22562`.

**PG&E dumps (local, never committed — same policy as `roms/cuda/`):**

- `roms/pge/pge_boot.bin` — 512 B PG&E 68HC05 boot mask ROM, CRC32
  `62d4dfed` / SHA1 `79dc721651bf47aec53f57885779c84c4781761d`
  (MAME `m68hc05pge.cpp`). The PG&E's *main* firmware is uploaded by the
  system ROM over SPI at every boot — only this bootloader is silicon.
- `roms/pge/duobatid.bin` — 8 B Dallas DS2400 battery serial, CRC32
  `7545c341` / SHA1 `61b094ee5b398077f70eaa1887921c8366f7abfe`
  (`macpwrbkmsc.cpp` ROM_START). Read over the PG&E's 1-Wire (port E bit 7);
  modelled as a real 1-Wire slave (`PgePmu::Ds2400`, MAME `ds2401.cpp` state
  machine on the MCU cycle clock), including MAME's two transport spins
  (80 µs host after a REQ edge, 20 µs PMU after an ACK edge).

## Address map (Duo 2xx; PB150 expected close — verify against its ROM)

From `macpwrbkmsc.cpp:588-627` + `msc.cpp:30-38` + the `gsc.cpp` map (MSC
device mapped at `$40000000`, GSC at `$50000000`). The as-built copy with the
POM68K decode notes is `MscMemory.h:9-19`.

| Range | Device |
|---|---|
| `$40000000-$400FFFFF` (mirror ×16 through `$4FF…`) | ROM, `rom_switch_r` overlay behaviour like djMEMC |
| `$50F00000-$50F01FFF` | **VIA1** (MSC-internal `mscvia_device` — the CB1 quirk, below) |
| `$50F04000-$50F05FFF` | SCC 8530 (`scc_r/scc_w`) |
| `$50F06000-$50F07FFF` | SCSI pseudo-DMA (`scsi_drq`) |
| `$50F10000-$50F11FFF` | NCR 5380 registers |
| `$50F12000-$50F13FFF` | SCSI DRQ mirror |
| `$50F14000-$50F15FFF` | **ASC-MSC flavour** (`asc_msc_device` — a 4th ASC variant next to V8/Sonora/IOSB) |
| `$50F20000-$50F21FFF` | **GSC registers** (GSC internal `$00F20000`) |
| `$50F26000-$50F27FFF` | **pseudo-VIA2** (the PMU handshake lives on its port B) |
| `$50FA0000-$50FA0003` | `power_cycle_w` |
| `$5FFFFFFC` | box ID: Duo 210 `$A55A1004`, 230 `$A55A1005`, 250 `$A55A1006`, 270c `$A55A1002`, 280 `$A55A1000` |
| `$60000000-$6001FFFF` (mirror `$0FFE0000`) | GSC VRAM, 128 KB |

## The PG&E power manager

68HC05-family MCU ("PG&E"), MAME device `m68hc05pge`; POM68K's clone is
`src/M68hc05Pge.*` (a separate interpreter, not a subclass of `M68hc05` —
different address width, stack window, vectors, map and peripherals) plus
`src/PgePmu.*` for the board wiring.

- **Host comms = SPI slave**, not bit-banged GPIO: the MSC pseudo-VIA2 port B
  bit 1 = `/PMU_ACK`, bit 2 = `/PMU_REQ` (`macpwrbkmsc.cpp:23-26`), data
  through the VIA1 shifter (`Via6522::extShiftCB1` is the matching half). This
  is the Cuda transport shape with a hardware shifter on the MCU end.
- On-chip: 11 GPIO ports (A-L), ADC (battery voltage + the non-linear
  temperature sensor — lookup table excerpted at `macpwrbkmsc.cpp:36-66`),
  PLM timers (backlight brightness: PLM1 `$7F`→`$26`, PLM2 `$01`→`$5A`,
  PLM1+PLM2 always `$80`), PWM (A0 charge current, B0 contrast `$33`-`$C5`),
  1-Wire master (DS2400 battery ID), seconds/RTC, NVRAM — **the PRAM lives in
  the PMU here**, like Cuda.
- Trackball X/Y/button counters read by the PMU (`read_tbX/Y/B`) — input
  reaches the guest THROUGH the PMU, which is also the ADB controller on Duos.
  POM68K routes the KEYBOARD through the real matrix since 2026-08-13 and the
  POINTER through the counters since 2026-08-14 (`PgePmu::mouseMove`;
  `POM68K_PGE_ADBMOUSE=1` restores the old ADB-cell route, where the guest's
  Mouse global never moved once). **The counters are LATCHED, not drained on
  read**: one frame's accumulated motion moves into `$15`/`$16` at 60 Hz and
  stays there for the whole frame, which is what MAME's `vbl_w` does. Drained
  on read instead, the firmware — which reads a register more than once per
  sample — gets the delta on one read and zero on the next, and which one it
  acts on is a race: measured, two directions out of four worked and the
  other two moved the pointer nowhere.
- Port wiring (`macpwrbkmsc.cpp:773-789`): pull-ups port C `$FF`, port E `$80`
  (bit 7 = the 1-Wire bus).

## MSC ASIC notes

- VIA1 is MSC-internal with a **customized CB1 interrupt** (`msc.h:19-26`),
  matching the leaked System 7.1 source — see the /PMU_INT level finding above.
- VIA2 is a pseudo-VIA (`PseudoVia::Flavour::Msc`).
- MSC embeds the sound (`asc_msc_device`) and wants an "audio active" signal
  into power management (`msc.cpp:129` — sleep must not trigger while sound
  plays). That matters for milestone 6.
- `rom_switch_r` overlay + `power_cycle_w`: the machine can power-cycle itself;
  reset semantics go via MSC, not a global line.

## Milestones

1. ✅ `duo_trace` boots the ROM to first PMU contact (no PG&E dumps needed) —
   `MscMemory` skeleton, `MscCpu`, tracer on the `q605_trace` pattern.
2. ✅ **PG&E LLE** (`M68hc05Pge` + `PgePmu`) + `pge_boot.bin` → firmware upload
   handshake completes, RTC/PRAM served, boot proceeds — including the mid-boot
   BORG v2 re-upload and the /PMU_INT level semantics.
3. ✅ **GSC video decode** (`MscMemory::decodeScreen`, 1/2/4 bpp per
   `gsc.cpp`) → `duo230_boot_etalon` GREEN 2026-07-31 (menu bar 0.04 dark,
   desktop 0.43, SCSI 3448 commands, GSC mode 2).
3b. ✅ **The 37th profile, 2026-08-06** — `runDuo` + `MscMachine` in `main.cpp`
   (the loop ticks the machine while `cpuHeld()`, like the Egret loops), the
   `kProfiles` row (`ECFA989B`, group *MSC + PG&E*), `MachineKind::Duo`, the
   JIT engine menu (`MscCpu` exposes the same `jit()/engine()/setEngine()` trio
   as the other 030s), `SnapMachine::Duo230 = 37` + the save/load pair (gated
   in `savestate_030_test`: the 32 KB PG&E SRAM carrying the BORG v2 upload
   round-trips, plus a 3 M-cycle determinism check), and the battery file
   `<image>.duo230.pram` — the PG&E's own RAM + SRAM in MAME's layout with its
   `$91` power-flag scrub (`MscMemory.cpp` `loadPram`, gated by
   `msc_parity_test`).
   Found by the save-state walk and fixed the same day: `MscMemory::pmuReq_`
   was not serialized while `PgePmu::reqLevel_` — the PMU's view of the *same
   wire* — was, so a restore desynced the two ends. It never failed a test
   (REQ is idle-high at most sample points), which is the whole reason it is
   written down.
   **Still absent, machine-side API gaps rather than shell gaps:** no floppy
   (the drive lives in the Dock; `MscMemory` has no SWIM), no drive sounds, no
   live CD-bay swap (`attachCdromEmpty` absent), and `mouseButton(bool)` takes
   no index so button 1 is dropped (`MscMemory.h:134`).
4. ✅ **Input through the PMU**, both halves. The **keyboard matrix**
   (2026-08-13, `PgePmu`): rows selected on port C, columns on port A (X0-X7)
   and port B bits 0-2 (X8-X10), modifiers on port B bits 3-7, all active low,
   from MAME's `Y0`-`Y7` + `keyb_special` tables; `keyEvent` takes Mac virtual
   key codes like every other machine and drops what this keyboard does not
   physically have. The **trackball** (2026-08-14): the quadrature counters
   above, latched at 60 Hz. Both gated by `duo_persist_etalon`, which
   dismisses the boot alert with Return, creates a folder with Cmd-N, and then
   steers the pointer into the Finder's `Special` menu to shut the machine
   down. Two rules for anyone steering it, both paid for: inject one delta and
   wait for the GUEST to answer it (the latch only says the hardware presented
   the motion; steering on that builds a backlog that lands as one jump), and
   never let the last step be ±1 — that is below System 7's mouse-scaling
   floor, so the pointer stops moving and a halving loop never converges.
   A dedicated `duo230_input_etalon` is still worth having: the pointer's only
   coverage today is inside the persist leg.
5. ⬜ Variants: Duo 210/250 (trivial, § *Why this order*), 270c (CSC), 280
   (040), then **PB150** (GSC-480 + IDE, `$A55A` probing from its own ROM, no
   oracle).
6. 🟨 **The actual point — power management observable.** The **CPU
   power-down half is done** (2026-08-13): `power_cycle_w` was a milestone-1
   `fprintf`, so when the idle System asked to be cycled — 58 s into an idle
   Finder — the ROM's `bra.b *` at `$x88B14` never ended and the machine
   froze with the interrupt mask at 7. MAME `msc.cpp:191-206`: 0 and
   `$5A000000` pulse the CPU's reset line (the overlay is NOT re-armed —
   the vectors come from the RAM at `$0`), anything else halts for a full
   system sleep. `duo_soak_etalon` covers it: 180 s of Mac clock across many
   power-down cycles. Still open: the FULL sleep (port G bit 5) and the wake
   event, i.e. `duo230_sleep_etalon` proper. No other machine can test it.
   **First measurement, 2026-08-14** — `PgePmu::setClamshell(false)` drives
   port F bit 3 (the lid; MAME hard-wires it open). Closing it **holds the
   68030 within 5 s**, so the firmware plainly acts on the switch — but the
   System runs no sleep procs on the way: not one write command reaches the
   disk and the mounted volume's `vcbFlags` dirty bit is untouched, where a
   real PowerBook flushes everything before the power goes. Re-opening the lid
   does not wake the machine either. Those two are the milestone.
   *Separately, and NOT part of sleep:* a machine reset must scrub the PG&E's
   `$91` power flag or the mask ROM takes its RESUME path and STOPs at `$FE0D`
   for ever (`PgePmu::reset`, 2026-08-14). Six-second reproduction: run the
   ROM 600 frames, `cpu.hardReset()`, watch `cpuHeld()`.

## Open questions (answer from ROM traces, not guesses)

- The **Power Manager protocol document** is missing. Without it, what a
  selector's payload is supposed to contain is guesswork and the PG&E
  disassembly is the only oracle. This is the ceiling on milestones 4 and 6.
- PB150: exact GSC flavour, IDE decode window, box ID, and whether its PMU
  wants a different firmware image from the Duo one (`FDA22562` will say).
- MSC II vs MSC differences for the 040 Duos (280/280c).
- DFAC presence on the Duo audio path (`dfac.h` is included by the driver —
  check whether boot needs its I2C ACK the way Cuda 2.35 does,
  `pom68k-cuda-0417-wedge`).
