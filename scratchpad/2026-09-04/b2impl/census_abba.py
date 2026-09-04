#!/usr/bin/env python3
"""Phase-wall ABBA of lcii_simcity_census: unmodified HEAD (A) vs slices 1+2 (B).

Why this workload and not jit_bench_lcii: the bench boots and idles, so it
draws the desktop once — the VRAM refusals slice 1 removes are barely on its
path, and its ABBA read NOT A CLAIM at both budgets. The profile that named
`V8Memory::read16/write16` and the refused framebuffer is the SimCity census
(CHANGELOG 2026-09-02 (sixth), app-load phase: a QuickDraw-heavy redraw loop),
so that is the phase entitled to judge the slice.

Protocol, the same as abba.sh (docs/MEASURING.md § 1):
  * one discarded warm-up pair, then 3 counterbalanced pairs AB / BA / AB;
  * both arms are the same workload at the same FIXED phase budgets, so the
    comparison is guest-work-for-guest-work — the fingerprint and the screen
    hash are printed with every run and must not move;
  * medians, per-arm spreads, and a delta judged against the WIDEST of
    (spread A, spread B, host floor), never the narrowest;
  * the 1-minute load is stamped before AND after every run, because a
    measurement taken while anything else runs is provisional whatever its
    spread says (§ 1bis).

Each phase's wall includes that phase's own histogram dump — identical work in
both arms, so it cancels here, but it makes an absolute number from this
script incomparable with anything outside it.
"""
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORKTREE = "/home/gistarcade/src/pom68k/.claude/worktrees/agent-aa9179cca4f63271d"
ARMS = {
    "A": (os.path.join(HERE, "base-src/build-base/lcii_simcity_census"),
          os.path.join(HERE, "base-src")),
    "B": (os.path.join(WORKTREE, "build/lcii_simcity_census"), WORKTREE),
}
TIMES = os.path.join(HERE, "census_abba.times")
FLOOR_PERMILLE = 10.0            # performance_budgets.tsv:67 (x86_64)
REPEATS = 3                      # counterbalanced pairs, after the warm-up pair

ENV = {
    "POM68K_BEYOND_IMG": "hdv/ref/GISTPERSO-boot.vhd",   # the LOCKED reference
    "POM68K_CPU_ENGINE": "jit",
    "POM68K_JIT_BACKEND": "x64",
    "POM68K_JIT_BLOCKS": "1",
    "POM68K_JIT_HOT": "1",
}


def load1():
    with open("/proc/loadavg") as f:
        return float(f.read().split()[0])


def run(arm, tag):
    binary, cwd = ARMS[arm]
    log = os.path.join(HERE, "census_abba_%s_%s.log" % (arm, tag))
    env = dict(os.environ)
    env.update(ENV)
    before = load1()
    out = subprocess.run(
        [sys.executable, os.path.join(HERE, "run_census.py"), binary, cwd, log],
        env=env, capture_output=True, text=True).stdout
    after = load1()
    kv = dict(l.split("=", 1) for l in out.strip().splitlines() if "=" in l)
    kv["LOAD_BEFORE"], kv["LOAD_AFTER"] = "%.2f" % before, "%.2f" % after
    print("  %s arm=%s  rc=%s total=%ss app-load=%ss  load %.2f->%.2f  "
          "fp=%s screen=%s launch=%s%%"
          % (tag, arm, kv.get("RC"), kv.get("TOTAL"), kv.get("APPWALL"),
             before, after, kv.get("FP"), kv.get("SCREEN"),
             kv.get("LAUNCHMOVED")), flush=True)
    return kv


def main():
    rows = []
    with open(TIMES, "w") as f:
        f.write("# lcii_simcity_census phase-wall ABBA — A = unmodified HEAD, "
                "B = slices 1+2\n")
        f.write("# engine: jit/x64 blocks=1 hot=1, image hdv/ref/"
                "GISTPERSO-boot.vhd (locked reference, write-back OFF)\n")
        f.write("# columns: phase\tarm\ttag\twall_s\tfp\tscreen\tload_before\t"
                "load_after\n")

        print("-- warm-up pair (discarded) --", flush=True)
        for arm in ("A", "B"):
            run(arm, "warmup")

        print("-- ABBA (%d pairs) --" % REPEATS, flush=True)
        for i in range(REPEATS):
            order = ("A", "B") if i % 2 == 0 else ("B", "A")
            for arm in order:
                kv = run(arm, "p%d" % i)
                if kv.get("RC") != "0":
                    print("  !! rc=%s — see the log" % kv.get("RC"))
                for k, v in kv.items():
                    if k.startswith("PHASE_") or k in ("TOTAL", "APPWALL"):
                        phase = (k[6:] if k.startswith("PHASE_") else
                                 "TOTAL" if k == "TOTAL" else "app-load(own)")
                        rows.append((phase, arm, "p%d" % i, float(v),
                                     kv.get("FP", "?"), kv.get("SCREEN", "?")))
                        f.write("%s\t%s\tp%d\t%s\t%s\t%s\t%s\t%s\n"
                                % (phase, arm, i, v, kv.get("FP"),
                                   kv.get("SCREEN"), kv["LOAD_BEFORE"],
                                   kv["LOAD_AFTER"]))

    verdict(rows)


def verdict(rows):
    phases = []
    for p, *_ in rows:
        if p not in phases:
            phases.append(p)
    out = []
    out.append("")
    out.append("%-16s %8s %8s %10s %10s %10s   %s"
               % ("phase", "A (s)", "B (s)", "delta %", "sprd A ‰",
                  "sprd B ‰", "verdict"))
    for p in phases:
        med, spread = {}, {}
        for arm in ("A", "B"):
            v = sorted(r[3] for r in rows if r[0] == p and r[1] == arm)
            if not v:
                break
            med[arm] = statistics.median(v)
            spread[arm] = 1000.0 * (v[-1] - v[0]) / med[arm] if med[arm] else 0
        if len(med) != 2:
            continue
        d = 1000.0 * (med["B"] - med["A"]) / med["A"]
        widest = max(spread["A"], spread["B"], FLOOR_PERMILLE)
        out.append("%-16s %8.2f %8.2f %+10.2f %10.1f %10.1f   %s"
                   % (p, med["A"], med["B"], d / 10.0, spread["A"],
                      spread["B"],
                      "CLAIM" if abs(d) > widest else "NOT A CLAIM"))
    fps = sorted({(r[4], r[5]) for r in rows})
    out.append("")
    out.append("fingerprints/screens seen: %s" % (fps,))
    out.append("(must be exactly ONE pair; a delta whose fingerprint moved is "
               "not a timing claim)")
    out.append("floor: %.0f permille (performance_budgets.tsv:67, x86_64); "
               "the verdict uses the widest of (spread A, spread B, floor)"
               % FLOOR_PERMILLE)
    text = "\n".join(out)
    print(text)
    with open(TIMES, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--verdict":
        # self-test / re-print from an existing times file
        rows = []
        for line in open(sys.argv[2]):
            if line.startswith("#") or not line.strip():
                continue
            c = line.rstrip("\n").split("\t")
            if len(c) < 6:
                continue
            rows.append((c[0], c[1], c[2], float(c[3]), c[4], c[5]))
        verdict(rows)
    else:
        main()
