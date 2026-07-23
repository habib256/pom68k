#!/usr/bin/env bash
# POM68K — MacIP layer: real TCP/IP for the emulated Macs, tunneled in
# AppleTalk DDP (IP-in-DDP, "MacIP") and NATed to the internet by the host.
#
#   sudo tools/macip/macip.sh          # start (AppleShare bridge must be up)
#   sudo tools/macip/macip.sh stop     # tear down (reverses every host change)
#
# Normally you don't call this directly:  sudo tools/netatalk2/appleshare.sh
# --macip  brings the whole stack up including this layer.
#
# What it runs: the vendored macipgw (extern/macipgw, built by
# tools/macip/build_macipgw.sh against the vendored netatalk 2.4.9). macipgw
#   - registers "<gw-ip>:IPGATEWAY@<zone>" via NBP (the name Open Transport's
#     "AppleTalk (MacIP)" and MacTCP's "LocalTalk" look up),
#   - answers MacIP address-assignment ATP requests on DDP socket 72,
#   - moves IP packets between DDP type-22 datagrams and a host tun device.
#
# Host changes made — every one is echoed when made and undone by `stop`:
#   1. net.ipv4.ip_forward=1        (previous value saved and restored)
#   2. three iptables rules tagged with the comment "pom68k-macip"
#      (MASQUERADE for the MacIP subnet + two FORWARD accepts)
#   3. the macipgw process itself (its tun dev vanishes when it exits)
#
# Tunables (env):
#   MACIP_NET=192.168.151.0   MacIP subnet (must not collide with any host net)
#   MACIP_MASK=255.255.255.0  its netmask; gateway takes .1, clients .2+
#   MACIP_DNS=8.8.8.8         DNS server advertised to MacIP clients
#                             (must be a real resolver reachable through NAT —
#                             NOT the systemd stub 127.0.0.53)
#   MACIP_ZONE=POM68K         AppleTalk zone to register the gateway in
#   MACIP_DEBUG=0x0111        macipgw -d mask; 0x1111 adds per-packet +
#                             per-lease detail (verbose — for bring-up only)

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
NA="$ROOT/extern/netatalk2-build/install"
BIN="$ROOT/extern/netatalk2-build/macipgw/macipgw"
RUN="$ROOT/tools/netatalk2/run"            # shared with appleshare.sh (gitignored)
STATE="$RUN/macip.state"

MACIP_NET=${MACIP_NET:-192.168.151.0}
MACIP_MASK=${MACIP_MASK:-255.255.255.0}
MACIP_DNS=${MACIP_DNS:-8.8.8.8}
MACIP_ZONE=${MACIP_ZONE:-POM68K}
MACIP_DEBUG=${MACIP_DEBUG:-0x0111}

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
mkdir -p "$RUN"

# ── tiny IP math (gateway = net+1, CIDR prefix from mask) ──
ip2int() { local a b c d; IFS=. read -r a b c d <<<"$1"; echo $(( (a<<24)|(b<<16)|(c<<8)|d )); }
int2ip() { local i=$1; echo "$(( (i>>24)&255 )).$(( (i>>16)&255 )).$(( (i>>8)&255 )).$(( i&255 ))"; }
NETI=$(ip2int "$MACIP_NET"); MASKI=$(ip2int "$MACIP_MASK")
GW=$(int2ip $(( NETI + 1 )))
PFX=0; m=$MASKI; while (( m & 0x80000000 )); do PFX=$((PFX+1)); m=$(( (m<<1) & 0xFFFFFFFF )); done
SUBNET="$MACIP_NET/$PFX"

# The three firewall rules, one place (same spec for add and -D delete):
NAT_SPEC=(-s "$SUBNET" ! -d "$SUBNET" -m comment --comment pom68k-macip -j MASQUERADE)
FWD_OUT=(FORWARD -s "$SUBNET" -m comment --comment pom68k-macip -j ACCEPT)
FWD_IN=(FORWARD -d "$SUBNET" -m conntrack --ctstate RELATED,ESTABLISHED
        -m comment --comment pom68k-macip -j ACCEPT)

stop_macip() {
    pkill -f "$BIN" 2>/dev/null && echo "macipgw stopped (tun device gone with it)" || true
    if command -v iptables >/dev/null; then
        iptables -t nat -D POSTROUTING "${NAT_SPEC[@]}" 2>/dev/null \
            && echo "removed NAT rule (MASQUERADE $SUBNET)" || true
        iptables -D "${FWD_OUT[@]}" 2>/dev/null && echo "removed FORWARD rule (out)" || true
        iptables -D "${FWD_IN[@]}"  2>/dev/null && echo "removed FORWARD rule (in)"  || true
    fi
    if [ -f "$STATE" ]; then
        # shellcheck disable=SC1090
        . "$STATE"
        if [ "${SAVED_IP_FORWARD:-1}" = 0 ]; then
            sysctl -qw net.ipv4.ip_forward=0
            echo "restored net.ipv4.ip_forward=0"
        fi
        rm -f "$STATE"
    fi
    rm -f "$RUN/macipgw.pid"
}

if [ "${1:-}" = "stop" ]; then stop_macip; echo "MacIP layer stopped."; exit 0; fi

[ -x "$BIN" ] || { echo "macipgw not built — run tools/macip/build_macipgw.sh first"; exit 1; }
command -v iptables >/dev/null || { echo "iptables not found (needed for NAT)"; exit 1; }
pgrep -f "$NA/sbin/atalkd" >/dev/null || {
    echo "atalkd is not running — start the bridge first:"
    echo "  sudo tools/netatalk2/appleshare.sh --macip"; exit 1; }

stop_macip 2>/dev/null || true          # idempotent restart

# ── 1. IP forwarding (previous value saved for stop) ──
prev=$(sysctl -n net.ipv4.ip_forward)
echo "SAVED_IP_FORWARD=$prev" > "$STATE"
if [ "$prev" = 0 ]; then
    sysctl -qw net.ipv4.ip_forward=1
    echo "enabled net.ipv4.ip_forward (was 0 — will restore on stop)"
fi

# ── 2. macipgw (root: it creates the tun dev and configures it) ──
# -d keeps it in the foreground (nohup'd) and logging to run/macipgw.log;
# 0x0111 = macip+route+tunnel events, 0x1111 adds per-packet/lease detail.
nohup "$BIN" -d "$MACIP_DEBUG" -z "$MACIP_ZONE" -n "$MACIP_DNS" \
      "$MACIP_NET" "$MACIP_MASK" > "$RUN/macipgw.log" 2>&1 &
echo $! > "$RUN/macipgw.pid"
echo "macipgw started: gateway $GW, clients $(int2ip $((NETI+2)))-$(int2ip $(( (NETI | ~MASKI & 0xFFFFFFFF) - 1 ))), DNS $MACIP_DNS, zone $MACIP_ZONE (run/macipgw.log)"

# Wait for the tun device to come up with the gateway address (macipgw picks
# the first free tun0..tun9), then for the NBP registration.
TUNIF=
for i in $(seq 1 40); do
    TUNIF=$(ip -o -4 addr show | awk -v gw="$GW" '$4 ~ "^"gw"/" {print $2; exit}')
    [ -n "$TUNIF" ] && break
    kill -0 "$(cat "$RUN/macipgw.pid")" 2>/dev/null || {
        echo "macipgw died — run/macipgw.log:"; tail -5 "$RUN/macipgw.log"; exit 1; }
    sleep 0.5
done
[ -n "$TUNIF" ] || { echo "tun device did not appear — run/macipgw.log:"; tail -5 "$RUN/macipgw.log"; exit 1; }
echo "tunnel up: $TUNIF = $GW/$PFX (MTU 586, the DDP payload limit)"

# ── 3. NAT: MacIP subnet → wherever the host's default route goes ──
iptables -t nat -A POSTROUTING "${NAT_SPEC[@]}"
iptables -I "${FWD_OUT[@]}"
iptables -I "${FWD_IN[@]}"
echo "NAT on: MASQUERADE $SUBNET + FORWARD accepts (tagged pom68k-macip)"

# ── self-check: the gateway must be NBP-visible where the Macs will look ──
sleep 2                                   # NBP registration takes a moment
echo -n "nbplkup =:IPGATEWAY@$MACIP_ZONE : "
if "$NA/bin/nbplkup" "=:IPGATEWAY@$MACIP_ZONE" 2>/dev/null | grep -m1 IPGATEWAY; then
    :
else
    echo "NOT FOUND (yet) — macipgw retries registration for ~25 s;"
    echo "  check again with: $NA/bin/nbplkup '=:IPGATEWAY@$MACIP_ZONE'"
fi
echo
echo "MacIP up. Guest side (see docs/APPLETALK.md §6.4):"
echo "  OS 8.1:  TCP/IP control panel → Connect via 'AppleTalk (MacIP)',"
echo "           Configure 'Using MacIP Server', zone $MACIP_ZONE, via DHCP-like assignment."
echo "  Sys 7.5: MacTCP control panel → LocalTalk icon, 'Server' addressing."
echo "Stop:  sudo tools/macip/macip.sh stop"
