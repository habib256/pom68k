#!/usr/bin/env bash
# POM68K — the PGO TRAINING RUN.
#
# One definition of "a representative load", shared by every PGO recipe:
# tools/pgo_train.sh (build here, run here) and
# packaging/raspberry/build_native_pi.sh (build on the Pi). Duplicating the
# gate list would let the two drift, and a training set that drifts is a
# training set nobody trusts.
#
#   tools/pgo_train_run.sh --list        print the gate binaries, one per line
#                                        (what the instrumented pass must build)
#   tools/pgo_train_run.sh <build-dir>   run the load, then audit what it wrote
#
# Env:
#   POM68K_PGO_GATES    space-separated subset of labels (see kGates below).
#                       A Pi 4 takes minutes per gate; "q605 lcii classic" is
#                       a defensible short set.
#   POM68K_PGO_ENGINES  "interp jit" (default) or "interp".
#   LLVM_PROFILE_FILE   honoured by Clang builds. GCC ignores it and keeps
#                       writing .gcda beside its objects, which is exactly why
#                       both passes must share ONE build directory.
#
# WHY THE BREADTH. GCC gives a function the training set never entered the
# treatment it reserves for cold code. Train on the Quadra alone and every
# 68030 machine ships with its hot loop laid out as if it were cold — the
# shipped recipe did precisely that until 2026-07-28. One machine per CPU
# family, plus the paths no hard-disk boot reaches (the IWM/GCR floppy
# engine), is the floor. `-fprofile-partial-training` in CMakeLists.txt
# covers what remains cold on purpose.
#
# Bit-identical emulation is unaffected throughout: PGO changes code layout,
# never semantics.
set -uo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

# label | gate binary | extra environment
# One machine per CPU family, then the paths a hard-disk boot never touches.
kGates=(
    "q605|q605_boot_etalon|"                       # 68040 + MMU, MEMCjr/DAFB/Cuda LLE
    "lc3|lc3_boot_etalon|"                         # 68030, MMU on, Sonora
    "lcii|lcii_boot_etalon|"                       # 68030, MMU off, V8
    "lc|lc_boot_etalon|"                           # 68020 + HMMU
    "macii|macii_boot_etalon|"                     # 68020, GLUE/NuBus front end
    "classic|compact_boot_etalon|POM68K_COMPACT_MODEL=classic"  # 68000 + contention
    "plusfloppy|system_boot_etalon|"               # 68000 + IWM/GCR: no HD boot goes here
)

if [ "${1:-}" = "--list" ]; then
    # De-duplicated: two labels can share one binary.
    for g in "${kGates[@]}"; do printf '%s\n' "$(echo "$g" | cut -d'|' -f2)"; done | sort -u
    exit 0
fi

BUILD="${1:?usage: pgo_train_run.sh --list | pgo_train_run.sh <build-dir>}"
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
[ -d "$BUILD" ] || { echo "error: no such build directory: $BUILD" >&2; exit 1; }

ENGINES="${POM68K_PGO_ENGINES:-interp jit}"
WANTED="${POM68K_PGO_GATES:-}"

ran=0 skipped=0 missing=0 failed=0
for g in "${kGates[@]}"; do
    label="${g%%|*}"; rest="${g#*|}"
    bin="${rest%%|*}"; env_kv="${rest#*|}"
    if [ -n "$WANTED" ] && ! printf ' %s ' "$WANTED" | grep -q " $label "; then continue; fi
    if [ ! -x "$BUILD/$bin" ]; then
        printf '  [pgo] %-14s %-22s not built\n' "$label" "$bin"; missing=$((missing + 1)); continue
    fi
    for eng in $ENGINES; do
        # A gate soft-skips (exit 0, "SKIP:" on stdout) when the user-provided
        # ROM or disk image is absent. That must not fail the build — a missing
        # asset costs coverage for one family, not correctness — but it must
        # not be counted as training either, or a tree with no assets at all
        # would "succeed" at collecting nothing.
        printf '  [pgo] %-14s %-9s ' "$label" "$eng"
        out=$(
            cd "$ROOT" || exit 1
            if [ "$eng" = jit ]; then
                # `auto` picks the backend whose full-boot gate is closed on
                # this host — the AArch64 code generator on a Pi, `threaded`
                # for the guests it does not cover.
                export POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=auto
            fi
            [ -n "$env_kv" ] && export "${env_kv?}"
            "$BUILD/$bin" 2>&1
        )
        rc=$?
        if printf '%s' "$out" | grep -q '^SKIP:'; then
            echo "skipped (assets)"; skipped=$((skipped + 1))
        elif [ "$rc" -ne 0 ]; then
            # A gate that FAILS still leaves counters, and its counters are
            # still representative of the code that ran — so this trains, it
            # does not abort. But say so: a red gate here usually means the
            # instrumented build is broken, and a profile taken from a broken
            # build is worth exactly nothing.
            echo "ran but FAILED (rc=$rc)"; ran=$((ran + 1)); failed=$((failed + 1))
        else
            echo "ok"; ran=$((ran + 1))
        fi
    done
done

echo "[pgo_train_run] ${ran} trained (${failed} failing), ${skipped} skipped, ${missing} not built"
[ "$failed" -gt 0 ] && echo "[pgo_train_run] WARNING: ${failed} gate(s) failed — check the build before trusting this profile"

# ── The audit: PGO's failure mode is SILENCE ────────────────────────────────
# GCC names each .gcda after the ABSOLUTE path of the object it belongs to.
# Instrument in one build directory and read back from another and the use
# pass finds no profile at all — and `-Wno-missing-profile` (which this tree
# needs, for the GUI objects that are never trained) makes that failure
# completely silent: the binary comes out with no gain and no message. The
# only defence is to check that counters actually landed.
if [ "$ran" -eq 0 ]; then
    echo "error: no gate ran — every one soft-skipped or was not built." >&2
    echo "       A PGO build on this profile would be a plain -O3 build that" >&2
    echo "       merely took twice as long. Provide roms/ and hdv/ assets," >&2
    echo "       or narrow POM68K_PGO_GATES to what this tree can run." >&2
    exit 1
fi

gcda=$(find "$BUILD" -name '*.gcda' 2>/dev/null | wc -l)
raw=$(find "$BUILD" -name '*.profraw' 2>/dev/null | wc -l)
if [ "$gcda" -eq 0 ] && [ "$raw" -eq 0 ]; then
    echo "error: gates ran but no counter file was written under $BUILD." >&2
    echo "       The binaries are not instrumented, or they are not the ones" >&2
    echo "       this directory built. Both PGO passes must share ONE build dir." >&2
    exit 1
fi
if [ "$gcda" -gt 0 ]; then
    # GCC writes a .gcda for every instrumented object LINKED INTO the program
    # that ran, so presence proves the paths line up; the counts inside are
    # what `ran` above vouches for. Only Moira.cpp is demanded: it is the one
    # translation unit every gate necessarily executes. Naming a machine's own
    # .cpp here would misfire whenever POM68K_PGO_GATES narrows the set — a
    # static library contributes only the objects the link actually pulls in.
    find "$BUILD" -name 'Moira.cpp.gcda' | grep -q . || {
        echo "error: gates ran, counters were written, but none for Moira.cpp —" >&2
        echo "       the use pass would fall back to guessed branch probabilities" >&2
        echo "       for the one loop that matters. Check that the instrumented" >&2
        echo "       binaries came from THIS build directory." >&2
        exit 1; }
fi
echo "[pgo_train_run] counters: ${gcda} gcda, ${raw} profraw"
