#!/usr/bin/env bash
# POM68K — what does the asset-free tier actually reach?
#
# Third item of the 2026-08-26 review. The interesting output is NOT a global
# percentage: it is the list of src/ files with ZERO executed lines, because
# that list is the inventory of code only the private ROM/disk assets prove.
# A percentage moves when a test file grows; the zero list moves only when
# coverage of the product changes, which is the thing worth watching.
#
#   cmake -S . -B build-cov -DPOM68K_COVERAGE=ON -DPOM68K_NATIVE=OFF -DPOM68K_LTO=OFF
#   cmake --build build-cov -j
#   tools/coverage_report.sh build-cov [ctest-label]
#
# Writes <build>/coverage.txt (the full per-file report) and prints the
# summary plus the zero list. Label defaults to asset-none.
#
# Clang and GCC do not read each other's data: Clang instruments profiles and
# is read back with llvm-cov, GCC drops .gcno/.gcda beside the objects and is
# read with gcovr. Both paths are here because the CI leg is GCC and the dev
# host is Clang.

set -uo pipefail

build=${1:-build-cov}
label=${2:-asset-none}

if [ ! -d "$build" ]; then
    echo "usage: $0 <coverage-build-dir> [ctest-label]" >&2
    exit 2
fi
if ! grep -q "POM68K_COVERAGE:BOOL=ON" "$build/CMakeCache.txt" 2>/dev/null; then
    echo "FAIL: $build was not configured with -DPOM68K_COVERAGE=ON" >&2
    exit 2
fi

profdir="$build/coverage-profiles"
rm -rf "$profdir"; mkdir -p "$profdir"

echo "== running ctest -L $label under instrumentation =="
# %p keeps one raw profile per process; the gates fork nothing, but a single
# fixed filename would have every gate overwrite the previous one's data.
LLVM_PROFILE_FILE="$profdir/%p.profraw" \
    ctest --test-dir "$build" -L "$label" -j "$(getconf _NPROCESSORS_ONLN)" \
    > "$build/coverage-ctest.log" 2>&1
ctest_status=$?
tail -3 "$build/coverage-ctest.log"
if [ "$ctest_status" -ne 0 ]; then
    echo "WARN: the tier was not green; the report below covers what ran" >&2
fi

report="$build/coverage.txt"

if compgen -G "$profdir/*.profraw" > /dev/null; then
    # ── Clang: merge the raw profiles, then report per file ──────────────
    profdata=$(command -v llvm-profdata || xcrun --find llvm-profdata 2>/dev/null)
    cov=$(command -v llvm-cov || xcrun --find llvm-cov 2>/dev/null)
    if [ -z "$profdata" ] || [ -z "$cov" ]; then
        echo "FAIL: llvm-profdata/llvm-cov not found" >&2
        exit 2
    fi
    "$profdata" merge -sparse "$profdir"/*.profraw -o "$build/coverage.profdata" || exit 2
    # One -object per gate binary: llvm-cov reports the union of what they
    # cover, which is exactly the question ("does ANY gate in the tier reach
    # this file?").
    objs=()
    while IFS= read -r line; do objs+=(-object "$line"); done < <(
        ctest --test-dir "$build" -N -L "$label" 2>/dev/null |
        sed -n 's/.*Test *#[0-9]*: //p' |
        while read -r name; do
            [ -x "$build/$name" ] && echo "$build/$name"
        done)
    # Script-driven gates run a binary the registry does not name: gui_smoke_test
    # is `bash tools/run_gui_smoke.sh POM68K ...`, so without this the whole GUI
    # layer reads as never-executed and the report blames the code for a gap in
    # the tooling. Measured 2026-08-27: 66 "unreached" files fell to 41 once the
    # product binary joined the object list.
    [ -x "$build/POM68K" ] && objs+=(-object "$build/POM68K")
    if [ "${#objs[@]}" -eq 0 ]; then
        echo "FAIL: no gate binaries found for label $label" >&2
        exit 2
    fi
    "$cov" report "${objs[@]}" -instr-profile="$build/coverage.profdata" \
        -ignore-filename-regex='(extern|imgui|tests)/' > "$report" 2>/dev/null || exit 2
elif compgen -G "$build/**/*.gcda" > /dev/null 2>&1 || find "$build" -name '*.gcda' -print -quit | grep -q .; then
    # ── GCC: gcovr over the .gcda dropped beside the objects ─────────────
    if ! command -v gcovr > /dev/null; then
        echo "FAIL: gcovr not installed (pip install gcovr)" >&2
        exit 2
    fi
    gcovr --root . --filter 'src/' --print-summary --txt -o "$report" \
          --csv "$build/coverage.csv" || exit 2
else
    echo "FAIL: no coverage data produced — was the tree built with POM68K_COVERAGE=ON?" >&2
    exit 2
fi

echo
echo "== summary =="
tail -2 "$report"

# ── Normalise: <path>\t<line-coverage %> ────────────────────────────────────
# llvm-cov and gcovr disagree about everything except the question being asked,
# so the two formats collapse to one table here and the rest of the script
# never learns which toolchain ran.
norm="$build/coverage-files.tsv"
: > "$norm"
if [ -f "$build/coverage.csv" ]; then
    # gcovr --csv: filename,line_total,line_covered,line_percent,...
    awk -F, 'NR > 1 && $1 != "" { sub(/^src\//, "", $1); print $1 "\t" $4 }' \
        "$build/coverage.csv" >> "$norm"
else
    # llvm-cov report columns: Filename Regions Missed Cover Functions Missed
    # Executed Lines Missed Cover Branches Missed Cover -> line cover is $10.
    awk 'NF >= 10 && $1 ~ /\.(cpp|h)$/ { sub(/%$/, "", $10); print $1 "\t" $10 }' \
        "$report" >> "$norm"
fi

echo
echo "== src/ code the '$label' tier never reaches =="
# Two shapes of "never reached", and reporting only the first would flatter the
# tier: a file the report lists at 0% of lines, and a file absent from the
# report entirely because nothing in the tier links it. The second is the
# larger and quieter half.
{
    awk -F'\t' '$2 + 0 == 0 { print $1 }' "$norm"
    if [ -d src ]; then
        listed=$(cut -f1 "$norm")
        for f in $(cd src && find . -name '*.cpp' -o -name '*.h' | sed 's|^\./||' | sort); do
            printf '%s\n' "$listed" | grep -qx "$f" || echo "$f"
        done
    fi
} | sort -u | sed 's/^/  /' | tee "$build/coverage-zero.txt"
count=$(wc -l < "$build/coverage-zero.txt" | tr -d ' ')
echo
echo "$count src/ file(s) with no executed line in the '$label' tier"
echo "reports: $report, $norm, $build/coverage-zero.txt"
