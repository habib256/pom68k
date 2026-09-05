#!/bin/bash
# B.2 slice 5 — knob-off identity and knob-on liveness table.
# NO timing is collected or quoted here: only fingerprints, the 030 icache
# triple and the data-window counters. The A/B belongs to the orchestrator.
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915
OUT="$W/scratchpad/2026-09-04/b2s5"
LOGS="$OUT/logs"
BASE=/home/gistarcade/src/pom68k/build/jit_bench_lcii     # unmodified HEAD 346f084
MINE="$W/build/jit_bench_lcii"                            # slice 5

export POM68K_BENCH_ROM="/home/gistarcade/src/pom68k/roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM"
export POM68K_BENCH_DISK=/home/gistarcade/src/pom68k/hdv/boot.vhd

run() {                     # run <label> <binary> <frames> <extra env...>
    local label=$1 bin=$2 frames=$3; shift 3
    local log="$LOGS/${label}.log"
    env POM68K_BENCH_FRAMES="$frames" "$@" "$bin" >"$log" 2>&1
    local fp ic dw
    fp=$(grep -o 'fp=[0-9a-f]*' "$log" | head -1)
    ic=$(grep -o 'icache: .*' "$log" | head -1)
    dw=$(grep -o 'data window: .*' "$log" | head -1)
    printf '%-34s %s | %s | %s\n' "$label" "$fp" "$ic" "$dw"
}

for frames in 2000 6000; do
    echo "=== $frames frames ==="
    for eng in "interp" "threaded" "x64"; do
        case $eng in
          interp)   e=(POM68K_CPU_ENGINE=interp) ;;
          threaded) e=(POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded) ;;
          x64)      e=(POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64) ;;
        esac
        run "head-${eng}-${frames}"     "$BASE" "$frames" "${e[@]}"
        run "off-${eng}-${frames}"      "$MINE" "$frames" "${e[@]}" POM68K_DATA_WINDOW=0
        run "on-${eng}-${frames}"       "$MINE" "$frames" "${e[@]}" POM68K_DATA_WINDOW=1
    done
done
