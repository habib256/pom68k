#!/usr/bin/env bash
# POM68K TODO § B.2 slices 1+2 — the A/B, to be run on a QUIET host.
#
# Protocol (docs/MEASURING.md § 1 R1/R2/R3, § 4.1):
#   0. refuse to start on a busy host (1-min load over 1/4 of the hardware
#      threads), the same bar `bench::compare` applies at both ends of a run;
#   1. the NULL experiment FIRST — this host's noise floor, measured, not
#      assumed (§ 4.1). `POM68K_BENCH_NULL=1 POM68K_BENCH_COMPARE=5` runs the
#      reference arm against itself inside ONE process, so whatever delta it
#      prints is the harness;
#   2. then the A/B. Its two arms are two BINARIES (unmodified HEAD vs the
#      slice tree), which the in-process `bench::compare` cannot express — it
#      alternates ENGINES, not executables. So the ABBA is done here, at the
#      process level, with the same three properties the harness gives:
#      counterbalanced pairs (AB / BA / AB / BA / AB), a discarded warm-up
#      pair, and 5 repeats per arm. Medians, per-arm spreads and the verdict
#      are computed against the WIDEST of (spread A, spread B, host floor) —
#      never the narrowest (§ R2).
#
# Both budgets are run (§ R3: a budget ending inside the boot amortises
# nothing; quote the trend, not one point). Every timed run's own conditions
# (knobs, build stamp, fingerprint) land in abba.raw beside the times.
#
# The fingerprint table at the end is not decoration: a delta whose
# fingerprint moved is not a timing claim at all. One value per budget.
#
#   abba.sh                  both budgets (2000 and 6000)
#   abba.sh 2000             one budget
#   abba.sh --verdict FILE   re-print the verdict from an existing times file
#                            (also the self-test: docs/MEASURING.md § R5 —
#                             a guard nobody has watched say both yes and no
#                             is not an instrument)
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NEW="/home/gistarcade/src/pom68k/.claude/worktrees/agent-aa9179cca4f63271d/build/jit_bench_lcii"
NEW_DIR="/home/gistarcade/src/pom68k/.claude/worktrees/agent-aa9179cca4f63271d"
BASE="$HERE/base-src/build-base/jit_bench_lcii"
BASE_DIR="$HERE/base-src"
OUT="$HERE/abba.times"
RAW="$HERE/abba.raw"

REPEATS=5                       # pairs, after the discarded warm-up pair
FLOOR_PERMILLE=10               # performance_budgets.tsv:67 (x86_64)

# ── the verdict, computed from the times table and nothing else ──────────
verdict() {
    awk -v floor="$FLOOR_PERMILLE" -F'\t' '
    NF == 5 && $3 != "warmup" {
        k = $1 "|" $2
        n[k]++
        v[k, n[k]] = $4
        if (index(fp[k], " " $5) == 0) fp[k] = fp[k] " " $5
    }
    END {
        print ""
        print "=== medians (warm-up pair excluded) ==="
        for (k in n) {
            c = n[k]
            for (i = 1; i <= c; i++)
                for (j = i + 1; j <= c; j++)
                    if (v[k, j] + 0 < v[k, i] + 0) {
                        t = v[k, i]; v[k, i] = v[k, j]; v[k, j] = t
                    }
            med[k] = (c % 2) ? v[k, int(c / 2) + 1] + 0 \
                             : (v[k, c / 2] + v[k, c / 2 + 1]) / 2.0
            lo[k] = v[k, 1] + 0
            hi[k] = v[k, c] + 0
            spread[k] = med[k] > 0 ? 1000.0 * (hi[k] - lo[k]) / med[k] : 0
            printf "%s  n=%d  median=%.2fs  [%.2f-%.2f]  spread=%.1f permille\n", \
                   k, c, med[k], lo[k], hi[k], spread[k]
        }
        print ""
        print "=== delta (B = slices 1+2, A = unmodified HEAD) ==="
        for (k in n) {
            split(k, p, "|")
            if (p[2] != "A") continue
            kb = p[1] "|B"
            if (!(kb in n)) continue
            d = 1000.0 * (med[kb] - med[k]) / med[k]
            widest = spread[k]
            if (spread[kb] > widest) widest = spread[kb]
            if (floor > widest) widest = floor
            ad = d < 0 ? -d : d
            printf "frames=%s  A=%.2fs  B=%.2fs  delta=%+.1f permille (%+.2f %%)  " \
                   "widest noise=%.1f permille  ->  %s\n", \
                   p[1], med[k], med[kb], d, d / 10.0, widest, \
                   (ad > widest ? "CLAIM" : "NOT A CLAIM")
        }
        print ""
        print "=== fingerprints (must be ONE value per budget, both arms) ==="
        for (k in fp) {
            nfp = split(fp[k], q, " ")
            printf "%s:%s%s\n", k, fp[k], \
                   (nfp > 1 ? "   <-- MOVED, NOT A TIMING CLAIM" : "")
        }
    }' "$1"
}

if [ $# -ge 1 ] && [ "$1" = "--verdict" ]; then
    verdict "$2"
    exit 0
fi

for b in "$NEW" "$BASE"; do
    [ -x "$b" ] || { echo "missing binary: $b" >&2; exit 2; }
done

# The x64 68030 generator is the engine under test: the VRAM DTLB refusals
# (slice 1) and the duplicated word decode (slice 2) are both on its path.
export POM68K_CPU_ENGINE=jit
export POM68K_JIT_BACKEND=x64
export POM68K_JIT_BLOCKS=1
export POM68K_JIT_HOT=1

BUDGETS=("$@")
[ ${#BUDGETS[@]} -eq 0 ] && BUDGETS=(2000 6000)

threads=$(nproc)
load1=$(awk '{print $1}' /proc/loadavg)
if awk -v l="$load1" -v t="$threads" 'BEGIN{exit !(l > t/4)}'; then
    echo "HOST BUSY: 1-min load $load1 over 1/4 of $threads threads." >&2
    echo "docs/MEASURING.md § 1bis — nothing measured here would be quotable." >&2
    exit 1
fi

: > "$OUT"
: > "$RAW"
{
    echo "# POM68K B.2 slices 1+2 A/B — $(date -Is)"
    echo "# host: $(uname -srm)  threads=$threads  load1(start)=$load1"
    echo "# A = base (unmodified HEAD): $BASE"
    echo "# B = new  (slices 1+2):      $NEW"
    echo "# engine: POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 BLOCKS=1 HOT=1"
} | tee -a "$OUT" >> "$RAW"

# One timed run. $1 arm letter, $2 binary, $3 its source dir (the bench finds
# roms/ and hdv/ relative to the cwd), $4 frames, $5 tag.
run_one() {
    local arm="$1" bin="$2" dir="$3" frames="$4" tag="$5"
    local log wall fp
    log=$(cd "$dir" && POM68K_BENCH_FRAMES="$frames" "$bin" 2>&1)
    printf -- '----- %s arm=%s frames=%s\n%s\n' "$tag" "$arm" "$frames" "$log" >> "$RAW"
    wall=$(printf '%s\n' "$log" | sed -n 's/.*wall=\([0-9.]*\)s .*/\1/p' | head -1)
    fp=$(printf '%s\n' "$log" | sed -n 's/.*fp=\([0-9a-f]*\).*/\1/p' | head -1)
    if [ -z "$wall" ]; then
        echo "NO RESULT ($tag arm=$arm) — see $RAW" | tee -a "$OUT"
        return 1
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$frames" "$arm" "$tag" "$wall" "$fp" >> "$OUT"
    echo "  $tag arm=$arm frames=$frames wall=${wall}s fp=$fp"
}

for frames in "${BUDGETS[@]}"; do
    echo "" | tee -a "$OUT"
    echo "== budget $frames frames ==" | tee -a "$OUT"

    # ── 1. the noise floor, on THIS host, at THIS budget ─────────────────
    echo "-- null experiment (POM68K_BENCH_NULL=1, COMPARE=5) --" | tee -a "$OUT"
    nulllog=$(cd "$NEW_DIR" && POM68K_BENCH_FRAMES="$frames" \
              POM68K_BENCH_NULL=1 POM68K_BENCH_COMPARE=5 "$NEW" 2>&1)
    printf -- '----- NULL frames=%s\n%s\n' "$frames" "$nulllog" >> "$RAW"
    printf '%s\n' "$nulllog" \
        | grep -E 'delta|spread|floor|NOT A CLAIM|POLICY|HOST|fp=' \
        | sed 's/^/  /' | tee -a "$OUT"

    # ── 2. the A/B: warm-up pair discarded, then AB / BA / AB / BA / AB ──
    run_one A "$BASE" "$BASE_DIR" "$frames" warmup > /dev/null
    run_one B "$NEW"  "$NEW_DIR"  "$frames" warmup > /dev/null
    echo "-- ABBA ($REPEATS pairs, warm-up pair discarded) --" | tee -a "$OUT"
    for ((i = 0; i < REPEATS; i++)); do
        if (( i % 2 == 0 )); then
            run_one A "$BASE" "$BASE_DIR" "$frames" "p$i"
            run_one B "$NEW"  "$NEW_DIR"  "$frames" "p$i"
        else
            run_one B "$NEW"  "$NEW_DIR"  "$frames" "p$i"
            run_one A "$BASE" "$BASE_DIR" "$frames" "p$i"
        fi
    done
done

echo "# load1(end)=$(awk '{print $1}' /proc/loadavg)" | tee -a "$OUT"
verdict "$OUT" | tee -a "$OUT"

echo ""
echo "raw runs: $RAW"
echo "table:    $OUT"
