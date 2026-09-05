#!/usr/bin/env bash
# POM68K TODO § B.2 slice 3 / § B.3 clause 1 — the A/B that the plan calls
# "easy to do right": ONE binary, two values of an explicit-wins env knob
# (JitEngine.cpp:99), so nothing but the knob differs between the arms.
#
# Protocol is abba.sh's, unchanged (docs/MEASURING.md § 1 R1/R2/R3, § 4.1):
# refuse a busy host, NULL experiment first, warm-up pair discarded, ABBA
# pairs, verdict against the WIDEST of (spread A, spread B, recorded floor).
#
# Added here, and only here: a PAIRED verdict beside the range verdict. The
# ABBA already runs A and B adjacent in time, so the per-pair difference
# cancels the slow drift that dominates a range taken over separate process
# invocations (§ R2's own warning about "process start-up, page cache and the
# afternoon"). The range verdict stays the headline; the paired one says
# whether a refusal is "no effect" or "not resolvable here".
#
#   abba_knob.sh VAR VAL_A VAL_B BIN DIR [frames...]
set -u

VAR="$1"; VAL_A="$2"; VAL_B="$3"; BIN="$4"; DIR="$5"; shift 5
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAGNAME="$(echo "${VAR}_${VAL_A}v${VAL_B}" | tr 'A-Z' 'a-z')"
OUT="$HERE/$TAGNAME.times"
RAW="$HERE/$TAGNAME.raw"
REPEATS=${REPEATS:-5}
FLOOR_PERMILLE=10               # performance_budgets.tsv (x86_64)

verdict() {
    awk -v floor="$FLOOR_PERMILLE" -v va="$VAL_A" -v vb="$VAL_B" -F'\t' '
    NF == 5 && $3 != "warmup" {
        k = $1 "|" $2
        n[k]++; v[k, n[k]] = $4
        if (index(fp[k], " " $5) == 0) fp[k] = fp[k] " " $5
        if ($2 == "A") { pa[$1 "|" $3] = $4 } else { pb[$1 "|" $3] = $4 }
    }
    END {
        print ""
        print "=== medians (warm-up pair excluded) ==="
        for (k in n) {
            c = n[k]
            for (i = 1; i <= c; i++) for (j = i + 1; j <= c; j++)
                if (v[k, j] + 0 < v[k, i] + 0) { t = v[k,i]; v[k,i] = v[k,j]; v[k,j] = t }
            med[k] = (c % 2) ? v[k, int(c/2)+1] + 0 : (v[k, c/2] + v[k, c/2+1]) / 2.0
            lo[k] = v[k,1] + 0; hi[k] = v[k,c] + 0
            spread[k] = med[k] > 0 ? 1000.0 * (hi[k] - lo[k]) / med[k] : 0
            printf "%s  n=%d  median=%.2fs  [%.2f-%.2f]  spread=%.1f permille\n", \
                   k, c, med[k], lo[k], hi[k], spread[k]
        }
        print ""
        printf "=== delta (A = %s=%s, B = %s=%s) ===\n", "knob", va, "knob", vb
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
            printf "frames=%s  n=%d  mean paired delta=%+.1f permille  sd=%.1f  " \
                   "se=%.1f  95%%CI=[%+.1f, %+.1f] permille  ->  %s\n", \
                   b, c, m, sd, se, m - 2.78 * se, m + 2.78 * se, \
                   ((m - 2.78*se) * (m + 2.78*se) > 0 ? "sign resolved" : "sign UNRESOLVED")
        }
        print ""
        print "=== fingerprints (must be ONE value per budget, both arms) ==="
        for (k in fp) {
            nfp = split(fp[k], q, " ")
            printf "%s:%s%s\n", k, fp[k], (nfp > 1 ? "   <-- MOVED, NOT A TIMING CLAIM" : "")
        }
    }' "$1"
}

[ -x "$BIN" ] || { echo "missing binary: $BIN" >&2; exit 2; }

export POM68K_CPU_ENGINE=jit
export POM68K_JIT_BACKEND=x64
export POM68K_JIT_BLOCKS=1
export POM68K_JIT_HOT=1

BUDGETS=("$@")
[ ${#BUDGETS[@]} -eq 0 ] && BUDGETS=(2000 6000)

threads=$(nproc); load1=$(awk '{print $1}' /proc/loadavg)
if awk -v l="$load1" -v t="$threads" 'BEGIN{exit !(l > t/4)}'; then
    echo "HOST BUSY: 1-min load $load1 over 1/4 of $threads threads." >&2
    exit 1
fi

: > "$OUT"; : > "$RAW"
{
    echo "# POM68K B.2 slice 3 A/B — $(date -Is)"
    echo "# host: $(uname -srm)  threads=$threads  load1(start)=$load1"
    echo "# binary: $BIN  ($(git -C "$DIR" rev-parse --short HEAD 2>/dev/null))"
    echo "# A: $VAR=$VAL_A     B: $VAR=$VAL_B"
    echo "# engine: POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64 BLOCKS=1 HOT=1"
} | tee -a "$OUT" >> "$RAW"

run_one() {
    local arm="$1" val="$2" frames="$3" tag="$4" log wall fp
    log=$(cd "$DIR" && env "$VAR=$val" POM68K_BENCH_FRAMES="$frames" "$BIN" 2>&1)
    printf -- '----- %s arm=%s %s=%s frames=%s\n%s\n' "$tag" "$arm" "$VAR" "$val" "$frames" "$log" >> "$RAW"
    wall=$(printf '%s\n' "$log" | sed -n 's/.*wall=\([0-9.]*\)s .*/\1/p' | head -1)
    fp=$(printf '%s\n' "$log" | sed -n 's/.*fp=\([0-9a-f]*\).*/\1/p' | head -1)
    [ -n "$wall" ] || { echo "NO RESULT ($tag arm=$arm) — see $RAW" | tee -a "$OUT"; return 1; }
    printf '%s\t%s\t%s\t%s\t%s\n' "$frames" "$arm" "$tag" "$wall" "$fp" >> "$OUT"
    echo "  $tag arm=$arm $VAR=$val frames=$frames wall=${wall}s fp=$fp"
}

for frames in "${BUDGETS[@]}"; do
    echo "" | tee -a "$OUT"
    echo "== budget $frames frames ==" | tee -a "$OUT"
    echo "-- null experiment (POM68K_BENCH_NULL=1, COMPARE=5) --" | tee -a "$OUT"
    nulllog=$(cd "$DIR" && POM68K_BENCH_FRAMES="$frames" POM68K_BENCH_NULL=1 \
              POM68K_BENCH_COMPARE=5 "$BIN" 2>&1)
    printf -- '----- NULL frames=%s\n%s\n' "$frames" "$nulllog" >> "$RAW"
    printf '%s\n' "$nulllog" | grep -E 'delta|spread|floor|NOT A CLAIM|POLICY|HOST|fp=' \
        | sed 's/^/  /' | tee -a "$OUT"

    run_one A "$VAL_A" "$frames" warmup > /dev/null
    run_one B "$VAL_B" "$frames" warmup > /dev/null
    echo "-- ABBA ($REPEATS pairs, warm-up pair discarded) --" | tee -a "$OUT"
    for ((i = 0; i < REPEATS; i++)); do
        if (( i % 2 == 0 )); then
            run_one A "$VAL_A" "$frames" "p$i"; run_one B "$VAL_B" "$frames" "p$i"
        else
            run_one B "$VAL_B" "$frames" "p$i"; run_one A "$VAL_A" "$frames" "p$i"
        fi
    done
done

echo "# load1(end)=$(awk '{print $1}' /proc/loadavg)" | tee -a "$OUT"
verdict "$OUT" | tee -a "$OUT"
echo ""; echo "raw runs: $RAW"; echo "table:    $OUT"
