#!/usr/bin/env bash
# POM68K — AppleShare bridge, step 1/3: kernel module + local interfaces.
# Topology (all local, nothing touches your LAN):
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
# Order matters — TashRouter must be routing BEFORE atalkd starts, so the
# non-seed atalkd learns net/zone from it instead of fighting it:
#   1. sudo tools/netatalk2/appleshare_bridge.sh          (this script)
#   2. .venv-tools/bin/python tools/netatalk2/router.py   (keep running)
#   3. sudo tools/netatalk2/appleshare_daemons.sh         (atalkd + afpd)
# In the guest: Chooser → AppleShare → "POM68K" → volume "Input" (Guest).

set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "needs sudo (module + interfaces)"; exit 1; }

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
echo "Next: .venv-tools/bin/python tools/netatalk2/router.py"
echo "Then: sudo tools/netatalk2/appleshare_daemons.sh"
