#!/usr/bin/env python3
"""Measure the peak memory and wall time of every CTest gate, per host.

Why this exists
---------------
A full `ctest` run costs 4 h 30 SEQUENTIALLY, and that is why it is run
rarely — the 2026-08-16 pass found ten red gates in five unrelated causes
after nine days without one.  Nothing in `CMakeLists.txt` actually *requires*
sequential execution: there is no `RUN_SERIAL`, no `RESOURCE_LOCK`, and
`ScsiDisk::open()` loads the disk image whole into memory with write-back off
(`ScsiDisk.h`, "tests run write-back off"), so gates are readers of the shared
assets, not writers.

The real ceiling is RAM: each etalon holds its whole image resident, so N
gates in parallel hold N images.  CTest can schedule around that — resource
groups — but only if somebody tells it what each gate costs.  This script
produces that number instead of guessing it, and it is re-runnable on any
host, the same way `performance_budgets.tsv` pins performance policy per
host rather than assuming one.

The trap this script refuses to fall into
-----------------------------------------
Most gates SOFT-SKIP without their ROM/disk assets, and a skipped gate uses
almost no memory.  Measuring on a machine without assets and writing the
result into a budget table would record "etalon = 8 MB" and quietly build a
scheduler that oversubscribes RAM on the machine that *has* the assets.  So
every row carries `ran` or `skipped`, taken from the gate's own output, and
a skipped row is NOT a measurement of that gate — only of its startup.
`--fail-on-skip` makes that refusal loud for a calibration run.

Usage
-----
    tools/measure_gate_ram.py --build-dir build -o gate_ram.tsv
    tools/measure_gate_ram.py --build-dir build -L asset-none
    tools/measure_gate_ram.py --build-dir build -R '^q605_' --fail-on-skip

Output is TSV on stdout (or -o), one row per gate, widest first:

    name  status  peak_rss_kb  wall_s  exit
"""

import argparse
import json
import os
import subprocess
import sys
import time


def ctest_tests(build_dir, label=None, regex=None):
    """The gate roster as CTest itself resolves it, commands and all."""
    cmd = ["ctest", "--show-only=json-v1"]
    if label:
        cmd += ["-L", label]
    if regex:
        cmd += ["-R", regex]
    out = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True,
                         check=True).stdout
    return json.loads(out).get("tests", [])


def props(test):
    """CTest hands properties back as a list of {name, value}."""
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def run_one(test, build_dir, timeout_cap):
    """Run one gate and return (status, peak_rss_kb, wall_s, exit_code).

    Peak RSS comes from wait4()'s rusage for THIS child specifically, not
    from RUSAGE_CHILDREN, which accumulates across every child already
    reaped and would report the high-water mark of the whole run.
    """
    p = props(test)
    cwd = p.get("WORKING_DIRECTORY", build_dir)

    env = os.environ.copy()
    for kv in p.get("ENVIRONMENT", []):
        key, _, value = kv.partition("=")
        env[key] = value

    # A calibration run should not inherit the gate's own 30-minute timeout
    # by accident; the cap is explicit and reported as a status of its own.
    timeout = float(p.get("TIMEOUT", timeout_cap) or timeout_cap)
    timeout = min(timeout, timeout_cap)

    started = time.monotonic()
    proc = subprocess.Popen(test["command"], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # Cannot use communicate(): we need wait4()'s rusage for this pid.
        # Read the pipe first so a chatty gate cannot deadlock on a full one.
        output = proc.stdout.read()
        _, status, usage = os.wait4(proc.pid, 0)
    except Exception:
        proc.kill()
        proc.wait()
        return "error", 0, time.monotonic() - started, -1
    wall = time.monotonic() - started

    code = os.waitstatus_to_exitcode(status)
    text = output.decode("utf-8", "replace")
    if wall >= timeout:
        state = "timeout"
    elif "SKIP" in text:
        state = "skipped"
    elif code != 0:
        state = "failed"
    else:
        state = "ran"
    return state, usage.ru_maxrss, wall, code


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("-L", "--label", help="only gates carrying this CTest label")
    ap.add_argument("-R", "--regex", help="only gates matching this regex")
    ap.add_argument("-o", "--output", help="write TSV here instead of stdout")
    ap.add_argument("--timeout-cap", type=float, default=3600.0,
                    help="per-gate ceiling in seconds (default 3600)")
    ap.add_argument("--fail-on-skip", action="store_true",
                    help="exit 1 if any gate soft-skipped: a skipped gate is "
                         "not a measurement, and a budget built from one "
                         "under-declares the host that has the assets")
    args = ap.parse_args()

    tests = ctest_tests(args.build_dir, args.label, args.regex)
    if not tests:
        print("no gates matched", file=sys.stderr)
        return 1

    rows = []
    for i, t in enumerate(tests, 1):
        print(f"[{i}/{len(tests)}] {t['name']}", file=sys.stderr, flush=True)
        state, rss, wall, code = run_one(t, args.build_dir, args.timeout_cap)
        rows.append((t["name"], state, rss, wall, code))

    rows.sort(key=lambda r: r[2], reverse=True)
    lines = ["name\tstatus\tpeak_rss_kb\twall_s\texit"]
    lines += [f"{n}\t{s}\t{r}\t{w:.2f}\t{c}" for n, s, r, w, c in rows]
    text = "\n".join(lines) + "\n"

    if args.output:
        with open(args.output, "w") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)

    ran = sum(1 for r in rows if r[1] == "ran")
    skipped = sum(1 for r in rows if r[1] == "skipped")
    bad = [r[0] for r in rows if r[1] in ("failed", "timeout", "error")]
    print(f"\n{ran} measured, {skipped} skipped (NOT measurements), "
          f"{len(bad)} failed/timed out", file=sys.stderr)
    if bad:
        print("  " + ", ".join(bad), file=sys.stderr)
    if skipped and args.fail_on_skip:
        print("error: --fail-on-skip and this host soft-skipped "
              f"{skipped} gate(s); calibrate where the assets are",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
