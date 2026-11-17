"""Build an original Dr. Evil (the Order of the Flame final boss, a giant horned serpent) in Blender, headless, with
its own baked texture.

The 1999 PC model `drevil(h)` (Common/NPC/NPCs, 539 polygons) is replaced at convert time by this mesh: the converter
fits it on the PC skeleton (a nine-joint spine, the head, a two-joint jaw and a three-joint tongue) and takes the skin
weights from the player's own disc (editor/packages/formats/src/ootfauthored.ts); the texture is the model's own. This
script reads no game data: the segmented body with its ring ridges and dorsal spines, the long horned head with three
eyes a side, the open jaw with its teeth and the tongue are lofted from rounded proportions measured on the reference
(joint positions, heights and widths), merged per part with a voxel remesh and decimated to a triangle budget. Every
slot is painted with procedural shader nodes (olive scales with dark crevices and blotches, a pale plated belly, amber
eyes with slit pupils, a red ridged mouth, ivory horns and teeth) and baked into one atlas by own_texture.bake_atlas,
saved beside the JSON (original art; run scripts/buildtool/pin_model_textures.py).

    blender -b --factory-startup -P analysis/scripts/blender/drevil_model.py -- \
        --out build/authored/drevil-model [--authored scripts/buildtool/kit/models-draft/drevil.json] [--size 512] \
        [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the side, y toward the tail, z up; the serpent faces -Y and
is about 14 units long), so the `landmarks` are the PC joint positions and the converter's joint fit is the identity.
Outputs under --out (gitignored): drevil.blend and renders of the procedural paint render_<view>.png.
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
    ap.add_argument("--size", type=int, default=512, help="baked atlas size in pixels")
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

ORDER = ["Body", "Head", "Mouth", "Horn", "Teeth"]


def make_mats():
    return {name: bpy.data.materials.new(name) for name in ORDER}


# ---------------------------------------------------------------------------- painting (the model's own textures)
# Every slot is painted procedurally with shader nodes in object space (the serpent sits at the origin in PC units) and
# baked into one atlas by own_texture.bake_atlas: base colour x occlusion x a soft top light. Nothing is sampled from
# the 1999 skin; it was only looked at for the colour scheme (olive green scales with darker blotches, a paler belly,
# a red mouth, a row of pale eyes a side).

PAL = {
    "scale_dark": (0.022, 0.026, 0.010),
    "scale": (0.085, 0.095, 0.032),
    "scale_hi": (0.230, 0.225, 0.090),
    "blotch": (0.028, 0.036, 0.014),
    "belly": (0.30, 0.26, 0.13),
    "belly_lo": (0.13, 0.11, 0.05),
    "mouth": (0.20, 0.022, 0.018),
    "mouth_lo": (0.060, 0.006, 0.006),
    "tongue": (0.32, 0.07, 0.06),
    "horn": (0.36, 0.31, 0.20),
    "horn_lo": (0.09, 0.075, 0.045),
    "eye": (0.62, 0.42, 0.08),
    "eye_glow": (1.0, 0.55, 0.08),
    "pupil": (0.010, 0.006, 0.004),
}

# the eyes as build_head places them: (y, z, x) per eye, x mirrored, and the eyeball radius
EYES = [((-12.55, 0.68, 0.345), 0.085), ((-12.88, 0.64, 0.32), 0.075), ((-13.20, 0.60, 0.285), 0.065)]


class Painter:
    """Small helper over a material's node tree: object-space position, noise, cells, bands, masks and mixes."""

    def __init__(self, mat):
        self.nt = mat.node_tree
        self.bsdf = self.nt.nodes["Principled BSDF"]
        self.bsdf.inputs["Roughness"].default_value = 0.9
        self.pos = self.node("ShaderNodeTexCoord").outputs["Object"]
        sep = self.node("ShaderNodeSeparateXYZ")
        self.link(self.pos, sep.inputs[0])
        self.x, self.y, self.z = sep.outputs[0], sep.outputs[1], sep.outputs[2]
        nsep = self.node("ShaderNodeSeparateXYZ")
        self.link(self.node("ShaderNodeNewGeometry").outputs["Normal"], nsep.inputs[0])
        self.nz = nsep.outputs[2]

    def node(self, kind, **props):
        n = self.nt.nodes.new(kind)
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def link(self, a, b):
        if isinstance(a, (int, float)):
            b.default_value = a
        elif isinstance(a, tuple):
            b.default_value = (*a, 1.0) if len(a) == 3 and b.type == "RGBA" else a
        else:
            self.nt.links.new(a, b)

    def math(self, op, a, b=0.0):
        n = self.node("ShaderNodeMath", operation=op)
        self.link(a, n.inputs[0])
        self.link(b, n.inputs[1])
        return n.outputs[0]

    def vec(self, x, y, z):
        n = self.node("ShaderNodeCombineXYZ")
        for i, s in enumerate((x, y, z)):
            self.link(s, n.inputs[i])
        return n.outputs[0]

    def mapped(self, scale=(1, 1, 1), loc=(0, 0, 0), vec=None):
        n = self.node("ShaderNodeMapping")
        self.link(vec or self.pos, n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Location"].default_value = loc
        return n.outputs[0]

    def noise(self, scale, detail=3.0, rough=0.55, vec=None):
        n = self.node("ShaderNodeTexNoise")
        self.link(vec or self.pos, n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        return n.outputs["Factor"]

    def cells(self, scale, vec=None, randomness=0.85):
        """Voronoi scales: (distance to the cell edge, a random value per cell)."""
        e = self.node("ShaderNodeTexVoronoi", feature="DISTANCE_TO_EDGE")
        c = self.node("ShaderNodeTexVoronoi", feature="F1")
        for n in (e, c):
            self.link(vec or self.pos, n.inputs["Vector"])
            n.inputs["Scale"].default_value = scale
            n.inputs["Randomness"].default_value = randomness
        sep = self.node("ShaderNodeSeparateColor")
        self.link(c.outputs["Color"], sep.inputs[0])
        return e.outputs["Distance"], sep.outputs[0]

    def bands(self, u, scale, distortion=0.0):
        """Sine bands (0..1) along the coordinate `u` (a float socket)."""
        n = self.node("ShaderNodeTexWave", wave_type="BANDS", bands_direction="X")
        self.link(self.vec(u, self.x, self.z), n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Distortion"].default_value = distortion
        return n.outputs["Factor"]

    def step(self, a, lo, hi):
        """0 at lo, 1 at hi, smooth in between (hi < lo inverts)."""
        n = self.node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
        self.link(a, n.inputs["Value"])
        n.inputs["From Min"].default_value = lo
        n.inputs["From Max"].default_value = hi
        return n.outputs["Result"]

    def ellipsoid_value(self, centre, r):
        """|(p - centre) / r|: 1 on the ellipsoid's surface."""
        v = self.mapped(scale=tuple(1 / k for k in r), loc=tuple(-c / k for c, k in zip(centre, r)))
        n = self.node("ShaderNodeVectorMath", operation="LENGTH")
        self.link(v, n.inputs[0])
        return n.outputs["Value"]

    def mix(self, fac, a, b, blend="MIX"):
        n = self.node("ShaderNodeMix", data_type="RGBA", blend_type=blend, clamp_factor=True)
        ins = {s.identifier: s for s in n.inputs}
        self.link(fac, ins["Factor_Float"])
        self.link(a, ins["A_Color"])
        self.link(b, ins["B_Color"])
        return next(s for s in n.outputs if s.identifier == "Result_Color")

    def shade(self, col, k):
        g = (k, k, k) if isinstance(k, (int, float)) else self.vec(k, k, k)
        return self.mix(1.0, col, g, "MULTIPLY")

    def out(self, col, glow=None, glow_mask=None, strength=1.0):
        self.link(col, self.bsdf.inputs["Base Color"])
        if glow is not None:
            self.link(glow, self.bsdf.inputs["Emission Color"])
            self.link(self.math("MULTIPLY", glow_mask, strength), self.bsdf.inputs["Emission Strength"])


def scale_skin(p, size, belly_plates=True):
    """Olive scales: dark crevices between the cells, a lighter rim on each scale, per-scale tone, large dark blotches
    over the back, a darker dorsal stripe and a pale plated belly."""
    edge, rnd = p.cells(1.0 / size, vec=p.mapped(scale=(1.0, 0.75, 1.0)))
    col = p.mix(p.math("MULTIPLY", rnd, 0.7), PAL["scale"], PAL["scale_hi"])
    blot = p.step(p.noise(0.11 / size, 3, 0.6, vec=p.mapped(scale=(1.0, 0.6, 1.0))), 0.48, 0.56)
    col = p.mix(p.math("MULTIPLY", blot, 0.85), col, PAL["blotch"])
    col = p.mix(p.math("MULTIPLY", p.step(p.nz, 0.55, 0.95), 0.5), col, PAL["blotch"])
    # rim light on each scale, dark crevice at its edge
    col = p.mix(p.math("MULTIPLY", p.step(edge, 0.10, 0.20), 0.35), col, PAL["scale_hi"])
    col = p.mix(p.step(edge, 0.08, 0.0), col, PAL["scale_dark"])
    if not belly_plates:
        return col
    plates = p.bands(p.y, 0.314 / (size * 1.6), 0.0)
    belly = p.mix(p.step(plates, 0.12, 0.0), PAL["belly"], PAL["belly_lo"])
    belly = p.mix(p.math("MULTIPLY", p.noise(6, 3, 0.6), 0.5), belly, PAL["belly_lo"])
    return p.mix(p.step(p.nz, -0.30, -0.55), col, belly)


def paint_body(mat):
    p = Painter(mat)
    p.out(scale_skin(p, 0.20))


def paint_head(mat):
    """Head: finer scales, the eyes (amber with a slit pupil, a dark lid rim, a faint glow), dark nostrils."""
    p = Painter(mat)
    col = scale_skin(p, 0.10, belly_plates=False)
    glow = None
    for sx in (1, -1):
        for (y, z, w), r in EYES:
            c = (w * sx, y, z)
            v = p.ellipsoid_value(c, (r * 0.95, r * 1.25, r * 1.05))
            rim = p.step(v, 1.30, 1.05)
            ball = p.step(v, 1.06, 0.98)
            pupil = p.step(p.ellipsoid_value(c, (r * 1.2, r * 0.22, r * 0.85)), 1.0, 0.85)
            col = p.mix(rim, col, PAL["scale_dark"])
            col = p.mix(ball, col, PAL["eye"])
            col = p.mix(pupil, col, PAL["pupil"])
            g = p.math("MULTIPLY", ball, p.math("SUBTRACT", 1.0, pupil))
            glow = g if glow is None else p.math("MAXIMUM", glow, g)
        col = p.mix(
            p.step(p.ellipsoid_value((0.08 * sx, -13.82, 0.52), (0.045, 0.07, 0.035)), 1.0, 0.8), col, (0, 0, 0)
        )
    # inside the mouth (the lining and the inner faces of the jaws): red, darker toward the throat
    inner = p.math("ADD", 0.31, p.math("MULTIPLY", p.math("ADD", p.y, 11.6), 0.11))
    m = p.step(p.math("SUBTRACT", inner, p.math("ABSOLUTE", p.x)), 0.0, 0.04)
    m = p.math("MULTIPLY", m, p.math("MULTIPLY", p.step(p.z, -0.06, 0.0), p.step(p.z, 0.40, 0.34)))
    m = p.math("MULTIPLY", m, p.math("MULTIPLY", p.step(p.y, -11.0, -11.3), p.step(p.y, -13.72, -13.58)))
    # not on the underside of the lower jaw
    m = p.math("MULTIPLY", m, p.math("MAXIMUM", p.step(p.z, 0.10, 0.16), p.step(p.nz, -0.5, -0.2)))
    ridges = p.bands(p.y, 0.314 / 0.09, 0.0)
    red = p.mix(p.step(ridges, 0.25, 0.0), PAL["mouth"], PAL["mouth_lo"])
    red = p.mix(p.step(p.y, -12.2, -11.2), red, PAL["mouth_lo"])
    col = p.mix(m, col, red)
    p.out(col, PAL["eye_glow"], glow, 0.6)


def paint_mouth(mat):
    """Mouth lining and tongue: deep red with palate ridges across, the tongue paler on top."""
    p = Painter(mat)
    ridges = p.bands(p.y, 0.314 / 0.09, 0.0)
    col = p.mix(p.step(ridges, 0.25, 0.0), PAL["mouth"], PAL["mouth_lo"])
    col = p.mix(p.math("MULTIPLY", p.noise(9, 3, 0.6), 0.5), col, PAL["mouth_lo"])
    tongue = p.math("MULTIPLY", p.step(p.math("ABSOLUTE", p.x), 0.10, 0.06), p.step(p.z, 0.15, 0.20))
    tongue = p.math("MULTIPLY", tongue, p.step(p.y, -11.9, -12.1))
    p.out(p.mix(tongue, col, PAL["tongue"]))


def paint_horn(mat):
    """Horns and spines: ivory with lengthwise streaks and dark grime."""
    p = Painter(mat)
    streak = p.noise(14, 3, 0.6, vec=p.mapped(scale=(3.0, 1.0, 0.6)))
    col = p.mix(p.step(streak, 0.35, 0.75), PAL["horn_lo"], PAL["horn"])
    p.out(p.mix(p.math("MULTIPLY", p.noise(4, 2, 0.5), 0.4), col, PAL["horn_lo"]))


def paint_teeth(mat):
    p = Painter(mat)
    col = p.mix(p.step(p.noise(30, 2, 0.5), 0.3, 0.8), (0.30, 0.26, 0.17), (0.48, 0.44, 0.33))
    p.out(col)


PAINT = {"Body": paint_body, "Head": paint_head, "Mouth": paint_mouth, "Horn": paint_horn, "Teeth": paint_teeth}


def paint(model):
    for mat in model.data.materials:
        mat.use_nodes = True
        nt = mat.node_tree
        nt.nodes.clear()
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        bsdf.name = "Principled BSDF"
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(bsdf.outputs[0], out.inputs[0])
        PAINT[mat.name.split(".")[0]](mat)


def uv_islands(bm, uv):
    """Faces grouped by UV connectivity (edges whose two sides share their UVs)."""
    bm.faces.ensure_lookup_table()
    parent = list(range(len(bm.faces)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        f0, f1 = e.link_faces
        same = True
        for v in e.verts:
            a = next(lo[uv].uv for lo in f0.loops if lo.vert == v)
            b = next(lo[uv].uv for lo in f1.loops if lo.vert == v)
            same &= (a - b).length < 1e-6
        if same:
            parent[find(f0.index)] = find(f1.index)
    groups = {}
    for f in bm.faces:
        groups.setdefault(find(f.index), []).append(f)
    return list(groups.values())


def hidden_faces(bm, reach):
    """Indices of faces that cannot be seen: of nine rays (the normal and eight tilted ones) eight hit the model nearby
    (six when the one along the normal does)."""
    from mathutils.bvhtree import BVHTree

    tree = BVHTree.FromBMesh(bm)
    out = set()
    for f in bm.faces:
        n = f.normal
        if n.length < 0.5:
            continue
        t = n.orthogonal().normalized()
        b = n.cross(t)
        o = f.calc_center_median() + n * 2e-5
        dirs = [n]
        for k in range(8):
            a = k * math.pi / 4
            dirs.append((n + (t * math.cos(a) + b * math.sin(a)) * 1.5).normalized())
        hits = [tree.ray_cast(o, d, reach)[0] is not None for d in dirs]
        if sum(hits) >= (6 if hits[0] else 8):
            out.add(f.index)
    return out


def unwrap_weighted(obj, weights, margin=0.004, reach=0.6):
    """The "atlas" UV map, as own_texture.unwrap makes it, but with a texel density per slot: the skull's face gets more
    of the picture than its area would (the converter keeps 256 px of it); faces nobody sees get little."""
    bpy.context.view_layer.objects.active = obj
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    me = obj.data
    if "atlas" not in me.uv_layers:
        me.uv_layers.new(name="atlas")
    me.uv_layers.active = me.uv_layers["atlas"]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=margin, area_weight=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv["atlas"]
    names = [m.name.split(".")[0] for m in me.materials]
    hidden = hidden_faces(bm, reach)
    for isl in uv_islands(bm, uv):
        area = sum(f.calc_area() for f in isl) or 1.0
        k = weights.get(names[isl[0].material_index], 1.0)
        if sum(f.calc_area() for f in isl if f.index in hidden) / area > 0.85:
            k *= 0.25  # inside the hood or buried in the robe: never seen
        if k != 1.0:
            for f in isl:
                for lo in f.loops:
                    lo[uv].uv = lo[uv].uv * k
    bm.to_mesh(me)
    bm.free()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


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
    return model  # quads kept for the unwrap (fewer islands); the exporter triangulates


def report(model):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + 1
    print(f"drevil: {len(me.vertices)} vertices, {len(me.polygons)} faces, per slot {per}")


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


# texel density per slot (linear scale before packing): the head and its eyes read at game distance
UV_WEIGHTS = {"Head": 2.2, "Mouth": 1.2, "Horn": 0.8, "Teeth": 0.7}


def main():
    model = build()
    report(model)
    unwrap_weighted(model, UV_WEIGHTS)
    paint(model)
    if ARGS.authored:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from authored_export import export_authored
        from own_texture import bake_atlas

        out = Path(ARGS.authored).resolve()
        slots = bake_atlas(model, out.with_suffix(".png"), size=ARGS.size)
        export_authored(
            model,
            out,
            model="drevil(h)",
            label="Dr. Evil",
            fit="joints",
            slots=slots,
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}},
        )
        paint(model)  # the bake left its image nodes; repaint for the renders
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "drevil.blend"))
    if not ARGS.no_render:
        render()


main()
