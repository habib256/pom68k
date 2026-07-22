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
sudo tools/netatalk2/appleshare_bridge.sh      # ifaces + atalkd + afpd
.venv-tools/bin/python tools/netatalk2/router.py   # LToUDP ⇄ EtherTalk
POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <ROM> <disk>…
```

In the guest: **Chooser → AppleShare** → server "POM68K" → log in as
Guest → mount **Input**. StuffIt archives can be expanded straight off
the share (or copy to the local disk first — LocalTalk runs at its real
230.4 kbit/s).

Notes:
- Kernel module: `modprobe appletalk` (done by the bridge script). The
  module ships with Ubuntu's generic kernels.
- Nothing binds to your physical network: the DDP segment lives on a
  local veth pair, and LToUDP multicast has TTL 1.
- `tools/netatalk2/run/` holds the generated configs + pid files
  (gitignored). Stop with `sudo pkill afpd atalkd` and Ctrl-C the router.
- System 6 guests: open the Chooser to start AppleTalk (Sys 7 opens it
  at boot with `POM68K_APPLETALK=1`).
