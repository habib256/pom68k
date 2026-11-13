# Q630 rare divergence — snapshot/replay harness (2026-11-13)

What found the x18 defect (`CHANGELOG.md` 2026-11-13 (third)). Not a gate:
the paths inside the scripts are those of the session that wrote them.

- `harness_main.inc` — a replacement `main()` for
  `tests/q630_ide_boot_etalon.cpp`. `snap <dir>` runs phase 1 and saves the
  machine state and an APFS clone of the IDE image at each of the 65 script
  steps; `replay <dir> <step> <nsteps>` reloads one step in a fresh process
  and runs on, printing clock, PC and a RAM hash every `EVERY` frames
  (default 300). `ENG=interp|threaded` selects the engine, `DENY=from:to`
  the JIT deny range.
- `mkharness.sh <tree> <builddir> <outbin>` — splices that `main()` in,
  builds the gate's target, copies the binary out and restores the file.
- `mass.sh` — N rounds of P parallel replays of one binary, one md5 per
  trace. `ab.sh` — the same with two binaries interleaved in each round.
- `x18.c` — writes x18, spins 20 ms, reads it back. On the M4 under
  macOS 27.2 it reads 0 every time.

A replay of steps 7 to 19 (frames 19 428 to 22 934) takes 10 s under the
JIT and 33 s under the interpreter, against 100 s for phase 1 alone.
