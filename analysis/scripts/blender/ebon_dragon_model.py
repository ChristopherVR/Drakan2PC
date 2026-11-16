"""Build an original low-poly Ebon Dragon (Order of the Flame bat dragon) in Blender, headless, for the converter.

The 1999 PC model `ebondragon` (Common/NPC/NPCs, 384 polygons) is replaced at convert time by this mesh: the converter
fits it on the PC skeleton and takes UVs, textures (the black 1999 skin) and skin weights from the player's own disc
(editor/packages/formats/src/ootfauthored.ts). This script reads no game data: it lofts the body, head, legs and wing
arms from rounded proportions measured on the reference (joint positions, lengths), merges them with a voxel remesh,
decimates each part to a triangle budget and adds wing membranes, finger bones, ears, claws, teeth and the tail blade.

    blender -b --factory-startup -P analysis/scripts/blender/ebon_dragon_model.py -- \
        --out build/authored/ebon-model [--authored scripts/buildtool/kit/models/ebon_dragon.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x right, y toward the tail, z up; the dragon faces -Y), so the
`landmarks` are the PC joint positions and the converter's joint fit is the identity. Outputs under --out (gitignored):
ebon_dragon.blend and clay renders render_<view>.png.
"""

import argparse
import itertools
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector, geometry

# ---------------------------------------------------------------------------- args


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/authored/ebon-model")
    ap.add_argument("--authored", help="write the mesh as an authored model for the converter (.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=900, help="render height in pixels")
    return ap.parse_args(argv)


ARGS = parse_args()
OUT = Path(ARGS.out).resolve()
OUT.mkdir(parents=True, exist_ok=True)

V = Vector
UP = Vector((0, 0, 1))

# ---------------------------------------------------------------------------- reference proportions
# Joint positions measured on the reference rig (rounded), Blender axes: (x, toward the tail, up). Right side (+x);
# the left side mirrors it. They double as the converter landmarks.
JOINTS = {
    "tailbone": (0.0, 0.326, 0.008),
    "tail1": (0.0, 0.68, 0.001),
    "tail2": (0.0, 0.918, -0.029),
    "tail3": (0.0, 1.154, -0.053),
    "tail4": (0.0, 1.389, -0.056),
    "tail5": (0.0, 1.597, -0.04),
    "tailblade": (0.0, 1.789, -0.006),
    "neck2": (0.0, -0.204, -0.013),
    "head": (0.0, -0.345, -0.011),
    "jaw": (0.0, -0.436, -0.05),
    "Rthigh": (0.094, 0.51, -0.029),
    "Rmidleg": (0.094, 0.47, -0.257),
    "Rfoot": (0.094, 0.644, -0.044),
    "Rtoe": (0.12, 0.833, -0.19),
    "Rtoetip": (0.12, 0.971, -0.228),
    "Rwingbase": (0.121, 0.0, 0.0),
    "Rwing2": (0.389, 0.019, -0.029),
    "Rwing3": (0.865, -0.347, -0.08),
    "Rwingtip": (1.39, -0.631, -0.115),
}
LANDMARKS = [
    "tailbone",
    "tail2",
    "tail4",
    "tailblade",
    "neck2",
    "head",
    "Rthigh",
    "Rmidleg",
    "Rtoetip",
    "Rwing2",
    "Rwing3",
    "Rwingtip",
]

# wing: wrist, finger tips (leading finger first), trailing edge points toward the body
WRIST = V((0.865, -0.347, -0.08))
KNUCKLE = V((1.39, -0.625, -0.115))
FINGER_TIPS = [
    V((1.87, -0.607, -0.149)),
    V((1.87, -0.227, -0.141)),
    V((1.671, 0.163, -0.121)),
    V((1.319, 0.437, -0.095)),
    V((0.862, 0.525, -0.068)),
]
# the inner fingers bow out toward the wing tip (where the 1999 skin paints them): (x, y) points between wrist and tip
FINGER_MIDS = [
    [],
    [(1.30, -0.41), (1.607, -0.43)],
    [(1.211, -0.22), (1.45, -0.03)],
    [(1.148, -0.06), (1.285, 0.294)],
    [(0.899, 0.04)],
]
TRAIL = [V((0.636, 0.458, -0.054)), V((0.361, 0.324, -0.045))]
BODY_ATTACH = V((0.095, 0.19, -0.042))
SHOULDER = V((0.09, 0.02, 0.0))
ELBOW = V((0.389, 0.019, -0.029))

# triangle budgets per part (the whole model aims at about 2,500 triangles)
BUDGET = {"body": 800, "head": 380, "leg": 145}

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


def loft(name, ctrl, sides=16, per=8, up=UP, flat=0.0, tip=False, sq=1.0):
    """Closed tube through control tuples (x, y, z, half width, half height). `flat` flattens the underside (0..1).
    `tip`: close the far end in a point instead of a flat cap."""
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
        rw, rh = max(s[3], 1e-4), max(s[4], 1e-4)
        ring = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            c, sn = math.cos(a), math.sin(a)
            if sq != 1.0:
                c = math.copysign(abs(c) ** sq, c)
                sn = math.copysign(abs(sn) ** sq, sn)
            if sn < 0:
                sn *= 1.0 - flat
            ring.append(bm.verts.new(p + side * (c * rw) + u * (sn * rh)))
        rings.append(ring)
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bm.faces.new(list(reversed(rings[0])))
    if tip:
        apex = bm.verts.new(pts[-1] + (pts[-1] - pts[-2]).normalized() * 0.01)
        for k in range(sides):
            bm.faces.new((rings[-1][k], rings[-1][(k + 1) % sides], apex))
    else:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def cone(name, base, tipp, r, sides=6, rings=3, bend=None):
    """Tapered closed spike from `base` (radius r) to `tipp`; `bend` curves it (offset of the middle)."""
    bm = bmesh.new()
    axis = tipp - base
    t = axis.normalized()
    ref = V((0, 0, 1)) if abs(t.z) < 0.9 else V((1, 0, 0))
    s = t.cross(ref).normalized()
    u = s.cross(t).normalized()
    loops = []
    for i in range(rings):
        f = i / rings
        c = base + axis * f
        if bend is not None:
            c = c + bend * (4 * f * (1 - f))
        rr = r * (1 - f) ** 0.8
        loops.append([bm.verts.new(c + (s * math.cos(a) + u * math.sin(a)) * rr) for a in _angles(sides)])
    apex = bm.verts.new(tipp)
    for r0, r1 in itertools.pairwise(loops):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    for k in range(sides):
        bm.faces.new((loops[-1][k], loops[-1][(k + 1) % sides], apex))
    bm.faces.new(list(reversed(loops[0])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def _angles(n):
    return [2 * math.pi * k / n for k in range(n)]


def ellipsoid(name, c, r, seg=24):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=seg // 2 + 1, radius=1.0)
    for v in bm.verts:
        v.co = V((c[0] + v.co.x * r[0], c[1] + v.co.y * r[1], c[2] + v.co.z * r[2]))
    return mesh_from_bm(name, bm)


def remesh_decimate(obj, voxel, tris, smooth=4):
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
    obj.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(obj)
    n = len(obj.data.polygons)
    dec = obj.modifiers.new("dec", "DECIMATE")
    dec.decimate_type = "COLLAPSE"
    dec.ratio = min(1.0, tris / max(n, 1))
    dec.use_collapse_triangulate = True
    apply_modifiers(obj)
    return obj


def mirror_copy(obj, name):
    """Copy of `obj` mirrored to -x, with the winding fixed."""
    me = obj.data.copy()
    me.name = name
    bm = bmesh.new()
    bm.from_mesh(me)
    for v in bm.verts:
        v.co.x = -v.co.x
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me))


def set_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.material_index = 0


# ---------------------------------------------------------------------------- materials (slots)


def make_mats():
    mats = {}
    for name, col in (
        ("Body", (0.05, 0.07, 0.06)),
        ("Wing", (0.10, 0.09, 0.07)),
        ("Teeth", (0.6, 0.55, 0.45)),
        ("Blade", (0.12, 0.14, 0.12)),
        ("Claw", (0.5, 0.5, 0.45)),
    ):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.55
        mats[name] = m
    return mats


# ---------------------------------------------------------------------------- parts


def build_trunk():
    """Neck, torso and tail as one loft; wing arms join it before the remesh."""
    # (x, y, z, half width, half height)
    spine = [
        (0.0, -0.40, -0.012, 0.040, 0.040),
        (0.0, -0.33, -0.010, 0.040, 0.042),
        (0.0, -0.22, -0.006, 0.047, 0.050),
        (0.0, -0.10, 0.018, 0.078, 0.078),
        (0.0, 0.00, 0.040, 0.112, 0.100),
        (0.0, 0.10, 0.040, 0.118, 0.110),
        (0.0, 0.20, 0.014, 0.105, 0.097),
        (0.0, 0.32, -0.012, 0.090, 0.082),
        (0.0, 0.45, -0.006, 0.083, 0.066),
        (0.0, 0.58, 0.000, 0.062, 0.054),
        (0.0, 0.68, 0.000, 0.050, 0.046),
        (0.0, 0.918, -0.029, 0.038, 0.036),
        (0.0, 1.154, -0.053, 0.030, 0.029),
        (0.0, 1.389, -0.056, 0.024, 0.023),
        (0.0, 1.597, -0.040, 0.018, 0.017),
        (0.0, 1.789, -0.006, 0.012, 0.012),
        (0.0, 1.90, 0.010, 0.007, 0.007),
    ]
    trunk = loft("trunk", spine, sides=20, per=10, flat=0.25)
    parts = [trunk]
    # shoulder blades and chest keel, so the torso reads as muscle rather than a tube
    for sx in (1, -1):
        parts.append(ellipsoid("blade", (0.07 * sx, 0.04, 0.085), (0.06, 0.09, 0.045)))
        parts.append(ellipsoid("pec", (0.06 * sx, -0.03, -0.04), (0.06, 0.07, 0.04)))
    parts.append(ellipsoid("keel", (0.0, 0.02, -0.055), (0.05, 0.12, 0.035)))
    for sx in (1, -1):
        arm = [
            (0.06 * sx, 0.01, 0.03, 0.045, 0.045),
            (0.20 * sx, 0.012, 0.005, 0.034, 0.032),
            (ELBOW.x * sx, ELBOW.y, ELBOW.z, 0.027, 0.026),
            (0.52 * sx, -0.09, -0.045, 0.020, 0.020),
            (0.70 * sx, -0.21, -0.062, 0.018, 0.018),
            (WRIST.x * sx, WRIST.y, WRIST.z, 0.026, 0.024),
            (0.90 * sx, -0.37, -0.083, 0.020, 0.018),
        ]
        parts.append(loft("arm", arm, sides=10, per=6))
        parts.append(ellipsoid("elbow", (ELBOW.x * sx, ELBOW.y + 0.005, ELBOW.z), (0.022, 0.024, 0.022)))
    return join(parts, "trunk")


def section(y, top, bottom, half, x=0.0):
    """Loft control tuple from a section's top, bottom and half width."""
    return (x, y, (top + bottom) / 2, half, (top - bottom) / 2)


def build_head():
    """Wedge skull with brow ridges and a crest, an open lower jaw with wide hinges and a mouth lining, remeshed."""
    skull = loft(
        "skull",
        [
            section(-0.630, -0.012, -0.028, 0.016),
            section(-0.605, -0.002, -0.028, 0.026),
            section(-0.560, 0.017, -0.027, 0.042),
            section(-0.510, 0.031, -0.028, 0.050),
            section(-0.460, 0.040, -0.026, 0.053),
            section(-0.400, 0.036, -0.032, 0.050),
            section(-0.340, 0.030, -0.050, 0.044),
        ],
        sides=20,
        per=8,
        sq=0.85,
    )
    jaw = loft(
        "jaw",
        [
            section(-0.620, -0.053, -0.067, 0.016),
            section(-0.585, -0.055, -0.071, 0.025),
            section(-0.520, -0.054, -0.074, 0.035),
            section(-0.460, -0.048, -0.076, 0.045),
            section(-0.410, -0.036, -0.080, 0.058),
            section(-0.360, -0.022, -0.066, 0.048),
        ],
        sides=16,
        per=6,
        sq=0.8,
    )
    lining = loft(
        "lining",
        [
            section(-0.600, -0.034, -0.054, 0.011),
            section(-0.520, -0.032, -0.056, 0.022),
            section(-0.440, -0.026, -0.050, 0.036),
        ],
        sides=12,
        per=4,
    )
    parts = [skull, jaw, lining]
    for sx in (1, -1):
        parts.append(ellipsoid("brow", (0.036 * sx, -0.50, 0.030), (0.020, 0.040, 0.010)))
        parts.append(ellipsoid("nostril", (0.011 * sx, -0.616, -0.008), (0.007, 0.010, 0.006)))
    parts.append(ellipsoid("crest", (0.0, -0.455, 0.040), (0.010, 0.055, 0.012)))
    return join(parts, "head")


def build_leg(sx):
    """Right leg (sx=1) or its mirror: haunch, thigh, knee, the shin rising to the ankle, the foot, three toes."""
    x = 0.095 * sx
    leg = [
        (x * 0.85, 0.47, 0.000, 0.060, 0.058),
        (x, 0.50, -0.090, 0.050, 0.048),
        (x, 0.48, -0.200, 0.034, 0.032),
        (x, 0.47, -0.257, 0.028, 0.027),
        (x, 0.545, -0.170, 0.022, 0.021),
        (x, 0.644, -0.050, 0.022, 0.021),
        (x * 1.1, 0.74, -0.120, 0.018, 0.017),
        (x * 1.25, 0.833, -0.190, 0.020, 0.016),
    ]
    parts = [loft("leg", leg, sides=12, per=6)]
    parts.append(ellipsoid("knee", (x, 0.465, -0.252), (0.03, 0.03, 0.03)))
    parts.append(ellipsoid("heel", (x, 0.648, -0.042), (0.024, 0.024, 0.024)))
    toes = []
    base = V((x * 1.25, 0.833, -0.19))
    for dx, ty, tz in ((-0.025, 0.955, -0.226), (0.0, 0.975, -0.228), (0.03, 0.945, -0.215)):
        tip = V((base.x + dx * sx, ty, tz))
        mid = base.lerp(tip, 0.5) + V((0, 0, 0.012))
        parts.append(
            loft(
                "toe",
                [(*base, 0.012, 0.011), (*mid, 0.010, 0.009), (*tip, 0.008, 0.007)],
                sides=8,
                per=3,
            )
        )
        toes.append((tip, (tip - base).normalized()))
    return join(parts, "leg"), toes


def finger_path(j, n=10):
    """Dense points along inner finger j (1..4): wrist, bow points, tip; heights interpolated by length."""
    tip = FINGER_TIPS[j]
    pts2 = [WRIST.xy] + [V(m) for m in FINGER_MIDS[j]] + [tip.xy]
    dense = [V(p) for p in catmull([tuple(p) for p in pts2], per=n // (len(pts2) - 1) + 1)]
    lens = [0.0]
    for a, b in itertools.pairwise(dense):
        lens.append(lens[-1] + (b - a).length)
    return [V((p.x, p.y, WRIST.z + (tip.z - WRIST.z) * (L / lens[-1]))) for p, L in zip(dense, lens)]


def membrane(mats):
    """Right wing membrane: constrained Delaunay of the outline with the fingers as edges, heights through the bones."""
    # leading finger, then scalloped trailing edges between the tips, then along the body back to the arm
    chain = list(FINGER_TIPS) + list(TRAIL) + [BODY_ATTACH]
    pts = [WRIST]
    pts += [KNUCKLE.lerp(FINGER_TIPS[0], f) for f in (0.0, 0.5)]
    for a, b in itertools.pairwise(chain):
        n = 6 if (a - b).length > 0.3 else 3
        for k in range(n):
            f = k / n
            p = a.lerp(b, f)
            # scallop toward the wrist, deepest between the tips
            sc = 0.10 if b in FINGER_TIPS or a in FINGER_TIPS else 0.05
            p = p + (WRIST - p).normalized() * ((a - b).length * sc * math.sin(math.pi * f))
            pts.append(p)
    pts.append(BODY_ATTACH)
    pts += [BODY_ATTACH.lerp(SHOULDER, f) for f in (0.5,)] + [SHOULDER]
    pts += [SHOULDER.lerp(ELBOW, f) for f in (0.5,)] + [ELBOW]
    pts += [ELBOW.lerp(WRIST, f) for f in (0.33, 0.66)]
    # height control: every outline point and points along the fingers
    ctrl = [p.copy() for p in pts]
    bones = [[WRIST, KNUCKLE, FINGER_TIPS[0]]] + [finger_path(j) for j in range(1, len(FINGER_TIPS))]
    for b in bones:
        for a, c in itertools.pairwise(b):
            for k in range(1, 8):
                ctrl.append(a.lerp(c, k / 8))
    verts2 = [(p.x, p.y) for p in pts]
    nout = len(verts2)
    face = list(range(nout))
    cons = []
    # finger edges: wrist to tip (inner fingers)
    tip_index = {}
    for i, p in enumerate(pts):
        for j, t in enumerate(FINGER_TIPS):
            if (p - t).length < 1e-6:
                tip_index[j] = i
    for j in range(1, len(FINGER_TIPS)):
        path = finger_path(j, 4)
        prev = 0
        for q in path[1:-1]:
            verts2.append(tuple(q.xy))
            cons.append((prev, len(verts2) - 1))
            prev = len(verts2) - 1
        cons.append((prev, tip_index[j]))
    # interior fill: a jittered grid
    xs = [p[0] for p in verts2]
    ys = [p[1] for p in verts2]
    step = 0.15
    poly2 = [V((p[0], p[1], 0)) for p in verts2[:nout]]
    yy = min(ys)
    row = 0
    while yy < max(ys):
        xx = min(xs) + (step * 0.5 if row % 2 else 0.0)
        while xx < max(xs):
            q = V((xx, yy, 0))
            if _inside(q, poly2) and _clear(q, verts2, step * 0.45):
                verts2.append((xx, yy))
            xx += step
        yy += step * 0.87
        row += 1
    out = geometry.delaunay_2d_cdt(
        [V(p) for p in verts2], [(i, (i + 1) % nout) for i in range(nout)] + cons, [face], 1, 1e-6
    )
    ov, _oe, of = out[0], out[1], out[2]
    bm = bmesh.new()
    bv = []
    for p in ov:
        z = _idw(p, ctrl)
        bv.append(bm.verts.new((p.x, p.y, z)))
    for f in of:
        try:
            bm.faces.new([bv[i] for i in f])
        except ValueError:
            pass
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for f in bm.faces:
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
    obj = mesh_from_bm("membrane", bm)
    set_material(obj, mats["Wing"])
    return obj


def _inside(q, poly):
    c = False
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if (a.y > q.y) != (b.y > q.y):
            x = a.x + (q.y - a.y) / (b.y - a.y) * (b.x - a.x)
            if x > q.x:
                c = not c
    return c


def _clear(q, pts, d):
    return all((q.x - p[0]) ** 2 + (q.y - p[1]) ** 2 > d * d for p in pts)


def _idw(p, ctrl):
    num = den = 0.0
    for c in ctrl:
        d2 = (p.x - c.x) ** 2 + (p.y - c.y) ** 2
        if d2 < 1e-10:
            return c.z
        w = 1.0 / (d2 * d2)
        num += w * c.z
        den += w
    return num / den


def propatagium(mats):
    """Right fore-membrane between the neck and the elbow, ahead of the upper arm."""
    outline = [
        V((0.045, -0.205, -0.008)),
        V((0.14, -0.15, -0.025)),
        V((0.26, -0.085, -0.035)),
        V((0.385, -0.02, -0.042)),
        V((0.30, 0.0, -0.025)),
        V((0.20, -0.01, -0.012)),
        V((0.10, -0.03, -0.01)),
        V((0.05, -0.12, -0.008)),
    ]
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in outline]
    bm.faces.new(vs)
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method="BEAUTY", ngon_method="BEAUTY")
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if not e.is_boundary], cuts=1)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for f in bm.faces:
        f.normal_update()
        if f.normal.z < 0:
            f.normal_flip()
    o = mesh_from_bm("propatagium", bm)
    set_material(o, mats["Wing"])
    return o


def finger_bones(mats):
    """Right wing finger bones (thin tubes lying in the membrane) and the two thumb claws."""
    parts = []
    lead = [
        (*WRIST, 0.016, 0.014),
        (*KNUCKLE, 0.012, 0.011),
        (*(KNUCKLE.lerp(FINGER_TIPS[0], 0.6)), 0.008, 0.007),
        (*FINGER_TIPS[0], 0.003, 0.003),
    ]
    parts.append(loft("lead", lead, sides=5, per=2, tip=True))
    for j in range(1, len(FINGER_TIPS)):
        path = finger_path(j, 2)
        n = len(path) - 1
        ctrl = [(*q, 0.012 - 0.009 * (k / n), 0.010 - 0.007 * (k / n)) for k, q in enumerate(path)]
        parts.append(loft("finger", ctrl, sides=4, per=1, tip=True))
    fingers = join(parts, "fingers")
    set_material(fingers, mats["Wing"])
    thumbs = [
        cone(
            "thumb",
            WRIST + V((0.01, -0.02, -0.01)),
            V((0.899, -0.755, -0.186)),
            0.014,
            sides=5,
            rings=3,
            bend=V((0.0, 0.0, 0.02)),
        ),
        cone(
            "thumb",
            WRIST + V((0.02, -0.02, 0.0)),
            V((0.91, -0.59, -0.09)),
            0.012,
            sides=5,
            rings=2,
            bend=V((0.0, 0.0, 0.012)),
        ),
    ]
    th = join(thumbs, "thumbs")
    set_material(th, mats["Body"])
    return [fingers, th]


def ear(sx, mats):
    """A cupped bat ear: a fan from the base to a pointed rim, given thickness."""
    base = V((0.036, -0.44, 0.02))
    rim = [
        V((0.022, -0.47, 0.035)),
        V((0.045, -0.52, 0.085)),
        V((0.066, -0.556, 0.118)),
        V((0.08, -0.52, 0.08)),
        V((0.11, -0.505, 0.05)),
        V((0.144, -0.494, 0.023)),
        V((0.10, -0.465, 0.0)),
        V((0.06, -0.44, -0.005)),
    ]
    bm = bmesh.new()
    rings = 2
    grid = []
    fwd = V((0, -1, 0.3)).normalized()
    for i in range(rings + 1):
        f = i / rings
        row = []
        for j, r in enumerate(rim):
            p = base.lerp(r, f)
            cup = 0.018 * math.sin(math.pi * f) * math.sin(math.pi * j / (len(rim) - 1))
            p = p + fwd * (-cup)
            row.append(bm.verts.new((p.x * sx, p.y, p.z)))
        grid.append(row)
    for r0, r1 in itertools.pairwise(grid):
        for j in range(len(rim) - 1):
            bm.faces.new((r0[j], r0[j + 1], r1[j + 1], r1[j]))
    # collapse the base row to a point
    bmesh.ops.pointmerge(bm, verts=grid[0], merge_co=grid[0][len(rim) // 2].co.copy())
    o = mesh_from_bm("ear", bm)
    so = o.modifiers.new("solid", "SOLIDIFY")
    so.thickness = 0.007
    so.offset = 0.0
    apply_modifiers(o)
    o.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(o)
    set_material(o, mats["Body"])
    _outward(o)
    return o


def _outward(o):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(o.data)
    bm.free()


def teeth(mats):
    """Rows of teeth along both jaws as thin cards (the 1999 skin paints the teeth on cards)."""
    bm = bmesh.new()
    for sx in (1, -1):
        for y0, y1, ztop, zbot, half, grow in (
            (-0.616, -0.545, -0.024, -0.046, 0.019, 0.026),  # upper row hangs down
            (-0.610, -0.55, -0.036, -0.056, 0.017, 0.024),  # lower row stands up
        ):
            n = 6
            row_t, row_b = [], []
            for k in range(n + 1):
                f = k / n
                y = y0 + (y1 - y0) * f
                w = (half + grow * f) * sx
                row_t.append(bm.verts.new((w, y, ztop)))
                row_b.append(bm.verts.new((w, y, zbot)))
            for k in range(n):
                q = (row_t[k], row_t[k + 1], row_b[k + 1], row_b[k])
                bm.faces.new(q if sx > 0 else tuple(reversed(q)))
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    o = mesh_from_bm("teeth", bm)
    set_material(o, mats["Teeth"])
    fangs = []
    for sx in (1, -1):
        fangs.append(
            cone("fang", V((0.02 * sx, -0.598, -0.03)), V((0.021 * sx, -0.602, -0.066)), 0.006, sides=4, rings=2)
        )
    f = join(fangs, "fangs")
    set_material(f, mats["Teeth"])
    return [o, f]


def tail_blade(mats):
    """Arrow-head blade at the tail tip: two crossed diamond cards."""
    bm = bmesh.new()
    back, front, mid, half = 2.05, 1.74, 1.86, 0.076
    z0 = 0.012
    for plane in ("h", "v"):
        ring = []
        for p in ((0, front, z0), (half, mid, z0), (0, back, z0 + 0.016), (-half, mid, z0)):
            if plane == "v":
                p = (0, p[1], z0 + p[0] * 0.75)
            ring.append(p)
        c = bm.verts.new((0, (front + back) / 2 - 0.02, z0 + 0.004))
        vs = [bm.verts.new(p) for p in ring]
        for k in range(4):
            a, b = vs[k], vs[(k + 1) % 4]
            f = bm.faces.new((c, a, b) if plane == "h" else (c, b, a))
            f.normal_update()
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    o = mesh_from_bm("blade", bm)
    set_material(o, mats["Blade"])
    return o


def dorsal_spikes(mats):
    """A row of small spines from the neck to the tail."""
    parts = []
    for y, z, h in (
        (-0.27, 0.035, 0.02),
        (-0.16, 0.058, 0.025),
        (-0.04, 0.135, 0.03),
        (0.08, 0.148, 0.032),
        (0.20, 0.108, 0.03),
        (0.32, 0.068, 0.026),
        (0.45, 0.058, 0.024),
        (0.60, 0.052, 0.02),
        (0.80, 0.025, 0.016),
        (1.02, -0.008, 0.013),
    ):
        parts.append(cone("spike", V((0, y, z - 0.01)), V((0, y + h * 0.9, z + h)), h * 0.35, sides=4, rings=1))
    o = join(parts, "spikes")
    set_material(o, mats["Body"])
    return o


# ---------------------------------------------------------------------------- build


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats()
    objs = []
    trunk = remesh_decimate(build_trunk(), 0.006, BUDGET["body"])
    set_material(trunk, mats["Body"])
    objs.append(trunk)
    head = remesh_decimate(build_head(), 0.004, BUDGET["head"], smooth=2)
    set_material(head, mats["Body"])
    objs.append(head)
    for sx in (1, -1):
        leg, toes = build_leg(sx)
        leg = remesh_decimate(leg, 0.004, BUDGET["leg"], smooth=2)
        set_material(leg, mats["Body"])
        objs.append(leg)
        claws = [
            cone("claw", t - d * 0.004, t + d * 0.035 + V((0, 0, -0.018)), 0.008, sides=5, rings=2) for t, d in toes
        ]
        c = join(claws, "claws")
        set_material(c, mats["Claw"])
        objs.append(c)
        objs.append(ear(sx, mats))
    wing = membrane(mats)
    objs.append(wing)
    objs.append(mirror_copy(wing, "membrane_l"))
    fore = propatagium(mats)
    objs += [fore, mirror_copy(fore, "propatagium_l")]
    for o in finger_bones(mats):
        objs.append(o)
        objs.append(mirror_copy(o, o.name + "_l"))
    objs += teeth(mats)
    objs.append(tail_blade(mats))
    objs.append(dorsal_spikes(mats))
    for o in objs:
        print(f"part {o.name}: {len(o.data.polygons)} faces")
        for p in o.data.polygons:
            p.use_smooth = True
    # one object with the four material slots in a fixed order
    model = join(objs, "ebon_dragon")
    names = [m.name for m in model.data.materials]
    order = ["Body", "Wing", "Teeth", "Blade", "Claw"]
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
    print(f"ebon dragon: {len(me.vertices)} vertices, {len(me.polygons)} triangles, per slot {per}")


# ---------------------------------------------------------------------------- render


def render(model):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = int(ARGS.res * 1.5)
    sc.render.resolution_y = ARGS.res
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.30, 0.32, 0.36)
    for loc, e in (((-2, -3, 3), 300), ((3, -1, 2), 150), ((0, 3, 3), 200), ((0, 0, -3), 80)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.energy, ld.size = e, 3
        lo = link(bpy.data.objects.new("l", ld))
        lo.location = loc
        lo.rotation_euler = (V((0, 0, 0.1)) - V(loc)).to_track_quat("-Z", "Y").to_euler()
    for m, col in (
        ("Body", (0.35, 0.36, 0.34)),
        ("Wing", (0.55, 0.42, 0.30)),
        ("Teeth", (0.9, 0.9, 0.8)),
        ("Blade", (0.4, 0.5, 0.45)),
        ("Claw", (0.8, 0.8, 0.7)),
    ):
        mat = bpy.data.materials[m]
        mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*col, 1)
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.type = "ORTHO"
    views = [
        ("top", V((0, 0.6, 0)), V((0, 0, 1)), 4.2),
        ("side", V((0, 0.6, 0)), V((1, 0, 0.0001)), 3.0),
        ("front", V((0, 0, -0.05)), V((0, -1, 0.0001)), 4.0),
        ("persp", V((0, 0.5, 0)), V((0.8, -1, 0.7)), 4.0),
        ("head", V((0.0, -0.47, 0.0)), V((0.8, -1, 0.45)), 0.6),
        ("headside", V((0.0, -0.45, 0.0)), V((1, 0, 0.0001)), 0.5),
        ("legs", V((0.0, 0.55, -0.1)), V((1, 0.3, 0.2)), 0.9),
        ("under", V((0, 0.6, 0)), V((0.2, 0.3, -1)), 4.2),
    ]
    for name, c, d, scale in views:
        d = d.normalized()
        cam.location = c + d * 8
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
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
            model="ebondragon",
            label="Ebon Dragon",
            fit="joints",
            slots={
                "Body": {"pcTextures": [1]},
                "Wing": {"pcTextures": [0]},
                "Teeth": {"pcTextures": [0]},
                "Blade": {"pcTextures": [0]},
                "Claw": {"pcTextures": [0]},
            },
            extra={"landmarks": {n: list(JOINTS[n]) for n in LANDMARKS}},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "ebon_dragon.blend"))
    if not ARGS.no_render:
        render(model)


main()
