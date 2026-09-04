#!/usr/bin/env bash
# POM68K — SCC deadline hand-off A/B (TODO.md § B.1, second bullet).
#
# Protocol: CHANGELOG.md 2026-09-03 (sixteenth), reproduced exactly —
# two binary-alternated A/B pairs, the second pair's order reversed:
#
#     pair 1   old new new old
#     pair 2   new old old new
#
# so each arm runs four times and each one lands twice early and twice late.
# One discarded warm-up run per arm precedes them (docs/MEASURING.md § R1:
# the first run of anything pays the cold page cache for the ROM and the
# 300 MB boot image; charging that to whichever arm went first is a fake
# effect). --no-warmup drops it.
#
# The workload is the fixed-frame Q605 stopwatch `jit_bench`
# (POM68K_BENCH_FRAMES): it is the instrument that prints the retired
# machine/core cycles and the architectural fingerprint the (sixteenth)
# entry quotes, so an arm whose fingerprint or cycle counts moved is not a
# timing arm at all (docs/MEASURING.md, preamble).
#
#   ./abba.sh              6 000 frames (the entry's budget)
#   ./abba.sh --short      3 000 frames (the entry's confirming budget)
#   ./abba.sh --frames N   any budget
#   ./abba.sh --no-warmup  exactly eight recorded runs, nothing discarded
#
# Run it on a QUIET host: no build, no ctest, no other agent's tier.
set -u

WT=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a5a3ffaf74087926b
OUT=/tmp/claude-1000/-home-gistarcade-src-pom68k/742ac6a0-9a99-4ebe-8701-713deafa5293/scratchpad/scc
OLD="$WT/build-base/jit_bench"     # arm A — unmodified HEAD (8f74d42)
NEW="$WT/build-new/jit_bench"      # arm B — SCC deadline hand-off
FRAMES=6000
WARMUP=1

# `bench::findAsset` (tests/BenchHarness.h:51) opens the spelling it is given
# and does NOT route hdv/X to hdv/ref/X the way testasset::find does, and this
# worktree only carries the immutable reference. Name it explicitly. jit_bench
# attaches with writeBack=false (tests/jit_bench.cpp:96, ScsiDisk::open), so
# the reference image is read into memory and never opened for writing.
export POM68K_BENCH_DISK="hdv/ref/MacOS-8.1-boot.vhd"
export POM68K_BENCH_ROM="roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM"

while [ $# -gt 0 ]; do
    case "$1" in
        --short)     FRAMES=3000 ;;
        --frames)    shift; FRAMES="$1" ;;
        --no-warmup) WARMUP=0 ;;
        *) echo "usage: $0 [--short] [--frames N] [--no-warmup]" >&2; exit 2 ;;
    esac
    shift
done

for b in "$OLD" "$NEW"; do
    [ -x "$b" ] || { echo "missing arm binary: $b" >&2; exit 1; }
done

TIMES="$OUT/abba.times"
: > "$TIMES"

stamp() { date '+%H:%M:%S'; }

say() { echo "$*" | tee -a "$TIMES"; }

say "=== SCC deadline hand-off ABBA — $(date '+%Y-%m-%d %H:%M:%S') ==="
say "host: $(uname -sm)  load: $(cut -d' ' -f1-3 /proc/loadavg)"
say "arms: A=$OLD"
say "      B=$NEW"
say "workload: jit_bench, POM68K_BENCH_FRAMES=$FRAMES, cwd=$WT"
say "rom:  $POM68K_BENCH_ROM"
say "disk: $POM68K_BENCH_DISK"
say ""

# One run. $1 = arm label, $2 = binary. Prints the harness's own timed-loop
# wall clock (it excludes ROM load and the boot to the first frame), the
# retired machine/core cycles and the fingerprint, plus the process's real
# time for context.
run_arm() {
    local label="$1" bin="$2" tag="$3"
    local t0 t1 line real
    t0=$(date +%s.%N)
    line=$(cd "$WT" && POM68K_BENCH_FRAMES="$FRAMES" "$bin" 2>/dev/null \
           | grep -m1 'fp=')
    t1=$(date +%s.%N)
    real=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
    if [ -z "$line" ]; then
        say "$(stamp) $label $tag  FAILED (no output — assets missing?)"
        return 1
    fi
    # jit_bench prints one line:
    #   q605 engine=jit  cycles=N machine (M core, boost ×4.0)  wall=W.WWs
    #   for G.GGs of guest time  ×R real time  C core MHz  fp=HHHHHHHHHHHHHHHH
    local wall mach core fp
    wall=$(sed -n 's/.*wall=\([0-9.]*\)s.*/\1/p' <<<"$line")
    mach=$(sed -n 's/.*cycles=\([0-9]*\) machine.*/\1/p' <<<"$line")
    core=$(sed -n 's/.*machine (\([0-9]*\) core.*/\1/p' <<<"$line")
    fp=$(sed -n 's/.*fp=\([0-9a-f]*\).*/\1/p' <<<"$line")
    say "$(stamp) $label $tag  wall=${wall}s  real=${real}s  machine=${mach}  core=${core}  fp=${fp}"
    echo "$label ${wall}" >> "$OUT/.abba.raw"
    echo "$label ${mach} ${core} ${fp}" >> "$OUT/.abba.ident"
}

: > "$OUT/.abba.raw"
: > "$OUT/.abba.ident"

if [ "$WARMUP" = 1 ]; then
    say "--- warm-up pair (printed, NOT counted) ---"
    run_arm old "$OLD" warmup || exit 1
    run_arm new "$NEW" warmup || exit 1
    : > "$OUT/.abba.raw"
    : > "$OUT/.abba.ident"
    say ""
fi

say "--- pair 1: old new new old ---"
run_arm old "$OLD" p1.1 || exit 1
run_arm new "$NEW" p1.2 || exit 1
run_arm new "$NEW" p1.3 || exit 1
run_arm old "$OLD" p1.4 || exit 1
say ""
say "--- pair 2 (order reversed): new old old new ---"
run_arm new "$NEW" p2.1 || exit 1
run_arm old "$OLD" p2.2 || exit 1
run_arm old "$OLD" p2.3 || exit 1
run_arm new "$NEW" p2.4 || exit 1
say ""

# The precondition of the whole comparison: every run of both arms retired
# the same machine and core cycles and ended at the same fingerprint. One
# distinct "machine core fp" triple across all eight runs, or there is no
# timing claim to make (docs/MEASURING.md, preamble).
say "--- identity precondition (must be ONE distinct triple) ---"
sort -u "$OUT/.abba.ident" | while read -r l; do say "  $l"; done
if [ "$(cut -d' ' -f2- "$OUT/.abba.ident" | sort -u | wc -l)" != 1 ]; then
    say "  WARNING: the arms did not retire identical cycles / fingerprint —"
    say "  the timing comparison below is NOT a claim (docs/MEASURING.md)."
fi
say ""

say "--- summary ---"
awk '{ s[$1]+=$2; n[$1]++;
       if (!($1 in mn) || $2<mn[$1]) mn[$1]=$2;
       if (!($1 in mx) || $2>mx[$1]) mx[$1]=$2;
       v[$1]=v[$1] $2 "/" }
     END { for (a in s)
             printf "%-4s n=%d  runs=%s  min=%.2f  max=%.2f  mean=%.3f s\n",
                    a, n[a], substr(v[a],1,length(v[a])-1), mn[a], mx[a], s[a]/n[a];
           if ("old" in s && "new" in s)
             printf "delta = %+.2f %% (new against old); ranges %s\n",
                    (s["new"]/n["new"] - s["old"]/n["old"]) / (s["old"]/n["old"]) * 100,
                    (mx["new"] < mn["old"] || mx["old"] < mn["new"]) \
                        ? "do NOT overlap" : "OVERLAP — under the noise, no claim";
         }' "$OUT/.abba.raw" | tee -a "$TIMES"

say ""
say "noise floor: performance_budgets.tsv, host_wallclock/any/<host>/noise_floor_permille"
say "=== end $(date '+%H:%M:%S') ==="
echo
echo "written: $TIMES"
