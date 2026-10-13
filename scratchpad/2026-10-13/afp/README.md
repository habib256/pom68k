# `q605_afp_live_etalon` as a Quadra 605

| File | What it is |
|---|---|
| `afp_live_trace_x86_64_interp.txt` | x86-64, `POM68K_CPU_ENGINE=interp` — the accuracy oracle, 22 boundaries |
| `afp_live_trace_x86_64_x64.txt` | x86-64, default (`x64`) — identical to the line above at all 22 boundaries |

They supersede `scratchpad/2026-10-02/afp/`. The rig now builds its board
from the catalogue's Quadra 605 profile (`tests/Q605Profile.h`, board ID
`$A55A2225`) instead of `defaultCoreConfig()`, whose `$A55A2221` made the
guest an LC 475 with a 68040 FPU (BoxFlag 83 → 88, Gestalt machineType
89 → 94). That is the only input the change gives this rig, and it moves every
boundary from 0 (CHANGELOG 2026-10-13 (seventh)).

The old reference still holds for the old identity: `0ae5eae`, rebuilt from a
`git archive` of that commit, reproduces `2026-10-02/afp/afp_live_trace_x86_64_x64.txt`
at all 22 boundaries on the same host the same day.

## Reproducing

```
ctest --test-dir build -R '^q605_afp_live_etalon$' -V
python3 tools/afp_trace_diff.py scratchpad/2026-10-13/afp/afp_live_trace_x86_64_interp.txt <log>
```
