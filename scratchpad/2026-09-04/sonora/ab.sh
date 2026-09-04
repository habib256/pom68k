#!/usr/bin/env bash
# Sonora CACR A/B: the same boot etalon, once with the board default
# (flush retired) and once with POM68K_JIT_030_CACR_FLUSH=1 (hint forced
# back on). Byte-identical output is the proof that retiring the flush
# changed nothing the guest can observe.
set -u
W=/home/gistarcade/src/pom68k/.claude/worktrees/agent-a56c2e7843281856d
OUT=/tmp/claude-1000/-home-gistarcade-src-pom68k/742ac6a0-9a99-4ebe-8701-713deafa5293/scratchpad/sonora
cd "$W" || exit 1

gate="$1"        # e.g. lc3_boot_etalon
tag="$2"         # e.g. lc3_x64
shift 2
# remaining args: extra environment assignments

run() {
    local name="$1"; shift
    env "$@" "$W/build/$gate" > "$OUT/${tag}_${name}.out" 2>&1
    echo "$name exit=$?"
}

run default "$@"
run forced   "$@" POM68K_JIT_030_CACR_FLUSH=1

if diff -q "$OUT/${tag}_default.out" "$OUT/${tag}_forced.out" > /dev/null; then
    echo "BYTE-IDENTICAL: $tag"
else
    echo "DIFFERS: $tag"
    diff "$OUT/${tag}_default.out" "$OUT/${tag}_forced.out"
fi
sha256sum "$OUT/${tag}_default.out" "$OUT/${tag}_forced.out"
