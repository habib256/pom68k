# Public CPU vectors

These are generated WinUAE outputs, with random RAM and CPU state. They
contain no Apple ROM or system media. They are grade-D evidence from the
oracle pinned in `oracle/uae/VENDOR.md` (Hatari `e77819f7`, with the local
strict-FPU adapter), not an independent silicon reference.

| Model | Vectors | Cells |
|---|---:|---|
| 68030 + 68882 | 112 | integer core, translated MMU and faults, FPU arithmetic, two-instruction FPU continuation |
| full 68040 | 144 | the same, plus 8 KB pages and three-instruction FPU-handler/FSAVE sequences |

`manifest.json` records every generator option and seed, plus the count,
uncompressed size and SHA-256 of each canonical JSON stream. The gzip files
keep this small corpus in the repository. The large optional corpora under
`tests/data/` retain their separate `tests/sst_corpora.lock` identities.

The existing SST harnesses compare registers, MMU/FPU state and RAM. These
gates require all 256 vectors to execute, with zero MMU skips and no known
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
