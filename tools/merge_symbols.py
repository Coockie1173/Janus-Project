#!/usr/bin/env python3
"""Mix the three symbol sources into the names the recompiler and runtime use.

    python tools/merge_symbols.py [--dol game/sys/main.dol] [--out build/names.tsv]

Sources, all for SJBE52 (GoldenEye 007, Wii, NTSC-U), none fully accurate:

    config/symbols/symbols.txt     dtk-style, the more canonical SDK/MSL names
    config/symbols/symbols-2.txt   dtk-style, Ghidra-flavoured names
    config/symbols/main.dol.map    Ghidra map export
    config/names-manual.tsv        names found by hand, each with its evidence

For every function address one name is chosen, by rank:

    manual  >  symbols.txt  >  symbols-2.txt  >  main.dol.map  >  signatures  >  fn_XXXXXXXX

(signatures: build/sig_guess.tsv from tools/sigmatch.py and Dolphin's
totaldb.dsy, if present; guesses, so a name matched at several addresses or
on a function under 0x30 bytes is not taken, and they never add a seed.)

where a source only counts if its name is a real one (not fn_/FUN_/lbl_/...).
Every function address any source knows becomes a discovery seed, so the
boundaries of all three are mixed as well; an address outside the DOL's text,
or not word-aligned, is dropped and reported.

Writes:
    <out>                 addr, size, bind, type, section, name, source  (functions)
    <out minus .tsv>.data.tsv   addr, size, section, name, source        (data, for reading)
    <out minus .tsv>.report.txt what each source gave, conflicts between real names
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
AUTO = re.compile(r"^(fn_|FUN_|lbl_|LAB_|DAT_|sub_|thunk_FUN_|caseD_|switchD|switchdataD|"
                  r"nullsub|@|jumptable_|PTR_|UNK_|FLOAT_|DOUBLE_|s_|u_)", re.I)
DTK = re.compile(r"^(\S+) = (\S+):0x([0-9A-Fa-f]+); // (.*)$")


def is_auto(name):
    return bool(AUTO.match(name)) or re.fullmatch(r"[0-9A-Fa-f]{8}", name) is not None


def text_ranges(dol):
    d = open(dol, "rb").read(0x100)
    off = struct.unpack(">18I", d[0:0x48])
    addr = struct.unpack(">18I", d[0x48:0x90])
    size = struct.unpack(">18I", d[0x90:0xD8])
    return [(addr[i], addr[i] + size[i]) for i in range(7) if size[i]]


def read_dtk(path):
    """[(addr, name, section, is_function, size)]"""
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = DTK.match(line.rstrip("\n"))
            if not m:
                continue
            attrs = dict(a.split(":", 1) for a in m[4].split() if ":" in a)
            size = int(attrs.get("size", "0"), 16)
            out.append((int(m[3], 16), m[1], m[2], attrs.get("type") == "function", size))
    return out


def read_map(path):
    """[(addr, name, size)] of the map's top-level ("Global") entries."""
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.split()
            if len(p) < 6 or p[-1] != "Global":
                continue
            try:
                out.append((int(p[0], 16), p[4], int(p[1], 16)))
            except ValueError:
                pass
    return out


def read_manual(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) >= 2 and not line.startswith("#"):
                try:
                    out[int(p[0], 16)] = p[1]
                except ValueError:
                    pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dol", default=os.path.join(ROOT, "game/sys/main.dol"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/names.tsv"))
    ap.add_argument("--sym-dir", default=os.path.join(ROOT, "config/symbols"))
    ap.add_argument("--sig", default=os.path.join(ROOT, "build/sig_guess.tsv"))
    ap.add_argument("--manual", default=os.path.join(ROOT, "config/names-manual.tsv"))
    a = ap.parse_args()

    text = text_ranges(a.dol)
    in_text = lambda x: any(lo <= x < hi for lo, hi in text)

    s1 = read_dtk(os.path.join(a.sym_dir, "symbols.txt"))
    s2 = read_dtk(os.path.join(a.sym_dir, "symbols-2.txt"))
    mp = read_map(os.path.join(a.sym_dir, "main.dol.map"))
    manual = read_manual(a.manual)

    #function candidates per address: {addr: {source: (name, size)}}
    funcs, data, dropped = {}, {}, []
    for src, syms in (("symbols.txt", s1), ("symbols-2.txt", s2)):
        for addr, name, section, is_fn, size in syms:
            if is_fn:
                if in_text(addr) and addr % 4 == 0:
                    funcs.setdefault(addr, {}).setdefault(src, (name, size))
                else:
                    dropped.append((addr, name, src))
            elif not in_text(addr):
                data.setdefault(addr, {}).setdefault(src, (name, size, section))
    for addr, name, size in mp:
        if in_text(addr):
            #the map has no types: in text, only an address a dtk source also
            #calls a function, or a real name, is taken as a function
            if addr % 4 == 0 and (addr in funcs or not is_auto(name)):
                funcs.setdefault(addr, {}).setdefault("main.dol.map", (name, size))
        elif not is_auto(name):
            data.setdefault(addr, {}).setdefault("main.dol.map", (name, size, ""))
    for addr, name in manual.items():
        if in_text(addr):
            funcs.setdefault(addr, {})["manual"] = (name, 0)
    if os.path.exists(a.sig):
        sig, count = [], {}
        with open(a.sig, encoding="utf-8") as f:
            next(f)
            for line in f:
                p = line.rstrip("\n").split("\t")
                addr, size, name = int(p[0], 16), int(p[1], 16), p[2]
                sig.append((addr, size, name))
                count[name] = count.get(name, 0) + 1
        for addr, size, name in sig:
            #C++ signatures (templates, methods) are other games' code: only plain C names
            if count[name] == 1 and size >= 0x30 and addr in funcs and re.fullmatch(r"\w+", name):
                funcs[addr].setdefault("signatures", (name, size))

    rank = ("manual", "symbols.txt", "symbols-2.txt", "main.dol.map", "signatures")
    chosen, used, conflicts, taken = {}, {}, [], {}
    def best(addr):
        c = funcs[addr]
        return next((i for i, s in enumerate(rank) if s in c and not is_auto(c[s][0])), len(rank))
    #the better source claims a name first, should two addresses want it
    for addr in sorted(funcs, key=lambda x: (best(x), x)):
        c = funcs[addr]
        real = [(s, c[s][0]) for s in rank if s in c and not is_auto(c[s][0])]
        size = max(v[1] for v in c.values())
        if real:
            src, name = real[0]
            others = {n for _, n in real[1:]} - {name}
            if others:
                conflicts.append((addr, name, src, sorted(others)))
        else:
            src, name = "auto", f"fn_{addr:08X}"
        name = re.sub(r"\s", "_", name)
        if name in taken: #two addresses, one name: hooks look names up
            name = f"{name}_{addr:08X}"
        taken[name] = addr
        chosen[addr] = (name, size, src)
        used[src] = used.get(src, 0) + 1

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    base = a.out[:-4] if a.out.endswith(".tsv") else a.out
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("addr\tsize\tbind\ttype\tsection\tname\tsource\n")
        for addr, (name, size, src) in sorted(chosen.items()):
            f.write(f"{addr:08X}\t{size}\t1\t2\t.text\t{name}\t{src}\n")
    with open(base + ".data.tsv", "w", encoding="utf-8") as f:
        f.write("addr\tsize\tsection\tname\tsource\n")
        for addr in sorted(data):
            c = data[addr]
            real = [s for s in rank if s in c and not is_auto(c[s][0])]
            if real:
                name, size, section = c[real[0]]
                f.write(f"{addr:08X}\t{size}\t{section}\t{name}\t{real[0]}\n")
    with open(base + ".report.txt", "w", encoding="utf-8") as f:
        f.write(f"functions        {len(chosen)}\n")
        for s in rank + ("auto",):
            f.write(f"  name from {s:<15} {used.get(s, 0)}\n")
        for s, syms in (("symbols.txt", s1), ("symbols-2.txt", s2)):
            only = [x for x in funcs if list(funcs[x]) == [s]]
            f.write(f"functions only in {s}: {len(only)}  {' '.join(f'{x:08X}' for x in only[:40])}\n")
        f.write(f"dropped (not in text / unaligned): {len(dropped)}\n")
        for addr, name, src in dropped:
            f.write(f"  {addr:08X} {name} ({src})\n")
        f.write(f"real-name conflicts: {len(conflicts)}\n")
        for addr, name, src, others in conflicts:
            f.write(f"  {addr:08X} {name} [{src}]  vs  {', '.join(others)}\n")
    named = len(chosen) - used.get("auto", 0)
    print(f"merge_symbols: {len(chosen)} functions, {named} named "
          f"({', '.join(f'{used.get(s, 0)} {s}' for s in rank)}), "
          f"{len(conflicts)} conflicts, {len(dropped)} dropped -> {a.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
