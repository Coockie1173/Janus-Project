#!/usr/bin/env python3
"""Name the samples of a GE_PROFILE run.

    GE_PROFILE=build/profile.bin ./launch.sh ...
    python3 tools/profile_report.py build/profile.bin [--last N] [--top 40]

Samples are program counters as offsets in build/out/wiiboot; recompiled
functions (f_XXXXXXXX, orig_XXXXXXXX) are shown with their guest names.
--last N keeps only the last N samples (the end of the run: the scene reached).
"""
import argparse, bisect, collections, os, struct, subprocess

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
ap = argparse.ArgumentParser()
ap.add_argument("samples")
ap.add_argument("--exe", default=os.path.join(ROOT, "build/out/wiiboot"))
ap.add_argument("--last", type=int, default=0)
ap.add_argument("--top", type=int, default=40)
a = ap.parse_args()

syms, base = [], None
for line in subprocess.run(["nm", "-C", "-n", "--defined-only", a.exe], capture_output=True, text=True).stdout.splitlines():
    p = line.split(" ", 2)
    if len(p) == 3:
        if p[2] == "__executable_start":
            base = int(p[0], 16)
        if p[1] in "tTwW":
            syms.append((int(p[0], 16), p[2]))
base = base or 0
addrs = [s[0] for s in syms]
guest = {}
tsv = os.path.join(ROOT, "build/recomp/symbols.tsv")
if os.path.exists(tsv):
    for l in open(tsv).read().split("\n")[1:]:
        p = l.split("\t")
        if len(p) > 5:
            guest[p[0].upper()] = p[5]
data = open(a.samples, "rb").read()
s = struct.unpack("<%dI" % (len(data) // 4), data)
if a.last:
    s = s[-a.last:]
hist = collections.Counter()
for off in s:
    i = bisect.bisect_right(addrs, base + off) - 1
    name = syms[i][1] if 0 <= i and off < 0x10000000 else "(outside the executable: libraries, the GL driver)"
    for pre in ("f_", "orig_"):
        if name.startswith(pre) and name[len(pre):len(pre) + 8].upper() in guest:
            name = f"{name[:len(pre) + 8]} {guest[name[len(pre):len(pre) + 8].upper()]}"
    hist[name] += 1
print(f"{len(s)} samples")
for name, n in hist.most_common(a.top):
    print(f"{100 * n / len(s):6.2f}%  {name}")
