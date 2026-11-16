"""Build an original low-poly Boss Blade Dragon mesh in Blender, headless, for the Order of the Flame converter.

Order of the Flame's Boss Blade Dragon (PC model `bladedragBOSS(M)`, the Blade Dragon's mesh and 36-joint rig with a
green skin) is 181 polygons. This script authors a new mesh from scratch at the same bind pose (wings spread flat, front
blade legs reaching forward, hind legs crouched, tail down and straight back) and writes it as an authored model
(`drakan-authored-model/1`, see analysis/scripts/blender/authored_export.py). It reads no game data: the proportions
below are rounded measurements of the reference, like a modeller working from a picture. The converter takes the UVs,
the 1999 textures and the skin weights from the player's own PC model at convert time.

    blender -b --factory-startup -P analysis/scripts/blender/boss_blade_dragon_model.py -- \
        --out build/authored/bossblade-model [--authored scripts/buildtool/kit/models/boss_blade_dragon.json] [--no-render]

Coordinates: built in PC model units with Blender axes (Z up, the dragon faces -Y, its right wing on +X), then scaled
by UNIT to roughly metres. The converter places the mesh with `fit: extent` (scale by the largest box side, which is
the wingspan; centred on the PC box; lowest point on its floor), so the extremes match the reference: snout tip
y = -0.64, tail tip y = 1.52, wing tips x = +-1.37, toes z = -0.55.

Parts and their slots (each slot projects from the PC texture entries it names):
* Body (entries 0, 2, 3): head, jaw, neck, body, tail, hind legs, horns, teeth, back blades.
* Wing (entry 1, the alpha-cut wing texture): the membranes and the wing bones.
* Arm (entry 1): the front blade legs, which the 1999 model draws from the wing texture's lower corner.
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from authored_export import export_authored

ROOT = Path(__file__).resolve().parents[3]
UNIT = 5.0  # PC units to (roughly) metres


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/authored/bossblade-model"))
    ap.add_argument("--authored", default=str(ROOT / "scripts/buildtool/kit/models/boss_blade_dragon.json"))
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=900)
    return ap.parse_args(argv)


# ---------------------------------------------------------------------------- small math


def lerp(a, b, t):
    return a + (b - a) * t


def catmull(pts, per_seg):
    """Catmull-Rom through tuples of floats; per_seg samples per segment, both ends included."""
    n = len(pts)
    ext = [
        tuple(2 * a - b for a, b in zip(pts[0], pts[1])),
        *list(pts),
        tuple(2 * a - b for a, b in zip(pts[-1], pts[-2])),
    ]
    out = []
    for i in range(n - 1):
        p0, p1, p2, p3 = ext[i], ext[i + 1], ext[i + 2], ext[i + 3]
        segs = per_seg[i] if isinstance(per_seg, (list, tuple)) else per_seg
        for k in range(segs):
            t = k / segs
            t2, t3 = t * t, t * t * t
            out.append(
                tuple(
                    0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
                    for a, b, c, d in zip(p0, p1, p2, p3)
                )
            )
    out.append(tuple(pts[-1]))
    return out


def resample(poly, n):
    """n + 1 points along a polyline (Vectors), evenly by arc length."""
    lens = [0.0]
    for a, b in itertools.pairwise(poly):
        lens.append(lens[-1] + (b - a).length)
    total = lens[-1]
    out = []
    for i in range(n + 1):
        s = total * i / n
        k = 1
        while k < len(lens) - 1 and lens[k] < s:
            k += 1
        t = (s - lens[k - 1]) / max(lens[k] - lens[k - 1], 1e-9)
        out.append(poly[k - 1].lerp(poly[k], min(max(t, 0.0), 1.0)))
    return out


def bezier(a, c, b, n):
    """Quadratic Bezier a -> b with control c, n + 1 points."""
    return [a * (1 - t) ** 2 + c * 2 * t * (1 - t) + b * t * t for t in (i / n for i in range(n + 1))]


# ---------------------------------------------------------------------------- mesh builders


class Part:
    """Vertices and faces with a material name per face, merged into one object at the end."""

    def __init__(self):
        self.verts: list[Vector] = []
        self.faces: list[tuple[list[int], str]] = []

    def v(self, p):
        self.verts.append(Vector(p))
        return len(self.verts) - 1

    def f(self, idx, mat):
        self.faces.append((list(idx), mat))


def loft(part, ctrl, sides, per_seg, mat, up=(0, 0, 1), phase=0.0, profile=None, cap="none"):
    """Tube through ctrl points (x, y, z, half width, half height): elliptical rings, ends closed by a tip vertex (zero
    radius) or a centre fan. `profile(angle, s)` scales the ring radius (s runs 0..1 along the tube). Faces are wound
    counter-clockwise seen from outside."""
    samples = catmull(ctrl, per_seg)
    centres = [Vector(s[:3]) for s in samples]
    upv = Vector(up)
    rings = []
    n = len(samples)
    for i, s in enumerate(samples):
        t = (centres[min(i + 1, n - 1)] - centres[max(i - 1, 0)]).normalized()
        side = t.cross(upv)
        if side.length < 1e-6:
            side = t.cross(Vector((0, 1, 0)))
        side.normalize()
        u = side.cross(t).normalized()
        w, h = max(s[3], 0.0), max(s[4], 0.0)
        if w < 1e-5 and h < 1e-5:
            rings.append([part.v(centres[i])])
            continue
        ring = []
        for j in range(sides):
            a = phase + 2 * math.pi * j / sides
            k = profile(a, i / (n - 1)) if profile else 1.0
            ring.append(part.v(centres[i] + side * (math.cos(a) * w * k) + u * (math.sin(a) * h * k)))
        rings.append(ring)
    for r0, r1 in itertools.pairwise(rings):
        if len(r0) == 1 and len(r1) == 1:
            continue
        if len(r0) == 1:
            for j in range(sides):
                part.f([r0[0], r1[j], r1[(j + 1) % sides]], mat)
        elif len(r1) == 1:
            for j in range(sides):
                part.f([r0[j], r1[0], r0[(j + 1) % sides]], mat)
        else:
            for j in range(sides):
                part.f([r0[j], r1[j], r1[(j + 1) % sides], r0[(j + 1) % sides]], mat)
    # caps: "both", "end" (the start is buried in another part) or "none"; the start cap faces back along the tube
    ends = {"both": (0, -1), "end": (-1,), "none": ()}[cap]
    for e in ends:
        r, c = rings[e], centres[e]
        if len(r) > 1:
            cv = part.v(c)
            for j in range(sides):
                f = [cv, r[j], r[(j + 1) % sides]]
                part.f(f if e == 0 else f[::-1], mat)


def spike(part, base, tip, r, mat, sides=4, up=(0, 0, 1), flat=1.0, phase=math.pi / 4):
    """A cone from base (radius r, `flat` scales the second axis) to a point."""
    loft(part, [(*base, r, r * flat), (*tip, 0.0, 0.0)], sides, 1, mat, up=up, phase=phase, cap="none")


def coons(left, right, bottom, top):
    """Grid (rows along v) of a Coons patch: bottom/top run along u (n + 1 points), left/right along v (m + 1)."""
    n, m = len(bottom) - 1, len(left) - 1
    grid = []
    for j in range(m + 1):
        v = j / m
        row = []
        for i in range(n + 1):
            u = i / n
            lc = bottom[i] * (1 - v) + top[i] * v
            ld = left[j] * (1 - u) + right[j] * u
            b = bottom[0] * (1 - u) * (1 - v) + bottom[n] * u * (1 - v) + top[0] * (1 - u) * v + top[n] * u * v
            row.append(lc + ld - b)
        grid.append(row)
    return grid


def grid_faces(part, grid, mat):
    ids = [[part.v(p) for p in row] for row in grid]
    for j in range(len(ids) - 1):
        for i in range(len(ids[0]) - 1):
            part.f([ids[j][i], ids[j][i + 1], ids[j + 1][i + 1], ids[j + 1][i]], mat)


def to_object(part, name, mats, merge=1e-5):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in part.verts], [], [f for f, _ in part.faces])
    for m in mats:
        me.materials.append(m)
    names = [m.name for m in mats]
    for poly, (_, mat) in zip(me.polygons, part.faces):
        poly.material_index = names.index(mat)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=merge)
    # degenerate faces left by collapsed corners
    bad = [f for f in bm.faces if f.calc_area() < 1e-10]
    if bad:
        bmesh.ops.delete(bm, geom=bad, context="FACES")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mirror_part(part):
    """The left-side copy of a right-side part (x negated)."""
    out = Part()
    out.verts = [Vector((-p.x, p.y, p.z)) for p in part.verts]
    out.faces = [(list(reversed(f)), m) for f, m in part.faces]
    return out


def merged(*parts):
    out = Part()
    for p in parts:
        base = len(out.verts)
        out.verts += [v.copy() for v in p.verts]
        out.faces += [([i + base for i in f], m) for f, m in p.faces]
    return out


# ---------------------------------------------------------------------------- the dragon
# All numbers: PC units, Blender axes. Main sections are (y, z centre, half width, half height).

SPINE = [
    (-0.643, -0.042, 0.0, 0.0),  # snout tip
    (-0.632, -0.040, 0.022, 0.026),
    (-0.585, -0.042, 0.034, 0.040),
    (-0.520, -0.042, 0.038, 0.044),
    (-0.450, -0.042, 0.044, 0.048),
    (-0.390, -0.050, 0.052, 0.058),  # brows and eyes
    (-0.335, -0.070, 0.050, 0.070),  # back of the skull
    (-0.275, -0.105, 0.042, 0.068),  # throat
    (-0.205, -0.125, 0.056, 0.088),
    (-0.130, -0.095, 0.095, 0.125),  # shoulders
    (-0.030, -0.055, 0.135, 0.170),  # chest and the hump between the wings
    (0.090, -0.050, 0.132, 0.158),
    (0.210, -0.062, 0.112, 0.138),  # belly
    (0.320, -0.092, 0.092, 0.102),  # hips
    (0.440, -0.128, 0.060, 0.072),
    (0.580, -0.150, 0.044, 0.056),
    (0.740, -0.172, 0.034, 0.044),
    (0.960, -0.208, 0.025, 0.033),
    (1.200, -0.243, 0.016, 0.023),
    (1.430, -0.270, 0.008, 0.012),
    (1.520, -0.280, 0.0, 0.0),  # tail tip
]
SPINE_SEGS = [1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1]


def spine_at(y):
    """Linear section of the spine at y: (z centre, half width, half height)."""
    for a, b in itertools.pairwise(SPINE):
        if a[0] <= y <= b[0]:
            t = (y - a[0]) / (b[0] - a[0])
            return tuple(lerp(a[k], b[k], t) for k in (1, 2, 3))
    return SPINE[-1][1:]


def body_profile(a, s):
    """Flatter belly, a slight keel on the back, a squarer skull."""
    c, sn = math.cos(a), math.sin(a)
    k = 1.0
    if sn < 0:  # underside: flatter
        k *= 1.0 - 0.12 * (-sn) ** 2
    else:  # back ridge
        k *= 1.0 + 0.06 * sn**8
    k *= 1.0 + 0.05 * (c * c * sn * sn) * 4  # squarer section
    return k


def build_body():
    p = Part()
    ctrl = [(0.0, y, z, w, h) for y, z, w, h in SPINE]
    loft(p, ctrl, 10, SPINE_SEGS, "Body", phase=math.pi / 2, profile=body_profile)

    # lower jaw, slightly open
    jaw = [
        (0.0, -0.345, -0.118, 0.034, 0.030),
        (0.0, -0.420, -0.112, 0.038, 0.030),
        (0.0, -0.500, -0.110, 0.034, 0.027),
        (0.0, -0.570, -0.112, 0.026, 0.022),
        (0.0, -0.612, -0.115, 0.012, 0.012),
    ]
    loft(p, jaw, 8, [2, 2, 2, 1], "Body", phase=math.pi / 2, cap="both")
    centre = p
    p = Part()  # right-side details, mirrored later

    # teeth: upper row down from the lip, lower row up from the jaw, a pair of long fangs
    for i in range(5):
        y = -0.585 + i * 0.045
        p_side = 0.030 - 0.002 * (i == 0)
        spike(p, (p_side, y, -0.072), (p_side + 0.002, y + 0.004, -0.100), 0.0055, "Body", sides=3)
        spike(p, (p_side - 0.003, y + 0.022, -0.098), (p_side - 0.002, y + 0.020, -0.080), 0.0045, "Body", sides=3)
    spike(p, (0.020, -0.605, -0.070), (0.023, -0.598, -0.120), 0.007, "Body", sides=4)

    # brow ridges over the eyes
    loft(
        p,
        [
            (0.030, -0.455, -0.005, 0.006, 0.006),
            (0.047, -0.405, 0.008, 0.012, 0.010),
            (0.044, -0.355, 0.002, 0.006, 0.006),
        ],
        5,
        2,
        "Body",
    )
    # horns: a long swept pair from the back of the skull and a cheek spike
    loft(
        p,
        [
            (0.026, -0.375, -0.005, 0.017, 0.017),
            (0.040, -0.320, 0.020, 0.014, 0.014),
            (0.058, -0.250, 0.050, 0.009, 0.009),
            (0.070, -0.175, 0.072, 0.0, 0.0),
        ],
        6,
        2,
        "Body",
    )
    loft(
        p,
        [
            (0.042, -0.390, -0.075, 0.011, 0.011),
            (0.068, -0.330, -0.068, 0.006, 0.006),
            (0.085, -0.285, -0.062, 0.0, 0.0),
        ],
        5,
        2,
        "Body",
    )
    # a small nose horn (centre)
    spike(centre, (0.0, -0.585, -0.004), (0.0, -0.560, 0.022), 0.010, "Body", sides=4)
    side = p
    p = Part()  # right hind leg

    # hind leg (right): thigh, shin, foot, three toes with claws and a heel spur
    loft(
        p,
        [
            (0.075, 0.275, -0.060, 0.050, 0.080),
            (0.098, 0.360, -0.150, 0.062, 0.070),
            (0.100, 0.465, -0.300, 0.040, 0.042),
            (0.096, 0.420, -0.400, 0.030, 0.028),
            (0.094, 0.370, -0.470, 0.026, 0.024),
            (0.094, 0.290, -0.520, 0.032, 0.020),
        ],
        7,
        [2, 2, 1, 1, 1],
        "Body",
        up=(1, 0, 0),
        cap="end",
    )
    for tip in ((0.094, 0.085, -0.542), (0.022, 0.195, -0.540), (0.166, 0.195, -0.540)):
        base = Vector((0.094, 0.290, -0.525))
        t = Vector(tip)
        mid = base.lerp(t, 0.55)
        mid.z += 0.010
        knuckle = base.lerp(t, 0.8)
        loft(
            p,
            [(*base, 0.017, 0.013), (*mid, 0.013, 0.010), (*knuckle, 0.010, 0.008), (*t, 0.0, 0.0)],
            4,
            [1, 1, 1],
            "Body",
        )
        # claw: hooks down past the toe
        d = (t - base).normalized()
        spike(p, t - d * 0.012 + Vector((0, 0, 0.002)), t + d * 0.010 + Vector((0, 0, -0.010)), 0.006, "Body", sides=4)
    spike(p, (0.094, 0.380, -0.495), (0.094, 0.425, -0.535), 0.012, "Body", sides=4)
    side = merged(side, p)
    p = centre

    # back blades: a row of flat fins from the neck down the tail
    ys = [-0.30, -0.24, -0.17, -0.10, 0.17, 0.25, 0.33, 0.41, 0.50, 0.60, 0.71, 0.83, 0.95, 1.08, 1.21, 1.34]
    for i, y in enumerate(ys):
        z, w, h = spine_at(y)
        top = z + h * 1.02
        size = 0.05 * (1.0 - 0.55 * i / len(ys)) if y > 0 else 0.032
        base = (0.0, y, top - 0.008)
        tip = (0.0, y + size * 0.9, top + size * 0.75)
        loft(p, [(*base, 0.006, size * 0.45), (*tip, 0.0, 0.0)], 4, 1, "Body", up=(1, 0, 0), phase=0.0)
    # tail blade: a flat arrowhead before the tip
    z, _, _ = spine_at(1.40)
    loft(
        p,
        [(0.0, 1.33, z + 0.004, 0.006, 0.008), (0.0, 1.39, z - 0.002, 0.008, 0.045), (0.0, 1.50, -0.279, 0.0, 0.0)],
        4,
        [1, 1],
        "Body",
        up=(1, 0, 0),
        phase=0.0,
    )
    return p, side


def build_front_leg():
    """Right front leg: shoulder, a long forearm reaching forward and down, three toe claws and the blade spur."""
    p = Part()
    loft(
        p,
        [
            (0.085, 0.020, -0.120, 0.045, 0.060),
            (0.112, 0.090, -0.215, 0.040, 0.040),
            (0.118, 0.130, -0.275, 0.032, 0.030),  # elbow
            (0.120, 0.020, -0.335, 0.026, 0.024),
            (0.118, -0.100, -0.385, 0.020, 0.018),  # wrist
            (0.116, -0.150, -0.400, 0.016, 0.013),
        ],
        6,
        [2, 1, 2, 1, 1],
        "Arm",
        up=(1, 0, 0),
        cap="end",
    )
    wrist = Vector((0.116, -0.150, -0.400))
    for tip in ((0.116, -0.205, -0.418), (0.090, -0.190, -0.412), (0.142, -0.190, -0.412)):
        t = Vector(tip)
        spike(p, wrist.lerp(t, 0.1), t, 0.008, "Arm", sides=4)
    # the blade spur sweeping back from the forearm, flat and upright
    loft(
        p,
        [
            (0.119, -0.080, -0.372, 0.005, 0.016),
            (0.120, 0.080, -0.392, 0.004, 0.012),
            (0.120, 0.220, -0.392, 0.003, 0.007),
            (0.120, 0.340, -0.383, 0.0, 0.0),
        ],
        4,
        [2, 2, 1],
        "Arm",
        up=(1, 0, 0),
        phase=0.0,
    )
    return p


# wing joints (right side): shoulder, elbow, wrist, knuckle and the three finger tips, membrane root on the back
W_S = Vector((0.075, -0.020, 0.062))
W_E = Vector((0.380, -0.195, 0.038))
W_W = Vector((0.650, -0.420, 0.026))
W_K = Vector((1.010, -0.445, 0.016))
W_T = Vector((1.368, -0.060, 0.000))
W_F2 = Vector((0.965, 0.200, 0.002))
W_F3 = Vector((0.630, 0.235, 0.012))
W_R = Vector((0.040, 0.220, 0.035))


def scallop(a, b, depth, n):
    """Trailing edge from a to b, bowed in towards the wrist."""
    mid = (a + b) / 2
    inward = (W_W - mid).normalized()
    c = mid + inward * depth
    c.z = mid.z
    return bezier(a, c, b, n)


def build_wing():
    p = Part()
    nu = 5
    f1 = resample([W_W, W_K, W_T], nu)
    f2 = resample([W_W, W_F2], nu)
    f3 = resample([W_W, W_F3], nu)
    arm = resample([W_W, W_E, W_S], nu)
    wl = lambda m: [W_W.copy() for _ in range(m + 1)]
    patches = [
        coons(wl(3), scallop(W_T, W_F2, 0.07, 3), f1, f2),
        coons(wl(3), scallop(W_F2, W_F3, 0.06, 3), f2, f3),
        coons(wl(5), scallop(W_F3, W_R, 0.08, 3)[:-1] + resample([W_R, W_S], 2), f3, arm),
    ]
    bones = [[W_S, W_E, W_W], [W_W, W_K, W_T], [W_W, W_F2], [W_W, W_F3]]

    def bone_dist(q):
        best = 1e9
        for b in [*bones, [W_S, W_R]]:
            for a, c in itertools.pairwise(b):
                ab = (c - a).xy
                t = max(0.0, min(1.0, (q.xy - a.xy).dot(ab) / max(ab.length_squared, 1e-12)))
                best = min(best, (q.xy - (a.xy + ab * t)).length)
        return best

    # membrane: one sheet, sagging between the bones, then a thin closed shell
    sheet = Part()
    for g in patches:
        g = [[q + Vector((0, 0, -min(0.9 * bone_dist(q) ** 2, 0.03))) for q in row] for row in g]
        grid_faces(sheet, g, "Wing")
    # bones: arm, three fingers, thumb claw
    root = (0.030, 0.000, 0.030, 0.024, 0.020)  # buried in the shoulder
    loft(
        p,
        [root, (*W_S, 0.022, 0.018), (*W_S.lerp(W_E, 0.5), 0.020, 0.016), (*W_E, 0.022, 0.018)],
        5,
        [1, 1, 2],
        "Wing",
        cap="end",
    )
    loft(
        p,
        [(*W_E, 0.020, 0.017), (*W_E.lerp(W_W, 0.5), 0.015, 0.012), (*W_W, 0.018, 0.015)],
        5,
        [1, 2],
        "Wing",
        cap="end",
    )
    loft(
        p,
        [(*W_W, 0.013, 0.011), (*W_K, 0.009, 0.008), (*W_K.lerp(W_T, 0.6), 0.006, 0.005), (*W_T, 0.0, 0.0)],
        4,
        [2, 2, 1],
        "Wing",
    )
    for tip in (W_F2, W_F3):
        loft(p, [(*W_W, 0.011, 0.009), (*W_W.lerp(tip, 0.6), 0.006, 0.005), (*tip, 0.0, 0.0)], 4, [2, 1], "Wing")
    spike(p, W_W + Vector((0.0, -0.005, 0.0)), W_W + Vector((0.020, -0.070, 0.010)), 0.010, "Wing", sides=4)
    return p, sheet


def solid_sheet(sheet, thickness):
    """Close a one-sided sheet into a thin shell (top, bottom and a rim), centred on the sheet."""
    me = bpy.data.meshes.new("sheet")
    me.from_pydata([tuple(v) for v in sheet.verts], [], [f for f, _ in sheet.faces])
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bad = [f for f in bm.faces if f.calc_area() < 1e-10]
    if bad:
        bmesh.ops.delete(bm, geom=bad, context="FACES")
    for f in bm.faces:
        if f.normal.z < 0:
            f.normal_flip()
    bm.normal_update()
    out = Part()
    bm.verts.ensure_lookup_table()
    top = {v.index: out.v(v.co + v.normal * (thickness / 2)) for v in bm.verts}
    bot = {v.index: out.v(v.co - v.normal * (thickness / 2)) for v in bm.verts}
    for f in bm.faces:
        ids = [v.index for v in f.verts]
        out.f([top[i] for i in ids], "Wing")
        out.f([bot[i] for i in reversed(ids)], "Wing")
    for e in bm.edges:
        if e.is_boundary:
            loop = e.link_loops[0]  # the sheet face runs a -> b along this edge
            a, b = loop.vert.index, loop.link_loop_next.vert.index
            out.f([top[b], top[a], bot[a], bot[b]], "Wing")
    bm.free()
    bpy.data.meshes.remove(me)
    return out


# ---------------------------------------------------------------------------- scene, renders, export


def materials():
    cols = {"Body": (0.20, 0.42, 0.24, 1), "Wing": (0.35, 0.50, 0.30, 1), "Arm": (0.45, 0.35, 0.25, 1)}
    out = []
    for name, c in cols.items():
        m = bpy.data.materials.new(name)
        m.diffuse_color = c
        out.append(m)
    return out


def render(obj, prefix, res):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_cavity = True
    sc.display.shading.show_object_outline = False
    sc.display.shading.show_backface_culling = True  # a flipped face shows as a hole
    sc.render.resolution_x, sc.render.resolution_y = res, int(res * 0.75)
    sc.render.film_transparent = False
    if not sc.world:
        sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.08, 0.08, 0.09)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    pts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    c = (lo + hi) / 2
    size = (hi - lo).length
    views = {
        "threequarter": (Vector((-1.0, -1.2, 0.8)), size * 0.80, c),
        "front": (Vector((0, -1, 0.05)), size * 0.75, c),
        "side": (Vector((1, 0, 0.02)), size * 0.62, c),
        "top": (Vector((0, -0.001, 1)), size * 0.80, c),
        "head": (Vector((-0.9, -1.0, 0.35)), size * 0.20, Vector((0.0, -0.45, -0.07)) * UNIT),
        "leg": (Vector((1.0, -0.6, 0.2)), size * 0.35, Vector((0.10, 0.15, -0.35)) * UNIT),
        "shoulder": (Vector((-0.9, -1.0, 0.35)), size * 0.15, Vector((-0.12, -0.12, 0.02)) * UNIT),
    }
    out = []
    for name, (d, scale, aim) in views.items():
        d = d.normalized()
        cam.location = aim + d * size * 2
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
        cam.data.clip_end = size * 10
        sc.render.filepath = f"{prefix}_{name}.png"
        bpy.ops.render.render(write_still=True)
        out.append(sc.render.filepath)
    return out


def main():
    a = parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    mats = materials()

    body, body_side = build_body()
    leg = build_front_leg()
    wing, sheet = build_wing()
    shell = solid_sheet(sheet, 0.006)
    right = merged(body_side, leg, wing, shell)
    whole = merged(body, right, mirror_part(right))
    for name, part in (
        ("body", body),
        ("head details + hind leg", body_side),
        ("front leg", leg),
        ("wing bones", wing),
        ("membrane", shell),
    ):
        tris = sum(len(f) - 2 for f, _ in part.faces)
        print(f"part {name}: {tris} triangles (one side)" if part is not body else f"part {name}: {tris} triangles")
    obj = to_object(whole, "BossBladeDragon", mats)
    obj.scale = (UNIT, UNIT, UNIT)
    bpy.context.view_layer.update()
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for poly in obj.data.polygons:
        poly.use_smooth = True

    vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
    lo = [min(v[k] for v in vs) / UNIT for k in range(3)]
    hi = [max(v[k] for v in vs) / UNIT for k in range(3)]
    print("box (PC units):", [round(x, 3) for x in lo], [round(x, 3) for x in hi])

    summary = export_authored(
        obj,
        a.authored,
        model="bladedragBOSS(M)",
        label="Boss Blade Dragon",
        fit="extent",
        slots={"Body": {"pcTextures": [0, 2, 3]}, "Wing": {"pcTextures": [1]}, "Arm": {"pcTextures": [1]}},
    )
    print("summary:", summary)
    bpy.ops.wm.save_as_mainfile(filepath=str(out / "boss_blade_dragon.blend"))
    if not a.no_render:
        for f in render(obj, str(out / "clay"), a.res):
            print("render:", f)


if __name__ == "__main__":
    main()
