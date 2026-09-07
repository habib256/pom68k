#!/usr/bin/env bash
# Equal-arms AppleTalk hub cost in the GUI (TODO § C.5): same image, same
# turbo, same launch; only POM68K_APPLETALK differs. A B B A, N samples each.
set -uo pipefail
ROM="roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM"
IMG="hdv/GISTPERSO-boot.vhd"
run() {  # $1 = appletalk 0|1, $2 = label
    POM68K_TURBO=1 POM68K_SPEED_LOG=1 POM68K_SPEED_LOG_SKIP=40 POM68K_SPEED_LOG_COUNT=40 \
    POM68K_APPLETALK=$1 POM68K_AUDIO=0 ./build/POM68K --machine-profile=lcii "$ROM" "$IMG" \
        2>&1 | grep "\[gui-speed\]" | awk -v l="$2" '{split($4,a,"="); s+=a[2]; n++} END {if (n) printf "%s n=%d mean_ratio=%.4f\n", l, n, s/n; else printf "%s n=0\n", l}'
}
for round in 1 2; do
    run 1 "hub-on "; run 0 "hub-off"; run 0 "hub-off"; run 1 "hub-on "
done
