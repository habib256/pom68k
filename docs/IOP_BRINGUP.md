# Mac IIfx / Quadra 900-950 bring-up — the Apple PIC IOP + OSS brick

**Status: DONE.** One brick unlocked three machines, all three shipped as GUI
profiles with save states: the **Mac IIfx** (2026-08-01, OSS + two IOPs +
SCSIDMA — "platform #12" in `IIfxMemory.h:4` is a creation-order label, not a
row number in CLAUDE.md's machine table) and the **Quadra 900 / 950**
(2026-08-02 — the Quadra 700 board wearing the IIfx's front end).
Gates `iifx_post_etalon`,
`iifx_boot_etalon` (Finder on System 7.6), `iifx_input_etalon`,
`q900_boot_etalon`, `q950_boot_etalon`, plus the device gates `r65c02_test`
and `applepic_test`; save states in `savestate_030_test` (IIfx) and
`savestate_040_test` (the Eclipse tail).

**The machines as-built are `DEV.md` § 2.10 (IIfx) and § 2.8 (discrete 040) —
this file is the porting reference and the debugging record**: what MAME says
and where (cite `file:line` when porting), the facts the bring-up established,
and the traps that cost days. Sources: MAME `machine/applepic.cpp`,
`apple/maciifx.cpp`, `apple/scsidma.cpp`, `apple/macquadra700.cpp`
(R. Belmont / AJR, BSD-3-Clause).

**The finding that made this brick cheap: the IOP firmware needs no dump.**
The PIC's internal 64 KB map is *all RAM* plus registers
(`applepic.cpp:63-77`); the host ROM downloads the 65C02 firmware through the
shared-RAM window at boot, exactly like the Duo's BORG upload
(`docs/DUO_BRINGUP.md`). Unlike Egret/Cuda there is no non-distributable MCU
ROM: the system ROM *is* the firmware source. `iifx_post_etalon` verifies the
upload byte-perfect against the ROM (SCC vector `$040E` at ROM+`$5F471`, SWIM
vector `$5000` at ROM+`$5A7EE`).

ROMs verified in `roms/`: IIfx `4147DD77` (512 KB), Q700 & Q900 `420DBFF3`,
Q950 `3DC27823` (1 MB).

---

## 1. The Apple PIC (343S1021) — one device, instantiated twice per board

NCR standard-cell ASIC: **R65C02 core at input-clock/8** (C15M/8 =
1.9584 MHz, `applepic.cpp:81`), **32 KB of internal RAM** mirrored across the
64 KB space, 2 DMA channels, host/peripheral mailboxes, one timer. MAME models
it as a self-contained device (`applepic.cpp`) → `src/ApplePic.*`, whose header
carries the same map with the POM68K-side timing contract.

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
(`.umask32(0xff00ff00)` + `.umask32(0x00ff00ff)`, `maciifx.cpp:196-197`) —
**both** lanes, which is bug #2/#4 in § 5b:

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
`dat1byte`. **On the IIfx `dat1byte` drives channel A alone; on the Eclipse it
drives both** (`macquadra700.cpp:879-880` sets reqa and appends reqb — the line
means "the ISM FIFO can move a byte now", and whichever channel the firmware
enabled is the one that moves). `IIfxMemory.cpp:46` vs `Q700Memory.cpp:62-65`.

### GPIO — the ADB path

The **SWIM PIC's** gpout0 drives the ADB line (inverted) and gpin reads it
back (`maciifx.cpp:483-484`): **the IOP firmware bit-bangs ADB in 65C02 code.**
MAME feeds this to its HLE `macadb`; POM68K wires it to the real
`AdbBus`/`AdbLine` devices instead — LLE on both ends of the wire.

**`gpout1` is NOT floppy head-select.** A 2026-08 static note suspected it
was; current MAME disproves it — SWIM1's own `hdsel_cb` drives
`eclipse_state::fdc_hdsel` (`macquadra700.cpp:815,819`), and in ISM mode that
output is mode bit 5 (`swim1.cpp:341-342`), which POM68K already consumes
through `Swim1::side1()`. Wiring `gpout1` to `Iwm::setSel` would invent a board
connection.

---

## 2. The IIfx board (`maciifx.cpp`)

68030 @ 40 MHz, VIA1 = R65NC22 at C7M/10 = 783.36 kHz, RTC 343-0042 on VIA
port B (same wiring as the Mac II), **discrete ASC** (base flavour, version
`$00` — `IIfxMemory.h:225` `AscV8 asc_{0x00}`; IRQ → OSS input 8), SWIM1,
SCC 85C30, **no VIA2 — the OSS replaces it**, **no onboard video** — it boots
on a NuBus card (MAME defaults slot 9 to `mdc824`; POM68K reuses
`TobyVideo`/`DeclRom` from the Mac II platform).

The address map, the OSS input assignment and the clock domains are in
`IIfxMemory.h:14-38` and `DEV.md` § 2.10; the MAME originals are
`maciifx.cpp:191-206` (map) and `:329-398` (OSS). Two entries worth repeating
because they are load-bearing and non-obvious:

- **`$50024000-$50027FFF` must BUS ERROR** — that is how the ROM recognises an
  FMC machine (`maciifx.cpp:204`).
- **OSS**: a flat `$400`-byte register file. `regs[0..15]` = per-input
  **priority** (the IPL that input requests); `regs[$202]/[$203]` = pending
  flags (inputs 8-15 / 0-7); `regs[$200]` bit 7 = interrupt active; writing
  `$207` acks the 60.15 Hz tick, which also pulses VIA1 CA1 (`:369-374`). On
  any input change: scan all 16, drive the highest priority level into the
  68030, level-triggered, previous line cleared first.

### SCSIDMA (343S0064, `scsidma.cpp`)

NCR **53C80** cell + Apple handshake logic for blind `MOVE.L (A0),(A2)+`
transfers + arbitration. **"No shipped version of MacOS supports the full
DMA mode. But A/UX does"** (`scsidma.cpp:12`) — so the Mac OS boot needs only
the 5380 register model behind the SCSIDMA control/handshake registers. POM68K
implements exactly that subset **inline in `IIfxMemory.cpp:237+`**, not as a
separate `ScsiDma` class; true DMA and the restartable handshake stall are
deliberately absent (`IIfxMemory.h:239-240`).

The trap that cost a day (fixed 2026-08-01): **`$00-$03` is both the handshake
data port and 5380 register 0.** With no DRQ active it must fall through to the
register (`scsidma.cpp:262-310`); swallowing it means selection never puts the
target ID on the bus and the boot scan finds nothing.

---

## 3. The Quadra 900/950 delta (`macquadra700.cpp`)

The Q900 ("Eclipse") / Q950 ("Zydeco") are the Q700 state class
(`Q700Memory::Model {Spike, Q900, Q950}`, `eclipse()` is the one predicate the
code branches on) **plus** the IIfx front end:

- `quadra900_map` (`:574-593`): SCC PIC host window at `$5000C000`, SWIM PIC at
  `$5001E000`, each `$1000` wide and mapped on **both** byte lanes; DAFB VRAM
  window 2 MB (`$F9000000-$F91FFFFF`).
- **ADB stays on the Egret** (`eclipse_state` has `m_egret`, `:190-216`) — the
  IOPs carry SCC and SWIM only, and the Egret replaces the Spike's discrete
  RTC. There is also a second 53C96 bus, and VIA2 port B carries no DFAC.
  `POM68K_Q900_ADB=iop` forces the IIfx-style IOP-only wire for A/B.
- VIA1 PA identity `$D0` (Q900) / `$90` (Q950) vs the Spike's `$C0`, each
  OR'd with the diagnostic-disabled bit 0 — feeding PA0 = 0 sends the ROM down
  the burn-in path (the IIci lesson, `Q700Memory.cpp:229-234`).
- Q950: `via_in_a_q950` (`:671`), `DAFB_Q950` flavour at 50 MHz/2 (`:937`),
  ROM `3DC27823`, 33.333 MHz. Q900 shares the Q700 ROM (`#define rom_macqd900
  rom_macqd700`, `:954`) and runs at 25 MHz.
- 5 NuBus slots instead of 2 (`:912`); 16 SIMM slots, no soldered RAM (`:926`).

`q700_boot_etalon <q700|q900|q950>` selects the machine — one binary, three
gates. A `$3DC27823` ROM pins q950 regardless of `POM68K_Q700_MODEL`, and q950
without that ROM falls back to q700 (`main.cpp:4320-4321`).

---

## 4. The 65C02 core — POM2's `M6502`, not POMIIGS's `CPU65816`

An earlier revision of this file designated POMIIGS `CPU65816` (emulation
mode). **That was wrong**: MAME's PIC core is an **R65C02** (`applepic.h:9`)
with the Rockwell bit ops `RMB/SMB/BBR/BBS`, and 65816 emulation mode does not
have them (those opcode columns are `ORA [dp]` etc. on a 65816). The sibling
**POM2 `M6502`** has a runtime CMOS 65C02 mode including the full Rockwell set,
cycle counts cited against MAME `ow65c02.lst`, and two ready test images.
Vendored as `src/R65c02.*`; the only couplings to strip were
`memory->memRead/memWrite` (→ `read8`/`write8` callbacks, the `M68hc05`
pattern) and four Apple II-isms (`advanceCycles`, `getCycleCounter`,
`iieModeFlags`, `busStateSummary`).

**The §6 WAI/STP question is CLOSED.** POM2 implements the WDC halts
**WAI/STP** (`$CB/$DB`) where MAME's `r65c02` treats them as NOPs. A capstone
sweep over both uploaded firmware blobs finds **zero** `$CB/$DB`, so the
personality is unobservable on this board and the WDC behaviour is kept —
which is also what lets `r65c02_test` run Klaus Dormann's 65C02 extended
image to its `$24F1` success trap, since that image tests WAI/STP
(`R65c02.h:24-30`).

---

## 5. Milestones — what each established (all closed on the Mac OS path)

| M | Deliverable | Gate | The fact it pinned |
|---|---|---|---|
| M1 | `src/R65c02.*` vendored from POM2 `M6502` | `r65c02_test` (Klaus's 65C02 extended image → `$24F1`, plus the base 6502 functional image; `tests/assets/`, label `unit`, no machine assets) | CMOS + Rockwell set required; WAI/STP kept (§ 4) |
| M2 | `src/ApplePic.*` — host window, shared RAM + auto-increment, status/control, timer, 2-channel DMA, int mask/flags, GPIO, bypass | `applepic_test` (uploads a hand-assembled 65C02 program, then proves reset-release, mailboxes both ways, timer one-shot + continuous cadence, DMA loopback, bypass pass-through) | 32 KB RAM, not 2 KB; timer/DMA phase counted in PIC clocks with remainders carried |
| M3 | `src/IIfxMemory.*` + platform loop: map, OSS, BIU stub, the `$50024000` BERR window, VIA1/RTC/ASC/SWIM1/SCC, ROM overlay | `iifx_post_etalon` | The IOP firmware uploads are byte-identical to the system ROM — verify that before suspecting anything else |
| M4 | SCSIDMA | the Mac OS subset folded into M3 (`IIfxMemory.cpp:237+`), no separate class; true DMA + the restartable handshake stall stay deferred as A/UX-only, and `IIfxMemory.h:239-240` still calls that remainder "M4" | Mac OS needs only the 5380 + soft handshake; `$00-$03` must fall through to reg 0 with no DRQ |
| M5 | ADB through the SWIM PIC GPIO ↔ `AdbBus`/`AdbLine` | `iifx_boot_etalon` (Finder on 7.6, ~26 guest-seconds) | Zero HLE on the wire: ADBReInit → IOP mailbox → real SWIM PIC firmware → GPIO bit-bang → `AdbLine` |
| M6 | Profile plumbing: `kProfiles` row, `SnapMachine::IIfx = 34`, save states (`ApplePic::visit` carries the 32 KB RAM, R65C02 registers, DMA and timer phase) | `iifx_input_etalon` (cursor + KeyMap, 8-byte window) | MCU↔host *phase* is load-bearing state, not derivable — the Cuda lesson |
| M7 | Quadra 900/950 = the Eclipse front end on `Q700Memory` | `q900_boot_etalon`, `q950_boot_etalon`; `SnapMachine::Quadra900/950 = 35/36` | § 5b — four Quadra 700 rules that must not apply to the tower |

The Q950 came up the same day as the Q900, off the same one-line fix, at
640×480×**8** where the Q900 lands at 1 bpp — so it is also the tower gate that
exercises a colour DAFB path. Nothing Zydeco-specific had to be touched.

## 5b. The Eclipse's four inherited-rule bugs — the pattern

Every Q900 bug was a **Quadra 700 rule that must not apply to the tower**.
Worth stating as a pattern before the next one:

1. **The SCC interrupt line.** `tick()` re-derived `sccIrq_` from the SCC chip
   every tick. On the Eclipse that line belongs to the SCC IOP's *host*
   interrupt; the chip's `/INT` is a *peripheral* interrupt into the IOP. The
   Q700 line wiped the IOP's interrupt the instant it was raised, and the boot
   waited forever on an `_IOPMsgRequest` (`$A087`) that had in fact been
   answered.
2. **The SWIM word-write quirk.** The Q700's SWIM1 sits on the odd byte lane,
   so `write16` there delivers only the low half. On the Eclipse that window is
   the SWIM IOP host window, whose 16-bit RAM-address register the ROM writes
   with one `move.w d0,(a2)` (a2 = `$5001E001`, ROM `$40804D38`). The quirk
   dropped the address's high byte, every upload landed in the low 256 bytes,
   the ROM's 32 KB IOP RAM test failed, and the IOP was never released from
   /RSTPIC.
3. **The discrete RTC and the DFAC.** Gone on the Eclipse — the Egret owns
   clock/PRAM/power on VIA1 CB1/CB2, and VIA2 port B carries no DFAC.
4. **The SWIM word-write quirk again — on the READ side.** #2 guarded
   `write16` with `!eclipse()` and left `read16` (three dozen lines away)
   applying the Spike rule. ROM `$408050CA` `move.w (a2),d3` then read the
   IOP's shared-RAM address register as `hi<<8` (`$0203` → `$0200`); the
   following `subq.w #1` aimed the next byte write at **`$01FF`** — the top of
   the 65C02's stack, straight through the return address `jsr $53ED` had just
   pushed. The IOP then `RTS`'d to `$0042` and fell into its own BRK panic
   handler at `$5060`. One `!eclipse()` guard, and the tower boots.
   **A byte-lane quirk is a property of a WINDOW, not of a direction — fix both
   halves in the same edit, and grep for the other one before closing the
   entry.**

**Two hypotheses that trace killed, both correctly, and one wrong lead they
produced.** Not a stack imbalance (SP was `$FA` entering the epilogue and `$FF`
after five pulls — exactly right) and not an interrupt storm (no `$504E` in 256
PCs). But the *lead* drawn from them was wrong: the state machine at
`$5418`-`$5436` comparing `$4E87,x` to `$4E87,y` was running fine and no SWIM
register fed it anything bad — it was killed on the way out. Recorded because
the next reader will otherwise re-open `$4E87`.

**The bring-up tooling that closed it** (all in-tree, all env-gated):
`R65c02::spTrail()` prints the SP beside every PC in the trail — that is what
proved the depth exact and moved suspicion to the bytes;
`ApplePic::watch` / `POM68K_Q900_IOPWATCH=<hex>` names the *writer* of an IOP
RAM byte (`cpu` / `host` / `dma`) and printed `[IOP-WATCH] $01FF <- $00 by
host`; `POM68K_Q900_IOPBRK` dumps a 64-deep ring of host-window touches with the
decoded offset, the shared-RAM pointer at that instant and the 68k PC — which
showed the ROM setting the pointer to `$01FF` and firing. `POM68K_Q900_IOP_TRACE`
and `POM68K_IIFX_*_TRACE` are the coarser eyes; `tests/iifx_trace.cpp` is the
runner.

**The debugging order that found all four, cheapest first, is worth reusing:**
IOP held/released → IOP cycle counter moving → how many bytes of firmware each
IOP holds → VIA IFR/IER (who is armed, who never fires) → the IOP's own
interrupt flags/mask → disassemble the IOP RAM at its stuck PC.

## 6. Risks called at the start — how they landed

- **"MAME's IIfx is A/UX-only"** (MAMEdev wiki): the driver carries no
  MACHINE_NOT_WORKING flag but the oracle coverage on the Mac OS path is thin.
  It held — where MAME and the ROM disagreed, the ROM won (the LC520
  precedent). It is also why the practical ceiling is System 7.6: the IIfx ROM
  is 32-bit dirty and 8.x needs a 32-bit-clean one.
- **"Mac OS barely uses the machine's extra hardware"** (`maciifx.cpp:11-14`):
  correct. The SCC runs in bypass mode; the SWIM/ADB firmware is the real
  consumer of the IOP model. DMA was not gold-plated and did not need to be.
- **ASC flavour**: settled — the IIfx carries the **discrete** ASC, version
  `$00`, like the Mac II (`IIfxMemory.h:225`), not the V8 `$E8` or the
  Sonora/IOSB variants.
- **Timer/DMA phase**: both counted in PIC clocks (input clock /8 for
  instructions, /8 per DMA byte), slaved to `emuCycles` with remainders carried
  as debt from day one (`pom68k-mcu-lle-clock-drift`). That is also what makes
  the save states restore in phase.
