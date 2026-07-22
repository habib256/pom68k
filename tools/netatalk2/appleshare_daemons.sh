#!/usr/bin/env bash
# POM68K — AppleShare bridge, step 3/3: atalkd + afpd (run AFTER router.py
# is up — see appleshare_bridge.sh for the full order).
#
# atalkd runs NON-SEED on veth-atalk: it learns net/zone from the running
# TashRouter (a second seed router on the same segment would fight it —
# that mistake made getzones time out on the first bring-up).
# afpd 2.4 flags: -F conf, -P pidfile, -f/-s default/system AppleVolumes
# (NOT -v, which prints the version and exits — first bring-up bug too).

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NA="$ROOT/extern/netatalk2-build/install"
[ -x "$NA/sbin/afpd" ] || { echo "run tools/netatalk2/build_netatalk2.sh first"; exit 1; }
[ "$(id -u)" = 0 ] || { echo "needs sudo (DDP daemons)"; exit 1; }
ip link show veth-atalk >/dev/null 2>&1 || { echo "run appleshare_bridge.sh first"; exit 1; }

CONF="$HERE/run"
mkdir -p "$CONF"
cat > "$CONF/atalkd.conf" <<EOF
veth-atalk -phase 2
EOF
cat > "$CONF/afpd.conf" <<EOF
"POM68K" -ddp -notcp -uamlist uams_guest.so -nosavepassword
EOF
cat > "$CONF/AppleVolumes.default" <<EOF
$ROOT/input "Input" options:usedots
EOF
: > "$CONF/AppleVolumes.system"

pkill -f "$NA/sbin/atalkd" 2>/dev/null || true
pkill -f "$NA/sbin/afpd" 2>/dev/null || true
sleep 1

"$NA/sbin/atalkd" -f "$CONF/atalkd.conf"
echo "atalkd up (learning net/zone from the router)..."
sleep 3
"$NA/bin/getzones" | head -3 || true
"$NA/sbin/afpd" -F "$CONF/afpd.conf" -P "$CONF/afpd.pid" \
    -f "$CONF/AppleVolumes.default" -s "$CONF/AppleVolumes.system"
sleep 1
"$NA/bin/nbplkup" | head -6 || true
echo "afpd up. Guest: Chooser → AppleShare → 'POM68K' → volume 'Input'."
