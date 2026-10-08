#!/usr/bin/env python3
# tools/netlab/turn.py NAME FROM TO: NAME's own view against the host's copy of it, twice a second
import re,sys
n,t0,t1=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]); last=None
for l in open(n+'/run.log',errors='replace'):
    m=re.match(r'\s*(\d+) goldeneye: yaw (\S+) pitch (\S+), drawn with (\S+);',l)
    if m and t0<=int(m[1])<=t1: last=(int(m[1]),float(m[2]),float(m[3].rstrip(','))); own=True; continue
    m=re.match(r"\s*(\d+) goldeneye: the host's copy \w+: yaw (\S+) pitch (\S+),",l)
    if m and last and int(m[1])==last[0]:
        print('t=%d own yaw %7.4f pitch %7.4f | host copy yaw %7.4f pitch %7.4f'%(last[0],last[1],last[2],float(m[2]),float(m[3].rstrip(',')))); last=None
    elif last and m is None and 'yaw' not in l: pass
