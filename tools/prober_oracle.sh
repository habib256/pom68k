#!/usr/bin/env bash
# POM68K — Retro68 differential oracle, LC II (TODO § Preuve).
#
# The guest Prober (dev/prober) reports Gestalt, low memory, a bus-error
# topology and the device inventory as a TSV next to itself at launch. This
# script boots ONE prepared image — the LC II reference volume with the
# Prober in Startup Items — under POM68K and under MAME `maclc2` (romset
# built from the tree's own ROM), pulls both reports out with hfsutils and
# diffs them.
#
#   tools/prober_oracle.sh <work-dir> [mame-seconds]
#
# Needs: build/lcii_prober_oracle, dev/prober/build/POM68KProber.bin,
# the Retro68 hfsutils (dev/Retro68-build/toolchain/bin), `mame` on PATH,
# roms/512KB ROMs/…35C28F5F… and roms/egret/*.bin.
#
# Read the diff with the MAME model's limits in mind: maclc raises no bus
# error on unmapped space, so a probe MAME calls "present" there is not
# evidence (CHANGELOG 2026-10-02 (night)).
set -euo pipefail

work=${1:?usage: tools/prober_oracle.sh <work-dir> [mame-seconds]}
secs=${2:-90}
root=$(cd "$(dirname "$0")/.." && pwd)
hfs="$root/dev/Retro68-build/toolchain/bin"
report=':System 7.5.5:Startup Items:POM68K Prober.txt'
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

# POM68K half (writes prepared.hd and pom68k.hd).
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
(cd "$work" && mame -rompath roms -cfg_directory cfg maclc2 -ramsize 10M -hard1 mame.hd \
     -video none -sound none -nothrottle -seconds_to_run "$secs" >/dev/null)

extract() {
    "$hfs/hmount" "$1" 1 >/dev/null
    "$hfs/hcopy" -t "$report" "$2.raw"
    "$hfs/humount" >/dev/null
    iconv -f MACROMAN -t UTF-8 "$2.raw" | tr '\r' '\n' > "$2"
}
extract "$work/mame.hd"   "$work/mame.tsv"
extract "$work/pom68k.hd" "$work/pom68k.tsv"
diff "$work/mame.tsv" "$work/pom68k.tsv" || true
