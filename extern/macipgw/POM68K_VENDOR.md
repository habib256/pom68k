# macipgw — vendored for POM68K

- **Upstream**: https://github.com/zero2sixd/macipgw
- **Commit**: `5c05e5eb34c6cc6512dee570f03bdbd0cb6ac284` ("Debug
  improvements (#13)", 2025-02-20) — vendored 2026-07-23, `.git` stripped.
- **Provenance chain**: Stefan Bethke's original FreeBSD `macipgw` 1.0
  (1997, http://macipgw.sourceforge.net), Linux tun/tap port by Jason
  King (zero2sixd, 2015). This is the same code the macip.net appliance
  VM ships.
- **License**: BSD 2-clause (see `COPYRIGHT` and the per-file headers;
  parts (c) Regents of The University of Michigan, netatalk-style
  permissive) — compatible with POM68K's GPLv3.
- **Local changes**: one, mechanical (2026-07-23): the debug guards
  `gDebug & DEBUG_MACIP & DEBUG_DETAIL` / `gDebug & DEBUG_TUNNEL &
  DEBUG_DETAIL` in `macip.c`/`tunnel_linux.c` chained disjoint bitmasks
  (`0x0001 & 0x1000 == 0`), so detail logging — including the "assigned
  a.b.c.d" lease line, the one observable proof a client got its MacIP
  address — could never fire. Rewritten as
  `(gDebug & DEBUG_X) && (gDebug & DEBUG_DETAIL)` (10 sites, debug
  printf guards only; no behavioral code touched).

## What it is

A **MacIP gateway** daemon: it registers an NBP name of type
`IPGATEWAY` in an AppleTalk zone, answers MacIP address-assignment ATP
requests (DDP socket 72) from clients (Open Transport "AppleTalk
(MacIP)" / classic MacTCP "LocalTalk"), and shuttles IP datagrams
encapsulated in DDP type 22 to/from a Linux `tun` interface. The host
then routes/NATs those packets like any other IP traffic. Client → AT
address mapping ("MacIP ARP") is done with NBP lookups of type
`IPADDRESS` plus learning from inbound packets; idle leases are probed
with ICMP echo and reclaimed.

## How POM68K builds and runs it

- Built by `tools/macip/build_macipgw.sh` **against the vendored
  netatalk 2.4.9** (`extern/netatalk2` headers + `sys/netatalk/` kernel
  headers, linked to `extern/netatalk2-build/install/.../libatalk.so`
  with an rpath). It uses libatalk's public ATP/NBP API plus four
  internal-but-exported symbols (`nbp_parse`, `at_addr_eq`,
  `atp_alloc_buf`, `atp_free_buf`) — all verified exported by the
  vendored `libatalk.so` (2026-07-23). Output:
  `extern/netatalk2-build/macipgw/macipgw` (gitignored).
- Started by `tools/macip/macip.sh` (called from
  `tools/netatalk2/appleshare.sh --macip`) after `atalkd`, because
  `nbp_rgstr`/ZIP `GetZoneList` talk to the local `atalkd` through the
  kernel AppleTalk stack. Requires root (tun creation + `SIOCSIFADDR`).
- Needs the `ddp` entries in `/etc/services` (`rtmp 1/ddp`, `nbp 2/ddp`,
  `echo 4/ddp`, `zip 6/ddp`) — present on stock Debian/Ubuntu.

## Known upstream quirks (left as-is)

- `-V` (version) is declared as `V:` in getopt, so it demands a dummy
  argument.
- The `-u user` privilege-drop swaps uid/gid (`setgid(pw_uid)` /
  `setuid(pw_gid)`) — do not use `-u`; POM68K runs it as root like
  `afpd`.
- `iptoa()` uses `%ld` for `uint32_t` (format warnings, harmless on
  LP64).
- Debug (`-d mask`) requires `-DDEBUG` at compile time; the stock
  Makefile enables it and so does our build script (`0x0111` = macip +
  route + tunnel, `0x1111` adds per-packet detail).
