#!/usr/bin/env python3
# POM68K — the macOS leg of tools/profile_census.py: decode a /usr/bin/sample
# call-graph of a census/bench run into the same TIME buckets, so the
# milestone-2 rule (order work by a TIME profile, never by a fallback
# histogram) has an instrument on both proof architectures.
#
# Usage:
#   tools/profile_census_macos.py <binary> <raw-sample.txt> [args...]
#
# If <raw-sample.txt> exists it is parsed; otherwise the binary is launched
# (stdout/stderr beside the raw file as .out/.err), sampled every 1 ms for
# the whole run (`sample <pid> 3600 1 -mayDie`), and the capture is parsed.
#
# Two differences against the gperftools leg, both load-bearing:
#   * `sample` snapshots EVERY thread each tick, running or blocked, where
#     gperftools charges CPU time only. Stacks whose leaf is a known wait
#     syscall are split into a separate off-CPU line and the bucket table is
#     normalized over on-CPU samples, so the two legs' percentages compare.
#   * ld64 deduplicates identical functions (ICF) and `sample` then reports
#     `<deduplicated_symbol>` — the same class of lie LTO folding told the
#     Linux leg (dumpHisto at 8.7 %, 2026-09-02). Profile a tree linked
#     with -Wl,-no_deduplicate (build-profile) and the tool refuses a
#     capture where the folded share could move the ranking.
#
# JIT-generated code appears as `??? (in <unknown binary>)` frames and is
# attributed to the generated-bodies bucket, like the anonymous-mmap rule
# of the Linux leg.

import os
import re
import subprocess
import sys
from collections import Counter

BUCKETS = [
    ('corps générés (natif)', r'^\[GENERATED CODE\]$'),
    ('moteur — compilation', r'Emitter|x64::Asm|a64::Asm|decodeEaPlan|EaPlan|::emit|compileBlock|Encoder|Backend::compile|jit::classify'),
    ('moteur — runtime/fenêtres', r'jit::Engine|jit::.*(dispatch|Window|window|census|Histo|histo)|pomJit|describeInstruction|Cpu0[0-9]+::|pom68kJitSync|MoiraCpu|Backend::run|pom68kA64|pom68kX64|CodeGuard'),
    ('MMU/cache', r'moira::.*(mmu|Mmu|Cache040|pomCache|Atc)'),
    ('LLE/périphériques', r'CudaLle|Egret|AdbLine|AdbBus|AdbVia|M68hc05|M68HC05|Mcu|Swim|Sony|Scc|Ncr5|ScsiDisk|Via6522|Asc|Rtc|Dfac|Iwm|ApplePic|Pge|Dafb'),
    ('carte mémoire/thunks', r'Memory::|::peek|::poke|makeJitMemoryHooks'),
    ('interpréteur (fallback)', r'moira::'),
    ('hôte/harnais', r'^\[lib|^main$|testasset|Sha256|writableFixture|V8Video|TobyVideo|SonoraVideo|dumpPpm|decodeScreen|std::|__gnu|^\[dyld\]$|^\[host lib\]$'),
]

# A self-sample whose own frame is one of these is a thread parked in the
# kernel, not CPU time; `sample` records it every tick anyway.
WAIT = re.compile(r'mach_msg|semaphore_wait|semaphore_timedwait|__psynch|'
                  r'__semwait|kevent|__select|poll(?:$|\()|__ulock_wait|'
                  r'swtch|thread_switch|__sigsuspend|nanosleep|usleep|'
                  r'__workq_kernreturn|start_wqthread|_dispatch_worker')

LINE = re.compile(r'^(?P<pre>[ +!:|]*?)(?P<n>\d+) (?P<rest>.*)$')


def capture(binary, raw, args):
    out, err = raw + '.out', raw + '.err'
    with open(out, 'wb') as o, open(err, 'wb') as e:
        child = subprocess.Popen([binary] + args, stdout=o, stderr=e)
        sampler = subprocess.Popen(
            ['sample', str(child.pid), '3600', '1', '-mayDie', '-f', raw],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rc = child.wait()
        sampler.wait()
    print(f'run rc={rc}  stdout={out}  stderr={err}', file=sys.stderr)
    if rc != 0:
        raise SystemExit(f'workload exited {rc}: not a profile of the '
                         'claimed run')


def frame(rest, binname):
    m = re.match(r'(?P<sym>.+?)  \(in (?P<lib>.+?)\)', rest)
    if not m:
        return rest.strip()
    sym, lib = m.group('sym'), m.group('lib')
    if sym == '???':
        return '[GENERATED CODE]'
    if lib != binname:
        if lib == 'dyld':
            return '[dyld]'
        return f'[{lib}]'
    return sym


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__ or
                         'usage: profile_census_macos.py <binary> <raw> '
                         '[args...]')
    binary, raw = sys.argv[1], sys.argv[2]
    if not os.path.exists(raw):
        capture(binary, raw, sys.argv[3:])
    binname = os.path.basename(binary)

    self_cpu = Counter()
    blocked = 0
    total = 0
    in_graph = False
    # stack of [depth, remaining_selfcount, symbol]
    stack = []

    def settle(upto_depth):
        nonlocal blocked, total
        while stack and stack[-1][0] >= upto_depth:
            depth, self_n, sym = stack.pop()
            if self_n <= 0:
                continue
            total += self_n
            if sym.startswith('Thread_'):
                continue
            if WAIT.search(sym):
                blocked += self_n
            else:
                self_cpu[sym] += self_n

    for line in open(raw, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('Call graph:'):
            in_graph = True
            continue
        if not in_graph:
            continue
        if line.startswith('Total number in stack'):
            break
        m = LINE.match(line)
        if not m or not m.group('rest'):
            continue
        depth = len(m.group('pre'))
        n = int(m.group('n'))
        settle(depth)
        if stack:
            stack[-1][1] -= n
        stack.append([depth, n, frame(m.group('rest'), binname)])
    settle(0)

    if not total:
        raise SystemExit('no call graph parsed — not a sample(1) capture?')
    cpu = total - blocked
    folded = self_cpu.get('<deduplicated_symbol>', 0)
    print(f'{total} samples, {cpu} on-CPU ({100.0 * cpu / total:.1f} %), '
          f'{blocked} parked in wait syscalls')
    if folded > 0.005 * cpu:
        raise SystemExit(f'<deduplicated_symbol> holds '
                         f'{100.0 * folded / cpu:.2f} % of on-CPU time: '
                         'link the profiled tree with -Wl,-no_deduplicate '
                         '(build-profile) before trusting any ranking')

    agg = Counter()
    for sym, n in self_cpu.items():
        for name, pat in BUCKETS:
            if re.search(pat, sym):
                agg[name] += n
                break
        else:
            agg['autre'] += n
    print('\n── buckets (% of on-CPU) ──')
    for name, n in agg.most_common():
        print(f'  {name:32s} {100.0 * n / cpu:6.2f} %  ({n})')
    print('\n── top 45 self symbols ──')
    for sym, n in self_cpu.most_common(45):
        print(f'  {100.0 * n / cpu:6.2f} %  {sym}')


if __name__ == '__main__':
    main()
