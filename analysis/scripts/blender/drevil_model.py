"""Build a remake of Dr. Evil (the Order of the Flame final boss, a giant horned serpent) in Blender, headless.

The 1999 PC model `drevil(h)` (Common/NPC/NPCs, 539 polygons) is replaced at convert time by this mesh: the converter
fits it on the PC skeleton (a nine-joint spine, the head, a two-joint jaw and a three-joint tongue), takes the skin
weights from the player's own disc and projects the player's own 1999 textures onto it
(editor/packages/formats/src/ootfauthored.ts). The remake follows the 1999 shape so the painted 1999 skin, eyes and
gullet land where they belong: a sharp wedge of a head (a gabled forehead plate rising from the snout to the brow, the
pale eyes on the plate edge near the snout, a lean keeled jaw whose rim rises beside the upper lip), a nose horn, a
dorsal crest spike, back-swept skull spikes and three-sided cheek spikes, chin and under-jaw spikes, long thin teeth, a forked
tongue, and the seven-sided body tube with its long ridges. Only the geometry is refined: more sections, slightly
convex panels between sharp creases, sharper spikes and modelled teeth.

This script reads no game data: the sections below are rounded measurements taken off the reference (heights and
widths of the creases at a few stations along the body), like a modeller working from a picture.

    blender -b --factory-startup -P analysis/scripts/blender/drevil_model.py -- \
        --out build/authored/drevil-model [--authored scripts/buildtool/kit/models-draft/drevil.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the side, y toward the tail, z up; the serpent faces -Y and
is about 14 units long), so the `landmarks` are the PC joint positions and the converter's joint fit is the identity.
Slots (each projects from the PC texture entries it names): Head and Body (0, 1: the painted skin), Mouth (1: the red
gullet), Horn and Teeth (0: the head sheet with its pale horn paint; the 1999 fang cards, entry 2, are mostly
transparent and would cut holes in modelled teeth). `radialJoints` is empty: every triangle takes the nearest 1999
triangle, because rays from the head joint graze the long snout and smear the eyes. Outputs under --out (gitignored):
drevil.blend and clay renders render_<view>.png.
"""

from __future__ import annotations

import argparse
import itertools
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

V = Vector


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/authored/drevil-model")
    ap.add_argument("--authored", help="write the mesh as an authored model for the converter (.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=900, help="render height in pixels")
    return ap.parse_args(argv)


ARGS = parse_args()
OUT = Path(ARGS.out).resolve()
OUT.mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------------------- reference proportions
# Joint positions measured on the reference rig (rounded), Blender axes. They double as the converter landmarks.
JOINTS = {
    "jnt5_1": (0.0, -0.047, 0.040),
    "jnt5_2": (0.0, -2.133, 0.085),
    "jnt5_3": (0.0, -3.970, 0.142),
    "jnt5_4": (0.0, -5.444, 0.187),
    "jnt5_5": (0.0, -6.839, 0.221),
    "jnt5_6": (0.0, -8.132, 0.255),
    "jnt5_7": (0.0, -9.402, 0.289),
    "jnt5_8": (0.0, -10.445, 0.323),
    "jnt5_9": (0.0, -11.092, 0.368),
    "head": (0.0, -11.817, 0.675),
    "jaw02": (0.0, -12.282, -0.017),
}

# Sections are the right half, from the top centre line around to the bottom centre line, as crease points (x, z). The
# panels between creases bulge out a little (BULGE, a fraction of the panel width).

# Body: (y, top centre z, top corner, flank, lower flank, keel z). The 1999 body is a seven-sided tube: a flat back
# narrowing toward the neck, a flank crease, a lower crease and a keel, tallest at the buried end.
BODY = [
    (0.02, 0.99, (0.37, 0.98), (0.56, 0.03), (0.51, -0.37), -0.64),
    (-1.5, 0.97, (0.39, 0.96), (0.53, 0.05), (0.49, -0.33), -0.56),
    (-3.5, 0.93, (0.36, 0.92), (0.50, 0.09), (0.46, -0.26), -0.46),
    (-5.5, 0.87, (0.31, 0.86), (0.48, 0.16), (0.43, -0.18), -0.37),
    (-7.5, 0.81, (0.24, 0.80), (0.44, 0.21), (0.40, -0.10), -0.26),
    (-8.5, 0.78, (0.17, 0.77), (0.40, 0.25), (0.37, -0.06), -0.20),
    (-9.4, 0.74, (0.10, 0.73), (0.36, 0.28), (0.33, -0.01), -0.14),
    (-10.3, 0.72, (0.08, 0.70), (0.34, 0.31), (0.30, 0.06), -0.07),
    (-11.0, 0.76, (0.09, 0.73), (0.35, 0.33), (0.30, 0.11), -0.05),
    (-11.4, 0.74, (0.10, 0.70), (0.30, 0.38), (0.26, 0.10), 0.00),
]
BODY_SEGS = [2, 3, 2, 2]
BODY_BULGE = [0.05, 0.07, 0.06, 0.06]

# Upper head: (y, ridge z, plate edge, cheek, lip, palate side, palate centre z). The forehead is a gable: a sharp
# centre ridge rising from the snout to the brow, sloping plates down to the plate edge (where the eyes sit near the
# snout), a near-vertical side wall to the lip, and the palate inside the mouth.
SKULL = [
    (-13.81, 0.47, (0.09, 0.45), (0.12, 0.41), (0.12, 0.38), (0.07, 0.38), 0.38),
    (-13.73, 0.51, (0.13, 0.46), (0.16, 0.41), (0.16, 0.37), (0.10, 0.38), 0.39),
    (-13.57, 0.61, (0.18, 0.54), (0.22, 0.45), (0.23, 0.37), (0.13, 0.42), 0.43),
    (-13.39, 0.73, (0.20, 0.59), (0.25, 0.49), (0.27, 0.39), (0.15, 0.44), 0.46),
    (-13.16, 0.85, (0.22, 0.65), (0.27, 0.54), (0.29, 0.43), (0.17, 0.47), 0.50),
    (-12.99, 0.89, (0.24, 0.70), (0.27, 0.58), (0.29, 0.47), (0.18, 0.49), 0.51),
    (-12.74, 0.95, (0.24, 0.75), (0.27, 0.58), (0.29, 0.44), (0.19, 0.47), 0.49),
    (-12.40, 0.97, (0.26, 0.74), (0.34, 0.55), (0.36, 0.36), (0.20, 0.40), 0.40),
    (-12.10, 0.99, (0.29, 0.74), (0.36, 0.56), (0.39, 0.31), (0.22, 0.34), 0.33),
    (-11.80, 1.01, (0.31, 0.76), (0.37, 0.58), (0.39, 0.29), (0.22, 0.31), 0.29),
    (-11.55, 0.88, (0.20, 0.78), (0.34, 0.55), (0.37, 0.29), (0.22, 0.32), 0.30),
    (-11.30, 0.82, (0.13, 0.78), (0.32, 0.48), (0.31, 0.30), (0.18, 0.34), 0.34),
    (-11.05, 0.70, (0.10, 0.64), (0.22, 0.46), (0.20, 0.36), (0.12, 0.38), 0.40),
]
SKULL_SEGS = [[0.1, 0.55, 0.92, 1.0], [0.12, 1.0], [0.88, 1.0], [0.2, 1.0], 1]
SKULL_BULGE = [0.03, 0.06, 0.05, 0.05, 0.08]
SKULL_MOUTH = {3, 4}  # the panels from the lip inward are the mouth

# Lower jaw: (y, floor centre z, gum, rim, low flank, keel side, keel z). A lean jaw with a V keel, deepest under the
# eyes; its rim rises outside the upper lip at the corner of the mouth.
JAW = [
    (-13.86, 0.10, (0.07, 0.13), (0.11, 0.15), (0.10, 0.03), (0.05, -0.01), -0.02),
    (-13.76, 0.06, (0.14, 0.12), (0.19, 0.15), (0.17, 0.01), (0.08, -0.04), -0.07),
    (-13.55, 0.03, (0.19, 0.12), (0.23, 0.14), (0.21, 0.00), (0.10, -0.05), -0.10),
    (-13.30, 0.04, (0.22, 0.12), (0.26, 0.14), (0.24, 0.01), (0.10, -0.06), -0.10),
    (-13.00, 0.08, (0.25, 0.16), (0.30, 0.19), (0.28, 0.03), (0.11, -0.06), -0.19),
    (-12.75, 0.15, (0.28, 0.24), (0.37, 0.37), (0.32, 0.05), (0.11, -0.06), -0.26),
    (-12.45, 0.17, (0.29, 0.27), (0.35, 0.34), (0.30, 0.02), (0.12, -0.08), -0.26),
    (-12.10, 0.18, (0.30, 0.27), (0.34, 0.30), (0.27, 0.00), (0.14, -0.12), -0.25),
    (-11.80, 0.22, (0.30, 0.27), (0.34, 0.28), (0.28, 0.07), (0.17, -0.12), -0.18),
    (-11.50, 0.24, (0.28, 0.27), (0.33, 0.28), (0.31, 0.03), (0.20, -0.05), -0.07),
    (-11.25, 0.22, (0.22, 0.25), (0.27, 0.26), (0.27, 0.04), (0.16, -0.04), -0.04),
]
JAW_SEGS = [1, [0.25, 1.0], [0.1, 0.55, 1.0], [0.85, 1.0], 1]
JAW_BULGE = [-0.08, 0.03, 0.08, 0.06, 0.03]
JAW_MOUTH = {0, 1}

# The cheek spike, a three-sided ridge on the side wall of the skull: (y, wall top, outer ridge, wall bottom).
CHEEK = [
    (-12.45, (0.31, 0.50), (0.33, 0.46), (0.31, 0.41)),
    (-12.10, (0.32, 0.55), (0.42, 0.46), (0.32, 0.37)),
    (-11.80, (0.33, 0.58), (0.50, 0.46), (0.33, 0.35)),
    (-11.62, (0.33, 0.57), (0.53, 0.45), (0.33, 0.36)),
]

# The pale eyes are not modelled: the 1999 paint puts them on the plate edge near the snout, so the plate edge crease
# runs through the 1999 crease points there (stations -13.57, -13.39, -13.16, -12.99) and the ovals project whole.
# Sunken sockets were tried and broke the ovals (the projection lands on the 1999 planes either side of the crease).

# Row spacing along the body (units) per part.
STEP = {"body": 0.6, "skull": 0.13, "jaw": 0.15}

# ---------------------------------------------------------------------------- small math


def catmull(ctrl, per):
    """Catmull-Rom through tuples of floats; per[i] samples on segment i (an int for all), both ends included."""
    n = len(ctrl)
    ext = [tuple(2 * a - b for a, b in zip(ctrl[0], ctrl[1])), *ctrl]
    ext.append(tuple(2 * a - b for a, b in zip(ctrl[-1], ctrl[-2])))
    out = []
    for i in range(n - 1):
        p0, p1, p2, p3 = ext[i], ext[i + 1], ext[i + 2], ext[i + 3]
        k = per[i] if isinstance(per, list) else per
        for s in range(k):
            t = s / k
            t2, t3 = t * t, t * t * t
            out.append(
                tuple(
                    0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
                    for a, b, c, d in zip(p0, p1, p2, p3)
                )
            )
    out.append(tuple(ctrl[-1]))
    return out


def flatten_row(row):
    """A table row (y, numbers or (x, z) pairs) as one flat tuple of floats."""
    out = []
    for v in row:
        out += list(v) if isinstance(v, tuple) else [v]
    return tuple(out)


def table_at(table, y):
    """Linear lookup of a flattened table row at y (rows run from the snout toward the tail)."""
    rows = [flatten_row(r) for r in table]
    if y <= rows[0][0]:
        return rows[0]
    for a, b in itertools.pairwise(rows):
        if a[0] <= y <= b[0] or b[0] <= y <= a[0]:
            t = (y - a[0]) / (b[0] - a[0])
            return tuple(p + (q - p) * t for p, q in zip(a, b))
    return rows[-1]


def section(points, segs, bulge):
    """Dense half section through crease points (x, z): each panel is a quadratic curve bowed outward by bulge times
    its width; the creases stay sharp. segs[i] is a count of even steps or a list of curve parameters (support points
    close to a crease keep it crisp under smooth shading). Returns the points and the panel index of each span."""
    out = [V((points[0][0], points[0][1]))]
    span = []
    for i, (a, b) in enumerate(itertools.pairwise(points)):
        a, b = V(a), V(b)
        d = b - a
        n = V((-d.y, d.x))
        if n.length > 1e-9:
            n.normalize()
        c = (a + b) / 2 + n * bulge[i] * d.length
        ts = segs[i] if isinstance(segs[i], list) else [k / segs[i] for k in range(1, segs[i] + 1)]
        for t in ts:
            out.append(a * (1 - t) ** 2 + c * 2 * t * (1 - t) + b * t * t)
            span.append(i)
    return out, span


# ---------------------------------------------------------------------------- mesh builder


class Part:
    """Vertices and faces with a slot name per face, merged into one object at the end."""

    def __init__(self):
        self.verts: list[Vector] = []
        self.faces: list[tuple[list[int], str]] = []

    def v(self, p):
        self.verts.append(V(p))
        return len(self.verts) - 1

    def face(self, idx, mat, inside=None):
        """Add a face; when `inside` (a point inside the solid) is given, wind it to face away from it."""
        idx = [i for k, i in enumerate(idx) if i != idx[k - 1]]
        if len(idx) < 3:
            return
        if inside is not None:
            ps = [self.verts[i] for i in idx]
            n = (ps[1] - ps[0]).cross(ps[2] - ps[0])
            if len(ps) == 4:
                n += (ps[2] - ps[0]).cross(ps[3] - ps[0])
            c = sum(ps, V()) / len(ps)
            if n.dot(c - inside) < 0:
                idx.reverse()
        self.faces.append((idx, mat))

    def add(self, other):
        base = len(self.verts)
        self.verts += [p.copy() for p in other.verts]
        self.faces += [([i + base for i in f], m) for f, m in other.faces]

    def mirrored(self):
        m = Part()
        m.verts = [V((-p.x, p.y, p.z)) for p in self.verts]
        m.faces = [(list(reversed(f)), mt) for f, mt in self.faces]
        return m


def rows_along(table, step):
    """The table rows resampled along y by Catmull-Rom, about `step` apart."""
    flat = [flatten_row(r) for r in table]
    per = [max(1, round(abs(b[0] - a[0]) / step)) for a, b in itertools.pairwise(flat)]
    return catmull(flat, per)


def shell(part, table, segs, bulge, step, mat, mouth=(), mouth_mat="Mouth", shape=None, cap_front=True, cap_back=True):
    """A closed tube lofted through the table's sections (rows of y then crease points). `shape(y, half)` may move the
    dense half section of a row. Panels listed in `mouth` take the mouth slot."""
    rings = []
    spans = None
    for row in rows_along(table, step):
        y = row[0]
        nums = row[1:]
        pts = [(0.0, nums[0])]
        k = 1
        while k < len(nums) - 1:
            pts.append((nums[k], nums[k + 1]))
            k += 2
        pts.append((0.0, nums[-1]))
        half, spans = section(pts, segs, bulge)
        if shape:
            half = shape(y, half)
        ring = [V((p.x, y, p.y)) for p in half]
        # the left half mirrored, without the two centre-line points
        ring += [V((-p.x, p.y, p.z)) for p in reversed(ring[1:-1])]
        rings.append(ring)
    n = len(rings[0])
    span_of = spans + list(reversed(spans))  # span of edge k (k = 0..n-1) around the ring
    ids = [[part.v(p) for p in ring] for ring in rings]
    for r in range(len(rings) - 1):
        a, b = rings[r], rings[r + 1]
        inside = (sum(a, V()) + sum(b, V())) / (2 * n)
        for k in range(n):
            k2 = (k + 1) % n
            m = mouth_mat if span_of[k] in mouth else mat
            part.face([ids[r][k], ids[r][k2], ids[r + 1][k2], ids[r + 1][k]], m, inside)
    for end, cap in ((0, cap_front), (len(rings) - 1, cap_back)):
        if not cap:
            continue
        ring = rings[end]
        c = sum(ring, V()) / n
        nb = rings[1 if end == 0 else end - 1]
        inside = c + (sum(nb, V()) / n - c) * 0.5
        ci = part.v(c)
        for k in range(n):
            part.face([ids[end][k], ids[end][(k + 1) % n], ci], mat, inside)
    return part


def cone(part, base, tip, r, mat, sides=6, rings=3, flat=None, flat_ratio=1.0, bend=None, power=1.0, sink=0.0):
    """A spike from base to tip: rings of `sides` points shrinking to the tip. `flat` (a direction) squashes the
    section to `flat_ratio` along it (a blade); `bend` bows the axis (offset at the middle); `sink` pushes the base
    ring back along the axis so it starts inside the surface."""
    base, tip = V(base), V(tip)
    axis = tip - base
    ax = axis.normalized()
    base = base - ax * sink
    fd = V(flat) if flat is not None else (ax.cross(V((0, 0, 1))) if abs(ax.z) < 0.9 else V((1, 0, 0)))
    fd = (fd - ax * fd.dot(ax)).normalized()
    sd = ax.cross(fd).normalized()
    ring_ids = []
    centres = []
    for i in range(rings):
        t = i / rings
        c = base.lerp(tip, t)
        if bend is not None:
            c = c + V(bend) * math.sin(math.pi * t)
        rr = r * (1 - t) ** power
        ids = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            ids.append(part.v(c + fd * math.cos(a) * rr * flat_ratio + sd * math.sin(a) * rr))
        ring_ids.append(ids)
        centres.append(c)
    ti = part.v(tip)
    for i in range(rings - 1):
        inside = (centres[i] + centres[i + 1]) / 2
        for k in range(sides):
            k2 = (k + 1) % sides
            part.face([ring_ids[i][k], ring_ids[i][k2], ring_ids[i + 1][k2], ring_ids[i + 1][k]], mat, inside)
    last = ring_ids[-1]
    inside = centres[-1] + (tip - centres[-1]) * 0.3
    for k in range(sides):
        part.face([last[k], last[(k + 1) % sides], ti], mat, inside)
    return part


def wedge(part, stations, tip, mat, per=2):
    """A three-sided ridge lying on a wall (the cheek spike): stations (y, wall top, ridge, wall bottom) as (x, z),
    lofted with Catmull-Rom between them, closed by a point in front and drawn out to the tip at the back. The upper and
    lower faces bow out a little; the inner face stays inside the head."""
    rows = catmull([flatten_row(r) for r in stations], per)
    rings = []
    for y, tx, tz, rx, rz, bx, bz in rows:
        top, ridge, bot = V((tx, y, tz)), V((rx, y, rz)), V((bx, y, bz))
        up = (top + ridge) / 2 + V((0.08 * (tz - rz), 0, 0.08 * (rx - tx)))
        low = (ridge + bot) / 2 + V((0.08 * (rz - bz), 0, -0.08 * (rx - bx)))
        inner = (top + bot) / 2 - V((0.04, 0, 0))
        rings.append([top, up, ridge, low, bot, inner])
    ids = [[part.v(p) for p in r] for r in rings]
    n = 6
    for r in range(len(rings) - 1):
        inside = (sum(rings[r], V()) + sum(rings[r + 1], V())) / (2 * n) - V((0.02, 0, 0))
        for k in range(n):
            k2 = (k + 1) % n
            part.face([ids[r][k], ids[r][k2], ids[r + 1][k2], ids[r + 1][k]], mat, inside)
    front = part.v(sum(rings[0], V()) / n - V((0, 0.06, 0)))
    back = part.v(V(tip))
    for end, ci, d in ((0, front, 0.05), (len(rings) - 1, back, -0.05)):
        inside = sum(rings[end], V()) / n + V((-0.02, d, 0))
        for k in range(n):
            part.face([ids[end][k], ids[end][(k + 1) % n], ci], mat, inside)
    return part


def blade(part, a, b, tip, side, thick, mat, nu=4, nv=4, bend=None):
    """A flat blade (a fin): its root runs from a to b (sunk in the surface), its edges meet at the tip. Both faces bow
    out along `side` by up to `thick` in the middle of the root, thinning to a sharp edge and point."""
    a, b, tip, side = V(a), V(b), V(tip), V(side).normalized()
    grid = {}
    for s in (1, -1):
        for j in range(nv):
            v = j / nv
            for i in range(nu + 1):
                u = i / nu
                edge = i in (0, nu)
                if edge and (i, j, -s) in grid:
                    grid[(i, j, s)] = grid[(i, j, -s)]
                    continue
                p = a.lerp(b, u).lerp(tip, v)
                if bend is not None:
                    p = p + V(bend) * math.sin(math.pi * v) * (1 - v * 0.3)
                p = p + side * s * thick * math.sin(math.pi * u) * (1 - v) ** 0.9
                grid[(i, j, s)] = part.v(p)
    ti = part.v(tip)
    mid = a.lerp(b, 0.5).lerp(tip, 0.3)
    for s in (1, -1):
        for j in range(nv):
            for i in range(nu):
                if j < nv - 1:
                    q = [grid[(i, j, s)], grid[(i + 1, j, s)], grid[(i + 1, j + 1, s)], grid[(i, j + 1, s)]]
                else:
                    q = [grid[(i, j, s)], grid[(i + 1, j, s)], ti]
                inside = mid - side * s * thick
                part.face(q, mat, inside)
    # the root (inside the head): close it so the blade is a solid
    for i in range(nu):
        q = [grid[(i, 0, 1)], grid[(i + 1, 0, 1)], grid[(i + 1, 0, -1)], grid[(i, 0, -1)]]
        part.face(q, mat, mid + (tip - mid) * 0.2)
    return part


# ---------------------------------------------------------------------------- parts


def build_body():
    part = Part()
    shell(part, BODY, BODY_SEGS, BODY_BULGE, STEP["body"], "Body")
    return part


def build_skull():
    part = Part()
    shell(part, SKULL, SKULL_SEGS, SKULL_BULGE, STEP["skull"], "Head", mouth=SKULL_MOUTH)
    return part


def build_jaw():
    part = Part()
    shell(part, JAW, JAW_SEGS, JAW_BULGE, STEP["jaw"], "Head", mouth=JAW_MOUTH)
    return part


def build_horns():
    """The spikes and fins of the 1999 head: right side features (mirrored later) and the centre ones."""
    side = Part()
    # cheek spike: a three-sided ridge growing out of the side of the skull behind the mouth, its point swept back
    wedge(side, CHEEK, (0.55, -11.36, 0.44), "Horn")
    # back-swept skull spikes over the neck
    cone(side, (0.22, -11.80, 0.68), (0.28, -10.88, 0.84), 0.10, "Horn", sides=6, rings=3, power=0.9)
    # under-jaw spikes
    cone(side, (0.18, -11.68, 0.06), (0.45, -11.22, -0.30), 0.07, "Horn", sides=5, rings=2, power=0.9, sink=0.04)
    # chin spikes
    cone(side, (0.10, -13.60, 0.02), (0.20, -13.55, -0.20), 0.04, "Horn", sides=5, rings=1, sink=0.03)
    # tall spine rising from the jaw rim at the corner of the mouth, and its small neighbours
    cone(
        side,
        (0.33, -12.78, 0.30),
        (0.37, -12.77, 0.74),
        0.035,
        "Horn",
        sides=5,
        rings=2,
        sink=0.02,
        bend=(0.01, 0.02, 0),
    )
    cone(side, (0.33, -12.66, 0.30), (0.40, -12.62, 0.46), 0.025, "Horn", sides=4, rings=1, sink=0.02)
    cone(side, (0.30, -12.56, 0.28), (0.40, -12.56, 0.31), 0.022, "Horn", sides=4, rings=1, sink=0.02)
    whole = Part()
    whole.add(side)
    whole.add(side.mirrored())
    # nose horn on the ridge of the snout
    blade(whole, (0, -13.66, 0.50), (0, -13.40, 0.66), (0, -13.53, 0.90), (1, 0, 0), 0.03, "Horn", nu=4, nv=4)
    # dorsal crest blade rising from the brow to the great back spike
    cone(
        whole,
        (0, -11.95, 0.78),
        (0, -10.95, 1.33),
        0.20,
        "Horn",
        sides=8,
        rings=4,
        flat=(1, 0, 0),
        flat_ratio=0.35,
        sink=0.08,
        bend=(0, 0, 0.04),
    )
    # low spike on the back of the neck
    cone(whole, (0, -11.20, 0.64), (0, -10.48, 0.94), 0.09, "Horn", sides=6, rings=3, flat=(1, 0, 0), flat_ratio=0.5)
    return whole


def lip_teeth(part, table, ys, lip_index, inset, down, lengths, radius, lean):
    """A row of teeth along a crease of a table (the upper lip or the jaw rim), pointing down (-1) or up (+1)."""
    for y, ln in zip(ys, lengths):
        row = table_at(table, y)
        x, z = row[lip_index], row[lip_index + 1]
        base = V((x - inset, y, z - 0.01 * down))
        tip = base + V((lean[0], lean[1], down * ln))
        cone(
            part,
            base,
            tip,
            radius * (0.8 + ln * 2.0),
            "Teeth",
            sides=4,
            rings=2 if ln >= 0.1 else 1,
            power=1.2,
            sink=0.02,
            bend=(0.004, 0.0, 0.0),
        )


def build_teeth():
    side = Part()
    # upper teeth hang from the lip (the SKULL lip point is flattened index 6), long thin fangs at the front
    ys = [-13.72, -13.58, -13.42, -13.24, -13.08, -12.92, -12.74, -12.56]
    ln = [0.16, 0.08, 0.12, 0.07, 0.10, 0.06, 0.09, 0.05]
    lip_teeth(side, SKULL, ys, 6, 0.025, -1, ln, 0.018, (0.0, 0.01))
    # lower teeth stand on the jaw rim (the JAW rim point is flattened index 4)
    ys = [-13.80, -13.64, -13.46, -13.28, -13.10, -12.92]
    ln = [0.15, 0.07, 0.10, 0.07, 0.09, 0.08]
    lip_teeth(side, JAW, ys, 4, 0.03, 1, ln, 0.018, (0.0, -0.01))
    whole = Part()
    whole.add(side)
    whole.add(side.mirrored())
    return whole


def build_tongue():
    """A thin forked tongue lying in the jaw and reaching to the tip of the snout."""
    part = Part()
    path = [(-12.55, 0.20, 0.050), (-12.95, 0.17, 0.050), (-13.30, 0.15, 0.045), (-13.55, 0.18, 0.035)]
    pts = catmull([tuple(p) for p in path], 3)
    rings = []
    for y, z, w in pts:
        rings.append(
            [V((w * math.cos(a), y, z + 0.016 * math.sin(a))) for a in (0, math.pi / 2, math.pi, -math.pi / 2)]
        )
    ids = [[part.v(p) for p in r] for r in rings]
    for r in range(len(rings) - 1):
        inside = (sum(rings[r], V()) + sum(rings[r + 1], V())) / 8
        for k in range(4):
            k2 = (k + 1) % 4
            part.face([ids[r][k], ids[r][k2], ids[r + 1][k2], ids[r + 1][k]], "Mouth", inside)
    c = sum(rings[0], V()) / 4
    ci = part.v(c)
    for k in range(4):
        part.face([ids[0][k], ids[0][(k + 1) % 4], ci], "Mouth", c + V((0, -0.05, 0)))
    for sx in (1, -1):
        cone(part, (0.018 * sx, -13.55, 0.18), (0.05 * sx, -13.78, 0.23), 0.02, "Mouth", sides=4, rings=2, sink=0.02)
    return part


# ---------------------------------------------------------------------------- object

ORDER = ["Body", "Head", "Mouth", "Horn", "Teeth"]
SLOTS = {
    "Body": {"pcTextures": [0, 1]},
    "Head": {"pcTextures": [0, 1]},
    "Mouth": {"pcTextures": [1]},
    "Horn": {"pcTextures": [0]},
    "Teeth": {"pcTextures": [0]},
}


def to_object(part, name):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(p) for p in part.verts], [], [f for f, _ in part.faces])
    me.update()
    mats = {}
    for n in ORDER:
        m = bpy.data.materials.new(n)
        m.diffuse_color = {
            "Body": (0.32, 0.30, 0.16, 1),
            "Head": (0.36, 0.34, 0.18, 1),
            "Mouth": (0.45, 0.10, 0.08, 1),
            "Horn": (0.62, 0.58, 0.44, 1),
            "Teeth": (0.85, 0.82, 0.70, 1),
        }[n]
        me.materials.append(m)
        mats[n] = len(mats)
    for poly, (_, m) in zip(me.polygons, part.faces):
        poly.material_index = mats[m]
        poly.use_smooth = True
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bm.to_mesh(me)
    bm.free()
    return obj


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    whole = Part()
    for name, part in (
        ("body", build_body()),
        ("skull", build_skull()),
        ("jaw", build_jaw()),
        ("horns", build_horns()),
        ("teeth", build_teeth()),
        ("tongue", build_tongue()),
    ):
        tris = sum(len(f) - 2 for f, _ in part.faces)
        print(f"part {name}: {len(part.verts)} vertices, {tris} triangles")
        whole.add(part)
    return to_object(whole, "drevil")


def report(model):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + len(p.vertices) - 2
    print(f"drevil: {len(me.vertices)} vertices, {sum(per.values())} triangles, per slot {per}")


# ---------------------------------------------------------------------------- render


def render():
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = int(ARGS.res * 1.5)
    sc.render.resolution_y = ARGS.res
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.30, 0.32, 0.36)
    for d, e in ((V((-0.5, -1, 0.8)), 4.0), (V((1, -0.2, 0.4)), 2.0), (V((0, 1, 0.6)), 2.0), (V((0, 0, -1)), 0.6)):
        ld = bpy.data.lights.new("l", "SUN")
        ld.energy = e
        lo = bpy.data.objects.new("l", ld)
        sc.collection.objects.link(lo)
        lo.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    views = [
        ("side", V((0, -6.9, 0.35)), V((1, 0, 0.0001)), 15.0),
        ("head", V((0, -12.3, 0.45)), V((0.9, -1, 0.45)), 3.6),
        ("headside", V((0, -12.2, 0.45)), V((1, 0, 0.0001)), 3.6),
        ("headfront", V((0, -12.2, 0.45)), V((0, -1, 0.0001)), 2.4),
    ]
    for name, c, d, scale in views:
        d = d.normalized()
        cam.location = c + d * 30
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
        cam.data.clip_end = 100
        sc.render.filepath = str(OUT / f"render_{name}.png")
        bpy.ops.render.render(write_still=True)


def main():
    model = build()
    report(model)
    if ARGS.authored:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from authored_export import export_authored

        export_authored(
            model,
            Path(ARGS.authored).resolve(),
            model="drevil(h)",
            label="Dr. Evil",
            fit="joints",
            slots=SLOTS,
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}, "radialJoints": []},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "drevil.blend"))
    if not ARGS.no_render:
        render()


main()
