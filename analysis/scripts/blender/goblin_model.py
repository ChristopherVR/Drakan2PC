"""Build an original Goblin mesh in Blender, headless, for the Order of the Flame converter.

The 1999 PC model `goblin(h)` (Common/NPC/NPCs, 323 polygons at LOD 0; also the Goblin Grenadier) is replaced at convert
time by this mesh: the converter fits it on the PC skeleton and takes UVs, the 1999 textures and the skin weights from
the player's own disc (editor/packages/formats/src/ootfauthored.ts). This script reads no game data: the proportions
below are rounded measurements of the reference (joint positions, limb thickness), like a modeller working from a
picture. The style follows the Drakan 2 goblins: a hunched, muscular body, a blunt frog-like head with a wide mouth,
teeth and a crest of horns, clawed three-fingered hands and three-toed feet, a ragged loincloth.

    blender -b --factory-startup -P analysis/scripts/blender/goblin_model.py -- \
        --out build/authored/goblin-model [--authored scripts/buildtool/kit/models/goblin.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the goblin's right, y toward its back, z up; it faces -Y),
in the 1999 bind pose (arms spread, knees bent, head forward of the shoulders). The `landmarks` are the PC joint
positions, so the converter's joint fit is the identity. The right hand is a fist (it holds the weapon), the left one is
open, as on the 1999 model.

Slots: Torso (entry 0: body and arms), Legs and Head (entry 1), Horn (entries 0 and 1: claws, horns, teeth), Cloth (the loincloth, entries 1 and 2,
where the 1999 loincloth is painted), Fringe (the cut-out webbing under the forearms, entry 2).

The helpers (tube, ellipsoid, cone, remesh_decimate, ...) are shared by tick_model.py and drone_model.py.
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
ROOT = Path(__file__).resolve().parents[3]

# ---------------------------------------------------------------------------- shared helpers


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
    """Dense samples through control tuples (any number of floats each), Catmull-Rom."""
    n = len(ctrl)
    out = []
    for i in range(n - 1):
        p0, p1, p2, p3 = ctrl[max(i - 1, 0)], ctrl[i], ctrl[i + 1], ctrl[min(i + 2, n - 1)]
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


def tube(name, ctrl, sides=12, per=6, up=(0, 0, 1), sq=1.0):
    """Closed tube through control tuples (x, y, z, half width, half height). The section frame is carried along the
    path (parallel transport) from `up`: the half height lies along it, the half width across. `sq` below 1 makes the
    section boxier (superellipse)."""

    def se(v):
        return math.copysign(abs(v) ** sq, v)

    s = catmull(ctrl, per)
    pts = [V(p[:3]) for p in s]
    bm = bmesh.new()
    rings = []
    n = V(up)
    for i, p in enumerate(s):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        n = n - t * n.dot(t)
        if n.length < 1e-6:
            n = t.orthogonal()
        n.normalize()
        side = t.cross(n).normalized()
        rw, rh = max(p[3], 1e-5), max(p[4], 1e-5)
        rings.append(
            [
                bm.verts.new(pts[i] + side * (se(math.cos(a)) * rw) + n * (se(math.sin(a)) * rh))
                for a in (2 * math.pi * k / sides for k in range(sides))
            ]
        )
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    for ring, end in ((rings[0], pts[0]), (rings[-1], pts[-1])):
        c = bm.verts.new(end)
        for k in range(sides):
            bm.faces.new((ring[k], ring[(k + 1) % sides], c))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def ellipsoid(name, c, r, seg=20, rot=None):
    """Ellipsoid at c with radii r; `rot` is an optional mathutils Matrix (3x3) applied to the radii frame."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=seg // 2 + 1, radius=1.0)
    for v in bm.verts:
        q = V((v.co.x * r[0], v.co.y * r[1], v.co.z * r[2]))
        if rot is not None:
            q = rot @ q
        v.co = V(c) + q
    return mesh_from_bm(name, bm)


def cone(name, base, tipp, r, sides=5, rings=1, bend=None):
    """Tapered closed spike from `base` (radius r) to `tipp`; `bend` curves it (offset of the middle)."""
    base, tipp = V(base), V(tipp)
    bm = bmesh.new()
    axis = tipp - base
    t = axis.normalized()
    s = t.orthogonal().normalized()
    u = s.cross(t).normalized()
    loops = []
    for i in range(rings):
        f = i / rings
        c = base + axis * f
        if bend is not None:
            c = c + V(bend) * (4 * f * (1 - f))
        rr = r * (1 - f) ** 0.8
        loops.append(
            [
                bm.verts.new(c + (s * math.cos(a) + u * math.sin(a)) * rr)
                for a in (2 * math.pi * k / sides for k in range(sides))
            ]
        )
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


def remesh(obj, voxel, smooth=4, factor=0.5):
    rm = obj.modifiers.new("remesh", "REMESH")
    rm.mode = "VOXEL"
    rm.voxel_size = voxel
    rm.adaptivity = 0.0
    apply_modifiers(obj)
    if smooth:
        sm = obj.modifiers.new("smooth", "SMOOTH")
        sm.factor = factor
        sm.iterations = smooth
        apply_modifiers(obj)
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


def remesh_decimate(obj, voxel, tris, smooth=4):
    return decimate(remesh(obj, voxel, smooth), tris)


def sculpt(obj, fn):
    """Move each vertex by fn(co, normal) -> offset Vector (or None)."""
    me = obj.data
    me.update()
    for v in me.vertices:
        d = fn(v.co.copy(), v.normal.copy())
        if d is not None:
            v.co += d
    me.update()


def dist_polyline(p, pts):
    best = 1e9
    for a, b in itertools.pairwise(pts):
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
        best = min(best, (a + ab * t - p).length)
    return best


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


def card(name, top, bottom, two_sided=True):
    """Strip between two point rows (same length); both sides when `two_sided` (cut-out cards)."""
    bm = bmesh.new()
    a = [bm.verts.new(V(p)) for p in top]
    b = [bm.verts.new(V(p)) for p in bottom]
    for i in range(len(a) - 1):
        bm.faces.new((a[i], b[i], b[i + 1], a[i + 1]))
    if two_sided:
        # the back side sits a hair behind the front: the exporter merges coincident vertices, which would fold the two
        # sides into one
        n = (V(top[-1]) - V(top[0])).cross(V(bottom[0]) - V(top[0])).normalized() * 0.0004
        a2 = [bm.verts.new(V(p) + n) for p in top]
        b2 = [bm.verts.new(V(p) + n) for p in bottom]
        for i in range(len(a2) - 1):
            bm.faces.new((a2[i + 1], b2[i + 1], b2[i], a2[i]))
    return mesh_from_bm(name, bm)


def make_mats(spec):
    mats = {}
    for name, col in spec:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.6
        m.use_backface_culling = False
        mats[name] = m
    return mats


def set_material(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.material_index = 0
    return obj


def split_material(obj, mat_a, mat_b, pred):
    """Two slots on one part: faces whose centre satisfies `pred` take `mat_a`, the rest `mat_b`."""
    obj.data.materials.clear()
    obj.data.materials.append(mat_a)
    obj.data.materials.append(mat_b)
    for p in obj.data.polygons:
        p.material_index = 0 if pred(p.center) else 1
    return obj


def combine(objs, mats, order, name):
    """Join parts into one object with the material slots in `order`, triangulated, smooth shaded."""
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
    for p, i in zip(model.data.polygons, idx):
        p.material_index = i
    model.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(model)
    return model


def report(model, label):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + 1
    print(f"{label}: {len(me.vertices)} vertices, {len(me.polygons)} triangles, per slot {per}")


def render_views(out: Path, views, res=800, bg=(0.30, 0.32, 0.36)):
    """Clay renders: views = [(name, centre, direction, ortho scale)]."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x = res
    sc.render.resolution_y = res
    sc.world = bpy.data.worlds.new("w")
    sc.world.color = bg
    for loc, e in (((-2, -3, 3), 8), ((3, -1, 2), 4), ((0, 3, 3), 5), ((0, 0, -3), 2)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.energy, ld.size = e, 1.5
        lo = link(bpy.data.objects.new("l", ld))
        lo.location = V(loc) * 0.4
        lo.rotation_euler = (V((0, 0, 0)) - V(loc)).to_track_quat("-Z", "Y").to_euler()
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.clip_start = 0.001
    for name, c, d, scale in views:
        d = V(d).normalized()
        cam.location = V(c) + d * 2
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
        sc.render.filepath = str(out / f"render_{name}.png")
        bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------------------- goblin proportions

# PC joint positions (Blender axes, model units, 4 decimals so the joint fit stays the identity).
JOINTS = {
    "root": (0.0, 0.0, 0.0),
    "chest": (0.0, -0.021, 0.0499),
    "neckbase": (0.0, -0.0547, 0.1106),
    "head": (0.0, -0.1183, 0.0937),
    "rforearm": (0.1589, -0.0612, 0.1099),
    "rhand": (0.2785, -0.0732, 0.1012),
    "midlend": (-0.3527, -0.0677, 0.1025),
    "rcalf": (0.0332, -0.0429, -0.0766),
    "rheel": (0.0329, -0.0027, -0.1109),
    "rtoe": (0.0354, -0.0169, -0.1320),
}

BUDGET = {"body": 900, "head": 480, "hand": 110, "foot": 85, "cloth": 220}

# the mouth line (right half, front to corner) and where the eyes sit (the 1999 face paints them near the snout top)
MOUTH = [V((0.0, -0.2, 0.069)), V((0.015, -0.193, 0.069)), V((0.027, -0.172, 0.067)), V((0.033, -0.145, 0.066))]
MOUTH += [V((0.035, -0.118, 0.068))]
EYE = V((0.021, -0.170, 0.097))


def build_body():
    """Torso, neck, arms and legs as one skin (voxel remesh)."""
    parts = [
        ellipsoid("pelvis", (0, 0.002, -0.004), (0.037, 0.029, 0.03)),
        ellipsoid("belly", (0, -0.014, 0.028), (0.031, 0.028, 0.03)),
        ellipsoid("ribs", (0, -0.03, 0.066), (0.044, 0.037, 0.038)),
        ellipsoid("hump", (0, -0.046, 0.099), (0.056, 0.034, 0.024)),
        ellipsoid("traps", (0, -0.042, 0.109), (0.036, 0.028, 0.014)),
        ellipsoid("lats", (0, -0.03, 0.08), (0.05, 0.03, 0.03)),
        tube(
            "neck",
            [(0, -0.044, 0.096, 0.028, 0.026), (0, -0.075, 0.088, 0.027, 0.026), (0, -0.1, 0.08, 0.026, 0.025)],
        ),
    ]
    for sx in (1, -1):
        parts += [
            ellipsoid("pec", (sx * 0.022, -0.064, 0.083), (0.024, 0.013, 0.02)),
            ellipsoid("delt", (sx * 0.056, -0.056, 0.107), (0.022, 0.019, 0.018)),
            ellipsoid("glute", (sx * 0.019, 0.016, -0.01), (0.021, 0.017, 0.022)),
        ]
        x = sx
        parts.append(
            tube(
                "arm",
                [
                    (x * 0.045, -0.055, 0.107, 0.016, 0.016),
                    (x * 0.085, -0.056, 0.108, 0.016, 0.016),
                    (x * 0.13, -0.058, 0.109, 0.012, 0.0115),
                    (x * 0.159, -0.0615, 0.1085, 0.0105, 0.0105),
                    (x * 0.19, -0.064, 0.1065, 0.0135, 0.012),
                    (x * 0.235, -0.068, 0.104, 0.011, 0.0095),
                    (x * 0.27, -0.072, 0.1015, 0.0078, 0.0068),
                ],
                sides=14,
            )
        )
        parts.append(
            tube(
                "leg",
                [
                    (x * 0.017, 0.002, 0.004, 0.018, 0.018),
                    (x * 0.024, -0.016, -0.03, 0.0165, 0.017),
                    (x * 0.03, -0.032, -0.06, 0.0145, 0.015),
                    (x * 0.033, -0.042, -0.077, 0.0125, 0.013),
                    (x * 0.033, -0.026, -0.093, 0.0125, 0.012),
                    (x * 0.033, -0.009, -0.106, 0.0095, 0.0095),
                    (x * 0.033, -0.002, -0.113, 0.009, 0.009),
                ],
                sides=14,
                up=(0, -1, 0),
            )
        )
        # calf muscle behind the shin
        parts.append(ellipsoid("calf", (x * 0.033, -0.022, -0.089), (0.012, 0.01, 0.015)))
        # knee cap
        parts.append(ellipsoid("knee", (x * 0.033, -0.051, -0.074), (0.01, 0.008, 0.01)))
    obj = join(parts, "body")
    remesh(obj, 0.0022, smooth=6)
    # backbone groove and a crease under the pectorals
    spine = [V((0, 0.025, 0.0)), V((0, 0.02, 0.04)), V((0, -0.01, 0.09)), V((0, -0.04, 0.122))]

    def carve(p, n):
        d = dist_polyline(p, spine)
        off = -n * 0.0016 * math.exp(-((d / 0.004) ** 2))
        # ribs: shallow horizontal grooves on the flanks
        if abs(p.x) > 0.028 and 0.045 < p.z < 0.085:
            off += -n * 0.0008 * max(0.0, math.sin((p.z - 0.045) / 0.012 * math.pi)) ** 2
        return off

    sculpt(obj, carve)
    return decimate(obj, BUDGET["body"])


def build_head():
    """Blunt frog-like skull with a wide mouth, throat pouch, cheek ridges, eye bulges under heavy brows, nostrils."""
    parts = [
        # a blunt wedge lofted from the nape to the snout: (y, half width, top, bottom)
        tube(
            "skull",
            [
                (0, y, (top + bot) / 2, hw, (top - bot) / 2)
                for y, hw, top, bot in (
                    (-0.084, 0.028, 0.108, 0.036),
                    (-0.105, 0.033, 0.112, 0.034),
                    (-0.13, 0.035, 0.112, 0.039),
                    (-0.155, 0.032, 0.108, 0.045),
                    (-0.176, 0.027, 0.1, 0.051),
                    (-0.19, 0.02, 0.091, 0.056),
                    (-0.197, 0.011, 0.082, 0.062),
                )
            ],
            sides=20,
            per=5,
            sq=0.7,
        ),
    ]
    for sx in (1, -1):
        e = V((sx * EYE.x, EYE.y, EYE.z))
        parts += [
            ellipsoid("eye", e, (0.0095, 0.0095, 0.0085)),
            ellipsoid("brow", e + V((sx * 0.001, 0.003, 0.007)), (0.012, 0.012, 0.0055)),
            ellipsoid("cheek", (sx * 0.031, -0.122, 0.078), (0.007, 0.028, 0.01)),
        ]
    obj = join(parts, "head")
    remesh(obj, 0.0015, smooth=8)
    right = MOUTH
    left = [V((-p.x, p.y, p.z)) for p in MOUTH]
    nostrils = [V((0.008, -0.197, 0.082)), V((-0.008, -0.197, 0.082))]

    def carve(p, n):
        d = min(dist_polyline(p, right), dist_polyline(p, left))
        off = -n * 0.005 * math.exp(-((d / 0.0018) ** 2))
        for q in nostrils:
            dn = (p - q).length
            off += -n * 0.0025 * math.exp(-((dn / 0.0025) ** 2))
        # eye socket ring: a groove around each eye bulge
        for sx in (1, -1):
            de = (p - V((sx * EYE.x, EYE.y, EYE.z))).length
            off += -n * 0.0012 * math.exp(-(((de - 0.0105) / 0.0016) ** 2))
        return off

    sculpt(obj, carve)
    return decimate(obj, BUDGET["head"])


def build_hand_open(sx):
    """Left-style open hand: palm, three clawed fingers and a thumb (flat, as in the T pose)."""
    x = sx
    parts = [ellipsoid("palm", (x * 0.292, -0.075, 0.1005), (0.02, 0.017, 0.0075))]
    fingers = [(-0.062, 0.385), (-0.073, 0.395), (-0.085, 0.383)]
    tips = []
    for y, tipx in fingers:
        a = V((x * 0.302, y, 0.101))
        b = V((x * tipx, y + (y + 0.073) * 0.35, 0.0995))
        m = a.lerp(b, 0.5) + V((0, 0, 0.0015))
        parts.append(
            tube(
                "finger",
                [(*a, 0.0058, 0.0048), (*m, 0.0048, 0.0042), (*b, 0.0033, 0.003)],
                sides=8,
                per=4,
            )
        )
        tips.append((b, (b - m).normalized()))
    ta = V((x * 0.286, -0.087, 0.1))
    tb = V((x * 0.322, -0.101, 0.0995))
    parts.append(
        tube("thumb", [(*ta, 0.006, 0.005), (*ta.lerp(tb, 0.5), 0.005, 0.0045), (*tb, 0.0035, 0.003)], sides=8)
    )
    tips.append((tb, (tb - ta).normalized()))
    hand = remesh_decimate(join(parts, "hand"), 0.0012, BUDGET["hand"], smooth=3)
    claws = [cone("claw", t - d * 0.002, t + d * 0.011 + V((0, 0, -0.003)), 0.0028, sides=5) for t, d in tips]
    return hand, claws


def build_fist(sx):
    """Right-style fist around the weapon grip: knuckles forward, thumb wrapped over."""
    x = sx
    parts = [ellipsoid("fist", (x * 0.296, -0.071, 0.098), (0.024, 0.017, 0.013))]
    knuckles = []
    for k, y in enumerate((-0.062, -0.072, -0.082)):
        c = V((x * 0.318, y, 0.101 + 0.001 * k))
        parts.append(ellipsoid("knuckle", c, (0.008, 0.0055, 0.0075)))
        parts.append(
            tube(
                "curl",
                [(*c, 0.005, 0.005), (x * 0.322, y, 0.092, 0.005, 0.005), (x * 0.312, y, 0.087, 0.0045, 0.0045)],
                sides=8,
            )
        )
        knuckles.append(V((x * 0.309, y, 0.085)))
    parts.append(
        tube(
            "thumb",
            [
                (x * 0.283, -0.085, 0.1, 0.006, 0.0055),
                (x * 0.3, -0.092, 0.097, 0.0055, 0.005),
                (x * 0.314, -0.088, 0.093, 0.004, 0.004),
            ],
            sides=8,
        )
    )
    hand = remesh_decimate(join(parts, "fist"), 0.0012, BUDGET["hand"], smooth=3)
    claws = [
        cone("claw", k + V((x * 0.002, 0, 0.002)), k + V((-x * 0.006, 0, -0.006)), 0.0026, sides=5) for k in knuckles
    ]
    return hand, claws


def build_foot(sx):
    """Three long toes forward, a heel spur back; flat sole at the 1999 floor (z = -0.139)."""
    x = sx
    parts = [
        ellipsoid("foot", (x * 0.034, -0.018, -0.129), (0.015, 0.024, 0.0085)),
        ellipsoid("heel", (x * 0.033, 0.003, -0.126), (0.01, 0.01, 0.01)),
    ]
    toes = [(0.022, -0.058, 0.014), (0.036, -0.068, 0.035), (0.05, -0.058, 0.056)]
    tips = []
    for x0, ty, tx in toes:
        a = V((x * (0.024 + (x0 - 0.022) * 0.5), -0.03, -0.131))
        b = V((x * tx, ty, -0.1355))
        m = a.lerp(b, 0.55) + V((0, 0, 0.0028))
        parts.append(tube("toe", [(*a, 0.0068, 0.0058), (*m, 0.0058, 0.005), (*b, 0.0042, 0.0036)], sides=8, per=4))
        tips.append((b, (b - m).normalized()))
    foot = join(parts, "foot")
    remesh(foot, 0.0013, smooth=3)
    sculpt(foot, lambda p, n: V((0, 0, -0.139 - p.z)) if p.z < -0.139 else None)
    decimate(foot, BUDGET["foot"])
    claws = [cone("claw", t - d * 0.002, t + d * 0.01 + V((0, 0, -0.0035)), 0.0032, sides=5) for t, d in tips]
    claws.append(cone("spur", V((x * 0.033, 0.009, -0.124)), V((x * 0.034, 0.019, -0.132)), 0.0028, sides=5))
    return foot, claws


def build_teeth():
    """Upper teeth hanging over the lip line, longer fangs at the front, a few lower teeth pointing up."""
    out = []
    for sx in (1, -1):
        for k, t in enumerate((0.06, 0.2, 0.36, 0.52, 0.68, 0.84)):
            # point along the mouth polyline (by index blend)
            f = t * (len(MOUTH) - 1)
            i = min(int(f), len(MOUTH) - 2)
            p = MOUTH[i].lerp(MOUTH[i + 1], f - i)
            p = V((sx * p.x, p.y, p.z))
            outward = V((sx * 0.6, -1.0 if t < 0.2 else -0.25, 0)).normalized()
            base = p + outward * 0.0022 + V((0, 0, 0.0015))
            length = 0.0085 if k == 1 else 0.0058
            out.append(cone("tooth", base, base + V((0, 0, -length)) + outward * 0.0008, 0.0019, sides=4, rings=1))
        for t in (0.15, 0.4):
            f = t * (len(MOUTH) - 1)
            i = min(int(f), len(MOUTH) - 2)
            p = MOUTH[i].lerp(MOUTH[i + 1], f - i)
            p = V((sx * p.x, p.y, p.z))
            outward = V((sx * 0.6, -1.0, 0)).normalized()
            base = p + outward * 0.0022 + V((0, 0, -0.002))
            out.append(cone("ltooth", base, base + V((0, 0, 0.0065)), 0.0019, sides=4, rings=1))
    return out


def build_horns():
    """A crest of swept-back horns on the skull and short spines down the hump (Drakan 2 goblin style)."""
    out = []
    for k, (y, z, ln) in enumerate(((-0.152, 0.111, 0.016), (-0.134, 0.1125, 0.02), (-0.116, 0.111, 0.017))):
        b = V((0, y, z - 0.002))
        out.append(
            cone("horn", b, b + V((0, 0.55, 0.85)).normalized() * ln, 0.0042, sides=6, rings=2, bend=(0, 0.003, 0))
        )
        for sx in (1, -1):
            if k < 2:
                b2 = V((sx * 0.016, y + 0.006, z - 0.004))
                out.append(cone("horn", b2, b2 + V((sx * 0.4, 0.6, 0.6)).normalized() * ln * 0.6, 0.003, sides=5))
    for y, z in ((-0.062, 0.124), (-0.043, 0.123), (-0.024, 0.115)):
        b = V((0, y, z - 0.003))
        out.append(cone("spine", b, b + V((0, 0.5, 0.85)).normalized() * 0.01, 0.0035, sides=5))
    return out


def build_cloth():
    """Ragged loincloth: front and back flaps from a belt at the hips, slit up the sides, plus a rolled belt."""
    na, rows = 36, 6
    bm = bmesh.new()
    grid = []
    for r in range(rows + 1):
        f = r / rows
        row = []
        for a in range(na):
            th = 2 * math.pi * a / na
            s, c = math.sin(th), math.cos(th)
            hem = -0.03 - (0.032 if c > 0 else 0.022) * abs(c) ** 1.5
            # ragged hem: teeth every other column
            if r == rows:
                hem += 0.004 * (a % 2) - 0.002
            z = 0.009 + (hem - 0.009) * f
            rx = 0.0395 + 0.009 * f
            ry_front = 0.032 + 0.018 * f
            ry_back = 0.032 + 0.0075 * f
            ry = ry_front if c > 0 else ry_back
            row.append(bm.verts.new((rx * s, -ry * c + 0.002 * (1 - f), z)))
        grid.append(row)
    for r in range(rows):
        for a in range(na):
            a1 = (a + 1) % na
            bm.faces.new((grid[r][a], grid[r][a1], grid[r + 1][a1], grid[r + 1][a]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    obj = mesh_from_bm("cloth", bm)
    # the faces are outward; thicken inward
    so = obj.modifiers.new("solid", "SOLIDIFY")
    so.thickness = 0.0022
    so.offset = -1.0
    apply_modifiers(obj)
    belt = tube(
        "belt",
        [
            (
                0.0405 * math.sin(2 * math.pi * k / 16),
                -0.0335 * math.cos(2 * math.pi * k / 16) + 0.001,
                0.009,
                0.0032,
                0.0028,
            )
            for k in range(18)
        ],
        sides=6,
        per=2,
        up=(0, 0, 1),
    )
    knot = ellipsoid("knot", (0.02, -0.037, 0.006), (0.006, 0.004, 0.006), seg=10)
    flap = tube("tail", [(0.02, -0.04, 0.004, 0.003, 0.0018), (0.024, -0.042, -0.012, 0.0025, 0.0015)], sides=6, per=2)
    obj = join([obj, belt, knot, flap], "cloth")
    return decimate(obj, BUDGET["cloth"])


def build_fringe():
    """The cut-out webbing hanging under each forearm (the 1999 card, drawn from the same texture)."""
    out = []
    for sx in (1, -1):
        top, bottom = [], []
        n = 6
        for i in range(n + 1):
            t = i / n
            x = 0.165 + 0.105 * t
            zt = 0.1 - 0.006 * t
            y = -0.0625 - 0.008 * t
            depth = 0.042 * (1 - abs(2 * t - 1)) ** 0.8 + 0.006
            top.append((sx * x, y, zt))
            bottom.append((sx * (x + 0.004), y + 0.002, zt - depth))
        out.append(card("fringe", top, bottom))
    # the webbing hanging under the throat, along the middle
    top = [(0, -0.126 + 0.011 * i, 0.04 + 0.0018 * i) for i in range(6)]
    bottom = [(0, y - 0.003, z - 0.03 * math.sin(math.pi * (0.15 + 0.7 * i / 5))) for i, (_, y, z) in enumerate(top)]
    out.append(card("throat", top, bottom))
    return out


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats(
        (
            ("Torso", (0.42, 0.47, 0.36)),
            ("Legs", (0.38, 0.44, 0.34)),
            ("Head", (0.5, 0.52, 0.42)),
            ("Horn", (0.6, 0.58, 0.48)),
            ("Cloth", (0.36, 0.22, 0.16)),
            ("Fringe", (0.55, 0.55, 0.4)),
        )
    )
    body = build_body()
    # torso and arms take the 1999 body sheet, the legs (below the belt) the leg and head sheet
    split_material(body, mats["Torso"], mats["Legs"], lambda c: c.z > 0.004)
    objs = [body, set_material(build_head(), mats["Head"])]
    for sx in (1, -1):
        hand, claws = build_fist(sx) if sx > 0 else build_hand_open(sx)
        foot, fclaws = build_foot(sx)
        objs += [set_material(hand, mats["Torso"]), set_material(foot, mats["Legs"])]
        objs += [set_material(o, mats["Horn"]) for o in [*claws, *fclaws]]
    objs += [set_material(o, mats["Horn"]) for o in build_teeth() + build_horns()]
    objs.append(set_material(build_cloth(), mats["Cloth"]))
    objs += [set_material(o, mats["Fringe"]) for o in build_fringe()]
    return combine(objs, mats, ["Torso", "Legs", "Head", "Horn", "Cloth", "Fringe"], "goblin")


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/authored/goblin-model"))
    ap.add_argument("--authored", help="write the mesh as an authored model for the converter (.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=800)
    return ap.parse_args(argv)


def main():
    args = parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = build()
    report(model, "goblin")
    if args.authored:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from authored_export import export_authored

        export_authored(
            model,
            Path(args.authored).resolve(),
            model="goblin(h)",
            label="Goblin",
            fit="joints",
            slots={
                "Torso": {"pcTextures": [0]},
                "Legs": {"pcTextures": [1]},
                "Head": {"pcTextures": [1]},
                "Horn": {"pcTextures": [0, 1]},
                "Cloth": {"pcTextures": [1, 2]},
                "Fringe": {"pcTextures": [2]},
            },
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(out / "goblin.blend"))
    if not args.no_render:
        render_views(
            out,
            [
                ("front", (0, -0.05, 0.0), (0, -1, 0.0001), 0.3),
                ("side", (0, -0.06, 0.0), (1, 0, 0.0001), 0.3),
                ("tq", (0, -0.06, 0.0), (0.8, -1, 0.4), 0.3),
                ("back", (0, -0.03, 0.0), (-0.3, 1, 0.3), 0.3),
                ("full", (0, -0.05, 0.0), (0.25, -1, 0.35), 0.8),
                ("head", (0, -0.14, 0.08), (0.7, -1, 0.35), 0.13),
                ("headside", (0, -0.14, 0.075), (1, 0, 0.0001), 0.13),
                ("hands", (0, -0.07, 0.1), (0, 0, 1), 0.8),
                ("feet", (0, -0.03, -0.12), (0.6, -1, 0.3), 0.14),
            ],
            res=args.res,
        )


if __name__ == "__main__":
    main()
