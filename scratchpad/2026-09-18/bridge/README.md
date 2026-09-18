# A real AppleShare session, against a real server — and the three things
# that had to be fixed to get there

`TODO` § Services réseau asked for one: a complete AppleShare session against
a REAL server instead of the in-process `AtalkHub` every AFP gate talks to.
It is done, twice, and the proof is the host's own filesystem.

```
phase 7: Cmd-O; volumes mounted: "Mac-8.1-US", "Input"
phase 8: host saw "untitled folder"
phase 9: duplicate="BONJOUR.txt copy" data=32791 resource=8317
phase 9: 41108 bytes in 4.05 s of guest time (9.9 KiB/s)
phase 9: the source file is still byte-exact: yes
PASS
```

Both passes agree to the figure — 41 108 bytes in 4.05 s of guest time — and
the artefacts were verified on the host independently of the probe's own
oracle: data fork identical (32 791 B), resource fork identical (8 317 B),
Finder info `TEXTttxt` identical, all inside netatalk's `.AppleDouble`.

The wire: guest SCC → LToUDP multicast → TashRouter → `pomtap0` → the
kernel's DDP stack → `atalkd` + `afpd` serving `input/` as "Input". The
harness is `q605_afp_bridge_probe` (a dev tool, not a gate: it needs a
daemon brought up with sudo).

## Three defects, one of them the product's

**1. A stale absolute path, three times over.** The checkout was renamed
`POM68K` → `pom68k` at some point after netatalk was built, and netatalk
bakes absolute paths in at build time. It cost three separate failures:
`atalkd: error while loading shared libraries: libatalk.so.0` (the RUNPATH),
`Cannot create .../afp_signature.conf` (the signature file), and — the one
that reached the guest — a UAM directory that no longer existed, so afpd
loaded NO authentication module and the AppleShare client said so in its own
words: **"This file server does not use a recognizable log on sequence."**
`appleshare.sh` now sets `LD_LIBRARY_PATH` from the install tree it actually
has, and names `-uampath` and `-signature` explicitly; `macip.sh` carries the
library half for the same reason.

**2. The guest's receiver was deaf at exactly the wrong moment — a product
defect.** `GuiHostServices.h` armed the SCC's Rx queue only for the
in-process hub *without* a cable (`hub && !cable`), bundling it with the wire
boost. But LocalTalk is half-duplex: the driver drops its receiver while
transmitting and re-arms it on the EOM interrupt, and `Scc8530.cpp` drops a
frame that lands in that window — "receiver off = no ear", which is the
truth on a real wire. A peer behind a socket answers in MICROSECONDS, so
TashRouter's reply to the Chooser's `BrRq` landed in the deaf window
essentially every time. Measured: **128 BrRq sent, 60 LkUp-Replies back on
the cable**, DDP checksums verified correct — and an empty "Select a file
server" list. The arrival instant is a property of the host socket, not of
the wire, so dropping on it models nothing. The queue is now armed for a
cable too, while the pace stays real (`docs_test` holds that split).

**3. Two instruments of my own that lied.** The guest's `WindowList` ($09D6)
is per-LAYER: sampled while the Finder is the current process it describes
the Finder's front window — kind 8, title "" — with the Chooser plainly open,
and the retry loop it fed clicked the Apple menu on top of an open Chooser
three times. And a dark-pixel count over the server list counted the list's
own frame and scrollbar (398 dark pixels on an EMPTY list). Both replaced by
calibrated readings: the list panel's mean luminance (253.6 open, 234.9
desktop, 123.2 under the startup alert), and the guest's own VCB queue for
what is mounted.

## Files

| File | What it is |
|---|---|
| `session_pass1.log` / `session_pass2.log` | the two complete sessions, PASS, same figures |
| `session_before_rx_queue.log` | the same route with the Rx queue off: server list empty, mount never happens |
| `nbp_lookup_and_reply.txt` | the exchange on the cable — `BrRq` out, the router's `LkUp`, `LkUp-Reply POM68K:AFPServer@* at 2.220:128` back |
| `ddp_checksum_verified.txt` | one reply's DDP checksum, carried `$E3E8` = computed — the frames were never malformed |
| `probe_phase3_no_peer.log` | phases 0-3 with nothing on the segment, for contrast (`wire=377/0`) |
| `probe_phase3_router_only.log` | phases 0-3 against a router-only TashRouter, no root needed |
| `tashrouter_ltoudp_only.log` | that router, "LToUDP claiming node address 254" |
| `chooser_appleshare_empty_list.png` | the Chooser with AppleShare selected and an empty list (`*.png` is gitignored: local evidence) |

## Reproducing

```bash
sudo tools/netatalk2/appleshare.sh          # bridge up, self-checks with nbplkup
make -C build q605_afp_bridge_probe
POM68K_DUMP=1 POM68K_BRIDGE_SHARE=$PWD/input build/q605_afp_bridge_probe
tools/netatalk2/llap_sniff.py 300 --quiet --checksum   # what crosses the cable
```

`POM68K_BRIDGE_KEEP=1` leaves what the guest created on the share; without it
the probe puts the share back as it found it. Mac OS leaves a `Temporary
Items` folder behind either way — the guest's own, not the probe's.
