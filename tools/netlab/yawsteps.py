#!/usr/bin/env python3
# tools/netlab/yawsteps.py NAME own|other FROM TO: the yaw's change from update to update, in units of 0.00001 rad, run-length coded
import re,sys,math,collections
n,who,t0,t1=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]); tr=collections.defaultdict(list)
for l in open(n+'/run.log',errors='replace'):
    m=re.match(r'\s*(\d+) nettrace: frame (\d+): (\w+) (\w+) at \S+ \S+ \S+ yaw (\S+), pitch (\S+)',l.replace(', yaw',' yaw'))
    if m and m[3]==who and t0<=int(m[1])<=t1: tr[m[4]].append((int(m[2]),float(m[5]),float(m[6])))
for c,p in tr.items():
    d=[]
    for a,b in zip(p,p[1:]):
        x=b[1]-a[1]; x=(x+math.pi)%(2*math.pi)-math.pi; d.append(round(x*1e5))
    mv=[x for x in d if x!=0]
    if not mv: print(c,'never turned'); continue
    mean=sum(mv)/len(mv); sd=(sum((x-mean)**2 for x in mv)/len(mv))**.5
    print('%s: %d updates, %d turning; step mean %.1f sd %.1f, min %d max %d, total %.4f rad, pitch %.4f -> %.4f'%(c,len(p),len(mv),mean,sd,min(mv),max(mv),sum(d)/1e5,p[0][2],p[-1][2]))
    out=[];k=next(i for i,x in enumerate(d) if x!=0)+40
    while k<len(d) and len(out)<40:
        j=k
        while j<len(d) and d[j]==d[k]: j+=1
        out.append('%dx%d'%(d[k],j-k)); k=j
    print('   ',' '.join(out))
