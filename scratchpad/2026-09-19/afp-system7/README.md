# A System 7.0 client on our AFP server: zero refused opcodes

`TODO`'s jalon 3 asks for "zéro opcode refusé sur les sessions live". Every
live session so far had the same client: Mac OS 8.1's AppleShare 3.7.4. Mini
vMac put a **System 7.0** client on the cable — seven years older, and the
first of its generation this server has ever answered — so the criterion
could be measured against a second one instead of assumed from the first.

## What the client did, and what the server counted

Mount as Guest, enumerate the share, Get Info on a file, Duplicate a file
with both forks. Our side, from the AppleTalk window at the end:

> *Sessions : 1 (volume monté) • utilisateur : Guest*
> *lu 65 664 o / écrit 98 304 o*
> **no refusal line** — `refusedCount` stayed at 0

`POM68KProber.bin copy` (65 664 B) landed on the host, next to its
`.AppleDouble` sidecar: the write path answers this client too.

The refusal line is new. `AfpServer` has counted refusals since it existed
and the live gate prints them, but the window never did — so a session with
an older System could quietly lose a feature with nothing said. It is
silent at zero, which is where both client generations leave it.

The opcodes our dispatch does NOT implement, checked against the AFP 2.x
set before the run, were the watch list: `FPGetForkParms` (13),
`FPMapID`/`FPMapName` (21/22), `FPLoginCont` (19), `FPChangePassword` (36).
The System 7.0 Finder asked for none of them. `FPOpenDir` (25) is correctly
absent: the server advertises fixed DIDs (volume signature 2), and a client
must not call it.

## The one oddity, and why it is not ours

Get Info on a 65 664-byte file reports **"Size: 1,024 MB on disk (65,664
bytes used)"**. That looked like a malformed parameter reply — until the
same client, on the same desktop, was pointed at netatalk's volume:

| Server | File | Finder's Get Info |
|---|---|---|
| POM68K (`AppleShare`) | `POM68KProber.bin`, 65 664 B | 1,024 MB on disk (65,664 bytes used) |
| netatalk (`Input`) | `harry.bin`, 6 413 568 B | 1,030 MB on disk (6,413,568 bytes used) |

Both volumes also report the same header — *zero K in disk, 2,047.9 MB
available*. It is the System 7.0 Finder's own arithmetic on a 2 GB AFP
volume, reproduced identically by the reference implementation. Nothing to
fix here; recorded so the next person who sees it does not go looking.

| File | What it is |
|---|---|
| `s7_list_zoom.png` | the Chooser listing POM68K (netatalk) and POMTEST (ours) |
| `s7_desktop_icons.png` | both volumes mounted on one System 7.0 desktop |
| `s7_getinfo_crop.png` | Get Info on our volume |
| `s7_ni_crop.png` | Get Info on netatalk's, same absurd "on disk" |
| `s7_server2_crop.png` | our server at the end: session, bytes, no refusals |

(`*.png` is gitignored: local evidence.)

## Reproducing

```bash
sudo tools/netatalk2/appleshare.sh                    # the segment's router
POM68K_LTOUDP=1 POM68K_APPLETALK=1 ./build/POM68K <Plus ROM> hdv/HD20SC.vhd
#   AppleTalk window → Configuration des services → rename to POMTEST →
#   Appliquer, so the Chooser can tell our server from netatalk's
DISPLAY=:77 ./minivmac disk1.dsk                      # System 7.0 on a Plus
```
