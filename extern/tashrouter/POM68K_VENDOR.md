# TashRouter — vendored for POM68K

- **Upstream**: https://github.com/lampmerchant/tashrouter
- **Commit**: `7905bec61fec07ea3433ad0cb3591d6184184b04` (2026-07-22)
- **License**: MIT (see `LICENSE`) — compatible with POM68K's GPLv3.
- **Local changes**: none — pristine upstream (`.git` stripped).

An AppleTalk router in pure Python. POM68K uses its `LtoudpPort` (the
same LToUDP wire format as `src/LtoUdp.*` — interop verified against its
address-acquisition probes) and `MacvtapPort` to bridge the emulated
Macs' LocalTalk onto a host interface where the vendored netatalk 2.4
(`extern/netatalk2`) serves AFP. Entry point: `tools/netatalk2/router.py`;
full bridge: `tools/netatalk2/appleshare_bridge.sh` + `README.md` there.
