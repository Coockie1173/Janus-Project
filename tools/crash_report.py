#!/usr/bin/env python3
"""Finish a crash dump: name its host frames and show the guest code around its addresses.

    python3 tools/crash_report.py [build/crashes/<dump>]     (the newest one by default)

The game writes report.txt while it dies (port/debug/crashdump.cpp) and can't do much more than
offsets there. This adds analysis.txt next to it: every wiiboot+0x... frame named from the
executable's symbols, and a disassembly around the guest addresses of the crashing thread's
call chain. Only right while build/out/wiiboot is still the build that crashed.
"""
import bisect, os, re, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
EXE = os.path.join(ROOT, "build", "out", "wiiboot")
DOL = os.path.join(ROOT, "build", "patched", "main.dol")
CRASHES = os.path.join(ROOT, "build", "crashes")


def newest_dump():
    dumps = sorted(d for d in os.listdir(CRASHES) if os.path.isdir(os.path.join(CRASHES, d))) if os.path.isdir(CRASHES) else []
    return os.path.join(CRASHES, dumps[-1]) if dumps else None


def load_symbols():
    #same idea as profile_report.py: offsets in the report are from __executable_start
    syms, base = [], 0
    out = subprocess.run(["nm", "-C", "-n", "--defined-only", EXE], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split(" ", 2)
        if len(p) == 3:
            if p[2] == "__executable_start":
                base = int(p[0], 16)
            if p[1] in "tTwW":
                syms.append((int(p[0], 16), p[2]))
    return syms, base


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else newest_dump()
    if not dump or not os.path.exists(os.path.join(dump, "report.txt")):
        print("no crash dump found")
        return 1
    report = open(os.path.join(dump, "report.txt")).read()
    lines = report.splitlines()
    out = ["analysis of " + os.path.basename(dump), "", lines[2] if len(lines) > 2 else "", ""]

    if os.path.getmtime(EXE) > os.path.getmtime(os.path.join(dump, "report.txt")):
        out += ["!! build/out/wiiboot was rebuilt after this dump, the host names below may be off", ""]

    syms, base = load_symbols()
    addrs = [s[0] for s in syms]
    out.append("host call stack:")
    for m in re.finditer(r"^(host thread .*)|^  wiiboot\+0x([0-9a-f]+)", report, re.M):
        if m.group(1): #a stall dump has one stack for every host thread
            out.append(m.group(1))
            continue
        off = int(m.group(2), 16)
        i = bisect.bisect_right(addrs, base + off) - 1
        name = syms[i][1] if i >= 0 else "?"
        out.append(f"  wiiboot+0x{off:x}  {name}+0x{base + off - syms[i][0]:x}" if i >= 0 else f"  wiiboot+0x{off:x}  ?")

    #the crashing thread's chain is the first block of the guest threads section
    guest = []
    section = report.split("== guest threads ==", 1)[-1].split("thread ", 2)
    block = section[1] if len(section) > 1 else ""
    for m in re.finditer(r"^  (?:lr  )?([89][0-9A-F]{7})  ", block, re.M):
        a = int(m.group(1), 16)
        if a not in guest:
            guest.append(a)
    dol = DOL if os.path.exists(DOL) else os.path.join(ROOT, "game", "sys", "main.dol")
    out += ["", "guest code around the crashing thread's return addresses (the call is the line before each):"]
    for a in guest[:6]:
        dis = subprocess.run([sys.executable, "-m", "wiikit.ppc", dol, "--at", f"{a - 0x18:08X}", "--count", "10"],
                             capture_output=True, text=True, cwd=ROOT).stdout
        out += ["", f"-- {a:08X}"] + [("=>" if l.strip().startswith(f"{a:08X}") else "  ") + l for l in dis.splitlines()]

    text = "\n".join(out) + "\n"
    open(os.path.join(dump, "analysis.txt"), "w").write(text)
    print(text)
    print(f"written to {os.path.join(dump, 'analysis.txt')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
