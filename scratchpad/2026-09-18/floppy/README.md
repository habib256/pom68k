# `lcii_floppy_etalon` on x86-64: what the red actually was

The 2026-09-12 note (`scratchpad/2026-09-12/floppy/`) left one instruction:
"instrument the IWM/SWIM1 read path on both machines from the first read
that differs". Done on this host, the instrument answers something else —
the read path refuses nothing at all.

| File | What it is |
|---|---|
| `run_failing_head.log` | the leg at `e2a9498`, before the fix — FAILED |
| `sony_trace_failing.log` | `lcii_sony_trace --frames 1800` on that same state: every .Sony call, with its `ioResult` |
| `run_settle3600.log` | the same, plus 60 further emulated seconds before the gesture — still FAILED |
| `boost_1_2_3_identical.txt` | `POM68K_CACHE_BOOST` 1, 2, 3 — identical counters to the digit |
| `cmdn_folder_in_games_window.png` | the failing Cmd-N: "untitled folder" in MacPack's **Games** window, 15 items |
| `cmdn_folder_in_system_tools_window.png` | the same gesture after the fix: the folder is in **System Tools**, 7 items |
| `run_passing.log` | the leg with the window opened by gesture — PASSED |

## The driver refuses nothing

`sony_trace_failing.log`, on the run that fails: **24 Primes, 2 Controls,
zero failures**, every one `0 noErr`; `_MountVol` returns `noErr`; the
session's last call is Control csCode 22 (mediaIcon), the Finder asking for
the icon it then paints on the desktop. The sectors read are 2, 4, 16-23,
233-243 — a mount, then the catalog nodes behind the icon.

So the two figures the previous note reasoned from are *effects*: the head
is parked on track 10 because sector 243 was the last one anyone asked for,
and the last 512 nibbles carry no address mark because they are what the
coasting motor produced after it. A finished read, not a failing one.

## The cause is on screen

`cmdn_folder_in_games_window.png`: the Cmd-N created "untitled folder"
inside MacPack's own **Games** window — 14 items before the gesture, 15
after — on the boot volume. The floppy is mounted, its icon is on the
desktop, and its window never opened. The leg had assumed since 2026-08-05
that "mounting opens the volume's window, so it is frontmost".

With the window named by gesture (close all, type-select, Cmd-O) this host
reproduces the committed reference's downstream figures exactly: Cmd-N
repaints 3066 px over x 17..503 (reference: 3066 px), centre white 0.91
(0.91), `'untitled folder' 0 -> 2`. Everything after the gesture was
already identical between the two hosts.

## Measured, not argued

- run to run on this host: byte-identical logs.
- `POM68K_CACHE_BOOST` 1 / 2 / 3: identical counters — the floppy boost gate
  freezes the ratio to 1 while the motor runs, so the knob cannot reach this.
- `-march=native` + LTO vs a plain `-O2` build (`build-plain`): byte-identical
  logs.
- `POM68K_FLOPPY_SETTLE=3600`: no window, 60 emulated seconds later.
- **The engine arms of 2026-09-12 were the same engine twice.** This rig
  builds its CPU with `jit::defaultResolvedConfig()`, whose `engine` defaults
  to `EngineKind::Interp`; no environment knob reaches a default-constructed
  config, so `POM68K_CPU_ENGINE` is inert here and every figure ever recorded
  for this gate is the interpreter's.

## What is still open

Why the Finder had that window open on the run committed at `662a64f`,
whose host was never recorded. The first mechanism to look at is the
Finder's habit of reopening the windows a volume had open when it was last
ejected — volume state, not host state. The gate no longer depends on it.
