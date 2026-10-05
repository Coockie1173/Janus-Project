#!/usr/bin/env python3
# Finds the moments a run's picture jumps and jumps back within a moment (a flash):
#     tools/flash_scan.py DIR      DIR holds video.mkv and video.index (tools/flicker_run.sh, or
#                                  WIIKIT_THUMBS / WIIKIT_THUMBS_INDEX by hand)
# Prints each one with its pictures, the frame and the time; ones of under 4 pictures are mostly
# the game's own (rain, lightning) and aren't counted.
import sys,subprocess,statistics
d=sys.argv[1]; S=880
subprocess.run(['ffmpeg','-loglevel','error','-y','-i',d+'/video.mkv','-vf','scale=40:22,format=gray','-f','rawvideo',d+'/gray.raw'],check=True)
a=open(d+'/gray.raw','rb').read(); n=len(a)//S
fr=[a[i*S:(i+1)*S] for i in range(n)]
def diff(x,y): return sum(abs(p-q) for p,q in zip(x,y))/S
idx=[l.split() for l in open(d+'/video.index')]
st=next(i for i,l in enumerate(idx) if float(l[2])>26)
dd=[0]*n
for i in range(st,n): dd[i]=diff(fr[i],fr[i-1])
i=st+31; ev=0
while i<n:
    base=statistics.median(dd[i-30:i])
    if dd[i]>max(8,4*base):
        back=None
        for m in range(i+1,min(n,i+150)):
            if dd[m]>max(2,3*base) and diff(fr[m],fr[i-1])<0.5*diff(fr[m-1],fr[i-1]): back=m;break
        if back:
            ev+=(back-i>=4); print('  flash: pictures %d..%d (%d), frame %s, %.2f s, jump %.1f'%(i,back,back-i,idx[min(i,len(idx)-1)][1],float(idx[min(i,len(idx)-1)][2]),dd[i])); i=back+1; continue
    i+=1
print(d,'pictures',n,'flashes',ev)
