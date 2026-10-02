#!/usr/bin/env python3
"""Add a "Player struct" group to a Dolphin Memory Engine watch list (.dmw).

    python3 tools/dmw_player.py "GoldenEye 007 (Wii).dmw" [build/obj.txt]

Every word of the player object, through the pointer at 806EB698: the fields the port has
found, named; the rest as unk_XXX, typed by what they held in a dump of the running game
(build/obj.txt, from GE_OBJ="806EB698:1000:t,t,..."; without one they are hexadecimal words).
The group is replaced if it is already there.
"""
import json
import struct
import sys

GROUP = "Player struct (806EB698 ->)"
SIZE = 0x720                      # where the dump stops looking like one object
PTR = "806EB698"

KNOWN = {                         # offset: (name, type) with f float, x hex word, d word, p pointer
    0x004: ("unk_004", "x"),
    0x00C: ("flags (halfword at +0xC)", "x"),
    0x060: ("-> into the controller object", "p"),
    0x098: ("transform m00 (cos yaw)", "f"), 0x09C: ("transform m01", "f"), 0x0A0: ("transform m02 (sin yaw)", "f"),
    0x0A4: ("transform m03", "f"), 0x0A8: ("transform m10", "f"), 0x0AC: ("transform m11", "f"),
    0x0B0: ("transform m12", "f"), 0x0B4: ("transform m13", "f"), 0x0B8: ("transform m20 (-sin yaw)", "f"),
    0x0BC: ("transform m21", "f"), 0x0C0: ("transform m22 (cos yaw)", "f"), 0x0C4: ("transform m23", "f"),
    0x0C8: ("transform position X", "f"), 0x0CC: ("transform position Y", "f"), 0x0D0: ("transform position Z", "f"),
    0x0D4: ("transform m33", "f"),
    0x0D8: ("rotation X", "f"), 0x0DC: ("rotation Y: yaw (radians, the mouse turns this)", "f"), 0x0E0: ("rotation Z", "f"),
    0x0F8: ("position X", "f"), 0x0FC: ("position Y (height)", "f"), 0x100: ("position Z", "f"),
    0x14C: ("-> components (list head)", "p"),
    0x158: ("-> (checked by the look update)", "p"),
    0x16C: ("-> view object (pitch at +0x768)", "p"),
    0x184: ("state word", "x"),
    0x23C: ("position X (previous)", "f"), 0x240: ("position Y (previous)", "f"), 0x244: ("position Z (previous)", "f"),
    0x250: ("yaw (copy of +0xDC)", "f"),
    0x25C: ("velocity X", "f"), 0x260: ("velocity Y", "f"), 0x264: ("velocity Z", "f"),
    0x29C: ("velocity X (copy)", "f"), 0x2A0: ("velocity Y (copy)", "f"), 0x2A4: ("velocity Z (copy)", "f"),
    0x2FC: ("Z velocity (watch list's)", "d"),
    0x340: ("body size (0.425 standing, 0.5 crouched)", "f"),
    0x34C: ("Z velocity (watch list's)", "d"),
    0x390: ("crouch amount? (0.15 standing, 0.65 crouched)", "f"),
    0x3C0: ("stance (0.5 standing, 0.825 crouched)", "f"), 0x3C8: ("stance (copy)", "f"),
}


def dump_types(path):
    """{offset: type} from a GE_OBJ dump: p if it ever held a guest address, f if it reads as floats."""
    types = {}
    try:
        lines = open(path).read().split("\n")
    except OSError:
        return types
    for line in lines:
        if not line.startswith("+"):
            continue
        p = line.split()
        off, words = int(p[0][1:], 16), [int(x, 16) for x in p[1::2]]
        if any(0x80003000 <= w < 0x81800000 or 0x90000000 <= w < 0x94000000 for w in words):
            types[off] = "p"
        elif not any(words):
            types[off] = "d"
        else:
            fl = [struct.unpack(">f", struct.pack(">I", w))[0] for w in words if w]
            types[off] = "f" if all(1e-6 < abs(v) < 1e7 for v in fl) else ("d" if all(w < 0x10000 for w in words) else "x")
    return types


def entry(label, offsets, t):
    e = {"address": PTR, "baseIndex": 1 if t in "xp" else 0, "label": label, "pointerOffsets": offsets,
         "typeIndex": 3 if t == "f" else 2, "unsigned": t != "f" and t != "d"}
    return e


def main():
    path = sys.argv[1]
    types = dump_types(sys.argv[2] if len(sys.argv) > 2 else "build/obj.txt")
    fields = []
    for off in range(0, SIZE, 4):
        name, t = KNOWN.get(off, (None, None))
        if name is None:
            t = types.get(off, "x")
            name = {"p": "ptr", "f": "f", "d": "unk", "x": "unk"}[t] + f"_{off:03X}"
        fields.append(entry(f"+{off:03X}  {name}", [f"{off:X}"], t))
    view = {"groupName": "View object (player +0x16C ->)", "groupEntries": [
        entry("+768  pitch (radians, up is negative; the mouse writes this)", ["16C", "768"], "f"),
        entry("+760  pitch (follows +768)", ["16C", "760"], "f"),
    ]}
    named = {"groupName": "Named fields", "groupEntries": [
        entry(f"+{off:03X}  {KNOWN[off][0]}", [f"{off:X}"], KNOWN[off][1]) for off in sorted(KNOWN)]}
    group = {"groupName": GROUP, "groupEntries": [named, view,
             {"groupName": f"Every word (0x000 - 0x{SIZE - 4:03X})", "groupEntries": fields}]}
    d = json.load(open(path, encoding="utf-8"))
    d["watchList"] = [group] + [g for g in d["watchList"] if g.get("groupName") != GROUP]
    with open(path, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=4)
    print(f"{path}: \"{GROUP}\": {len(named['groupEntries'])} named, {len(fields)} words, 2 in the view object")


if __name__ == "__main__":
    main()
