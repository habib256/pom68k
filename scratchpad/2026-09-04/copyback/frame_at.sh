#!/bin/bash
# Print main()'s stack-frame probe size for tests/jit_copyback_write_040_test.cpp
# compiled from an already-extracted source tree ($1 = tree dir, $2 = label).
set -e
D="$1"; REV="$2"
OUT="$D.o"
/usr/bin/c++ -DPOM68K_JIT_BACKEND_X64 \
  -DPOM68K_PERF_COPYBACK_MAX_RATIO_PERMILLE=2000 \
  -DPOM68K_PERF_COPYBACK_MAX_SLOW_INSTRUCTIONS=4 \
  -DPOM68K_PERF_COPYBACK_SLACK_US=2000 \
  -I"$D/tests" -I"$D/src" -I"$D/extern/softfloat" \
  -isystem "$D/extern/moira/Moira" \
  -O2 -g -DNDEBUG -std=gnu++20 -march=native \
  -c "$D/tests/jit_copyback_write_040_test.cpp" -o "$OUT" 2>"$D.err" || {
    echo "$REV COMPILE_FAIL"; tail -3 "$D.err"; exit 0; }
FR=$(objdump -d "$OUT" | awk '/^0+ <main>:/{f=1} f && /lea .*\(%rsp\),%r11/{print $NF; exit}')
echo "$REV main-probe=$FR"
