#!/usr/bin/env python3
# tools/netlab/steps.py LOG...: the first walk another player does in each log, its steps in mm run-length coded
import re,math,sys
for f in sys.argv[1:]:
    p=[]
    for l in open(f,errors='replace'):
        m=re.match(r'\s*(\d+) nettrace: frame (\d+): other (\w+) at (\S+) (\S+) (\S+),',l)
        if m: p.append((int(m[1]),int(m[2]),float(m[4]),float(m[6])))
    st=[round(1000*math.hypot(b[2]-a[2],b[3]-a[3])) for a,b in zip(p,p[1:])]
    i=next(k for k in range(len(st)-50) if all(s>0 for s in st[k:k+50]))
    out=[];k=i
    while k<len(st) and len(out)<90:
        j=k
        while j<len(st) and st[j]==st[k]: j+=1
        out.append('%dx%d'%(st[k],j-k)); k=j
    print(f,' '.join(out))
