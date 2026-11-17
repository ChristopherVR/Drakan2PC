"""Build original low-poly Succubus and Queen Shillah meshes in Blender, headless, for the Order of the Flame converter.

The 1999 PC models `Succubus` (the winged women, Common/NPC/NPCs) and `the queen(shaun)` (their queen, a boss) are
replaced at convert time by these meshes: the converter fits them on the PC skeleton and takes UVs, textures and skin
weights from the player's own disc (editor/packages/formats/src/ootfauthored.ts). Both keep their 1999 head (face, hair
cards, the queen's spiked crown), smoothed once by the converter; the new body joins it at the neck.

This script reads no game data. Both characters share one winged-woman base: a lofted trunk with breasts, hips and
shoulders, limbs lofted along the rig's joint chains, hands, feet, and bat wings made of finger bones with membrane
panels between them. Per character only the measured proportions (rounded joint positions and section sizes) and the
wing layout differ: the succubus is slim with her wings folded and hanging from the arms like a cloak, the queen is
obese with short arms held out and her wings spread.

    blender -b --factory-startup -P analysis/scripts/blender/succubus_model.py -- \
        --which all --out build/authored/succubus-model [--authored scripts/buildtool/kit/models] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the character's left side as the PC rig names it "R", y
toward the back, z up; she faces -Y). So the `landmarks` are the PC joint positions and the converter's joint fit is the
identity. Outputs under --out (gitignored): <name>.blend and clay renders <name>_<view>.png.
"""

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
    ap.add_argument("--which", choices=["succubus", "queen", "all"], default="all")
    ap.add_argument("--out", default="build/authored/succubus-model")
    ap.add_argument("--authored", help="folder for the authored model files (succubus.json, queen_shillah.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--size", type=int, default=512, help="atlas size in pixels")
    ap.add_argument("--res", type=int, default=900, help="render height in pixels")
    return ap.parse_args(argv)


# ---------------------------------------------------------------------------- mesh helpers


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


def catmull(ctrl, per=6):
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
                    for a, b, c, d in zip(p0, p1, p2, p3, strict=True)
                )
            )
    out.append(tuple(ctrl[-1]))
    return out


def _ring_faces(bm, rings, sides, cap0=True, cap1=True, tip=None):
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    if cap0:
        bm.faces.new(list(reversed(rings[0])))
    if tip is not None:
        apex = bm.verts.new(tip)
        for k in range(sides):
            bm.faces.new((rings[-1][k], rings[-1][(k + 1) % sides], apex))
    elif cap1:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def _superellipse(a, sq):
    c, s = math.cos(a), math.sin(a)
    return math.copysign(abs(c) ** sq, c), math.copysign(abs(s) ** sq, s)


def trunk(name, sections, sides=24, per=4, sq=1.0):
    """Closed vertical loft through sections (z, front y, back y, half width): front is -y."""
    samples = catmull(sections, per)
    bm = bmesh.new()
    rings = []
    for z, yf, yb, rx in samples:
        cy, ry = (yf + yb) / 2, (yb - yf) / 2
        ring = []
        for k in range(sides):
            c, s = _superellipse(2 * math.pi * k / sides, sq)
            ring.append(bm.verts.new((rx * c, cy + ry * s, z)))
        rings.append(ring)
    _ring_faces(bm, rings, sides)
    return mesh_from_bm(name, bm)


X_AXIS = V((1, 0, 0))


def tube(name, ctrl, sides=10, per=4, ref=X_AXIS, tip=False, sq=1.0):
    """Closed tube through control tuples (x, y, z, radius along `ref`, radius across). `tip` ends in a point."""
    samples = catmull(ctrl, per)
    pts = [V(s[:3]) for s in samples]
    bm = bmesh.new()
    rings = []
    for i, s in enumerate(samples):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side = ref - t * ref.dot(t)
        if side.length < 1e-6:
            side = t.orthogonal()
        side.normalize()
        u = t.cross(side).normalized()
        ring = []
        for k in range(sides):
            c, sn = _superellipse(2 * math.pi * k / sides, sq)
            ring.append(bm.verts.new(pts[i] + side * (c * max(s[3], 1e-5)) + u * (sn * max(s[4], 1e-5))))
        rings.append(ring)
    tip_at = None
    if tip:
        tip_at = pts[-1] + (pts[-1] - pts[-2]).normalized() * max(samples[-1][3], 1e-4) * 1.5
    _ring_faces(bm, rings, sides, tip=tip_at)
    return mesh_from_bm(name, bm)


def ellipsoid(name, c, r, seg=16):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=seg // 2 + 1, radius=1.0)
    for v in bm.verts:
        v.co = V((c[0] + v.co.x * r[0], c[1] + v.co.y * r[1], c[2] + v.co.z * r[2]))
    return mesh_from_bm(name, bm)


def cone(name, base, tipp, r, sides=5, rings=2, bend=None):
    """Tapered closed spike (a claw) from `base` (radius r) to `tipp`; `bend` curves it (offset of the middle)."""
    axis = tipp - base
    ctrl = []
    for i in range(rings + 1):
        f = i / (rings + 1)
        c = base + axis * f
        if bend is not None:
            c = c + bend * (4 * f * (1 - f))
        rr = r * (1 - f) ** 0.8
        ctrl.append((*c, rr, rr))
    ctrl.append((*tipp, 1e-5, 1e-5))
    return tube(name, ctrl[:-1], sides=sides, per=1, tip=True)


def remesh_decimate(obj, voxel, tris, smooth=3):
    rm = obj.modifiers.new("remesh", "REMESH")
    rm.mode = "VOXEL"
    rm.voxel_size = voxel
    rm.adaptivity = 0.0
    apply_modifiers(obj)
    if smooth:
        sm = obj.modifiers.new("smooth", "CORRECTIVE_SMOOTH")
        sm.factor = 0.6
        sm.iterations = smooth
        sm.smooth_type = "LENGTH_WEIGHTED"
        sm.use_only_smooth = True
        apply_modifiers(obj)
    obj.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(obj)
    dec = obj.modifiers.new("dec", "DECIMATE")
    dec.decimate_type = "COLLAPSE"
    dec.ratio = min(1.0, tris / max(len(obj.data.polygons), 1))
    dec.use_collapse_triangulate = True
    apply_modifiers(obj)
    return obj


def mirror_x(p):
    return V((-p[0], p[1], p[2]))


def set_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.material_index = 0


def orient(obj, outward):
    """Flip the faces of a single-sided sheet so their normals point along `outward` (a function of the face centre)."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    flip = []
    for f in bm.faces:
        f.normal_update()
        if f.normal.dot(outward(f.calc_center_median())) < 0:
            flip.append(f)
    bmesh.ops.reverse_faces(bm, faces=flip)
    bm.to_mesh(obj.data)
    bm.free()


# ---------------------------------------------------------------------------- shared base: limbs, hands, feet, wings


def limb(name, chain, radii, ref, sides=12, per=4, offsets=None):
    """Tube along joint points `chain` with (radius along ref, radius across) per point; `offsets` shift points."""
    ctrl = []
    for i, p in enumerate(chain):
        q = V(p) + (V(offsets[i]) if offsets else V())
        ctrl.append((*q, *radii[i]))
    return tube(name, ctrl, sides=sides, per=per, ref=ref)


def hand(name, wrist, tip, palm_n, width, thick, fingers=4, finger_len=0.45, thumb_side=1.0, curl=0.25):
    """Palm from `wrist` toward `tip` (the finger ends), flat across `palm_n` (the palm's facing), plus fingers and a
    thumb as thin tapered tubes. Sizes are absolute (the units of the model)."""
    d = (tip - wrist).normalized()
    length = (tip - wrist).length
    across = palm_n.cross(d).normalized()
    n = d.cross(across).normalized()  # palm normal, toward the palm side
    knuck = wrist + d * (length * (1 - finger_len))
    palm = tube(
        name + "_palm",
        [
            (*(wrist - d * length * 0.08), width * 0.62, thick * 0.9),
            (*(wrist + d * length * 0.25), width * 0.9, thick * 1.0),
            (*knuck, width, thick * 0.8),
        ],
        sides=8,
        per=1,
        ref=across,
        sq=0.8,
    )
    parts = [palm]
    fl = length * finger_len
    for k in range(fingers):
        f = (k + 0.5) / fingers * 2 - 1  # -1 .. 1 across the palm
        base = knuck + across * (f * width * 0.78) - d * fl * 0.08
        lk = fl * (1.0 - 0.18 * abs(f + 0.15))
        mid = base + d * lk * 0.55 + n * lk * curl * 0.4
        end = base + d * lk + n * lk * curl
        r = thick * 0.85
        parts.append(tube(name + "_f", [(*base, r, r), (*mid, r * 0.9, r * 0.9), (*end, r * 0.6, r * 0.6)], 4, 1))
    tb = wrist + d * length * 0.22 + across * (width * 0.7 * thumb_side)
    tm = tb + d * length * 0.18 + across * (width * 0.45 * thumb_side) + n * length * 0.1
    te = tm + d * length * 0.16 + n * length * 0.08
    parts.append(
        tube(name + "_t", [(*tb, thick, thick), (*tm, thick * 0.9, thick * 0.9), (*te, thick * 0.6, thick * 0.6)], 4, 1)
    )
    return join(parts, name)


def foot(name, ankle, toe, sole_z, width, heel_up=0.0):
    """Foot from the heel under the ankle through the ball (the PC toe joint) to the toe tips, sole at `sole_z`."""
    fwd = V((toe.x - ankle.x, toe.y - ankle.y, 0)).normalized()
    side = V((-fwd.y, fwd.x, 0))
    heel = V((ankle.x, ankle.y, 0)) - fwd * width * 1.3
    tipp = V((toe.x, toe.y, 0)) + fwd * width * 2.0
    h_heel = width * 1.0
    h_ball = width * 0.62
    ctrl = [
        (*(heel - fwd * width * 0.2), sole_z + heel_up + h_heel * 0.9, width * 0.55, h_heel * 0.75),
        (*heel, sole_z + heel_up + h_heel, width * 0.8, h_heel),
        (*V((ankle.x, ankle.y, 0)), ankle.z - width * 0.2, width * 0.95, width * 1.25),
        (*V((toe.x, toe.y, 0)), sole_z + h_ball, width * 1.05, h_ball),
        (*(V((toe.x, toe.y, 0)) + fwd * width * 1.2), sole_z + h_ball * 0.85, width * 0.95, h_ball * 0.8),
        (*tipp, sole_z + h_ball * 0.6, width * 0.6, h_ball * 0.5),
    ]
    # (x, y, 0, z, ...) -> (x, y, z, ...)
    ctrl = [(c[0], c[1], c[3], c[4], c[5]) for c in ctrl]
    return tube(name, ctrl, sides=10, per=3, ref=side, sq=0.85)


def polyline_at(pts, f):
    """Point at fraction f (0..1) of the arc length along a polyline (Catmull-Rom smoothed)."""
    dense = [V(p) for p in catmull([tuple(p) for p in pts], 8)]
    lens = [0.0]
    for a, b in itertools.pairwise(dense):
        lens.append(lens[-1] + (b - a).length)
    t = max(0.0, min(1.0, f)) * lens[-1]
    for i in range(1, len(dense)):
        if lens[i] >= t:
            seg = lens[i] - lens[i - 1]
            u = (t - lens[i - 1]) / seg if seg > 1e-12 else 0.0
            return dense[i - 1].lerp(dense[i], u)
    return dense[-1]


def panel(name, rib_a, rib_b, rows, cols, scallop=0.0, billow=None):
    """Membrane between two ribs (polylines from the root to the tip): a grid ruled between them. `scallop` pulls the
    free edge between the tips toward the root (fraction of the length) and `billow` (vector) bows the middle."""
    bm = bmesh.new()
    grid = []
    for j in range(cols + 1):
        u = j / cols
        vmax = 1.0 - scallop * math.sin(math.pi * u)
        col = []
        for i in range(rows + 1):
            v = i / rows * vmax
            p = polyline_at(rib_a, v).lerp(polyline_at(rib_b, v), u)
            if billow is not None:
                p = p + billow * (math.sin(math.pi * u) * math.sin(math.pi * min(1.0, v * 1.4)))
            col.append(bm.verts.new(p))
        grid.append(col)
    for j in range(cols):
        for i in range(rows):
            bm.faces.new((grid[j][i], grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1]))
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-6)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    # drop the triangles that collapsed where two ribs share their root
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges[:], dist=1e-6)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def bone(name, pts, r0, r1, segs=6, sides=4):
    """Tapered finger bone along a polyline, ending in a point."""
    ctrl = []
    for k in range(segs + 1):
        f = k / segs
        p = polyline_at(pts, f)
        r = r0 + (r1 - r0) * f
        ctrl.append((*p, r, r))
    return tube(name, ctrl, sides=sides, per=1, tip=True)


# ---------------------------------------------------------------------------- materials (slots)


def make_mats(names):
    mats = {}
    for name, col in names.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.6
        m.use_backface_culling = False
        mats[name] = m
    return mats


def finish(objs, name, order, mats):
    """One triangulated object with the material slots in a fixed order."""
    for o in objs:
        for p in o.data.polygons:
            p.use_smooth = True
    model = join(objs, name)
    names = [m.name for m in model.data.materials]
    remap = {i: order.index(n.split(".")[0]) for i, n in enumerate(names)}
    idx = [remap[p.material_index] for p in model.data.polygons]
    model.data.materials.clear()
    for n in order:
        model.data.materials.append(mats[n])
    for p, i in zip(model.data.polygons, idx, strict=True):
        p.material_index = i
    model.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(model)
    return model


# ---------------------------------------------------------------------------- the succubus

# Joint positions measured on the 1999 rig (rounded), Blender axes. The rig is not symmetric: her left arm is bent
# further back and her left knee steps forward, so each side has its own values.
SUCC_JOINTS = {
    "tailbone": (0.0, 0.0, 0.0),
    "midspine": (0.0, -0.002, 0.031),
    "upperback": (0.0, -0.004, 0.067),
    "neck": (0.0, -0.008, 0.106),
    "head": (0.0, -0.008, 0.12),
    "RArm": (0.029, -0.001, 0.099),
    "Rforearm": (0.043, 0.023, 0.044),
    "RHand": (0.068, -0.04, 0.019),
    "Larm": (-0.03, 0.001, 0.098),
    "LForearm": (-0.051, 0.04, 0.052),
    "LHand": (-0.068, -0.016, 0.012),
    "RThigh": (0.018, 0.0, 0.0),
    "RCalf": (0.013, -0.013, -0.108),
    "RFoot": (0.01, 0.017, -0.227),
    "RToe": (0.015, -0.003, -0.242),
    "LThigh": (-0.014, 0.0, -0.002),
    "LCalf": (-0.014, -0.038, -0.105),
    "LFoot": (-0.015, 0.012, -0.217),
    "LToe": (-0.019, -0.004, -0.237),
}
SUCC_LANDMARKS = ["tailbone", "upperback", "RArm", "Rforearm", "RHand", "Larm", "LForearm", "LHand"]
SUCC_LANDMARKS += ["RThigh", "RCalf", "RFoot", "LThigh", "LCalf", "LFoot"]
# leg radii below the knee: calf, lower calf, ankle
SUCC_LOWER_LEG = [(0.0118, 0.0142), (0.0088, 0.0105), (0.0062, 0.0072)]
# the 1999 head, its hair cards and eyes stay
SUCC_KEEP = ["head", "hair1", "hair2", "hair3", "RBangs", "LBangs", "RightEye", "LeftEye"]

# trunk sections (z, front y, back y, half width), from the crotch to inside the head
SUCC_TRUNK = [
    (-0.024, -0.010, 0.016, 0.017),
    (-0.010, -0.014, 0.022, 0.027),
    (0.004, -0.015, 0.023, 0.031),
    (0.018, -0.016, 0.017, 0.026),
    (0.032, -0.017, 0.010, 0.0195),
    (0.046, -0.019, 0.009, 0.021),
    (0.060, -0.022, 0.010, 0.0235),
    (0.075, -0.024, 0.011, 0.0265),
    (0.088, -0.022, 0.011, 0.027),
    (0.097, -0.015, 0.009, 0.026),
    (0.104, -0.014, 0.004, 0.013),
    (0.114, -0.015, 0.003, 0.0095),
    (0.128, -0.016, 0.002, 0.009),
]

# wings (right side; the left side mirrors its own values): wrist, knuckle (where the folded fingers fan out), finger
# tips at the hem, the inner edge from the elbow down past the hip, the back edge from the shoulder blade to the waist
SUCC_WING = {
    "R": {
        "wrist": (0.072, -0.038, 0.017),
        "knuckle": (0.115, -0.088, -0.056),
        "tips": [(0.122, -0.105, -0.245), (0.102, -0.09, -0.236), (0.082, -0.07, -0.233)],
        "inner_tip": (0.072, -0.06, -0.198),
        "inner": [(0.046, 0.028, 0.041), (0.045, 0.014, -0.044), (0.054, -0.018, -0.1), (0.066, -0.048, -0.17)],
        "back": [(0.026, 0.007, 0.09), (0.014, 0.011, 0.042)],
    },
    "L": {
        "wrist": (0.072, -0.016, 0.014),
        "knuckle": (0.099, -0.066, -0.066),
        "tips": [(0.098, -0.06, -0.25), (0.084, -0.045, -0.24), (0.07, -0.022, -0.237)],
        "inner_tip": (0.068, -0.008, -0.2),
        "inner": [
            (0.052, 0.045, 0.052),
            (0.045, 0.047, -0.036),
            (0.043, 0.04, -0.07),
            (0.05, 0.02, -0.1),
            (0.06, 0.0, -0.17),
        ],
        "back": [(0.027, 0.006, 0.091), (0.013, 0.01, 0.041)],
    },
}


def succubus_body(mats):
    J = {k: V(v) for k, v in SUCC_JOINTS.items()}
    parts = [trunk("trunk", SUCC_TRUNK, sides=24, per=3, sq=0.92)]
    for sx in (1, -1):
        parts.append(ellipsoid("breast", (0.0148 * sx, -0.027, 0.0675), (0.0125, 0.0125, 0.0118)))
        parts.append(ellipsoid("buttock", (0.0125 * sx, 0.011, -0.004), (0.0145, 0.0125, 0.0165)))
        parts.append(ellipsoid("hip", (0.022 * sx, 0.002, 0.004), (0.011, 0.016, 0.016)))
        parts.append(ellipsoid("shoulder", (0.03 * sx, -0.001, 0.092), (0.0082, 0.009, 0.0074)))
        parts.append(ellipsoid("clavicle", (0.016 * sx, -0.009, 0.0985), (0.014, 0.006, 0.004)))
    for s in "RL":
        sx = 1 if s == "R" else -1
        sh = J["RArm" if s == "R" else "Larm"]
        el = J["Rforearm" if s == "R" else "LForearm"]
        wr = J["RHand" if s == "R" else "LHand"]
        arm_chain = [sh + V((-0.004 * sx, 0, 0.002)), sh.lerp(el, 0.45), el, el.lerp(wr, 0.35), wr]
        parts.append(
            limb(
                "arm",
                arm_chain,
                [(0.0098, 0.0098), (0.0085, 0.0085), (0.0066, 0.0068), (0.0072, 0.0068), (0.0048, 0.0042)],
                V((1, 0, 0)),
            )
        )
        th = J[f"{s}Thigh"]
        kn = J[f"{s}Calf"]
        an = J[f"{s}Foot"]
        toe = J[f"{s}Toe"]
        leg_chain = [
            th + V((0.0, 0.002, 0.012)),
            th.lerp(kn, 0.35),
            th.lerp(kn, 0.8),
            kn,
            kn.lerp(an, 0.28),
            kn.lerp(an, 0.6),
            an,
        ]
        offsets = [(-0.001 * sx, 0, 0), (0, -0.002, 0), (0, -0.001, 0), (0, -0.002, 0), (0, 0.004, 0), (0, 0.002, 0)]
        offsets.append((0, 0, 0))
        parts.append(
            limb(
                "leg",
                leg_chain,
                [(0.0175, 0.019), (0.0158, 0.0178), (0.0112, 0.0128), (0.0098, 0.0118), *SUCC_LOWER_LEG],
                V((1, 0, 0)),
                sides=14,
                offsets=offsets,
            )
        )
        sole = -0.25 if s == "R" else -0.2465
        parts.append(foot("foot", an, toe, sole, 0.0072, heel_up=0.0 if s == "R" else 0.006))
    body = remesh_decimate(join(parts, "succubus_body"), 0.0011, 1150)
    set_material(body, mats["Body"])
    hands = []
    for s in "RL":
        sx = 1 if s == "R" else -1
        wr = J["RHand" if s == "R" else "LHand"]
        w = SUCC_WING[s]
        k = V(w["knuckle"])
        if s == "L":
            k = mirror_x(k)
        # the hand points down and forward along the wing's leading finger, the palm toward the thigh
        tip = wr + (k - wr).normalized() * 0.04 + V((0, 0, -0.012))
        h = hand(f"hand{s}", wr, tip, V((-sx, 0, 0)), 0.0062, 0.0021, thumb_side=-1.0)
        set_material(h, mats["Body"])
        hands.append(h)
    return [body, *hands]


def succubus_wing(side, mats):
    """One folded wing hanging from the arm: finger panels from the knuckle to the hem, an inner panel from the forearm
    to the inner edge, a back panel from the shoulder blade to the elbow; finger bones in the folds."""
    w = SUCC_WING[side]
    sx = 1 if side == "R" else -1

    def P(p):
        q = V(p)
        return q if sx > 0 else mirror_x(q)

    wrist, knuckle = P(w["wrist"]), P(w["knuckle"])
    tips = [P(t) for t in w["tips"]]
    inner_tip = P(w["inner_tip"])
    inner = [P(p) for p in w["inner"]]
    back = [P(p) for p in w["back"]]
    out = V((sx, 0, 0))
    # folds: each finger rib bows a little outward between the knuckle and the hem, alternating, like a closed fan
    ribs = []
    for i, t in enumerate(tips):
        mid = knuckle.lerp(t, 0.5) + out * (0.006 if i % 2 == 0 else -0.002)
        ribs.append([knuckle, mid, t])
    lead = [wrist, wrist.lerp(knuckle, 0.5) + out * 0.003, knuckle]
    inner_rib = [wrist, wrist.lerp(inner_tip, 0.5) + out * 0.004, inner_tip]
    sheets = []
    rows = 9
    for a, b in itertools.pairwise(ribs):
        sheets.append(panel("wing_f", a, b, rows, 3, scallop=0.04))
    # from the leading finger's inside, back across the wrist to the inner finger
    sheets.append(panel("wing_w", ribs[-1], inner_rib, rows, 2, scallop=0.06))
    sheets.append(panel("wing_i", inner_rib, inner, rows, 2, scallop=0.05, billow=out * 0.003))
    upper_inner = [inner[0], inner[1]]
    sheets.append(panel("wing_b", back, upper_inner, 4, 2, scallop=0.0))
    membrane = join(sheets, f"wing{side}")
    set_material(membrane, mats["Wing"])
    centre = V((0, -0.01, 0.0))
    orient(membrane, lambda c: (c - centre) * V((1, 0.4, 0)))
    bones = [bone("bone", lead, 0.0026, 0.0022, segs=3)]
    # the bones show in the folds near the hand; lower down the 1999 skin paints them on the membrane
    for r in ribs:
        bones.append(bone("bone", [r[0], r[0].lerp(r[2], 0.3), r[0].lerp(r[2], 0.55)], 0.0022, 0.0008, segs=3))
    bones.append(bone("bone", [inner_rib[0], inner_rib[0].lerp(inner_rib[2], 0.4)], 0.0018, 0.0007, segs=2))
    bones.append(cone("claw", knuckle, knuckle + V((0.004 * sx, -0.008, 0.01)), 0.0022, sides=4, rings=1))
    b = join(bones, f"bones{side}")
    set_material(b, mats["Bone"])
    return [membrane, b]


def build_succubus():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats({"Body": (0.55, 0.36, 0.28), "Wing": (0.45, 0.08, 0.06), "Bone": (0.15, 0.14, 0.14)})
    objs = succubus_body(mats)
    for s in "RL":
        objs += succubus_wing(s, mats)
    return finish(objs, "succubus", ["Body", "Wing", "Bone"], mats)


# ---------------------------------------------------------------------------- the queen

QUEEN_JOINTS = {
    "lowerback": (0.0, 0.0, -0.008),
    "upperback": (0.0, 0.003, 0.051),
    "head": (0.0, 0.006, 0.172),
    "RArm": (0.101, 0.006, 0.143),
    "RForearm": (0.148, 0.006, 0.14),
    "RHand": (0.203, 0.0, 0.148),
    "LArm": (-0.101, 0.006, 0.143),
    "LForearm": (-0.15, 0.006, 0.14),
    "LHand": (-0.205, 0.0, 0.149),
    "RWing1B": (0.328, 0.0, 0.124),
    "LWing1B": (-0.317, 0.0, 0.168),
    "RThigh": (0.063, 0.0, -0.008),
    "RCalf": (0.063, -0.008, -0.12),
    "RFoot": (0.063, 0.0, -0.219),
    "LThigh": (-0.061, 0.0, -0.008),
    "LCalf": (-0.061, -0.008, -0.121),
    "LFoot": (-0.061, 0.004, -0.219),
}
QUEEN_LANDMARKS = ["lowerback", "upperback", "RArm", "RForearm", "RHand", "LArm", "LForearm", "LHand"]
QUEEN_LANDMARKS += ["RCalf", "RFoot", "LCalf", "LFoot"]
QUEEN_KEEP = ["head"]

# trunk sections (z, front y, back y, half width)
QUEEN_TRUNK = [
    (-0.042, -0.07, 0.045, 0.062),
    (-0.026, -0.086, 0.068, 0.096),
    (-0.006, -0.092, 0.078, 0.105),
    (0.015, -0.092, 0.079, 0.103),
    (0.035, -0.087, 0.075, 0.1),
    (0.055, -0.081, 0.07, 0.097),
    (0.075, -0.072, 0.071, 0.095),
    (0.095, -0.063, 0.073, 0.094),
    (0.115, -0.056, 0.07, 0.096),
    (0.13, -0.045, 0.066, 0.097),
    (0.145, -0.03, 0.061, 0.094),
    (0.158, -0.016, 0.053, 0.086),
    (0.168, -0.006, 0.046, 0.072),
    (0.176, 0.0, 0.036, 0.05),
    (0.184, 0.004, 0.03, 0.032),
]

# wings (right side): fingers from the wrist; the leading finger bends at the knuckle and ends at the wing tip, three
# more reach the trailing edge, the last one ends where the inner membrane meets the flank
QUEEN_WING = {
    "R": {
        "wrist": (0.205, 0.006, 0.15),
        "knuckle": (0.33, 0.012, 0.13),
        "tip": (0.612, -0.017, -0.034),
        "fingers": [
            [(0.205, 0.006, 0.15), (0.36, 0.028, 0.05), (0.565, 0.03, -0.02)],
            [(0.205, 0.006, 0.15), (0.33, 0.045, 0.0), (0.43, 0.056, -0.125)],
            [(0.205, 0.006, 0.15), (0.235, 0.05, 0.02), (0.245, 0.06, -0.095)],
            [(0.205, 0.006, 0.15), (0.175, 0.056, 0.04), (0.155, 0.063, -0.036)],
        ],
        "flank": [(0.105, 0.04, 0.128), (0.09, 0.06, 0.09), (0.082, 0.062, 0.05)],
    },
    "L": {
        "wrist": (0.207, 0.006, 0.151),
        "knuckle": (0.317, 0.004, 0.168),
        "tip": (0.657, -0.026, 0.113),
        "fingers": [
            [(0.207, 0.006, 0.151), (0.4, 0.02, 0.06), (0.59, 0.03, -0.03)],
            [(0.207, 0.006, 0.151), (0.34, 0.04, -0.01), (0.427, 0.05, -0.124)],
            [(0.207, 0.006, 0.151), (0.235, 0.048, 0.02), (0.24, 0.053, -0.088)],
            [(0.207, 0.006, 0.151), (0.172, 0.05, 0.04), (0.153, 0.055, -0.031)],
        ],
        "flank": [(0.105, 0.04, 0.128), (0.09, 0.058, 0.09), (0.08, 0.058, 0.05)],
    },
}


def queen_body(mats):
    J = {k: V(v) for k, v in QUEEN_JOINTS.items()}
    parts = [trunk("trunk", QUEEN_TRUNK, sides=28, per=3, sq=0.88)]
    parts.append(ellipsoid("gut", (0.0, -0.042, -0.004), (0.088, 0.062, 0.052)))
    parts.append(ellipsoid("gut_low", (0.0, -0.035, -0.032), (0.06, 0.04, 0.025)))
    for sx in (1, -1):
        parts.append(ellipsoid("breast", (0.05 * sx, -0.06, 0.074), (0.037, 0.034, 0.031)))
        parts.append(ellipsoid("buttock", (0.046 * sx, 0.032, -0.038), (0.05, 0.034, 0.045)))
        parts.append(ellipsoid("hip", (0.085 * sx, 0.0, -0.01), (0.03, 0.06, 0.04)))
        parts.append(ellipsoid("trapezius", (0.06 * sx, 0.02, 0.145), (0.045, 0.034, 0.026)))
        parts.append(ellipsoid("flank", (0.09 * sx, 0.012, 0.06), (0.022, 0.06, 0.06)))
    for s in "RL":
        sx = 1 if s == "R" else -1
        sh = J[f"{s}Arm"]
        el = J[f"{s}Forearm"]
        wr = J[f"{s}Hand"]
        chain = [
            sh + V((-0.03 * sx, 0.01, 0.0)),
            sh + V((0, 0.006, -0.006)),
            sh.lerp(el, 0.6),
            el,
            el.lerp(wr, 0.5),
            wr,
        ]
        radii = [(0.03, 0.03), (0.026, 0.024), (0.02, 0.019), (0.016, 0.016), (0.015, 0.014), (0.0105, 0.009)]
        parts.append(limb("arm", chain, radii, V((0, 0, 1)), sides=12))
        parts.append(ellipsoid("flab", (0.118 * sx, 0.014, 0.123), (0.032, 0.02, 0.017)))
        kn = J[f"{s}Calf"]
        an = J[f"{s}Foot"]
        x = 0.052 * sx
        leg = [
            (0.062 * sx, 0.004, 0.0, 0.048, 0.05),
            (0.056 * sx, 0.002, -0.06, 0.044, 0.047),
            (x, -0.008, -0.105, 0.037, 0.039),
            (x, -0.009, kn.z - 0.005, 0.033, 0.036),
            (0.053 * sx, -0.001, -0.15, 0.029, 0.03),
            (0.0535 * sx, 0.003, -0.18, 0.0225, 0.021),
            (0.054 * sx, 0.003, -0.203, 0.0175, 0.016),
            (0.055 * sx, an.y + 0.002, an.z + 0.002, 0.015, 0.0145),
        ]
        parts.append(tube("leg", leg, sides=16, per=3, ref=V((1, 0, 0))))
        toe = V((an.x - 0.003 * sx, -0.03, -0.222))
        parts.append(foot("foot", V((0.055 * sx, an.y + 0.002, an.z + 0.004)), toe, -0.2275, 0.0135))
    # 1,000 triangles: the rest of the budget goes to the feather cards and the collar
    body = remesh_decimate(join(parts, "queen_body"), 0.0022, 1000)
    set_material(body, mats["Body"])
    plumage_slot(body, mats["Down"])
    hands = []
    for s in "RL":
        sx = 1 if s == "R" else -1
        wr = J[f"{s}Hand"]
        h = hand(f"hand{s}", wr, wr + V((0.036 * sx, -0.012, -0.004)), V((0, 0, -1)), 0.0085, 0.0034, fingers=3)
        h2 = cone("claw", wr + V((0.012 * sx, -0.006, 0.006)), wr + V((0.022 * sx, -0.02, 0.016)), 0.004, rings=1)
        hh = join([h, h2], f"hand{s}")
        set_material(hh, mats["Body"])
        hands.append(hh)
    return [body, *hands]


def plumage_slot(body, mat):
    """Put the body faces that lie well inside the plumage (arms, shoulders, flanks: under the feather cards) into their
    own slot, so the atlas can give them a lower texel density. Same paint as the body (queen_body_material)."""
    body.data.materials.append(mat)
    for p in body.data.polygons:
        x, y, z = p.center
        ax = abs(x)
        f = min(max((z - 0.10) / 0.045, 0.0), 1.0)
        thr = 0.094 - 0.034 * f * f * (3 - 2 * f)
        arm = ax - thr > 0.012 and ax < 0.19 and z > 0.035
        mantle = z > 0.158 and y > -0.008 and ax > 0.06
        if arm or mantle:
            p.material_index = 1


def queen_wing(side, mats):
    """A spread bat wing: the leading finger bent at the knuckle to the tip, four fingers fanning to the trailing edge,
    scalloped between the tips, and an inner membrane from the last finger to the flank."""
    w = QUEEN_WING[side]
    sx = 1 if side == "R" else -1

    def P(p):
        q = V(p)
        return q if sx > 0 else mirror_x(q)

    wrist, knuckle, tip = P(w["wrist"]), P(w["knuckle"]), P(w["tip"])
    lead = [wrist, knuckle, knuckle.lerp(tip, 0.5) + V((0, -0.004, 0.012)), tip]
    fingers = [[P(p) for p in f] for f in w["fingers"]]
    flank = [P(p) for p in w["flank"]]
    back = V((0, 1, 0))
    sheets = [panel("wing_0", lead, fingers[0], 12, 2, scallop=0.12, billow=back * 0.006)]
    for i, (a, b) in enumerate(itertools.pairwise(fingers)):
        sheets.append(panel(f"wing_{i + 1}", a, b, 10, 3, scallop=0.14 if i < 2 else 0.1, billow=back * 0.01))
    sheets.append(panel("wing_in", fingers[-1], [wrist, *flank], 6, 2, scallop=0.06, billow=back * 0.004))
    membrane = join(sheets, f"wing{side}")
    set_material(membrane, mats["Wing"])
    orient(membrane, lambda c: V((0, -1, 0)))
    bones = [bone("bone", lead, 0.0075, 0.002, segs=7, sides=4)]
    for f in fingers:
        bones.append(bone("bone", f, 0.005, 0.0012, segs=5, sides=3))
    bones.append(cone("claw", wrist + V((0.006 * sx, 0, 0.004)), wrist + V((0.012 * sx, -0.012, 0.03)), 0.006))
    b = join(bones, f"bones{side}")
    set_material(b, mats["Bone"])
    return [membrane, b]


def _surface_hit(bvh, origin, direction):
    """Point and outward normal where a ray from inside the body leaves it (the last hit along the ray)."""
    direction = V(direction).normalized()
    hit, normal = None, None
    o = V(origin)
    for _ in range(8):
        loc, nrm, _i, _d = bvh.ray_cast(o, direction)
        if loc is None:
            break
        hit, normal = loc, nrm
        o = loc + direction * 1e-4
    if normal is not None and normal.dot(direction) < 0:
        normal = -normal
    return hit, normal


def feather_card(bm, fuv, p, n, d, length, width, lift, variant=0):
    """One feather: a kite-shaped card folded along its shaft (5 vertices, 4 triangles), its base sunk into the body at
    `p`, lying along `d` in the tangent plane and lifted `lift` off the surface at the tip, so rows of them overlap like
    shingles and stand out in silhouette. `fuv` gets the feather's own coordinates (u along the shaft, v across), u
    offset by 2 per `variant` (the painted variant, see FEATHER_TILES)."""
    n = V(n).normalized()
    d = (V(d) - n * V(d).dot(n)).normalized()
    s = n.cross(d)
    base = p - n * (width * 0.25) - d * (length * 0.05)
    mid = p + d * (length * 0.6) + n * (lift * 0.55)
    pts = {
        "base": (base, (0.0, 0.0)),
        "L": (mid - s * (width / 2), (0.6, -1.0)),
        "R": (mid + s * (width / 2), (0.6, 1.0)),
        "tip": (p + d * length + n * lift, (1.0, 0.0)),
        "ridge": (mid + n * (width * 0.16), (0.6, 0.0)),
    }
    vs = {k: bm.verts.new(c) for k, (c, _uv) in pts.items()}
    for tri in (("base", "ridge", "R"), ("base", "L", "ridge"), ("ridge", "tip", "R"), ("L", "tip", "ridge")):
        f = bm.faces.new([vs[k] for k in tri])
        for loop, k in zip(f.loops, tri, strict=True):
            loop[fuv].uv = (pts[k][1][0] + 2 * variant, pts[k][1][1])


# the painted feather variants share four tiles in the top right corner of the atlas (2 x 2, each 0.12 x 0.2): every
# card shows one of them, so each feather is painted at a size that still reads at 256 px
FEATHER_TILES = (0.76, 0.6, 0.12, 0.2, 0.008)


def feather_tile(fuv):
    """Atlas UV of a feather card loop from its "fuv" coordinates (u + 2 x variant along, v across in -1..1)."""
    x0, y0, w, h, inset = FEATHER_TILES
    k = math.floor(fuv[0] / 2 + 1e-6)
    u = fuv[0] - 2 * k
    tx, ty = x0 + (k % 2) * w + inset, y0 + (k // 2) * h + inset
    return V((tx + (fuv[1] + 1) / 2 * (w - 2 * inset), ty + u * (h - 2 * inset)))


def queen_feathers(body, mats):
    """Layered feather cards over the plumage the 1999 queen has painted: rows along the upper arms and forearms
    (pointing to the hand, the lower ones hanging like a fringe), a two-row mantle over the shoulders and three rows down
    each flank. Few and large, so they read by their silhouette at game distance."""
    from mathutils.bvhtree import BVHTree

    bvh = BVHTree.FromObject(body, bpy.context.evaluated_depsgraph_get())
    bm = bmesh.new()
    fuv = bm.loops.layers.uv.new("fuv")
    up, back, out_z = V((0, 0, 1)), V((0, 1, 0)), V((0, 0, -1))
    count = itertools.count()

    def var():
        c = next(count)
        return (c * 3 + c // 4) % 4

    for s in "RL":
        sx = 1 if s == "R" else -1
        sh = V(QUEEN_JOINTS[f"{s}Arm"])
        wr = V(QUEEN_JOINTS[f"{s}Hand"])
        axis = (wr - sh).normalized()
        # arms: 4 stations from the shoulder to the wrist, 5 feathers round the top, front, back and underside
        for i, t in enumerate((0.0, 0.3, 0.6, 0.88)):
            c = sh.lerp(wr, t) + V((-0.012 * sx, 0, 0))
            for k, ang in enumerate((-115, -60, 0, 60, 115)):
                a = math.radians(ang + (14 if i % 2 else 0))
                ray = up * math.cos(a) + back * math.sin(a)
                p, n = _surface_hit(bvh, c, ray)
                if p is None:
                    continue
                hang = 0.25 + 1.1 * (1 - math.cos(a)) / 2  # the lower ones point more downward
                d = axis + out_z * hang
                size = 1.0 - 0.18 * t
                feather_card(bm, fuv, p, n, d, 0.05 * size, 0.026 * size, 0.008 + 0.004 * (k % 2), var())
        # mantle: two rows round the shoulders from the collarbone to the shoulder blade, pointing down and outward
        centre = V((0.0, 0.015, 0.13))
        for row, (elev, phis, length, width) in enumerate(
            (
                (0.6, (30, 52, 74, 96, 118, 140, 162), 0.05, 0.03),
                (0.28, (42, 63, 85, 107, 129, 151, 172), 0.058, 0.032),
            )
        ):
            for phi in phis:
                f = math.radians(phi)
                horiz = V((math.sin(f) * sx, -math.cos(f), 0.0))
                p, n = _surface_hit(bvh, centre, horiz + up * elev)
                if p is None:
                    continue
                feather_card(bm, fuv, p, n, horiz * 0.45 + out_z, length, width, 0.011 - row * 0.002, var())
        # flanks under the arm, three staggered rows pointing down
        for r, z in enumerate((0.115, 0.08, 0.045)):
            for y in (-0.04, -0.008, 0.024):
                yy = y + (0.016 if r % 2 else 0.0)
                p, n = _surface_hit(bvh, V((0.0, yy, z)), V((sx, 0, 0)))
                if p is None:
                    continue
                feather_card(bm, fuv, p, n, out_z + V((sx * 0.15, 0, 0)), 0.048, 0.027, 0.008, var())
    obj = mesh_from_bm("feathers", bm)
    set_material(obj, mats["Feather"])
    return obj


def queen_collar(body, mats):
    """A gold collar band (a torc) round the base of the neck, lower at the front, over the seam with the kept head."""
    from mathutils.bvhtree import BVHTree

    bvh = BVHTree.FromObject(body, bpy.context.evaluated_depsgraph_get())
    bm = bmesh.new()
    n_ring, rings = 12, []
    for k in range(n_ring):
        a = 2 * math.pi * k / n_ring  # 0 at the front
        horiz = V((math.sin(a), -math.cos(a), 0.0))
        z = 0.163 - 0.012 * max(0.0, math.cos(a))
        p, n = _surface_hit(bvh, V((0.0, 0.02, z)), horiz)
        if p is None:
            p, n = V((0.0, 0.02, z)) + horiz * 0.05, horiz
        n = V((n.x, n.y, 0.0)).normalized()
        p = p + n * 0.0015
        h, t = 0.0055, 0.0032  # half height, thickness
        rings.append([bm.verts.new(p + V((0, 0, h)) + n * q) for q in (0.0, t)])
        rings[-1] += [bm.verts.new(p - V((0, 0, h)) + n * t), bm.verts.new(p - V((0, 0, h)))]
    for k in range(n_ring):
        r0, r1 = rings[k], rings[(k + 1) % n_ring]
        for j in range(4):
            j1 = (j + 1) % 4
            bm.faces.new((r0[j], r1[j], r1[j1], r0[j1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    obj = mesh_from_bm("collar", bm)
    set_material(obj, mats["Gold"])
    return obj


def build_queen():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats(
        {
            "Body": (0.62, 0.55, 0.52),
            "Wing": (0.35, 0.22, 0.2),
            "Bone": (0.5, 0.45, 0.4),
            "Feather": (0.3, 0.26, 0.22),
            "Gold": (0.6, 0.45, 0.15),
            "Down": (0.3, 0.26, 0.22),
        }
    )
    objs = queen_body(mats)
    objs += [queen_feathers(objs[0], mats), queen_collar(objs[0], mats)]
    for s in "RL":
        objs += queen_wing(s, mats)
    return finish(objs, "queen_shillah", ["Body", "Wing", "Bone", "Feather", "Gold", "Down"], mats)


# ---------------------------------------------------------------------------- own textures (procedural, baked)
#
# Drakan 2 characters carry painted-in shading and a clean, earthy surface. The body materials below are procedural in
# the model's own coordinates (object space, the PC units): regions (bodysuit, skin, feathers, bands) are masks on the
# position, patterns are Voronoi scales and stretched noise, creases are tinted through an ambient occlusion node. They
# are baked by own_texture.bake_atlas into one atlas per character. Nothing is taken from the 1999 textures; the 1999
# colours only guided the palette (the succubus red and dark, the queen pale with grey-brown feathers).


LIFT = 0.3


class Shade:
    """A small expression builder over a material's node tree. Values are floats, colour tuples or output sockets."""

    def __init__(self, mat):
        mat.use_nodes = True
        self.nt = mat.node_tree
        self.nt.nodes.clear()
        self.out = self.nt.nodes.new("ShaderNodeOutputMaterial")
        tc = self.nt.nodes.new("ShaderNodeTexCoord")
        self.P = tc.outputs["Object"]
        sep = self.nt.nodes.new("ShaderNodeSeparateXYZ")
        self.nt.links.new(self.P, sep.inputs[0])
        self.x, self.y, self.z = sep.outputs[0], sep.outputs[1], sep.outputs[2]

    def _set(self, sock, v):
        if isinstance(v, bpy.types.NodeSocket):
            self.nt.links.new(v, sock)
        elif isinstance(v, (tuple, list)):
            sock.default_value = (*v, 1.0)[: len(sock.default_value)] if len(v) == 3 else v
        else:
            sock.default_value = v

    def node(self, kind, **props):
        n = self.nt.nodes.new(kind)
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def math(self, op, a, b=0.0, c=None, clamp=False):
        n = self.node("ShaderNodeMath", operation=op, use_clamp=clamp)
        self._set(n.inputs[0], a)
        self._set(n.inputs[1], b)
        if c is not None:
            self._set(n.inputs[2], c)
        return n.outputs[0]

    def add(self, a, b):
        return self.math("ADD", a, b)

    def mul(self, a, b):
        return self.math("MULTIPLY", a, b)

    def sub(self, a, b):
        return self.math("SUBTRACT", a, b)

    def maxf(self, a, b):
        return self.math("MAXIMUM", a, b)

    def minf(self, a, b):
        return self.math("MINIMUM", a, b)

    def absf(self, a):
        return self.math("ABSOLUTE", a)

    def inv(self, a):
        return self.math("SUBTRACT", 1.0, a, clamp=True)

    def step(self, v, lo, hi):
        """Smoothstep of v from lo (0) to hi (1); hi < lo flips it."""
        n = self.node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP", clamp=True)
        self._set(n.inputs["Value"], v)
        a, b = (lo, hi) if hi > lo else (hi, lo)
        self._set(n.inputs["From Min"], a)
        self._set(n.inputs["From Max"], b)
        n.inputs["To Min"].default_value = 0.0 if hi > lo else 1.0
        n.inputs["To Max"].default_value = 1.0 if hi > lo else 0.0
        return n.outputs["Result"]

    def band(self, v, centre, half, soft):
        """1 where |v - centre| < half, fading over `soft`."""
        d = self.absf(self.sub(v, centre))
        return self.step(d, half + soft, half)

    def vec(self, x, y, z):
        n = self.node("ShaderNodeCombineXYZ")
        for i, v in enumerate((x, y, z)):
            self._set(n.inputs[i], v)
        return n.outputs[0]

    def vmath(self, op, a, b=None, out="Vector"):
        n = self.node("ShaderNodeVectorMath", operation=op)
        self._set(n.inputs[0], a)
        if b is not None:
            self._set(n.inputs[1], b)
        return n.outputs[out]

    def scaled(self, sx, sy, sz, vec=None):
        """The position scaled per axis (stretched patterns)."""
        return self.vmath("MULTIPLY", vec if vec is not None else self.P, (sx, sy, sz))

    def dist(self, centre, vec=None):
        return self.vmath("DISTANCE", vec if vec is not None else self.P, tuple(centre), out="Value")

    def along(self, origin, direction):
        """Signed distance of the position past `origin` along the unit `direction`."""
        d = self.vmath("SUBTRACT", self.P, tuple(origin))
        return self.vmath("DOT_PRODUCT", d, tuple(direction), out="Value")

    def noise(self, scale, vec=None, detail=3.0, rough=0.55, distortion=0.0, out="Fac"):
        n = self.node("ShaderNodeTexNoise")
        self._set(n.inputs["Vector"], vec if vec is not None else self.P)
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        n.inputs["Distortion"].default_value = distortion
        return n.outputs[out]

    def voronoi(self, scale, vec=None, feature="F1", rand=1.0):
        n = self.node("ShaderNodeTexVoronoi", feature=feature)
        self._set(n.inputs["Vector"], vec if vec is not None else self.P)
        n.inputs["Scale"].default_value = scale
        n.inputs["Randomness"].default_value = rand
        return n.outputs

    def mix(self, fac, a, b, blend="MIX"):
        n = self.node("ShaderNodeMix", data_type="RGBA", blend_type=blend, clamp_factor=True)
        ins = {s.identifier: s for s in n.inputs}
        self._set(ins["Factor_Float"], fac)
        self._set(ins["A_Color"], a)
        self._set(ins["B_Color"], b)
        return {s.identifier: s for s in n.outputs}["Result_Color"]

    def ramp(self, fac, stops):
        """Colour ramp over (position, colour) stops."""
        n = self.node("ShaderNodeValToRGB")
        self._set(n.inputs[0], fac)
        els = n.color_ramp.elements
        while len(els) < len(stops):
            els.new(0.5)
        for e, (pos, col) in zip(els, stops, strict=True):
            e.position = pos
            e.color = (*col, 1.0)
        return n.outputs[0]

    def ao(self, distance, samples=16):
        n = self.node("ShaderNodeAmbientOcclusion", samples=samples)
        n.inputs["Distance"].default_value = distance
        return n.outputs["AO"]

    def surface(self, colour, rough=0.7, glow=LIFT):
        """The bake light (a soft sky, about 0.45 on average) is dim: `glow` adds the colour as emission so the baked
        picture lands at the intended brightness while keeping the light's shading."""
        b = self.node("ShaderNodeBsdfPrincipled")
        self._set(b.inputs["Base Color"], colour)
        self._set(b.inputs["Emission Color"], colour)
        b.inputs["Emission Strength"].default_value = glow
        b.inputs["Roughness"].default_value = rough
        self.nt.links.new(b.outputs[0], self.out.inputs["Surface"])


def lerpf(s, a, b, t):
    return s.add(a, s.mul(t, b - a))


def skin(s, base, warm, crease, scale, occ_dist, occ_k=0.85, form=0.35, cool=None):
    """Skin: a soft mottle between `base` and `warm`, a faint pore grain, creases (occlusion) tinted toward `crease`."""
    mott = s.noise(scale, detail=4.0, rough=0.6)
    col = s.mix(s.step(mott, 0.36, 0.68), base, warm)
    if cool is not None:  # broad cool patches (veins showing through pale skin)
        col = s.mix(s.mul(s.step(s.noise(scale * 0.4, detail=3.0), 0.45, 0.75), 0.3), col, cool)
    grain = s.noise(scale * 7, detail=2.0)
    col = s.mix(s.mul(s.step(grain, 0.45, 0.7), 0.06), col, tuple(c * 0.8 for c in base))
    occ = s.ao(occ_dist)
    col = s.mix(s.mul(s.inv(occ), occ_k), col, crease)
    # painted form: the undersides (belly, breasts, buttocks) a shade darker, toward the crease colour
    nz = s.vmath("DOT_PRODUCT", s.node("ShaderNodeNewGeometry").outputs["Normal"], (0.0, 0.0, 1.0), out="Value")
    return s.mix(s.mul(s.step(nz, 0.1, -0.7), form), col, crease)


def side_value(s, right, left):
    """`right` on the +x side (the rig's R), `left` on the -x side."""
    return lerpf(s, left, right, s.step(s.x, -0.001, 0.001))


SUCC_SKIN = (0.84, 0.25, 0.08)


def succubus_body_material(mat):
    """Skin (the warm orange of the kept 1999 face) and a high-cut bodysuit of dark crimson scales with long sleeves,
    bronze piping on its edges, bronze bracelets and anklets."""
    s = Shade(mat)
    ax = s.absf(s.x)
    back = s.step(s.y, -0.004, 0.012)
    # leg openings: a high V at the front, lower over the seat
    leg_d = s.sub(s.z, s.add(s.mul(ax, lerpf(s, 1.15, 0.6, back)), lerpf(s, -0.022, -0.03, back)))
    # the collar round the neck base, rising over the shoulders
    neck_d = s.sub(s.add(0.0965, s.mul(s.maxf(s.sub(ax, 0.010), 0.0), 1.3)), s.z)
    # a narrow plunge between the breasts
    front = s.step(s.y, -0.004, -0.010)
    v_d = s.add(s.sub(ax, s.mul(s.maxf(s.sub(s.z, 0.06), 0.0), 0.28)), 0.003)
    v_d = s.add(v_d, s.mul(s.inv(front), 1.0))
    suit_d = s.minf(s.minf(leg_d, neck_d), v_d)
    cuffs = 0.0
    for el, wr in (("Rforearm", "RHand"), ("LForearm", "LHand")):
        e, w = V(SUCC_JOINTS[el]), V(SUCC_JOINTS[wr])
        d = (w - e).normalized()
        near = s.step(s.dist(w), 0.03, 0.022)
        t = s.along(w - d * 0.001, d)
        suit_d = s.minf(suit_d, s.add(s.mul(t, -1.0), s.mul(s.inv(near), 1.0)))
        cuffs = s.add(cuffs, s.mul(s.band(t, -0.0045, 0.0017, 0.0003), near))
    suit = s.step(suit_d, -0.0003, 0.0003)
    trim = s.band(suit_d, 0.0008, 0.00065, 0.00025)
    ankle_z = side_value(s, SUCC_JOINTS["RFoot"][2] + 0.0085, SUCC_JOINTS["LFoot"][2] + 0.0085)
    anklet = s.mul(s.band(s.z, ankle_z, 0.0007, 0.00025), s.step(s.z, -0.15, -0.17))

    # scales: Voronoi cells, each lit from above (lighter at its top), dark lines between them
    sc = 230.0
    cells = s.voronoi(sc)
    edge = s.voronoi(sc, feature="DISTANCE_TO_EDGE")["Distance"]
    rel = s.vmath("SUBTRACT", s.P, cells["Position"])
    up = s.vmath("DOT_PRODUCT", rel, (0.0, 0.0, sc), out="Value")
    lit = s.add(s.mul(up, 0.55), s.mul(s.inv(cells["Distance"]), 0.35))
    scol = s.ramp(lit, [(0.0, (0.03, 0.006, 0.006)), (0.5, (0.12, 0.016, 0.012)), (1.0, (0.36, 0.07, 0.04))])
    scol = s.mix(0.18, scol, cells["Color"], blend="OVERLAY")
    scol = s.mix(s.step(edge, 0.10, 0.02), scol, (0.012, 0.006, 0.006))
    # a broad sheen across the suit so it is not one flat dark
    sheen = s.noise(45.0, detail=2.0)
    scol = s.mix(s.mul(s.step(sheen, 0.45, 0.75), 0.35), scol, (0.24, 0.06, 0.045))

    skin_c = skin(s, SUCC_SKIN, (0.86, 0.22, 0.075), (0.45, 0.10, 0.045), 140.0, 0.006, 0.5)
    col = s.mix(suit, skin_c, scol)
    bronze = s.mix(s.step(s.noise(900.0), 0.3, 0.7), (0.30, 0.17, 0.06), (0.46, 0.30, 0.10))
    col = s.mix(s.mul(trim, 0.95), col, s.mix(0.5, bronze, (0.10, 0.04, 0.02)))
    col = s.mix(s.minf(s.add(cuffs, anklet), 1.0), col, bronze)
    s.surface(col, rough=0.6)


def horn_material(mat, dark, light, scale):
    """Wing bones and claws: mottled horn."""
    s = Shade(mat)
    m = s.noise(scale, detail=4.0, distortion=0.4)
    col = s.ramp(m, [(0.3, dark), (0.7, light)])
    occ = s.ao(0.006)
    col = s.mix(s.mul(s.inv(occ), 0.7), col, tuple(c * 0.4 for c in dark))
    s.surface(col)


def feathers(s, stretch, axis, sign, barbs):
    """Overlapping feathers: Voronoi cells stretched by `stretch` (long along `axis`), each lighter toward its tip
    (the `axis` end, times `sign`) so the rows read as overlapping, with a pale shaft, fine barbs and dark gaps."""
    fv = s.scaled(*stretch)
    cells = s.voronoi(1.0, vec=fv, rand=0.8)
    edge = s.voronoi(1.0, vec=fv, feature="DISTANCE_TO_EDGE", rand=0.8)["Distance"]
    rel = s.vmath("SUBTRACT", fv, cells["Position"])
    tip = s.mul(s.vmath("DOT_PRODUCT", rel, axis, out="Value"), sign)
    across = s.vmath("LENGTH", s.vmath("MULTIPLY", rel, tuple(1.0 - abs(a) for a in axis)), out="Value")
    f = s.add(s.add(0.45, s.mul(tip, 0.45)), s.mul(s.sub(barbs, 0.5), 0.7))
    f = s.add(f, s.mul(s.band(across, 0.0, 0.03, 0.03), s.mul(s.step(tip, -0.45, 0.1), 0.25)))
    col = s.ramp(
        f,
        [(0.1, (0.06, 0.045, 0.035)), (0.5, (0.15, 0.12, 0.09)), (0.8, (0.30, 0.26, 0.21)), (0.97, (0.52, 0.47, 0.40))],
    )
    col = s.mix(s.mul(s.step(edge, 0.08, 0.02), 0.4), col, (0.04, 0.03, 0.024))
    return s.mix(s.mul(s.step(s.noise(20.0, detail=2.0), 0.4, 0.7), 0.3), col, (0.16, 0.11, 0.07))


def seg_dist(s, p, a, b):
    """Distance of the vector `p` to the segment a-b (constant points)."""
    a, b = V(a), V(b)
    ab = b - a
    t = s.math("MULTIPLY", s.vmath("DOT_PRODUCT", s.vmath("SUBTRACT", p, tuple(a)), tuple(ab), out="Value"), 1.0)
    t = s.math("MULTIPLY", t, 1.0 / max(ab.length_squared, 1e-12), clamp=True)
    q = s.vmath("ADD", tuple(a), s.vec(s.mul(t, ab.x), s.mul(t, ab.y), s.mul(t, ab.z)))
    return s.vmath("DISTANCE", p, q, out="Value")


def poly_dist(s, p, pts):
    d = None
    for a, b in itertools.pairwise(pts):
        e = seg_dist(s, p, a, b)
        d = e if d is None else s.minf(d, e)
    return d


def side_sock(s, right, left):
    """Like side_value, for two sockets."""
    t = s.step(s.x, -0.001, 0.001)
    return s.add(left, s.mul(t, s.sub(right, left)))


def mirrored(s):
    """The position with x made positive: one set of right-side shapes serves both sides."""
    return s.vec(s.absf(s.x), s.y, s.z)


def ellipsoid_r(s, c, r):
    """Normalised radius of the position in the ellipsoid (centre c, radii r): 1 on its surface."""
    q = s.vmath("DIVIDE", s.vmath("SUBTRACT", mirrored(s), tuple(c)), tuple(r))
    return q, s.vmath("LENGTH", q, out="Value")


QUEEN_SKIN_LIT = (0.66, 0.45, 0.32)
QUEEN_CREASE = (0.30, 0.17, 0.19)


def queen_body_material(mat):
    """Warm pale skin with painted form: a soft light from above and the front, darker sides and undersides, lilac
    creases under the bust, the belly fold, the armpits and knees, a navel. The plumage areas (arms, shoulders, flanks)
    are painted as dark layered under-feathers: the feather cards lie over them, so the gaps read as depth. Regalia: a
    jewelled gold girdle round the hips and gold bracelets."""
    s = Shade(mat)
    ax = s.absf(s.x)
    rag = s.mul(s.sub(s.noise(70.0, detail=3.0), 0.5), 0.03)
    thr = s.sub(0.094, s.mul(s.step(s.z, 0.10, 0.145), 0.034))
    arm_d = s.sub(s.add(ax, rag), thr)
    arm = s.mul(s.step(arm_d, -0.002, 0.004), s.step(ax, 0.203, 0.196))
    arm = s.mul(arm, s.step(s.add(s.z, rag), 0.0, 0.02))
    mantle = s.mul(s.step(s.add(s.z, rag), 0.142, 0.15), s.step(s.y, -0.04, -0.015))
    mantle = s.mul(mantle, s.step(ax, 0.036, 0.05))  # the neck stays skin, to meet the kept head
    fmask = s.minf(s.add(arm, mantle), 1.0)
    groin_r = s.dist((0.0, -0.09, -0.07), vec=s.scaled(1.0, 1.0, 1.7))
    down = s.mul(s.step(s.add(groin_r, s.mul(rag, 0.5)), 0.03, 0.018), 0.75)

    barbs = s.noise(1.0, vec=s.scaled(90.0, 700.0, 700.0), detail=2.0)
    along_arm = s.step(ax, 0.104, 0.124)
    fcol = s.mix(
        along_arm,
        feathers(s, (40.0, 40.0, 18.0), (0.0, 0.0, -1.0), 1.0, barbs),
        feathers(s, (18.0, 44.0, 44.0), (1.0, 0.0, 0.0), s.math("SIGN", s.x), barbs),
    )
    fcol = s.mix(0.7, fcol, (0.045, 0.032, 0.026))  # under-plumage in the shade of the cards

    skin_c = skin(s, QUEEN_SKIN_LIT, (0.70, 0.45, 0.32), QUEEN_CREASE, 45.0, 0.03, 0.55, 0.7)
    geo = s.node("ShaderNodeNewGeometry").outputs["Normal"]
    nz = s.vmath("DOT_PRODUCT", geo, (0.0, 0.0, 1.0), out="Value")
    nf = s.vmath("DOT_PRODUCT", geo, (0.0, -1.0, 0.0), out="Value")
    nx = s.absf(s.vmath("DOT_PRODUCT", geo, (1.0, 0.0, 0.0), out="Value"))
    # painted light: tops and fronts lighter and warmer, the sides turning away toward the crease tone
    # (a key light from above and in front, painted in: the engine adds little lighting of its own)
    key = s.add(s.add(0.5, s.mul(nz, 0.32)), s.sub(s.mul(nf, 0.38), s.mul(nx, 0.22)))
    shade = s.ramp(
        key, [(0.1, (0.42, 0.33, 0.42)), (0.45, (0.8, 0.74, 0.76)), (0.68, (1.0, 1.0, 1.0)), (0.95, (1.16, 1.12, 1.06))]
    )
    skin_c = s.mix(1.0, skin_c, shade, blend="MULTIPLY")
    big_occ = s.ao(0.09)
    skin_c = s.mix(s.mul(s.inv(big_occ), 0.7), skin_c, (0.24, 0.13, 0.17))
    # creases
    crease = 0.0
    q, br = ellipsoid_r(s, (0.05, -0.06, 0.074), (0.037, 0.034, 0.031))
    qz = s.vmath("DOT_PRODUCT", q, (0.0, 0.0, 1.0), out="Value")
    # the lower half of each breast in soft shade, darkest at the fold
    crease = s.add(crease, s.mul(s.mul(s.step(br, 1.2, 0.95), s.step(qz, 0.0, -0.95)), 0.5))
    belly = s.mul(s.band(s.z, -0.057, 0.003, 0.008), s.mul(s.step(s.y, -0.03, -0.06), s.step(ax, 0.095, 0.07)))
    crease = s.add(crease, belly)
    crease = s.add(crease, s.mul(s.step(s.dist((0.0, -0.103, 0.003)), 0.0065, 0.002), 0.9))  # navel
    crease = s.add(crease, s.mul(s.step(s.dist((0.088, 0.012, 0.118), vec=mirrored(s)), 0.03, 0.008), 0.8))
    crease = s.add(crease, s.mul(s.step(s.dist((0.053, 0.028, -0.122), vec=mirrored(s)), 0.022, 0.006), 0.7))
    crease = s.add(crease, s.mul(s.step(s.dist((0.052, -0.04, -0.142), vec=mirrored(s)), 0.012, 0.003), 0.45))
    knee_cap = s.step(s.dist((0.052, -0.045, -0.114), vec=mirrored(s)), 0.016, 0.004)
    skin_c = s.mix(s.mul(knee_cap, 0.35), skin_c, (0.74, 0.55, 0.43))
    skin_c = s.mix(s.minf(crease, 0.85), skin_c, (0.27, 0.15, 0.22))

    halo = s.mul(s.step(arm_d, -0.016, -0.002), 0.5)
    skin_c = s.mix(halo, skin_c, (0.26, 0.20, 0.18))
    skin_c = s.mix(s.mul(down, s.add(0.5, s.mul(barbs, 0.5))), skin_c, (0.30, 0.22, 0.18))
    col = s.mix(fmask, skin_c, fcol)

    # gold girdle: round the hips, dipping under the belly at the front, red gems along it and a large one in front
    front = s.step(s.y, -0.02, -0.085)
    gz = s.sub(-0.03, s.mul(front, 0.03))
    gband = s.mul(s.band(s.z, gz, 0.0028, 0.0008), s.step(s.z, 0.0, -0.01))
    theta = s.math("ARCTAN2", s.x, s.mul(s.y, -1.0))
    gem_r = s.mul(s.step(s.math("COSINE", s.mul(theta, 9.0)), 0.86, 0.95), gband)
    big = s.step(s.dist((0.0, -0.093, -0.064)), 0.0075, 0.005)
    gold = s.mix(s.step(s.noise(500.0), 0.3, 0.7), (0.2, 0.1, 0.012), (0.5, 0.3, 0.045))
    gold = s.mix(s.mul(s.band(s.sub(s.z, gz), 0.0, 0.0006, 0.0004), 0.6), gold, (0.16, 0.08, 0.02))
    gem = s.mix(s.step(s.sub(s.z, gz), 0.0, 0.003), (0.18, 0.005, 0.02), (0.55, 0.06, 0.08))
    col = s.mix(s.minf(s.add(gband, big), 1.0), col, gold)
    col = s.mix(s.minf(s.add(gem_r, s.step(s.dist((0.0, -0.093, -0.064)), 0.0058, 0.0045)), 1.0), col, gem)
    # bracelets at the wrists
    brace = s.mul(s.band(ax, 0.186, 0.004, 0.0008), s.step(s.z, 0.1, 0.115))
    brace_c = s.mix(s.band(s.sub(ax, 0.186), 0.0, 0.0008, 0.0005), gold, (0.22, 0.01, 0.03))
    col = s.mix(brace, col, brace_c)
    s.surface(col, rough=0.65)


def feather_card_material(mat):
    """The feather cards, painted in their own coordinates ("fuv": u along the shaft plus 2 per variant, v across): a dark
    base, a vane lightening to the tip with a few bold barb bands, a pale edge round the outer half, an ivory shaft and a
    painted light on one side of the fold. Emission only: every card of a variant writes the same pixels into its shared
    tile, so the paint must not depend on where the card is."""
    s = Shade(mat)
    uvn = s.node("ShaderNodeUVMap", uv_map="fuv")
    sep = s.node("ShaderNodeSeparateXYZ")
    s.nt.links.new(uvn.outputs[0], sep.inputs[0])
    raw, v = sep.outputs[0], sep.outputs[1]
    k = s.math("FLOOR", s.add(s.mul(raw, 0.5), 1e-4))
    u = s.sub(raw, s.mul(k, 2.0))
    av = s.absf(v)
    w = s.maxf(s.minf(s.mul(u, 1 / 0.6), s.mul(s.sub(1.0, u), 1 / 0.4)), 0.02)
    e = s.math("DIVIDE", av, w)

    def is_k(j):
        return s.band(k, j, 0.2, 0.1)

    # variants: 0 grey-brown, 1 dark umber, 2 pale ash, 3 warm brown with a pale bar
    mid = s.mix(is_k(1), (0.13, 0.095, 0.07), (0.06, 0.04, 0.028))
    mid = s.mix(is_k(2), mid, (0.27, 0.23, 0.19))
    mid = s.mix(is_k(3), mid, (0.17, 0.10, 0.06))
    tip = s.mix(is_k(1), (0.32, 0.27, 0.21), (0.18, 0.13, 0.09))
    tip = s.mix(is_k(2), tip, (0.45, 0.41, 0.34))
    tip = s.mix(is_k(3), tip, (0.38, 0.27, 0.18))
    col = s.mix(s.step(u, 0.12, 0.42), (0.018, 0.012, 0.01), mid)
    col = s.mix(s.step(u, 0.5, 0.95), col, tip)
    # barbs: chevrons pointing to the tip, four bold bands
    chev = s.math("FRACT", s.mul(s.sub(u, s.mul(av, 0.25)), 4.0))
    col = s.mix(s.mul(s.band(chev, 0.5, 0.1, 0.12), s.mul(s.step(u, 0.3, 0.45), 0.5)), col, (0.0, 0.0, 0.0))
    bar = s.mul(is_k(3), s.band(s.sub(u, s.mul(av, 0.25)), 0.66, 0.05, 0.03))
    col = s.mix(bar, col, (0.62, 0.55, 0.44))
    # painted light: the side of the fold toward v > 0 lit, the other in shade
    col = s.mix(s.mul(s.step(v, 0.1, -0.6), 0.35), col, (0.0, 0.0, 0.0))
    # pale edge round the outer half, a dark rim at the very edge
    col = s.mix(s.mul(s.step(e, 0.7, 0.86), s.mul(s.step(u, 0.5, 0.7), 0.85)), col, (0.50, 0.44, 0.35))
    col = s.mix(s.mul(s.step(e, 0.9, 0.99), 0.6), col, (0.03, 0.022, 0.018))
    # shaft, dark at the quill, ivory along the vane
    shaft = s.mul(s.band(av, 0.0, 0.045, 0.035), s.step(u, 0.95, 0.85))
    col = s.mix(shaft, col, s.mix(s.step(u, 0.12, 0.4), (0.06, 0.045, 0.035), (0.62, 0.56, 0.45)))
    em = s.node("ShaderNodeEmission")
    s._set(em.inputs["Color"], col)
    em.inputs["Strength"].default_value = 1.0
    s.nt.links.new(em.outputs[0], s.out.inputs["Surface"])


def gold_material(mat):
    """The collar: hammered gold with a dark engraved middle line and red cabochons, a large one at the throat."""
    s = Shade(mat)
    gold = s.mix(s.step(s.noise(400.0, detail=3.0), 0.3, 0.7), (0.17, 0.08, 0.008), (0.46, 0.27, 0.035))
    occ = s.ao(0.006)
    gold = s.mix(s.mul(s.inv(occ), 0.8), gold, (0.12, 0.06, 0.015))
    theta = s.math("ARCTAN2", s.x, s.mul(s.sub(s.y, 0.02), -1.0))
    gems = s.step(s.math("COSINE", s.mul(theta, 8.0)), 0.95, 0.985)
    gems = s.maxf(gems, s.step(s.math("COSINE", theta), 0.975, 0.99))
    gem = s.mix(s.step(s.noise(300.0), 0.4, 0.8), (0.20, 0.004, 0.02), (0.6, 0.07, 0.09))
    s.surface(s.mix(gems, gold, gem), rough=0.4)


def queen_membrane_material(mat):
    """Opaque mauve-brown membranes: mottled, with strong branching veins, the finger bones' shadow along each finger
    and a dark leading edge along the arm and the leading finger."""
    s = Shade(mat)
    base, light, vein = (0.13, 0.05, 0.04), (0.25, 0.10, 0.08), (0.03, 0.008, 0.008)
    pm = mirrored(s)
    warp = s.noise(9.0, detail=2.0, out="Color")
    q = s.vmath("ADD", pm, s.vmath("MULTIPLY", warp, (0.025, 0.025, 0.025)))
    sq = s.node("ShaderNodeSeparateXYZ")
    s.nt.links.new(q, sq.inputs[0])
    rx, rz = s.sub(sq.outputs[0], 0.206), s.sub(sq.outputs[2], 0.15)
    theta = s.math("ARCTAN2", rz, rx)
    radius = s.math("SQRT", s.add(s.mul(rx, rx), s.mul(rz, rz)))
    # veins radiate from the wrist between the fingers (about 7 degrees apart), wavy, fading near the wrist
    wav = s.mul(s.sub(s.noise(16.0, detail=1.0), 0.5), 0.09)
    fan = s.math("FRACT", s.mul(s.add(theta, wav), 8.0))
    rad = s.mul(s.band(fan, 0.5, 0.035, 0.05), s.step(radius, 0.04, 0.09))
    cross = s.mul(s.band(s.math("FRACT", s.mul(s.add(radius, s.mul(wav, 0.3)), 9.0)), 0.5, 0.025, 0.04), 0.3)
    m = s.noise(14.0, detail=2.0, rough=0.4)
    col = s.mix(s.step(m, 0.3, 0.75), base, light)
    col = s.mix(s.mul(s.band(fan, 0.5, 0.11, 0.06), 0.35), col, (0.32, 0.15, 0.12))
    col = s.mix(s.minf(s.add(rad, s.mul(cross, s.step(radius, 0.06, 0.12))), 1.0), col, vein)
    lead_d, bone_d = [], []
    for side in "RL":
        w = QUEEN_WING[side]
        wrist, knuckle, tip = V(w["wrist"]), V(w["knuckle"]), V(w["tip"])
        lead_d.append(poly_dist(s, pm, [wrist, knuckle, knuckle.lerp(tip, 0.5) + V((0, -0.004, 0.012)), tip]))
        fd = None
        for f in w["fingers"]:
            e = poly_dist(s, pm, f)
            fd = e if fd is None else s.minf(fd, e)
        bone_d.append(fd)
    lead_d = side_sock(s, lead_d[0], lead_d[1])
    bone_d = side_sock(s, bone_d[0], bone_d[1])
    col = s.mix(s.mul(s.step(bone_d, 0.016, 0.003), 0.6), col, (0.07, 0.03, 0.03))
    col = s.mix(s.step(lead_d, 0.045, 0.01), col, (0.055, 0.022, 0.024))
    occ = s.ao(0.02)
    col = s.mix(s.mul(s.inv(occ), 0.6), col, (0.06, 0.03, 0.03))
    s.surface(col)


def ivory_material(mat):
    """Wing bones and claws: ivory, darker toward the thin ends, with dark knuckle rings and swollen pale joints."""
    s = Shade(mat)
    pm = mirrored(s)
    m = s.noise(300.0, detail=3.0, distortion=0.3)
    col = s.ramp(m, [(0.3, (0.40, 0.34, 0.24)), (0.75, (0.62, 0.56, 0.43))])
    joints = []
    for side in "RL":
        w = QUEEN_WING[side]
        pts = [V(w["knuckle"])] + [V(f[1]) for f in w["fingers"]]
        pts.append(V(w["knuckle"]).lerp(V(w["tip"]), 0.5))
        d = None
        for p in pts:
            e = s.dist(tuple(p), vec=pm)
            d = e if d is None else s.minf(d, e)
        joints.append(d)
    jd = side_sock(s, joints[0], joints[1])
    col = s.mix(s.mul(s.step(jd, 0.012, 0.004), 0.5), col, (0.78, 0.72, 0.58))
    col = s.mix(s.band(jd, 0.014, 0.0025, 0.0015), col, (0.10, 0.07, 0.045))
    tipd = s.step(s.absf(s.x), 0.45, 0.62)
    col = s.mix(s.mul(tipd, 0.5), col, (0.18, 0.13, 0.09))
    occ = s.ao(0.006)
    col = s.mix(s.mul(s.inv(occ), 0.7), col, (0.08, 0.06, 0.04))
    s.surface(col)


ANGLE = 45.0


def own_unwrap(obj, baked, density, margin=0.012, angle=ANGLE, fixed=None):
    """The "atlas" UV map for bake_atlas over the baked slots only (a slot not baked keeps its 1999 projection and takes
    no room), each slot's islands scaled by `density` (relative texel density) before the final packing. `fixed` maps
    a slot to a function giving each loop's atlas UV from its "fuv" coordinates: those faces are not unwrapped but
    placed there and pinned (shared tiles, such as one painted feather for all the feather cards), and the other
    islands are packed around them."""
    me = obj.data
    names = [m.name for m in me.materials]
    fixed = fixed or {}
    if "atlas" not in me.uv_layers:
        me.uv_layers.new(name="atlas")
    me.uv_layers.active = me.uv_layers["atlas"]
    for p in me.polygons:
        p.select = names[p.material_index] in baked and names[p.material_index] not in fixed
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="FACE")
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin, area_weight=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    uv = me.uv_layers["atlas"].data
    for p in me.polygons:
        k = density.get(names[p.material_index], 1.0) if p.select else 0.0
        for li in p.loop_indices:
            uv[li].uv = uv[li].uv * k if p.select else V((0.999, 0.999))
    if fixed:
        fuv = me.uv_layers["fuv"].data
        for p in me.polygons:
            place = fixed.get(names[p.material_index])
            if place is None:
                continue
            p.select = True
            for li in p.loop_indices:
                uv[li].uv = place(fuv[li].uv)
                uv[li].pin_uv = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.uv.select_all(action="SELECT")
    if fixed:
        bpy.ops.uv.pack_islands(margin=margin, rotate=True, scale=True, pin=True, pin_method="LOCKED")
    else:
        bpy.ops.uv.pack_islands(margin=margin, rotate=True, scale=True)
    bpy.ops.object.mode_set(mode="OBJECT")


def part_copy(model, keep):
    """A copy of the model with only the faces for which `keep(face)` holds."""
    dup = model.copy()
    dup.data = model.data.copy()
    link(dup)
    bm = bmesh.new()
    bm.from_mesh(dup.data)
    bm.faces.ensure_lookup_table()
    flags = [keep(f) for f in bm.faces]
    bmesh.ops.delete(bm, geom=[f for f, k in zip(bm.faces, flags, strict=True) if not k], context="FACES")
    bm.to_mesh(dup.data)
    bm.free()
    return dup


def body_faces(model):
    """Indices of the faces of the largest connected piece (the body without hands, wing bones and membranes)."""
    bm = bmesh.new()
    bm.from_mesh(model.data)
    bm.faces.ensure_lookup_table()
    seen = [False] * len(bm.faces)
    best = []
    for f0 in bm.faces:
        if seen[f0.index]:
            continue
        part, stack = [], [f0]
        seen[f0.index] = True
        while stack:
            f = stack.pop()
            part.append(f.index)
            for e in f.edges:
                for g in e.link_faces:
                    if not seen[g.index]:
                        seen[g.index] = True
                        stack.append(g)
        if len(part) > len(best):
            best = part
    bm.free()
    return set(best)


def uv_mask(obj, size):
    """Pixels (Blender image rows, bottom up) whose centre lies in a triangle of the object's atlas, grown by one pixel."""
    import numpy as np

    mask = np.zeros((size, size), dtype=bool)
    uv = obj.data.uv_layers["atlas"].data
    for p in obj.data.polygons:
        t = np.array([tuple(uv[li].uv) for li in p.loop_indices][:3]) * size - 0.5
        lo = np.maximum(np.floor(t.min(0)).astype(int), 0)
        hi = np.minimum(np.ceil(t.max(0)).astype(int), size - 1)
        if (hi < lo).any():
            continue
        xs, ys = np.meshgrid(np.arange(lo[0], hi[0] + 1), np.arange(lo[1], hi[1] + 1))
        (x0, y0), (x1, y1), (x2, y2) = t
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-12:
            continue
        a = ((y1 - y2) * (xs - x2) + (x2 - x1) * (ys - y2)) / den
        b = ((y2 - y0) * (xs - x2) + (x0 - x2) * (ys - y2)) / den
        inside = (a >= -0.02) & (b >= -0.02) & (1 - a - b >= -0.02)
        mask[ys[inside], xs[inside]] = True
    grown = mask.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        grown |= np.roll(mask, (dy, dx), axis=(0, 1))
    return grown


def bake_parts(model, path, baked, size, rings=24):
    """Bake the body and the other pieces (hands, wing bones, claws, membranes) apart, so they cast no shadows on each
    other (fingers and wing bones hanging by the thighs), merge the two pictures by their UV coverage and grow the
    islands into the empty atlas (edge padding: the converter's downscale to 256 px would otherwise mix the empty pixels
    into the island borders). The bake writes every pixel opaque, so the coverage comes from the UVs. The slots not
    baked are left out (a 1999 wing membrane must not shade the body either). Returns the exporter's slot fields."""
    import numpy as np
    from own_texture import bake_atlas

    names = [m.name for m in model.data.materials]
    body = body_faces(model)
    rgb = np.zeros((size, size, 3), dtype=np.float32)
    filled = np.zeros((size, size), dtype=bool)
    slots = {}
    for k, inside in enumerate((True, False)):
        part = part_copy(
            model, lambda f, inside=inside: (f.index in body) == inside and names[f.material_index] in baked
        )
        tmp = path.with_name(f"{path.stem}_part{k}.png")
        slots.update(bake_atlas(part, tmp, materials=list(baked), size=size))
        mask = uv_mask(part, size) & ~filled
        bpy.data.objects.remove(part)
        img = bpy.data.images.load(str(tmp), check_existing=False)
        px = np.empty(size * size * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        bpy.data.images.remove(img)
        tmp.unlink()
        rgb[mask] = px.reshape(size, size, 4)[mask][:, :3]
        filled |= mask
    for _ in range(rings):
        acc = np.zeros_like(rgb)
        cnt = np.zeros((size, size), dtype=np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            m = np.roll(filled, (dy, dx), axis=(0, 1))
            acc += np.roll(rgb * filled[..., None], (dy, dx), axis=(0, 1)) * m[..., None]
            cnt += m
        grow = (~filled) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        filled |= grow
    rgb[~filled] = rgb[filled].mean(0)
    out = bpy.data.images.new(path.stem, size, size, alpha=True)
    out.pixels.foreach_set(np.concatenate([rgb, np.ones((size, size, 1), dtype=np.float32)], axis=2).ravel())
    out.filepath_raw = str(path)
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    return {n: {"texture": "image", "image": path.name} for n in slots}


# ---------------------------------------------------------------------------- report, render, export


def report(model):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + 1
    print(f"{model.name}: {len(me.vertices)} vertices, {len(me.polygons)} triangles, per slot {per}")


def render(model, out, prefix, frame):
    """Clay renders: `frame` is (centre, ortho size)."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = ARGS.res
    sc.render.resolution_y = ARGS.res
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.3, 0.32, 0.36)
    for loc, e in (((-2, -3, 3), 400), ((3, -1, 2), 200), ((0, 3, 3), 250), ((0, 0, -3), 80)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.energy, ld.size = e, 3
        lo = link(bpy.data.objects.new("l", ld))
        lo.location = loc
        lo.rotation_euler = (V((0, 0, 0)) - V(loc)).to_track_quat("-Z", "Y").to_euler()
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.type = "ORTHO"
    c, size = frame
    views = [
        ("front", c, V((0, -1, 0.0001)), size),
        ("side", c, V((1, 0, 0.0001)), size),
        ("back", c, V((0, 1, 0.0001)), size),
        ("persp", c, V((0.7, -1, 0.35)), size),
    ]
    for name, cc, d, s in views:
        d = d.normalized()
        cam.location = V(cc) + d * 5
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = s
        sc.render.filepath = str(out / f"{prefix}_{name}.png")
        bpy.ops.render.render(write_still=True)


CHARACTERS = {
    "succubus": {
        "build": build_succubus,
        "file": "succubus.json",
        "model": "Succubus",
        "label": "Succubus",
        "joints": SUCC_JOINTS,
        "landmarks": SUCC_LANDMARKS,
        "keep": SUCC_KEEP,
        "slots": {"Wing": {"pcTextures": [4]}},
        "frame": ((0, 0, -0.04), 0.45),
        # the wings keep the 1999 cut-out membranes (ragged hems); body and bones get the own atlas
        "bake": {"Body": 1.0, "Bone": 0.5},
    },
    "queen": {
        "build": build_queen,
        "file": "queen_shillah.json",
        "model": "the queen(shaun)",
        "label": "Queen Shillah",
        "joints": QUEEN_JOINTS,
        "landmarks": QUEEN_LANDMARKS,
        "keep": QUEEN_KEEP,
        "slots": {},
        "frame": ((0, 0, 0.02), 1.4),
        # the 1999 wings are opaque, so the whole body, wings and bones are baked (wings at a lower texel density)
        "bake": {"Body": 1.0, "Wing": 1.0, "Bone": 0.35, "Feather": 1.0, "Gold": 0.4, "Down": 0.6},
        # fewer, larger islands on the lumpy body; the feather cards share painted tiles
        "unwrap": {"angle": 66.0, "margin": 0.008, "fixed": {"Feather": feather_tile}},
    },
}


def dressing(model, key):
    """The procedural materials of the baked slots; a slot that is not baked lets the bake's light through."""
    mats = {m.name: m for m in model.data.materials}
    if key == "succubus":
        succubus_body_material(mats["Body"])
        horn_material(mats["Bone"], (0.035, 0.022, 0.02), (0.16, 0.09, 0.07), 900.0)
    else:
        queen_body_material(mats["Body"])
        queen_body_material(mats["Down"])
        queen_membrane_material(mats["Wing"])
        ivory_material(mats["Bone"])
        feather_card_material(mats["Feather"])
        gold_material(mats["Gold"])


def main():
    out = Path(ARGS.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    which = ["succubus", "queen"] if ARGS.which == "all" else [ARGS.which]
    for key in which:
        ch = CHARACTERS[key]
        model = ch["build"]()
        report(model)
        if ARGS.authored:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            from authored_export import export_authored

            folder = Path(ARGS.authored).resolve()
            dressing(model, key)
            own_unwrap(model, set(ch["bake"]), ch["bake"], **ch.get("unwrap", {}))
            baked = bake_parts(model, folder / ch["file"].replace(".json", ".png"), set(ch["bake"]), ARGS.size)
            export_authored(
                model,
                Path(ARGS.authored).resolve() / ch["file"],
                model=ch["model"],
                label=ch["label"],
                fit="joints",
                slots={**ch["slots"], **baked},
                extra={
                    "landmarks": {n: list(ch["joints"][n]) for n in ch["landmarks"]},
                    "keepPcJoints": ch["keep"],
                },
            )
        bpy.ops.wm.save_as_mainfile(filepath=str(out / f"{key}.blend"))
        if not ARGS.no_render:
            render(model, out, key, ch["frame"])


ARGS = parse_args()
main()
