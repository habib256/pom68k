# Public CPU vectors

These are generated WinUAE outputs, with random RAM and CPU state. They
contain no Apple ROM or system media. They are grade-D evidence from the
oracle pinned in `oracle/uae/VENDOR.md` (Hatari `e77819f7`, with the local
strict-FPU adapter), not an independent silicon reference.

| Model | Vectors | Cells |
|---|---:|---|
| 68030 + 68882 | 424 | integer core, translated MMU and faults, FPU arithmetic, two-instruction FPU continuation, instructions against a page whose successor is invalid |
| full 68040 | 328 | the same, plus 8 KB pages, three-instruction FPU-handler/FSAVE sequences and faults under random transparent-translation windows |

`manifest.json` records every generator option and seed, plus the count,
uncompressed size and SHA-256 of each canonical JSON stream. The gzip files
keep this small corpus in the repository. The large optional corpora under
`tests/data/` retain their separate `tests/sst_corpora.lock` identities.

The page-end cells include legal MOVEM word/long loads and stores, with
the register mask ending the valid page or crossing into the invalid page.
Illegal EA forms are excluded to keep the D39 ruling out of these cells.

Each model also includes 128 `earaw` vectors (MMU off and identity):
full-format indexed MOVE.L, LEA and PEA, with independently drawn reserved
and legal extension fields. Reserved results pin oracle behavior, not a
hardware guarantee.

The 68030 adds 64 vectors using 4 KB pages and two levels of short
descriptors: core, fault, read-modify-write and page-end cells. The old
32 KB one-level recipes retain their identities. Another 64 vectors use
the same four families with long descriptors at both levels; attributes
and addresses occupy separate longwords. Mixed short/long tables and
root limits are not covered by these recipes. Another 32 `limits` probes
cover upper/lower table bounds, just before/on/after the inclusive boundary,
and reads/writes: all twelve combinations at seed 2026111541.

The existing SST harnesses compare registers, MMU/FPU state and RAM. These
gates require all 752 vectors to execute, with zero MMU skips and no known
divergence list. Missing data, changed bytes, empty data or a harness that
returns success after soft-skipping all fail the gate. Cycle accuracy is
not claimed for these 030/040 vectors.

```sh
cmake --build build --target sst68030 sst68040
ctest --test-dir build -L cpu-vectors --output-on-failure
```

The gates carry `asset-none`, so the Linux CI and nightly ASan/UBSan jobs
execute them from a clean checkout. `cpu_vectors_contract_test` holds the
failure paths, including a successful exit with no executed vectors.
`softfloat_primitives_test` shares the tier and holds the two C undefined
behaviors found by the larger sanitizer replay: shifts by 64 and signed
negation of `INT32_MIN`. It checks 1,040 wide shifts and five exact extended
conversions without an oracle.

Reproduce the pins with the separate oracle project and Python with NumPy
(the generator has a slower pure-Python fallback):

```sh
cmake -S oracle/uae -B build-oracle -DCMAKE_BUILD_TYPE=Release
cmake --build build-oracle --parallel 2
ctest --test-dir build-oracle --output-on-failure
python3 tools/cpu_vectors.py regenerate --oracle build-oracle/liboracle_uae.so
```

On macOS the library suffix is `.dylib`. Regeneration checks the existing
pins without writing them. Use `--write` only to prepare a reviewed corpus
change; inspect the new recipes, identities and replay results together.
The CI oracle step builds both smoke tests and checks regeneration; runtime
replay needs neither the oracle library nor NumPy.
