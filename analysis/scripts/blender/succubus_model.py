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
    body = remesh_decimate(join(parts, "queen_body"), 0.0022, 1250)
    set_material(body, mats["Body"])
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


def build_queen():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats({"Body": (0.62, 0.55, 0.52), "Wing": (0.35, 0.22, 0.2), "Bone": (0.5, 0.45, 0.4)})
    objs = queen_body(mats)
    for s in "RL":
        objs += queen_wing(s, mats)
    return finish(objs, "queen_shillah", ["Body", "Wing", "Bone"], mats)


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
        "slots": {"Wing": {"pcTextures": [4]}, "Bone": {"pcTextures": [4]}},
        "frame": ((0, 0, -0.04), 0.45),
    },
    "queen": {
        "build": build_queen,
        "file": "queen_shillah.json",
        "model": "the queen(shaun)",
        "label": "Queen Shillah",
        "joints": QUEEN_JOINTS,
        "landmarks": QUEEN_LANDMARKS,
        "keep": QUEEN_KEEP,
        "slots": {"Body": {"pcTextures": [1]}, "Wing": {"pcTextures": [0, 2]}, "Bone": {"pcTextures": [0]}},
        "frame": ((0, 0, 0.02), 1.4),
    },
}


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

            export_authored(
                model,
                Path(ARGS.authored).resolve() / ch["file"],
                model=ch["model"],
                label=ch["label"],
                fit="joints",
                slots=ch["slots"],
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
