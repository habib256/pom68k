# CHANGELOG

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
