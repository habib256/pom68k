import re,sys,bisect,collections
samp,mapf=sys.argv[1],sys.argv[2]
blocks=[]
for l in open(mapf):
    a,sz,pc,n=l.split(); blocks.append((int(a,16),int(sz),pc,int(n)))
# later entries override earlier at same address: keep last
d={}
for b in blocks: d[b[0]]=b
bl=sorted(d.values()); starts=[b[0] for b in bl]
def find(addr):
    i=bisect.bisect_right(starts,addr)-1
    if i>=0 and addr<bl[i][0]+bl[i][1]: return bl[i]
    return None
lines=open(samp,errors='replace').read().split('\n')
# call graph section: compute self counts of unknown frames = count - sum(children counts)
i0=next(i for i,l in enumerate(lines) if l.startswith('Call graph:'))
i1=next(i for i,l in enumerate(lines) if l.startswith('Total number in stack'))
rx=re.compile(r'^([ +!:|]*?)(\d+) (.*)$')
nodes=[]
for l in lines[i0+1:i1]:
    m=rx.match(l)
    if not m: continue
    depth=len(m.group(1)); nodes.append([depth,int(m.group(2)),m.group(3),0])
total_self=collections.Counter(); jit_self=collections.Counter(); jit_incl=collections.Counter()
stack=[]
for nd in nodes:
    while stack and stack[-1][0]>=nd[0]: stack.pop()
    if stack: stack[-1][3]+=nd[1]
    stack.append(nd)
tot=0; jit=0; byoff=collections.Counter()
for depth,cnt,txt,ch in nodes:
    self_=cnt-ch
    if 'unknown binary' in txt:
        addr=int(re.search(r'\[(0x[0-9a-f]+)\]',txt).group(1),16)
        b=find(addr)
        key=b[2] if b else 'nomap'
        jit_self[key]+=self_; jit_incl[key]+=cnt; jit+=self_
        if b: byoff[(b[2],(addr-b[0]))]+=self_
    elif self_>0:
        name=txt.split('  (in')[0]; total_self[name]+=self_
    tot+=self_ if depth>0 else 0
print('total self samples',tot,'jit self',jit)
print('--- top JIT blocks by self (entry pc, self, inclusive-with-helpers, host bytes, guest instrs)')
bm={b[2]:b for b in bl}
for k,v in jit_self.most_common(25):
    b=bm.get(k); print(k,v,jit_incl[k],b[1] if b else '',b[3] if b else '')
print('--- top non-JIT self')
for k,v in total_self.most_common(40): print(v,k[:110])
print('--- hottest offsets')
for (pc,off),v in byoff.most_common(30): print(pc,hex(off),v)
