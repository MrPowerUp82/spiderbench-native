"""Check SBCOLL1 bytes and 512 queries against results saved by the original JS."""
from array import array
import json
import math
from pathlib import Path
import struct
import sys

assert sys.byteorder == "little" and array("f").itemsize == 4 and array("I").itemsize == 4
root = Path(__file__).resolve().parents[2] / "build" / "city-bake"
manifest = json.loads((root / "collision.json").read_text())
oracle = json.loads((root / "queries.json").read_text())["queries"]

with (root / "collision.sbcol").open("rb") as f:
    def unpack(fmt):
        s = struct.Struct("<" + fmt)
        data = f.read(s.size)
        if len(data) != s.size:
            raise ValueError("truncated bake")
        return s.unpack(data)

    def read_array(code, n):
        a = array(code)
        data = f.read(n * a.itemsize)
        if len(data) != n * a.itemsize:
            raise ValueError("truncated bake array")
        a.frombytes(data)
        return a

    assert f.read(8) == b"SBCOLL1\0"
    version, n, nf, nzips, nboxes, nx, nz, ni = unpack("8I")
    cell, ox, oz, sx, sy, sz = unpack("6f")
    typ, flags, kind = (read_array("B", n) for _ in range(3))
    bb, par = read_array("f", n * 6), read_array("f", n * 6)
    start, items = read_array("I", nx * nz + 1), read_array("I", ni)
    fields = []
    for _ in range(nf):
        fx, fz = unpack("2I")
        fc, hmax, lomin = unpack("3f")
        fields.append((fx, fz, fc, read_array("f", fx * fz), read_array("f", fx * fz)))
    zip_pos, zip_kind = read_array("f", nzips * 6), read_array("B", nzips)
    box_pos = read_array("f", nboxes * 6)
    assert f.read(1) == b"", "trailing bytes"

assert version == 1
assert (n, nf, nzips, nboxes, nx, nz, ni) == (
    manifest["solids"], manifest["fields"], manifest["zipPoints"],
    manifest["buildingBoxes"], manifest["grid"]["nx"],
    manifest["grid"]["nz"], manifest["grid"]["items"])
assert start[-1] == ni and len(zip_pos) == nzips * 6 and len(box_pos) == nboxes * 6
assert sum(len(a[3]) for a in fields) == manifest["fieldCells"]

def top(i, x, z):
    j = i * 6
    if x < bb[j] or x > bb[j + 3] or z < bb[j + 2] or z > bb[j + 5]:
        return -math.inf
    if typ[i] == 0:
        return bb[j + 4]
    if typ[i] == 3:
        fx, fz, fc, h, _ = fields[int(par[j])]
        ix = min(fx - 1, math.floor((x - bb[j]) / fc))
        iz = min(fz - 1, math.floor((z - bb[j + 2]) / fc))
        v = h[iz * fx + ix]
        return par[j + 1] + v if v > -1e30 else -math.inf
    if typ[i] == 2:
        axis = int(par[j])
        return par[j + 1] + (par[j + 2] - par[j + 1]) * ((x if axis == 0 else z) - bb[j + axis]) / (bb[j + 3 + axis] - bb[j + axis])
    dx, dz = x - par[j], z - par[j + 1]
    d = math.hypot(dx, dz)
    r0, r1 = par[j + 2], par[j + 3]
    if d <= r1:
        return bb[j + 4]
    if d <= r0:
        return bb[j + 1] + (r0 - d) / (r0 - r1) * (bb[j + 4] - bb[j + 1])
    return -math.inf

def cell_items(x, z):
    cx, cz = math.floor((x - ox) / cell), math.floor((z - oz) / cell)
    if cx < 0 or cz < 0 or cx >= nx or cz >= nz:
        return ()
    c = cz * nx + cx
    return items[start[c]:start[c + 1]]

def top_at(x, z, ymax, skip_overhang):
    best, bid = -math.inf, -1
    for i in cell_items(x, z):
        if skip_overhang and flags[i] & 1 or flags[i] & 8:
            continue
        y = top(i, x, z)
        if y > best and y <= ymax:
            best, bid = y, i
    return best, bid

def inside(x, y, z):
    for i in cell_items(x, z):
        j = i * 6
        if y < bb[j + 1] or y > bb[j + 4]:
            continue
        t = top(i, x, z)
        if t < y:
            continue
        if typ[i] == 2 and par[j + 3] > 0 and y < t - par[j + 3]:
            continue
        if typ[i] == 3:
            fx, fz, fc, _, lo = fields[int(par[j])]
            ix = min(fx - 1, math.floor((x - bb[j]) / fc))
            iz = min(fz - 1, math.floor((z - bb[j + 2]) / fc))
            if y < par[j + 1] + lo[iz * fx + ix]:
                continue
        if typ[i] == 1:
            r = par[j + 2] + (par[j + 3] - par[j + 2]) * (y - bb[j + 1]) / (bb[j + 4] - bb[j + 1])
            if math.hypot(x - par[j], z - par[j + 1]) > r:
                continue
        return True
    return False

for q in oracle:
    x, y, z = q["x"], q["y"], q["z"]
    for name, ymax, skip in (("all", y + 0.5, False), ("grounded", math.inf, True)):
        actual, aid = top_at(x, z, ymax, skip)
        expected = q[name]["y"] if q[name]["y"] is not None else -math.inf
        assert aid == q[name]["id"] and (actual == expected or abs(actual - expected) <= 1e-4), (q, name, actual, aid)
    assert inside(x, y, z) == q["inside"], q

print(f"verified {len(oracle)} JS queries, {n} solids, {nf} fields, {nzips} zip points and {nboxes} building boxes")
