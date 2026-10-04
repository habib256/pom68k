# `q605_afp_live_etalon` on AArch64 after the a64 late-IPL fix

| File | What it is |
|---|---|
| `afp_live_trace_aarch64_interp.txt` | this host, `POM68K_CPU_ENGINE=interp` — the accuracy oracle, 23 boundaries |
| `afp_live_trace_aarch64_a64.txt` | this host, default (`a64`) after the fix — identical to the oracle at every boundary |
| `afp_live_trace_x86_64_interp.txt` | x86-64 at `25fed3b`, interpreter — **the oracle since drive B was unwired**, 22 boundaries |
| `afp_live_trace_x86_64_x64.txt` | x86-64 at `25fed3b`, default (`x64`) — identical to the line above |
| `afp_live_trace_aarch64_interp_at_3c4b674.txt` | AArch64 at `3c4b674`, interpreter — identical to the x86-64 pair at all 22 boundaries |
| `afp_live_trace_aarch64_a64_at_3c4b674.txt` | AArch64 at `3c4b674`, default (`a64`) — identical too |
| `afp_live_trace_x86_64_x64_at_69cb4b4.txt` | x86-64 at `69cb4b4` (before drive B was unwired), `x64` — identical to the AArch64 interpreter reference at all 23 boundaries |

They supersede `scratchpad/2026-09-18/afp/`: the Q605 SCC sync of 2026-09-27
moved every clock, so the older traces differ from boundary 0.

## Reproducing

```
ctest --test-dir build -R '^q605_afp_live_etalon$' -V                                  # ~1 min
POM68K_CPU_ENGINE=interp ctest --test-dir build -R '^q605_afp_live_etalon$' -V         # ~4 min
python3 tools/afp_trace_diff.py afp_live_trace_aarch64_interp.txt <other>
```

Bisection instruments used (CHANGELOG 2026-10-02 (later)): `POM68K_AFP_PHASE=6`
to stop early, `POM68K_JIT_DENY_FROM/_TO` to halve the pc space, and
`POM68K_AFP_IOLOG=<from>,<to>` to diff the guest I/O stream between two
machine clocks.

The AArch64 pair predates `bc37cab` (drive B unwired on the portless
profiles, the Q605 among them), which moves every boundary from 0; the
x86-64 pair is the reference at HEAD (CHANGELOG 2026-10-02 (sixth)).
Replayed 2026-10-04 at `3c4b674`: the AArch64 interpreter and `a64` equal the
x86-64 pair at all 22 boundaries — four arms, two hosts, one trace.
