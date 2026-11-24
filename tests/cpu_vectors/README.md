# Public CPU vectors

These are generated WinUAE outputs, with random RAM and CPU state. They
contain no Apple ROM or system media. They are grade-D evidence from the
oracle pinned in `oracle/uae/VENDOR.md` (Hatari `e77819f7`, with the local
strict-FPU adapter and direct-root ATC correction), not an independent silicon reference.

| Model | Vectors | Cells |
|---|---:|---|
| 68030 + 68882 | 1128 | integer core, translated MMU and faults, FPU arithmetic, two-instruction FPU continuation, instructions against a page whose successor is invalid |
| full 68040 | 576 | the same, plus 8 KB pages, three-instruction FPU-handler/FSAVE sequences and faults under random transparent-translation windows |

`manifest.json` records every generator option and seed, plus the count,
uncompressed size and SHA-256 of each canonical JSON stream. The gzip files
keep this small corpus in the repository. The large optional corpora under
`tests/data/` retain their separate `tests/sst_corpora.lock` identities.

Each model ends with 80 traced vectors (`trace=t1` in the recipe and in the
vector name): integer core with the MMU off and on, and faults under
translation. They start one NOP early and run two steps. WinUAE treats a
loaded T1 as just written and does not trace the first step; the NOP takes
that step, so the generated instruction is the traced one and its trace
exception belongs to the vector. `sst68030`/`sst68040` reproduce both rules
in `runVector`.

They are followed by `trace=t0` vectors, trace on change of flow (80 on the
68030; 104 on the 68040, which adds an FPU cell for FSAVE, FMOVEM and
FDBcc). Their lead-in is `EXG D0,D0`, not NOP: the 68040 traces NOP itself
in this mode.

The last 64 vectors of each model are a debugger's single-step loop
(`trace-handler: rte`, six steps): vector 9's handler is an RTE, so three
instructions in a row are run, traced and returned to — 32 under T1, 32
under T0.

`d23a-contract.json` is a separate, unmodified vector from the locked July
030 corpus (`fpu/off seed=11 #292`). It is not part of either stream above.
`sst_known_contract_test.py` uses its oracle format-$3 frame to prove that
the tagged D23a rule changes only the expected format/instruction address:
unrelated register or RAM errors and stale rules must still fail.

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
and addresses occupy separate longwords. Mixed short/long tables remain
outside these recipes. Another 32 `limits` probes
cover upper/lower table bounds, just before/on/after the inclusive boundary,
and reads/writes: all twelve combinations at seed 2026111541.

Two 64-vector `rootlimits` cells (seed 2026111600, short then long
descriptors) enable SRE and bound CRP while supervisor code and exception
frames use unlimited SRP. MOVES.L user-data accesses and PTEST FC=1
probe both root-bound directions at predecessor/equality/successor, for
reads and writes. Each recipe covers all 24 combinations; valid writable
leaves isolate the root limit. Bounded SRP and distinct CRP/SRP mappings
under SRE are not covered.

Two more 64-vector cells (`fcl`, seed 2026111615) add a function-code
lookup before the address tree. Every FC has a distinct physical operand
mapping. Each descriptor size covers all 24 FC/leaf-kind combinations
(valid/WP/invalid), and MOVES.L and PTEST for each FC, including FC=7's
MOVES bypass. Root bounds that must be ignored at the FC lookup vary
between upper zero and lower seven. Reserved FCs describe oracle behavior.
The additional focused probes below cover distinct CRP/SRP mappings.

The six focused MMU families add 224 vectors, in both short- and long-
descriptor geometries:

| Family | Vectors per geometry | Seed (short / long) | Coverage |
|---|---:|---|---|
| `sre` | 16 | 2026111627 / 2026111627 | distinct user/supervisor mappings; all eight bounded-root/access-FC/bound-direction combinations |
| `early` | 16 | 2026111620 / 2026111620 | root/first-level page termination; MOVES/PTEST, including a direct-root write |
| `indirect` | 16 | 2026111620 / 2026111620 | short/long indirect targets, valid and non-page DT; natural target alignment includes addresses not aligned to 16 bytes |
| `mixed` | 16 | 2026111620 / 2026111621 | short tables with long leaves and vice versa; MOVES/PTEST with user/supervisor FC |
| `ttprobe` | 32 | 2026111620 / 2026111620 | TT address/FC masks and R/W(M), distinct transparent/paged operands, protected/invalid leaves, PTEST and locked operations |
| `earlyfcl` | 16 | 2026111620 / 2026111620 | early pages under FCL; direct-root limits still apply without a function-code lookup |

TT1 serves a high logical alias for infrastructure; operand accesses take
the selected translation path. The new families omit explicit zero bytes
from their initial RAM lists; both oracle and replay start from zero-filled
RAM. The previous 680 public 030 vectors retain their original streams.
The shared 68030 walk and direct-root M transition also have independent,
focused controls in `moira_rules_test`; oracle smoke checks direct-root
MOVE/MOVES stores followed by PTEST of the resulting ATC M bit.

The existing SST harnesses compare registers, MMU/FPU state and RAM. These
gates require all 1,232 vectors to execute, with zero MMU skips and no known
divergence list. Missing data, changed bytes, empty data or a harness that
returns success after soft-skipping all fail the gate. Cycle accuracy is
not claimed for these 030/040 vectors.

Since 2026-11-24 the core is Dirk Hoffmann's Moira, whose restart model and
MMU edge cases disagree with WinUAE on part of these vectors
(`docs/MOIRA_CONVERGENCE.md`). The vectors are unchanged; each model's
manifest entry carries a `floor`, the number of vectors measured to agree,
and the gate fails if fewer agree (030: 811 of 1,128; 040: 383 of 576).
Every vector must still run. Raise the floor as rulings and fixes land.

JIT replay forces profit score zero so short 030 vectors can compile. Its
footer must report executed work. AArch64 and x86-64 backends must report
compiled/executed blocks and native instructions. Threaded replay remains
valid on portable targets when it executes blocks or its direct window
pump; it need not compile. A native-backend lockstep with zero compiled
blocks fails this proof contract.

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
