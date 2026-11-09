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

The 64-entry/four-way geometry, replacement policy and PFLUSH/PTEST side
effects remain separate open items. This gate does not establish them.
