#!/usr/bin/env bash
# POM68K — Retro68 differential oracle (TODO § Preuve).
#
# The guest Prober (dev/prober) reports Gestalt, low memory, a bus-error
# topology, AppleTalk and the device inventory as a TSV next to itself at
# launch. This script boots ONE prepared image — the profile's locked
# reference volume with the Prober in Startup Items — under POM68K and
# under MAME (romset built from the tree's own ROM), reads both reports back
# out of the images and diffs them.
#
#   tools/prober_oracle.sh <machine> <work-dir> [mame-seconds]
#
#   lcii  MAME maclc2,   hdv/ref/System 7.1 HD.dsk, 10 MB, FPU socket filled
#   q605  MAME macqd605, hdv/ref/MacOS-8.1-boot.vhd, 32 MB
#   q800  MAME macqd800, the same volume, 32 MB, bios "original" (F1A6F343)
#   c650  MAME macct650, likewise
#   q630  MAME macqd630, the same volume, 32 MB
#   q700  MAME macqd700, hdv/ref/System 7.1 HD.dsk, 8 MB
#   lc475, lc575  MAME maclc475 / maclc575, as q605 (their IDs, 68LC040)
#   q650, q610, c610  MAME macqd650 / macqd610 / macct610, as q800
#   lc580  MAME maclc580 (bios "older"), as q630, 68LC040
#   q900  MAME macqd900, as q700, with the IOPs and the Egret
#   q950  MAME macqd950, the same board at 33 MHz, its own ROM
#   lc    MAME maclc, hdv/ref/System 7.1 HD.dsk, 10 MB, FPU socket filled
#   lc3   MAME maclc3, the same volume, 8 MB, FPU socket filled
#
# <work-dir>/mame.tsv is what `<profile>_prober_oracle_etalon` compares
# POM68K with: after a change to the Prober, the volume or the rig, copy it
# to tools/prober_oracle_<mame-system>.tsv.
#
# Needs: build/prober_oracle, dev/prober/build/POM68KProber.bin, MAME, the
# profile's ROM and its MCU firmware (roms/egret, roms/cuda, roms/adbmodem).
# MAME is `$MAME` if set, else `mame` on PATH, else the flatpak
# org.mamedev.MAME (the x86-64 host's).
#
# Read the diff with the MAME model's limits in mind: maclc raises no bus
# error on unmapped space, so a probe MAME calls "present" there is not
# evidence (CHANGELOG 2026-10-02 (night)); the gate's unjudged list says
# which fields are not compared and why.
set -euo pipefail

usage="usage: tools/prober_oracle.sh <lcii|lc|lc3|q605|lc475|lc575|q800|q650|q610|c650|c610|q630|lc580|q700|q900|q950> <work-dir> [mame-seconds]"
machine=${1:?$usage}
work=${2:?$usage}
secs=${3:-90}
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$work/roms" "$work/cfg"
work=$(cd "$work" && pwd)
if [ -n "${MAME:-}" ]; then read -ra mame <<< "$MAME"
elif command -v mame >/dev/null; then mame=(mame)
else mame=(flatpak run "--filesystem=$work" org.mamedev.MAME)
fi

extra=()
bus=scsi                                      # MAME's SCSI slot prefix
case "$machine" in
lcii)
    system=maclc2
    mkdir -p "$work/roms/maclc2"
    # MAME's four byte lanes, from the tree's ROM (CRCs match maclc2's set).
    python3 - "$root" "$work/roms/maclc2" <<'EOF'
import sys
root, out = sys.argv[1], sys.argv[2]
rom = open(f"{root}/roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM", "rb").read()
for k, n in enumerate(["341-0476_ue2-hh.bin", "341-0475_ud2-mh.bin",
                       "341-0474_uc2-ml.bin", "341-0473_ub2-ll.bin"]):
    open(f"{out}/{n}", "wb").write(rom[k::4])
EOF
    cp "$root"/roms/egret/*.bin "$work/roms/maclc2/"
    # RAM matched to POM68K's V8 default (10 MB), and the FPU socket filled
    # as POM68K's default has it (maclc's ":config" port bit 0) — without it
    # hwCfg bit 12 differs and the bare POM68K LC II does not reach the
    # Finder to compare against.
    cat > "$work/cfg/maclc2.cfg" <<'CFG'
<?xml version="1.0"?>
<mameconfig version="10">
    <system name="maclc2">
        <input>
            <port tag=":config" type="CONFIG" mask="1" defvalue="0" value="1" />
        </input>
    </system>
</mameconfig>
CFG
    ram=10M
    ;;
q605|lc475|lc575)
    # maclc475 and maclc575 are clones of macqd605: one romset, one ROM.
    case "$machine" in q605) system=macqd605 ;; lc475) system=maclc475 ;; *) system=maclc575 ;; esac
    mkdir -p "$work/roms/macqd605" "$work/roms/cuda"
    cp "$root/roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM" \
       "$work/roms/macqd605/ff7439ee.bin"
    cp "$root"/roms/cuda/*.bin "$work/roms/cuda/"
    ram=32M                                   # POM68K's Q605 gates run 32 MB
    ;;
q800|q650|q610|c650|c610)
    # macqd650, macqd610, macct650 and macct610 are clones of macqd800: one
    # romset, and the tree's F1A6F343 is MAME's bios "original" (its default
    # is the later F1ACAD13).
    case "$machine" in
    q800) system=macqd800 ;; q650) system=macqd650 ;; q610) system=macqd610 ;;
    c650) system=macct650 ;; *) system=macct610 ;;
    esac
    mkdir -p "$work/roms/macqd800" "$work/roms/adbmodem"
    cp "$root/roms/1MB ROMs/1993-02 - F1A6F343 - Quadra, Centris 610,650.ROM" \
       "$work/roms/macqd800/f1a6f343.rom"
    cp "$root/roms/adbmodem/342s0440-b.bin" "$work/roms/adbmodem/"
    extra=(-bios original)
    ram=32M
    ;;
q630|lc580)
    # maclc580 is a clone of macqd630; its bios "older" is the 06684214 ROM.
    if [ "$machine" = q630 ]; then system=macqd630; else system=maclc580; fi
    mkdir -p "$work/roms/macqd630" "$work/roms/cuda"
    cp "$root/roms/1MB ROMs/1994-07 - 06684214 - LC,Quadra,Performa 630.ROM" \
       "$work/roms/macqd630/06684214.bin"
    cp "$root"/roms/cuda/*.bin "$work/roms/cuda/"
    bus=f108:scsi                             # the internal disk is IDE there
    # MAME plugs an imageless IDE disk into ata:0 by default (f108.cpp); with
    # it the SCSI disk's driver lands at unit 53, without it at 32 + ID as
    # on POM68K's 630, which has no IDE device (CHANGELOG 2026-10-03).
    extra=(-f108:ata:0 "")
    [ "$machine" = lc580 ] && extra+=(-bios older)
    ram=32M
    ;;
q700)
    system=macqd700
    mkdir -p "$work/roms/macqd700" "$work/roms/adbmodem"
    cp "$root/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM" \
       "$work/roms/macqd700/420dbff3.rom"
    cp "$root/roms/adbmodem/342s0440-b.bin" "$work/roms/adbmodem/"
    ram=8M                    # MAME 0.287's macqd700 is black with 20 or 36
    ;;
lc|lc3)
    # maclc (68020, V8) and maclc3 (Sonora): one ROM file each, the Egret,
    # the FPU socket filled as on lcii.
    if [ "$machine" = lc ]; then
        system=maclc; ram=10M
        rom="$root/roms/512KB ROMs/1990-10 - 350EACF0 - Mac LC.ROM"; romfile=350eacf0.rom
    else
        system=maclc3; ram=8M
        rom="$root/roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM"; romfile=ecbbc41c.rom
    fi
    mkdir -p "$work/roms/$system"
    cp "$rom" "$work/roms/$system/$romfile"
    cp "$root"/roms/egret/*.bin "$work/roms/$system/"
    cat > "$work/cfg/$system.cfg" <<CFG
<?xml version="1.0"?>
<mameconfig version="10">
    <system name="$system">
        <input>
            <port tag=":config" type="CONFIG" mask="1" defvalue="0" value="1" />
        </input>
    </system>
</mameconfig>
CFG
    ;;
q950)
    system=macqd950
    mkdir -p "$work/roms/macqd950"
    cp "$root/roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM" "$work/roms/macqd950/3dc27823.rom"
    cp "$root"/roms/egret/344s0100.bin "$root"/roms/egret/341s085[01].bin "$work/roms/macqd950/"
    ram=8M
    ;;
q900)
    # Its own romset: the same ROM, the IOPs' 344S0100 and the Egret.
    system=macqd900
    mkdir -p "$work/roms/macqd900"
    cp "$root/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM" \
       "$work/roms/macqd900/420dbff3.rom"
    cp "$root"/roms/egret/344s0100.bin "$root"/roms/egret/341s085[01].bin "$work/roms/macqd900/"
    ram=8M                    # like the Quadra 700: System 7.1 in 8 MB
    ;;
*) echo "$usage" >&2; exit 2 ;;
esac

# POM68K half (writes prepared.hd, pom68k.hd, pom68k.tsv, and companion.hd
# when the volume is an Infinite Mac image).
(cd "$root" && ./build/prober_oracle "$machine" "$work")

# MAME half on a copy of the same prepared image. The disk sits at SCSI ID 0
# as on POM68K (MAME's default is ID 6); the blank "Infinite HD" companion,
# when there is one, at ID 1 as on POM68K (InfiniteHdCompanion.h).
cp "$work/prepared.hd" "$work/mame.hd"
disks=("-$bus:0" harddisk "-$bus:6" "" -hard1 mame.hd)
[ -f "$work/companion.hd" ] && disks=("-$bus:0" harddisk "-$bus:1" harddisk "-$bus:6" ""
                                      -hard1 mame.hd -hard2 companion.hd)
(cd "$work" && "${mame[@]}" -rompath roms -cfg_directory cfg "$system" -ramsize "$ram" "${extra[@]}" \
     "${disks[@]}" -video none -sound none -nothrottle -seconds_to_run "$secs" >/dev/null)

"$root/build/prober_oracle" --extract "$work/mame.hd" "$work/mame.tsv"
diff "$work/mame.tsv" "$work/pom68k.tsv" || true
