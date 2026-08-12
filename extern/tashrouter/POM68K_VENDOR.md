# TashRouter — vendored for POM68K

- **Upstream**: https://github.com/lampmerchant/tashrouter
- **Commit**: `7905bec61fec07ea3433ad0cb3591d6184184b04` (2026-07-22)
- **License**: MIT (see `LICENSE`) — compatible with POM68K's GPLv3.
- **Local changes**: none — pristine upstream (`.git` stripped).

An AppleTalk router in pure Python. POM68K uses its `LtoudpPort` (the
same LToUDP wire format as `src/LtoUdp.*` — interop verified against its
address-acquisition probes) and `LinuxTapPort` to bridge the emulated
Macs' LocalTalk onto a host interface where the vendored netatalk 2.4
(`extern/netatalk2`) serves AFP. Entry point: `tools/netatalk2/router.py`
(`tashrouter/port/localtalk/ltoudp.py` `LtoudpPort`,
`tashrouter/port/ethertalk/tap.py` `LinuxTapPort` on `pomtap0`); full
bridge: `tools/netatalk2/appleshare.sh` + `README.md` there.

Its `MacvtapPort` (`port/ethertalk/macvtap.py`) is **not** used: the
macvtap-on-veth bridge was tried first and abandoned — macvtap filtered
the `09:00:07:ff:ff:ff` RTMP multicasts, so `atalkd` never learned the
network (`tools/netatalk2/README.md`, `router.py:26-28`).
