# 2026-11-12 — a64 pinned bases: evidence and the experiments left out

Companion to `CHANGELOG.md` 2026-11-12 (sixth).

| File | What it is |
|---|---|
| `abba_results.txt` | raw output of the five-arm interleaved measurement (arms: `base` = `6c3eb704`, `B` = window kept armed, `AB` = B + settle, `cand` = B + pinned bases, `C` = capped window) |
| `abba.py` | the interleaving script (A…E / E…A, discarded warm-up, `wall_ns` and fingerprint from `POM68K_JIT_METRICS`) |
| `blockprof.py` | attributes the `<unknown binary>` frames of a `sample` call graph to JIT blocks, given a block map |
| `tmp_blockmap_snippet.txt` | the temporary dump that goes just before `auto* c = new A64Compiled;` in `A64Backend::compile` (`POM68K_TMP_BLOCKMAP=<file>`, `POM68K_TMP_BLOCKDUMP=<dir>`); not committed, reads the environment |
| `dis.sh` | disassembles one dumped block (`<dir>/<entry pc>.bin`) with clang + objdump |
| `candidate_A_seam_and_gate.patch` | `Moira::pomJitSettleIrq` and its `moira_rules_test` cell; the engine side is one line in `Engine::executeUntil`: `if (!cpu_.pomJitIdle() && !cpu_.pomJitSettleIrq()) {` |
| `candidate_B_window_armed.patch` | the fetch window kept armed across flagged interpreter steps |
| `candidate_C_capped_window.patch` | the window path capped at the block's own length (refused) |

All patches apply to `6c3eb704`.
