# CHANGELOG

## 2026-07-15 — M6: the startup chime plays

- Sound: `MacAudio` pulls the 370-sample/frame PWM buffer (ramTop−$300,
  even byte = 8-bit sample, PA3 buffer-select, PB7 enable, PA2-0 volume);
  `MacAudioHost` plays it through miniaudio on a lock-free ring at
  22254 Hz. The turbo-vs-audio pitch problem is solved by pushing only
  non-silent frames — the ring drains while the machine races through the
  silent RAM test, so the chime and beeps keep the right pitch.
- The Mac Plus power-on chime — a ~601 Hz (≈D5) tone held ~0.7 s then
  muted — is audible. Verified headless by capturing it to a WAV and
  checking the fundamental + decay (`sound_test`).

## 2026-07-15 — M7: System 6 boots from a SCSI hard disk

- NCR 5380 controller + SCSI-1 target boot System 6 from HD20SC.vhd; the
  HD20SC volume mounts on the Finder desktop.
- The day-long blocker was NOT the controller (proven correct in isolation
  by ncr5380_test from the start) but the ROM never running its SCSI scan.
  Diagnosis chain: PRAM (ruled out — Plus ignores the default-boot-device,
  that's a 256K-ROM feature) → floppy presence (ruled out) → drive queue
  had only the floppy → SCSI Manager select/read primitives never executed
  → the gate is `HWCfgFlags` ($0B22) bit 7, set by `E_SoftReset`'s
  $420000-vs-$440000 ROM-mirror probe. We mirrored the ROM across the whole
  window so the probe saw no difference and declared "no SCSI". Fixed by
  returning address-dependent open bus above the true 128 KB ROM. (Nailed
  via the bit-exact Plus v3 ROM disassembly, jonathanschilling/mac_rom.)
- Second blocker: WRITE(6/10) is mandatory — the driver writes to the
  volume during mount; a read-only target hung the boot in a VIA interrupt
  storm right after the driver loaded. Added a DATA OUT phase to the
  controller and in-memory writes to the target.
- Also: GUI windows move only from the title bar (Finder drag-and-drop no
  longer drags the host window); floppy/SCSI paths resolve relative to the
  executable; SCSI disk auto-attaches from hdv/.

## 2026-07-14 — M5.5: the Finder is drivable (keyboard + mouse)

- Minimal SCC Z8530 (DCD ext/status interrupts, RR2B modified vector —
  the ROM's actual dispatch mechanism; it never reads RR3), quadrature
  mouse with the exact polarity table, M0110A keyboard with the
  two-SR-interrupt transaction (~3 ms per phase).
- The vicious one: with keyboard AND mouse alive, a naive IPL OR yields
  level 3, whose ROM vector is a bare RTE → instant livelock. The real
  glue disconnects the VIA /IPL0 while the SCC interrupts (GttMFH); the
  suppression formula fixed it in one line. Diagnosed by single-stepping
  at storm onset: the "handler" was just `rte` + the interrupted
  instruction alternating.
- Gate `input_etalon` verifies against System 6's own understanding:
  RawMouse deltas (±2 for inherent quadrature reversal loss), MBState,
  KeyMap bits. Bonus verification: headless click on the File menu —
  it drops with all items rendered.
- GUI: mouse captured over the Mac screen (2x scale compensated),
  55-key M0110 map, Cmd = Super, Option = Alt.

## 2026-07-14 — M5: System 6.0.5 boots to the Finder from floppy

- IWM + Sony 800K drive from the cross-verified research spec (MAME, pce,
  Snow). GCR checksum ported verbatim from MAME and cross-validated
  against pce's independent formulation before use.
- Three bugs found by tracing the ROM's Sony driver instruction by
  instruction (`sony_trace`, new dev tool):
  1. TACH must run on motor time, not data position — the ROM times
     spindle speed against VIA T2 before ever reading data.
  2. The IWM data register clears ~14 clocks AFTER a read, not
     immediately — the ROM's `tst.b`/`move.b` pairs read it twice.
  3. Boot blocks need the bbVersion word ($4418) at +6 and a BRA at
     bbEntry (+2); 'LK' alone is not enough.
- Verification chain, cheapest first: gcr_test (encoder roundtrip vs an
  independently-ported decoder) → disk_boot_etalon (synthetic boot block
  executes our 68000 code through the whole floppy path, no Apple bits
  needed) → system_boot_etalon (real System 6.0.5 to the Finder desktop,
  2.7 s headless).
- GUI probes disks35/ for a floppy image; boot_trace grew --disk.

## 2026-07-14 — M4.5: SingleStepTests/680x0 — 1 000 058 / 1 000 060

- `sst68000` harness (POM2/POMIIGS JSON-scanner pattern) runs the full
  1M-vector corpus in 3.7 s; `--dump` prints complete state diffs (the
  workhorse of the convergence loop); `--only/--skip/--max` for triage.
- Starting point was 81.1% (all non-address-error vectors already passed —
  Moira's core semantics were sound). The 19% gap was almost entirely the
  fine detail of 68000 address-error behavior: exception idle cycles,
  stacked-PC values per instruction class, An update ordering around
  faults, frame FC/IN bits, and MOVE's interim flags. Full patch catalogue
  in `extern/moira/POM68K_VENDOR.md § convergence`.
- Notable finds: branches/jumps/returns all stack PC = target−4 with the
  I/N bit set; BSR pushes its return address before faulting but JSR does
  not; ADDX/SUBX leave a faulting -(An).l at init−2 while plain reads keep
  the full −4; DIV/CHK undefined flags follow the preserve rules verified
  empirically by a research agent against the whole corpus (0 mismatches).
- Two ASL.b vectors are upstream-documented bad data (SST issue #4 — our
  computed values match the proposed corrections); skipped with citation.
- Two SST rules conflict with hardware literature (DIVS late-overflow
  timing vs ijor's paper; ASR over-shift C/X) — applied per the
  oracle-wins rule and flagged for oracle #2 in the LC II phase.
- Methodology note: this was the phase-2 oracle loop run manually — dump
  divergence, derive the rule from the corpus (python analysis over the
  JSON), patch, re-run. The 68030 phase will automate exactly this.

## 2026-07-14 — M4 complete: cycle-accurate boot hardware

- VIA timers T1/T2 count at φ2 = CPU/10, driven from `Cpu68k::sync()` via a
  peripheral catch-up (`lastPeriphClock_`) so contention wait states tick
  them too. One-shot/free-run semantics; ±1-cycle 6522 reload latency
  deferred (TODO M4.1).
- RTC 343-0042 bit-banged serial protocol (PB2 /enable, PB1 clock, PB0
  data), 20-byte PRAM, write-protect register, seconds counter on the CA2
  one-second tick. **Fixed the "2 MB" mystery**: with a working RTC the ROM
  now stores MemTop = $400000 — the earlier $1FFBE0 SSP was a mid-boot
  stack, not a sizing result.
- RAM/video contention (`Cpu68k::contentionDelay`): video owns alternate
  4-cycle slots during the 512 visible dots of lines 0-341, sound/PWM fetch
  steals the last 4 cycles of every line. The delay iterates across
  adjacent busy slots (a wait can land in the next line's video slot — the
  first version under-counted by 4 cycles × 341 lines). Gate reproduces
  GttMFH Table 5-3: 2.56 MB/s average RAM bandwidth, 21 246 accesses/frame.
- cpu_smoke hardened: with contention the 1M-cycle run stops mid-repaint,
  so the diagonal invariant is checked on 4 row pairs (3 must hold) instead
  of assuming a completed frame.
- `MacFrame.h`: shared frame clock — VBL (CA1) at line 342 = cycle 120 384,
  one-second every 60 frames; GUI, boot_trace, etalon all agree on phase.
- New gates: `contention_test` (budget math), `rom_boot_etalon` (real ROM →
  gray desktop 50% ± icon patch white ratio; soft-skips without a ROM).
- GUI: Turbo ×8 checkbox (default on with a real ROM — the 4 MB RAM test
  takes 45 s of machine time).

## 2026-07-14 — M0–M3.5 + first real-ROM boot

- Project scaffolded on the POMIIGS blueprint; Moira vendored from NeoST
  (`extern/moira/POM68K_VENDOR.md`).
- Built-in demo ROM gate: caught that the 6522 port A reads inputs with
  pull-ups (`ora | ~ddra`) — code must set DDRA before ORA to clear the
  overlay, exactly like the real ROM (DDRA=$7F, then ORA with PA4=0).
- Web-research report (MAME/pce/Mini vMac/GttMFH cross-checked) pinned in
  `DEV.md`; fixed SCC read/write split ($8/$9 even vs $A/$B odd), IWM range
  $C-$D with stub reads of `$1F` (required to reach the blinking-?), VIA PA6
  screen-buffer select.
- Level-sensitive IPL: recomputed after **every** VIA access (reads clear
  IFR flags too) — without this the first serviced VBL re-interrupts forever.
- **Mac Plus ROM v3 boots to the blinking-? floppy icon**: RAM test runs
  ~45 s of machine time on 4 MB (real hardware does the same — don't
  mistake the `movem.l`/`eor` loop at `$400E82` for a hang), then gray
  desktop + mouse pointer + ?-icon; VBL IRQ drives the blink counter wait
  at `$402420`. No VIA timers or RTC needed to get here, confirming the
  BMOW Plus Too minimal-hardware list.
