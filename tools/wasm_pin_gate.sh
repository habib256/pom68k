#!/usr/bin/env bash
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# One `wasm_<gate>` replay (cmake/Pom68kWasmPins.cmake): runs a boot etalon
# compiled to WebAssembly under node, from the source tree, with the native
# gate's argv. The etalon checks its pixel pin against tools/pixel_pins.tsv
# itself; this wrapper only refuses to pretend when there is nothing to run.
#
#   wasm_pin_gate.sh <node> <program.js> [argv...]
#
# Missing node or a missing program is an abstention (SKIP, exit 0), which
# tools/gate_execution_census.py counts as such — never as a replay.
set -u
node="$1"; js="$2"; shift 2
if [ -z "$node" ] || [ "${node%NOTFOUND}" != "$node" ] || ! [ -x "$node" ]; then
    echo "SKIP: node not found (configure with -DPOM68K_NODE=/path/to/node)"
    exit 0
fi
if ! [ -f "$js" ]; then
    echo "SKIP: $js not built — build the Emscripten tree's wasmpins target (DEV.md § WebAssembly pin replay)"
    exit 0
fi
echo "wasm pin replay: node $("$node" --version) $js $*"
exec "$node" "$js" "$@"
