# `lcii_floppy_etalon` on x86-64: the evidence

The gate fails on this host. A log committed at `662a64f` records it passing.
Rebuilding that same commit here and running it reproduces the failure, so the
comparison below is the same gate, the same code and the same assets on two
machines — ROM `35C28F5F` / `18c3de07…`, `hdv/boot.vhd` sha256 `cc364381…`,
`disks35/Disk605.dsk` sha256 `6ea0c1c7…`, untouched since 14 August.

The host that produced the passing log is **not recorded**: neither `662a64f`
nor the log itself names one. Earlier revisions of this note attributed it to
the M4; that was an inference, not a fact, and it has been removed.

| File | What it is |
|---|---|
| `reference_662a64f.log` | the passing run committed at `662a64f`, host unrecorded (was `scratchpad/2026-09-07/floppy/final_lcii_800k.log`) |
| `run_662a64f_this_host.log` | the same commit rebuilt and run **here** — the like-for-like comparison |
| `run_b7700f1.log` | this host at `b7700f1`, two commits later |
| `run_x86_64_x64_and_interp.log` | this host, current tree, x64 default and interpreter — both fail identically |
| `diff_vs_reference.txt` | the original confounded diff, kept for the record (see below) |
| `memcheck_partial.log` | `valgrind --tool=memcheck`, stopped in the boot phase |

## The like-for-like comparison, at `662a64f`

| | committed log | this host |
|---|---|---|
| nibbles read off the medium | 586 503 | 309 598 |
| head left at | track 0, TKO=0 | track 10, TKO=1 |
| last 512 nibbles consumed | 330 sync `$FF`, 3 marks | 5 sync `$FF`, **0 marks** |
| repaint after insert (fraction of pixels) | 0.123 | 0.012 |
| Put Away ejected after | 180 guest frames | 60 guest frames |
| `untitled folder` in the host file | 0 → 2 | never appears |
| verdict | PASSED | FAILED |

The volume mounts on both. What differs begins at the insert: half the nibbles,
the head parked on track 10, and no GCR address mark in the last 512 nibbles
consumed. The repaint figures are fractions of changed pixels, not delays — here
a 64×95 patch in the top-right corner, the disk **icon**, where the reference
repaints an eighth of the screen across nearly its full width, a volume
**window**. The Cmd-N that follows therefore lands elsewhere, and although the
guest commits sectors, no folder name reaches the host file.

## What made this red visible, on 2026-09-07

Bisecting the 49 commits between `697a572` and `fc7d472` puts the first red at
`f557e88`, whose message says the gate "had printed the Cmd-N folder without
asserting it since 2026-08-05". It added `&& guestEjected && grewF <
folderprobe::kCount` to the verdict, turning a question `TODO` had kept open
into an assertion. No emulator code changed there. This host's guest had never
written that folder to the host file; the gate merely stopped tolerating it,
which is why the two all-green registry runs of 2026-09-01 included this gate
and passed it.

Of the two added conjuncts only one fails here: the guest does Put Away the
volume and the drive empties, in 60 frames. The catalog write is what never
happens.

`diff_vs_reference.txt` is the first diff taken during this investigation. It
compared the reference log against a `b7700f1` build — thirty-six commits and two
floppy-path changes apart — so it varied host and code together and proved
nothing on its own. It is kept because its per-line figures are still accurate
for the two runs it names.

## Ruled out

- **A host clock.** Nothing in the floppy path reads one; `std::chrono` appears
  only in GUI and host files. Every quantity in the gate is guest-derived:
  `runFrames` advances emulated frames, `diffRatio` is a pixel fraction, and the
  eject count is a measured, capped response (`ejectFrames < 1800 &&
  drv.hasDisk()`), never a budget a fast host could expire.
- **Floating point.** There is no `float` or `double` anywhere in the read path;
  the PLL is `int64_t`.
- **Image drift between runs.** `Disk605.dsk` is unchanged since 14 August; the
  gate writes to a private copy.
- **The `senseAddr()` change** in that span, bit 3 moving from the ISM mode
  register to the drive's SEL line. It matches the symptom well — the sense
  address selects TK0 at `0x5` — but the failure predates it.
- **A memory error**, weakly: memcheck reported zero invalid accesses in the
  boot phase it reached, though it never got to the insert. It was stopped at 64×
  the native runtime because the x64 JIT backend makes Valgrind re-translate
  continuously; a rerun should pin `POM68K_CPU_ENGINE=interp`, which fails too.

## Next

Instrument the IWM/SWIM1 read path on both machines from the first read that
differs, which is at the insert and well before the folder the gate asserts.
Reproducer here: `POM68K_BEYOND=floppy build/lcii_beyond_etalon`, 65 s.
