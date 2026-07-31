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

*(An earlier revision of this file claimed the opposite of the third
bullet below — that the PMU interrupt should ride the SPI clock. It was
written before the measurement and is deleted, not softened.)*

- **SR mode 000 shifts IN under an external clock** — stock 6522 behaviour
  (`SR_DISABLED(m_acr)` → `shift_in()`, `:1199`), not an MSC invention, and
  the mode the PMU reply path actually uses. Opt-in (`setMscShiftQuirk`)
  so the Mac II decode is untouched.
- **MISO is driven only while the VIA shifts OUT** (MAME hangs
  `spi_miso_w` off the CB2 *output* callback, whose only caller is
  `shift_out`). Feeding the SR MSB back in shift-IN mode made the PMU read
  its own bytes one exchange later.

- **The PMU interrupt must NOT ride the SPI clock** — and this one is a
  deliberate, measured departure from MAME's literal wiring. MAME binds
  the PG&E's SCK to `msc_device::cb1_w` → the *interrupting* `write_cb1`.
  With that, every shift edge sets `IFR.CB1`; the driver re-enables IER at
  the end of each transaction with the flag still pending; its ISR issues
  readINT (`$78`) again; repeat. Same build, 5 G cycles: **interrupt half
  ON → 176 back-to-back `$78`, 0 SCSI selects; OFF → 1122 selects, 555
  commands, the System loads off disk.** `msc.h` models
  `write_cb1_noint` + a separate `pmu_int` (port F bit 2) for exactly this
  reason, and that is what we implement. `POM68K_PGE_CB1INT=1` restores
  the MAME-literal wiring for A/B.

DS2400 battery serial is now a real 1-Wire slave (`PgePmu::Ds2400`, MAME
`ds2401.cpp` state machine on the MCU cycle clock) fed from
`duobatid.bin`. MAME's two transport spins (80 µs host after a REQ edge,
20 µs PMU after an ACK edge) are reproduced.

**Disproved, kept so nobody re-tries it**: port A bit 5 is the power key
when no matrix row is selected, and `macpwrbkmsc.cpp` returns the literal
`0xdf` (bit 5 clear) for "not pressed". Implementing that hangs the boot
dead at `$408B98F2` with Ticks frozen at 0 — the firmware reads it as
"power key held". Bit 5 must read **1** for released, so a blanket `$FF`
is correct.

## Where it stops today: **ADBReInit**, and the next thread

Identified, not guessed. The stall PC `$4080A870` is the tail of
**ADBReInit**:

```
$4080A846  move.b #$24,($15d,A3)   ; ADBBase flags: bit 5 = init running
$4080A868  bsr    $4080a8f0        ; kick the ADB reset off
$4080A86C  andi.w #-$701,SR        ; interrupts on
$4080A870  btst   #$5,($15d,A3)    ; …and wait for bit 5 to clear
$4080A876  bne    $4080a870
$4080A878  bsr    $4080aa96        ; (never reached)
```

Boot state at that point: the System has loaded off disk (1122 SCSI
selects, 555 commands), QuickDraw is up (`ScrnBase = $60000000`, a live
MainDevice/PixMap at 4 bpp, rowBytes 320) and the desktop pattern is
painted. The ROM's own early ADBReInit completed; it is the **System's**
that hangs. Meanwhile the host stops issuing /PMU_REQ and only re-polls
`$D9` every ~0.66 s — a timeout retry, not progress.

Cause was the PG&E's **ADB modem cell**: `ADBCR/ADBSR/ADBDR` ($18-$1A)
modelled TDRE and TC on timers but never RDRF — and MAME's `m68hc05pge`
does not either (verified: `m_adbsr |=` only ever sets TDRE and TC), which
is a strong hint that MAME's `macpd230` does not reach the Finder either,
whatever its lack of a NOT_WORKING flag suggests.

**The cell now answers** (`AdbBus` behind it — command level is honest
here: the cell IS a hardware transceiver, so the wire lives inside the
PG&E's silicon and the firmware only exchanges bytes with it). Three
framing bugs had to be fixed in a row, each one found by
`POM68K_PGE_ADBTRACE=1`:

1. **A reply must be its own event.** Folding RDRF into the same timer
   expiry that raises TDRE also clobbered ADBDR while the firmware still
   held the transmitted byte there: the boot regressed from 1122 SCSI
   selects to 0. TX-done and RX-arrived are separate now.
2. **The TC timer overwrote the scheduled reply.** The firmware acks TDRE
   by writing ADBCR, which arms the 50 µs TC timer — wiping a reply
   scheduled off the TDRE expiry. Every reply was silently dropped and
   the cell looked inert. Both expiries chain into the reply now.
3. **Listen data bytes must reach the bus.** Running a Listen R3 with an
   empty payload means the relocation never happens, so the firmware
   re-finds the device at its old address and relocates it again —
   forever. The cell now buffers a Listen's two data bytes and only then
   drives the command.

Result: enumeration converges (Talk R3 → `$62 $05` keyboard, `$63 $01`
mouse) and settles into steady-state autopoll (`$2C` → `$FF $FF`), and
disk activity goes from 555 to **895 SCSI commands**.

Also disproved along the way, kept as a signpost: raising RDRF
unconditionally on transmit-done (`POM68K_PGE_ADBRX=1`) — 0 selects.

**Narrowed hard.** Instrumented with `DUO_PCCOUNT="pc,pc,…"` (execution
counts + the first hits with a clock stamp), which is how the following
came out:

- ADBReInit runs **three** times and the flag-clear at `$4080A958` fires
  only **twice**. `$4080A958` is the **only `bclr #5,($15D,A3)` in the
  whole 1 MB ROM** (verified by pattern search), so nothing else can end
  the wait.
- The third call dies in the FIRST thing `$4080A8F0` does: `$4080AA1A`
  is entered 3 times but its return point `$4080A8F4` only 2. The 16-way
  device-probe loop after it (`$4080A902`) runs 32 = 16×2 times — the
  third call never reaches it.
- Inside, the ADB dispatch `jsr ($5F0)` at `$4080AA48` is entered 835
  times and returns 835 times, so nothing is stuck *in* the dispatch.
- **ADBBase does NOT go stale — an earlier claim here was wrong.** A3 was
  `$5894` at one observation and `$57BC` at another, which looked like the
  waiter holding a relocated pointer. It was an artefact of comparing two
  different stop points: `--stop-at` with a small skip catches the *early*
  ROM-era ADBReInit, which legitimately runs at the older ADBBase. Measured
  at the real hang, `ADBBase ($0CF8) == A3 == $57BC` — they match. The
  re-entrancy theory is dead; `duo_trace` now prints both so the question
  cannot be fudged again.

**The deadlock, caught red-handed.** `$4080AA1A`/`$4080AA22`/`$4080AA28`
are three entry points converging on one dispatch:

```
$4080AA3E  pea     ($163,A3)     ; push a parameter pointer (4)
$4080AA42  movea.l A7,A0
$4080AA44  movea.l $5f0.w,A1     ; the ADB Manager proc
$4080AA48  jsr     (A1)
$4080AA4A  addq.w  #$8,A7        ; pop EIGHT
$4080AA4C  rts
```

It pops **8** bytes having pushed only 4, so the healthy path depends on
the callee at `($5F0)` leaving a 4-byte result behind: `addq` then eats
result+pointer, and the `rts` finds its own return address. Stopped at the
835th (last) `rts`, `[sp+0] = $4080A86C` — **not** `$4080A8F4`. The callee
took an early exit without pushing its result, `addq` swallowed the
routine's own return address, and the `rts` went two levels up: straight
to `$4080A86C`, which is `andi.w #-$701,SR` immediately followed by the
wait loop. The third ADBReInit therefore lands in its wait **without ever
running the 16-way device probe or reaching the only `bclr #5` in the
ROM**. The flag can never clear. That is the whole deadlock.

Caveat on that reading: the two-level return may well be the ROM's own
idiom rather than damage — ADBReInit's wait loop sits exactly at the
return target, which is suspiciously convenient. What is certain is the
*measured* fact: on the third call control reaches the wait without the
device probe having run.

Following the thread one layer deeper (all measured, `DUO_PCCOUNT`):

- `jADBProc ($05F0) = $4080A3DC`, the queue driver. It **never** takes its
  error exit (`$4080A428`: 0 hits); it enqueues 846 times and kicks
  processing 846 times. So nothing is failing to queue.
- `$4080A42C` has the interesting guard: if the queue is idle **and**
  ADBReInit is running (bit 5), it returns doing nothing — during a
  reinit, ADBReInit is supposed to drive the bus itself.
- The send path `$4080A458` jumps through a machine-specific vector
  `($130,ADBBase)`, which on the Duo is **`$408B2E76`** — the ADB→PMU
  bridge. It masks VIA1 CB1 (`move.b #$10,($1C00,A1)`), builds a command
  block on the stack and issues **PmgrOp selector `$20`** — the `$20`
  commands already visible in the `DUO_PMLOG` stream. At the hang VIA1
  IER is `$B3`, so CB1 has been re-enabled; the mask is not the leak.

**That trace is now done** (`POM68K_PGE_SPIBYTES=1` logs every completed
SPI exchange). The third ADBReInit starts at host clk 4 620 607 926 and
**1 729 clocks later issues exactly one PmgrOp `$20`** (`DUO_PMLOG`:
`cmd=$0020 … 00 20 00 03`, i.e. selector $20, length 3). On the wire, at
MCU cycle 74 893 563:

```
out=$00 in=$E2      ; host: $E2
out=$62 in=$00
out=$00 in=$20      ; host: $20   ← the ADB PmgrOp
out=$20 in=$03      ; host: $03   ← length
out=$03 in=$00
out=$00 in=$00
out=$00 in=$00
…then nothing but the idle $D9/$DC poll, for ever
```

(`out` is the PMU, `in` is the host. The PMU echoes each received byte on
the next exchange — the protocol's per-byte ack. An idle poll is three
exchanges: host `$D9`, two zeros, PMU answers `$80`.)

So **the command reaches the PMU and the PMU does nothing with it**: no
ADB transaction on the cell afterwards (the cell only ever shows its own
`$2C` autopoll), no completion, no interrupt (`intEdges` frozen at 859).
The transport is fine — the idle `$D9` polls flow through the same path
before and after.

**The next concrete step** is the PG&E firmware's own handling of
selector `$20`: single-step the MCU from the moment that byte lands
(cycle 74 893 563 above) and find where it decides not to act. Everything
needed is in place — `POM68K_PGE_SPIBYTES` for the wire, `_ADBTRACE` for
the cell, `pcHistory()` on the MCU for its PC ring. What is missing is the
Power Manager protocol document; without it, what selector `$20`'s three
payload bytes are supposed to contain is guesswork, and the firmware
disassembly is the only oracle left.

**The 80 µs host stall is load-bearing.** MAME's `via2_out_b` spins the
68030 80 µs on a /PMU_REQ edge; it looked like optional pacing, so it was
bisected (`POM68K_PGE_SPINUS=0`): without it the machine does not boot at
all — ADBReInit #1 never completes, 0 SCSI selects. Same lesson as the
Cuda transport (`pom68k-mactv-gate-broken`): the MCU/host interleave is
not a free parameter. `POM68K_PGE_SPINUS=<µs>` is left in for phase
experiments, and the sweep says the window is **sharp**:

| host stall | ADBReInit clears | SCSI commands |
|---|---|---|
| 0 µs | 0 | 0 |
| 40 µs | 2 | 895 |
| **80 µs (MAME, default)** | **2** | **895** |
| 160 µs | 0 | 0 |

40-80 µs boots, 160 µs does not boot at all. That knife edge is itself a
hint about the third ADBReInit: it may be the same phase fragility biting
one layer further in, rather than a missing feature.

**Narrowed further**: ADBReInit now completes **twice** (at ~1.4 G and
~2.2 G cycles) and hangs on the **third** call. So the mechanism works;
something about the state it is left in after two rounds does not. Inside
`$4080A8F0`, the exit that clears the flag is `$4080A958`, reached either
when the device map `($14C,A3)` is empty or when the relocation dance at
`$4080A97A`-`$4080A982` finishes. The third call never gets there. Tried
and did NOT fix it (kept anyway, it is a real fidelity gap): honouring the
Listen R3 activator byte in `AdbBus` so a device only moves on `$00`/`$FE`.

Next steps, in order:
1. Instrument the **third** ADBReInit specifically. What the ADB cell
   shows at that moment is *steady-state autopoll* — `$2C` → `$FF $FF`,
   the keyboard being polled with no key down — i.e. the PG&E considers
   ADB fully initialised and is not doing anything the host asked for. So
   the host's third "re-init ADB" request never reaches the PMU: the host
   has stopped asserting /PMU_REQ, its transport is interrupt-driven, and
   the PMU (with nothing to report) has stopped interrupting. That
   chicken-and-egg is the thing to break — find what lets the host start a
   transaction outside its ISR, or what the PMU should still be reporting.
   The PMU command stream is visible with
   `DUO_PMLOG=408898CA` (`$3A $38 $21 $68 $6B $32 $78 $71 $DC $11 $20
   $10`, settling to `$D9`/`$DC`); the ADB cell with
   `POM68K_PGE_ADBTRACE=1`.
2. Feed the matrix keyboard and trackball (milestone 4) — the PG&E is the
   input controller, so `PgePmu::keyEvent/mouseMove/mouseButton` already
   exist and reach the guest through the ADB devices above.
3. GSC decode gate, then the sleep/wake gate.

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
