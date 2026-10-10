#!/usr/bin/env bash
# POM68K — behavioural GUI smoke gate wrapper.

set -uo pipefail

exe=${1:-}
report=${2:-}
missing_rom=${3:-}
# lifecycle (default): open/render/engine/save/close, relaunch intercepted.
# relaunch: the first generation stages the DaynaPort card and really
# re-executes; the second generation must attest the card and close.
# session: two session files with different media; the first generation
# opens the second through the Session menu's path and re-executes.
mode=${4:-lifecycle}
option="--gui-smoke=$report"
if [ "$mode" = relaunch ]; then option="--gui-smoke-relaunch=$report"; fi
if [ "$mode" = session ]; then option="--gui-smoke-session=$report"; fi

if [ -z "$exe" ] || [ ! -x "$exe" ]; then
    echo "SKIP: POM68K GUI executable is not built"
    exit 77
fi
if [ -z "$report" ] || [ -z "$missing_rom" ]; then
    echo "FAIL: GUI smoke wrapper needs report and missing-ROM paths" >&2
    exit 2
fi

# Never accept a report left by an older invocation as current evidence.
: > "$report"

runner=()
case "$(uname -s)" in
    Linux)
        if [ -z "${DISPLAY:-}" ]; then
            if command -v xvfb-run >/dev/null 2>&1; then
                runner=(xvfb-run -a)
            else
                echo "SKIP: no DISPLAY and xvfb-run is unavailable"
                exit 77
            fi
        fi
        ;;
esac

inputs=("$missing_rom" "" "")
if [ "$mode" = session ]; then
    # A blank 128 KB ROM starts the Plus board (the missing-ROM machine of
    # the other modes, with a file to name); each session has its own
    # blank 800K floppy. Directory and names carry spaces and UTF-8.
    sessions="$report.sessions/répertoire des sessions"
    rm -rf "$report.sessions"
    mkdir -p "$sessions/disques"
    head -c 131072 /dev/zero > "$sessions/plus.rom"
    for name in first second; do
        head -c 819200 /dev/zero > "$sessions/disques/$name floppy.dsk"
        printf 'pom68k-session 1\nprofile = plus\nrom = plus.rom\nmedia = disques/%s floppy.dsk\n' \
            "$name" > "$sessions/$name.pomsession"
    done
    inputs=("--session=$sessions/first.pomsession")
fi

smoke_log="$report.log"
if [ "${#runner[@]}" -gt 0 ]; then
    "${runner[@]}" "$exe" "$option" "${inputs[@]}" 2>&1 | tee "$smoke_log"
    status=${PIPESTATUS[0]}
else
    "$exe" "$option" "${inputs[@]}" 2>&1 | tee "$smoke_log"
    status=${PIPESTATUS[0]}
fi
if [ "$status" -ne 0 ]; then
    # A runner with no display cannot open a GL window at all: GLFW reports
    # "Failed to find a suitable pixel format" (NSGL 65545 on a headless
    # macOS runner, the WGL twin on a Windows one). That is the runner's
    # shape, not the GUI's lifecycle, and the first MSVC asset-none run
    # (2026-09-07) read it as a red. Same verdict as the no-DISPLAY Linux
    # case above: SKIP, loudly.
    # GLFW 65542 "API unavailable" (WGL: the driver does not support OpenGL,
    # the Windows runner) and 65545 "format unavailable" (NSGL, a headless
    # macOS runner) are the same fact: no GL surface on this machine.
    if grep -q -E "GLFW error 6554[25]" "$smoke_log"; then
        echo "SKIP: no OpenGL surface on this runner (headless) — the GUI smoke needs a display"
        exit 77
    fi
    echo "FAIL: POM68K GUI smoke scenario exited $status" >&2
    exit "$status"
fi

if ! grep -qx 'result=PASS' "$report"; then
    echo "FAIL: GUI smoke report does not attest the complete lifecycle" >&2
    if [ "$mode" = session ]; then
    # Generation 2's report, generation 1 in the log: each came up with its
    # own session's medium, resolved against the session's directory.
    dir=$(cd "$sessions" && pwd)
    if ! grep -qx 'generation=2' "$report" ||
       ! grep -qxF "session=$dir/second.pomsession" "$report" ||
       ! grep -qxF "media=$dir/disques/second floppy.dsk" "$report" ||
       ! grep -qF "gui-smoke: generation 1 session=$dir/first.pomsession media=$dir/disques/first floppy.dsk" "$smoke_log" ||
       ! grep -q 'gui-smoke: generation 1 PASS, re-executing' "$smoke_log"; then
        echo "FAIL: the two sessions were not each opened with their own media" >&2
        sed -n '1,40p' "$report" >&2
        exit 1
    fi
fi

sed -n '1,40p' "$report" >&2
    exit 1
fi
if [ "$mode" = relaunch ]; then
    # The report is the second generation's; the first is in the log. A
    # process that never re-executed would have left a generation=1 report.
    if ! grep -qx 'generation=2' "$report" || ! grep -qx 'card_seen=1' "$report" ||
       ! grep -qx 'daynaport_id=3' "$report" ||
       ! grep -q 'gui-smoke: generation 1 PASS, re-executing' "$smoke_log"; then
        echo "FAIL: the relaunch was not observed end to end (generation 1 exec, generation 2 with the card)" >&2
        sed -n '1,40p' "$report" >&2
        exit 1
    fi
fi

sed -n '1,40p' "$report"
