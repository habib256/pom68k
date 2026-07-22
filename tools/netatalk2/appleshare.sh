#!/usr/bin/env bash
# POM68K — AppleShare bridge, ONE command:
#
#   sudo tools/netatalk2/appleshare.sh          # bring everything up
#   sudo tools/netatalk2/appleshare.sh stop     # tear everything down
#
# Brings up, in order (order matters — atalkd learns net/zone from the
# router, so the router must route first):
#   1. kernel module `appletalk` + local veth pair + macvtap pomtap0
#      (macvtap-on-veth: host DDP stack and router see each other; nothing
#      touches your LAN)
#   2. TashRouter (vendored, runs as the invoking user, logs to run/)
#   3. atalkd (non-seed) + afpd serving input/ as volume "Input"
# then self-checks with getzones + nbplkup.
#
# Guest side:  POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <ROM> <disk>
#              Chooser → AppleShare → "POM68K" → Guest → mount "Input".

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NA="$ROOT/extern/netatalk2-build/install"
CONF="$HERE/run"
REAL_USER=${SUDO_USER:-$USER}
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }

stop_all() {
    pkill -f "$NA/sbin/afpd" 2>/dev/null || true
    pkill -f "$NA/sbin/atalkd" 2>/dev/null || true
    pkill -f "tools/netatalk2/router.py" 2>/dev/null || true
}

if [ "${1:-}" = "stop" ]; then
    stop_all
    ip link del pomtap0 2>/dev/null || true
    ip link del veth-atalk 2>/dev/null || true   # legacy macvtap-era ifaces
    echo "AppleShare bridge stopped."
    exit 0
fi

[ -x "$NA/sbin/afpd" ] || { echo "run tools/netatalk2/build_netatalk2.sh first"; exit 1; }
mkdir -p "$CONF"
# run/ may have been created by a previous sudo run — the router writes its
# pid/log there as the real user.
chown -R "$REAL_USER" "$CONF"
stop_all
sleep 1

# ── 1. module + interface (idempotent) ──
# A plain TAP: the router (as the user) owns the char device, and the host
# DDP stack talks directly on pomtap0 — no veth/macvtap (macvtap filtered
# the 09:00:07 RTMP multicasts and atalkd never learned the network).
modprobe appletalk
ip link del veth-atalk 2>/dev/null || true       # clean legacy macvtap setup
ip link show pomtap0 >/dev/null 2>&1 && ip link del pomtap0 2>/dev/null || true
ip tuntap add dev pomtap0 mode tap user "$REAL_USER"

# ── 2. router (as the real user; it owns the tap + the LToUDP socket) ──
sudo -u "$REAL_USER" -- bash -c \
    "cd '$ROOT' && nohup .venv-tools/bin/python tools/netatalk2/router.py \
     > '$CONF/router.log' 2>&1 & echo \$! > '$CONF/router.pid'"
for i in $(seq 1 20); do
    grep -q "claiming node address" "$CONF/router.log" 2>/dev/null && break
    sleep 0.5
done
grep -q "claiming node address" "$CONF/router.log" || {
    echo "router did not come up — $CONF/router.log:"; tail -5 "$CONF/router.log"; exit 1; }
ip link set pomtap0 up                 # carrier present once the router holds the tap
echo "router up (run/router.log)"

# ── 3. atalkd (non-seed: learns net/zone from the router) + afpd ──
cat > "$CONF/atalkd.conf" <<EOF
pomtap0 -phase 2
EOF
# Guest sessions run as the real user, not the default "nobody": Ubuntu
# homes are drwxr-x--- so nobody cannot traverse to $ROOT/input — afpd
# then hides the volume at login (empty Chooser volume list, 2026-07-22)
# — and could not write into it anyway. The DDP segment is local-only,
# so mapping guest to $REAL_USER is safe and gives read/write with
# correct file ownership.
cat > "$CONF/afpd.conf" <<EOF
"POM68K" -ddp -notcp -uamlist uams_guest.so -nosavepassword -guestname "$REAL_USER"
EOF
cat > "$CONF/AppleVolumes.default" <<EOF
$ROOT/input "Input" options:usedots
EOF
: > "$CONF/AppleVolumes.system"

"$NA/sbin/atalkd" -f "$CONF/atalkd.conf"
sleep 3
"$NA/sbin/afpd" -F "$CONF/afpd.conf" -P "$CONF/afpd.pid" \
    -f "$CONF/AppleVolumes.default" -s "$CONF/AppleVolumes.system"
sleep 4                                 # NBP registration takes a moment

echo "── self-check ──"
echo -n "zones: ";   "$NA/bin/getzones" 2>&1 | head -2
echo "nbp:";         "$NA/bin/nbplkup" 2>&1 | head -6
echo
echo "Bridge up. Now run:"
echo "  POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <ROM> <disk>"
echo "Chooser → AppleShare → 'POM68K' → Guest → mount 'Input'."
echo "Stop everything:  sudo tools/netatalk2/appleshare.sh stop"
