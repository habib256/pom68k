# `q605_afp_live_etalon` on AArch64 after the a64 late-IPL fix

| File | What it is |
|---|---|
| `afp_live_trace_aarch64_interp.txt` | this host, `POM68K_CPU_ENGINE=interp` — the accuracy oracle, 23 boundaries |
| `afp_live_trace_aarch64_a64.txt` | this host, default (`a64`) after the fix — identical to the oracle at every boundary |

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

Open: the same comparison on x86-64, interpreter and `x64`.
