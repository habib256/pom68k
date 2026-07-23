#!/usr/bin/env bash
# POM68K — build the VENDORED macipgw (extern/macipgw, see POM68K_VENDOR.md
# there) against the VENDORED netatalk 2.4.9. No system packages, no sudo.
#
# Prereq: tools/netatalk2/build_netatalk2.sh must have run (macipgw links
# against the libatalk.so it produced).
#
# Result: extern/netatalk2-build/macipgw/macipgw   (gitignored build area)
# Run it via tools/macip/macip.sh (or appleshare.sh --macip), not directly.

set -euo pipefail
cd "$(dirname "$0")/../.."                 # repo root
SRC="$PWD/extern/macipgw"
NA_SRC="$PWD/extern/netatalk2"
NA_LIB="$PWD/extern/netatalk2-build/install/lib/x86_64-linux-gnu"
OUT="$PWD/extern/netatalk2-build/macipgw"

[ -f "$SRC/macip.c" ]           || { echo "vendored macipgw missing: $SRC"; exit 1; }
[ -f "$NA_LIB/libatalk.so" ]    || { echo "run tools/netatalk2/build_netatalk2.sh first"; exit 1; }

mkdir -p "$OUT"
# Out-of-tree object build (keep extern/macipgw pristine): compile each .c
# with the vendored netatalk includes; -DDEBUG keeps the -d debug mask
# usable (macip.sh runs with -d 0x0111 so the log shows leases/tunnel).
CFLAGS="-O2 -g -Wall -DDEBUG -I$NA_SRC/include -I$NA_SRC/sys"
objs=()
for c in atp_input macip main nbp_lkup_async tunnel_linux util; do
    gcc $CFLAGS -c "$SRC/$c.c" -o "$OUT/$c.o"
    objs+=("$OUT/$c.o")
done
gcc "${objs[@]}" -o "$OUT/macipgw" \
    "$NA_LIB/libatalk.so" -Wl,-rpath,"$NA_LIB"

echo "OK: $OUT/macipgw"
