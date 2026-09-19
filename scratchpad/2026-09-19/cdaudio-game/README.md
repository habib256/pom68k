# A pressed CD game, its music and its voices, on a 68040

Jalon 4's exit criterion asked for "un jeu CD avec audio joué de bout en
bout". It is met, and by the most direct measurement there is: the machine
was handed to the user, who played it and reported **"les sons et paroles
ainsi que les musiques fonctionnent parfaitement"**.

## The session

| | |
|---|---|
| Profile | **Macintosh LC 475** (MemcJr board, the Quadra 605's twin), 68040 @ 25 MHz, Moira + 040 MMU |
| Screen | 640×480 @ 8 bpp |
| Boot | `hdv/MacOS-8.1-boot.vhd` |
| CD bay (SCSI 1) | a pressed mixed-mode game disc — **22 tracks: 1 data (40.3 MB, 2048-byte blocks) + 21 audio**, first audio at 04:35:09, last starting 42:22:32 (`disc_toc.txt`) |
| Launched with | `DISPLAY=:1 ./build/POM68K <FF7439EE ROM> hdv/MacOS-8.1-boot.vhd <disc>.cue` |

The game's music, its speech and its sound effects all come off the CD's
Red Book tracks — not from files in the data partition — so what played is
the CD-DA path end to end: TOC, PLAY AUDIO, the transport on machine time,
and the sectors reaching the host DAC.

`battlechess_lc475.png` is the user's own screen capture of the running
game (local: `*.png` is gitignored).

## What it took, and what it says

The first launch of the same disc, on a binary built one commit earlier,
printed:

```
CD-ROM: …BIN declares 512-byte blocks — attaching it as a removable disk, not a CD
```

This disc carries an Apple partition map whose driver descriptor declares
512-byte blocks, and our reader believed the map over the cue sheet — so
the CD arrived as a removable hard disk, with no TOC and no audio. That is
the defect fixed the same day (CHANGELOG 2026-09-19 (third)), found with
`cd_toc_probe` on a different pressed disc; this game is its second
witness, and the reason the criterion could be met at all.

Worth recording: the archive page for this disc states that the image has
to be burned to a physical CD because emulation cannot reach the audio
partition. On this emulator, with the sheet believed over the map, it can.

## Reproducing

```bash
build/cd_toc_probe <disc>.cue                      # what our reader sees
DISPLAY=:1 ./build/POM68K "roms/1MB ROMs/1993-10 - FF7439EE - …ROM" \
    hdv/MacOS-8.1-boot.vhd <disc>.cue
```

The disc itself is a private input under `cd/`, which `.gitignore` keeps
out of the repository — like every ROM and system medium here.
