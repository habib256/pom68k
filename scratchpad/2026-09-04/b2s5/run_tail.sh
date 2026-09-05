#!/bin/bash
# Wait for the in-flight full build, rebuild once so every object is compiled
# from the FINAL tree (a late comment-only edit to Moira.h landed mid-build),
# then run the asset-free tier twice: knob off, knob on. Sequential by
# construction — never two ctests in one build dir.
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-af30bba2444f67915
LOGS="$W/scratchpad/2026-09-04/b2s5/logs"


systemd-run --user --scope -p MemoryMax=11G --quiet make -C "$W/build" -j3 \
    > "$LOGS/build_final.log" 2>&1
echo "final rebuild rc=$? — $(tail -1 "$LOGS/build_final.log")"

echo "=== ctest -L asset-none, POM68K_DATA_WINDOW=0 ==="
POM68K_DATA_WINDOW=0 ctest --test-dir "$W/build" -L asset-none --output-on-failure \
    > "$LOGS/ctest-asset-none-off.log" 2>&1
echo "rc=$?"; tail -4 "$LOGS/ctest-asset-none-off.log"

echo "=== ctest -L asset-none, POM68K_DATA_WINDOW=1 ==="
POM68K_DATA_WINDOW=1 ctest --test-dir "$W/build" -L asset-none --output-on-failure \
    > "$LOGS/ctest-asset-none-on.log" 2>&1
echo "rc=$?"; tail -4 "$LOGS/ctest-asset-none-on.log"

echo "=== post-rebuild identity re-check (150 frames, all three engines) ==="
export POM68K_BENCH_ROM="/home/gistarcade/src/pom68k/roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM"
export POM68K_BENCH_DISK=/home/gistarcade/src/pom68k/hdv/boot.vhd
cd "$W" || exit 1
for eng in interp threaded x64; do
  case $eng in
    interp)   e=(POM68K_CPU_ENGINE=interp) ;;
    threaded) e=(POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=threaded) ;;
    x64)      e=(POM68K_CPU_ENGINE=jit POM68K_JIT_BACKEND=x64) ;;
  esac
  for k in 0 1; do
    out=$(env POM68K_BENCH_FRAMES=150 "${e[@]}" POM68K_DATA_WINDOW=$k \
              "$W/build/jit_bench_lcii" 2>&1)
    printf '%-10s knob=%s  %s | %s | %s\n' "$eng" "$k" \
      "$(grep -o 'fp=[0-9a-f]*' <<<"$out" | head -1)" \
      "$(grep -o 'icache: .*' <<<"$out" | head -1)" \
      "$(grep -o 'data window: .*' <<<"$out" | head -1)"
  done
  out=$(env POM68K_BENCH_FRAMES=150 "${e[@]}" \
            /home/gistarcade/src/pom68k/build/jit_bench_lcii 2>&1)
  printf '%-10s HEAD    %s | %s |\n' "$eng" \
    "$(grep -o 'fp=[0-9a-f]*' <<<"$out" | head -1)" \
    "$(grep -o 'icache: .*' <<<"$out" | head -1)"
done
