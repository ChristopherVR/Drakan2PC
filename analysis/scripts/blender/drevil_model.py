"""Build an original Dr. Evil (the Order of the Flame final boss, a giant horned serpent) in Blender, headless.

The 1999 PC model `drevil(h)` (Common/NPC/NPCs, 539 polygons) is replaced at convert time by this mesh: the converter
fits it on the PC skeleton (a nine-joint spine, the head, a two-joint jaw and a three-joint tongue) and takes UVs,
textures (the 1999 scaled skin, the red mouth and the cut-out teeth) and skin weights from the player's own disc
(editor/packages/formats/src/ootfauthored.ts). This script reads no game data: the segmented body with its belly
plates, ring ridges and dorsal spines, the long horned head with three eyes a side, the open jaw with its teeth and the
tongue are lofted from rounded proportions measured on the reference (joint positions, heights and widths), merged
per part with a voxel remesh and decimated to a triangle budget.

    blender -b --factory-startup -P analysis/scripts/blender/drevil_model.py -- \
        --out build/authored/drevil-model [--authored scripts/buildtool/kit/models/drevil.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the side, y toward the tail, z up; the serpent faces -Y and
is about 14 units long), so the `landmarks` are the PC joint positions and the converter's joint fit is the identity.
Outputs under --out (gitignored): drevil.blend and clay renders render_<view>.png.
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
    ap.add_argument("--out", default="build/authored/drevil-model")
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

# Body sections from the tail end to the neck: (y, centre z, half width, half height). The 1999 body is a seven-sided
# tube, tallest at the buried end (1.6 units) and tapering to the neck (0.8 units).
BODY = [
    (0.02, 0.175, 0.50, 0.76),
    (-0.10, 0.175, 0.56, 0.82),
    (-2.0, 0.215, 0.52, 0.75),
    (-3.5, 0.240, 0.50, 0.69),
    (-5.0, 0.245, 0.48, 0.63),
    (-6.5, 0.265, 0.45, 0.56),
    (-8.0, 0.290, 0.42, 0.52),
    (-9.0, 0.300, 0.37, 0.45),
    (-10.0, 0.320, 0.34, 0.39),
    (-10.7, 0.400, 0.33, 0.38),
    (-11.2, 0.480, 0.30, 0.34),
]

# Upper head (skull and snout): (y, top, bottom, half width). Lower jaw: the same, open about 0.15 at the front.
SKULL = [
    (-10.30, 0.70, 0.12, 0.30),
    (-10.70, 0.90, 0.10, 0.39),
    (-11.05, 1.02, 0.28, 0.46),
    (-11.60, 0.98, 0.34, 0.44),
    (-12.10, 0.90, 0.37, 0.38),
    (-12.60, 0.83, 0.36, 0.34),
    (-13.10, 0.75, 0.34, 0.29),
    (-13.50, 0.65, 0.32, 0.23),
    (-13.78, 0.54, 0.32, 0.14),
    (-13.90, 0.46, 0.33, 0.05),
]
JAW = [
    (-10.75, 0.26, -0.08, 0.30),
    (-11.10, 0.26, -0.24, 0.41),
    (-11.60, 0.20, -0.23, 0.38),
    (-12.20, 0.13, -0.19, 0.33),
    (-12.80, 0.10, -0.15, 0.28),
    (-13.30, 0.10, -0.12, 0.23),
    (-13.68, 0.12, -0.09, 0.15),
    (-13.86, 0.15, -0.03, 0.05),
]

BUDGET = {"body": 1150, "head": 760, "horns": 240, "spines": 200, "teeth": 140}

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


def _angles(n):
    return [2 * math.pi * k / n for k in range(n)]


def yloft(name, sections, sides=24, per=4, flat=0.0, sq=1.0):
    """Closed body along -Y through (y, centre z, half width, half height) sections; `flat` flattens the underside."""
    samples = catmull(sections, per)
    bm = bmesh.new()
    rings = []
    for y, cz, rx, rz in samples:
        ring = []
        for a in _angles(sides):
            c, s = math.cos(a), math.sin(a)
            c = math.copysign(abs(c) ** sq, c)
            s = math.copysign(abs(s) ** sq, s)
            if s < 0:
                s *= 1.0 - flat
            ring.append(bm.verts.new((c * max(rx, 1e-4), y, cz + s * max(rz, 1e-4))))
        rings.append(ring)
    for r0, r1 in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0[k], r0[k1], r1[k1], r1[k]))
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def head_sections(rows):
    """(y, top, bottom, half width) rows as yloft sections."""
    return [(y, (t + b) / 2, w, (t - b) / 2) for y, t, b, w in rows]


def body_at(y):
    """Interpolated body section (centre z, half width, half height) at y."""
    for a, b in itertools.pairwise(BODY):
        if b[0] <= y <= a[0]:
            f = (a[0] - y) / (a[0] - b[0])
            return tuple(a[k] + (b[k] - a[k]) * f for k in (1, 2, 3))
    s = BODY[0] if y > BODY[0][0] else BODY[-1]
    return s[1], s[2], s[3]


def ellipsoid(name, c, r, seg=16):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=seg // 2 + 1, radius=1.0)
    for v in bm.verts:
        v.co = V((c[0] + v.co.x * r[0], c[1] + v.co.y * r[1], c[2] + v.co.z * r[2]))
    return mesh_from_bm(name, bm)


def horn(name, pts, r0, sides=7, per=3, flat=1.0):
    """Tapered curved horn through points (first is the base), ending in a point; `flat` squashes it sideways."""
    samples = catmull([tuple(p) for p in pts], per)
    ps = [V(s) for s in samples]
    bm = bmesh.new()
    rings = []
    n = len(ps)
    for i, p in enumerate(ps[:-1]):
        t = (ps[min(i + 1, n - 1)] - ps[max(i - 1, 0)]).normalized()
        ref = UP if abs(t.dot(UP)) < 0.9 else V((1, 0, 0))
        s = t.cross(ref).normalized()
        u = s.cross(t).normalized()
        r = r0 * (1 - i / (n - 1)) ** 0.9
        rings.append([bm.verts.new(p + (s * math.cos(a) * flat + u * math.sin(a)) * r) for a in _angles(sides)])
    apex = bm.verts.new(ps[-1])
    for r0_, r1_ in itertools.pairwise(rings):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((r0_[k], r0_[k1], r1_[k1], r1_[k]))
    for k in range(sides):
        bm.faces.new((rings[-1][k], rings[-1][(k + 1) % sides], apex))
    bm.faces.new(list(reversed(rings[0])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return mesh_from_bm(name, bm)


def ring_tube(name, path, radius, sides=5):
    """Closed ring along a closed path of points."""
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
    return decimate(obj, tris)


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
    "Body": (0.18, 0.17, 0.08),
    "Head": (0.20, 0.20, 0.09),
    "Mouth": (0.30, 0.05, 0.04),
    "Horn": (0.45, 0.42, 0.30),
    "Teeth": (0.80, 0.78, 0.65),
}
ORDER = list(SLOT_COLOURS)


def make_mats():
    mats = {}
    for name, col in SLOT_COLOURS.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*col, 1.0)
        b.inputs["Roughness"].default_value = 0.6
        mats[name] = m
    return mats


# ---------------------------------------------------------------------------- parts


def build_body():
    """The serpent body: a tall tube with a flattened belly, ring ridges every half unit and belly plates."""
    parts = [yloft("body", BODY, sides=28, per=4, flat=0.25, sq=0.9)]
    y = -0.45
    k = 0
    while y > -10.4:
        cz, rx, rz = body_at(y)
        grow = 1.0 + (0.035 if k % 2 == 0 else 0.02)
        path = []
        for a in _angles(28):
            c, s = math.cos(a), math.sin(a)
            c = math.copysign(abs(c) ** 0.9, c)
            s = math.copysign(abs(s) ** 0.9, s)
            if s < 0:
                s *= 0.75
            # the ridge leans back a little on top, like overlapping scale rings
            path.append(V((c * rx * grow, y + 0.08 * max(s, 0.0), cz + s * rz * grow)))
        parts.append(ring_tube("ring", path, 0.045 * (rz / 0.6) ** 0.5, sides=5))
        y -= 0.52
        k += 1
    body = join(parts, "body")
    return remesh_decimate(body, 0.03, BUDGET["body"], smooth=2)


def build_spines():
    """A row of back-swept dorsal spines from the buried end to the neck, smaller toward the head."""
    parts = []
    y = -0.6
    while y > -10.3:
        cz, rx, rz = body_at(y)
        top = cz + rz
        f = -y / 10.3
        h = 0.34 - 0.16 * f
        base = V((0.0, y, top - 0.06))
        parts.append(
            horn("spine", [base, base + V((0, 0.10, h * 0.6)), base + V((0, 0.30, h))], 0.09 - 0.03 * f, 5, 2, 0.45)
        )
        y -= 1.04
    return decimate(join(parts, "spines"), BUDGET["spines"])


def build_head():
    """Skull and snout, the open lower jaw and the mouth lining, brow ridges over three eyes a side, nostrils."""
    parts = [
        yloft("skull", head_sections(SKULL), sides=24, per=4, sq=0.85),
        yloft("jaw", head_sections(JAW), sides=20, per=4, sq=0.8),
        # the throat and palate between the jaws, set back from their edges
        yloft(
            "lining",
            [
                (-11.2, 0.28, 0.22, 0.11),
                (-12.2, 0.25, 0.15, 0.09),
                (-13.2, 0.23, 0.10, 0.07),
                (-13.6, 0.23, 0.05, 0.05),
            ],
            sides=14,
            per=3,
        ),
        ellipsoid("cheekL", (0.36, -11.35, 0.40), (0.14, 0.34, 0.20)),
        ellipsoid("cheekR", (-0.36, -11.35, 0.40), (0.14, 0.34, 0.20)),
    ]
    for sx in (1, -1):
        # brow ridge over the row of eyes and the three eyes, highest at the back
        parts.append(ellipsoid("brow", (0.29 * sx, -12.85, 0.80), (0.07, 0.50, 0.05)))
        for k, (y, z, w) in enumerate(((-12.55, 0.68, 0.345), (-12.88, 0.64, 0.32), (-13.20, 0.60, 0.285))):
            r = 0.085 - 0.01 * k
            parts.append(ellipsoid("eye", (w * sx, y, z), (r * 0.7, r, r * 0.8), seg=12))
        parts.append(ellipsoid("nostril", (0.08 * sx, -13.80, 0.52), (0.05, 0.07, 0.04), seg=10))
        parts.append(ellipsoid("chin", (0.22 * sx, -11.5, -0.18), (0.12, 0.40, 0.08)))
    head = join(parts, "head")
    return remesh_decimate(head, 0.022, BUDGET["head"], smooth=2)


def build_horns():
    """Two great horns swept back over the neck, a smaller pair below them, a nose horn, cheek and chin spikes."""
    parts = []
    for sx in (1, -1):
        parts.append(
            horn(
                "horn",
                [V((0.20 * sx, -11.15, 0.92)), V((0.25 * sx, -10.90, 1.18)), V((0.29 * sx, -10.55, 1.34))],
                0.12,
            )
        )
        parts.append(
            horn(
                "horn2",
                [V((0.34 * sx, -11.35, 0.80)), V((0.46 * sx, -11.05, 0.95)), V((0.55 * sx, -10.80, 1.02))],
                0.08,
            )
        )
        parts.append(
            horn(
                "cheek",
                [V((0.40 * sx, -11.30, 0.44)), V((0.52 * sx, -11.10, 0.45)), V((0.60 * sx, -10.85, 0.44))],
                0.08,
            )
        )
        parts.append(
            horn(
                "chin",
                [V((0.28 * sx, -11.30, -0.18)), V((0.36 * sx, -11.10, -0.27)), V((0.43 * sx, -10.90, -0.32))],
                0.07,
            )
        )
        parts.append(
            horn(
                "jawspike",
                [V((0.20 * sx, -13.55, -0.05)), V((0.24 * sx, -13.50, -0.15)), V((0.26 * sx, -13.40, -0.22))],
                0.04,
            )
        )
    parts.append(horn("nose", [V((0.0, -12.15, 0.86)), V((0.0, -12.05, 0.98)), V((0.0, -11.92, 1.05))], 0.07, flat=0.6))
    parts.append(
        horn("snout", [V((0.0, -13.45, 0.62)), V((0.0, -13.42, 0.72)), V((0.0, -13.35, 0.78))], 0.05, flat=0.6)
    )
    return decimate(join(parts, "horns"), BUDGET["horns"])


def build_teeth():
    """Fangs along the upper jaw pointing down and along the lower jaw pointing up, longest at the front corners."""
    parts = []
    for y0, y1, z0, z1, w0, w1, down in (
        (-11.8, -13.75, 0.36, 0.34, 0.30, 0.10, True),
        (-11.9, -13.70, 0.17, 0.11, 0.27, 0.10, False),
    ):
        n = 7
        for k in range(n):
            f = k / (n - 1)
            y = y0 + (y1 - y0) * f
            z = z0 + (z1 - z0) * f
            w = w0 + (w1 - w0) * f
            length = 0.10 + 0.06 * math.sin(math.pi * f) + (0.06 if k == n - 2 else 0.0)
            for sx in (1, -1):
                base = V((w * sx, y, z))
                tip = base + V((0.0, 0.0, -length if down else length))
                parts.append(horn("tooth", [base, base.lerp(tip, 0.5), tip], 0.032, 4, 1))
    return decimate(join(parts, "teeth"), BUDGET["teeth"])


def build_tongue():
    """A forked tongue lying on the lower jaw."""
    parts = [horn("tongue", [V((0, -12.0, 0.20)), V((0, -12.7, 0.24)), V((0, -13.3, 0.20))], 0.07, 6, 2)]
    for sx in (1, -1):
        parts.append(
            horn("fork", [V((0, -13.25, 0.20)), V((0.04 * sx, -13.5, 0.19)), V((0.07 * sx, -13.72, 0.18))], 0.03, 4, 1)
        )
    return join(parts, "tongue")


# ---------------------------------------------------------------------------- build


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats()
    parts = [
        (build_body(), "Body"),
        (build_spines(), "Horn"),
        (build_head(), "Head"),
        (build_horns(), "Horn"),
        (build_teeth(), "Teeth"),
        (build_tongue(), "Mouth"),
    ]
    objs = []
    for o, m in parts:
        set_material(o, mats[m])
        outward(o)
        print(f"part {o.name}: {len(o.data.polygons)} faces")
        for p in o.data.polygons:
            p.use_smooth = True
        objs.append(o)
    model = join(objs, "drevil")
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
    print(f"drevil: {len(me.vertices)} vertices, {len(me.polygons)} triangles, per slot {per}")


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
        lo = link(bpy.data.objects.new("l", ld))
        lo.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.type = "ORTHO"
    views = [
        ("side", V((0, -6.9, 0.35)), V((1, 0, 0.0001)), 15.0),
        ("top", V((0, -6.9, 0.35)), V((0, 0, 1)), 15.0),
        ("persp", V((0, -6.9, 0.35)), V((0.8, -1, 0.6)), 13.0),
        ("front", V((0, -6.9, 0.4)), V((0, -1, 0.0001)), 3.0),
        ("head", V((0, -12.3, 0.45)), V((0.9, -1, 0.45)), 3.6),
        ("headside", V((0, -12.2, 0.45)), V((1, 0, 0.0001)), 3.6),
        ("headbelow", V((0, -12.3, 0.3)), V((0.6, -0.8, -0.5)), 3.6),
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

        # the teeth are the 1999 cut-out tooth cards (entry 2); everything else uses the opaque skin and mouth sheets
        export_authored(
            model,
            Path(ARGS.authored).resolve(),
            model="drevil(h)",
            label="Dr. Evil",
            fit="joints",
            slots={"Teeth": {"pcTextures": [2]}},
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "drevil.blend"))
    if not ARGS.no_render:
        render()


main()
