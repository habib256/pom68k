#!/usr/bin/env python3
"""Refuse to trust a ctest run whose binaries are not the tree's.

`CLAUDE.md` states the rule already — *a green ctest is only worth the
freshness of its binaries* — and records what breaking it costs: a run in
early August returned 143/143 over binaries linked at different times, 102 of
them older than `libpom68k_core.a`, and proved nothing. It got quoted anyway,
because a pass invites no investigation.

Its mirror image arrived on 2026-08-18. A full build was killed at 90 % to
free the machine; `ctest` then reported four gates FAILED, and all four were
missing executables. That one costs a diagnosis rather than a release — the
failure was investigated, at length, as a code regression.

One check kills both. Run it before any tier:

    python3 tools/check_binaries_fresh.py --build-dir build [-R <regex>]

Exit 0 when every gate's executable exists and `make` considers it up to
date; exit 1 with the list otherwise. The roster comes from `ctest -N`, so it
covers exactly the gates about to run, and the freshness verdict comes from
`make -q`, so it uses the real dependency graph rather than an mtime proxy.

── Which rule you ask is the whole tool (fixed 2026-08-18, same day) ───────
The first version asked `make -q <target>` on the build directory's top-level
Makefile. Every CMake convenience rule there reads

    adbline_test: cmake_check_build_system
            $(MAKE) -f CMakeFiles/Makefile2 adbline_test

and `cmake_check_build_system` is **phony**, so it is never up to date, so
`make -q` answered "would rebuild" for **every** target — freshly linked ones
included. Verified on this tree: 3 gates checked, 3 reported STALE, all three
current. `CMakeFiles/Makefile2` is no better; its rules recurse in turn. Only
`CMakeFiles/<t>.dir/build.make`, target `CMakeFiles/<t>.dir/build`, carries
the real prerequisites — compiler depfiles included.

A guard that answers "no" to everything is worse than no guard at all: the
one it cries wolf at is the reader, who stops running it inside a day. So
this file also ships `--self-test`, which proves both answers on the real
tree before you trust either — `make -W` (what-if) makes one prerequisite
artificially new without touching a single file, and the guard must flip.
`docs/MEASURING.md` § R5 is the general form: **a guard nobody has watched
say both yes and no is an unmeasured instrument.**
"""

import argparse
import os
import re
import subprocess
import sys


def gate_commands(build_dir, regex):
    """Every gate's argv[0], from ctest itself."""
    cmd = ["ctest", "-N", "-V"]
    if regex:
        cmd += ["-R", regex]
    out = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"ctest -N failed in {build_dir}:\n{out.stderr}")
    # `ctest -N -V` prints one "Test command: /abs/path/exe "args"" per gate.
    seen = {}
    for line in out.stdout.splitlines():
        m = re.search(r"Test command:\s+(\S+)", line)
        if m:
            path = m.group(1).strip('"')
            seen.setdefault(path, 0)
            seen[path] += 1
    return seen


def real_rule(build_dir, target):
    """The per-target makefile that carries the real prerequisites, or None.

    Not every executable ctest runs is a CMake target (a python script, a
    shell wrapper), and a target's directory disappears with a `clean`. Both
    are "cannot answer", never "fresh".
    """
    mk = os.path.join("CMakeFiles", target + ".dir", "build.make")
    return mk if os.path.isfile(os.path.join(build_dir, mk)) else None


def make_q(build_dir, target, what_if=None):
    """0 = up to date, 1 = make would rebuild, 2 = make could not answer.

    `make -q` builds nothing. `-W <file>` is the what-if flag: it makes one
    prerequisite artificially new, which is how --self-test proves the guard
    can still say "stale" without editing the tree.
    """
    mk = real_rule(build_dir, target)
    if mk is None:
        return 2
    cmd = ["make", "-q", "-f", mk]
    if what_if:
        cmd += ["-W", what_if]
    cmd += [os.path.join("CMakeFiles", target + ".dir", "build")]
    r = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True)
    return r.returncode if r.returncode in (0, 1) else 2


def first_prerequisite(build_dir, target):
    """One absolute source path the target's objects depend on, for -W."""
    mk = real_rule(build_dir, target)
    if mk is None:
        return None
    with open(os.path.join(build_dir, mk)) as f:
        for line in f:
            m = re.match(r"^CMakeFiles/\S+\.o:\s+(/\S+)$", line.strip())
            if m:
                return m.group(1)
    return None


def classify(build_dir, targets):
    """Split `targets` into (stale, unanswerable) using make's own graph."""
    stale, unknown = [], []
    for t in sorted(targets):
        rc = make_q(build_dir, t)
        if rc == 1:
            stale.append(t)
        elif rc == 2:
            unknown.append(t)
    return stale, unknown


def self_test(build_dir, targets):
    """Prove the guard answers BOTH ways on this tree, mutating nothing.

    Picks the first target that is currently up to date, then asks the same
    question again with one of its own sources declared artificially new.
    Fresh must read 0 and what-if must read 1; anything else means the tool
    is asking a rule that does not carry the dependencies, which is exactly
    the defect this file was born with.
    """
    for t in sorted(targets):
        src = first_prerequisite(build_dir, t)
        if src is None or make_q(build_dir, t) != 0:
            continue
        rc = make_q(build_dir, t, what_if=src)
        print(f"self-test on {t}:  as-is 0 (fresh)  ·  "
              f"what-if {os.path.basename(src)} newer -> {rc}")
        if rc != 1:
            print("SELF-TEST FAILED: the guard cannot see a newer source. It "
                  "is asking a rule without real prerequisites (see the "
                  "module docstring) and every verdict it prints is noise.")
            return 1
        print("self-test passed: the guard says yes and no on this tree.")
        return 0
    print("SELF-TEST INCONCLUSIVE: no up-to-date target with a parsable "
          "prerequisite. Run `make` first, then re-run --self-test.")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("-R", "--regex", default=None,
                    help="same gate filter you are about to pass to ctest")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the freshness question can answer both ways")
    args = ap.parse_args()

    if not os.path.isdir(args.build_dir):
        sys.exit(f"no build directory at {args.build_dir}")

    commands = gate_commands(args.build_dir, args.regex)
    if not commands:
        sys.exit("no gates matched — check the -R filter")

    missing, targets = [], set()
    build_abs = os.path.abspath(args.build_dir)
    for exe, gates in sorted(commands.items()):
        if not os.path.isabs(exe) or not exe.startswith(build_abs):
            continue                      # not something this build produced
        if os.path.exists(exe):
            targets.add(os.path.basename(exe))
        else:
            missing.append((exe, gates))

    if args.self_test:
        return self_test(args.build_dir, targets)

    stale, unknown = classify(args.build_dir, targets)

    if not missing and not stale:
        print(f"binaries fresh: {len(targets) - len(unknown)} gate "
              f"executable(s) present and up to date"
              + (f"; {len(unknown)} not a make target (not checked)"
                 if unknown else ""))
        return 0

    # The two failures mean opposite things and deserve different sentences:
    # missing = an interrupted build, and ctest will call them FAILURES;
    # stale   = a build that was never run after an edit, and ctest will call
    #           them PASSES.
    for exe, gates in missing:
        print(f"MISSING  {os.path.basename(exe)}  ({gates} gate(s)) — the build "
              "was interrupted; ctest reports these as FAILURES that are not "
              "regressions")
    for t in stale:
        print(f"STALE    {t} — make would rebuild it; a pass here proves "
              "nothing about the current tree")
    print(f"\n{len(missing)} missing, {len(stale)} stale out of "
          f"{len(targets)} gate executable(s) checked"
          + (f" ({len(unknown)} not a make target)" if unknown else "")
          + ". Run `make` before trusting this tier.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
