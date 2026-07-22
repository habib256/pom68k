#!/usr/bin/env bash
# POM68K — AppleShare bridge: serve the repo's input/ folder to the emulated
# Macs over real AppleTalk. Topology (all local, nothing touches your LAN):
#
#   POM68K guests (POM68K_LTOUDP=1 POM68K_APPLETALK=1)
#      ⇄ LToUDP multicast ⇄ TashRouter [LtoudpPort ⇄ MacvtapPort]
#      ⇄ pomtap0 (macvtap on veth-router) ⇄ veth-atalk (kernel DDP)
#      ⇄ atalkd + afpd (netatalk 2.4, built by build_netatalk2.sh)
#
# macvtap-on-veth is the standard trick: the host stack on veth-atalk and
# the macvtap on veth-router see each other's frames (plain macvtap on a
# physical NIC would NOT loop back to the host stack).
#
# Needs sudo for: modprobe appletalk, the veth/macvtap pair, and the
# netatalk daemons (DDP sockets). Run: sudo tools/netatalk2/appleshare_bridge.sh
# Then start TashRouter:  .venv-tools/bin/python tools/netatalk2/router.py
# In the guest: Chooser → AppleShare → "POM68K" → volume "Input" (guest login).

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NA="$ROOT/extern/netatalk2/install"
[ -x "$NA/sbin/afpd" ] || { echo "run tools/netatalk2/build_netatalk2.sh first"; exit 1; }
[ "$(id -u)" = 0 ] || { echo "needs sudo (DDP sockets + interfaces)"; exit 1; }

modprobe appletalk

# Idempotent interface setup.
ip link show veth-atalk >/dev/null 2>&1 || {
    ip link add veth-atalk type veth peer name veth-router
}
ip link set veth-atalk up
ip link set veth-router up
ip link show pomtap0 >/dev/null 2>&1 || {
    ip link add link veth-router name pomtap0 type macvtap mode bridge
}
ip link set pomtap0 up
TAPIDX=$(cat /sys/class/net/pomtap0/ifindex)
REAL_USER=${SUDO_USER:-$USER}
chown "$REAL_USER" "/dev/tap$TAPIDX"
echo "pomtap0 = /dev/tap$TAPIDX (owned by $REAL_USER)"

# netatalk configuration (generated next to this script).
CONF="$HERE/run"
mkdir -p "$CONF"
cat > "$CONF/atalkd.conf" <<EOF
veth-atalk -phase 2 -net 10 -zone "POM68K"
EOF
cat > "$CONF/afpd.conf" <<EOF
"POM68K" -ddp -notcp -uamlist uams_guest.so -nosavepassword
EOF
cat > "$CONF/AppleVolumes.default" <<EOF
$ROOT/input "Input" options:usedots
EOF
cat > "$CONF/AppleVolumes.system" <<EOF
EOF

"$NA/sbin/atalkd" -f "$CONF/atalkd.conf"
sleep 2
"$NA/bin/nbprgstr" -p 4 "POM68K:Workstation" || true
"$NA/sbin/afpd" -F "$CONF/afpd.conf" -P "$CONF/afpd.pid" \
    -v "$CONF/AppleVolumes.default" -s "$CONF/AppleVolumes.system" || {
    # older 2.x afpd uses -f/-v differently; fall back to defaults dir
    "$NA/sbin/afpd" -F "$CONF/afpd.conf"
}
echo "atalkd + afpd up. Zone: POM68K, server 'POM68K', volume 'Input'."
echo "Now: .venv-tools/bin/python tools/netatalk2/router.py"
