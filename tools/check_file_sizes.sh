#!/usr/bin/env bash
# POM68K — file-size ratchet.
#
# CLAUDE.md's first convention is "one concern per file". It had no mechanism,
# and `src/main.cpp` is what a convention without a mechanism is worth: twelve
# `runXxx()` GUI runners, ~350 lines each, ~85 % identical text, grown one
# platform at a time until the file reached 5990 lines. Nothing objected,
# because nothing was watching. This script watches.
#
# It is a ratchet, not a limit. Every first-party file at or above WATCH lines
# is listed in tools/file_size_budget.txt with the size it had when recorded.
# That number may go DOWN freely — record the win; it may not go up. A file
# not in the list that crosses LIMIT is a new god-object and also fails.
#
# So the convention becomes reviewable: growing main.cpp now means editing the
# budget in the same commit, which is exactly the moment to ask whether the
# new code belongs in a new translation unit instead.
#
#   tools/check_file_sizes.sh            # check (gate mode: non-zero on fail)
#   tools/check_file_sizes.sh --update   # re-record every ceiling from disk
#
# Vendored third-party sources are exempt — their size is not a signal about
# this tree's architecture. `extern/` is not scanned at all: Moira is a fork
# whose provenance is tracked in extern/moira/POM68K_VENDOR.md, not here.

set -uo pipefail

cd "$(dirname "$0")/.."

BUDGET="tools/file_size_budget.txt"
WATCH=1000          # files at or above this must carry a ceiling
LIMIT=2000          # a file crossing this without a ceiling is a new offender

is_exempt() {
    case "$1" in
        src/third_party/*) return 0 ;;   # Dear ImGui, miniaudio, stb
    esac
    return 1
}

list_sources() {
    find src -type f \( -name '*.cpp' -o -name '*.h' \) | sort
}

# ── --update: rewrite the budget from what is on disk ─────────────────────
if [ "${1:-}" = "--update" ]; then
    {
        echo "# POM68K file-size budget — see tools/check_file_sizes.sh"
        echo "#"
        echo "# Ceilings for every first-party file at or above ${WATCH} lines."
        echo "# A number here may go DOWN (record the win); it may not go up."
        echo "# Growing one of these means editing this file in the same commit."
        echo "#"
        echo "# Regenerate with: tools/check_file_sizes.sh --update"
        echo ""
    } > "$BUDGET"
    while read -r f; do
        is_exempt "$f" && continue
        n=$(wc -l < "$f")
        [ "$n" -ge "$WATCH" ] && printf "%-40s %d\n" "$f" "$n" >> "$BUDGET"
    done < <(list_sources)
    echo "Wrote $BUDGET:"
    grep -v '^#' "$BUDGET" | grep -v '^$' | sed 's/^/  /'
    exit 0
fi

# ── check ─────────────────────────────────────────────────────────────────
if [ ! -f "$BUDGET" ]; then
    echo "check_file_sizes: $BUDGET is missing — run '$0 --update'" >&2
    exit 2
fi

declare -A ceiling=()
while read -r path limit_value _rest; do
    case "$path" in ''|\#*) continue ;; esac
    ceiling["$path"]="$limit_value"
done < "$BUDGET"

fail=0
slack_report=""

while read -r f; do
    is_exempt "$f" && continue
    n=$(wc -l < "$f")
    cap="${ceiling[$f]:-}"

    if [ -n "$cap" ]; then
        if [ "$n" -gt "$cap" ]; then
            echo "FAIL  $f: $n lines, ceiling $cap (+$((n - cap)))" >&2
            fail=1
        elif [ $((cap - n)) -ge 100 ]; then
            slack_report+="      $f: $n lines, ceiling $cap — lower it by $((cap - n))"$'\n'
        fi
        unset 'ceiling[$f]'
    elif [ "$n" -ge "$LIMIT" ]; then
        echo "FAIL  $f: $n lines and no ceiling — a new file over $LIMIT lines." >&2
        echo "      Split it, or record the ceiling with '$0 --update' and say why." >&2
        fail=1
    fi
done < <(list_sources)

# A ceiling whose file is gone (renamed, split, deleted) is stale.
for stale in "${!ceiling[@]}"; do
    echo "FAIL  $BUDGET lists $stale, which no longer exists — run '$0 --update'" >&2
    fail=1
done

if [ "$fail" -ne 0 ]; then
    echo "" >&2
    echo "The rule is CLAUDE.md's first convention: one concern per file." >&2
    echo "A new platform's GUI runner is a spec row, not another 350 lines." >&2
    exit 1
fi

if [ -n "$slack_report" ]; then
    echo "check_file_sizes: passed, with ceilings that can be tightened:"
    printf '%s' "$slack_report"
else
    echo "check_file_sizes: passed."
fi
exit 0
