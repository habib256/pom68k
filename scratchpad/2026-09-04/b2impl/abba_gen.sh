#!/usr/bin/env bash
# POM68K B.2 — one ABBA driver for both shapes of arm, and for any engine.
#
# The 2026-09-04 drivers hard-coded `POM68K_JIT_BACKEND=x64`, which is a
# DIAGNOSTIC override on this host: `X64Backend::caps()` declares
# `autoFamilies = kGuest68040` only, so a 68030 guest's automatic backend on
# x86-64 is `threaded` — and `threaded` runs every instruction through
# `pomJitExecOne()` -> `mmuExecuteStart`, which is where the interpreted-path
# slices are worth most. Measuring only the x64 arm prices the arm that does
# not ship. Hence: the engine is an argument.
#
# Protocol otherwise unchanged (docs/MEASURING.md § 1 R1/R2/R3, § 4.1):
# refuse a busy host, NULL experiment first, warm-up pair discarded, ABBA
# pairs, verdict against the WIDEST of (spread A, spread B, recorded floor),
# fingerprints must be one value per budget. The paired-difference verdict is
# printed beside the range verdict: the ABBA runs A and B adjacent in time, so
# the per-pair difference cancels the slow drift that dominates a range taken
# over separate process invocations (R2's own warning about "process start-up,
# page cache and the afternoon"). The range verdict stays the headline.
#
# Usage, two shapes:
#   ENGINE=threaded abba_gen.sh bin  TAG  A_BIN  A_DIR  B_BIN  B_DIR  [frames...]
#   ENGINE=threaded abba_gen.sh knob TAG  VAR VAL_A VAL_B  BIN DIR   [frames...]
#
# ENGINE is one of: interp | threaded | x64 | a64 (default threaded).
# REPEATS overrides the pair count (default 5).
set -u

SHAPE="$1"; TAG="$2"; shift 2
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE="${ENGINE:-threaded}"
REPEATS="${REPEATS:-5}"
FLOOR_PERMILLE="${FLOOR_PERMILLE:-10}"   # performance_budgets.tsv:67 (x86_64)

case "$SHAPE" in
  bin)  A_BIN="$1"; A_DIR="$2"; B_BIN="$3"; B_DIR="$4"; shift 4 ;;
  knob) VAR="$1"; VAL_A="$2"; VAL_B="$3"; A_BIN="$4"; A_DIR="$5"
        B_BIN="$A_BIN"; B_DIR="$A_DIR"; shift 5 ;;
  *) echo "shape must be bin or knob" >&2; exit 2 ;;
esac

OUT="$HERE/$TAG.times"; RAW="$HERE/$TAG.raw"

verdict() {
    awk -v floor="$FLOOR_PERMILLE" -F'\t' '
    NF == 5 && $3 != "warmup" {
        k = $1 "|" $2
        n[k]++; v[k, n[k]] = $4
        if (index(fp[k], " " $5) == 0) fp[k] = fp[k] " " $5
        if ($2 == "A") pa[$1 "|" $3] = $4; else pb[$1 "|" $3] = $4
    }
    END {
        print ""
        print "=== medians (warm-up pair excluded) ==="
        for (k in n) {
            c = n[k]
            for (i = 1; i <= c; i++) for (j = i + 1; j <= c; j++)
                if (v[k,j] + 0 < v[k,i] + 0) { t = v[k,i]; v[k,i] = v[k,j]; v[k,j] = t }
            med[k] = (c % 2) ? v[k, int(c/2)+1] + 0 : (v[k, c/2] + v[k, c/2+1]) / 2.0
            lo[k] = v[k,1] + 0; hi[k] = v[k,c] + 0
            spread[k] = med[k] > 0 ? 1000.0 * (hi[k] - lo[k]) / med[k] : 0
            printf "%s  n=%d  median=%.2fs  [%.2f-%.2f]  spread=%.1f permille\n", \
                   k, c, med[k], lo[k], hi[k], spread[k]
        }
        print ""
        print "=== delta (B relative to A) ==="
        for (k in n) {
            split(k, p, "|"); if (p[2] != "A") continue
            kb = p[1] "|B"; if (!(kb in n)) continue
            d = 1000.0 * (med[kb] - med[k]) / med[k]
            widest = spread[k]
            if (spread[kb] > widest) widest = spread[kb]
            if (floor > widest) widest = floor
            ad = d < 0 ? -d : d
            printf "frames=%s  A=%.2fs  B=%.2fs  delta=%+.1f permille (%+.2f %%)  " \
                   "widest noise=%.1f permille  ->  %s\n", \
                   p[1], med[k], med[kb], d, d/10.0, widest, \
                   (ad > widest ? "CLAIM" : "NOT A CLAIM")
        }
        print ""
        print "=== paired differences (B-A within each ABBA pair; drift cancels) ==="
        for (key in pa) {
            if (!(key in pb)) continue
            split(key, q, "|")
            dd = (pb[key] - pa[key]) / pa[key] * 1000.0
            pn[q[1]]++; pv[q[1], pn[q[1]]] = dd
        }
        for (b in pn) {
            c = pn[b]; s = 0
            for (i = 1; i <= c; i++) s += pv[b,i]
            m = s / c; ss = 0
            for (i = 1; i <= c; i++) ss += (pv[b,i] - m) * (pv[b,i] - m)
            sd = c > 1 ? sqrt(ss / (c - 1)) : 0
            se = c > 1 ? sd / sqrt(c) : 0
            t = (c == 2) ? 12.71 : (c == 3) ? 4.30 : (c == 4) ? 3.18 : \
                (c == 5) ? 2.78 : (c == 6) ? 2.57 : (c == 7) ? 2.45 : \
                (c == 8) ? 2.36 : (c == 9) ? 2.31 : (c == 10) ? 2.26 : 2.14
            printf "frames=%s  n=%d  mean paired delta=%+.1f permille  sd=%.1f  " \
                   "se=%.1f  95%%CI=[%+.1f, %+.1f] permille  ->  %s\n", \
                   b, c, m, sd, se, m - t * se, m + t * se, \
                   ((m - t*se) * (m + t*se) > 0 ? "sign resolved" : "sign UNRESOLVED")
        }
        print ""
        print "=== fingerprints (must be ONE value per budget, both arms) ==="
        for (k in fp) {
            nfp = split(fp[k], q, " ")
            printf "%s:%s%s\n", k, fp[k], (nfp > 1 ? "   <-- MOVED, NOT A TIMING CLAIM" : "")
        }
    }' "$1"
}

if [ "$SHAPE" = "verdict" ]; then verdict "$TAG"; exit 0; fi

for b in "$A_BIN" "$B_BIN"; do
    [ -x "$b" ] || { echo "missing binary: $b" >&2; exit 2; }
done

case "$ENGINE" in
  interp)   ENGENV=(POM68K_CPU_ENGINE=interp) ;;
  threaded) ENGENV=(POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded
                    POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1) ;;
  *)        ENGENV=(POM68K_CPU_ENGINE=jit "POM68K_JIT_BACKEND=$ENGINE"
                    POM68K_JIT_BLOCKS=1 POM68K_JIT_HOT=1) ;;
esac

BUDGETS=("$@"); [ ${#BUDGETS[@]} -eq 0 ] && BUDGETS=(2000 6000)

threads=$(nproc); load1=$(awk '{print $1}' /proc/loadavg)
if awk -v l="$load1" -v t="$threads" 'BEGIN{exit !(l > t/4)}'; then
    echo "HOST BUSY: 1-min load $load1 over 1/4 of $threads threads." >&2
    echo "docs/MEASURING.md § 1bis — nothing measured here would be quotable." >&2
    exit 1
fi

: > "$OUT"; : > "$RAW"
{
    echo "# POM68K B.2 — $TAG — $(date -Is)"
    echo "# host: $(uname -srm)  threads=$threads  load1(start)=$load1"
    echo "# engine: ${ENGENV[*]}"
    if [ "$SHAPE" = "bin" ]; then
        echo "# A binary: $A_BIN"
        echo "# B binary: $B_BIN"
    else
        echo "# binary: $A_BIN"
        echo "# A: $VAR=$VAL_A     B: $VAR=$VAL_B"
    fi
} | tee -a "$OUT" >> "$RAW"

run_one() {
    local arm="$1" frames="$2" tag="$3" bin dir extra log wall fp
    if [ "$arm" = A ]; then bin="$A_BIN"; dir="$A_DIR"; else bin="$B_BIN"; dir="$B_DIR"; fi
    extra=()
    [ "$SHAPE" = knob ] && { [ "$arm" = A ] && extra=("$VAR=$VAL_A") || extra=("$VAR=$VAL_B"); }
    log=$(cd "$dir" && env "${ENGENV[@]}" "${extra[@]+"${extra[@]}"}" \
          POM68K_BENCH_FRAMES="$frames" "$bin" 2>&1)
    printf -- '----- %s arm=%s frames=%s %s\n%s\n' "$tag" "$arm" "$frames" \
             "${extra[*]-}" "$log" >> "$RAW"
    wall=$(printf '%s\n' "$log" | sed -n 's/.*wall=\([0-9.]*\)s .*/\1/p' | head -1)
    fp=$(printf '%s\n' "$log" | sed -n 's/.*fp=\([0-9a-f]*\).*/\1/p' | head -1)
    [ -n "$wall" ] || { echo "NO RESULT ($tag arm=$arm) — see $RAW" | tee -a "$OUT"; return 1; }
    printf '%s\t%s\t%s\t%s\t%s\n' "$frames" "$arm" "$tag" "$wall" "$fp" >> "$OUT"
    echo "  $tag arm=$arm frames=$frames wall=${wall}s fp=$fp"
}

for frames in "${BUDGETS[@]}"; do
    echo "" | tee -a "$OUT"
    echo "== budget $frames frames ==" | tee -a "$OUT"
    echo "-- null experiment (POM68K_BENCH_NULL=1, COMPARE=5) --" | tee -a "$OUT"
    nulllog=$(cd "$A_DIR" && env "${ENGENV[@]}" POM68K_BENCH_FRAMES="$frames" \
              POM68K_BENCH_NULL=1 POM68K_BENCH_COMPARE=5 "$A_BIN" 2>&1)
    printf -- '----- NULL frames=%s\n%s\n' "$frames" "$nulllog" >> "$RAW"
    printf '%s\n' "$nulllog" | grep -E 'delta|spread|floor|NOT A CLAIM|POLICY|HOST|fp=' \
        | sed 's/^/  /' | tee -a "$OUT"

    run_one A "$frames" warmup > /dev/null
    run_one B "$frames" warmup > /dev/null
    echo "-- ABBA ($REPEATS pairs, warm-up pair discarded) --" | tee -a "$OUT"
    for ((i = 0; i < REPEATS; i++)); do
        if (( i % 2 == 0 )); then
            run_one A "$frames" "p$i"; run_one B "$frames" "p$i"
        else
            run_one B "$frames" "p$i"; run_one A "$frames" "p$i"
        fi
    done
done

echo "# load1(end)=$(awk '{print $1}' /proc/loadavg)" | tee -a "$OUT"
verdict "$OUT" | tee -a "$OUT"
echo ""; echo "raw runs: $RAW"; echo "table:    $OUT"
