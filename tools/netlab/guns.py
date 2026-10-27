#!/usr/bin/env python3
# tools/netlab/guns.py LOG...: from GE_TRACEOWNED's "owned: items here/there" pairs, how often a joined
# console's own hand held another gun than the host's copy, or an empty item (a gun given away)
import re, sys

def held(line):
    m = re.search(r"hand (-?\d+) next -?\d+:(.*)", line)
    items = [i.split("/") for i in m.group(2).split()]
    h = int(m.group(1))
    return (items[h][0], int(items[h][2])) if 0 <= h < len(items) else (None, 0)

for path in sys.argv[1:]:
    pairs = apart = empty = takes = 0
    here = None
    for line in open(path, errors="replace"):
        if "owned: items here" in line: here = held(line)
        elif "owned: items there" in line and here:
            there = held(line)
            pairs += 1
            apart += here[0] != there[0]
            empty += here[0] is not None and here[1] <= 0
            here = None
        elif "host's weapon after" in line: takes += 1
    print(f"{path}: {pairs} changes, another gun than the host's in {apart}, an empty one in {empty}, {takes} takes after waiting")
