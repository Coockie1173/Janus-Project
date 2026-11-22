#!/usr/bin/env python3
"""Scales a level that eurochef's `ge new-map` made, without its glTF: every position in the
geometry file (mg_NAME.edb) times FACTOR around the origin, and the triggers of each of its
level files (mt_NAME.edb, and the one in every gamemode's folder) moved to match.

    tools/scale_level.py modpacks/classicspack/mg_archives_classic.edb 1.15

The files are written in place; the old ones go to --backup (build/scaled_backup/ by default)
the first time, so scaling again starts from the originals unless --from-scaled is given.

What is scaled in the geometry file, the way geomap.rs writes it: the map's bounds, the zones'
boxes, the zone tree's planes, the portals' corners, the placements, the placement trees' boxes,
every mesh (bounds, spheres, area, positions, its collision variants), every split group (its
tree of boxes and its meshes) and the zones' edges and ladders.
"""

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EUROCHEF = ROOT / "eurochef/target/release/eurochef-cli"

ENTITY_MESH = 0x601
ENTITY_SPLIT = 0x603
ENTITY_ZONE = 0x608
TRIGGER_LEVEL = 0x3E  # the trigger that loads the geometry, not a place


class Level:
    def __init__(self, data: bytes, factor: float):
        self.buf = bytearray(data)
        self.s = factor
        self.done = set()  # float offsets already scaled, nothing twice
        self.seen = set()  # entities walked

    def u8(self, at):
        return self.buf[at]

    def u16(self, at):
        return struct.unpack_from(">H", self.buf, at)[0]

    def i16(self, at):
        return struct.unpack_from(">h", self.buf, at)[0]

    def u32(self, at):
        return struct.unpack_from(">I", self.buf, at)[0]

    def i32(self, at):
        return struct.unpack_from(">i", self.buf, at)[0]

    def rel(self, at):
        """A relative pointer: an offset from where it is"""
        return (at + self.i32(at)) & 0xFFFFFFFF

    def scale(self, at, count=1, by=None):
        by = self.s if by is None else by
        for i in range(count):
            p = at + 4 * i
            if p in self.done:
                raise RuntimeError(f"0x{p:X} scaled twice")
            self.done.add(p)
            v = struct.unpack_from(">f", self.buf, p)[0]
            struct.pack_into(">f", self.buf, p, v * by)

    # entities

    def entity(self, at):
        if at in self.seen:
            return
        self.seen.add(at)
        kind = self.u32(at)
        if kind == ENTITY_MESH:
            self.mesh(at)
        elif kind == ENTITY_SPLIT:
            self.split(at)
        elif kind == ENTITY_ZONE:
            self.zone_entity(at)
        else:
            raise RuntimeError(f"entity 0x{kind:X} at 0x{at:X} unknown")

    def variants(self, at):
        """(id, address) of each variant an entity's word at +0x40 lists"""
        word = self.u32(at + 0x40)
        if word == 0:
            return []
        table = at + 0x40 + (word >> 8)
        out = []
        for i in range(bin(word & 0xFF).count("1")):
            w = self.u32(table + 4 * i)
            out.append((w & 0xFF, table + 4 * i + (w >> 8)))
        return out

    def mesh(self, at):
        self.scale(at + 0x0C, by=self.s * self.s)  # area
        self.scale(at + 0x10, 3)  # min
        self.scale(at + 0x20, 3)  # max
        self.scale(at + 0x30, 4)  # the group's sphere
        self.scale(at + 0x7C, 4)  # own sphere
        self.scale(at + 0x8C, 3)
        self.scale(at + 0x98, 3)
        positions = self.rel(at + 0x5C)
        for i in range(self.u32(at + 0xA8)):
            self.scale(positions + 16 * i, 3)
        for vid, v in self.variants(at):
            if vid not in (0, 1):
                raise RuntimeError(f"mesh 0x{at:X} has variant {vid}")
            self.entity(v)

    def split(self, at):
        self.scale(at + 0x0C, by=self.s * self.s)
        self.scale(at + 0x10, 3)
        self.scale(at + 0x20, 3)
        self.scale(at + 0x30, 4)
        count = self.u32(at + 0x54)
        if count == 0:
            return
        self.tree_group(self.rel(at + 0x58))
        for i in range(count):
            self.entity(self.rel(at + 0x5C + 4 * i))

    def tree_group(self, at):
        n = abs(self.i32(at + 0x0C))
        for i in range(n):
            node = at + 0x30 * i
            self.scale(node, 3)
            self.scale(node + 0x10, 3)
            self.scale(node + 0x20, 4)
            if self.i32(node + 0x0C) > 0:
                self.tree_group(self.rel(node + 0x1C))

    def zone_entity(self, at):
        for vid, v in self.variants(at):
            if vid != 6:
                raise RuntimeError(f"zone entity 0x{at:X} has variant {vid}")
            self.edges(v)

    def edges(self, at):
        chains, p_chains = self.u32(at), self.rel(at + 4)
        polygons, p_polygons = self.u32(at + 8), self.rel(at + 12)
        p = p_polygons
        for _ in range(polygons):
            corners = self.u8(p)
            p += 16  # the header's float is the way the ladder's top runs
            for _ in range(corners):
                self.scale(p, 3)
                p += 16
        p = p_chains
        for _ in range(chains):
            n = self.u8(p)
            self.scale(p + 4, 3)
            p += 16
            for _ in range(n):
                self.scale(p + 4, 3)
                p += 16

    # the map

    def placement_node(self, at):
        children = self.u8(at)
        for i in range(children):
            c = at + 4 + 0x1C * i
            self.scale(c + 4, 6)
            self.placement_node(self.rel(c))

    def run(self):
        b = self.buf
        if b[:4] != b"GEOM":
            raise RuntimeError("not a geometry file")
        # list heads: count, hashes, relative pointer
        refptr_count, p_refptrs = self.i16(0x48), self.rel(0x4C)
        entity_count, p_entities = self.i16(0x50), self.rel(0x54)
        p_maps = self.rel(0x74)

        m = self.u32(p_maps + 8)
        if self.u32(m) != 0x500:
            raise RuntimeError("map not where it should be")
        self.scale(m + 0x6C, 6)  # bounds
        zones = self.u32(m + 0x84)

        # the zone tree: a child above 0 is a node
        tree = self.rel(m + 4)
        todo, nodes = [0], set()
        while todo:
            n = todo.pop()
            if n in nodes:
                continue
            nodes.add(n)
            node = tree + 32 * n
            self.scale(node + 12)  # the plane's distance
            for c in (self.i16(node + 16), self.i16(node + 18)):
                if c > 0:
                    todo.append(c)

        for i in range(self.u32(m + 0x38)):
            self.scale(self.rel(m + 0x3C) + 0x44 * i + 0x14, 12)

        for i in range(self.u32(m + 0x48)):
            self.scale(self.rel(m + 0x4C) + 0x3C * i + 4, 3)

        for z in range(zones):
            zone = m + 0x88 + 0x8C * z
            self.scale(zone + 0x68, 6)
            info = self.rel(zone + 0x28)
            self.placement_node(self.rel(info + 8))

        for i in range(entity_count):
            self.entity(self.u32(p_entities + 20 * i + 8))
        for i in range(refptr_count):
            self.entity(self.u32(p_refptrs + 16 * i + 8))
        return bytes(self.buf)


def triggers(path):
    out = subprocess.run([EUROCHEF, "ge", "triggers", path], check=True, capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines()[1:]:
        f = line.split()
        if len(f) > 7 and f[1] == "type" and f[4] == "at":
            found.append((int(f[0]), int(f[2]), [float(x) for x in f[5:8]]))
    return found


def scale_triggers(path, factor):
    moves = []
    for index, kind, pos in triggers(path):
        if kind != TRIGGER_LEVEL:
            moves += ["--move", f"{index}:" + ",".join(f"{v * factor:.4f}" for v in pos)]
    if moves:
        subprocess.run([EUROCHEF, "ge", "edit-triggers", path, "-o", path, *moves], check=True, capture_output=True)
    return len(moves) // 2


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("geometry", type=Path, help="mg_NAME.edb")
    ap.add_argument("factor", type=float)
    ap.add_argument("--backup", type=Path, default=ROOT / "build/scaled_backup")
    ap.add_argument("--from-scaled", action="store_true", help="scale the files as they are now, not the backups")
    args = ap.parse_args()

    mg = args.geometry.resolve()
    name = mg.name[3:]  # after mg_
    pack = mg.parent
    files = [mg] + sorted(pack.glob(f"mt_{name}")) + sorted(pack.glob(f"*/mt_{name}"))
    backup = args.backup / pack.name
    for f in files:
        keep = backup / f.relative_to(pack)
        if not keep.exists():
            keep.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, keep)
        elif not args.from_scaled:
            shutil.copy2(keep, f)

    mg.write_bytes(Level(mg.read_bytes(), args.factor).run())
    print(f"{mg.relative_to(ROOT) if mg.is_relative_to(ROOT) else mg}: x{args.factor}")
    for f in files[1:]:
        print(f"{f.relative_to(pack)}: {scale_triggers(f, args.factor)} triggers moved")
    print(f"originals in {backup}")


if __name__ == "__main__":
    sys.exit(main())
