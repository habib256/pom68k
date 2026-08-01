# Mac IIfx / Quadra 900-950 bring-up — the Apple PIC IOP + OSS brick

**Goal.** The one brick that unlocks three machines at once
(`docs/68K_FAMILY_SCOPE.md` §3): the **Mac IIfx** (1990, fastest 68030 Mac,
40 MHz, "F19" — platform #12: OSS interrupt controller + two IOPs + SCSIDMA),
and the **Quadra 900/950** (platform #9's Q700 board grown to a tower: same
DAFB/discrete-040 back end POM68K already ships, plus the *same two IOPs*).
This is the `docs/LCII_HARDWARE.md`-style blueprint; everything below is read
from MAME `machine/applepic.cpp`, `apple/maciifx.cpp`, `apple/scsidma.cpp`,
`apple/macquadra700.cpp` (R. Belmont / AJR, BSD-3-Clause) — cite file:line
when porting.

Status 2026-08-01 (night): **M1-M3 and M5 done, gated — the IIfx BOOTS
SYSTEM 7.6 TO THE FINDER** (`iifx_boot_etalon`, screenshot-verified;
Finder in ~26 guest-seconds). The chain with zero HLE on the wire:
ADBReInit → IOP mailbox → the SWIM PIC's real firmware (uploaded by the
ROM, verified byte-perfect: SCC vector $040E at ROM+$5F471, SWIM vector
$5000 at ROM+$5A7EE) → GPIO bit-bang → `AdbLine` keyboard+mouse
answering. Earlier gates: `r65c02_test`, `applepic_test`,
`iifx_post_etalon`. The SCSIDMA $00 handshake/ODR aliasing bug and the
$15D(A3) ADB spin are in `CHANGELOG.md` § 2026-08-01. **The §6 WAI/STP
question is CLOSED**: a capstone sweep over both firmware blobs finds
zero WAI/STP. **M6 done too — the IIfx is the 34th profile** (GUI machine
loop with both IOP cycle counters, `SnapMachine::IIfx = 34` + save states
carrying the IOP RAM, ROM dispatch on the 512 KB `$4147DD77`, gate
`iifx_input_etalon`). Remaining: M7 (Q900/950), M4 deferred (A/UX-only
true DMA; also dedupe the multi-ID attach mirror — 7.6 mounts all seven
copies).
ROMs on hand and verified in `roms/`: IIfx `4147DD77` (512 KB), Q700&900
`420DBFF3`, Q950 `3DC27823` (1 MB).

**The headline finding of the recon** — the reason this brick is cheaper than
it looks: **the IOP firmware needs no dump.** The PIC's internal 64 KB map is
*all RAM* plus registers (`applepic.cpp:63-77`); the host ROM downloads the
65C02 firmware through the shared-RAM window at boot, exactly like the Duo's
BORG upload (`docs/DUO_BRINGUP.md`). Unlike Egret/Cuda there is no
non-distributable MCU ROM: the IIfx system ROM we already have IS the
firmware source. Verify the upload byte-perfect against the host ROM, the way
the Duo gate does.

---

## 1. The Apple PIC (343S1021) — one device, instantiated twice

NCR standard-cell ASIC: **R65C02 core at clock/8** (C15M/8 = 1.9584 MHz,
`applepic.cpp:81`), 2 KB-class shared RAM (see map), 2 DMA channels,
host/peripheral mailboxes, one timer. MAME models it as a self-contained
device (`applepic.cpp`, 578 lines) — port it as `src/ApplePic.*` the same
way.

### Internal 65C02 map (`applepic.cpp:63-77`)

| Range | What |
|---|---|
| `$0000-$6FFF` (mirror `$8000`) | RAM |
| `$7000-$77FF` | RAM (no mirror) |
| `$7800-$7FFF` (mirror `$8000` → `$F800-$FFFF`) | RAM — **the 6502 vectors land here**, uploaded by the host |
| `$F010-$F013` | timer: count lo/hi, latch lo/hi; writing latch-hi arms one-shot `latch*8+12` clocks; continuous mode re-arms `(latch+2)*8` (`applepic.cpp:282-322`) |
| `$F020-$F02F` | DMA channels ×2: control / map lo/hi / tc lo/hi (tc is 11-bit) |
| `$F030` | scc_control — **bit 0 = bypass mode**, bit 7 → gpout1 |
| `$F031` | io_control |
| `$F032` | timer/DPLL control — bit 0 = timer continuous; bit 1 → gpout0; bits 2-3 read gpin |
| `$F033/$F034` | int mask / int flags (write-1-to-clear) |
| `$F035` | host_reg: write bits 2/3 = INTHST0/1 → host interrupt |
| `$F040-$F04F` | device registers → peripheral read/write callbacks (non-bypass mode) |

6502 IRQ sources (`applepic.cpp:20-26`): 1 = DMA1, 2 = DMA2,
3 = peripheral (SCC /INT in non-bypass), 4 = host, 5 = timer.
`int_reg_r` returns `flags & mask` — MAME's comment warns the firmware
gets confused otherwise (`applepic.cpp:484-488`).

### Host window (32 bytes, `applepic.cpp:125-217`)

Decoded by offset bits, byte lanes doubled in the host map
(`.umask32(0xff00ff00)` + `.umask32(0x00ff00ff)`, `maciifx.cpp:196-197`):

- bit 4 set → device regs `$0-$F` **in bypass mode only** (the host talks
  straight through to the SCC; boot path).
- bit 2 set → shared-RAM data port, auto-increment when status bit 1 set.
- bit 1 set → status/control: read = PINT/REQ status + host-int flags;
  write: bit 2 = /RSTPIC (releases the 65C02), bit 3 = interrupt the IOP
  (IRQ_HOST), bits 4-5 ack the two host interrupts.
- bits 0/1 clear → RAM address register lo/hi.

### DMA (`applepic.cpp:386-422`)

1 byte per channel per 8 clocks. Control bits `DMAEN/DREQ/DMADIR/DENxONx`
(`:28-32`): direction I/O↔RAM, chaining (channel B auto-enables when A
completes), IRQ on tc=0. DREQ lines: SCC WREQA/WREQB (inverted), SWIM
`dat1byte` (`maciifx.cpp:475-486`).

### GPIO — the ADB path on the IIfx

The **SWIM PIC's** gpout0 drives the ADB line (inverted) and gpin reads it
back (`maciifx.cpp:483-484`): **the IOP firmware bit-bangs ADB in 65C02
code.** MAME feeds this to its HLE `macadb`; POM68K wires it to the real
`AdbBus`/`AdbLine` devices instead — LLE on both ends of the wire, the same
step up the Egret work made.

---

## 2. The IIfx board (`maciifx.cpp`)

68030 @ 40 MHz, VIA1 = R65NC22 at C7M/10 = 783.36 kHz, RTC 343-0042 on VIA
port B (same wiring as the Mac II), ASC (base flavour, IRQ → OSS input 8),
SWIM1 (POM68K has it), SCC 85C30 (POM68K's 8530 model to start), **no VIA2 —
the OSS replaces it**, **no onboard video** — boots on a NuBus card
(MAME defaults slot 9 to `mdc824`; POM68K reuses `TobyVideo`/`DeclRom` from
the Mac II platform).

### Address map (`maciifx.cpp:191-206`)

| Range (mirror `$00F00000`) | Device |
|---|---|
| `$40000000-$4007FFFF` | ROM (+ overlay at 0 until first read, `:169-186`) |
| `$50000000` | VIA1 |
| `$50004000` | **SCC PIC** host window |
| `$50008000` | SCSIDMA |
| `$50010000` | ASC |
| `$50012000` | **SWIM PIC** host window |
| `$50018000` | BIU — MAME stubs reads as 0 (`:320-327`) |
| `$5001A000` | OSS |
| `$50024000-$50027FFF` | **must BUS ERROR** — the ROM probes this to know it's an FMC machine (`:204`) |
| `$50040000` | VIA1 mirror |

### OSS interrupt controller (`maciifx.cpp:329-398`)

A flat `$400`-byte register file. `regs[0..15]` = per-input **priority**
(IPL level the input requests); `regs[$202]/[$203]` = pending flags (inputs
8-15 / 0-7); `regs[$200]` bit 7 = interrupt active; write `$207` acks the
60.15 Hz tick. On any input change: scan all 16, drive the highest priority
level into the 68030 (level-triggered, previous line cleared first).

Inputs: 0-5 = NuBus slots 9-E, 6 = SWIM PIC, 7 = SCC PIC, 8 = ASC,
9 = SCSIDMA, 10 = 60.15 Hz tick (also pulses VIA1 CA1, `:369-374`),
11 = VIA1.

### SCSIDMA (343S0064, `scsidma.cpp` — 424 lines)

NCR **53C80** cell + Apple handshake logic for blind `MOVE.L (A0),(A2)+`
transfers + arbitration. **"No shipped version of MacOS supports the full
DMA mode. But A/UX does"** (`scsidma.cpp:12`) — so the Mac OS boot needs the
5380 register model (POM68K has `Ncr5380` + pseudo-DMA already) behind the
SCSIDMA control/handshake registers; full DMA can wait.

---

## 3. The Quadra 900/950 delta (`macquadra700.cpp`)

The Q900 ("Eclipse") / Q950 ("Zydeco") are the Q700 state class POM68K
already implements (platform #9: discrete 040 + DAFB + TurboSCSI + Egret),
**plus** the two IOPs replacing the direct SCC/SWIM decode:

- `quadra900_map` (`:574-593`): SCC PIC at `$5000C000`, SWIM PIC at
  `$5001E000`; DAFB VRAM window 2 MB (`$F9000000-$F91FFFFF`).
- **ADB stays on the Egret** (`eclipse_state` has `m_egret`, `:190-216`) —
  the IOPs only carry SCC and SWIM here. No new ADB work.
- Q950: `via_in_a_q950` (`:671`), `DAFB_Q950` flavour at 50 MHz/2 (`:937`),
  ROM `3DC27823`. Q900 shares the Q700 ROM (`#define rom_macqd900
  rom_macqd700`, `:954`).
- 5 NuBus slots instead of 2 (`:912`).
- 16 SIMM slots, no soldered RAM (`:926`).

So: **IIfx = new platform #12** (OSS instead of VIA2), **Q900/950 = two new
profiles on platform #9** once `ApplePic` exists.

---

## 4. The 65C02 core — vendor POM2's `M6502`, not POMIIGS's `CPU65816`

`docs/68K_FAMILY_SCOPE.md` §3 designated POMIIGS `CPU65816` (emulation mode)
as the candidate. **Recon supersedes that**: MAME's PIC core is an
**R65C02** (`applepic.h:9`) with the Rockwell bit ops `RMB/SMB/BBR/BBS` —
and 65816 emulation mode does *not* have them (those opcode columns are
`ORA [dp]` etc. on a 65816). Meanwhile the sibling **POM2 `M6502`**
(2115 lines, GPL, same author) has a runtime **CMOS 65C02 mode including the
full Rockwell set**, cycle counts cited against MAME `ow65c02.lst`, and two
ready gates: Klaus's 65C02 extended-opcodes image
(`POM2/tests/65C02_extended_opcodes_test.bin`, SHA-pinned, success trap
`$24F1`) and a Tom Harte `wdc65c02` manifest. Vendor **that**.

Adaptation is small and mechanical — the only couplings are
`memory->memRead/memWrite` (→ `read8`/`write8` callbacks, the `M68hc05`
pattern) and four Apple II-isms to strip (`advanceCycles`,
`getCycleCounter`, `iieModeFlags`, `busStateSummary`, `Logger.h`).

One personality question to settle in M2, not M1: POM2 implements the WDC
halts **WAI/STP** (`$CB/$DB`); MAME's `r65c02` treats those as NOPs. Keep
them behind a flag and scan the uploaded firmware for `$CB/$DB` before
choosing the PIC's personality.

---

## 5. Milestones — each gated before the next (house rule)

- **M1 — `src/R65c02.*`** vendored from POM2 `M6502` (CMOS default, bus
  callbacks, no Apple II couplings). Gate: `r65c02_test` — Klaus's
  65C02 extended image (checked into `tests/assets/`; it is GPL test code,
  not a ROM) run to the `$24F1` success trap, plus the base 6502 functional
  image. Label `unit`, no machine assets needed.
- **M2 — `src/ApplePic.*`**: host window, shared RAM + auto-increment,
  status/control, timer, 2-channel DMA, interrupt mask/flags, GPIO, bypass
  mode. Gate: `applepic_test` — upload a small 65C02 program through the
  host window, release /RSTPIC, prove: mailbox echo (host int via `$F035`,
  IOP int via status bit 3), timer IRQ cadence, DMA loopback against a fake
  peripheral, bypass-mode pass-through.
- **M3 — `src/IIfxMemory.*` + platform loop**: map above, OSS, BIU stub,
  the `$50024000` bus-error window, VIA1/RTC/ASC/SWIM1/SCC wiring, ROM
  overlay. Gate: `iifx_post_etalon` — the ROM POST completes its IOP
  firmware uploads (verified byte-perfect) and reaches the boot chime /
  SCSI scan. Build the scratchpad tracer first
  (`pom68k-rom-debug-workflow`).
- **M4 — SCSIDMA** (`src/ScsiDma.*` wrapping `Ncr5380`): register model +
  blind handshake path (Mac OS path only; full DMA deferred with a LOUD
  stub). Gate: unit test + the POST's SCSI probe.
- **M5 — ADB through the SWIM PIC GPIO** ↔ `AdbBus`/`AdbLine`. Gates:
  `iifx_boot_etalon` (Finder, System 7.x reference image) then
  `iifx_input_etalon` (cursor + KeyMap, 8-byte window —
  `pom68k-false-green-wide-assert`).
- **M6 — profile plumbing**: `kProfiles` row, `SnapMachine` tag, save
  states (`ApplePic::visit` carries the 64 KB RAM, R65C02 registers, DMA
  and timer phase — the Cuda lesson: MCU↔host phase is load-bearing,
  `pom68k-mactv-gate-broken`). The 34th profile.
- **M7 — Quadra 900/950**: platform landed, **boot not yet reached**.
  `Q700Memory::Model {Spike, Q900, Q950}` carries the whole Eclipse front
  end (two `ApplePic`, `AdbLine`, `Egret` on VIA1 CB1/CB2, the second
  53C96 bus, VIA1 PA identities `$D0`/`$90`, no DFAC on VIA2 PB, the
  $1000-wide IOP host windows at +$0C000 / +$1E000, Q950 at 33.333 MHz);
  `q700_boot_etalon <q700|q900|q950>` selects the machine.

  **Where it stands (2026-08-01, measured):** the Q900 ROM POSTs, brings
  up 640×480 DAFB video, uploads **both** IOP firmwares and releases
  them. The SCC IOP runs its real firmware (2484 B) and passes the ROM's
  bypass-mode walking test on the SCC's WR2/RR2. The SWIM IOP's firmware
  is verified **byte-perfect**: the ROM's upload script is
  `[len][addr:2][data…]` chunks at ROM `$5A7EB`, and all **54 chunks /
  11516 bytes** land exactly (`q900_swimpic.ram` vs the ROM — the check
  is in this file's history, redo it before suspecting the upload).

  **The wall**: the SWIM IOP's 65C02 ends in its own **BRK panic handler**
  (`$5060`: `tsx; lda $0106,x; bit #$10; bne $5069` — it tests the pushed
  B flag and hangs at `$5069` walking the stack). So the firmware executed
  a `$00` — a control-flow divergence, not a bad upload. Its interrupt
  unit reads `flags=$30 mask=$3E` (HOST + TIMER pending and unmasked), and
  the host side sits at ROM `$4080A8E6` (`btst #5,$15d(a3)`) with VIA2
  `IER=$01` — i.e. the ROM has armed the SWIM IOP's host interrupt (VIA2
  CA2) and is waiting for a reply the firmware never sends.

  **Traced to the instruction (2026-08-01, `POM68K_Q900_IOPBRK=1`)** — the
  tooling is now in-tree: `R65c02::setTrace()/onBrk/pcTrail()` keeps a
  256-deep ring of executed PCs and dumps it on the first `$00`.
  The firmware reaches the BRK at **`$0042`** (page zero) through a normal
  epilogue: `$53FC PLY; PLX; PLA; $53FF RTS`, whose `RTS` read a garbage
  return address. Registers there: `A=41 X=FF Y=59 SP=FF P=30`.

  **Two hypotheses this KILLS — do not re-open them without new data:**
  - *Not a stack-discipline underflow.* SP was `$FA` entering the
    epilogue and `$FF` after its five pulls — the depth was exactly
    right. The bad return address was simply what sat at `$01FE/$01FF`,
    the very bottom of the stack, i.e. the routine returned one frame
    PAST its own top level.
  - *Not an interrupt storm.* The last 256 executed PCs contain no
    `$504E` (the firmware's IRQ vector) at all.

  What it was doing instead: spinning in the state machine at
  `$5418`-`$5436` (`jsr $54B0` / `jsr $54BF` / `jsr $54E7`, then
  `beq $5418`), whose inner routine compares `$4E87,x` against `$4E87,y`
  — a table/buffer at `$4E87`-`$4E9F` that never reaches its end
  condition, so the loop eventually falls out the wrong way.
  **Next step**: find which `ApplePic`/SWIM register read feeds that
  `$4E87` state. Suspects, refined:
  1. the SWIM device-register mapping behind `readPeriph`/`writePeriph`;
  2. the `ApplePic` timer's reload semantics;
  3. `dat1byte` → `reqa_w`/`reqb_w`, which MAME wires to **both** DMA
     channels here (`macquadra700.cpp:879-880`). **POM68K's `Swim1` has
     no `dat1byte` callback at all**, so this one is a real extension of
     the floppy controller, not a wiring line.

  Also still open once it boots: profiles 35/36 (`kProfiles`,
  `SnapMachine`, GUI loop) — **not registered yet, by the house rule that
  a profile earns its row only with a Finder cell behind it**.

## 5b. The Eclipse's three inherited-rule bugs (M7, all the same class)

Every Q900 bug so far was a **Quadra 700 rule that must not apply to the
tower**. Worth stating as a pattern before the next one:

1. **The SCC interrupt line.** `tick()` re-derived `sccIrq_` from the SCC
   chip every tick. On the Eclipse that line belongs to the SCC IOP's
   *host* interrupt; the chip's `/INT` is a *peripheral* interrupt into
   the IOP. The Q700 line wiped the IOP's interrupt the instant it was
   raised, and the boot waited forever on an `_IOPMsgRequest` ($A087) that
   had in fact been answered.
2. **The SWIM word-write quirk.** The Q700's SWIM1 sits on the odd byte
   lane, so `write16` there delivers only the low half. On the Eclipse
   that window is the SWIM IOP host window, whose 16-bit RAM-address
   register the ROM writes with one `move.w d0,(a2)` (a2 = `$5001E001`,
   ROM `$40804D38`). The quirk dropped the address's high byte, so every
   upload landed in the low 256 bytes, the ROM's 32 KB IOP RAM test
   failed, and the IOP was never released from `/RSTPIC`.
3. **The discrete RTC and the DFAC.** Gone on the Eclipse — the Egret owns
   clock/PRAM/power on VIA1 CB1/CB2, and VIA2 port B carries no DFAC.

The debugging order that found them, cheapest first, is worth reusing:
IOP held/released → IOP cycle counter moving → how many bytes of firmware
each IOP holds → VIA IFR/IER (who is armed, who never fires) → the IOP's
own interrupt flags/mask → disassemble the IOP RAM at its stuck PC.

## 6. Risks, called now

- **MAME's IIfx was long booked "A/UX-only"** (MAMEdev wiki, cited in
  `68K_FAMILY_SCOPE.md`); the current driver carries no MACHINE_NOT_WORKING
  flag, but expect thin oracle coverage on the Mac OS path. Where MAME and
  the ROM disagree, the ROM wins (the LC520 precedent).
- **Mac OS barely uses the machine's extra hardware** (`maciifx.cpp:11-14`):
  expect the SCC to run in **bypass mode** and the SWIM/ADB firmware to be
  the real consumers of the IOP model. Don't gold-plate DMA before a gate
  needs it.
- **ASC flavour**: MAME instantiates the base ASC on the IIfx — check which
  POM68K `Asc` flavour matches before wiring ($BB vs $BC vs plain).
- **Timer/DMA phase**: both are counted in PIC clocks (host clock /8 for
  instructions, /8 per DMA byte). Keep them slaved to `emuCycles` from day
  one; the MCU-debt lesson (`pom68k-mcu-lle-clock-drift`) applies verbatim.
