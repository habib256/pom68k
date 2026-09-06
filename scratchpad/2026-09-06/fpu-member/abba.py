#!/usr/bin/env python3
"""Counterbalanced ABBA over separate processes for lcii_speedometer_census.

usage: abba.py <rounds> <mode> <label>=<binary>[:ENV=V,...] <label>=<binary>[:...]
Each round runs A B B A. Prints every wall time, then per-arm medians.
The census fingerprints must be identical across every run or the number
means nothing; the script refuses to summarise a run whose fingerprint or
frame count differs from the first.
"""
import os, re, statistics, subprocess, sys

rounds = int(sys.argv[1]); mode = sys.argv[2]
arms = []
for spec in sys.argv[3:]:
    label, rest = spec.split('=', 1)
    binary, _, envs = rest.partition(':')
    env = dict(os.environ); env['POM68K_SPEEDO_MODE'] = mode
    for kv in filter(None, envs.split(',')):
        k, v = kv.split('=', 1); env[k] = v
    arms.append((label, binary, env))
assert len(arms) == 2
order = []
for _ in range(rounds): order += [0, 1, 1, 0]
walls = {a[0]: [] for a in arms}
ident = None
for idx in order:
    label, binary, env = arms[idx]
    out = subprocess.run([binary], env=env, capture_output=True, text=True).stdout
    m = re.search(r'speedometer-test: mode=(\S+) done=(\d) frames=(\d+) wall=([0-9.]+)s .* fp=(\w+) screen=(\w+)', out)
    if not m or m.group(2) != '1':
        print(f'{label}: FAILED to complete\n{out[-800:]}'); sys.exit(1)
    key = (m.group(1), m.group(3), m.group(5), m.group(6))
    if ident is None: ident = key
    elif key != ident:
        print(f'{label}: identity drift {key} vs {ident}'); sys.exit(1)
    w = float(m.group(4)); walls[label].append(w)
    print(f'{label:12s} wall={w:.6f}s frames={key[1]} fp={key[2]}', flush=True)
print('identity', ident)
for label, ws in walls.items():
    print(f'{label:12s} n={len(ws)} median={statistics.median(ws):.6f} min={min(ws):.6f} max={max(ws):.6f}')
a, b = [statistics.median(walls[l]) for l, _, _ in arms]
print(f'delta {arms[1][0]} vs {arms[0][0]}: {100.0 * (b - a) / a:+.2f} %')
