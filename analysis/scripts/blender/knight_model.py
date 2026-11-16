"""Build an original low-poly armoured knight in Blender, headless, for the Order of the Flame converter.

The 1999 PC models `crimsonknight` and `ebonknight(h)_cap` (Common/NPC/NPCs) share one mesh and rig (569 vertices, 954
polygons; only the skin differs), so one armour base replaces both at convert time. The converter fits it on the PC
skeleton and takes UVs, textures (the crimson or ebon 1999 skin) and skin weights from the player's own disc
(editor/packages/formats/src/ootfauthored.ts). This script reads no game data: every part is lofted from rounded
proportions measured on the reference like a modeller would (joint positions, section widths).

    blender -b --factory-startup -P analysis/scripts/blender/knight_model.py -- \
        --which all [--out build/authored/knight-model] [--kit scripts/buildtool/kit/models] [--no-render]

Coordinates: the PC model's units times S (about metres for a 2 m knight), Blender axes (x right, y back, z up; the
knight faces -Y, origin at the pelvis joint). The `landmarks` are the PC joints scaled by S, so the converter's joint fit
is a uniform scale. Outputs: <kit>/crimson_knight.json, <kit>/ebon_knight.json; clay renders under --out (gitignored).

Parts: a ridged cuirass with abdomen lames, a belt with buckle, two tiers of faulds, a front and back tabard, a hood
helm with a jutting chin guard, eye recess and crest, layered spiked pauldrons, mail sleeves, vambraces with
blades and elbow cops, gauntlets, cuisses, knee cops with a spike and side fan, greaves with a shin ridge and pointed
sabatons.
"""

import argparse
import itertools
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--which", choices=["crimson", "ebon", "all"], default="all")
    ap.add_argument("--out", default="build/authored/knight-model")
    ap.add_argument("--kit", default="scripts/buildtool/kit/models")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=900)
    return ap.parse_args(argv)


ARGS = parse_args()
ROOT = Path(__file__).resolve().parents[3]
OUT = (ROOT / ARGS.out).resolve() if not Path(ARGS.out).is_absolute() else Path(ARGS.out)
KIT = (ROOT / ARGS.kit).resolve() if not Path(ARGS.kit).is_absolute() else Path(ARGS.kit)
OUT.mkdir(parents=True, exist_ok=True)

V = Vector
S = 4.0  # PC units -> build units
TAU = 2 * math.pi
UP = V((0, 0, 1))

# ---------------------------------------------------------------------------- reference proportions
# Joint positions measured on the reference rig (rounded, PC units, Blender axes: x, toward the back, up). Right side
# (+x); the left mirrors it. They double as the converter landmarks (times S).
JOINTS = {
    "upperback": (0.0, 0.0038, 0.0517),
    "jnt10_1": (0.0, 0.0021, 0.1662),
    "RArm": (0.0675, 0.0003, 0.1505),
    "RForearm": (0.1247, 0.0068, 0.1466),
    "RHand": (0.2104, 0.0008, 0.1408),
    "RThigh": (0.0275, 0.0006, -0.0025),
    "RCalf": (0.0275, -0.0086, -0.1102),
    "RFoot": (0.0255, 0.0063, -0.2431),
}
FLOOR = -0.2761 * S


def joint(name, sx=1):
    p = JOINTS[name]
    return V((p[0] * sx * S, p[1] * S, p[2] * S))


def landmarks():
    out = {}
    for n in JOINTS:
        out[n] = [round(c, 4) for c in joint(n)]
        if n.startswith("R"):
            out["L" + n[1:]] = [round(c, 4) for c in joint(n, -1)]
    return out


# ---------------------------------------------------------------------------- mesh builder

SLOTS = ["armour", "helm", "gauntlet", "tabard", "pauldron"]
# PC texture list entries per slot: 0 body sheet, 1 plate sheet, 2 cloth fringe (cut-out) and hands
PC_TEXTURES = {"tabard": [2], "pauldron": [0, 1], "gauntlet": [0, 1, 2]}
PARTS = []  # (name, bmesh, slot)


def new_bm():
    return bmesh.new()


def add_part(name, bm, slot, inside=None, closed=False, sx=1):
    """Store a part. Closed parts get outward normals by recalculation, open shells by the `inside` reference
    (a point or a function of the face centre). sx = -1 mirrors the part to the left side."""
    if sx < 0:
        for v in bm.verts:
            v.co.x = -v.co.x
    bm.normal_update()
    if closed:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    elif inside is not None:
        for f in bm.faces:
            c = f.calc_center_median()
            if callable(inside):
                ref = inside(V((c.x * sx, c.y, c.z)))
                ref = V((ref.x * sx, ref.y, ref.z))
            else:
                ref = V((inside[0] * sx, inside[1], inside[2]))
            if f.normal.dot(c - ref) < 0:
                f.normal_flip()
    PARTS.append((name, bm, slot))


def grid_faces(bm, rows, closed_ring=True):
    """Quads between consecutive rows of BMVerts."""
    for a, b in itertools.pairwise(rows):
        n = len(a)
        m = n if closed_ring else n - 1
        for k in range(m):
            k1 = (k + 1) % n
            bm.faces.new((a[k], a[k1], b[k1], b[k]))


def cap(bm, row, at=None):
    """Close a ring with a fan around its centre (or `at`)."""
    c = at if at is not None else sum((v.co for v in row), V()) / len(row)
    cv = bm.verts.new(c)
    n = len(row)
    for k in range(n):
        bm.faces.new((row[k], row[(k + 1) % n], cv))


def se(a, p):
    """Superellipse cos/sin pair for angle a and exponent p (2 = ellipse, larger = boxier)."""
    c, s = math.cos(a), math.sin(a)
    return math.copysign(abs(c) ** (2 / p), c), math.copysign(abs(s) ** (2 / p), s)


def section(z, hx, yf, yb, n, p=2.4, keel=0.0, kw=0.035, xc=0.0, mod=None, a0=None, a1=None):
    """Horizontal section at height z: half width hx, front yf (negative), back yb; `keel` pushes the front middle
    forward (a ridge) over width kw. Angle 0 is +x, -pi/2 the front. Optional arc a0..a1 (open) and `mod(x, y, z)`."""
    yc = (yf + yb) / 2
    ry = (yb - yf) / 2
    pts = []
    for k in range(n):
        if a0 is None:
            a = -math.pi / 2 + TAU * k / n
        else:
            a = a0 + (a1 - a0) * k / (n - 1)
        cx, sy = se(a, p)
        x = hx * cx
        y = yc + ry * sy
        if sy < 0 and keel:
            y -= keel * math.exp(-((x / kw) ** 2)) * (-sy)
        if mod:
            x, y, z2 = mod(x, y, z)
        else:
            z2 = z
        pts.append(V((x + xc, y, z2)))
    return pts


def loft(rings, bottom=False, top=False, apex=None, open_ring=False):
    bm = new_bm()
    rows = [[bm.verts.new(p) for p in r] for r in rings]
    grid_faces(bm, rows, not open_ring)
    if bottom:
        cap(bm, rows[0])
    if top or apex is not None:
        cap(bm, rows[-1], apex)
    return bm


def frame(t, hint):
    t = t.normalized()
    u = (hint - t * hint.dot(t)).normalized()
    w = t.cross(u)
    return u, w


def tube(path, radii, n, hint=UP, caps=(True, True), p=2.0, phase=0.0, mod=None):
    """Loft rings along a path; radii[i] = (r along hint, r across). `mod(i, k, a, offset)` may change offsets."""
    rings = []
    for i, c in enumerate(path):
        t = path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]
        u, w = frame(t, hint)
        ru, rw = radii[i]
        ring = []
        for k in range(n):
            a = phase + TAU * k / n
            cu, sw = se(a, p)
            off = u * (ru * cu) + w * (rw * sw)
            if mod:
                off = mod(i, k, a, off, u, w)
            ring.append(c + off)
        rings.append(ring)
    return loft(rings, bottom=caps[0], top=caps[1])


def patch(c, a3, a1, a2, th, ph, nu, nv):
    """Ellipsoid patch: point = c + a3 cos(th) cos(ph) + a1 sin(th) cos(ph) + a2 sin(ph), th and ph in the ranges given."""
    bm = new_bm()
    rows = []
    for i in range(nu + 1):
        t = th[0] + (th[1] - th[0]) * i / nu
        row = []
        for j in range(nv + 1):
            f = ph[0] + (ph[1] - ph[0]) * j / nv
            row.append(
                bm.verts.new(c + a3 * (math.cos(t) * math.cos(f)) + a1 * (math.sin(t) * math.cos(f)) + a2 * math.sin(f))
            )
        rows.append(row)
    grid_faces(bm, rows, closed_ring=False)
    return bm


def spike(base, tip, r, sides=4, flat=1.0, hint=UP, twist=0.0):
    """A closed cone (base ring capped); `flat` squashes it across the hint (blades)."""
    u, w = frame(tip - base, hint)
    bm = new_bm()
    ring = []
    for k in range(sides):
        a = twist + TAU * k / sides
        ring.append(bm.verts.new(base + u * (r * math.cos(a)) + w * (r * flat * math.sin(a))))
    tv = bm.verts.new(tip)
    for k in range(sides):
        bm.faces.new((ring[k], ring[(k + 1) % sides], tv))
    bm.faces.new(ring[::-1])
    return bm


def box(c, size):
    bm = new_bm()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = c + V((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    return bm


def lerp(a, b, t):
    return a + (b - a) * t


def piecewise(pts, x):
    """Linear interpolation through sorted (x, value) pairs (values may be Vectors)."""
    if x <= pts[0][0]:
        return pts[0][1]
    for (x0, v0), (x1, v1) in itertools.pairwise(pts):
        if x <= x1:
            return lerp(v0, v1, (x - x0) / (x1 - x0))
    return pts[-1][1]


# ---------------------------------------------------------------------------- torso


def build_torso():
    n = 16
    # (z, half width, front, back, keel): a ridged breastplate over abdomen lames that step out going up
    secs = [
        (0.10, 0.132, -0.108, 0.086, 0.0),
        (0.17, 0.136, -0.114, 0.090, 0.006),
        (0.235, 0.142, -0.118, 0.088, 0.010),
        (0.238, 0.152, -0.128, 0.095, 0.010),
        (0.305, 0.156, -0.131, 0.094, 0.012),
        (0.308, 0.165, -0.140, 0.100, 0.014),
        (0.39, 0.170, -0.152, 0.106, 0.020),
        (0.47, 0.176, -0.160, 0.122, 0.024),
        (0.55, 0.174, -0.163, 0.132, 0.022),
        (0.615, 0.163, -0.152, 0.132, 0.014),
        (0.665, 0.140, -0.120, 0.124, 0.004),
        (0.70, 0.105, -0.090, 0.100, 0.0),
    ]
    rings = [section(z, hx, yf, yb, n, p=2.5, keel=k) for z, hx, yf, yb, k in secs]
    add_part("cuirass", loft(rings, bottom=True, top=True), "armour", closed=True)
    # hips under the faulds (seen between the legs)
    hips = [
        (0.13, 0.128, -0.100, 0.084),
        (0.0, 0.160, -0.112, 0.114),
        (-0.10, 0.168, -0.098, 0.110),
        (-0.165, 0.110, -0.060, 0.070),
    ]
    rings = [section(z, hx, yf, yb, 12, p=2.2) for z, hx, yf, yb in hips]
    add_part("hips", loft(rings, bottom=True, top=True), "armour", closed=True)
    # belt with a square buckle
    belt = [(0.112, 0.146, -0.120, 0.098), (0.122, 0.154, -0.128, 0.105), (0.168, 0.154, -0.128, 0.105),
            (0.178, 0.144, -0.118, 0.096)]  # fmt: skip
    rings = [section(z, hx, yf, yb, 16, p=2.5, keel=0.006) for z, hx, yf, yb in belt]
    add_part("belt", loft(rings), "armour", inside=lambda c: V((0, -0.01, c.z)))
    add_part("buckle", box(V((0, -0.140, 0.145)), (0.052, 0.018, 0.052)), "armour", closed=True)
    # two tiers of faulds, flaring over the hips
    for top, bot in (
        ((0.130, 0.150, -0.128, 0.104), (0.035, 0.176, -0.144, 0.128)),
        ((0.050, 0.172, -0.140, 0.124), (-0.070, 0.198, -0.152, 0.142)),
    ):
        rings = [section(z, hx, yf, yb, 16, p=2.6, keel=0.004) for z, hx, yf, yb in (top, bot)]
        add_part("fauld", loft(rings), "armour", inside=lambda c: V((0, -0.01, c.z + 0.05)))


def build_tabards():
    """Front and back cloth panels hanging from the belt, cut to a point."""
    for side, y0, bottom in ((-1, 0.150, -0.40), (1, 0.150, -0.46)):
        bm = new_bm()
        rows = []
        zs = [0.12, 0.04, -0.06, -0.16, -0.26, bottom]
        for i, z in enumerate(zs):
            hx = lerp(0.098, 0.084, i / (len(zs) - 1))
            row = []
            for j in range(5):
                u = (j - 2) / 2
                x = hx * u
                y = side * (y0 + 0.012 * min(1.0, max(0.0, (0.12 - z) / 0.2)) - 0.30 * x * x)
                zz = z - (0.07 if i == len(zs) - 1 and j == 2 else 0.0) + (0.02 * abs(u) if i == len(zs) - 1 else 0.0)
                row.append(bm.verts.new(V((x, y, zz))))
            rows.append(row)
        grid_faces(bm, rows, closed_ring=False)
        add_part("tabard", bm, "tabard", inside=lambda c: V((0, 0, c.z)))


# ---------------------------------------------------------------------------- head


def build_helm():
    n = 18

    def eye_mod(z):
        def m(x, y, zz):
            # eye recess either side of the nose ridge, under the brow
            if y < -0.08 and 0.018 < abs(x) < 0.075:
                y += 0.016 * math.sin(math.pi * (abs(x) - 0.018) / 0.057)
            return x, y, zz

        return m

    # (z, half width, front, back, keel, mod): a cowl on the shoulders, a chin guard jutting forward, a tall pointed hood
    secs = [
        (0.585, 0.135, -0.090, 0.118, 0.0, None),
        (0.615, 0.152, -0.112, 0.130, 0.060, None),
        (0.650, 0.154, -0.122, 0.132, 0.048, None),
        (0.700, 0.132, -0.128, 0.131, 0.026, None),
        (0.748, 0.108, -0.138, 0.129, 0.016, None),
        (0.790, 0.094, -0.138, 0.121, 0.010, eye_mod(0.790)),
        (0.835, 0.082, -0.126, 0.102, 0.006, None),
        (0.870, 0.068, -0.112, 0.077, 0.0, None),
        (0.893, 0.046, -0.084, 0.046, 0.0, None),
    ]
    rings = [section(z, hx, yf, yb, n, p=2.2, keel=k, kw=0.03, mod=m) for z, hx, yf, yb, k, m in secs]
    add_part(
        "helm", loft(rings, apex=V((0, -0.030, 0.912))), "helm", inside=lambda c: V((0, 0.0, min(max(c.z, 0.62), 0.86)))
    )
    # crest: a low fin from the forehead over the crown to the nape
    pts = []
    for k in range(7):
        a = -1.25 + 2.6 * k / 6
        pts.append(V((0, -0.03 + 0.105 * math.sin(a), 0.80 + 0.105 * math.cos(a))))
    bm = new_bm()
    base = []
    for p in pts:
        d = (p - V((0, -0.03, 0.80))).normalized()
        base.append((p - d * 0.01, p + d * (0.035 if abs(p.y + 0.03) < 0.09 else 0.02)))
    rows = [
        [bm.verts.new(b[0] + V((0.012, 0, 0))), bm.verts.new(b[1]), bm.verts.new(b[0] - V((0.012, 0, 0)))] for b in base
    ]
    for a, b in itertools.pairwise(rows):
        bm.faces.new((a[0], b[0], b[1], a[1]))
        bm.faces.new((a[1], b[1], b[2], a[2]))
    add_part("crest", bm, "helm", inside=lambda c: V((0, -0.03, 0.80)))


# ---------------------------------------------------------------------------- arms


def arm_c(x):
    """Centre of the right arm at a distance x out from the middle (shoulder, elbow, wrist, grip joints)."""
    return piecewise(
        [
            (0.12, V((0.12, 0.004, 0.625))),
            (joint("RArm").x, joint("RArm")),
            (joint("RForearm").x, joint("RForearm")),
            (joint("RHand").x, joint("RHand")),
            (0.91, V((0.91, 0.003, 0.558))),
        ],
        x,
    )


def build_arm(sx):
    # mail sleeve, shoulder to elbow
    xs = [0.13, 0.25, 0.38, 0.50]
    add_part(
        "sleeve",
        tube([arm_c(x) for x in xs], [(0.060, 0.066), (0.058, 0.064), (0.056, 0.060), (0.054, 0.058)], 8),
        "armour",
        closed=True,
        sx=sx,
    )
    # vambrace, flared at the wrist, a ridge along the top
    xs = [0.455, 0.53, 0.63, 0.74, 0.815, 0.845]
    rr = [(0.068, 0.072), (0.072, 0.076), (0.066, 0.070), (0.058, 0.062), (0.054, 0.058), (0.066, 0.070)]

    def ridge(i, k, a, off, u, w):
        return off + u * (0.010 * max(0.0, math.cos(a)) ** 8)

    add_part("vambrace", tube([arm_c(x) for x in xs], rr, 10, mod=ridge), "armour", closed=True, sx=sx)
    # elbow cop on the back of the elbow (arms out, palms down: the elbow points back, +y)
    e = arm_c(0.50)
    add_part(
        "elbow",
        patch(e, V((0, 0.088, 0)), V((0.066, 0, 0)), V((0, 0, 0.068)), (-1.2, 1.2), (-1.15, 1.15), 4, 4),
        "armour",
        inside=e,
        sx=sx,
    )
    add_part(
        "elbowspike", spike(e + V((0, 0.080, 0)), e + V((-0.01, 0.15, 0.0)), 0.02, 4), "armour", closed=True, sx=sx
    )
    # blades along the vambrace, front and back, swept towards the hand (the 1999 knight's silhouette)
    for bx, tx, ty, r in ((0.56, 0.68, 0.275, 0.042), (0.80, 0.90, 0.205, 0.032)):
        for side in (-1, 1):
            b = arm_c(bx) + V((0, side * 0.055, 0))
            t = V((tx, side * ty, arm_c(tx).z - 0.01))
            add_part("blade", spike(b, t, r, 4, flat=0.25), "armour", closed=True, sx=sx)
    # gauntlet: a mailed fist, knuckle plates, thumb
    xs = [0.84, 0.868, 0.905, 0.94, 0.968, 0.986]
    rr = [(0.046, 0.050), (0.052, 0.058), (0.056, 0.060), (0.058, 0.060), (0.050, 0.054), (0.030, 0.034)]
    path = [arm_c(x) + V((0, -0.026, -0.006)) for x in xs]

    def knuckle(i, k, a, off, u, w):
        return off * (1.0 + (0.10 if i == 3 and math.cos(a) > 0.3 else 0.0))

    add_part("gauntlet", tube(path, rr, 8, mod=knuckle), "gauntlet", closed=True, sx=sx)
    t0 = arm_c(0.88) + V((0, -0.075, -0.012))
    add_part(
        "thumb",
        tube([t0, t0 + V((0.035, -0.012, -0.012)), t0 + V((0.065, -0.004, -0.026))], [(0.018, 0.020)] * 3, 5),
        "gauntlet",
        closed=True,
        sx=sx,
    )


def build_pauldron(sx):
    c = V((0.205, 0.010, 0.598))
    a3, a1, a2 = V((0, 0, 0.128)), V((0.122, 0, 0)), V((0, 0.150, 0))
    add_part("pauldron", patch(c, a3, a1, a2, (-0.55, 1.75), (-1.2, 1.2), 7, 6), "pauldron", inside=c, sx=sx)
    # two lames under the dome, each a little wider and lower
    for k, (lo, hi) in enumerate(((1.45, 1.95), (1.75, 2.15))):
        cc = c + V((0.012 * (k + 1), 0, -0.022 * (k + 1)))
        f = 1.05 + 0.05 * k
        add_part("lame", patch(cc, a3 * f, a1 * f, a2 * f, (lo, hi), (-1.1, 1.1), 1, 6), "pauldron", inside=cc, sx=sx)
    # spikes rising from the dome
    for th, ph, d, r in (
        (0.35, -0.45, V((0.035, -0.030, 0.165)), 0.030),
        (0.75, 0.45, V((0.055, 0.040, 0.125)), 0.026),
    ):
        b = c + (a3 * math.cos(th) + a1 * math.sin(th)) * math.cos(ph) + a2 * math.sin(ph)
        add_part("spike", spike(b - d.normalized() * 0.02, b + d, r, 5), "pauldron", closed=True, sx=sx)


# ---------------------------------------------------------------------------- legs


def build_leg(sx):
    knee, ankle = joint("RCalf"), joint("RFoot")
    # thigh (mail), hip to knee
    path = [V((0.11, 0.004, -0.02)), V((0.11, -0.004, -0.20)), V((0.11, -0.018, -0.36)), V((0.11, -0.026, -0.46))]
    rr = [(0.090, 0.092), (0.088, 0.092), (0.076, 0.080), (0.068, 0.072)]
    add_part("thigh", tube(path, rr, 8, hint=V((0, -1, 0))), "armour", closed=True, sx=sx)
    # cuisse: a plate over the front and outside of the thigh
    rings = []
    for z, r in ((-0.07, 0.100), (-0.17, 0.099), (-0.27, 0.094), (-0.37, 0.086)):
        cy = lerp(0.004, -0.02, (-0.07 - z) / 0.30)
        rings.append(section(z, r, cy - r * 1.05, cy + r, 6, p=2.0, xc=0.11, a0=-math.pi / 2 - 0.95, a1=0.35))
    add_part("cuisse", loft(rings, open_ring=True), "armour", inside=lambda c: V((0.11, -0.01, c.z)), sx=sx)
    # knee cop with a forward spike and a fan on the outside
    k = knee + V((0, -0.005, 0.0))
    add_part(
        "kneecop",
        patch(k, V((0, -0.100, 0)), V((0.088, 0, 0)), V((0, 0, 0.085)), (-1.25, 1.4), (-1.1, 1.15), 4, 4),
        "armour",
        inside=k,
        sx=sx,
    )
    add_part(
        "kneespike",
        spike(k + V((0, -0.088, 0.01)), k + V((0.0, -0.185, 0.085)), 0.026, 4),
        "armour",
        closed=True,
        sx=sx,
    )
    add_part(
        "kneefan",
        patch(
            k + V((0.01, 0.0, 0)), V((0.095, 0, 0)), V((0, -0.075, 0)), V((0, 0, 0.07)), (-0.7, 0.9), (-1.0, 1.0), 2, 3
        ),
        "armour",
        inside=k,
        sx=sx,
    )
    # greave: shin ridge in front, calf behind, narrowing to the ankle, a flare at the bottom
    secs = [
        (-0.40, 0.074, -0.110, 0.050, 0.0),
        (-0.50, 0.082, -0.112, 0.070, 0.010),
        (-0.62, 0.088, -0.105, 0.098, 0.014),
        (-0.75, 0.076, -0.086, 0.088, 0.014),
        (-0.87, 0.058, -0.056, 0.074, 0.010),
        (-0.955, 0.048, -0.034, 0.072, 0.006),
        (-0.99, 0.060, -0.052, 0.086, 0.0),
    ]
    xc = ankle.x
    rings = [
        section(z, hx, yf, yb, 10, p=2.3, keel=kk, kw=0.03, xc=lerp(0.11, xc, (-0.40 - z) / 0.59))
        for z, hx, yf, yb, kk in secs
    ]
    add_part("greave", loft(rings, bottom=True, top=True), "armour", closed=True, sx=sx)
    # sabaton: pointed, lamed toe cap, from heel to toe
    secs = [
        (0.090, 0.048, -0.99),
        (0.040, 0.060, -0.975),
        (-0.030, 0.066, -1.000),
        (-0.034, 0.068, -1.012),
        (-0.100, 0.060, -1.035),
        (-0.104, 0.060, -1.045),
        (-0.165, 0.040, -1.068),
        (-0.215, 0.012, -1.090),
    ]
    rings = []
    for y, hx, zt in secs:
        zb = FLOOR
        zc, rz = (zt + zb) / 2, (zt - zb) / 2
        ring = []
        for kk in range(8):
            a = TAU * kk / 8 + math.pi / 8
            cx, sz = se(a, 3.0)
            ring.append(V((xc + 0.004 + hx * cx, y, zc + rz * sz)))
        rings.append(ring)
    add_part("sabaton", loft(rings, bottom=True, top=True), "armour", closed=True, sx=sx)


# ---------------------------------------------------------------------------- assemble, export, render


def build():
    build_torso()
    build_tabards()
    build_helm()
    for sx in (1, -1):
        build_arm(sx)
        build_pauldron(sx)
        build_leg(sx)
    mats = []
    for name in SLOTS:
        m = bpy.data.materials.new(name)
        m.diffuse_color = {
            "armour": (0.45, 0.42, 0.42, 1),
            "helm": (0.5, 0.4, 0.38, 1),
            "gauntlet": (0.4, 0.4, 0.45, 1),
            "tabard": (0.45, 0.1, 0.1, 1),
            "pauldron": (0.42, 0.42, 0.48, 1),
        }[name]
        mats.append(m)
    whole = bmesh.new()
    for _name, bm, slot in PARTS:
        me = bpy.data.meshes.new("tmp")
        bm.to_mesh(me)
        for p in me.polygons:
            p.material_index = SLOTS.index(slot)
        whole.from_mesh(me)
        bpy.data.meshes.remove(me)
        bm.free()
    me = bpy.data.meshes.new("Knight")
    whole.to_mesh(me)
    whole.free()
    for m in mats:
        me.materials.append(m)
    obj = bpy.data.objects.new("Knight", me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def report(obj):
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    print(f"knight: {len(obj.data.vertices)} vertices, {tris} triangles")


def render(obj):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_cavity = True
    sc.render.resolution_x = ARGS.res
    sc.render.resolution_y = ARGS.res
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    tgt = V((0, 0, -0.09))
    for view, az, scale, tz in (
        ("front", 0, 2.2, -0.09),
        ("threequarter", -35, 2.2, -0.09),
        ("side", -90, 2.2, -0.09),
        ("back", 180, 2.2, -0.09),
        ("head", -30, 0.75, 0.66),
    ):
        a = math.radians(az)
        tgt = V((0, 0, tz)) if view != "head" else V((0.12, 0, tz))
        cam.data.ortho_scale = scale
        cam.location = tgt + V((-6 * math.sin(a), -6 * math.cos(a), 0.6))
        cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = str(OUT / f"clay_{view}.png")
        bpy.ops.render.render(write_still=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    obj = build()
    report(obj)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored

    targets = {
        "crimson": ("crimsonknight", "Crimson Knight", "crimson_knight.json"),
        "ebon": ("ebonknight(h)_cap", "Ebon Knight", "ebon_knight.json"),
    }
    for key in ("crimson", "ebon") if ARGS.which == "all" else (ARGS.which,):
        model, label, fname = targets[key]
        export_authored(
            obj,
            KIT / fname,
            model=model,
            label=label,
            fit="joints",
            extra={"landmarks": landmarks(), "radialJoints": []},
            slots={k: {"pcTextures": v} for k, v in PC_TEXTURES.items()},
        )
    if not ARGS.no_render:
        render(obj)


main()
