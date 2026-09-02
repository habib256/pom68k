#!/usr/bin/env python3
# POM68K — decode a gperftools CPU profile of a census/bench run into the
# TIME buckets TODO § 2 names: generated bodies, engine runtime/windows,
# compilation, MMU/cache, memory-map thunks, LLE peripherals, interpreter
# fallback, host/harness. The point is the milestone-2 rule: order work by
# a TIME profile, never by a raw fallback histogram.
#
# Usage:
#   tools/profile_census.py <binary> <cpuprofile>
#
# Producing the profile (Ubuntu's gperftools ignores CPUPROFILE unless
# ProfilerStart is called, so a 6-line LD_PRELOAD shim does it; and the
# child writes CPUPROFILE_<pid>, not CPUPROFILE):
#   printf '#include <cstdlib>\n
#     extern "C" int ProfilerStart(const char*);\n
#     extern "C" void ProfilerStop();\n
#     __attribute__((constructor)) static void s(){\n
#         if (const char* p = std::getenv("CPUPROFILE")) ProfilerStart(p); }\n
#     __attribute__((destructor)) static void e(){ ProfilerStop(); }\n' \
#     > shim.cpp
#   g++ -shared -fPIC shim.cpp -o shim.so -l:libprofiler.so.0
#   LD_PRELOAD=./shim.so CPUPROFILE=out.prof CPUPROFILE_FREQUENCY=500 <run>
#
# Profile against a NO-LTO RelWithDebInfo build (build-profile): under LTO,
# identical-code folding reassigns samples to arbitrary sibling symbols and
# the report lies (measured 2026-09-02: dumpHisto at 8.7 %). The no-LTO
# build kept the guest fingerprint byte-identical, so the workload is the
# same machine.
#
# Symbolization subtlety that produced a whole wrong report once: the exec
# LOAD segment's ELF vaddr is NOT 0 on these binaries (the r-xp mapping
# comes first in the recorded maps), so the slide is
# map_start - exec_p_vaddr, never plain map_start.

import bisect
import re
import struct
import subprocess
import sys
from collections import Counter

BUCKETS = [
    ('corps générés (natif)', r'^\[GENERATED CODE\]$'),
    ('moteur — compilation', r'Emitter|x64::Asm|a64::Asm|decodeEaPlan|EaPlan|::emit|compileBlock|Encoder'),
    ('moteur — runtime/fenêtres', r'jit::Engine|jit::.*(dispatch|Window|window|census|Histo|histo)|pomJit|describeInstruction|Cpu0[0-9]+::|pom68kJitSync|MoiraCpu'),
    ('MMU/cache', r'moira::.*(mmu|Mmu|Cache040|pomCache|Atc)'),
    ('LLE/périphériques', r'CudaLle|Egret|AdbLine|AdbBus|AdbVia|M68hc05|M68HC05|Mcu|Swim|Sony|Scc|Ncr5|ScsiDisk|Via6522|Asc|Rtc|Dfac|Iwm|ApplePic|Pge'),
    ('carte mémoire/thunks', r'Memory::|::peek|::poke'),
    ('interpréteur (fallback)', r'moira::'),
    ('hôte/harnais', r'^\[lib|^main$|testasset|Sha256|writableFixture|V8Video|TobyVideo|SonoraVideo|dumpPpm|std::|__gnu'),
]


def exec_vaddr(binary):
    out = subprocess.run(['readelf', '-l', binary], capture_output=True,
                         text=True).stdout
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if 'LOAD' in line and i + 1 < len(lines) and ' E' in lines[i + 1]:
            return int(line.split()[2], 16)
    raise SystemExit('no exec LOAD segment found')


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ or 'usage: profile_census.py <binary> <prof>')
    binary, prof = sys.argv[1], sys.argv[2]
    data = open(prof, 'rb').read()
    m = re.search(rb'(?m)^[0-9a-f]+-[0-9a-f]+ [rwxps-]{4} ', data)
    if not m:
        raise SystemExit('no maps section in profile')
    words = struct.unpack('<%dQ' % (m.start() // 8), data[:m.start() // 8 * 8])
    if words[0] != 0 or words[1] != 3:
        raise SystemExit('not a gperftools CPU profile')
    period_us = words[3]
    i = 5
    leaf = Counter()
    total = 0
    while i + 1 < len(words):
        n, depth = words[i], words[i + 1]
        if n == 0 and depth == 1 and words[i + 2] == 0:
            break
        if depth == 0 or depth > 512:
            break
        leaf[words[i + 2]] += n
        total += n
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

    def sym(pc):
        for a, b, pr, p in maps:
            if a <= pc < b:
                if p.endswith(name):
                    j = bisect.bisect_right(addrs, pc - slide) - 1
                    return syms[j][1] if j >= 0 else f'bin+{pc - slide:#x}'
                if not p or p.startswith('['):
                    return '[GENERATED CODE]'
                return f'[{p.rsplit("/", 1)[-1]}]'
        return '[GENERATED CODE]'   # runtime mmap outside recorded maps

    agg = Counter()
    for pc, n in leaf.items():
        agg[sym(pc)] += n

    def bucket(s):
        for k, rx in BUCKETS:
            if re.search(rx, s):
                return k
        return 'autre'

    per = Counter()
    top = {}
    for s, n in agg.items():
        k = bucket(s)
        per[k] += n
        top.setdefault(k, Counter())[s] += n

    print(f'{total} samples x {period_us} us = {total * period_us / 1e6:.1f} s '
          f'CPU attribués')
    print('\n== buckets ==')
    for k, n in per.most_common():
        print(f'{100.0 * n / total:6.2f}%  {k}')
    print('\n== top 6 par bucket ==')
    for k, _ in per.most_common():
        print(f'\n-- {k}')
        for s, n in top[k].most_common(6):
            print(f'  {100.0 * n / total:6.2f}%  {s[:130]}')


if __name__ == '__main__':
    main()
