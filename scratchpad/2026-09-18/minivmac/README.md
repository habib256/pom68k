# Mini vMac on our cable: the interop goes both ways

`TODO` § Services réseau asked to test LToUDP interop with Mini vMac — "same
multicast group, both directions". Done, at the level that matters: a foreign
emulator's guest **mounted a volume served by POM68K's own AppleTalk stack**,
and read a file from it.

## The setup

Mini vMac **37.03**, built here from Gryphel's source with LocalTalk over UDP
(`./setup_t -t lx64 -m Plus -lt -lto udp`). Its `src/LTOVRUDP.h` joins
**239.192.76.84:1954** and tags each datagram with its pid — the same wire
`src/LtoUdp.cpp` speaks, which is what made this a five-minute experiment
rather than a protocol port. Guest: System 7.0 on a Macintosh Plus, under
Xvfb, driven with `xdotool` (Mac menus need press-drag-release, not a click).

POM68K ran beside it — Macintosh Plus, HD20SC, `POM68K_LTOUDP=1
POM68K_APPLETALK=1` — with the netatalk bridge of the same day still up, so
three AppleTalk nodes shared one virtual cable.

## Both directions, on the wire and on the screen

The two servers answered the same lookup from Mini vMac's Chooser:

```
128->25 [local]        LkUp-Reply | POMTEST:AFPServer@POM68K at 2.128:132   ← POM68K's own stack
254->25 [2.125->1.25]  LkUp-Reply | POM68K:AFPServer@*      at 2.125:128    ← netatalk, via the router
```

They are one name apart because they were one name apart on purpose: both
default to "POM68K", and the AppleTalk window's own form renamed ours to
POMTEST **live** (`Appliquer`, no restart) so the Chooser could tell them
apart — which is also a first exercise of that control against a foreign
client.

Then Mini vMac's guest logged in as Guest and mounted, and the session is
node-to-node on the LocalTalk segment, no router in the path:

```
25->128 [local] ATP sock 252->130   ×10      128->25 [local] ATP sock 130->252   ×5
```

POM68K's own AppleTalk window agreed, in its own words: *Sessions : 1 (volume
monté) • utilisateur : Guest*, *Dernière commande : GetVolParms*, *Invité vu :
25* — Mini vMac's node number.

| File | What it is |
|---|---|
| `cable_first_lookup.log` | Mini vMac's first `BrRq =:AFPServer@*` on our cable, answered by netatalk |
| `cable_two_servers.log` | both servers replying to the same lookup, ours from node 128 |
| `cable_mount.log` | the ASP/ATP session, Mini vMac's node 25 ⇄ our node 128 |
| `list2_zoom.png` | its Chooser listing POM68K **and** POMTEST |
| `login3.png`, `volumes.png` | "Connect to the file server POMTEST", then our volume "AppleShare" |
| `mounted.png`, `volume_open.png` | the volume on its desktop, and open: 7 items, `HELLO.txt` among them |
| `pom68k_session_state.png` | our side of the same moment |

(`*.png` is gitignored — these are local.)

## What it also exposed: our hub's network number is hard-coded

`AtalkHub::attach` configures the stack as **net 2, node 128**
(`stack_.configure(2, 128, …)`), whatever the segment actually is. Here the
LToUDP segment is net 1 (TashRouter seeds it) and netatalk's TAP segment is
net 2 — so two different networks were both numbered 2, and our hub sat on
the wrong one by construction. Nothing failed in this test: NBP replies carry
the address, and Mini vMac reached node 128 locally. But it is visible in the
trace — Mini vMac's first connection attempt went to `25->125 local`, trying
netatalk's node number on the local wire before retrying through the router —
and a guest that trusts the net number would be misled. Left as a finding,
not fixed: the fix is for the hub to learn its net from the router (RTMP)
rather than assert one, and that wants a gate of its own.

## Reproducing

```bash
# Mini vMac, once
curl -O https://www.gryphel.com/d/minivmac/minivmac-37.03/minivmac-37.03.src.tgz
tar xzf minivmac-37.03.src.tgz && cd minivmac
gcc -o setup_t setup/tool.c && ./setup_t -t lx64 -m Plus -lt -lto udp > bld.sh
bash bld.sh && make          # NOT `> Makefile`: the script rewrites that name under itself

# then, side by side
Xvfb :77 & DISPLAY=:77 ./minivmac disk1.dsk &        # vMac.ROM = a Plus ROM
POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <Plus ROM> hdv/HD20SC.vhd
tools/netatalk2/llap_sniff.py 60 --quiet             # who says what
```
