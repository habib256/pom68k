# PowerBook 140–180 — M50753 Power Manager bring-up

## Boundary of the first milestone

`M740` and `M50753` now feed a headless PowerBook 140 board:
`Pb030Pmu`, `Pb030Memory` and `Pb030Cpu`. It reaches the System 7.5.5 Finder
with keyboard, trackball and raw state replay. There is no GUI composer or
public session-file identity yet; the catalogue remains at 47 profiles.
Catalogue admission still requires GUI and session-file integration, as
required by `DEV.md` § Adding a platform.

The first consumer is the private 6,144-byte Power Manager firmware used by
the PowerBook 140/145/170 and 160/165/180 families. It cold-starts from
`$FFFE`, releases the host through port 3, and answers an actual parallel
`readPmgrVers` transaction. The harness drives pins; it does not implement
PMgr commands or patch firmware/RAM.

## Sources and ownership

- Apple ROM source: [PowerMgr.a](https://github.com/elliotnunn/mac-rom/blob/master/OS/PowerMgr/PowerMgr.a),
  `SendByte` and `ReceiveByte`; [PowerPrivEqu.a](https://github.com/elliotnunn/mac-rom/blob/master/Internal/Asm/PowerPrivEqu.a),
  `readPmgrVers = $EA`. These define the host transport and command.
- MAME [macpwrbk030.cpp](https://github.com/mamedev/mame/blob/master/src/mame/apple/macpwrbk030.cpp):
  board pins, 3.932160 MHz PMU oscillator, charger/input/analog values.
- MAME [m740.cpp](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6502/m740.cpp),
  [dm740.lst](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6502/dm740.lst)
  and [om740.lst](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6502/om740.lst):
  opcode layout, transfer flag, stack, vectors and bus access patterns.
- MAME [m5074x.cpp](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6502/m5074x.cpp):
  peripheral register layout, interrupt priority, timer divider and ADC.

`src/M740.*` owns instruction execution; `M740Table.h` is its opcode map.
`src/M50753.*` owns RAM, immutable ROM input, GPIO direction/latches,
interrupt arbitration, three timers and ADC scheduling. `Pb030Pmu` owns clock
conversion, pin wiring and the parallel host bus; `Pb030Memory` wires the VIA,
Misc GLU, discrete RTC, EASC, 5380, SWIM/Sony drive and SCC to the Jaws map.
The existing R65C02 and PG&E interpreters retain their own instruction sets.

## Controller contract

| Resource | Contract |
|---|---|
| CPU clock | oscillator / 4; `run()` accepts CPU cycles, can overshoot by an instruction |
| RAM | `$0000–$00BF`; survives MCU reset; zero on construction |
| ROM | `$E800–$FFFF`, exactly 6,144 bytes; reset vector `$FFFE`; immutable |
| Stack | page zero, 8-bit SP; firmware places it below the I/O window |
| Ports | `$E0–$E5`, `$E8–$EB`; alternating data/DDR; port 4 has four bits |
| Input/ADC | `$EE` / `$EF`; control `$F3`, start `$F2`; completion after 72 µs |
| Timers | `$F9–$FD`; base divider `4 × (prescaler + 2)` CPU cycles |
| IRQ controls | `$FE/$FF`; INT1, X, ADC/T1, T2, INT2 in priority order |
| Vectors | `$FFFA`, `$FFF8`, `$FFF6`, `$FFF4`, `$FFF2`; BRK defaults to `$FFFC` |
| Snapshot | registers, RAM, pins, timers including phase, ADC deadline and IRQ/wait state |

Output callbacks report DDR-masked levels plus injected board pullups.
Loading a snapshot republishes restored output levels through the receiving
instance's callbacks. ROM, pullups, oscillator and callback bindings are board
configuration, not payload; the owner must restore on the same configuration.
Neither component reads process environment or host time.

The parallel transport has different transmit and receive ordering:

| Direction | Sequence |
|---|---|
| Host sends | data stable → REQ low → wait ACK low → REQ high → wait ACK high |
| Host receives | wait ACK low → sample offered data → REQ low → wait ACK high → REQ high |

The old interface sends a byte count even for a zero-data request. `$EA, $00`
therefore returns the command echo, reply count `$02`, and two version bytes.
Asserting REQ before a reply is offered can instead start another host-send
handshake. The MCU must run while the host waits for the line.

## Executable evidence

```bash
cmake --build build --parallel 4 --target m740_test m50753_test m50753_firmware_etalon
ctest --test-dir build -R '^(m740_test|m50753_test|m50753_firmware_etalon)$' --output-on-failure
```

- `m740_test`, asset-free: common binary ALU/data semantics against the
  independently Klaus-qualified R65C02, 64 deterministic states per opcode;
  explicit Mitsubishi stack/vector, transfer, bit, branch, indirect call,
  decimal flag, wait and dummy-access tests. Timing is checked separately
  because Mitsubishi indexing, stores and taken branches have different costs.
- `m50753_test`, asset-free: GPIO directions/pullups, register holes, ROM
  immutability, reset retention, interrupt priority/acknowledgement, synthetic
  firmware ISR, timer reload/halt, ADC completion and deterministic snapshot
  replay with timer/ADC activity in flight.
- `m50753_firmware_etalon`, private-asset required: all three inputs below
  actually execute; absent input produces an explicit soft skip. Digest
  mismatch fails. No ROM or firmware bytes are committed.

| Input below `roms/archive/macroms/pmu/` | SHA-256 | Version |
|---|---|---|
| `Powerbook-140-145-170__PMU.bin` | `7b91c8a2591b2644bc14c35cd00656ba722c356c79717ed0ef176e5e89ccbcc6` | `02B3` |
| `Powerbook-160-165-180__Powerbook 160 PMU.bin` | `125a8680ab3faa8cc2fed666463ea7741045d8a4b893d8d7b08f4f369790e465` | `03A2` |
| `Powerbook-165c-180c__PMU.bin` | same digest as the grayscale 160–180 dump | `03A2` |

The last two archive names contain identical firmware, so they do not prove
two independent MCU implementations. Each test cold-starts with zero RAM,
observes port 3 releasing HALT and RESET, then freezes an offered reply with
ACK low. Finishing the reply, restoring, and finishing again must produce
the expected version and byte-identical controller state.

## Remaining work and limits

Execution and peripheral scheduling are at instruction boundaries, not a
microcycle scheduler. IRQ entry does not model the delayed CLI/PLP sampling
pipeline. Timer X implements free-running/halt only; counter-pin modes,
serial I/O and PWM waveforms remain unimplemented (PWM control is a latch).
ADC request enable follows the existing MAME bit-2 behavior; readout uses the
selected analog callback at read time. These paths need additional hardware
evidence before broader fidelity claims.

The new CPU's transfer flag follows the Mitsubishi memory-destination
semantics; the firmware transaction proves only the paths it executes. The
common-opcode differential does not qualify decimal, transfer or interrupt
timing by comparison with a 65C02. Dedicated tests cover named cases; this is
not an exhaustive Mitsubishi hardware-oracle qualification.

## Headless Jaws board

The first board consumer is the PowerBook 140, at **15.6672 MHz**
(`31.3344 MHz / 2`). Its VIA clock is **1.56672 MHz**, as configured by MAME
(`31.3344 MHz / 20`); the generic 783.36 kHz comment in MAME's `via_sync`
does not describe this board's actual configured clock.

| Window | Board behavior |
|---|---|
| `$00000000` | 8 MB expanded RAM; first 1 MB hidden by boot ROM until a ROM-window read |
| `$40000000–$4FFFFFFF` | 1 MB ROM mirrors; a real read drops the overlay |
| `$50000000–$51FFFFFF` | I/O mirrors with bits 20–24 ignored, including the bit-24 Jaws detection mirror |
| `+$00000` | VIA1, `$200` register stride, byte replicated on word reads |
| `+$02000` | Misc GLU pseudo-VIA, VIA-style ports and IFR/IER only |
| `+$04000`, `+$06000`, `+$10000`, `+$12060` | SCC, SCSI DMA, 5380 registers, DMA mirror |
| `+$14000`, `+$16000` | discrete EASC `$B0`, SWIM1 with VIA1 PA5 head selection |
| `+$24000–+$27FFF` | bus-error decoder probe; must not expose an MSC/RBV block |
| `+$80000–+$BFFFF` | Jaws speed/power registers; power-cycle request defers host CPU reset |
| `$FEE00000–$FEE0FFFF` | 32 KB DDC VRAM, mirrored at +32 KB; 640×400, 1 bpp |

PMU execution and the bit-serial `AdbLine` use separate fractional clock
bridges. MCU instruction overshoot is carried as debt between host ticks;
the ADB wire retains its established C15M timing domain. CPU hold burns
machine time so the PMU can release the CPU. Warm host reset rearms the
ROM overlay without resetting the independently powered PMU.

The real ROM first booted an error-type-10 dialog: its CPU-space FPU
response-register read at `$22000`, FC 7, saw RAM on the bare 68030.
`Pb030Cpu` now raises bus error for that empty socket, like `Cpu030` on the
LC II. The ROM and System then select BoxFlag 19 (PowerBook 140) themselves;
no guest flag or ROM patch is used. Finder is checked both visually and
through the guest's `CurApName`, alongside SCSI traffic and desktop pixels.

Additional gates:

- `pb030_board_test` is asset-free: Jaws mirrors/overlay, DDC alias and
  pixels, VIA head select, decoder bus errors, Misc GLU interrupt semantics,
  request readback, PMU clock partition invariance, reset edges and CPU hold.
- `pb140_boot_etalon` and `interp_pb140_boot_etalon` execute the real
  `420DBFF3` ROM and `02B3` firmware on the System 7.5.5 reference disk.
  `Infinite HD` is the existing private temporary companion for its startup
  alias. Keyboard press/release reaches KeyMap, trackball movement reaches
  the guest pointer, and Ticks resumes after restoring RAM/CPU/devices.
  The raw visitor restores CPU/machine clocks, Ticks and pixels exactly.
  The settled desktop pin is `e1f182417db75b70` under both engines.

The raw state proof uses the same configured in-process board; it is not a
public save-state container and does not claim profile/ROM mismatch refusal.
The Jaws wrapper conservatively flushes JIT blocks on CACR invalidation;
there is no store-inventory admission yet. GUI/thread composition, firmware
selection/provenance, battery persistence, public session identity, disk-agent
integration and PowerBook 170 qualification remain before catalogue admission.
Portable/PB100 remain a later board integration.

## MAME-derived source notice

The opcode layout and peripheral reference adaptation retain the following
BSD-3-Clause notice (MAME contributors Olivier Galibert and R. Belmont):

Copyright (c) Olivier Galibert, R. Belmont. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
