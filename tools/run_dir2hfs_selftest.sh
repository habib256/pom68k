#!/usr/bin/env bash
# POM68K — dir2hfs self-test wrapper: pick the interpreter at RUN time.
#
# The gate used to bind its interpreter at CONFIGURE time (`if(EXISTS
# .venv-tools/bin/python)`), which means a tree configured before the venv
# exists keeps calling the system python3 forever — and the system python3
# has no machfs, so the gate soft-skips, exits 0 and counts green. Measured
# 2026-08-27: creating the venv did not turn the gate green until the build
# directory was reconfigured, which nothing tells you to do.
#
# Choosing here costs nothing and cannot go stale.

set -uo pipefail
cd "$(dirname "$0")/.."

if [ -x .venv-tools/bin/python ]; then
    exec .venv-tools/bin/python tools/dir2hfs.py --selftest "$@"
fi
exec python3 tools/dir2hfs.py --selftest "$@"
