#!/usr/bin/env python3
# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
"""Run a CPU specification test against a list of known divergences.

Since 2026-11-23 the CPU core is Dirk Hoffmann's Moira. Several CPU tests
were written against the fork that preceded it and encode, besides the
architecture, the fork's continuation model and some WinUAE readings. Their
checks stay: a known list (tests/known/<test>.txt) records the failures that
are understood, each under a heading that says why (a design difference, an
oracle reading, an open defect). The gate then holds the rest:

  * a failure that is not listed, or listed fewer times, fails the gate;
  * a crash (signal) or a non-zero exit with no failure line fails it;
  * a listed failure that no longer occurs is reported, so that the list
    can be lowered.

A failure line is any output line with the word FAIL, other than the test's
own summary. File:line prefixes are removed, so that the list survives edits.

  known_failures.py --known tests/known/x.txt -- <test command...>
  known_failures.py --known tests/known/x.txt --write -- <command>   (rewrite
      the counts of the list, keeping its headings; review the diff)
"""
import argparse
import collections
import re
import subprocess
import sys

SUMMARY = re.compile(r"^\s*(?:\[[^\]]*\]\s*)?(?:FAIL(?:ED)?:?\s*\d*\s*(?:check\(s\))?\s*$|"
                     r"FAILED\s*$|[\w ]+: \d+ executed sequences, \d+ failure\(s\)\s*$)")
# A __FILE__ prefix in any spelling: relative, POSIX or Windows (D:\a\...\x.cpp).
LOCATION = re.compile(r"(?:[A-Za-z]:)?[^\s:]*\.(?:cpp|h):\d+:\s*")


def normalize(line):
    text = LOCATION.sub("", line)
    text = re.sub(r"\bFAIL\b", "", text)
    return re.sub(r"\s+", " ", text).strip()


def failures(output):
    found = collections.Counter()
    for line in output.splitlines():
        if not re.search(r"\bFAIL\b", line) or SUMMARY.match(line):
            continue
        found[normalize(line)] += 1
    return found


def read_known(path):
    known = collections.Counter()
    try:
        with open(path, encoding="utf-8") as f:
            for raw in f:
                if not raw.strip() or raw.startswith("#"):
                    continue
                count, _, message = raw.rstrip("\n").partition("\t")
                known[message] += int(count)
    except FileNotFoundError:
        pass
    return known


def write_known(path, found):
    """Keep the headings and the order of the existing list; append new rows."""
    lines, seen = [], set()
    try:
        with open(path, encoding="utf-8") as f:
            for raw in f:
                if not raw.strip() or raw.startswith("#"):
                    lines.append(raw.rstrip("\n"))
                    continue
                message = raw.rstrip("\n").partition("\t")[2]
                seen.add(message)
                if found.get(message):
                    lines.append(f"{found[message]}\t{message}")
    except FileNotFoundError:
        pass
    new = [m for m in sorted(found) if m not in seen]
    if new:
        lines.append("# Not yet classified (review and move under a heading):")
        lines.extend(f"{found[m]}\t{m}" for m in new)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--known", required=True)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = [c for c in args.command if c != "--"]
    # UTF-8 whatever the host locale: the lists are UTF-8, and a Windows
    # code page would misread (or refuse) the tests' non-ASCII output.
    result = subprocess.run(command, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    found = failures(output)
    if args.write:
        write_known(args.known, found)
        print(f"known_failures: wrote {sum(found.values())} failure(s) to {args.known}")
        return 0
    if result.returncode < 0:
        print(output[-4000:])
        print(f"known_failures: the test died with signal {-result.returncode}")
        return 1
    if result.returncode != 0 and not found:
        print(output[-4000:])
        print("known_failures: non-zero exit with no failure line")
        return 1
    known = read_known(args.known)
    new = {m: n - known.get(m, 0) for m, n in found.items() if n > known.get(m, 0)}
    fixed = {m: k - found.get(m, 0) for m, k in known.items() if found.get(m, 0) < k}
    for message, n in sorted(new.items()):
        print(f"NEW  {n} x {message}")
    for message, n in sorted(fixed.items()):
        print(f"note {n} x no longer fails (lower {args.known}): {message}")
    total, listed = sum(found.values()), sum(known.values())
    if new:
        print(f"known_failures: {sum(new.values())} failure(s) beyond {args.known}")
        return 1
    print(f"known_failures: {total} known failure(s) of {listed} listed; no new failure")
    return 0


if __name__ == "__main__":
    sys.exit(main())
