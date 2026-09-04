#!/usr/bin/env python3
"""Run lcii_simcity_census once and time each phase, without touching the tool.

The census prints its phase markers on STDERR (unbuffered) via
Engine::censusPhase(), and only ONE phase carries a wall of its own
(`app-load: wall=...`, the two-emulated-minute QuickDraw redraw loop that
CHANGELOG 2026-09-02 (sixth) profiled). Every other phase boundary is
recoverable from the arrival time of its marker line, which is what this does:
stamp each line as it arrives, then difference the markers.

The phase's wall therefore includes that phase's own histogram dump. That is
identical work in both arms, so it cancels in the A/B; it is stated because an
absolute number from here is not comparable with anything else.

  run_census.py <binary> <cwd> <stamped-log-out>
prints one KEY=VALUE per line on stdout for the caller to parse.
"""
import os
import re
import subprocess
import sys
import time

binary, cwd, logpath = sys.argv[1], sys.argv[2], sys.argv[3]

env = dict(os.environ)
proc = subprocess.Popen(
    ["stdbuf", "-o0", "-e0", binary],
    cwd=cwd, env=env,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    text=True, bufsize=1)

t0 = time.monotonic()
marks = []          # (phase name, elapsed at its marker)
info = {}
phase_re = re.compile(r"════ census phase '([^']*)' ════")

with open(logpath, "w") as log:
    for line in proc.stdout:
        el = time.monotonic() - t0
        log.write("%9.3f %s" % (el, line))
        m = phase_re.search(line)
        if m:
            marks.append((m.group(1), el))
            continue
        m = re.search(r"app-load: wall=([0-9.]+)s fp=([0-9a-f]+) screen=([0-9a-f]+)", line)
        if m:
            info["APPWALL"], info["FP"], info["SCREEN"] = m.groups()
        m = re.search(r"launch: ([0-9.]+)% of the screen changed, SCSI \+(\d+)", line)
        if m:
            info["LAUNCHMOVED"], info["LAUNCHSCSI"] = m.groups()
        m = re.search(r"boot: Finder (\S+), SCSI (\d+) commands", line)
        if m:
            info["BOOTFINDER"], info["BOOTSCSI"] = m.groups()
        m = re.search(r"halted=(\d+), SCSI (\d+) commands total", line)
        if m:
            info["HALTED"], info["SCSITOTAL"] = m.groups()

rc = proc.wait()
total = time.monotonic() - t0

print("RC=%d" % rc)
print("TOTAL=%.3f" % total)
prev = 0.0
for name, el in marks:
    print("PHASE_%s=%.3f" % (name.replace("(", "_").replace(")", "_")
                                 .replace(":", "_").replace(" ", "_")
                                 .replace("-", "_"), el - prev))
    prev = el
for k, v in info.items():
    print("%s=%s" % (k, v))
