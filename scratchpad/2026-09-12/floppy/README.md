# `lcii_floppy_etalon` on x86-64: the evidence

The gate fails on this host and was reported green on the M4 at the same commit
(`b7700f1`, CHANGELOG 2026-09-09 (third)). The inputs are identical on both
sides — ROM `35C28F5F`, `hdv/boot.vhd` sha256 `cc364381…`, `disks35/Disk605.dsk`
sha256 `6ea0c1c7…` — so this is a host divergence, the second of its kind after
the AFP one (CHANGELOG 2026-09-11 (third)).

| File | What it is |
|---|---|
| `reference_m4_2026-09-07.log` | the M4's passing run, `scratchpad/2026-09-07/floppy/final_lcii_800k.log` |
| `run_b7700f1.log` | this host, same commit `b7700f1`, built in a throwaway worktree |
| `run_x86_64_x64_and_interp.log` | this host, current tree, x64 default and interpreter — both fail identically |
| `diff_vs_reference.txt` | `reference_m4_2026-09-07.log` against `run_b7700f1.log` |
| `memcheck_partial.log` | `valgrind --tool=memcheck --track-origins=yes`, stopped in the boot phase — see below |

## What the diff says

The volume mounts on **both** hosts: each run ends the insert with "volume icon
appeared (MOUNTED)". What differs is everything the guest does to get there.

| | M4 (passes) | x86-64 (fails) |
|---|---|---|
| nibbles read off the medium | 586 503 | 309 598 |
| head left at | track 0, TKO=0 | track 10, TKO=1 |
| last 512 nibbles consumed | 330 sync `$FF`, 3 marks | 5 sync `$FF`, **0 marks** |
| consumed tail | `… FF FF D5 AA 96 …` | no `D5 AA 96` anywhere |
| repaint after insert (fraction of pixels) | 0.123 — 24 163 px, x 3..503 y 25..335 | 0.012 — 2 310 px, x 439..503 y 28..123 |
| repaint on Cmd-N | 3 066 px, y 52..170 | 3 069 px, y 45..316 |
| Put Away ejected after (guest frames) | 180 | 60 |
| folder in the host file | `untitled folder` 0 → 2 | none appeared |

The repaint regions are the tell, and they are pixel fractions rather than
delays — `diffRatio` over two framebuffers. On the M4 the insert repaints an
eighth of the screen across almost its full width: a volume **window** opening.
On x86-64 it repaints a 64×95 patch in the top-right corner — the disk **icon**
on the desktop, and nothing more. The Cmd-N that follows therefore lands
somewhere other than an open floppy window: the hard-disk image is untouched on
both sides, the guest does commit sectors to the floppy on both sides, yet no
candidate folder name reaches the host file here.

The divergence is already present at the insert, before the gesture the gate is
nominally about. The half-sized nibble count with the head parked at track 10
and not one GCR address mark in the last 512 nibbles consumed says the read path
itself is where the two hosts part ways, not the Finder scripting on top of it.
The shorter eject (60 guest frames against 180) fits that: less was read and
less is left to flush.

## The gate does not read the host clock

Worth stating, because the AFP investigation this session ended in gate
calibration rather than an emulator defect, and the same escape had to be
excluded here rather than assumed away. It does not apply: every quantity in
this gate's floppy path is guest-derived. `runFrames` advances emulated frames,
`diffRatio` is a fraction of changed pixels, and the eject count is a *measured*
guest response — `for (; ejectFrames < 1800 && drv.hasDisk(); ejectFrames += 30)`
— capped, never imposed. A faster host cannot cut this guest off early, so the
180-against-60 difference is the guest genuinely doing less work, not a budget
expiring.

## Memcheck reported nothing, and could not finish

`valgrind --tool=memcheck --track-origins=yes` was run against the reproducing
binary and stopped after 93 minutes of wall time and 4 131 s of CPU — 64× the
65 s the same binary takes natively, well past the 30–50× memcheck normally
costs. In everything it did reach, the whole boot phase up to and including
asset loading, it reported **zero** invalid reads, invalid writes or
uninitialised values. That is a weak negative, not an exoneration: it never got
as far as the floppy insert, which is where the divergence lives.

The reason it crawled is in its own stack trace: `pom68kJitSync` calling into an
anonymous region (`0x88D6C57 ???`). This binary runs the x64 JIT backend by
default, and code generated at run time forces Valgrind to re-translate
continuously. A rerun worth the wait must pin the interpreter —
`POM68K_CPU_ENGINE=interp` — which the gate also fails under, so nothing is lost
by doing so. A memory error was never the strong hypothesis anyway: the failure
is deterministic and identical under both engines.

## What is not yet known

Which layer of the read path diverges, and why it is host-dependent at all when
machine time is guest time. Next: re-run the gate on the M4 to confirm it still
passes there, then instrument the IWM/SWIM1 path on both hosts from the first
read that differs. Reproducer here: `POM68K_BEYOND=floppy build/lcii_beyond_etalon`,
65 s.
