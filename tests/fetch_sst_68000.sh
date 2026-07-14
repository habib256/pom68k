#!/usr/bin/env bash
# POM68K — fetch SingleStepTests/680x0 68000 vectors for the sst68000 gate.
# 125 files, one per instruction/size (~193 MB .gz, ~1 GB unpacked), 8 065
# vectors each: initial/final CPU state + RAM + prefetch queue + cycle count.
# Default pulls everything; pass instruction names to pull a subset.
#
#   tests/fetch_sst_68000.sh <out-dir> [INSTR ...]     e.g. ABCD ADD.b MOVE.w
#
# Source: https://github.com/SingleStepTests/680x0  (main, 68000/v1)
# This is also the exchange format planned for the 68030 oracle phase.
set -euo pipefail
OUT="${1:?usage: fetch_sst_68000.sh <out-dir> [INSTR ...]}"
BASE="https://raw.githubusercontent.com/SingleStepTests/680x0/main/68000/v1"
API="https://api.github.com/repos/SingleStepTests/680x0/contents/68000/v1"
mkdir -p "$OUT"

if [[ $# -gt 1 ]]; then
    FILES=$(printf '%s.json.gz\n' "${@:2}")
else
    FILES=$(curl -sf "$API" | grep -o '"name": *"[^"]*\.json\.gz"' | cut -d'"' -f4)
fi

n=0
for f in $FILES; do
    j="${f%.gz}"
    [[ -s "$OUT/$j" ]] && { n=$((n+1)); continue; }
    if curl -sfL "$BASE/$f" -o "$OUT/$f"; then
        gunzip -f "$OUT/$f" && n=$((n+1))
    else
        echo "warn: could not fetch $f" >&2
    fi
done
echo "present: $n .json files in $OUT"
