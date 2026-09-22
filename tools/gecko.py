#!/usr/bin/env python3
"""Apply the Gecko codes of config/gecko.txt.

    python3 tools/gecko.py game/sys/main.dol config/gecko.txt --out build/patched

Writes, into --out:
    main.dol     the executable with every write that lands in its code or data applied
                 (what the recompiler reads: recompiled code cannot be patched at run time)
    gecko.ram    "addr size value" lines for the port to store every frame: the writes
                 outside the code (data, the heap), as a code handler would repeat them
    gecko.report what each code did

Code types: 00 (bytes, with fill), 02 (halfwords, with fill), 04 (word), at ba = 80000000.
"""
import argparse
import os
import struct
import sys


def parse(path):
    """[(name, [(a, b)])]"""
    codes, cur = [], None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line[0] in "#*":
                continue
            if line[0] == "$":
                cur = (line[1:].strip(), [])
                codes.append(cur)
                continue
            p = line.split()
            if cur is None or len(p) != 2:
                sys.exit(f"{path}: cannot read \"{line}\"")
            try:
                cur[1].append((int(p[0], 16), int(p[1], 16)))
            except ValueError:
                sys.exit(f"{path}: \"{line}\" has placeholders (X, Y, Z...): fill them in")
    return codes


def writes(lines):
    """[(addr, size, value)] of a code, or None if it has a line that is not a RAM write."""
    out = []
    for a, b in lines:
        typ = a >> 24 & 0xFE
        addr = 0x80000000 | (a & 0x01FFFFFF)
        if a & 0x10000000:
            return None                                   # relative to the pointer: not static
        if typ == 0x00:
            for i in range((b >> 16) + 1):
                out.append((addr + i, 1, b & 0xFF))
        elif typ == 0x02:
            for i in range((b >> 16) + 1):
                out.append((addr + 2 * i, 2, b & 0xFFFF))
        elif typ == 0x04:
            out.append((addr, 4, b))
        else:
            return None
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dol")
    ap.add_argument("codes")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    dol = bytearray(open(a.dol, "rb").read())
    off = struct.unpack(">18I", dol[0:0x48])
    adr = struct.unpack(">18I", dol[0x48:0x90])
    siz = struct.unpack(">18I", dol[0x90:0xD8])
    secs = [(adr[i], adr[i] + siz[i], off[i], i < 7) for i in range(18) if siz[i]]

    ram, report, in_code = [], [], 0
    for name, lines in parse(a.codes) if os.path.exists(a.codes) else []:
        w = writes(lines)
        if w is None:
            report.append(f"SKIPPED  {name}: has a code type other than 00/02/04")
            print(f"gecko: skipped \"{name}\" (only RAM writes 00/02/04 are supported)", file=sys.stderr)
            continue
        n_code = 0
        for addr, size, value in w:
            sec = next((s for s in secs if s[0] <= addr and addr + size <= s[1]), None)
            if sec:
                o = sec[2] + addr - sec[0]
                dol[o:o + size] = value.to_bytes(size, "big")
                n_code += sec[3]
            if not (sec and sec[3]):
                ram.append((addr, size, value))            # data and heap: kept up while the game runs
        in_code += n_code
        report.append(f"APPLIED  {name}: {len(w)} write(s), {n_code} in code")

    os.makedirs(a.out, exist_ok=True)

    def put(name, data, mode):
        path = os.path.join(a.out, name)
        try:
            if open(path, "rb").read() == (data if mode == "wb" else data.encode()):
                return                                     # unchanged: nothing downstream rebuilds
        except OSError:
            pass
        with open(path, mode) as f:
            f.write(data)

    put("main.dol", bytes(dol), "wb")
    put("gecko.ram", "".join(f"{x:08X} {s} {v:X}\n" for x, s, v in ram), "w")
    put("gecko.report", "\n".join(report) + "\n", "w")
    print(f"gecko: {len(report)} code(s), {in_code} write(s) into code, {len(ram)} kept up at run time",
          file=sys.stderr)


if __name__ == "__main__":
    main()
