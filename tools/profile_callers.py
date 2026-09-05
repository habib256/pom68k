#!/usr/bin/env python3
# POM68K — split a gperftools CPU profile's hot MEMORY buckets by their
# CALLER, not by their leaf symbol.
#
# `tools/profile_census.py` answers "where does the time go" and buckets by
# leaf. That is the wrong instrument for TODO § B.2, whose open question is
# not how big `Moira::mmuRead<2>` is but WHO CALLS IT: the code generator
# (through an access thunk, a whole-instruction replay stub, or a DTLB fill)
# or the interpreter running an instruction the generator handed back. The
# 2026-09-04 lesson that made this necessary is written in TODO § B.2:
# slices 1 and 2 retired 26.9 % of a SimCity session's interpreted replays
# and moved no wall clock, because a profile bucket is not retired in full
# when the generator only owns half of it.
#
# Usage:
#   tools/profile_callers.py <binary> <cpuprofile> [--top N]
#
# Producing the profile is `profile_census.py`'s recipe verbatim — the same
# LD_PRELOAD shim, the same no-LTO build, the same exec-vaddr slide. Read
# that file's header before changing anything here; both of its documented
# traps (identical-code folding under LTO reassigning samples, and the
# non-zero ELF vaddr of the exec LOAD segment) apply unchanged.
#
# Why a caller attribution is possible at all even though generated code is
# unwind-opaque: it does not need to be walked. Every entry from generated
# code back into C++ goes through exactly one named extern "C" seam —
# `pom68kJitRead` / `pom68kJitWrite` for an exact access, `pom68kJitStep`
# for a whole-instruction replay, `Engine::fillDtlb` for a translation — and
# those frames DO have unwind info. A stack that reaches one of them is the
# generator's; a stack that reaches `Engine::runWindow` or plain
# `Moira::execute` without passing one is the interpreter's. The unwind
# stopping at the JIT frame is therefore not a limitation here, it is the
# answer.

import bisect
import re
import struct
import subprocess
import sys
from collections import Counter

# The buckets TODO § B.2 quotes from the 2026-09-02 profile, plus the two
# translation helpers they sit on. Matched against the LEAF symbol.
TARGETS = [
    ('mmuFetchWord',        r'Moira::mmuFetchWord'),
    ('mmuRead<N>',          r'Moira::mmuRead<'),
    ('mmuWrite<N>',         r'Moira::mmuWrite<'),
    ('mmuTranslateAccess',  r'Moira::mmuTranslateAccess'),
    ('mmuAtcLookup/Fill',   r'Moira::mmuAtc(Lookup|Fill)'),
    ('V8Memory::read16',    r'V8Memory::read16'),
    ('V8Memory::write16',   r'V8Memory::write16'),
    ('V8Memory::read8',     r'V8Memory::read8'),
    ('V8Memory::write8',    r'V8Memory::write8'),
    ('V8Memory::dataSpan',  r'V8Memory::(dataSpan|codeSpan)'),
]

# Who asked. Tested against the WHOLE stack, in this order: these are
# mutually exclusive dispatch seams, and the order resolves the nesting
# (a thunk runs inside Engine::run, so the specific test must come first).
#
# `Cpu0NN::runCycles` is deliberately NOT here. It is the whole-machine entry
# point, so it sits under EVERY stack in the JIT arm too; using it as an
# "interpreter" seam silently relabelled peripheral pacing and truncated
# window stacks as interpreter work, and inflated `mmuFetchWord`'s
# interpreter share from 8 % to 60 %. The seam has to be the thing that
# actually dispatches a guest instruction, which is `Moira::execute` for a
# plain interpreter and `Moira::pomJitExecOne` under the engine.
CALLERS = [
    ('generator — exact access thunk',
     r'pom68kJitRead|pom68kJitWrite|pom68kJitReadProg|pomJitReadData|'
     r'pomJitWriteData'),
    ('generator — whole-instruction replay',
     r'pom68kJitStep'),
    ('generator — DTLB fill',
     r'jit::Engine::fillDtlb|Engine::fillDtlb|dtlbFillThunk'),
    ('interpreter — engine fetch window',
     r'jit::Engine::runWindow|Engine::runWindow'),
    ('interpreter — instruction dispatch',
     r'Moira::execute|Moira::pomJitExecOne'),
    ('devices / peripheral pacing / harness',
     r'V8Video|Via6522|Ncr5|Swim|Sony|Scc|Asc|Egret|AdbLine|AdbBus|M68hc05|'
     r'ScsiDisk|Rtc|Iwm|Dfac|CudaLle|screen|decodeScreen|dumpPpm|'
     r'V8Memory::tick|flushTicks|schedulePeriphDeadline|pom68kJitSync|'
     r'MoiraCpu<.*>::sync|updateIpl|pollBoostGate|iplLevel'),
]

# Checked BEFORE the stack seams, against the LEAF only: a sample whose leaf
# is inside the code buffer IS the generator's own body. Calling that
# "unattributed" because the generated frame carries no unwind info would
# hide the largest single share of a JIT run.
GENERATED_LEAF = 'generated — native block body'


def exec_vaddr(binary):
    out = subprocess.run(['readelf', '-l', binary], capture_output=True,
                         text=True).stdout
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if 'LOAD' in line and i + 1 < len(lines) and ' E' in lines[i + 1]:
            return int(line.split()[2], 16)
    raise SystemExit('no exec LOAD segment found')


def load(binary, prof):
    """Return (period_us, [(count, [pc, ...leaf first])], symbolizer)."""
    data = open(prof, 'rb').read()
    m = re.search(rb'(?m)^[0-9a-f]+-[0-9a-f]+ [rwxps-]{4} ', data)
    if not m:
        raise SystemExit('no maps section in profile')
    words = struct.unpack('<%dQ' % (m.start() // 8), data[:m.start() // 8 * 8])
    if words[0] != 0 or words[1] != 3:
        raise SystemExit('not a gperftools CPU profile')
    period_us = words[3]

    samples = []
    i = 5
    while i + 1 < len(words):
        n, depth = words[i], words[i + 1]
        if n == 0 and depth == 1 and words[i + 2] == 0:
            break
        if depth == 0 or depth > 512:
            break
        samples.append((n, list(words[i + 2:i + 2 + depth])))
        i += 2 + depth

    maps = []
    for line in data[m.start():].decode('ascii', 'replace').splitlines():
        mm = re.match(r'([0-9a-f]+)-([0-9a-f]+) (....) [0-9a-f]+ \S+ \d+\s*(.*)',
                      line)
        if mm:
            maps.append((int(mm.group(1), 16), int(mm.group(2), 16),
                         mm.group(3), mm.group(4).strip()))
    name = binary.rsplit('/', 1)[-1]
    slide = min(a for a, b, pr, p in maps
                if p.endswith(name) and 'x' in pr) - exec_vaddr(binary)

    nm = subprocess.run(['nm', '-C', '--defined-only', binary],
                        capture_output=True, text=True).stdout
    syms = sorted((int(p[0], 16), p[2]) for p in
                  (l.split(' ', 2) for l in nm.splitlines())
                  if len(p) == 3 and p[1] in 'tTwW')
    addrs = [a for a, _ in syms]
    cache = {}

    def sym(pc):
        s = cache.get(pc)
        if s is not None:
            return s
        s = '[GENERATED CODE]'
        for a, b, pr, p in maps:
            if a <= pc < b:
                if p.endswith(name):
                    j = bisect.bisect_right(addrs, pc - slide) - 1
                    s = syms[j][1] if j >= 0 else f'bin+{pc - slide:#x}'
                elif p and not p.startswith('['):
                    s = f'[{p.rsplit("/", 1)[-1]}]'
                break
        cache[pc] = s
        return s

    return period_us, samples, sym


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    top = 12
    for a in sys.argv[1:]:
        if a.startswith('--top'):
            top = int(a.split('=', 1)[1]) if '=' in a else 12
    if len(args) != 2:
        raise SystemExit(__doc__ or
                         'usage: profile_callers.py <binary> <prof> [--top N]')
    binary, prof = args
    period_us, samples, sym = load(binary, prof)

    targets = [(k, re.compile(rx)) for k, rx in TARGETS]
    callers = [(k, re.compile(rx)) for k, rx in CALLERS]

    total = 0
    depth_hist = Counter()
    per_target = Counter()                 # target -> samples
    matrix = Counter()                     # (target, caller) -> samples
    caller_total = Counter()               # caller -> samples (all leaves)
    unattributed_leaf = Counter()

    for n, stack in samples:
        total += n
        depth_hist[min(len(stack), 8)] += n
        names = [sym(pc) for pc in stack]
        leaf = names[0]

        which = None
        if leaf == '[GENERATED CODE]':
            which = GENERATED_LEAF
        else:
            for k, rx in callers:
                if any(rx.search(s) for s in names):
                    which = k
                    break
        if which is None:
            which = 'unattributed (stack truncated)'
        caller_total[which] += n

        for k, rx in targets:
            if rx.search(leaf):
                per_target[k] += n
                matrix[(k, which)] += n
                break
        else:
            if which == 'unattributed (stack truncated)':
                unattributed_leaf[leaf] += n

    def pct(x):
        return 100.0 * x / total if total else 0.0

    print(f'{total} samples x {period_us} us = '
          f'{total * period_us / 1e6:.1f} s CPU attributed')
    print(f'stack depth (capped at 8): '
          + '  '.join(f'{d}:{100.0 * c / total:.0f}%'
                      for d, c in sorted(depth_hist.items())))

    print('\n== every sample, by CALLER ==')
    for k, n in caller_total.most_common():
        print(f'  {pct(n):6.2f}%  {n:9d}  {k}')

    print('\n== the TODO § B.2 buckets, split by caller ==')
    order = ([GENERATED_LEAF] + [k for k, _ in CALLERS] +
             ['unattributed (stack truncated)'])
    for k, _ in TARGETS:
        n = per_target[k]
        if not n:
            continue
        print(f'\n-- {k}: {pct(n):.2f}% of the run ({n} samples)')
        for c in order:
            v = matrix[(k, c)]
            if v:
                print(f'     {100.0 * v / n:6.2f}% of bucket '
                      f'({pct(v):5.2f}% of run)  {c}')

    if unattributed_leaf:
        print('\n== top unattributed leaves (no recognised dispatch seam) ==')
        for s, n in unattributed_leaf.most_common(top):
            print(f'  {pct(n):6.2f}%  {s[:120]}')


if __name__ == '__main__':
    main()
