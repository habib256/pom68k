# PowerBook 140–180 — M50753 Power Manager bring-up

## Boundary of the first milestone

`M740` and `M50753` are standalone controller components. They are not yet
wired into a Macintosh platform or the GUI. The catalogue remains at 47
profiles. Adding a PowerBook profile requires its own Finder, input, storage,
GUI and session-state proofs, as required by `DEV.md` § Adding a platform.

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
interrupt arbitration, three timers and ADC scheduling. Board clock conversion,
pin wiring and host VIA integration belong to the future PowerBook platform.
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

Next integration: the PowerBook 140/170 memory map, host clock bridge and
VIA/pseudo-VIA parallel handshake; ADB through the PMU pins, RTC/PRAM,
DDC framebuffer and storage; then a real Finder and session/GUI proof.
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
