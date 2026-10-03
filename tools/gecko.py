#!/usr/bin/env python3
"""Apply the Gecko codes of config/gecko.txt.

    python3 tools/gecko.py game/sys/main.dol config/gecko.txt --out build/patched

Writes, into --out:
    main.dol     the executable with every write that lands in its code or data applied
                 (what the recompiler reads: recompiled code cannot be patched at run time)
    gecko.txt    the codes, or parts of codes, for the port's handler to run every frame:
                 writes outside the code (data, the heap), conditions and switches
    gecko.report what each code did

Baked into main.dol: plain writes (00, 02, 04, 08) that land in the code. For the handler:
those that land elsewhere, and codes with conditions (20-2E), switches (CC) and E0/E2.
Not supported: anything that inserts or changes code while the game runs (C0, C2, F6...).
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
    """[(addr, size, value)] of a code made only of plain RAM writes (00, 02, 04, 08), else None:
    a code with conditions or a switch is run by the port's handler while the game runs."""
    out, i = [], 0
    while i < len(lines):
        a, b = lines[i]
        i += 1
        typ = a >> 24 & 0xFE
        addr = 0x80000000 | (a & 0x01FFFFFF)
        if a & 0x10000000:
            return None #relative to the pointer: not static
        if typ == 0x00:
            for k in range((b >> 16) + 1):
                out.append((addr + k, 1, b & 0xFF))
        elif typ == 0x02:
            for k in range((b >> 16) + 1):
                out.append((addr + 2 * k, 2, b & 0xFFFF))
        elif typ == 0x04:
            out.append((addr, 4, b))
        elif typ == 0x08 and i < len(lines): #serial: value, then T NNN ZZZZ / value step
            c, step = lines[i]
            i += 1
            size = (1, 2, 4)[min(c >> 28, 2)]
            for k in range((c >> 16 & 0xFFF) + 1):
                out.append((addr + k * (c & 0xFFFF), size, (b + k * step) & (256 ** size - 1)))
        elif (a, b) == (0xE0000000, 0x80008000):
            pass
        else:
            return None
    return out


def text_writes(lines, secs):
    """Does a code the handler would run store into the executable's code? (It cannot.)"""
    for a, b in lines:
        typ = a >> 24 & 0xFE
        if typ in (0x00, 0x02, 0x04, 0x08) and not a & 0x10000000:
            addr = 0x80000000 | (a & 0x01FFFFFF)
            if any(s[0] <= addr < s[1] and s[3] for s in secs):
                return True
    return False


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

    run, report, in_code = [], [], 0
    for name, lines in parse(a.codes) if os.path.exists(a.codes) else []:
        w = writes(lines)
        if w is None:
            if any((x >> 24 & 0xFE) in (0xC0, 0xC2, 0xC6, 0xF2, 0xF6) for x, _ in lines) or text_writes(lines, secs):
                report.append(f"SKIPPED  {name}: inserts code or stores into code under a condition")
                print(f"gecko: skipped \"{name}\" (it changes the game's code while it runs)", file=sys.stderr)
            else:
                run.append((name, lines))
                report.append(f"HANDLER  {name}: run by the port while the game runs")
            continue
        n_code, rest = 0, []
        for addr, size, value in w:
            sec = next((s for s in secs if s[0] <= addr and addr + size <= s[1]), None)
            if sec:
                o = sec[2] + addr - sec[0]
                dol[o:o + size] = value.to_bytes(size, "big")
                n_code += sec[3]
            if not (sec and sec[3]): #data and heap: kept up while the game runs
                rest.append((((size >> 1) * 2) << 24 | (addr & 0x01FFFFFF), value))
        if rest:
            run.append((name, rest))
        in_code += n_code
        report.append(f"APPLIED  {name}: {len(w)} write(s), {n_code} in code")

    os.makedirs(a.out, exist_ok=True)

    def put(name, data, mode):
        path = os.path.join(a.out, name)
        try:
            if open(path, "rb").read() == (data if mode == "wb" else data.encode()):
                return #unchanged: nothing downstream rebuilds
        except OSError:
            pass
        with open(path, mode) as f:
            f.write(data)

    put("main.dol", bytes(dol), "wb")
    put("gecko.txt", "".join(f"${n}\n" + "".join(f"{x:08X} {y:08X}\n" for x, y in ls) for n, ls in run), "w")
    put("gecko.report", "\n".join(report) + "\n", "w")
    print(f"gecko: {len(report)} code(s), {in_code} write(s) into code, {len(run)} for the handler",
          file=sys.stderr)


if __name__ == "__main__":
    main()
