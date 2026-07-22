# netatalk 2.4.9 — vendored for POM68K

- **Upstream**: https://github.com/Netatalk/netatalk
- **Tag**: `netatalk-2-4-9` (commit `a76a908c76282e6e2d4546c7808bc8ea66826a4c`)
- **License**: GPL-2.0 (see `COPYING`) — compatible with POM68K's GPLv3.
- **Local changes**: none — pristine upstream sources (`.git` and build
  directories stripped).

## Why vendored

Netatalk 2.x is the last AFP-over-DDP (classic AppleTalk) file server;
3.x dropped AppleTalk entirely and 2.x is no longer packaged by modern
distributions. POM68K's LocalTalk stack (LLAP over the SCC + LToUDP
cable, see `src/Scc8530.*` / `src/LtoUdp.*`) needs it as the AppleShare
end of the TashRouter bridge that serves host folders to the emulated
Macs' Chooser. Same policy as `extern/moira`: pin the exact source in
the tree so the build never depends on upstream availability.

## Building

```bash
tools/netatalk2/build_netatalk2.sh
```

builds from THIS directory (meson/ninja) into
`extern/netatalk2-build/install/` (gitignored), together with its two
static dependencies fetched and checksum-pinned by the script (Berkeley
DB 5.3.28, libgcrypt 1.10.3 + libgpg-error 1.47 — generic libraries,
not vendored). Kernel requirement: the `appletalk` module (present on
Ubuntu's generic kernels). Bridge usage: `tools/netatalk2/README.md`.
