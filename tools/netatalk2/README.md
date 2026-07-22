# AppleShare bridge — serve `input/` to the emulated Macs' Chooser

End goal: the Chooser of a Mac running inside POM68K shows an AppleShare
server "POM68K" with a volume **Input** = the repo's `input/` folder,
over real AppleTalk (LLAP → DDP → AFP), no guest-side software needed.

```
POM68K guest (POM68K_LTOUDP=1 POM68K_APPLETALK=1)
   ⇄ SCC printer port, real LLAP (src/Scc8530.*)
   ⇄ LToUDP multicast 239.192.76.84:1954 (src/LtoUdp.*)
   ⇄ TashRouter (extern/tashrouter): LtoudpPort ⇄ MacvtapPort
   ⇄ pomtap0 (macvtap on a veth pair — stays off your LAN)
   ⇄ kernel DDP (module `appletalk`) on veth-atalk
   ⇄ atalkd + afpd — netatalk 2.4.9 (extern/netatalk2, vendored)
```

## One-time build (no sudo, no system packages)

```bash
tools/netatalk2/build_netatalk2.sh
```

Builds the vendored netatalk 2.4.9 plus static, checksum-pinned
Berkeley DB 5.3 and libgcrypt into `extern/netatalk2-build/` (gitignored).

## Run (each session)

```bash
sudo tools/netatalk2/appleshare.sh                 # everything, in order
POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <ROM> <disk>…
```

One script brings up module + interfaces, the TashRouter (as your user,
logs in `run/router.log`), then atalkd + afpd, and self-checks with
getzones/nbplkup. `sudo tools/netatalk2/appleshare.sh stop` tears it all
down. Internals: atalkd runs non-seed and learns net/zone from
TashRouter, so the router is started first (two seed routers on one
segment fight — the first bring-up's getzones timeout).

In the guest: **Chooser → AppleShare** → server "POM68K" → log in as
Guest → mount **Input**. StuffIt archives can be expanded straight off
the share (or copy to the local disk first — LocalTalk runs at its real
230.4 kbit/s).

Notes:
- Kernel module: `modprobe appletalk` (done by the bridge script). The
  module ships with Ubuntu's generic kernels.
- Nothing binds to your physical network: the DDP segment lives on a
  local veth pair, and LToUDP multicast has TTL 1.
- `tools/netatalk2/run/` holds the generated configs, logs + pid files
  (gitignored). `sudo tools/netatalk2/appleshare.sh stop` stops it all.
- System 6 guests: open the Chooser to start AppleTalk (Sys 7 opens it
  at boot with `POM68K_APPLETALK=1`).
