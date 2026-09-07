#!/usr/bin/env bash
# POM68K — behavioural GUI smoke gate wrapper.

set -uo pipefail

exe=${1:-}
report=${2:-}
missing_rom=${3:-}

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

smoke_log="$report.log"
if [ "${#runner[@]}" -gt 0 ]; then
    "${runner[@]}" "$exe" "--gui-smoke=$report" "$missing_rom" "" "" 2>&1 | tee "$smoke_log"
    status=${PIPESTATUS[0]}
else
    "$exe" "--gui-smoke=$report" "$missing_rom" "" "" 2>&1 | tee "$smoke_log"
    status=${PIPESTATUS[0]}
fi
if [ "$status" -ne 0 ]; then
    # A runner with no display cannot open a GL window at all: GLFW reports
    # "Failed to find a suitable pixel format" (NSGL 65545 on a headless
    # macOS runner, the WGL twin on a Windows one). That is the runner's
    # shape, not the GUI's lifecycle, and the first MSVC asset-none run
    # (2026-09-07) read it as a red. Same verdict as the no-DISPLAY Linux
    # case above: SKIP, loudly.
    if grep -q "Failed to find a suitable pixel format" "$smoke_log"; then
        echo "SKIP: no GL pixel format on this runner (headless) — the GUI smoke needs a display"
        exit 77
    fi
    echo "FAIL: POM68K GUI smoke scenario exited $status" >&2
    exit "$status"
fi

if ! grep -qx 'result=PASS' "$report"; then
    echo "FAIL: GUI smoke report does not attest the complete lifecycle" >&2
    sed -n '1,40p' "$report" >&2
    exit 1
fi

sed -n '1,40p' "$report"
