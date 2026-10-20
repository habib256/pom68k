# Macintosh Portable / PowerBook 100 — board and product integration

`PortableMemory`/`PortableCpu` compose the 68000 board and `PortablePmu`
shared by both machines. Both real ROMs boot System 7.1 to Finder with
their own BoxFlag (Portable 4, PB100 18). Both are catalogue profiles:
`portable` (session id 57) and `pb100` (58), with GUI composition, required
PMU firmware, battery persistence and distinct public session identities.

## Product lifecycle and persistence

The two 256 KB ROM signatures route directly through `MachineFactory`.
`PlatformPortable.cpp` composes the board and a mono ASC audio host using
the existing parallel-PMU GUI lifecycle in `GuiRunnerPb030.h`. The display,
keyboard/trackball, floppy, SCSI/CD bays, debugger devices, network, engine
menu and save/load slot use the shared application services. The default
startup image is System 7.1. Original fixture media remain immutable; GUI
write-back uses the normal work-copy policy.

Firmware is required at `roms/pmu/portable.bin`, beneath the configured
firmware root, or at the archive paths below. An explicit
`--firmware-override=pmu:lle:<path>` is authoritative: missing or malformed
input refuses startup without silently choosing another dump. HLE is refused.
The peripheral registry reports the actual selected firmware path.

The `.portable.pram` and `.pb100.pram` files persist all 192 bytes of PMU
battery RAM, including PRAM and RTC. The 216-byte record carries magic,
main-ROM checksum, firmware identity and an integrity checksum. All checks
precede RAM mutation; saves use atomic replacement. A Portable record cannot
silently initialize PB100, even with their shared firmware.

The GUI supplies host Mac seconds once when the PMU releases the initial
host reset, after firmware initialization. This pending seed is snapshot
state; restoration does not consult host time or fabricate a reset edge.
Firmware thereafter owns the clock. Other guest/device timing stays in
machine cycles.

Public `.pomss` containers carry profile 57 or 58, main-ROM identity, RAM
size and required-firmware identity. Mismatch or a corrupt chunk refuses
transactionally, rolling back RAM and devices. `.pomsession` configuration
files retain the profile, ROM and firmware override and refuse the other
profile's ROM before constructing a running machine.

`portable_product_test` exercises firmware refusals, battery integrity and
cross-model/firmware checks, clock seeding, public-container refusal and
rollback without private inputs. The real boot gates replay public containers
under both engines. The `portable_agent_boot_etalon` and
`pb100_agent_boot_etalon` variants require startup-agent polling and a disk
mounted live from its mailbox.

On x86-64, 2026-10-19, both GUI smoke runs passed under Xvfb: opening,
rendering, engine switch, completed state save, close and relaunch request.
The reports attested the following results before temporary-file cleanup:

| Profile | Rendered frames | Engine switched | Save completed | Window closed | Relaunch requested | Result |
|---|---|---|---|---|---|---|
| Portable | 136 | yes | yes | yes | yes | PASS |
| PB100 | 117 | yes | yes | yes | yes | PASS |

Both used temporary disk clones; the headless gates provide the full
Finder/input/restore proof.

## Headless 68000 board

The 68000 runs at 15.6672 MHz without cache boost or the compacts' shared
video/RAM contention. Its dedicated 32 KB VRAM renders a 640×400 monochrome
LCD, blanked by the PMU. The VIA runs at CPU/20; its accesses cost 50 host
cycles and SWIM accesses five, following MAME's board implementation.
The 60.15 Hz clock supplies PMU INT1 and a VIA CA1 pulse; the PMU supplies
VIA CA2 seconds and CB1 interrupts. SCSI IRQ/DRQ reach CB2/CA1. VIA IRQ
is IPL1; SCC and discrete ASC share IPL2.

| Physical window (24-bit bus) | Resource |
|---|---|
| `$000000–$03FFFF` at reset | 256 KB ROM overlay, replacing low RAM |
| Low memory after overlay removal | Installed RAM, 8 MB in the boot gates |
| `$900000–$9FFFFF` | 256 KB ROM with mirrors; a live read removes overlay |
| `$F60000–$F6FFFF` | SWIM1, register stride 512 bytes |
| `$F70000–$F7FFFF` | VIA, register stride 512 bytes; replicated word reads |
| `$F90000–$F9FFFF` | NCR 5380; upper read byte, lower write byte, pseudo-DMA aliases |
| `$FA8000–$FAFFFF` | 32 KB VRAM |
| `$FB0000–$FBFFFF` | Discrete ASC, version `$00` |
| `$FC0000–$FCFFFF` | PDS RAM expansion strap (zero at 6 MB or more) |
| `$FD0000–$FDFFFF` | SCC85C30 |

The CPU does not fetch reset vectors while the PMU holds SYS_RST low.
That premature fetch would remove the overlay and make the eventual reset
read vectors from RAM. Reset assertion rearms the overlay; release defers
the real CPU reset to an instruction boundary. The MCU remains independently
powered. Bus peeks have no peripheral or overlay side effects, and JIT
memory windows exclude I/O.

The headless gates use the immutable System 7.1 reference disk plus the
existing blank Infinite HD companion for its Startup Items alias. The stock
System 7.5.5 image reached an Apple Photo Access installation alert before
Finder; it is not the qualified boot fixture for this board. No system file
or ROM was modified to dismiss that alert.

`portable_board_test` checks the memory/VRAM map, PDS strap, VIA/PMU pins,
SCSI byte lanes and pseudo-DMA decode, CPU reset/hold, bus wait and exact
raw state replay without private assets. `portable_boot_etalon` and
`pb100_boot_etalon` check real ROM identity, guest Finder, desktop pixels,
keyboard down/up in KeyMap, mouse movement and an advancing guest clock.
Their public CPU/RAM/device containers must resume byte-identically after restore.
`jit_` variants explicitly use the threaded engine; the 68000 product
default remains the interpreter.

On x86-64, 2026-10-19, both engines executed both boot gates successfully.
The settled desktop pins on System 7.1 are `cc4a869f52d960ad` (Portable)
and `f3c9095a373a7f28` (PB100). The targeted regression ran 13 gates with
zero failures or soft skips, including the earlier PMU/controller gates.

```bash
cmake --build build --parallel 4 --target portable_board_test portable_boot_etalon
ctest --test-dir build -R '^(portable_board_test|(jit_)?(portable|pb100)_boot_etalon)$' --output-on-failure
```

Private ROM SHA-256 identities:

- Portable `96CA3846`: `3de29198fc61859ed6146dae0b84cec6c980045f4e3efd64a683aab4db8e04a4`.
- PB100 `96645F9C`: `62558d5faae58e1d6642a8b046f5ec432fe99073e018ac46597ec024b773b6a8`.

## Board contract

The MCU uses the existing `M740`/`M50753` core at oscillator 3.932160 MHz
divided by four. Host clock conversion carries instruction overshoot between
quanta. The ADB wire keeps its independent 15.6672 MHz timing domain.

| Connection | Portable/PB100 wiring |
|---|---|
| Input port | `$20`: normal boot, not target disk mode |
| Port 1 | `$08` charger input, bit 1 any-key-down |
| Port 2 | Bidirectional host data; input-direction output pins pulled high |
| Port 3 input | Bit 7 host PMREQ |
| Port 3 output | Bit 1 SYS_RST, bit 4 VIA CA2 (seconds), bit 5 VIA CB1 (PMINT), bit 6 PMACK |
| Port 4 | ADB out/in and LCD blanking; four external pullups |
| Analog inputs | `$FF` on each channel, matching the MAME board model |
| INT1 | Supplied by the board's 60.15 Hz clock |

Unlike Jaws, SYS_RST bit 1 alone controls the host reset. The VIA callbacks
publish physical levels, including active-low PMINT, so the future VIA
owner can perform its own edge detection. Reset callbacks report pin changes.
Snapshot restoration republishes VIA levels without creating a CPU reset.
Firmware identity and host frequency are checked before loading device state.
The snapshot includes MCU timers/RAM, the ADB transaction and host clock phases.

## Executable evidence

```bash
cmake --build build --parallel 4 --target portable_pmu_test portable_pmu_firmware_etalon
ctest --test-dir build -R '^portable_pmu' --output-on-failure
```

`portable_pmu_test` requires no assets. It checks pin routing, input events,
LCD/ADB levels, clock partition invariance, reset retention, fresh-instance
snapshot continuation and refusal of a different firmware or host frequency.

`portable_pmu_firmware_etalon` opens both private archive names:

- `roms/archive/macroms/pmu/Macintosh-Portable__PMU.bin`
- `roms/archive/macroms/pmu/Powerbook-100__PMU.bin`

Both are 6,144-byte copies of the same firmware, SHA-256
`0f9911ab33a62a2831d683d9648059bd7ad59c455b59dcd214c610ce16eebd57`.
The two names do not represent two independent implementations. Missing
inputs cause an explicit soft skip; a digest mismatch fails.

The gate cold-starts with zero RAM. The firmware initially polls two INT1
events for calibration with interrupts disabled; advancing the MCU without
the board clock leaves it waiting at `$E8FB`. Supplying frame interrupts
releases SYS_RST without a key event, RAM seed or firmware patch. The old
PMgr protocol returns `$02B5` for `$EA` (MAME's filename `pmuv1.bin` is not
the returned version). The first reply is snapshotted while ACK is low;
a fresh controller must finish with identical serialized state. Commands
`$30`/`$38` then write/read a known RTC value through the same physical bus.
Both archive inputs executed successfully on x86-64 on 2026-10-19.

## Sleep and warm resume

With the System 7.1 reference volume, choose **Special > Sleep** in the
guest Finder, confirm its AppleTalk warning, then press a key to wake.
The real System saves its state and sends the PMU request. Firmware asserts
SYS_RST and blanks the LCD; the independently powered PMU continues running.
A key releases reset, and the ROM resumes the interrupted Finder through
the System's wake vector. The board's existing reset-edge wiring supports
this path without a new command handler or guest patch.

`portable_sleep_etalon` and `pb100_sleep_etalon`, plus their `jit_`
threaded variants, operate the Finder through ADB. They dismiss the reference
volume's unsupported-QuickTime alert, select Sleep and confirm the network
warning. The checks require an idle host and disk for ten seconds, exact
public-state continuation while asleep, exact wake replay from that state,
and live keyboard/trackball input after returning to Finder. These are
guest sleep/resume checks, not measurements of power consumption or a
qualification of lid switches and other wake sources.

## Remaining limits and sources

The gates qualify the board, GUI composition, public snapshots and
Finder-initiated sleep/key wake. Other power policy and power consumption
remain unqualified. The controller reads
no host time; product composition supplies a one-time clock seed rather than
MAME's port-0 host-time injection. Its RAM survives controller reset, and
firmware commands own the RTC. Unimplemented MCU peripheral modes retain the limitations in
[PB030_BRINGUP.md](PB030_BRINGUP.md#remaining-work-and-limits).

- MAME [macprtb.cpp](https://github.com/mamedev/mame/blob/master/src/mame/apple/macprtb.cpp):
  `pmu_*` handlers, GPIO pullups and the periodic board clock.
- Apple [PowerMgr.a](https://github.com/elliotnunn/mac-rom/blob/master/OS/PowerMgr/PowerMgr.a):
  old parallel `SendByte`/`ReceiveByte` ordering.
- Apple [PowerPrivEqu.a](https://github.com/elliotnunn/mac-rom/blob/master/Internal/Asm/PowerPrivEqu.a):
  `readPmgrVers`, `timeWrite`, `timeRead`.

## MAME-derived source notice

Copyright (c) R. Belmont. All rights reserved.

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
