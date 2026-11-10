# Bare-core regression reproductions

A standalone reproduction of an open defect can become a permanent CTest
gate with its implementation fix. This index preserves that transition.

## 68040 ATC FC2

The reproduction originally lived at `tests/repros/mmu040_fc2.cpp` and failed
184 checks across 40 CPU sequences on commit `1b36defd`. It now lives in
[`tests/mmu040_atc_test.cpp`](../mmu040_atc_test.cpp), registered as
`mmu040_atc_test`. Four added MOVES/cache-publication sequences raise the
pre-fix failure count to 192.

```sh
cmake --build build --target mmu040_atc_test -j 4
ctest --test-dir build -R '^mmu040_atc_test$' --output-on-failure
```

The gate covers 68LC040/68040, 4K/8K pages, caches enabled/disabled, either
privilege mode first, instruction/data translations, JIT probes, MOVES
SFC/DFC and cache publications bound to the right ATC entry. It preserves
both resident translations after guest descriptor edits: a mode-switch
flush is not a valid fix. MC68040UM-1993 §3.3 distinguishes the FC2 tag from
the descriptor's supervisor-protection bit.

The gate has since gained register-retention and PFLUSH/PTEST cells
(`POM68K_VENDOR.md` rows 61/62). The 64-entry/four-way geometry and
replacement policy remain open.

## 68040 MOVEP partial stores

`moira_rules_test` holds 192 MOVEP sequences and 36 ordinary-store controls:
both 040 models, every .W/.L byte-fault position, external /BERR or 4K/8K
page faults, data-window prefixes, active caches, partial memory effects
and completion after repair/RTE. The old core fails 558 checks. These are
grade-D cells for the existing re-execution model, not silicon captures.

```sh
cmake --build build --target moira_rules_test -j 4
ctest --test-dir build -R '^moira_rules_test$' --output-on-failure
python3 scratchpad/2026-11-09/movep040-repro.py \
  --oracle /path/to/liboracle_uae.dylib --out /tmp/movep040/seed845.json
./build/sst68040 /tmp/movep040 --verbose
```

The last two commands regenerate and replay the original
`random/identity/8k seed=845 #1967` witness. The recipe pins its initial
state hash and reports generator drift. The permanent CTest gate needs
neither the oracle nor its generated vector.
