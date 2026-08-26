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

if [ "${#runner[@]}" -gt 0 ]; then
    "${runner[@]}" "$exe" "--gui-smoke=$report" "$missing_rom" "" ""
else
    "$exe" "--gui-smoke=$report" "$missing_rom" "" ""
fi
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL: POM68K GUI smoke scenario exited $status" >&2
    exit "$status"
fi

if ! grep -qx 'result=PASS' "$report"; then
    echo "FAIL: GUI smoke report does not attest the complete lifecycle" >&2
    sed -n '1,40p' "$report" >&2
    exit 1
fi

sed -n '1,40p' "$report"
