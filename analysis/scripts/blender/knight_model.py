"""Build an original low-poly armoured knight in Blender, headless, for the Order of the Flame converter, and paint it.

The 1999 PC models `crimsonknight` and `ebonknight(h)_cap` (Common/NPC/NPCs) share one mesh and rig (569 vertices, 954
polygons; only the skin differs), so one armour base replaces both at convert time. The converter fits it on the PC
skeleton and takes skin weights from the player's own disc (editor/packages/formats/src/ootfauthored.ts). This script
reads no game data: every part is lofted from rounded proportions measured on the reference like a modeller would
(joint positions, section widths), and each knight gets its own texture, painted procedurally in shader nodes and baked
by own_texture.bake_atlas into one 512 px atlas (original art, committed beside the JSON).

    blender -b --factory-startup -P analysis/scripts/blender/knight_model.py --         --which all --kit scripts/buildtool/kit/models-draft [--out build/authored/knight-model] [--no-render]

Coordinates: the PC model's units times S (about metres for a 2 m knight), Blender axes (x right, y back, z up; the
knight faces -Y, origin at the pelvis joint). The `landmarks` are the PC joints scaled by S, so the converter's joint fit
is a uniform scale. Outputs: <kit>/{crimson,ebon}_knight.{json,png}; texture renders under --out (gitignored).

Parts: a ridged cuirass with abdomen lames, a belt with buckle, two tiers of faulds, a front and back tabard, a hood
helm with a jutting chin guard, eye recess and crest, layered spiked pauldrons, mail sleeves, vambraces with
blades and elbow cops, gauntlets, cuisses, knee cops with a spike and side fan, greaves with a shin ridge and pointed
sabatons.

Paint: every part carries its own coordinates (a `param` UV map around and along it, its size in metres, a part id), so
the materials can place trims, rivets, stitching, hems and emblems on it: plate steel (mottling, brushing, scratches,
brass trims with rivets, enamel bands, a winged flame on the chest, diamonds on the shins), mail, leather with
stitching, a cloth tabard with folds, a hem band and a frayed edge, and the helm's painted mask (enamel stripes, glowing
eye slits, breathing holes). Grime in the recesses and a top light are painted in. The left side shares the right
side's atlas islands.
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
    ap.add_argument("--samples", type=int, default=64, help="bake samples")
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

# One material per surface kind (the exported slot names); every slot is baked into the knight's own atlas.
MATS = ["plate", "spike", "mail", "leather", "brass", "cloth", "helm"]
PART_MAT = {
    "cuirass": "plate",
    "hips": "mail",
    "belt": "leather",
    "buckle": "brass",
    "fauld": "plate",
    "tabard": "cloth",
    "helm": "helm",
    "crest": "helm",
    "sleeve": "mail",
    "vambrace": "plate",
    "elbow": "plate",
    "elbowspike": "spike",
    "blade": "spike",
    "gauntlet": "leather",
    "thumb": "leather",
    "pauldron": "plate",
    "lame": "plate",
    "spike": "spike",
    "thigh": "mail",
    "cuisse": "plate",
    "kneecop": "plate",
    "kneespike": "spike",
    "kneefan": "plate",
    "greave": "plate",
    "sabaton": "plate",
}
PART_ID = {n: i for i, n in enumerate(PART_MAT)}
PARTS = []  # (name, bmesh, sx)


def new_bm():
    bm = bmesh.new()
    layers(bm)  # before any face: adding a layer later invalidates the face references held by the builders
    return bm


def layers(bm):
    """The painting coordinates every part carries: a `param` UV map (u around or across the part, v along it, both
    0..1) and per-face floats: part id, open in u (1) or a closed ring (0), and the part's size in metres (lu, lv)."""
    uv = bm.loops.layers.uv.get("param") or bm.loops.layers.uv.new("param")
    fl = {n: bm.faces.layers.float.get(n) or bm.faces.layers.float.new(n) for n in ("part", "openu", "lu", "lv")}
    return uv, fl


def set_face(bm, f, uvs, lu=0.0, lv=0.0, openu=0.0):
    uv, fl = layers(bm)
    for loop, c in zip(f.loops, uvs, strict=True):
        loop[uv].uv = c
    f[fl["lu"]], f[fl["lv"]], f[fl["openu"]] = lu, lv, openu


def add_part(name, bm, slot, inside=None, closed=False, sx=1):
    """Store a part. Closed parts get outward normals by recalculation, open shells by the `inside` reference
    (a point or a function of the face centre). sx = -1 mirrors the part to the left side. `slot` is kept for
    reading; the material comes from PART_MAT."""
    _uv, fl = layers(bm)
    lu = max((f[fl["lu"]] for f in bm.faces), default=0.0) or 0.1
    lv = max((f[fl["lv"]] for f in bm.faces), default=0.0) or 0.1
    for f in bm.faces:
        f[fl["part"]] = PART_ID[name]
        if f[fl["lv"]] == 0.0:
            f[fl["lu"]], f[fl["lv"]] = lu, lv
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
    PARTS.append((name, bm, sx))


def grid_faces(bm, rows, closed_ring=True):
    """Quads between consecutive rows of BMVerts, with the painting coordinates (see `layers`)."""
    n = len(rows[0])
    m = n if closed_ring else n - 1
    vcum = [0.0]
    for a, b in itertools.pairwise(rows):
        vcum.append(vcum[-1] + sum((b[k].co - a[k].co).length for k in range(n)) / n)
    lv = vcum[-1] or 1e-4
    lu = sum(sum((r[(k + 1) % n].co - r[k].co).length for k in range(m)) for r in rows) / len(rows)
    for i, (a, b) in enumerate(itertools.pairwise(rows)):
        v0, v1 = vcum[i] / lv, vcum[i + 1] / lv
        for k in range(m):
            k1 = (k + 1) % n
            f = bm.faces.new((a[k], a[k1], b[k1], b[k]))
            u0, u1 = k / m, (k + 1) / m
            set_face(bm, f, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], lu, lv, 0.0 if closed_ring else 1.0)


def cap(bm, row, at=None, v=0.0):
    """Close a ring with a fan around its centre (or `at`); the fan keeps the ring's v."""
    c = at if at is not None else sum((v.co for v in row), V()) / len(row)
    cv = bm.verts.new(c)
    n = len(row)
    for k in range(n):
        f = bm.faces.new((row[k], row[(k + 1) % n], cv))
        set_face(bm, f, [(k / n, v), ((k + 1) / n, v), ((k + 0.5) / n, v)])


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
        cap(bm, rows[0], v=0.0)
    if top or apex is not None:
        cap(bm, rows[-1], apex, v=1.0)
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
    lu, lv = TAU * r, (tip - base).length
    for k in range(sides):
        f = bm.faces.new((ring[k], ring[(k + 1) % sides], tv))
        set_face(bm, f, [(k / sides, 0.0), ((k + 1) / sides, 0.0), ((k + 0.5) / sides, 1.0)], lu, lv)
    f = bm.faces.new(ring[::-1])
    set_face(bm, f, [(0.5, 0.0)] * sides, lu, lv)
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
    for i, (a, b) in enumerate(itertools.pairwise(rows)):
        v0, v1 = i / (len(rows) - 1), (i + 1) / (len(rows) - 1)
        f = bm.faces.new((a[0], b[0], b[1], a[1]))
        set_face(bm, f, [(0, v0), (0, v1), (0.5, v1), (0.5, v0)], 0.05, 0.3, 1.0)
        f = bm.faces.new((a[1], b[1], b[2], a[2]))
        set_face(bm, f, [(0.5, v0), (0.5, v1), (1, v1), (1, v0)], 0.05, 0.3, 1.0)
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


# ---------------------------------------------------------------------------- assemble


def build():
    build_torso()
    build_tabards()
    build_helm()
    middle = len(PARTS)
    for sx in (1, -1):
        build_arm(sx)
        build_pauldron(sx)
        build_leg(sx)
    whole = bmesh.new()
    info = []  # (name, side: 0 middle, 1 right, -1 left, first vertex, first face, faces) per part, in mesh order
    nv = nf = 0
    for k, (name, bm, sx) in enumerate(PARTS):
        sx = 0 if k < middle else sx
        me = bpy.data.meshes.new("tmp")
        bm.to_mesh(me)
        for p in me.polygons:
            p.material_index = MATS.index(PART_MAT[name])
        info.append((name, sx, nv, nf, len(me.polygons)))
        nv += len(me.vertices)
        nf += len(me.polygons)
        whole.from_mesh(me)
        bpy.data.meshes.remove(me)
        bm.free()
    me = bpy.data.meshes.new("Knight")
    whole.to_mesh(me)
    whole.free()
    for name in MATS:
        me.materials.append(bpy.data.materials.new(name))
    obj = bpy.data.objects.new("Knight", me)
    bpy.context.scene.collection.objects.link(obj)
    return obj, info


def report(obj):
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    print(f"knight: {len(obj.data.vertices)} vertices, {tris} triangles")


# ---------------------------------------------------------------------------- atlas

UV_SHARE = {"helm": 1.5, "crest": 1.2, "cuirass": 1.3, "tabard": 1.25, "pauldron": 1.25, "buckle": 1.5}


def hidden_faces(me, reach=0.2):
    """Faces that cannot be seen (inside or under other parts): nearly every ray from their front hits the mesh. They
    get small islands; their paint is never on screen."""
    from mathutils.bvhtree import BVHTree

    tree = BVHTree.FromPolygons([v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
    dirs = []
    for i in range(24):  # a fibonacci sphere
        z = 1 - 2 * (i + 0.5) / 24
        r = math.sqrt(1 - z * z)
        a = i * 2.39996
        dirs.append(V((r * math.cos(a), r * math.sin(a), z)))
    out = set()
    for p in me.polygons:
        o = p.center + p.normal * 0.003
        ds = [d for d in dirs if d.dot(p.normal) > 0.1]
        hit = sum(1 for d in ds if tree.ray_cast(o, d, reach)[0] is not None)
        if ds and hit >= len(ds) - 1:
            out.add(p.index)
    print(f"atlas: {len(out)} hidden faces get small islands")
    return out


def unwrap_atlas(obj, info, margin=0.008):
    """The `atlas` UV map: the middle and the right side are unwrapped and packed; each left part is mirrored from its
    right twin onto the same islands (the paint is symmetric), so the atlas holds every island once at more pixels.
    The parts seen most (helm, chest, tabard, pauldrons) get a larger share."""
    me = obj.data
    right = [p for p in info if p[1] > 0]
    left = [p for p in info if p[1] < 0]
    assert [p[0] for p in right] == [p[0] for p in left]
    left_faces = {f for p in left for f in range(p[3], p[3] + p[4])}
    uvl = me.uv_layers.new(name="atlas")
    me.uv_layers.active = uvl
    uvl.active_render = True
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    def select_unwrapped():
        for v in me.vertices:
            v.select = False
        for f in me.polygons:
            f.select = f.index not in left_faces
            if f.select:
                for v in f.vertices:
                    me.vertices[v].select = True

    select_unwrapped()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="FACE")
    bpy.ops.mesh.hide(unselected=True)
    bpy.ops.uv.smart_project(angle_limit=math.radians(62), island_margin=margin, area_weight=1.0)
    bpy.ops.mesh.reveal(select=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    uv = me.uv_layers["atlas"].data
    hidden = hidden_faces(me)
    for name, _side, _v0, f0, nf in info:
        for f in range(f0, f0 + nf):
            k = UV_SHARE.get(name, 1.0) * (0.2 if f in hidden else 1.0)
            for li in me.polygons[f].loop_indices:
                uv[li].uv = uv[li].uv * k
    select_unwrapped()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.hide(unselected=True)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(margin=margin, shape_method="CONCAVE", rotate=True)
    bpy.ops.mesh.reveal(select=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    uv = me.uv_layers["atlas"].data
    for r, lp in zip(right, left, strict=True):
        assert r[4] == lp[4], r[0]
        for j in range(r[4]):
            fr, fl = me.polygons[r[3] + j], me.polygons[lp[3] + j]
            by_vert = {me.loops[li].vertex_index: tuple(uv[li].uv) for li in fr.loop_indices}
            for li in fl.loop_indices:
                uv[li].uv = by_vert[me.loops[li].vertex_index - lp[2] + r[2]]


# ---------------------------------------------------------------------------- materials


def srgb(h):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    return tuple(lin(int(h[i : i + 2], 16)) for i in (0, 2, 4))


# Colour schemes (sRGB). Crimson: the 1999 knight's mauve steel with deep red cloth and enamel, dull brass trims.
# Ebon: blackened blue steel, navy cloth and enamel, silver trims.
PALETTES = {
    "crimson": {
        "steel": "6c6166",
        "steel_dark": "362e33",
        "steel_hi": "bcb0b0",
        "grime": "1e171a",
        "trim": "a07a40",
        "trim_dark": "4e381e",
        "accent": "6c1310",
        "accent_dark": "320807",
        "accent_hi": "962418",
        "mail": "625a5e",
        "mail_dark": "221e20",
        "leather": "5c3e2a",
        "leather_dark": "2e1e14",
        "stitch": "b8a078",
        "glow": "ff6a28",
    },
    "ebon": {
        "steel": "454b5a",
        "steel_dark": "1e2128",
        "steel_hi": "a0aabe",
        "grime": "141519",
        "trim": "b9bfca",
        "trim_dark": "5a606e",
        "accent": "1e3264",
        "accent_dark": "0e1630",
        "accent_hi": "34528c",
        "mail": "585d69",
        "mail_dark": "1c1e24",
        "leather": "3c322e",
        "leather_dark": "1c1614",
        "stitch": "9a9896",
        "glow": "7cf0b0",
    },
}


class Paint:
    """A small expression builder over a material's shader nodes: floats and colours (vectors) in, sockets out."""

    def __init__(self, mat, pal):
        mat.use_nodes = True
        self.nt = mat.node_tree
        self.nt.nodes.clear()
        self.pal = {k: srgb(v) for k, v in pal.items()}
        co = self.node("ShaderNodeTexCoord").outputs["Object"]
        x, y, z = self.sep(co)
        self.ax, self.y, self.z = self.math("ABSOLUTE", x), y, z
        self.P = self.comb(self.ax, y, z)  # mirrored object position: the paint is symmetric
        uvn = self.node("ShaderNodeUVMap")
        uvn.uv_map = "param"
        self.u, self.v, _ = self.sep(uvn.outputs["UV"])
        self.part = self.attr("part")
        self.lu, self.lv, self.openu = self.attr("lu"), self.attr("lv"), self.attr("openu")
        # distance (metres) to the part's ends (v) and, for open shells, to its sides (u)
        self.dv = self.mul(self.math("MINIMUM", self.v, self.sub(1.0, self.v)), self.lv)
        du = self.mul(self.math("MINIMUM", self.u, self.sub(1.0, self.u)), self.lu)
        self.du = self.add(du, self.mul(self.sub(1.0, self.openu), 10.0))
        self.dist = self.math("MINIMUM", self.dv, self.du)
        self.su = self.mul(self.u, self.lu)  # arc length along an end border
        ao = self.node("ShaderNodeAmbientOcclusion")
        ao.only_local = True
        ao.samples = 16
        ao.inputs["Distance"].default_value = 0.07
        self.ao = ao.outputs["AO"]

    # -- plumbing
    def node(self, t, **props):
        n = self.nt.nodes.new(t)
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def put(self, sock, val):
        if isinstance(val, (int, float)):
            sock.default_value = float(val)
        elif isinstance(val, tuple):
            sock.default_value = (*val, 1.0)[: len(sock.default_value)]
        else:
            self.nt.links.new(val, sock)

    def math(self, op, a, b=0.0, c=0.0):
        n = self.node("ShaderNodeMath", operation=op)
        for k, x in enumerate((a, b, c)):
            self.put(n.inputs[k], x)
        return n.outputs[0]

    def add(self, a, b):
        return self.math("ADD", a, b)

    def sub(self, a, b):
        return self.math("SUBTRACT", a, b)

    def mul(self, a, b):
        return self.math("MULTIPLY", a, b)

    def vmath(self, op, a, b=(0.0, 0.0, 0.0), scale=None):
        n = self.node("ShaderNodeVectorMath", operation=op)
        self.put(n.inputs[0], a)
        self.put(n.inputs[1], b)
        if scale is not None:
            self.put(n.inputs[3], scale)
        return n.outputs[0]

    def sep(self, vec):
        n = self.node("ShaderNodeSeparateXYZ")
        self.put(n.inputs[0], vec)
        return n.outputs[0], n.outputs[1], n.outputs[2]

    def comb(self, x, y, z):
        n = self.node("ShaderNodeCombineXYZ")
        for k, c in enumerate((x, y, z)):
            self.put(n.inputs[k], c)
        return n.outputs[0]

    def attr(self, name):
        n = self.node("ShaderNodeAttribute", attribute_type="GEOMETRY", attribute_name=name)
        return n.outputs["Fac"]

    # -- shaping
    def ss(self, x, a, b):
        """Smoothstep 0 at a to 1 at b (a < b)."""
        n = self.node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP", clamp=True)
        for k, val in enumerate((x, a, b, 0.0, 1.0)):
            self.put(n.inputs[k], val)
        return n.outputs[0]

    def inv(self, x):
        return self.sub(1.0, x)

    def band(self, x, c, w):
        """1 within w/2 of c, fading to 0 at w."""
        return self.inv(self.ss(self.math("ABSOLUTE", self.sub(x, c)), w * 0.5, w))

    def length2(self, a, b):
        return self.math("SQRT", self.add(self.mul(a, a), self.mul(b, b)))

    def is_part(self, *names):
        m = 0.0
        for n in names:
            m = self.add(m, self.math("COMPARE", self.part, float(PART_ID[n]), 0.5))
        return m

    def noise(self, vec, scale, detail=3.0, rough=0.55):
        n = self.node("ShaderNodeTexNoise")
        self.put(n.inputs["Vector"], vec)
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        return n.outputs["Fac"]

    def edges(self, vec, scale):
        n = self.node("ShaderNodeTexVoronoi", feature="DISTANCE_TO_EDGE")
        self.put(n.inputs["Vector"], vec)
        n.inputs["Scale"].default_value = scale
        return n.outputs["Distance"]

    # -- colours (vectors)
    def c(self, name):
        return self.pal[name]

    def mix(self, a, b, t):
        return self.vmath("ADD", a, self.vmath("SCALE", self.vmath("SUBTRACT", b, a), scale=t))

    def shade(self, col, k):
        return self.vmath("SCALE", col, scale=k)

    def out(self, col):
        bsdf = self.node("ShaderNodeBsdfPrincipled")
        self.put(bsdf.inputs["Base Color"], col)
        bsdf.inputs["Roughness"].default_value = 1.0
        bsdf.inputs["Metallic"].default_value = 0.0
        o = self.node("ShaderNodeOutputMaterial")
        self.nt.links.new(bsdf.outputs[0], o.inputs["Surface"])

    # -- shared looks
    def steel(self, base="steel", dark="steel_dark", hi="steel_hi"):
        """Plate steel: soft mottling, vertical brushing, a few bright scratches, grime in the recesses."""
        P = self.P
        col = self.mix(self.c(dark), self.c(base), self.ss(self.noise(P, 3.2, 4.0), 0.28, 0.72))
        streak = self.noise(self.vmath("MULTIPLY", P, (45.0, 45.0, 4.0)), 1.0, 2.0)
        col = self.mix(col, self.c(hi), self.mul(self.ss(streak, 0.52, 0.78), 0.16))
        line = self.inv(self.ss(self.edges(P, 7.0), 0.0, 0.022))
        sparse = self.ss(self.noise(P, 4.0, 2.0), 0.56, 0.66)
        col = self.mix(col, self.c(hi), self.mul(self.mul(line, sparse), 0.45))
        return col

    def brass(self):
        return self.mix(self.c("trim_dark"), self.c("trim"), self.ss(self.noise(self.P, 9.0, 3.0), 0.25, 0.7))

    def grime(self, col, k=1.0):
        """Grime in the recesses and a painted top light (Drakan 2 textures carry their shading)."""
        col = self.mix(self.c("grime"), col, self.add(self.mul(self.ss(self.ao, 0.15, 0.95), k), 1.0 - k))
        nz = self.sep(self.node("ShaderNodeNewGeometry").outputs["Normal"])[2]
        return self.shade(col, self.add(0.78, self.mul(self.ss(nz, -0.9, 0.9), 0.38)))

    def trims(self, col, width=0.022, rivet_step=0.055, only=None):
        """A painted trim along the part's borders: brass band, an engraved line inside it, rivets along the ends and a
        bright worn edge. `only` (a 0/1 mask) limits it to some parts."""
        d = self.dist
        # thin parts (lames) get a trim narrower than a fifth of their size
        size = self.math("MINIMUM", self.lv, self.add(self.lu, self.mul(self.sub(1.0, self.openu), 10.0)))
        width = self.math("MINIMUM", width, self.mul(size, 0.2))
        m = self.inv(self.ss(d, self.sub(width, 0.004), width))
        groove = self.band(d, self.add(width, 0.003), 0.005)
        fr = self.math("FRACT", self.math("DIVIDE", self.su, rivet_step))
        r = self.length2(self.mul(self.sub(fr, 0.5), rivet_step), self.sub(d, self.mul(width, 0.5)))
        rivet = self.mul(self.inv(self.ss(r, 0.0038, 0.0062)), self.math("LESS_THAN", self.dv, self.du))
        shadow = self.mul(self.band(r, 0.0072, 0.004), self.math("LESS_THAN", self.dv, self.du))
        edge = self.inv(self.ss(d, 0.0, 0.0055))
        if only is not None:
            m, groove, rivet, shadow, edge = (self.mul(x, only) for x in (m, groove, rivet, shadow, edge))
        col = self.mix(col, self.brass(), m)
        col = self.mix(col, self.c("grime"), self.mul(groove, 0.7))
        col = self.mix(col, self.c("grime"), self.mul(shadow, 0.6))
        col = self.mix(col, self.c("steel_hi"), rivet)
        col = self.mix(col, self.c("steel_hi"), self.mul(edge, 0.55))
        return col


def paint_plate(g):
    col = g.steel()
    ax, y, z, u, v = g.ax, g.y, g.z, g.u, g.v
    front = g.ss(g.math("MULTIPLY", y, -1.0), 0.07, 0.10)
    cuir = g.is_part("cuirass")
    # cuirass: lame steps (groove under, highlight over, a brass band below the step), centre ridge, a chest emblem
    for zs in (0.2365, 0.3065):
        col = g.mix(col, g.brass(), g.mul(g.band(z, zs - 0.010, 0.012), cuir))
        col = g.mix(col, g.c("grime"), g.mul(g.band(z, zs - 0.0025, 0.004), cuir))
        col = g.mix(col, g.c("steel_hi"), g.mul(g.band(z, zs + 0.004, 0.005), g.mul(cuir, 0.6)))
    ridge = g.mul(g.inv(g.ss(ax, 0.003, 0.009)), g.ss(z, 0.31, 0.34))
    col = g.mix(col, g.c("steel_hi"), g.mul(g.mul(ridge, front), g.mul(cuir, 0.7)))
    back = g.ss(y, 0.07, 0.10)
    spine = g.mul(g.inv(g.ss(ax, 0.004, 0.012)), g.mul(back, cuir))
    col = g.mix(col, g.c("grime"), g.mul(spine, 0.5))
    col = g.mix(col, g.c("steel_hi"), g.mul(g.mul(g.band(ax, 0.016, 0.008), g.mul(back, cuir)), 0.5))
    # emblem: a flame on the ridge between two swept wings, in brass with a dark rim
    fz = g.math("DIVIDE", g.sub(z, 0.395), 0.19)  # 0 at the flame foot, 1 at its tip
    fzc = g.math("MINIMUM", g.math("MAXIMUM", fz, 0.0), 1.0)
    fw = g.mul(0.030, g.mul(g.math("SINE", g.mul(g.math("POWER", fzc, 0.55), math.pi)), g.sub(1.1, fzc)))
    fw = g.mul(fw, g.add(1.0, g.mul(0.3, g.math("SINE", g.mul(fzc, 3.0 * math.pi)))))  # licking edges
    flame = g.mul(g.inv(g.ss(g.sub(ax, fw), -0.003, 0.002)), g.mul(g.ss(fz, 0.0, 0.04), g.inv(g.ss(fz, 0.96, 1.0))))
    wings = 0.0
    for rad, reach in ((0.075, 1.45), (0.100, 1.30), (0.125, 1.15)):  # three feathers, the outer ones shorter
        wx, wz = ax, g.sub(z, 0.37)
        arc = g.band(g.length2(wx, wz), rad, 0.012)
        arc = g.mul(arc, g.mul(g.ss(ax, 0.036, 0.05), g.ss(wz, -0.02, 0.0)))
        wings = g.add(wings, g.mul(arc, g.inv(g.ss(g.math("ARCTAN2", wx, wz), reach - 0.1, reach))))
    a1 = g.add(flame, wings)
    a2 = stem = curl = 0.0
    emb = g.math("MINIMUM", g.add(g.add(a1, a2), g.add(stem, curl)), 1.0)
    emb = g.mul(emb, g.mul(front, cuir))
    halo = g.mul(g.ss(emb, 0.05, 0.3), 0.5)
    col = g.mix(col, g.c("grime"), halo)
    col = g.mix(col, g.brass(), g.ss(emb, 0.35, 0.6))
    core = g.mul(
        g.inv(g.ss(g.sub(ax, g.mul(fw, 0.5)), -0.002, 0.002)), g.mul(g.ss(fz, 0.12, 0.18), g.inv(g.ss(fz, 0.8, 0.86)))
    )
    col = g.mix(col, g.c("accent"), g.mul(core, g.mul(front, cuir)))
    # pauldron and lames: a band of enamel inside the trim
    paul = g.is_part("pauldron", "lame")
    col = g.mix(col, g.c("accent"), g.mul(g.band(g.dist, 0.036, 0.016), paul))
    # vambrace: enamel ring near the elbow, a brass cuff at the wrist
    vam = g.is_part("vambrace")
    vm = g.mul(v, g.lv)
    col = g.mix(col, g.c("accent"), g.mul(g.band(vm, 0.075, 0.022), vam))
    # greave: shin ridge highlight, a brass diamond on the shin, an enamel stripe under the knee
    grv = g.is_part("greave")
    du0 = g.mul(g.math("MINIMUM", u, g.sub(1.0, u)), g.lu)
    col = g.mix(col, g.c("steel_hi"), g.mul(g.inv(g.ss(du0, 0.003, 0.010)), g.mul(grv, 0.6)))
    q = g.add(g.math("DIVIDE", du0, 0.032), g.math("DIVIDE", g.math("ABSOLUTE", g.sub(vm, 0.20)), 0.075))
    dia = g.mul(g.add(g.band(q, 1.0, 0.24), g.inv(g.ss(q, 0.22, 0.32))), grv)
    col = g.mix(col, g.c("grime"), g.mul(g.band(q, 1.0, 0.5), g.mul(grv, 0.5)))
    col = g.mix(col, g.brass(), g.math("MINIMUM", dia, 1.0))
    col = g.mix(col, g.c("accent"), g.mul(g.band(vm, 0.055, 0.02), grv))
    # knee and elbow cops: a boss ring in the middle
    cop = g.is_part("kneecop", "elbow")
    r = g.length2(g.mul(g.sub(u, 0.5), g.lu), g.mul(g.sub(v, 0.5), g.lv))
    col = g.mix(col, g.brass(), g.mul(g.band(r, 0.030, 0.010), cop))
    col = g.mix(col, g.c("steel_hi"), g.mul(g.inv(g.ss(r, 0.008, 0.016)), g.mul(cop, 0.6)))
    # sabaton lames
    sab = g.is_part("sabaton")
    for vs in (0.122, 0.192):
        col = g.mix(col, g.c("grime"), g.mul(g.band(vm, vs, 0.008), sab))
        col = g.mix(col, g.brass(), g.mul(g.band(vm, vs + 0.010, 0.008), sab))
    # borders: no trim on the cuirass and greave ends (hidden under the belt, cowl, knee and foot)
    col = g.trims(col, only=g.inv(g.is_part("cuirass", "greave", "sabaton")))
    return g.grime(col)


def paint_helm(g):
    col = g.steel()
    ax, y, z = g.ax, g.y, g.z
    front = g.ss(g.math("MULTIPLY", y, -1.0), 0.075, 0.105)
    crest = g.is_part("crest")
    helm = g.inv(crest)
    # rim trim and rivets at the bottom of the cowl
    rim = g.mul(g.v, g.lv)
    col = g.mix(col, g.brass(), g.mul(g.inv(g.ss(rim, 0.020, 0.025)), helm))
    fr = g.math("FRACT", g.math("DIVIDE", g.su, 0.05))
    rr = g.length2(g.mul(g.sub(fr, 0.5), 0.05), g.sub(rim, 0.011))
    col = g.mix(col, g.c("steel_hi"), g.mul(g.inv(g.ss(rr, 0.004, 0.0062)), helm))
    col = g.mix(col, g.c("grime"), g.mul(g.band(rim, 0.028, 0.005), g.mul(helm, 0.7)))
    # face stripes in enamel down the brow and cheeks (the 1999 knights' motif)
    stripe = g.mul(g.band(ax, 0.046, 0.016), g.mul(g.ss(z, 0.69, 0.71), g.inv(g.ss(z, 0.865, 0.885))))
    col = g.mix(col, g.c("accent"), g.mul(g.mul(stripe, front), helm))
    # nose ridge highlight
    nose = g.mul(g.inv(g.ss(ax, 0.004, 0.010)), g.mul(g.ss(z, 0.70, 0.72), front))
    col = g.mix(col, g.c("steel_hi"), g.mul(nose, g.mul(helm, 0.6)))
    # eye slits, angled up to the sides, dark with a glow core
    ex = g.math("DIVIDE", g.sub(ax, 0.046), 0.030)
    ez = g.math("DIVIDE", g.sub(z, g.add(0.789, g.mul(ex, 0.008))), 0.0085)
    e = g.add(g.mul(ex, ex), g.mul(ez, ez))
    slit = g.mul(g.inv(g.ss(e, 0.75, 1.0)), front)
    col = g.mix(col, (0.008, 0.007, 0.008), slit)
    col = g.mix(col, g.c("glow"), g.mul(g.inv(g.ss(e, 0.05, 0.4)), front))
    # breathing holes on the chin guard
    hx = g.math("FRACT", g.math("DIVIDE", ax, 0.015))
    hz = g.math("FRACT", g.math("DIVIDE", z, 0.015))
    hole = g.inv(g.ss(g.length2(g.sub(hx, 0.5), g.sub(hz, 0.5)), 0.18, 0.28))
    region = g.mul(
        g.mul(g.ss(ax, 0.012, 0.016), g.inv(g.ss(ax, 0.055, 0.06))),
        g.mul(g.ss(z, 0.635, 0.64), g.inv(g.ss(z, 0.69, 0.695))),
    )
    col = g.mix(col, g.c("grime"), g.mul(g.mul(hole, region), g.mul(front, helm)))
    # crest: brass with a bright ridge and a dark foot
    ccol = g.mix(g.brass(), g.c("steel_hi"), g.mul(g.band(g.u, 0.5, 0.12), 0.5))
    ccol = g.mix(ccol, g.c("grime"), g.mul(g.ss(g.math("ABSOLUTE", g.sub(g.u, 0.5)), 0.38, 0.5), 0.6))
    col = g.mix(col, ccol, crest)
    return g.grime(col)


def paint_spike(g):
    col = g.mix(g.c("steel_dark"), g.c("steel_hi"), g.ss(g.v, 0.0, 1.0))
    col = g.mix(col, g.c("steel_dark"), g.mul(g.ss(g.noise(g.P, 14.0, 3.0), 0.5, 0.75), 0.4))
    col = g.mix(col, g.c("grime"), g.inv(g.ss(g.v, 0.0, 0.12)))
    return g.grime(col)


def paint_mail(g):
    s = g.math("DIVIDE", g.mul(g.u, g.lu), 0.016)
    t = g.math("DIVIDE", g.mul(g.v, g.lv), 0.012)
    b = g.node("ShaderNodeTexBrick", offset=0.5, offset_frequency=2)
    g.put(b.inputs["Vector"], g.comb(s, t, 0.0))
    b.inputs["Scale"].default_value = 1.0
    b.inputs["Mortar Size"].default_value = 0.22
    b.inputs["Mortar Smooth"].default_value = 0.6
    b.inputs["Brick Width"].default_value = 1.0
    b.inputs["Row Height"].default_value = 1.0
    g.put(b.inputs["Color1"], g.c("mail"))
    g.put(b.inputs["Color2"], g.mix(g.c("mail"), g.c("steel_hi"), 0.25))
    g.put(b.inputs["Mortar"], g.c("mail_dark"))
    col = b.outputs["Color"]
    col = g.mix(col, g.c("mail_dark"), g.mul(g.ss(g.noise(g.P, 5.0, 3.0), 0.45, 0.8), 0.45))
    # sleeve and thigh ends: a leather hem
    hem = g.mul(g.inv(g.ss(g.dv, 0.012, 0.016)), g.is_part("sleeve", "thigh"))
    col = g.mix(col, g.c("leather"), hem)
    return g.grime(col, 1.2)


def paint_leather(g):
    col = g.mix(g.c("leather_dark"), g.c("leather"), g.ss(g.noise(g.P, 14.0, 5.0, 0.65), 0.3, 0.72))
    col = g.mix(col, g.c("leather_dark"), g.mul(g.inv(g.ss(g.dist, 0.0, 0.008)), 0.6))
    dash = g.math("LESS_THAN", g.math("FRACT", g.math("DIVIDE", g.su, 0.011)), 0.55)
    stitch = g.mul(g.mul(g.band(g.dv, 0.0075, 0.003), dash), g.math("LESS_THAN", g.dv, g.du))
    col = g.mix(col, g.c("stitch"), g.mul(stitch, 0.85))
    # gauntlet: steel knuckle plate with rivets and a steel cuff
    gnt = g.is_part("gauntlet")
    knuck = g.mul(g.band(g.v, 0.64, 0.24), gnt)
    cuff = g.mul(g.inv(g.ss(g.v, 0.16, 0.2)), gnt)
    plate = g.steel()
    col = g.mix(col, plate, g.math("MINIMUM", g.add(knuck, cuff), 1.0))
    col = g.mix(col, g.brass(), g.mul(g.band(g.v, 0.18, 0.03), gnt))
    fr = g.math("FRACT", g.math("DIVIDE", g.su, 0.03))
    rv = g.inv(g.ss(g.length2(g.mul(g.sub(fr, 0.5), 0.03), g.mul(g.sub(g.v, 0.64), g.lv)), 0.003, 0.0055))
    col = g.mix(col, g.c("steel_hi"), g.mul(rv, gnt))
    return g.grime(col)


def paint_brass(g):
    # the buckle: a brass frame around a dark opening with a prong
    sq = g.math("MAXIMUM", g.ax, g.math("ABSOLUTE", g.sub(g.z, 0.145)))
    col = g.brass()
    inner = g.inv(g.ss(sq, 0.026, 0.030))
    col = g.mix(col, g.c("leather_dark"), inner)
    col = g.mix(col, g.brass(), g.mul(g.inv(g.ss(g.ax, 0.003, 0.006)), inner))
    col = g.mix(col, g.c("steel_hi"), g.mul(g.band(sq, 0.040, 0.008), 0.5))
    return g.grime(col)


def paint_cloth(g):
    u, v = g.u, g.v
    fold = g.add(0.5, g.mul(0.5, g.math("COSINE", g.mul(u, TAU * 2.5))))
    wob = g.noise(g.vmath("MULTIPLY", g.P, (8.0, 8.0, 1.5)), 1.0, 2.0)
    col = g.mix(g.c("accent_dark"), g.c("accent"), g.add(0.35, g.mul(0.65, g.mul(fold, g.add(0.6, g.mul(wob, 0.8))))))
    col = g.mix(col, g.c("accent_hi"), g.mul(g.ss(fold, 0.85, 1.0), 0.25))
    weave = g.noise(g.P, 160.0, 1.0)
    col = g.mix(col, g.c("accent_dark"), g.mul(g.ss(weave, 0.4, 0.7), 0.25))
    hd = g.mul(g.sub(1.0, v), g.lv)  # distance to the hem
    hem = g.mul(g.ss(hd, 0.014, 0.018), g.inv(g.ss(hd, 0.040, 0.044)))
    side = g.mul(g.inv(g.ss(g.du, 0.010, 0.013)), g.ss(hd, 0.044, 0.05))
    col = g.mix(col, g.brass(), g.math("MINIMUM", g.add(hem, side), 1.0))
    col = g.mix(col, g.c("accent_dark"), g.mul(g.band(hd, 0.048, 0.006), 0.8))
    # frayed tail under the hem band
    fray = g.mul(
        g.inv(g.ss(hd, 0.0, 0.014)), g.ss(g.noise(g.vmath("MULTIPLY", g.P, (90.0, 90.0, 8.0)), 1.0, 2.0), 0.35, 0.6)
    )
    col = g.mix(col, g.c("accent_dark"), g.add(0.35, g.mul(fray, 0.5)))
    # front panel emblem: a brass lozenge with a flame inside
    front = g.ss(g.math("MULTIPLY", g.y, -1.0), 0.05, 0.1)
    q = g.add(g.math("DIVIDE", g.ax, 0.052), g.math("DIVIDE", g.math("ABSOLUTE", g.sub(g.z, -0.115)), 0.085))
    col = g.mix(col, g.c("accent_dark"), g.mul(g.band(q, 1.0, 0.36), g.mul(front, 0.7)))
    col = g.mix(col, g.brass(), g.mul(g.band(q, 1.0, 0.18), front))
    fz = g.math("DIVIDE", g.sub(g.z, -0.165), 0.10)  # 0 at the flame foot, 1 at its tip
    fw = g.mul(
        0.026,
        g.mul(g.math("SINE", g.mul(g.math("MINIMUM", g.math("MAXIMUM", fz, 0.0), 1.0), math.pi)), g.sub(1.15, fz)),
    )
    flame = g.mul(g.inv(g.ss(g.sub(g.ax, fw), -0.002, 0.002)), g.mul(g.ss(fz, 0.0, 0.05), g.inv(g.ss(fz, 0.95, 1.0))))
    col = g.mix(col, g.c("accent_hi"), g.mul(flame, front))
    col = g.mix(col, g.brass(), g.mul(g.mul(flame, g.inv(g.ss(g.sub(g.ax, g.mul(fw, 0.45)), -0.002, 0.002))), front))
    return g.grime(col, 0.8)


PAINTERS = {
    "plate": paint_plate,
    "helm": paint_helm,
    "spike": paint_spike,
    "mail": paint_mail,
    "leather": paint_leather,
    "brass": paint_brass,
    "cloth": paint_cloth,
}


def paint(obj, pal):
    for m in obj.data.materials:
        g = Paint(m, pal)
        g.out(PAINTERS[m.name](g))


# ---------------------------------------------------------------------------- export, render


def make_opaque(png, grow=6):
    """The bake leaves partial alpha on island borders; the game cuts alpha at 50 %, which would open holes along the
    seams. Grow the islands' colours a few more pixels into the empty space and make the whole picture opaque."""
    import numpy as np

    img = bpy.data.images.load(str(png), check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    filled = px[..., 3] > 0.5
    rgb = px[..., :3].copy()
    for _ in range(grow):
        acc = np.zeros_like(rgb)
        cnt = np.zeros((h, w), dtype=np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            m = np.roll(filled, (dy, dx), axis=(0, 1))
            acc += np.roll(rgb, (dy, dx), axis=(0, 1)) * m[..., None]
            cnt += m
        new = ~filled & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]
        filled |= new
    px[..., :3] = rgb
    px[..., 3] = 1.0
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = str(png)
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def render(obj, png, tag):
    """Workbench renders of the baked atlas on the mesh (no lighting but the studio light): what the texture carries."""
    img = bpy.data.images.load(str(png), check_existing=False)
    for m in obj.data.materials:
        n = m.node_tree.nodes.new("ShaderNodeTexImage")
        n.image = img
        n.interpolation = "Closest"
        m.node_tree.nodes.active = n
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "FLAT"
    sc.display.shading.color_type = "TEXTURE"
    sc.view_settings.view_transform = "Standard"
    sc.render.resolution_x = ARGS.res
    sc.render.resolution_y = ARGS.res
    cam = sc.camera
    if cam is None:
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        sc.collection.objects.link(cam)
        sc.camera = cam
    cam.data.type = "ORTHO"
    for view, az, scale, tgt in (
        ("front", 0, 2.2, V((0, 0, -0.09))),
        ("threequarter", -35, 2.2, V((0, 0, -0.09))),
        ("back", 180, 2.2, V((0, 0, -0.09))),
        ("head", -25, 0.55, V((0.03, 0, 0.72))),
        ("torso", -20, 0.9, V((0.05, 0, 0.30))),
        ("legs", -30, 1.1, V((0.0, 0, -0.55))),
        ("arm", -20, 0.9, V((0.55, 0, 0.58))),
    ):
        a = math.radians(az)
        cam.data.ortho_scale = scale
        cam.location = tgt + V((-6 * math.sin(a), -6 * math.cos(a), 0.6))
        cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = str(OUT / f"{tag}_{view}.png")
        bpy.ops.render.render(write_still=True)
    for m in obj.data.materials:
        m.node_tree.nodes.remove(m.node_tree.nodes.active)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    obj, info = build()
    report(obj)
    unwrap_atlas(obj, info)
    obj.data.shade_smooth()
    obj.data.set_sharp_from_angle(angle=math.radians(48))
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored
    from own_texture import bake_atlas

    targets = {
        "crimson": ("crimsonknight", "Crimson Knight", "crimson_knight"),
        "ebon": ("ebonknight(h)_cap", "Ebon Knight", "ebon_knight"),
    }
    for key in ("crimson", "ebon") if ARGS.which == "all" else (ARGS.which,):
        model, label, stem = targets[key]
        paint(obj, PALETTES[key])
        png = KIT / f"{stem}.png"
        slots = bake_atlas(obj, png, materials=MATS, size=512, samples=ARGS.samples)
        make_opaque(png)
        export_authored(
            obj,
            KIT / f"{stem}.json",
            model=model,
            label=label,
            fit="joints",
            extra={"landmarks": landmarks(), "radialJoints": []},
            slots=slots,
        )
        if not ARGS.no_render:
            render(obj, png, key)


main()
