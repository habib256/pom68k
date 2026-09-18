# `q605_afp_live_etalon` between hosts: there is no host difference

`TODO` § Services réseau asked for a date-pinned x86-64 reference, and then
for the interp/A64 gap to be located "before any attribution to the host".
Both are here, and the second answer removes the first question: **nothing
is attributable to the host.**

| File | What it is |
|---|---|
| `afp_live_trace_x86_64.txt` | this host, POM68K defaults (x64 generator for the 68LC040) — the date-pinned reference asked for |
| `afp_live_trace_x86_64_interp.txt` | this host, `POM68K_CPU_ENGINE=interp` — the accuracy oracle |
| `diff_a64_vs_oracle.txt` | the AArch64 default (a64) of 2026-09-13 against this host's default |

## Four arms, three of them identical

All 22 boundaries, clock **and** architectural fingerprint:

```
x86-64 interp  ==  x86-64 x64  ==  AArch64 interp (2026-09-13)
```

and against those three, the AArch64 **a64** default of 2026-09-13 diverges
from **boundary 14** — `afp_live_3_servers.ppm`, the server list after the
reconnection — onwards. At 14 it is 8 machine cycles late (5 867 689 809 vs
5 867 689 801) with every network counter still equal; by the end of the
run it has sent and received different totals: `afp` 210 vs 209, frames
678/507 vs 676/506, `atp` 229 vs 228.

Two engines on two hosts, one of them the interpreter on each, agree to the
cycle. So the divergence is not the host's: it is the **a64 backend's**, and
it is the accuracy oracle it steps away from — `CLAUDE.md`'s own rule, "an
accelerated path must match architectural state and timing at the boundary
its gate claims".

## What this does NOT say

The gate **passes in every arm**. The divergence is invisible to its
assertions and only the trace shows it, which is what the trace is for.
Nothing here says the a64 arm is wrong about AppleShare — it says it is out
of step with the oracle from a nameable boundary, on a reproducible route.

The comparison crosses trees (`07b00cc` for the AArch64 pair, `e2a9498` +
this session's working tree here); the interpreter traces being identical
across that span is itself evidence that the five days between them moved
nothing this gate can see.

## Reproducing

```
ctest --test-dir build -R '^q605_afp_live_etalon$' -V            # 2 min 24 s
POM68K_CPU_ENGINE=interp ctest --test-dir build -R '^q605_afp_live_etalon$' -V   # 8 min 4 s
python3 tools/afp_trace_diff.py <reference> <other>
```

Next, and it needs an AArch64 host: bisect the a64 arm inside the
reconnection phase — boundaries 12 and 13 are still identical, so the step
that separates them is between the Chooser reopening and the server list
being painted a second time.
