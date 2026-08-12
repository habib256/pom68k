# POM68K on the Raspberry Pi — the build recipe, and what each lever is worth

The Pi is the host where POM68K's margin is thinnest: an LC II at cache-boost
4, or any 68040 profile, wants every cycle a Cortex-A72 can give. This file is
the *how* and the *why* of squeezing the Pi specifically. It is a port of
NeoST's campaign (`../../neost/docs/PERFORMANCE.md`, `packaging/raspberry/`) —
same 68k interpreter (Moira), same conclusions, and the provenance of every
number below is stated because **most of them were not measured on POM68K**.

**None of it changes a single emulated value.** PGO, LTO and `-mcpu` all move
code layout and instruction selection, never semantics. The check that proves
it is `tests/jit_bench.cpp`: it prints an architectural-state fingerprint next
to its wall-clock, and two builds of the same tree must print the same one.

---

## 1. The three levers, and which apply to whom

| Lever | What it does | Portable? | Where it is set |
|---|---|---|---|
| **LTO** | inlines across translation units; lets the Moira dispatch and the bus fast path see each other | **yes** — no ISA change | `POM68K_LTO` (CMake) |
| **`-mtune=cortex-a72`** | schedules for the A72's pipeline; emits **no** A72-only instruction, so a Pi 3 still loads the binary | **yes** | `POM68K_TUNE` (CMake) |
| **`-mcpu=cortex-a72`** | the above **plus** raises the ISA floor to that core — **measured worth on this codebase: nothing, see § 1bis** | **no** | `packaging/raspberry/build_native_pi.sh` (on the board) and `build_in_bionic_pi.sh` (CI); both pass it through `CMAKE_CXX_FLAGS`, never `POM68K_TUNE`, which only emits `-mtune=` |
| **PGO** | lays the frequent branch outcome out in sequence | yes, but needs a training run with real assets | `POM68K_PGO=generate\|use` (CMake), driven by `tools/pgo_train.sh` / `--pgo`; the training load itself is `tools/pgo_train_run.sh` |

The split matters because two different people build POM68K for a Pi:

- **the one who downloads `POM68K-<v>-aarch64.AppImage`** — gets LTO and
  `-mtune=cortex-a72`, and nothing that could stop the file loading on a Pi 3
  or a Pi 5;
- **the one who compiles on the Pi** (`README.md`'s normal path) — gets
  `-mcpu=<the exact core>` and, with `--pgo`, the profile too.

---

## 1bis. `-mcpu` buys nothing over `-mtune` here — measured

Written down first because it is the one result that contradicts the recipe
this file ports, and because it is cheap to re-derive.

`-mcpu=X` is exactly `-march=<X's architecture> -mtune=X`. The generic
release AppImage already carries `-mtune=cortex-a72` (§ 2). What is left is
the *architecture* delta between the Cortex-A72 and baseline armv8-a: `crc`
and `crypto`. GCC does not emit either on its own — they arrive through
intrinsics, and POM68K uses none.

So the two builds should produce the same code, and they do. The aarch64
binaries out of `release.yml` and `pi400.yml` differ by **27 bytes out of
8 698 128**, in exactly two places:

| offset | bytes | what |
|---|---|---|
| 700-719 | 20 | `.note.gnu.build-id` — a hash of the contents, different between *any* two links |
| 7 531 192-7 531 198 | 7 | the version string (`0.1.0` vs `daba645`) |

Nothing else. **The machine code is byte-identical.**

Two consequences, and the second is the useful one:

- **A Pi 4/400 should use the release AppImage**, which is the same code and
  also runs everywhere else. `pi400.yml`'s remaining reason to exist is its
  **tarball** (§ 4bis), not speed.
- **NeoST's "~10-20 % for `-mcpu`" was never about the ISA floor.** It is the
  gain of a tuned build over a `-mtune=generic` one — which is real, and which
  POM68K now collects in the *portable* artifact. The lever was the cost
  model all along; raising the floor was incidental to it.

This is measured per run rather than remembered: `build_in_bionic_pi.sh`
compiles both ways, strips the build-id, diffs, and the job summary reports
which answer came out. A Pi 5's Cortex-A76 is armv8.2-a — LSE atomics, fp16,
dotprod — where the answer may genuinely differ.

---

## 2. The bug this port fixed on the way: released binaries had no LTO

`POM68K_NATIVE` used to mean *both* "`-march=native`" *and* "LTO". Every
distributable build therefore sets `POM68K_NATIVE=OFF` — and silently lost its
LTO with it. All four release artifacts, the Pi one included, shipped as plain
`-O3`.

LTO is not a portability hazard: it changes what the linker can inline, not
what the CPU must support. The knobs are separate since 2026-08-08, and
`packaging/linux/build_in_bionic.sh` now passes `-DPOM68K_LTO=ON`.

> Not yet applied to the macOS `.dmg` or the Windows `.zip`. The same argument
> holds for both; the reason to stop at Linux is that neither was exercised
> here, and MSVC needs `/GL` + `/LTCG`, which the CMake block skips today.

---

## 3. What the numbers actually are

Be precise about provenance — a borrowed number quoted as a local measurement
is how a "known" gain survives being wrong.

**Measured on POM68K, x86-64** (`CMakeLists.txt`, 2026-07-28):
PGO gives **−33 %** on the interpreter and **−18 %** on the JIT, with the state
fingerprints matching the plain build.

**Measured on NeoST, Cortex-A72**, on the same Moira interpreter
(`../../neost/docs/PERFORMANCE.md` § 5) — the closest thing to a prediction for
POM68K on a Pi that exists today:

| Variant | vs `-O3` |
|---|---|
| `-O3` + PGO | −20 % |
| `-O3` + PGO + LTO | **−34 %** |

and, separately, ~10-20 % for `-mcpu=<exact core>` over generic aarch64 —
which § 1bis shows is the **tuning** half of that flag, not the ISA floor, and
which the portable release artifact therefore already collects.

**Not measured anywhere yet:** POM68K on a Pi, before and after. Producing
that is the open item. Do it with the fixed-budget harness, never with a boot
etalon — an etalon stops the instant it recognises the Finder, so two builds
get timed over *different amounts of guest work* and the ratio flatters
whichever arrived first:

```bash
cmake --build build-pi -j2 --target jit_bench
POM68K_BENCH_FRAMES=600 ./build-pi/jit_bench     # wall-clock + fingerprint
```

---

## 4. The recipe on the Pi

```bash
./setup_imgui.sh                                     # once
packaging/raspberry/build_native_pi.sh               # -mcpu for this board
packaging/raspberry/build_native_pi.sh --pgo         # + PGO + LTO (recommended)
sudo packaging/raspberry/build_native_pi.sh --pgo --install    # → /opt/pom68k
```

**LTO is off in the plain run**; `--pgo` turns it on, and only above 2 GB of
RAM (third bullet below). A non-PGO build that wants it needs `POM68K_LTO=1`
in the environment — the script maps that to `-DPOM68K_LTO=ON` and otherwise
passes `OFF`, because a Pi's LTO link is minutes long and the operator who
skipped `--pgo` asked for the short path.

What the script does that a plain `cmake && make` does not:

- **Reads `/proc/device-tree/model`** rather than trusting `-mcpu=native`: on
  some 64-bit Pi kernels the MIDR GCC reads is incomplete and detection
  degrades to generic *without saying so*. Pi 5 → `cortex-a76`, Pi 4/400 →
  `cortex-a72`, Pi 3 → `cortex-a53`. A `-mcpu` the compiler rejects falls back
  to generic immediately instead of failing twenty minutes later.
- **Sizes `-j` against RAM**, ~1.2 GB per job. `-j4` on a 2 GB board OOM-kills
  `cc1plus` on `Moira.cpp`, and the symptom reads like a compiler bug.
- **Gates LTO on ≥ 2 GB** in the PGO pass: below that, PGO alone keeps most of
  the gain and the LTO link would thrash.
- **`--install` never overwrites data.** `roms/`, `hdv/`, `disks35/`, `assets/`
  are copied with `cp -rn`. POM68K *writes* to disk images; clobbering one on
  a binary upgrade destroys a guest volume.

**Budget the wait.** The instrumented pass is dominated by a single
translation unit: `Moira.cpp` under `-fprofile-generate` took **13 of the
14m42** a 16-core x86-64 desktop needed, single-threaded, with every other
object blocked behind it. `-j` buys nothing there, and a Pi is several times
slower. A full `--pgo` run on a Pi 4 is an afternoon, not a coffee break.

Training is long too — seven gates × two engines. Narrow it:

```bash
POM68K_PGO_GATES="q605 lcii classic" POM68K_PGO_ENGINES=interp \
    packaging/raspberry/build_native_pi.sh --pgo
```

---

## 4bis. The CI package: `-mcpu`, on demand

`.github/workflows/pi400.yml` — **Run workflow**, pick a core. Built on
GitHub's native arm64 runner, in the same pinned bionic image the release
uses, so the glibc floor stays 2.27 (Pi OS bookworm is 2.36).

**Take the tarball from it; on a Pi 4/400, take the AppImage from the
release.** § 1bis is why: for `cortex-a72` the raised floor produces identical
code, so the only thing this workflow adds is a package that does not need
libfuse2. The workflow measures that per run and says so in its summary.

The two Pi artifacts are still genuinely different products, and the filename
is the only thing that tells them apart:

| | ISA floor | Runs on | Where from |
|---|---|---|---|
| `POM68K-<v>-aarch64.AppImage` | armv8-a, `-mtune=cortex-a72` | every aarch64 machine | every tag, `release.yml` |
| `POM68K-<v>-pi400-aarch64.AppImage` | **the chosen core** | that core and its supersets | on demand, `pi400.yml` |

Two packages come out of one build, no recompilation. The `.tar.gz` is not a
convenience: **Raspberry Pi OS bookworm does not install `libfuse2`**, which a
type-2 AppImage needs in order to mount itself. Unpacked, there is nothing to
mount — and the launcher is the same `AppRun`, whose third candidate is its
own directory, so `roms/` goes at the root of the unpacked tree and the whole
folder is the installation.

The build **fails** rather than falls back if the compiler rejects the
`-mcpu`, and asserts the flag reached the real compile line
(`build-pi400/CMakeFiles/pom68k_core.dir/flags.make`) — CMake would otherwise
have dropped it and produced a generic binary under a `pi400` name, which is
worse than no artifact. What that does *not* prove is which instructions GCC
then chose to emit; the claim verified is the flag, not the encoding.

No PGO, and it cannot have any — see § 6.

---

## 5. The PGO trap: its failure mode is silence

GCC names each `.gcda` after the **absolute path of the object** it belongs
to. Instrument in `build-A`, read back from `build-B`, and the use pass finds
**no profile at all** — and `-Wno-missing-profile`, which this tree needs for
the GUI objects nothing trains, makes that outcome completely silent. The
binary comes out with no gain and no message. NeoST published a "PGO = −4 %"
that was nothing but this.

Two defences, both in place:

1. **One build directory for both passes** — `tools/pgo_train.sh` and
   `build_native_pi.sh` each reconfigure in place rather than switching trees.
2. **`tools/pgo_train_run.sh` audits what the run wrote** and exits non-zero
   when: no gate ran at all (every one soft-skipped for missing assets), or no
   counter file landed under the build directory, or no `Moira.cpp.gcda`
   exists — the one translation unit every gate necessarily executes.

A gate that *fails* is reported but still counted as training: its counters
describe code that really ran. A red gate here almost always means the
instrumented build is broken, and a profile from a broken build is worth
nothing — hence the warning line.

### The other half: cold code

Even the broad set below leaves most machine and device translation units
cold, and GCC optimizes cold functions **for size**. Untrained profiles would
come out of a PGO build *slower* than out of a plain `-O3` one.
`-fprofile-partial-training` (GCC ≥ 10, probed —
`CMakeLists.txt:174-176`) is what stops that, and the training set is
deliberately broad for the same reason — one machine per CPU family, plus the
floppy path no hard-disk boot ever reaches. The list lives in **one** place,
`tools/pgo_train_run.sh`'s `kGates`, shared by both recipes:

| Label | Gate | Covers |
|---|---|---|
| `q605` | `q605_boot_etalon` | 68040 + MMU, MEMCjr/DAFB/Cuda LLE |
| `lc3` | `lc3_boot_etalon` | 68030, MMU on, Sonora |
| `lcii` | `lcii_boot_etalon` | 68030, MMU off, V8 |
| `lc` | `lc_boot_etalon` | 68020 + HMMU |
| `macii` | `macii_boot_etalon` | 68020, GLUE/NuBus front end |
| `classic` | `compact_boot_etalon` | 68000 + contention |
| `plusfloppy` | `system_boot_etalon` | 68000 + IWM/GCR floppy engine |

The shipped recipe trained on the Quadra alone until 2026-07-28, which laid
out every 68030 machine's hot loop as if it were cold.

---

## 6. Not ported, and why

- **PGO in CI.** The `-mcpu` half of NeoST's Pi workflow *was* ported (§ 4bis);
  its PGO half cannot be. NeoST trains on its ARM64 runner because its
  TOS ROMs are in the repository. **POM68K's ROMs are user-provided and never
  committed**, so a runner has nothing to boot and any profile it collected
  would be empty — the exact silent failure § 5 describes. Training has to
  happen where the assets are. Reopening condition: an asset-free workload
  that is *representative* (the SST vector suites are not — they exercise
  every opcode uniformly, which is the opposite of a profile).
- **Kiosk/session/Bluetooth provisioning** (`install_kiosk.sh`,
  `neost-kiosk@.service`, …). That is arcade-cabinet plumbing for a
  single-application box. POM68K has no such product shape today; porting it
  would be inventing a requirement.
- **NeoST's code-level optimizations** (§ 3 of its PERFORMANCE.md: MMU decode
  cache, inline bus fast path, scheduler O(1) `nextDue`). POM68K has already
  been down this road with its own profile, and the answers differ because the
  machines differ. The peripheral **deadline** mechanism (`TODO.md` § 4) is
  POM68K's version of the scheduler fix — 833.2 M → 86.65 M `tick()` calls on
  the Q605 boot. Page-granular memory dispatch and an O(1) ATC lookup were
  both **measured and dropped** (`TODO.md`, *Measured and DROPPED*). Do not
  re-open either from NeoST's numbers; re-open them from POM68K's.

---

## 7. Verified here, and not

Written and checked on x86-64. What that host can prove, it proved:

- the CMake plumbing configures — `POM68K_LTO=ON` with `POM68K_NATIVE=OFF`
  now really emits `-flto=auto` (it did not before), `-fprofile-partial-training`
  is probed and accepted, and a `-mtune=` the compiler rejects produces a
  warning and is dropped rather than breaking the build;
- the whole PGO cycle runs on a genuinely instrumented build: an
  `-fprofile-generate` gate wrote 25 `.gcda` (`Moira.cpp.gcda` among them),
  the audit passed, and a translation unit recompiled with
  `-fprofile-use -fprofile-correction -fprofile-partial-training
  -Wmissing-profile` consumed that profile **without a missing-profile
  warning** — which is precisely the silent failure § 5 is about;
- `pgo_train_run.sh` lists its gates, trains on a real one, and fails loudly
  on both audit branches (no gate ran / no counters written).

What it cannot: **no aarch64 compiler, emulator or Pi was available**, so
`-mcpu=cortex-a72`, `-mtune=cortex-a72` and the whole
`build_native_pi.sh` path are unexercised. The release AppImage change
(`build_in_bionic.sh`) likewise needs a `release.yml` dispatch to be real.
