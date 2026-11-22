# POM68K's oracle vectors against Dirk Hoffmann's Moira

`sst_dirk` replays the WinUAE-oracle 68030/68040 corpora of `tests/data/`
against an upstream Moira tree (the `FPU` branch of
`github.com/dirkwhoffmann/Moira`, rebuilt from scratch since 2026-11-02 with
its own MMU, FPU and caches). It reuses the JSON reader, the unit of
execution and the `--strict-ram` rule of `tests/sst68030.cpp` and
`tests/sst68040.cpp`; only the core API differs. It is not a gate: nothing
in POM68K's build references it.

```sh
git clone https://github.com/dirkwhoffmann/Moira.git moira-dirk
git -C moira-dirk checkout FPU
cmake -S tools/dirk_moira -B build-dirk -DCMAKE_BUILD_TYPE=Release \
      -DDIRK_MOIRA=$PWD/moira-dirk/Moira -DPOM68K_TESTS=$PWD/tests
cmake --build build-dirk --parallel
build-dirk/sst_dirk --cpu 30 tests/data/sst68030 --known tests/sst68030.known --strict-ram
build-dirk/sst_dirk --cpu 40 tests/data/sst68040 --known tests/sst68040.known --strict-ram
build-dirk/sst_dirk --cpu 40 --fpu tests/data/sst68040_fpu --known tests/sst68040_fpu.known --strict-ram
```

`--tsv FILE` writes one row per mismatch (file, vector, opcode, whether
POM68K lists it as a known divergence, first differing field); `--name V
--all` prints every differing field of one vector.

Reading a mismatch takes care. The vectors are WinUAE's answers, so a
mismatch is a disagreement with WinUAE, not a defect by itself: where
POM68K overrules the oracle it lists the vector in its `.known` file, and
the oracle has known weaknesses (transcendentals through host doubles,
for one). The harness also preloads the prefetch queue without a bus
cycle, as POM68K's own runners do, so the descriptor U bit of the page
holding the first instruction is not set by a core that fetches nothing
else from that page. Findings and their evidence are recorded in
`CHANGELOG.md` (2026-11-22, eighth).
