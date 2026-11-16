"""Build an original Death Mage (Order of the Flame hooded wraith) in Blender, headless, for the converter.

The 1999 PC models `deathmage(h)`, `deathmage ghost(h)` (the same mesh with a pale skin) and `deathmageshaman(h)2` (the
same mesh, about 5 mm further back, with a chest plate and bracers) in Common/NPC/NPCs are replaced at convert time by
this mesh: the converter fits it on the PC skeleton and takes UVs, textures (the 1999 bandage and skull skins, all
cut-out textures) and skin weights from the player's own disc (editor/packages/formats/src/ootfauthored.ts). This script
reads no game data: the hooded skull, the wrapped robe tapering into a legless tail, bell sleeves, bare arm bones and
clawed hands, and the ribbons hanging from the arms and the hem are built from rounded proportions measured on the
reference (joint positions, heights and widths), merged per part with a voxel remesh and decimated to a triangle budget.

    blender -b --factory-startup -P analysis/scripts/blender/deathmage_model.py -- \
        --out build/authored/deathmage-model [--which mage|shaman|ghost|all] \
        [--authored scripts/buildtool/kit/models] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the mage's left, y toward the back, z up; the mage faces -Y),
so the `landmarks` are the PC joint positions and the converter's joint fit is the identity. The shaman's 1999 skeleton
does not sit on its mesh, so its file is placed by height instead. Outputs under --out (gitignored): deathmage_<which>.blend
and clay renders render_<which>_<view>.png.
"""

import argparse
import itertools
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

# ---------------------------------------------------------------------------- args


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/authored/deathmage-model")
    ap.add_argument("--which", default="all", choices=["mage", "shaman", "ghost", "all"])
    ap.add_argument("--authored", help="folder to write the authored models into (deathmage*.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=900, help="render height in pixels")
    return ap.parse_args(argv)


ARGS = parse_args()
OUT = Path(ARGS.out).resolve()
OUT.mkdir(parents=True, exist_ok=True)

V = Vector
UP = Vector((0, 0, 1))

# ---------------------------------------------------------------------------- reference proportions
# Joint positions measured on the reference rig (rounded), Blender axes. The two arms are not quite mirror images, so
# each side keeps its own.
JOINTS = {
    "head": (-0.001, 0.005, 0.102),
    "upperback": (-0.001, 0.008, 0.028),
    "waist": (-0.001, 0.008, -0.024),
    "LArm": (-0.047, 0.013, 0.091),
    "LForearm": (-0.109, 0.008, 0.063),
    "Lwrist": (-0.160, 0.003, 0.042),
    "LHand": (-0.180, 0.003, 0.031),
    "LFingers": (-0.195, 0.003, 0.023),
    "LThumb": (-0.170, -0.006, 0.036),
    "RArm": (0.039, 0.009, 0.089),
    "Rforearm": (0.103, 0.007, 0.063),
    "RWrist": (0.151, 0.003, 0.043),
    "RHand": (0.170, 0.003, 0.032),
    "RFingers": (0.184, 0.003, 0.025),
    "RThumb": (0.162, -0.006, 0.037),
    "Dangletip": (-0.001, -0.001, -0.120),
    "Tailtip": (-0.001, 0.039, -0.162),
}
LANDMARKS = ["head", "upperback", "waist", "LArm", "LForearm", "Lwrist", "RArm", "Rforearm", "RWrist", "Tailtip"]
# per side: shoulder, elbow, wrist, hand, finger tip joints and how far the claws reach (1999 vertex extent)
SIDES = {
    "L": ("LArm", "LForearm", "Lwrist", "LHand", "LFingers", "LThumb", -0.211),
    "R": ("RArm", "Rforearm", "RWrist", "RHand", "RFingers", "RThumb", 0.205),
}

# Torso and robe sections, top to bottom: (z, centre y, half width, half depth). The 1999 robe is narrow (about 9 cm
# across the chest, 4 cm deep, in its units) and tapers to a point under the waist; the hood rises to 0.151.
TORSO = [
    (0.114, 0.004, 0.011, 0.011),
    (0.106, 0.004, 0.022, 0.017),
    (0.097, 0.004, 0.040, 0.020),
    (0.087, 0.004, 0.048, 0.022),
    (0.072, 0.003, 0.047, 0.022),
    (0.052, 0.002, 0.043, 0.021),
    (0.030, 0.003, 0.037, 0.019),
    (0.008, 0.004, 0.031, 0.018),
    (-0.012, 0.005, 0.029, 0.018),
    (-0.040, 0.007, 0.028, 0.018),
    (-0.068, 0.010, 0.023, 0.016),
    (-0.095, 0.013, 0.016, 0.012),
    (-0.118, 0.016, 0.009, 0.008),
    (-0.135, 0.018, 0.004, 0.004),
    (-0.145, 0.019, 0.0012, 0.0012),
]

# triangle budgets per part (the whole model aims at about 2,500 triangles)
BUDGET = {"body": 820, "hood": 380, "skull": 520, "armour": 280}

# ---------------------------------------------------------------------------- helpers


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_from_bm(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me))


def apply_modifiers(obj):
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
        for m in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=m.name)


def join(objs, name):
    objs = [o for o in objs if o]
    with bpy.context.temp_override(
        object=objs[0], active_object=objs[0], selected_objects=objs, selected_editable_objects=objs
    ):
        bpy.ops.object.join()
    objs[0].name = name
    return objs[0]


def remove(obj):
    bpy.data.objects.remove(obj, do_unlink=True)


def catmull(ctrl, per=8):
    """Dense samples through control tuples (any number of floats each) with Catmull-Rom interpolation."""
    n = len(ctrl)
    out = []
    for i in range(n - 1):
        p0 = ctrl[max(i - 1, 0)]
        p1 = ctrl[i]
        p2 = ctrl[i + 1]
        p3 = ctrl[min(i + 2, n - 1)]
        for k in range(per):
            t = k / per
            t2, t3 = t * t, t * t * t
            out.append(
                tuple(
                    0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3)
                    for a, b, c, d in zip(p0, p1, p2, p3)
                )
            )
    out.append(tuple(ctrl[-1]))
    return out


def loft(name, ctrl, sides=12, per=6, up=UP, tip=False):
    """Closed tube through control tuples (x, y, z, half width, half height); `tip` ends it in a point."""
    samples = catmull(ctrl, per)
    pts = [V(s[:3]) for s in samples]
    bm = bmesh.new()
    rings = []
    for i, s in enumerate(samples):
        p = pts[i]
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side = t.cross(up)
        if side.length < 1e-4:
            side = t.cross(V((1, 0, 0)))
        side.normalize()
        u = side.cross(t).normalized()
        rw, rh = max(s[3], 1e-5), max(s[4], 1e-5)
        rings.append([bm.verts.new(p + side * (math.cos(a) * rw) + u * (math.sin(a) * rh)) for a in _angles(sides)])
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bm.faces.new(list(reversed(rings[0])))
    if tip:
        apex = bm.verts.new(pts[-1] + (pts[-1] - pts[-2]).normalized() * 0.002)
        for k in range(sides):
            bm.faces.new((rings[-1][k], rings[-1][(k + 1) % sides], apex))
    else:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def vloft(name, sections, sides=24, per=4, sq=1.0):
    """Closed vertical body through (z, centre y, half width, half depth) sections, top to bottom, the last a point."""
    samples = catmull(sections, per)
    bm = bmesh.new()
    rings = []
    for z, cy, rx, ry in samples:
        ring = []
        for a in _angles(sides):
            c, s = math.cos(a), math.sin(a)
            c = math.copysign(abs(c) ** sq, c)
            s = math.copysign(abs(s) ** sq, s)
            ring.append(bm.verts.new((c * max(rx, 1e-5), cy + s * max(ry, 1e-5), z)))
        rings.append(ring)
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def torso_at(z):
    """Interpolated torso section (centre y, half width, half depth) at height z."""
    for a, b in itertools.pairwise(TORSO):
        if b[0] <= z <= a[0]:
            f = (a[0] - z) / (a[0] - b[0])
            return tuple(a[k] + (b[k] - a[k]) * f for k in (1, 2, 3))
    s = TORSO[0] if z > TORSO[0][0] else TORSO[-1]
    return s[1], s[2], s[3]


def _angles(n):
    return [2 * math.pi * k / n for k in range(n)]


def ellipsoid(name, c, r, seg=20):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=seg // 2 + 1, radius=1.0)
    for v in bm.verts:
        v.co = V((c[0] + v.co.x * r[0], c[1] + v.co.y * r[1], c[2] + v.co.z * r[2]))
    return mesh_from_bm(name, bm)


def ring_tube(name, path, radius, sides=6):
    """Closed ring (a wrap or a hem band) along a closed path of points."""
    bm = bmesh.new()
    n = len(path)
    rings = []
    for i, p in enumerate(path):
        t = (path[(i + 1) % n] - path[i - 1]).normalized()
        ref = UP if abs(t.dot(UP)) < 0.9 else V((1, 0, 0))
        s = t.cross(ref).normalized()
        u = s.cross(t).normalized()
        rings.append([bm.verts.new(p + (s * math.cos(a) + u * math.sin(a)) * radius) for a in _angles(sides)])
    for i in range(n):
        r0, r1 = rings[i], rings[(i + 1) % n]
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def ribbon(name, ctrl, normal, width, thick=0.0012, per=1):
    """A hanging cloth strip: diamond section along control points (x, y, z, half width scale), tapering to a point.
    `normal` (a vector or a function of the point) is the side the strip faces; its width lies across it."""
    samples = catmull(ctrl, per)
    pts = [V(s[:3]) for s in samples]
    bm = bmesh.new()
    rings = []
    for i, s in enumerate(samples):
        p = pts[i]
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        n = normal(p) if callable(normal) else V(normal)
        side = t.cross(n).normalized()
        nn = side.cross(t).normalized()
        w = width * s[3]
        if i == len(samples) - 1:
            rings.append([bm.verts.new(p)])
            continue
        rings.append(
            [
                bm.verts.new(p + side * w),
                bm.verts.new(p + nn * thick),
                bm.verts.new(p - side * w),
                bm.verts.new(p - nn * thick),
            ]
        )
    for r0, r1 in itertools.pairwise(rings):
        for k in range(4):
            k1 = (k + 1) % 4
            if len(r1) == 1:
                bm.faces.new((r0[k], r0[k1], r1[0]))
            else:
                bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bm.faces.new(list(reversed(rings[0])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def remesh_decimate(obj, voxel, tris, smooth=3):
    rm = obj.modifiers.new("remesh", "REMESH")
    rm.mode = "VOXEL"
    rm.voxel_size = voxel
    rm.adaptivity = 0.0
    apply_modifiers(obj)
    if smooth:
        sm = obj.modifiers.new("smooth", "SMOOTH")
        sm.factor = 0.5
        sm.iterations = smooth
        apply_modifiers(obj)
    decimate(obj, tris)
    return obj


def decimate(obj, tris):
    obj.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(obj)
    n = len(obj.data.polygons)
    if n > tris:
        dec = obj.modifiers.new("dec", "DECIMATE")
        dec.decimate_type = "COLLAPSE"
        dec.ratio = tris / n
        dec.use_collapse_triangulate = True
        apply_modifiers(obj)
    return obj


def boolean(obj, cutter, op="DIFFERENCE"):
    m = obj.modifiers.new("bool", "BOOLEAN")
    m.operation = op
    m.object = cutter
    m.solver = "EXACT"
    apply_modifiers(obj)
    remove(cutter)
    return obj


def set_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.material_index = 0


def outward(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(obj.data)
    bm.free()


# ---------------------------------------------------------------------------- materials (slots)

SLOT_COLOURS = {
    "Robe": (0.10, 0.08, 0.07),
    "Hood": (0.07, 0.06, 0.06),
    "Skull": (0.45, 0.40, 0.30),
    "Bone": (0.40, 0.36, 0.28),
    "Tatter": (0.14, 0.11, 0.09),
    "Armour": (0.03, 0.04, 0.05),
}


def make_mats():
    mats = {}
    for name, col in SLOT_COLOURS.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.7
        mats[name] = m
    return mats


# ---------------------------------------------------------------------------- parts


def arm_points(side):
    sh, el, wr, hand, fing, thumb, reach = SIDES[side]
    return V(JOINTS[sh]), V(JOINTS[el]), V(JOINTS[wr]), V(JOINTS[hand]), V(JOINTS[fing]), V(JOINTS[thumb]), reach


def sleeve(side):
    """Upper sleeve from inside the shoulder to a bell cuff past the elbow; returns the tube and the cuff centre."""
    sh, el, wr, *_ = arm_points(side)
    sx = 1 if sh.x > 0 else -1
    root = V((0.030 * sx, 0.006, 0.090))
    cuff = el.lerp(wr, 0.05)
    ctrl = [
        (*root, 0.016, 0.017),
        (*sh.lerp(el, 0.15), 0.0145, 0.0150),
        (*sh.lerp(el, 0.55), 0.0120, 0.0125),
        (*el, 0.0120, 0.0125),
        (*el.lerp(cuff, 0.6), 0.0132, 0.0138),
        (*cuff, 0.0145, 0.0150),
    ]
    tube = loft("sleeve", ctrl, sides=14, per=4)
    # a hem band around the cuff and two wraps around the upper arm
    d = (cuff - el).normalized()
    parts = [tube, _band(cuff - d * 0.0015, d, 0.0147, 0.0016)]
    for f, r in ((0.35, 0.0140), (0.75, 0.0125)):
        c = sh.lerp(el, f)
        dd = (el - sh).normalized()
        tilt = (dd + V((0, 0, 0.35 * (1 if f < 0.5 else -1)))).normalized()
        parts.append(_band(c, tilt, r * 1.03, 0.0013))
    return join(parts, "sleeve"), cuff


def _band(c, axis, r, thick, n=14):
    s = axis.cross(UP if abs(axis.dot(UP)) < 0.9 else V((1, 0, 0))).normalized()
    u = s.cross(axis).normalized()
    path = [c + (s * math.cos(a) + u * math.sin(a)) * r for a in _angles(n)]
    return ring_tube("band", path, thick, sides=5)


def torso_wraps():
    """Bandage wraps around the robe: tilted closed bands that follow the torso surface."""
    parts = []
    for z0, tilt, grow in (
        (0.078, 0.10, 1.04),
        (0.060, -0.18, 1.05),
        (0.040, 0.22, 1.05),
        (0.018, -0.12, 1.05),
        (-0.005, 0.20, 1.06),
        (-0.030, -0.16, 1.06),
        (-0.058, 0.18, 1.06),
        (-0.088, -0.20, 1.07),
        (-0.115, 0.15, 1.08),
    ):
        path = []
        for a in _angles(28):
            x0 = math.cos(a)
            z = z0 + tilt * x0 * 0.03
            cy, rx, ry = torso_at(z)
            path.append(V((x0 * rx * grow, cy + math.sin(a) * ry * grow, z)))
        parts.append(ring_tube("wrap", path, 0.0016, sides=5))
    return parts


def build_body(shaman=False):
    """Torso and robe, the cowl collar, both sleeves and the wraps, merged in one remesh."""
    parts = [vloft("torso", TORSO, sides=24, per=3, sq=0.9)]
    # cowl folds at the collar, so the hood drapes onto the shoulders
    parts.append(ellipsoid("cowl", (0.0, 0.002, 0.104), (0.026, 0.022, 0.010)))
    parts.append(ellipsoid("cowlback", (0.0, 0.016, 0.110), (0.020, 0.012, 0.014)))
    # shoulder blades and the shoulder caps the sleeves hang from
    for sx in (1, -1):
        parts.append(ellipsoid("cap", (0.040 * sx, 0.006, 0.090), (0.016, 0.017, 0.010)))
        parts.append(ellipsoid("blade", (0.020 * sx, 0.018, 0.070), (0.016, 0.008, 0.020)))
    cuffs = {}
    for side in SIDES:
        s, cuff = sleeve(side)
        parts.append(s)
        cuffs[side] = cuff
    parts += torso_wraps()
    body = join(parts, "body")
    remesh_decimate(body, 0.0014, BUDGET["body"], smooth=2)
    return body, cuffs


def build_hood():
    """A deep cowl with a pointed crown drooping back, its sides falling onto the collar, and a recess cut in front in
    which the skull sits (the brim overhangs the brow)."""
    outer = [
        ellipsoid("hood", (0.0, 0.002, 0.1285), (0.0215, 0.0265, 0.0235), seg=32),
        loft(
            "crown",
            [
                (0.0, 0.006, 0.142, 0.016, 0.010),
                (0.0, 0.020, 0.146, 0.010, 0.008),
                (0.0, 0.032, 0.138, 0.005, 0.005),
                (0.0, 0.040, 0.124, 0.0015, 0.0015),
            ],
            sides=12,
            per=4,
            tip=True,
        ),
        loft(
            "drape",
            [
                (0.0, 0.004, 0.122, 0.021, 0.025),
                (0.0, 0.005, 0.111, 0.024, 0.024),
                (0.0, 0.005, 0.102, 0.027, 0.021),
            ],
            sides=16,
            per=3,
        ),
    ]
    hood = join(outer, "hood")
    remesh_decimate(hood, 0.0008, 100000, smooth=2)
    boolean(hood, ellipsoid("recess", (0.0, -0.036, 0.1260), (0.0155, 0.0265, 0.0230), seg=40))
    remesh_decimate(hood, 0.0006, BUDGET["hood"], smooth=1)
    return hood


def build_skull():
    """The face inside the hood: cranium, a broad smooth face, upper jaw and an open mandible, shallow eye sockets and
    the nose cut in. Proportions follow where the 1999 skin paints the skull (eyes high, wide cheeks); the face is kept
    smooth so the painted skull projects onto it in one piece."""
    parts = [
        ellipsoid("cranium", (0.0, -0.004, 0.1345), (0.0130, 0.0160, 0.0120), seg=28),
        ellipsoid("maxilla", (0.0, -0.0155, 0.1235), (0.0090, 0.0080, 0.0060)),
        ellipsoid("face", (0.0, -0.0120, 0.1300), (0.0118, 0.0110, 0.0110)),
    ]
    # upper teeth and the open mandible with its lower teeth
    upper = [V((sx * 0.0075, -0.0160, 0.1205)) for sx in (-1,)] + [V((0, -0.0238, 0.1205))]
    upper += [V((0.0075, -0.0160, 0.1205))]
    parts.append(loft("uteeth", [(*p, 0.0016, 0.0022) for p in catmull([tuple(p) for p in upper], 3)], sides=8))
    jaw = [
        (-0.0110, -0.0040, 0.1250),
        (-0.0098, -0.0060, 0.1140),
        (-0.0055, -0.0185, 0.1115),
        (0.0, -0.0225, 0.1110),
        (0.0055, -0.0185, 0.1115),
        (0.0098, -0.0060, 0.1140),
        (0.0110, -0.0040, 0.1250),
    ]
    parts.append(loft("mandible", [(*p, 0.0019, 0.0028) for p in jaw], sides=8, per=3))
    lower = [(-0.0068, -0.0165, 0.1168), (0.0, -0.0225, 0.1165), (0.0068, -0.0165, 0.1168)]
    parts.append(loft("lteeth", [(*p, 0.0014, 0.0018) for p in lower], sides=8, per=3))
    skull = join(parts, "skull")
    remesh_decimate(skull, 0.00045, 100000, smooth=1)
    for sx in (1, -1):
        boolean(skull, ellipsoid("socket", (0.0053 * sx, -0.0262, 0.1333), (0.0040, 0.0045, 0.0038)))
    boolean(skull, ellipsoid("nose", (0.0, -0.0262, 0.1287), (0.0016, 0.0040, 0.0027)))
    decimate(skull, BUDGET["skull"])
    return skull


def build_arm(side, cuff, style="bone"):
    """Forearm from the cuff to the wrist (two bare bones, or a bracer for the shaman), a bony hand and four long
    clawed fingers and a thumb, lying in the 1999 hand plane (palm down, fingers spread front to back)."""
    sh, el, wr, hand, fing, thumb, reach = arm_points(side)
    d = (wr - el).normalized()
    across = V((0, 1, 0))
    parts = []
    if style == "bone":
        for oy, r in ((-0.0024, 0.0021), (0.0024, 0.0019)):
            o = across * oy
            parts.append(
                loft(
                    "bone",
                    [(*(cuff + o - d * 0.004), r * 1.2, r * 1.2), (*(cuff.lerp(wr, 0.5) + o), r, r), (*(wr + o), r, r)],
                    sides=5,
                    per=1,
                )
            )
    else:
        parts.append(
            loft(
                "bracer",
                [(*(cuff - d * 0.004), 0.0062, 0.0062), (*cuff.lerp(wr, 0.5), 0.0056, 0.0056), (*wr, 0.0050, 0.0050)],
                sides=10,
                per=2,
            )
        )
    parts.append(ellipsoid("wrist", tuple(wr), (0.0040, 0.0046, 0.0032), seg=8))
    # palm: a flat block from the wrist to the knuckles
    knuck = wr.lerp(hand, 0.85)
    parts.append(loft("palm", [(*wr, 0.0046, 0.0024), (*knuck, 0.0056, 0.0020)], sides=8, per=1))
    # fingers: from the knuckles toward the 1999 finger tips, bending down at each joint, ending in a claw point
    tipd = (fing - hand).normalized()
    reach_len = abs(reach - knuck.x) / max(abs(tipd.x), 0.3)
    for k, oy in enumerate((-0.0048, -0.0016, 0.0016, 0.0046)):
        spread = across * (oy * 1.6)
        length = reach_len * (0.86 if k in (0, 3) else 1.0)
        base = knuck + across * oy
        p1 = base + tipd * (length * 0.36) + spread * 0.4
        p2 = p1 + (tipd - UP * 0.25).normalized() * (length * 0.32) + spread * 0.3
        p3 = p2 + (tipd - UP * 0.75).normalized() * (length * 0.36) + spread * 0.3
        parts.append(
            loft(
                "finger",
                [(*base, 0.0013, 0.0013), (*p1, 0.0012, 0.0011), (*p2, 0.0010, 0.0009), (*p3, 0.0004, 0.0004)],
                sides=4,
                per=1,
                tip=True,
            )
        )
    tb = wr.lerp(knuck, 0.4) + V((0, -0.004, 0))
    tm = tb + (tipd - across * 0.5).normalized() * 0.008 + V((0, 0, -0.001))
    tt = tm + (tipd - UP * 0.6 - across * 0.2).normalized() * 0.009
    parts.append(
        loft("thumb", [(*tb, 0.0015, 0.0015), (*tm, 0.0012, 0.0012), (*tt, 0.0004, 0.0004)], sides=4, per=1, tip=True)
    )
    return join(parts, f"arm_{side}")


def _out_from_axis(p):
    """Outward from the robe's axis at p (horizontal)."""
    cy, _rx, _ry = torso_at(p.z)
    v = V((p.x, p.y - cy, 0.0))
    return v.normalized() if v.length > 1e-6 else V((0, -1, 0))


def build_tatters():
    """Strips hanging from the sleeves and the ragged hem: thin tapered ribbons, wavy, of uneven lengths."""
    parts = []
    fwd = V((0, -1, 0))
    # from the sleeves (where the 1999 tatter chains hang), facing forward
    for x, y, z0, z1, w in (
        (-0.069, 0.004, 0.073, 0.010, 0.0070),
        (-0.090, 0.006, 0.066, 0.030, 0.0060),
        (-0.054, 0.006, 0.078, 0.042, 0.0060),
        (0.052, 0.004, 0.077, -0.004, 0.0070),
        (0.100, 0.004, 0.061, 0.018, 0.0065),
        (0.077, 0.007, 0.068, 0.036, 0.0060),
    ):
        n = 5
        ctrl = []
        for k in range(n + 1):
            f = k / n
            wob = 0.0025 * math.sin(f * math.pi * 2.0 + x * 40)
            ctrl.append((x + wob, y + 0.002 * f, z0 + (z1 - z0) * f, 1.0 - 0.6 * f))
        parts.append(ribbon("tatter", ctrl, fwd, w, thick=0.0010))
    # hem strips around the lower robe, following its taper, the longest at the back (the 1999 tail strips)
    hem = [
        (0.20, -0.025, -0.165, 0.0110),
        (0.85, -0.035, -0.195, 0.0105),
        (1.45, -0.025, -0.210, 0.0110),
        (2.05, -0.035, -0.180, 0.0105),
        (2.70, -0.025, -0.205, 0.0110),
        (3.35, -0.035, -0.218, 0.0110),
        (3.95, -0.025, -0.190, 0.0105),
        (4.60, -0.035, -0.212, 0.0110),
        (5.25, -0.025, -0.185, 0.0105),
    ]
    for a, z0, z1, w in hem:
        ctrl = []
        n = 5
        for k in range(n + 1):
            f = k / n
            z = z0 + (z1 - z0) * f
            cy, rx, ry = torso_at(max(z, -0.140))
            grow = 1.05 + 0.30 * f
            aa = a + 0.10 * math.sin(f * 3.0 + a)
            px = math.cos(aa) * max(rx, 0.006) * grow
            py = cy + math.sin(aa) * max(ry, 0.006) * grow
            ctrl.append((px, py, z, 1.0 - 0.55 * f))
        parts.append(ribbon("hem", ctrl, _out_from_axis, w, thick=0.0011))
    # the long trailing strip at the back (1999 Tail chain) and the front panel (Dangle)
    back = [(0.0, 0.020, -0.020, 1.0), (0.0, 0.031, -0.070, 0.9), (0.0, 0.037, -0.115, 0.7), (0.0, 0.040, -0.165, 0.3)]
    parts.append(ribbon("tail", [(x, y, z, s) for x, y, z, s in back], V((0, 1, 0)), 0.0085, thick=0.0012))
    front = [(0.0, -0.021, -0.020, 1.0), (0.0, -0.016, -0.070, 0.85), (0.0, -0.010, -0.125, 0.4)]
    parts.append(ribbon("panel", front, V((0, -1, 0)), 0.0090, thick=0.0012))
    return join(parts, "tatters")


def build_armour():
    """The shaman's chest plate (front, back and shoulder guards) over the robe, where the 1999 shaman paints it."""
    plate = []
    for z, cy, rx, ry in TORSO:
        if 0.024 <= z <= 0.110:
            plate.append((z, cy, rx * 1.10 + 0.002, ry * 1.18 + 0.002))
    plate.insert(0, (0.110, 0.004, 0.026, 0.020))
    plate.append((0.024, 0.003, 0.036, 0.020))
    parts = [vloft("plate", plate, sides=24, per=2, sq=0.75)]
    for sx in (1, -1):
        parts.append(ellipsoid("pauldron", (0.043 * sx, 0.006, 0.094), (0.017, 0.019, 0.009)))
    parts.append(ellipsoid("gorget", (0.0, 0.002, 0.106), (0.024, 0.022, 0.006)))
    a = join(parts, "armour")
    remesh_decimate(a, 0.0012, BUDGET["armour"], smooth=2)
    return a


# ---------------------------------------------------------------------------- build

ORDER = ["Robe", "Hood", "Skull", "Bone", "Tatter", "Armour"]


def build(which):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats()
    shaman = which == "shaman"
    objs = []
    body, cuffs = build_body(shaman)
    set_material(body, mats["Robe"])
    objs.append(body)
    hood = build_hood()
    set_material(hood, mats["Hood"])
    objs.append(hood)
    skull = build_skull()
    set_material(skull, mats["Skull"])
    objs.append(skull)
    for side in SIDES:
        arm = build_arm(side, cuffs[side], "bracer" if shaman else "bone")
        set_material(arm, mats["Bone"])
        objs.append(arm)
    tat = build_tatters()
    set_material(tat, mats["Tatter"])
    objs.append(tat)
    if shaman:
        arm_ = build_armour()
        set_material(arm_, mats["Armour"])
        objs.append(arm_)
    for o in objs:
        outward(o)
        print(f"part {o.name}: {len(o.data.polygons)} faces")
        for p in o.data.polygons:
            p.use_smooth = True
    model = join(objs, f"deathmage_{which}")
    names = [m.name for m in model.data.materials]
    order = [n for n in ORDER if any(x.split(".")[0] == n for x in names)]
    remap = {i: order.index(n.split(".")[0]) for i, n in enumerate(names)}
    idx = [remap[p.material_index] for p in model.data.polygons]
    model.data.materials.clear()
    for n in order:
        model.data.materials.append(mats[n])
    for p, i in zip(model.data.polygons, idx):
        p.material_index = i
    model.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(model)
    return model


def report(model):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + 1
    print(f"{model.name}: {len(me.vertices)} vertices, {len(me.polygons)} triangles, per slot {per}")


# ---------------------------------------------------------------------------- render


def render(model, which):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = ARGS.res
    sc.render.resolution_y = ARGS.res
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.30, 0.32, 0.36)
    for loc, e in (((-2, -3, 3), 300), ((3, -1, 2), 150), ((0, 3, 3), 200), ((0, 0, -3), 60)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.energy, ld.size = e, 3
        lo = link(bpy.data.objects.new("l", ld))
        lo.location = loc
        lo.rotation_euler = (V((0, 0, 0)) - V(loc)).to_track_quat("-Z", "Y").to_euler()
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.type = "ORTHO"
    views = [
        ("front", V((0, 0, -0.03)), V((0, -1, 0.0001)), 0.46),
        ("side", V((0, 0, -0.03)), V((1, 0, 0.0001)), 0.46),
        ("back", V((0, 0, -0.03)), V((0, 1, 0.0001)), 0.46),
        ("persp", V((0, 0, -0.03)), V((-0.7, -1, 0.35)), 0.46),
        ("head", V((0, -0.005, 0.125)), V((-0.5, -1, 0.15)), 0.075),
        ("headfront", V((0, -0.005, 0.125)), V((0, -1, 0.0001)), 0.075),
        ("headside", V((0, -0.005, 0.125)), V((1, 0, 0.0001)), 0.075),
        ("hand", V((-0.17, 0.0, 0.035)), V((-0.3, -1, 0.6)), 0.08),
    ]
    for name, c, d, scale in views:
        d = d.normalized()
        cam.location = c + d * 2
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
        cam.data.clip_start = 0.01
        sc.render.filepath = str(OUT / f"render_{which}_{name}.png")
        bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------------------- export

VARIANTS = {
    # which: (built geometry, PC model, label, file, fit, slots naming PC texture entries)
    "mage": ("mage", "deathmage(h)", "Death Mage", "deathmage.json", "joints", {}),
    "ghost": ("mage", "deathmage ghost(h)", "Death Mage ghost", "deathmage_ghost.json", "joints", {}),
    "shaman": (
        "shaman",
        "deathmageshaman(h)2",
        "Death Mage shaman",
        "deathmage_shaman.json",
        "height",
        {"Armour": [1]},
    ),
}


def export(model, which):
    _geo, pc_model, label, fname, fit, special = VARIANTS[which]
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored

    # every 1999 Death Mage texture is a cut-out (bandage strips): each slot names the entries it may use
    slots = {n: {"pcTextures": special.get(n, [0])} for n in ORDER}
    extra = {"landmarks": {n: list(JOINTS[n]) for n in LANDMARKS}} if fit == "joints" else {}
    export_authored(
        model,
        Path(ARGS.authored).resolve() / fname,
        model=pc_model,
        label=label,
        fit=fit,
        slots=slots,
        extra=extra,
    )


def main():
    which = ["mage", "shaman", "ghost"] if ARGS.which == "all" else [ARGS.which]
    built = {}
    for w in which:
        geo = VARIANTS[w][0]
        if geo not in built:
            model = build(geo)
            report(model)
            bpy.ops.wm.save_as_mainfile(filepath=str(OUT / f"deathmage_{geo}.blend"))
            if not ARGS.no_render:
                render(model, geo)
            built[geo] = True
        else:
            bpy.ops.wm.open_mainfile(filepath=str(OUT / f"deathmage_{geo}.blend"))
            model = bpy.data.objects[f"deathmage_{geo}"]
        if ARGS.authored:
            export(model, w)


main()
