#!/usr/bin/env python3
# tools/netlab/cmp.py [A.log B.log]: each thrown thing's life in the host's and the joiner's log (b's clock is 45 s behind)
import re,sys
fa,fb=(sys.argv[1],sys.argv[2]) if len(sys.argv)>2 else ('a/run.log','b/run.log')
def load(f,off):
    lives=[];cur={}
    for l in open(f,errors='replace'):
        m=re.match(r'\s*(\d+) nettrace: frame (\d+): thrown (\w+) \(class (\w+), state (\d+)\) at (\S+) (\S+) (\S+)',l)
        if m:
            t,fr,p=int(m[1])+off,int(m[2]),m[3]; pos=(float(m[6]),float(m[7]),float(m[8]))
            if p in cur and fr-cur[p]['last']<=16: cur[p]['last']=fr; cur[p]['end']=pos; cur[p]['n']+=1
            else:
                cur[p]={'t':t,'first':fr,'last':fr,'start':pos,'end':pos,'n':1}; lives.append(cur[p])
    return lives
a,b=load(fa,0),load(fb,45)
def show(x): return 't=%d %3d samples  %7.2f %6.2f %7.2f -> %7.2f %6.2f %7.2f'%(x['t'],x['n'],*x['start'],*x['end'])
used=set()
for x in a:
    best=None
    for k,y in enumerate(b):
        if k in used: continue
        d=sum((p-q)**2 for p,q in zip(x['start'],y['start']))**.5
        if abs(x['t']-y['t'])<=2 and d<1.0 and (best is None or d<best[1]): best=(k,d)
    if best is None: print('ONLY HOST   ',show(x)); continue
    used.add(best[0]); y=b[best[0]]
    e=sum((p-q)**2 for p,q in zip(x['end'],y['end']))**.5
    print('%s host %s\n%s join %s   ends %.2f apart'%('same  ' if e<0.3 else 'APART ',show(x),'      ',show(y),e))
for k,y in enumerate(b):
    if k not in used: print('ONLY JOINER ',show(y))
