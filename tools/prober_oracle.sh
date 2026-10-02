#!/usr/bin/env bash
# POM68K — Retro68 differential oracle, LC II (TODO § Preuve).
#
# The guest Prober (dev/prober) reports Gestalt, low memory, a bus-error
# topology and the device inventory as a TSV next to itself at launch. This
# script boots ONE prepared image — the LC II's locked reference volume
# (hdv/ref/System 7.1 HD.dsk) with the Prober in Startup Items — under
# POM68K and under MAME `maclc2` (romset built from the tree's own ROM),
# reads both reports back out of the images and diffs them.
#
#   tools/prober_oracle.sh <work-dir> [mame-seconds]
#
# <work-dir>/mame.tsv is what `lcii_prober_oracle_etalon` compares POM68K
# with: after a change to the Prober, the volume or the rig, copy it to
# tools/prober_oracle_maclc2.tsv.
#
# Needs: build/lcii_prober_oracle, dev/prober/build/POM68KProber.bin, MAME,
# roms/512KB ROMs/…35C28F5F… and roms/egret/*.bin. MAME is `$MAME` if set,
# else `mame` on PATH, else the flatpak org.mamedev.MAME (the x86-64 host's).
#
# Read the diff with the MAME model's limits in mind: maclc raises no bus
# error on unmapped space, so a probe MAME calls "present" there is not
# evidence (CHANGELOG 2026-10-02 (night)); the gate's unjudged list says
# which fields are not compared and why.
set -euo pipefail

work=${1:?usage: tools/prober_oracle.sh <work-dir> [mame-seconds]}
secs=${2:-90}
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$work/roms/maclc2"
work=$(cd "$work" && pwd)
if [ -n "${MAME:-}" ]; then read -ra mame <<< "$MAME"
elif command -v mame >/dev/null; then mame=(mame)
else mame=(flatpak run "--filesystem=$work" org.mamedev.MAME)
fi

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

# POM68K half (writes prepared.hd, pom68k.hd and pom68k.tsv).
(cd "$root" && ./build/lcii_prober_oracle "$work")

# MAME half on a copy of the same prepared image. RAM matched to POM68K's
# V8 default (10 MB), and the FPU socket filled as POM68K's default has it
# (maclc's ":config" port bit 0) — without it hwCfg bit 12 differs and the
# bare POM68K LC II does not reach the Finder to compare against.
mkdir -p "$work/cfg"
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
cp "$work/prepared.hd" "$work/mame.hd"
# The disk sits at SCSI ID 0 as on POM68K (MAME's default is ID 6), the
# blank "Infinite HD" companion at ID 1 as on POM68K (InfiniteHdCompanion.h).
(cd "$work" && "${mame[@]}" -rompath roms -cfg_directory cfg maclc2 -ramsize 10M \
     -scsi:0 harddisk -scsi:1 harddisk -scsi:6 "" \
     -hard1 mame.hd -hard2 companion.hd \
     -video none -sound none -nothrottle -seconds_to_run "$secs" >/dev/null)

"$root/build/lcii_prober_oracle" --extract "$work/mame.hd" "$work/mame.tsv"
diff "$work/mame.tsv" "$work/pom68k.tsv" || true
