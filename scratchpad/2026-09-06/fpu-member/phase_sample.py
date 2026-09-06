#!/usr/bin/env python3
"""Attach macOS `sample` to lcii_speedometer_census for the selected family's
phase only: start sampling (1 ms) when the first `[speedo:<mode>]` progress
line appears on stderr, for <seconds>. Writes <raw>.sample and prints the top
symbols by self samples (leaf frames) from the capture."""
import os, re, subprocess, sys, collections
binary, raw, mode, seconds = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
env = dict(os.environ); env['POM68K_SPEEDO_MODE'] = mode
for kv in sys.argv[5:]:
    k, v = kv.split('=', 1); env[k] = v
# A pty keeps the census line-buffered; through a pipe its progress lines
# would only arrive at exit, after the process the sampler needs is gone.
import pty
master, slave = pty.openpty()
raw = os.path.abspath(raw)
child = subprocess.Popen([binary], env=env, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)
sampler = None; tail = []; buf = b''
while True:
    try:
        chunk = os.read(master, 65536)
    except OSError:
        break
    if not chunk: break
    buf += chunk
    while b'\n' in buf:
        line, buf = buf.split(b'\n', 1)
        text = line.decode(errors='replace').rstrip('\r') + '\n'
        tail.append(text)
        if sampler is None and text.startswith(f'  [speedo:{mode}]'):
            sampler = subprocess.Popen(['sample', str(child.pid), seconds, '1', '-mayDie', '-f', raw],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
child.wait()
if sampler: sampler.wait()
else: print('sampler never triggered'); sys.exit(1)
print(''.join(l for l in tail if 'speedometer-test' in l), end='')
# leaf attribution: sample's call tree indents children; a leaf is a line
# whose next line is not deeper. Count samples of leaf lines by symbol.
lines = open(raw, errors='replace').read().splitlines()
tree = []
for l in lines:
    m = re.match(r'^( *)(\+ )?(\d+) (.*?)  \(in ', l) or re.match(r'^( *)(\d+) (.*?)  \(in ', l)
    if not m: continue
    if len(m.groups()) == 4: ind, _, n, sym = m.groups()
    else: ind, n, sym = m.groups()
    tree.append((len(ind), int(n), sym.strip()))
leaf = collections.Counter(); total = 0
for i, (d, n, sym) in enumerate(tree):
    nxt = tree[i + 1][0] if i + 1 < len(tree) else -1
    if nxt <= d:
        leaf[sym] += n; total += n
print(f'leaf samples {total}')
for sym, n in leaf.most_common(25):
    print(f'{100.0 * n / total:6.2f} %  {n:6d}  {sym[:90]}')
