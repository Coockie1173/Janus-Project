#!/usr/bin/env python3
# tools/netlab/walk.py NAME [FROM TO]: how the other players move in NAME/run.log (GE_NETTRACE), frame to frame
import re,sys,math,collections
n=sys.argv[1]; t0=float(sys.argv[2]) if len(sys.argv)>2 else 0; t1=float(sys.argv[3]) if len(sys.argv)>3 else 1e9
tr=collections.defaultdict(list)
for l in open(n+'/run.log',errors='replace'):
    m=re.match(r'\s*(\d+) nettrace: frame (\d+): other (\w+) at (\S+) (\S+) (\S+), yaw (\S+), pitch (\S+)',l)
    if m and t0<=int(m[1])<=t1: tr[m[3]].append((int(m[2]),float(m[4]),float(m[5]),float(m[6]),float(m[7].rstrip(',')),float(m[8])))
for c,p in tr.items():
    steps=[];back=0;still=0;prev=None;dup=0
    for a,b in zip(p,p[1:]):
        if b[0]==a[0]: dup+=1
        d=(b[1]-a[1],b[3]-a[3]); L=math.hypot(*d)
        if L<1e-5: still+=1; continue
        if prev and d[0]*prev[0]+d[1]*prev[1]<0: back+=1
        prev=d; steps.append(L)
    if not steps: print(c,len(p),'updates, never moved'); continue
    mean=sum(steps)/len(steps); sd=(sum((s-mean)**2 for s in steps)/len(steps))**.5
    big=sum(1 for s in steps if s>3*mean)
    print('%s: %d updates (%d twice in a frame), %d moving steps, %d standing; step mean %.4f sd %.4f (%.0f%%), %d over 3x the mean, %d backwards; path %.2f'%(c,len(p),dup,len(steps),still,mean,sd,100*sd/mean,big,back,sum(steps)))
