#!/usr/bin/env python3
"""Name the samples of a GE_PROFILE run.

    GE_PROFILE=build/profile.bin ./launch.sh ...
    python3 tools/profile_report.py build/profile.bin [--last N] [--top 40]

Samples are program counters as offsets in build/out/wiiboot; recompiled
functions (f_XXXXXXXX, orig_XXXXXXXX) are shown with their guest names.
--last N keeps only the last N samples (the end of the run: the scene reached).
--inclusive: the game thread's functions by the time spent in them and in what
they call (from FILE.stacks, the call stack at each sample).
--under ADDR [--depth N]: the game thread's samples inside the guest function ADDR,
split by the path of N calls it made towards them (where its inclusive time goes).
"""
import argparse, bisect, collections, os, struct, subprocess

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
ap = argparse.ArgumentParser()
ap.add_argument("samples", nargs="?")
ap.add_argument("--offsets", nargs="+", metavar="HEX", help="name these offsets instead of reading a sample file")
ap.add_argument("--exe", default=os.path.join(ROOT, "build/out/wiiboot"))
ap.add_argument("--last", type=int, default=0)
ap.add_argument("--top", type=int, default=40)
ap.add_argument("--inclusive", action="store_true")
ap.add_argument("--under", metavar="ADDR")
ap.add_argument("--depth", type=int, default=1)
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
if a.offsets:
    for o in a.offsets:
        i = bisect.bisect_right(addrs, base + int(o, 16)) - 1
        n = syms[i][1]
        g = guest.get(n.split("_", 1)[-1][:8].upper(), "") if n.startswith(("f_", "orig_")) else ""
        print(o, n, g)
    raise SystemExit
data = open(a.samples, "rb").read()
s = struct.unpack("<%dI" % (len(data) // 4), data)
pairs = list(zip(s[0::2], s[1::2])) #(thread, offset)
if a.last:
    pairs = pairs[-a.last:]


def name_of(off):
    i = bisect.bisect_right(addrs, base + off) - 1
    name = syms[i][1] if 0 <= i and off < 0x10000000 else "(outside the executable: libraries, the GL driver)"
    for pre in ("f_", "orig_"):
        if name.startswith(pre) and name[len(pre):len(pre) + 8].upper() in guest:
            name = f"{name[:len(pre) + 8]} {guest[name[len(pre):len(pre) + 8].upper()]}"
    return name


threads = collections.Counter(t for t, _ in pairs)
print(f"{len(pairs)} samples (1 ms of CPU time each), {len(threads)} threads")
for tid, n in threads.most_common(4):
    hist = collections.Counter(name_of(o) for t, o in pairs if t == tid)
    recomp = sum(v for k, v in hist.items() if k.startswith(("f_", "orig_")))
    print(f"\nthread {tid}: {100 * n / len(pairs):.1f}% of all samples; recompiled game code {100 * recomp / n:.1f}% of it")
    for name, k in hist.most_common(a.top):
        print(f"{100 * k / n:6.2f}%  {name}")

if a.inclusive or a.under:
    DEPTH = 40
    raw = open(a.samples + ".stacks", "rb").read()
    st = struct.unpack("<%dQ" % (len(raw) // 8), raw)
    stacks = [st[i:i + DEPTH] for i in range(0, len(st), DEPTH)]
    if a.last:
        stacks = stacks[-a.last:]
    exe_lo = exe_hi = 0
    for l in open(a.samples + ".maps"):
        p = l.split()
        if len(p) > 5 and os.path.realpath(p[5]) == os.path.realpath(a.exe):
            lo, hi = (int(x, 16) for x in p[0].split("-"))
            exe_lo, exe_hi = (min(exe_lo, lo) if exe_lo else lo), max(exe_hi, hi)
    #the game's thread is the one running recompiled code, not always the busiest one
    game = collections.Counter(t for t, o in pairs if name_of(o).startswith(("f_", "orig_")))
    tid = (game or threads).most_common(1)[0][0]
    inc, below, n, m = collections.Counter(), collections.Counter(), 0, 0
    for (t, off), chain in zip(pairs, stacks):
        if t != tid:
            continue
        n += 1
        #a return address: the call before it
        names = [name_of(pc - exe_lo - (1 if k else 0)) if exe_lo <= pc < exe_hi else "?" for k, pc in enumerate(chain)]
        for x in set(names) - {"?"}:
            inc[x] += 1
        if a.under:
            at = [k for k, x in enumerate(names) if x.split(" ")[0][-8:].upper() == a.under.upper()]
            if at:
                m += 1
                k = at[-1] #the outermost, for a function that recurses
                below[" > ".join(reversed(names[max(0, k - a.depth):k])) or "(itself)"] += 1
    if a.under:
        print(f"\nthread {tid}: {100 * m / n:.2f}% under {a.under}, by what it called ({a.depth} deep)")
        for path, k in below.most_common(a.top):
            print(f"{100 * k / n:6.2f}%  {path}")
        raise SystemExit
    print(f"\nthread {tid}, inclusive: a function and what it calls ({DEPTH} frames deep at most)")
    for name, k in inc.most_common(a.top):
        print(f"{100 * k / n:6.2f}%  {name}")
