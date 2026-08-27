#!/usr/bin/env python3
"""How many gates of a ctest run actually EXECUTED, and which abstained.

Why this exists
---------------
`ctest` prints `228/228 tests passed`, and that number is quoted in this
project's documentation as if it meant 228 behaviours were exercised. It does
not. Most gates soft-skip when a private ROM, disk image, firmware dump or
external corpus is missing: they print `SKIP: …` and exit 0, so a host with no
assets at all reports the same green as the host that owns every byte.

The 2026-08-26 review named this (`TODO.md` § 0·B item 4): the honesty was in
prose, not in a counter. This is the counter. It reads the per-gate output
`ctest` already writes to `Testing/Temporary/LastTest.log` — no rerun, no new
gate protocol, and the marker is the one `tools/measure_gate_ram.py` has always
used: the literal string `SKIP` in a gate's own output.

    tools/gate_execution_census.py                      # after any ctest run
    tools/gate_execution_census.py --log saved.log      # a preserved copy
    tools/gate_execution_census.py --fail-on-skip       # calibration runs

Trap worth knowing: `LastTest.log` is overwritten by EVERY ctest invocation,
including a one-gate `ctest -R docs_test`. Copy it the moment a full run ends,
or the census is of the last small run instead.
"""

import argparse
import os
import re
import sys

BLOCK = re.compile(r"^(\d+)/(\d+) Test: (\S+)$", re.M)


def census(text):
    """Return (name, status, reason) per gate, in log order."""
    marks = list(BLOCK.finditer(text))
    rows = []
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        body = text[m.start():end]
        name = m.group(3)
        skip = re.search(r"^.*\bSKIP\b.*$", body, re.M)
        if skip:
            reason = skip.group(0).strip()
            reason = reason.split("SKIP", 1)[1].lstrip(": ").strip()
            rows.append((name, "skipped", reason))
        elif "Test Failed" in body or "***Failed" in body:
            rows.append((name, "failed", ""))
        else:
            rows.append((name, "executed", ""))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--log", help="a preserved LastTest.log instead of the "
                                  "build directory's current one")
    ap.add_argument("--fail-on-skip", action="store_true",
                    help="exit 1 if any gate abstained — for a run that is "
                         "supposed to prove coverage, not just exit 0")
    args = ap.parse_args()

    path = args.log or os.path.join(args.build_dir,
                                    "Testing", "Temporary", "LastTest.log")
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as exc:
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        return 2

    rows = census(text)
    if not rows:
        print(f"error: {path} contains no gate output — was it overwritten by "
              f"a later ctest -R run?", file=sys.stderr)
        return 2

    skipped = [r for r in rows if r[1] == "skipped"]
    failed = [r for r in rows if r[1] == "failed"]
    executed = [r for r in rows if r[1] == "executed"]

    for name, _, reason in skipped:
        print(f"SKIP     {name:<34} {reason}")
    for name, _, _ in failed:
        print(f"FAIL     {name}")
    print(f"\n{len(executed)} executed, {len(skipped)} soft-skipped, "
          f"{len(failed)} failed, {len(rows)} gates in the log")
    if skipped:
        print("A soft-skipped gate exited 0 and was counted green. It proved "
              "nothing about the behaviour it names.")
    # A census that reports a failure and exits 0 is the same shape of lie as
    # a green tier full of skips: report it, and say so in the exit code.
    if failed:
        return 1
    if skipped and args.fail_on_skip:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
