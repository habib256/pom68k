#!/usr/bin/env python3
"""Interleaved A B C / C B A bench runs; prints mean wall per arm and fingerprint check.
usage: abba.py <bench: jit_bench|jit_bench_lcii> <frames> <rounds> name=path ..."""
import sys,subprocess,json,os,statistics
bench,frames,rounds=sys.argv[1],sys.argv[2],int(sys.argv[3])
arms=[a.split('=',1) for a in sys.argv[4:]]
def run(path):
    env=dict(os.environ,POM68K_BENCH_FRAMES=frames)
    out=subprocess.run([path],capture_output=True,text=True,env=env,cwd=os.environ.get('POM68K_ROOT', os.getcwd())).stdout
    for l in out.splitlines():
        if l.startswith('POM68K_JIT_METRICS '):
            j=json.loads(l.split(' ',1)[1]); return j['wall_ns']/1e9,j['fingerprint']
    raise SystemExit('no metrics from '+path+'\n'+out[-500:])
res={n:[] for n,_ in arms}; fps=set()
run(arms[0][1])  # discarded warm-up
load0=os.getloadavg()[0]
for r in range(rounds):
    order=arms if r%2==0 else arms[::-1]
    for n,p in order:
        w,fp=run(p); res[n].append(w); fps.add(fp)
load1=os.getloadavg()[0]
base=statistics.mean(res[arms[0][0]])
print(f'{bench} frames={frames} n={rounds} load {load0:.1f}->{load1:.1f} fingerprints={sorted(fps)}')
for n,_ in arms:
    v=res[n]; m=statistics.mean(v)
    print(f'  {n:8s} mean {m:.3f}s  min {min(v):.3f} max {max(v):.3f} spread {100*(max(v)-min(v))/m:.1f}%  vs {arms[0][0]} {100*(m-base)/base:+.1f}%')
