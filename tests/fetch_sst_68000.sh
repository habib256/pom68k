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
    # The listing and the payload come from two different hosts, and only the
    # payload host is reachable everywhere: a sandbox or CI proxy that serves
    # raw.githubusercontent.com fine will answer api.github.com with 403, and
    # GitHub rate-limits unauthenticated API calls besides. Under `set -o
    # pipefail` that made the whole script exit 1 with NO output at all —
    # empty out-dir, and the gate downstream soft-skipping as if the vectors
    # had simply not been asked for. Fail loudly, and say what to do instead.
    if ! api=$(curl -sf "$API"); then
        echo "error: cannot list $API (403/rate-limit/proxy?)." >&2
        echo "       The payload host may still be reachable — get the names" >&2
        echo "       from git and pass them explicitly:" >&2
        echo "         git clone --filter=blob:none --no-checkout --depth 1 \\" >&2
        echo "             https://github.com/SingleStepTests/680x0.git sst" >&2
        echo "         git -C sst ls-tree --name-only HEAD 68000/v1/ \\" >&2
        echo "             | sed 's|68000/v1/||; s|\\.json\\.gz\$||' > names.txt" >&2
        echo "         $0 $OUT \$(tr '\\n' ' ' < names.txt)" >&2
        exit 1
    fi
    FILES=$(printf '%s' "$api" | grep -o '"name": *"[^"]*\.json\.gz"' | cut -d'"' -f4)
    if [[ -z "$FILES" ]]; then
        echo "error: $API answered, but no .json.gz names in it — format change?" >&2
        exit 1
    fi
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
