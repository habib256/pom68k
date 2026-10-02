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
#   tools/prober_oracle.sh <lcii|q605> <work-dir> [mame-seconds]
#
#   lcii  MAME maclc2,   hdv/ref/System 7.1 HD.dsk, 10 MB, FPU socket filled
#   q605  MAME macqd605, hdv/ref/MacOS-8.1-boot.vhd, 32 MB
#
# <work-dir>/mame.tsv is what `<profile>_prober_oracle_etalon` compares
# POM68K with: after a change to the Prober, the volume or the rig, copy it
# to tools/prober_oracle_<mame-system>.tsv.
#
# Needs: build/prober_oracle, dev/prober/build/POM68KProber.bin, MAME, the
# profile's ROM and its MCU firmware (roms/egret, roms/cuda). MAME is
# `$MAME` if set, else `mame` on PATH, else the flatpak org.mamedev.MAME
# (the x86-64 host's).
#
# Read the diff with the MAME model's limits in mind: maclc raises no bus
# error on unmapped space, so a probe MAME calls "present" there is not
# evidence (CHANGELOG 2026-10-02 (night)); the gate's unjudged list says
# which fields are not compared and why.
set -euo pipefail

usage="usage: tools/prober_oracle.sh <lcii|q605> <work-dir> [mame-seconds]"
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
q605)
    system=macqd605
    mkdir -p "$work/roms/macqd605" "$work/roms/cuda"
    cp "$root/roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM" \
       "$work/roms/macqd605/ff7439ee.bin"
    cp "$root"/roms/cuda/*.bin "$work/roms/cuda/"
    ram=32M                                   # POM68K's Q605 gates run 32 MB
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
disks=(-scsi:0 harddisk -scsi:6 "" -hard1 mame.hd)
[ -f "$work/companion.hd" ] && disks=(-scsi:0 harddisk -scsi:1 harddisk -scsi:6 ""
                                      -hard1 mame.hd -hard2 companion.hd)
(cd "$work" && "${mame[@]}" -rompath roms -cfg_directory cfg "$system" -ramsize "$ram" \
     "${disks[@]}" -video none -sound none -nothrottle -seconds_to_run "$secs" >/dev/null)

"$root/build/prober_oracle" --extract "$work/mame.hd" "$work/mame.tsv"
diff "$work/mame.tsv" "$work/pom68k.tsv" || true
