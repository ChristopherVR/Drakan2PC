"""Build an original Death Mage (Order of the Flame hooded wraith) in Blender, headless, with its own baked textures.

The 1999 PC models `deathmage(h)`, `deathmage ghost(h)` (the same mesh with a pale skin) and `deathmageshaman(h)2` (the
same mesh, about 5 mm further back, with a chest plate and bracers) in Common/NPC/NPCs are replaced at convert time by
this mesh: the converter fits it on the PC skeleton and takes the skin weights from the player's own disc
(editor/packages/formats/src/ootfauthored.ts); the textures are the model's own. This script reads no game data: the
hooded skull, the bandage-wrapped robe tapering into a legless tail, slim wrapped sleeves, bare arm bones and clawed
hands, raised wraps, and the separate thin strips hanging from the arms, shoulders, hood and all round the hem are built
from rounded proportions measured on the reference (joint positions, heights and widths). Every slot is painted with
procedural shader nodes (pale dirty bandages with rust and old blood over a dark robe, ivory bone with dark sockets and
an ember in each, the shaman's dark plate with green stones; a pale palette for the ghost) and baked into one atlas per
variant by own_texture.bake_atlas, saved beside the JSON (original art; run scripts/buildtool/pin_model_textures.py).

    blender -b --factory-startup -P analysis/scripts/blender/deathmage_model.py -- \
        --out build/authored/deathmage-model [--which mage|shaman|ghost|all] \
        [--authored scripts/buildtool/kit/models-draft] [--size 512] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the mage's left, y toward the back, z up; the mage faces -Y),
so the `landmarks` are the PC joint positions and the converter's joint fit is the identity. The shaman's 1999 skeleton
does not sit on its mesh, so its file is placed by height instead. Outputs under --out (gitignored): deathmage_<geo>.blend
and renders of the procedural paint render_<geo>_<view>.png.
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
    ap.add_argument("--size", type=int, default=512, help="baked atlas size in pixels")
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

# triangle budgets per remeshed part (with the strips, wraps and hands the model has about 3,000 triangles)
BUDGET = {"body": 640, "hood": 340, "skull": 440, "armour": 260}

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


def ribbon(name, ctrl, normal, width, thick=0.0012, per=1, point=True):
    """A hanging cloth strip: diamond section along control points (x, y, z, half width scale), tapering to a point.
    `normal` (a vector or a function of the point) is the side the strip faces; its width lies across it. Without
    `point` the strip ends square (a torn end)."""
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
        if point and i == len(samples) - 1:
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
    if not point:
        bm.faces.new(rings[-1])
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


# ---------------------------------------------------------------------------- painting (the model's own textures)
# Every slot is painted procedurally with shader nodes in object space (the model sits at the origin in PC units) and
# baked into one atlas by own_texture.bake_atlas: base colour x occlusion x a soft top light, like the painted-in shading
# of Drakan 2 textures. Nothing is sampled from the 1999 skins; they were only looked at for the colour scheme (pale
# bandages with rust and old blood over a dark body, ivory bone, a dark shaman plate with green stones).

PALETTES = {
    # linear colours
    "mage": {
        "bandage": (0.28, 0.180, 0.110),
        "bandage_lo": (0.110, 0.062, 0.040),
        "robe": (0.010, 0.0075, 0.007),
        "robe_hi": (0.034, 0.024, 0.020),
        "rust": (0.13, 0.050, 0.020),
        "blood": (0.12, 0.014, 0.010),
        "bone": (0.34, 0.26, 0.155),
        "bone_lo": (0.11, 0.070, 0.035),
        "socket": (0.020, 0.010, 0.008),
        "glow": (1.0, 0.25, 0.04),
        "armour": (0.016, 0.018, 0.022),
        "armour_hi": (0.055, 0.058, 0.064),
        "gem": (0.10, 0.85, 0.20),
    },
    "ghost": {
        "bandage": (0.40, 0.42, 0.43),
        "bandage_lo": (0.17, 0.19, 0.21),
        "robe": (0.022, 0.026, 0.034),
        "robe_hi": (0.060, 0.068, 0.082),
        "rust": (0.15, 0.17, 0.16),
        "blood": (0.10, 0.13, 0.14),
        "bone": (0.42, 0.42, 0.39),
        "bone_lo": (0.13, 0.14, 0.15),
        "socket": (0.015, 0.020, 0.030),
        "glow": (0.35, 0.75, 1.0),
        "armour": (0.016, 0.018, 0.022),
        "armour_hi": (0.055, 0.058, 0.064),
        "gem": (0.10, 0.85, 0.20),
    },
}


class Painter:
    """Small helper over a material's node tree: object-space position, noise, bands, masks and mixes."""

    def __init__(self, mat):
        self.nt = mat.node_tree
        self.bsdf = self.nt.nodes["Principled BSDF"]
        self.bsdf.inputs["Roughness"].default_value = 0.9
        self.pos = self.node("ShaderNodeTexCoord").outputs["Object"]
        sep = self.node("ShaderNodeSeparateXYZ")
        self.link(self.pos, sep.inputs[0])
        self.x, self.y, self.z = sep.outputs[0], sep.outputs[1], sep.outputs[2]

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

    def math(self, op, a, b=0.0, clamp=False):
        n = self.node("ShaderNodeMath", operation=op, use_clamp=clamp)
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

    def noise(self, scale, detail=3.0, rough=0.55, vec=None, distortion=0.0):
        n = self.node("ShaderNodeTexNoise")
        self.link(vec or self.pos, n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        n.inputs["Distortion"].default_value = distortion
        return n.outputs["Factor"]

    def bands(self, u, scale, distortion=2.0):
        """Sine bands (0..1) along the coordinate `u` (a float socket); y and z vary the distortion over the surface."""
        n = self.node("ShaderNodeTexWave", wave_type="BANDS", bands_direction="X")
        self.link(self.vec(u, self.y, self.z), n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Distortion"].default_value = distortion
        n.inputs["Detail"].default_value = 2.0
        n.inputs["Detail Scale"].default_value = 1.5
        return n.outputs["Factor"]

    def voronoi_edge(self, scale, vec=None):
        n = self.node("ShaderNodeTexVoronoi", feature="DISTANCE_TO_EDGE")
        self.link(vec or self.pos, n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        return n.outputs["Distance"]

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

    def spot(self, centre, r, soft=0.4):
        """1 inside an ellipsoid (centre, radii), fading to 0 over its outer `soft` part."""
        return self.step(self.ellipsoid_value(centre, r), 1.0, 1.0 - soft)

    def mix(self, fac, a, b, blend="MIX"):
        n = self.node("ShaderNodeMix", data_type="RGBA", blend_type=blend, clamp_factor=True)
        ins = {s.identifier: s for s in n.inputs}
        self.link(fac, ins["Factor_Float"])
        self.link(a, ins["A_Color"])
        self.link(b, ins["B_Color"])
        return next(s for s in n.outputs if s.identifier == "Result_Color")

    def shade(self, col, k):
        """Colour times a factor (a float socket or a number)."""
        g = (k, k, k) if isinstance(k, (int, float)) else self.vec(k, k, k)
        return self.mix(1.0, col, g, "MULTIPLY")

    def out(self, col, glow=None, glow_mask=None, strength=6.0):
        self.link(col, self.bsdf.inputs["Base Color"])
        if glow is not None:
            self.link(glow, self.bsdf.inputs["Emission Color"])
            self.link(self.math("MULTIPLY", glow_mask, strength), self.bsdf.inputs["Emission Strength"])


def cloth_dirt(p, col, pal, dirt_scale=1.0, stains=True):
    """Weathering shared by the bandages: grime toward the hem, rust stains and a few spots of old blood, a fine weave."""
    low = p.step(p.z, 0.02, -0.20)
    grime = p.math("MULTIPLY", p.noise(28 * dirt_scale, 4, 0.6), p.math("ADD", 0.35, low))
    col = p.mix(p.step(grime, 0.20, 0.60), col, pal["bandage_lo"])
    if stains:
        rust = p.step(p.noise(55 * dirt_scale, 3, 0.55, vec=p.mapped(loc=(3.1, 1.7, 0.4))), 0.57, 0.66)
        col = p.mix(p.math("MULTIPLY", rust, 0.8), col, pal["rust"])
        blood = p.step(p.noise(40 * dirt_scale, 2, 0.5, vec=p.mapped(loc=(-5.3, 2.2, 7.9))), 0.64, 0.70)
        col = p.mix(p.math("MULTIPLY", blood, 0.85), col, pal["blood"])
    return p.shade(col, p.math("ADD", 0.85, p.math("MULTIPLY", p.noise(900, 1, 0.5), 0.3)))


def wrap_bands(p, u, scale):
    """Bandage mask (1 on the strips, 0 in the gaps) and an edge shade (dark creases along the strip edges)."""
    b = p.bands(u, scale, distortion=2.6)
    # the strips change width along their length: shift the threshold with a slow noise
    t = p.math("MULTIPLY", p.noise(18, 2, 0.5, vec=p.mapped(loc=(scale, 0.0, 0.0))), 0.30)
    b = p.math("ADD", b, p.math("SUBTRACT", t, 0.15))
    mask = p.step(b, 0.30, 0.38)
    crease = p.math("ADD", 0.50, p.math("MULTIPLY", p.step(b, 0.34, 0.66), 0.50))
    return mask, crease


def paint_robe(mat, pal):
    """The body: a dark robe under criss-crossed bandage wraps (around the trunk; around the arms on the sleeves)."""
    p = Painter(mat)
    ax = p.math("ABSOLUTE", p.x)
    # coordinate across the bandages: height on the trunk, along the arm (drooping about 23 degrees) on the sleeves
    arm_u = p.math("SUBTRACT", p.math("MULTIPLY", ax, 0.92), p.math("MULTIPLY", p.z, 0.40))
    on_arm = p.step(ax, 0.046, 0.058)
    trunk1 = p.math("ADD", p.z, p.math("MULTIPLY", p.x, 0.22))
    trunk2 = p.math("SUBTRACT", p.math("SUBTRACT", p.z, p.math("MULTIPLY", p.x, 0.30)), 0.0037)
    m1, c1 = wrap_bands(p, trunk1, 34)
    m2, c2 = wrap_bands(p, trunk2, 27)
    a1, ac1 = wrap_bands(p, arm_u, 40)
    # the second trunk layer only in patches, so the wraps cross here and there
    patch = p.step(p.noise(11, 2, 0.5, vec=p.mapped(loc=(1.3, 0.2, 4.0))), 0.48, 0.58)
    m2 = p.math("MULTIPLY", m2, patch)
    trunk_mask = p.math("MAXIMUM", m1, m2)
    trunk_crease = p.math("MINIMUM", c1, p.math("ADD", c2, p.math("SUBTRACT", 1.0, patch)))
    mask = p.mix(on_arm, p.vec(trunk_mask, 0, 0), p.vec(a1, 0, 0))
    crease = p.mix(on_arm, p.vec(trunk_crease, 0, 0), p.vec(ac1, 0, 0))
    sm = p.node("ShaderNodeSeparateXYZ")
    p.link(mask, sm.inputs[0])
    sc = p.node("ShaderNodeSeparateXYZ")
    p.link(crease, sc.inputs[0])
    # gaps: dark robe with faint vertical folds
    folds = p.noise(70, 2, 0.5, vec=p.mapped(scale=(1, 1, 0.25)))
    robe = p.mix(p.step(folds, 0.35, 0.75), pal["robe"], pal["robe_hi"])
    band = p.shade(cloth_dirt(p, pal["bandage"], pal), sc.outputs[0])
    # torn holes in the wraps
    torn = p.step(p.noise(45, 3, 0.6, vec=p.mapped(loc=(7, 7, 7))), 0.66, 0.70)
    m = p.math("MULTIPLY", sm.outputs[0], p.math("SUBTRACT", 1.0, torn))
    p.out(p.mix(m, robe, band))


def paint_hood(mat, pal):
    """Dark cowl with soft vertical folds, a pale bandage hem around the face opening and loose wraps on the crown."""
    p = Painter(mat)
    folds = p.noise(60, 3, 0.55, vec=p.mapped(scale=(1.0, 1.0, 0.3)))
    col = p.mix(p.step(folds, 0.30, 0.78), pal["robe"], pal["robe_hi"])
    band = cloth_dirt(p, pal["bandage"], pal, stains=False)
    # distance from the recess cut into the hood (build_hood): a rim band just outside it, at the front
    rim_v = p.ellipsoid_value((0.0, -0.036, 0.1260), (0.0155, 0.0265, 0.0230))
    rim = p.math("MULTIPLY", p.step(rim_v, 1.32, 1.14), p.step(p.y, -0.002, -0.010))
    edge = p.bands(p.math("ADD", p.z, p.math("MULTIPLY", p.x, 0.5)), 80, 1.0)
    rim_col = p.shade(band, p.math("ADD", 0.6, p.math("MULTIPLY", edge, 0.4)))
    crown, ccrease = wrap_bands(p, p.math("ADD", p.y, p.math("MULTIPLY", p.z, 0.6)), 22)
    crown = p.math("MULTIPLY", crown, p.step(p.z, 0.134, 0.142))
    crown = p.math("MULTIPLY", crown, p.step(p.noise(14, 1, 0.5), 0.45, 0.55))
    col = p.mix(p.math("MULTIPLY", crown, 0.8), col, p.shade(band, ccrease))
    p.out(p.mix(rim, col, rim_col))


def bone_colour(p, pal):
    """Ivory bone: grime and hairline cracks (occlusion darkens the joints)."""
    col = p.mix(p.step(p.noise(160, 4, 0.6), 0.35, 0.80), pal["bone"], p.mix(0.5, pal["bone"], pal["bone_lo"]))
    cracks = p.step(p.voronoi_edge(260), 0.03, 0.0)
    cracks = p.math("MULTIPLY", cracks, p.step(p.noise(30, 1, 0.5), 0.5, 0.6))
    return p.mix(p.math("MULTIPLY", cracks, 0.7), col, pal["bone_lo"])


SOCKETS = ((0.0053, -0.0255, 0.1333), (-0.0053, -0.0255, 0.1333))


def paint_skull(mat, pal):
    """The skull: bone with dark eye sockets and nose, a dim ember in each socket, teeth drawn on the teeth rows."""
    p = Painter(mat)
    col = bone_colour(p, pal)
    shadow = p.math("MAXIMUM", *[p.spot(c, (0.0072, 0.010, 0.0068), 0.6) for c in SOCKETS])
    cheeks = p.math("MAXIMUM", *[p.spot((c[0] * 1.55, -0.019, 0.1238), (0.0030, 0.008, 0.0030), 0.7) for c in SOCKETS])
    sockets = p.math("MAXIMUM", *[p.spot(c, (0.0050, 0.008, 0.0047), 0.30) for c in SOCKETS])
    nose = p.spot((0.0, -0.0262, 0.1284), (0.0023, 0.008, 0.0036), 0.35)
    col = p.mix(p.math("MULTIPLY", p.math("MAXIMUM", shadow, cheeks), 0.6), col, pal["bone_lo"])
    col = p.mix(p.math("MAXIMUM", sockets, nose), col, pal["socket"])
    # the mouth: a dark line between the upper teeth and the open mandible, gaps between the front teeth
    front = p.step(p.y, -0.014, -0.019)
    mouth = p.math("MULTIPLY", p.step(p.z, 0.1168, 0.1176), p.step(p.z, 0.1190, 0.1182))
    col = p.mix(p.math("MULTIPLY", mouth, front), col, pal["socket"])
    upper = p.math("MULTIPLY", p.step(p.z, 0.1186, 0.1192), p.step(p.z, 0.1222, 0.1214))
    lower = p.math("MULTIPLY", p.step(p.z, 0.1146, 0.1152), p.step(p.z, 0.1172, 0.1166))
    rows = p.math("MULTIPLY", p.math("MAXIMUM", upper, lower), front)
    gaps = p.step(p.bands(p.x, 150, 0.0), 0.18, 0.05)
    col = p.mix(p.math("MULTIPLY", p.math("MULTIPLY", rows, gaps), 0.8), col, pal["socket"])
    ember = p.math("MAXIMUM", *[p.spot((c[0], -0.0262, c[2] - 0.0004), (0.0026, 0.006, 0.0022), 0.7) for c in SOCKETS])
    p.out(col, pal["glow"], ember, 4.0)


def paint_arm_bone(mat, pal):
    p = Painter(mat)
    p.out(bone_colour(p, pal))


def paint_tatter(mat, pal):
    """Hanging bandage strips: lengthwise streaks, some strips darker, dirtier toward the ends, cross wrinkles."""
    p = Painter(mat)
    streak = p.noise(260, 2, 0.5, vec=p.mapped(scale=(1, 1, 0.08)))
    col = p.mix(p.step(streak, 0.3, 0.75), pal["bandage_lo"], pal["bandage"])
    dark = p.step(p.noise(16, 1, 0.5, vec=p.mapped(loc=(2.0, 5.0, 1.0))), 0.56, 0.60)
    col = p.mix(p.math("MULTIPLY", dark, 0.85), col, p.mix(0.5, pal["robe_hi"], pal["bandage_lo"]))
    col = cloth_dirt(p, col, pal, dirt_scale=1.2)
    wr = p.bands(p.z, 260, 3.0)
    p.out(p.shade(col, p.math("ADD", 0.78, p.math("MULTIPLY", wr, 0.22))))


def paint_bandage(mat, pal):
    """The raised wraps: pale strips with lengthwise streaks and stains."""
    p = Painter(mat)
    col = cloth_dirt(p, pal["bandage"], pal)
    streak = p.noise(300, 2, 0.5, vec=p.mapped(scale=(0.1, 0.1, 1.0)))
    p.out(p.shade(col, p.math("ADD", 0.75, p.math("MULTIPLY", streak, 0.4))))


GEMS = ((0.0, -0.026, 0.068), (0.030, -0.022, 0.086), (-0.030, -0.022, 0.086))


def paint_armour(mat, pal):
    """The shaman's plate and bracers: dark blued iron, scratched, bronze trim, green stones in the chest and shoulders."""
    p = Painter(mat)
    col = p.mix(p.step(p.noise(90, 4, 0.6), 0.55, 0.85), pal["armour"], pal["armour_hi"])
    scratches = p.step(p.voronoi_edge(70, vec=p.mapped(scale=(1, 1, 3))), 0.012, 0.0)
    scratches = p.math("MULTIPLY", scratches, p.step(p.noise(25, 1, 0.5), 0.5, 0.6))
    col = p.mix(p.math("MULTIPLY", scratches, 0.6), col, pal["armour_hi"])
    # engraved panel lines: a centre seam and ribs across the chest, and a bronze trim at the edges
    ax = p.math("ABSOLUTE", p.x)
    seam = p.step(ax, 0.0016, 0.0006)
    ribs = p.step(p.math("ABSOLUTE", p.math("SUBTRACT", p.bands(p.z, 38, 0.0), 0.5)), 0.47, 0.5)
    ribs = p.math("MULTIPLY", ribs, p.step(p.z, 0.090, 0.084))
    col = p.mix(p.math("MAXIMUM", seam, ribs), col, (0.004, 0.004, 0.005))
    trim = p.math("MAXIMUM", p.step(p.z, 0.100, 0.104), p.step(p.z, 0.033, 0.029))
    col = p.mix(p.math("MULTIPLY", trim, 0.7), col, (0.11, 0.075, 0.035))
    setting = p.math("MAXIMUM", *[p.spot(c, (0.0064, 0.012, 0.0064), 0.15) for c in GEMS[:2]])
    setting = p.math("MAXIMUM", setting, p.spot(GEMS[2], (0.0064, 0.012, 0.0064), 0.15))
    gems = p.math("MAXIMUM", *[p.spot(c, (0.0042, 0.012, 0.0042), 0.45) for c in GEMS[:2]])
    gems = p.math("MAXIMUM", gems, p.spot(GEMS[2], (0.0042, 0.012, 0.0042), 0.45))
    col = p.mix(setting, col, (0.13, 0.085, 0.035))
    col = p.mix(gems, col, p.shade(pal["gem"], 0.25))
    p.out(col, pal["gem"], gems, 0.7)


PAINT = {
    "Robe": paint_robe,
    "Hood": paint_hood,
    "Skull": paint_skull,
    "Bone": paint_arm_bone,
    "Tatter": paint_tatter,
    "Bandage": paint_bandage,
    "Armour": paint_armour,
}
ORDER = list(PAINT)


def make_mats():
    return {name: bpy.data.materials.new(name) for name in ORDER}


def paint(model, palette):
    """(Re)build every slot's shader for a palette."""
    pal = PALETTES[palette]
    for mat in model.data.materials:
        mat.use_nodes = True
        nt = mat.node_tree
        nt.nodes.clear()
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        bsdf.name = "Principled BSDF"
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(bsdf.outputs[0], out.inputs[0])
        PAINT[mat.name.split(".")[0]](mat, pal)


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


def hidden_faces(bm, reach=0.04):
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


def unwrap_weighted(obj, weights, margin=0.004):
    """The "atlas" UV map, as own_texture.unwrap makes it, but with a texel density per slot: the skull's face gets more
    of the picture than its area would (the converter keeps 256 px of it)."""
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
    hidden = hidden_faces(bm)
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


def arm_points(side):
    sh, el, wr, hand, fing, thumb, reach = SIDES[side]
    return V(JOINTS[sh]), V(JOINTS[el]), V(JOINTS[wr]), V(JOINTS[hand]), V(JOINTS[fing]), V(JOINTS[thumb]), reach


def sleeve(side):
    """A slim wrapped sleeve from inside the shoulder to just past the elbow; returns the tube and the cuff centre."""
    sh, el, wr, *_ = arm_points(side)
    sx = 1 if sh.x > 0 else -1
    root = V((0.030 * sx, 0.006, 0.090))
    cuff = el.lerp(wr, 0.30)
    ctrl = [
        (*root, 0.0150, 0.0160),
        (*sh.lerp(el, 0.15), 0.0118, 0.0125),
        (*sh.lerp(el, 0.60), 0.0098, 0.0102),
        (*el, 0.0090, 0.0094),
        (*cuff, 0.0072, 0.0076),
    ]
    return loft("sleeve", ctrl, sides=12, per=3), cuff


def _band(c, axis, r, thick, n=12, sides=3):
    s = axis.cross(UP if abs(axis.dot(UP)) < 0.9 else V((1, 0, 0))).normalized()
    u = s.cross(axis).normalized()
    path = [c + (s * math.cos(a) + u * math.sin(a)) * r for a in _angles(n)]
    return ring_tube("band", path, thick, sides=sides)


def build_wraps(cuffs):
    """Raised bandage wraps (separate geometry over the painted ones): three tilted bands round the robe and a band at
    each sleeve end."""
    parts = []
    for z0, tilt in ((0.062, 0.20), (0.012, -0.24), (-0.048, 0.22)):
        path = []
        for a in _angles(16):
            x0 = math.cos(a)
            z = z0 + tilt * x0 * 0.03
            cy, rx, ry = torso_at(z)
            path.append(V((x0 * rx * 1.04, cy + math.sin(a) * ry * 1.06, z)))
        parts.append(ring_tube("wrap", path, 0.0015, sides=3))
    for side in SIDES:
        sh, el, wr, *_ = arm_points(side)
        d = (wr - el).normalized()
        parts.append(_band(cuffs[side] - d * 0.0015, d, 0.0074, 0.0013, n=10))
    return join(parts, "wraps")


def build_body():
    """Torso and robe, the cowl collar and both sleeves, merged in one remesh."""
    parts = [vloft("torso", TORSO, sides=24, per=3, sq=0.9)]
    # cowl folds at the collar, so the hood drapes onto the shoulders
    parts.append(ellipsoid("cowl", (0.0, 0.002, 0.104), (0.026, 0.022, 0.010)))
    parts.append(ellipsoid("cowlback", (0.0, 0.016, 0.110), (0.020, 0.012, 0.014)))
    # shoulder blades and the shoulder caps the sleeves hang from
    for sx in (1, -1):
        parts.append(ellipsoid("cap", (0.038 * sx, 0.006, 0.090), (0.014, 0.015, 0.009)))
        parts.append(ellipsoid("blade", (0.020 * sx, 0.018, 0.070), (0.016, 0.008, 0.020)))
    cuffs = {}
    for side in SIDES:
        s, cuff = sleeve(side)
        parts.append(s)
        cuffs[side] = cuff
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
    the nose cut in (the sockets, nose and teeth are painted darker on top)."""
    parts = [
        ellipsoid("cranium", (0.0, -0.004, 0.1345), (0.0130, 0.0160, 0.0120), seg=28),
        ellipsoid("maxilla", (0.0, -0.0155, 0.1235), (0.0090, 0.0080, 0.0060)),
        ellipsoid("face", (0.0, -0.0120, 0.1300), (0.0118, 0.0110, 0.0110)),
    ]
    upper = [(-0.0075, -0.0160, 0.1205), (0, -0.0238, 0.1205), (0.0075, -0.0160, 0.1205)]
    parts.append(loft("uteeth", [(*p, 0.0016, 0.0022) for p in catmull(upper, 3)], sides=8))
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
    clawed fingers and a thumb, lying in the 1999 hand plane (palm down, fingers spread front to back). Returns the
    bones and the bracer (None for bare bones)."""
    sh, el, wr, hand, fing, thumb, reach = arm_points(side)
    d = (wr - el).normalized()
    across = V((0, 1, 0))
    parts = []
    bracer = None
    if style == "bone":
        for oy, r in ((-0.0022, 0.0019), (0.0022, 0.0017)):
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
        bracer = loft(
            "bracer",
            [(*(cuff - d * 0.004), 0.0066, 0.0066), (*cuff.lerp(wr, 0.5), 0.0058, 0.0058), (*wr, 0.0050, 0.0050)],
            sides=10,
            per=2,
        )
    parts.append(ellipsoid("wrist", tuple(wr), (0.0040, 0.0046, 0.0032), seg=8))
    knuck = wr.lerp(hand, 0.85)
    parts.append(loft("palm", [(*wr, 0.0046, 0.0024), (*knuck, 0.0056, 0.0020)], sides=8, per=1))
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
    return join(parts, f"arm_{side}"), bracer


def _out_from_axis(p):
    """Outward from the robe's axis at p (horizontal)."""
    cy, _rx, _ry = torso_at(max(p.z, -0.13))
    v = V((p.x, p.y - cy, 0.0))
    return v.normalized() if v.length > 1e-6 else V((0, -1, 0))


def _rand(i, k=0):
    """Deterministic jitter in [0, 1)."""
    v = math.sin(i * 12.9898 + k * 78.233) * 43758.5453
    return v - math.floor(v)


def hanging_strip(root, length, width, i, out=None, flare=0.0, n=6):
    """One loose bandage strip hanging from `root`: swaying a little, flaring out by `flare`, slightly narrower at its
    torn end. `out` is the horizontal direction it falls away from (None: straight down)."""
    ph = _rand(i) * 6.28
    ctrl = []
    for k in range(n):
        f = k / (n - 1)
        sway = V((0.0045 * math.sin(f * 3.0 + ph), 0.0030 * math.sin(f * 2.4 + ph * 1.7), 0.0))
        o = (out * (flare * f * f + 0.0008)) if out is not None else V((0, 0, 0))
        p = root + sway * f + o + V((0, 0, -length * f))
        ctrl.append((*p, 1.0 - 0.35 * f))
    facing = out if out is not None else V((math.sin(ph) * 0.4, -1.0, 0.0)).normalized()
    return ribbon("strip", ctrl, facing, width, thick=0.0007, point=False)


def build_tatters():
    """Separate thin strips hanging from the sleeves, the shoulders, the back of the hood and all round the hem."""
    parts = []
    i = 0
    for side in SIDES:
        sh, el, wr, *_ = arm_points(side)
        cuff = el.lerp(wr, 0.30)
        for f, length, w in (
            (0.25, 0.075, 0.0022),
            (0.55, 0.095, 0.0019),
            (0.85, 0.080, 0.0021),
            (1.25, 0.055, 0.0018),
        ):
            a = sh.lerp(el, f) if f <= 1.0 else el.lerp(cuff, f - 1.0)
            r = 0.0110 - 0.0025 * min(f, 1.0)
            i += 1
            root = a + V((0, 0.002 * (_rand(i, 2) - 0.5), -r * 0.75))
            sx = 1 if sh.x > 0 else -1
            swing = V((sx * (_rand(i, 8) - 0.35), (_rand(i, 9) - 0.5) * 0.8, 0)).normalized()
            parts.append(
                hanging_strip(root, length * (0.8 + 0.4 * _rand(i, 1)), w, i, swing, flare=0.004 + 0.008 * _rand(i, 7))
            )
    # from the front of the shoulders and the back of the hood
    for sx in (1, -1):
        i += 1
        parts.append(hanging_strip(V((0.024 * sx, -0.018, 0.088)), 0.060, 0.0020, i, V((0.3 * sx, -1, 0)).normalized()))
        i += 1
        parts.append(hanging_strip(V((0.008 * sx, 0.030, 0.104)), 0.095, 0.0024, i, V((0.2 * sx, 1, 0)).normalized()))
    # round the lower robe: roots on the robe at uneven heights, falling free well below the tail, longest at the back
    count = 14
    for k in range(count):
        i += 1
        a = 2 * math.pi * (k + 0.35 * _rand(i, 3)) / count
        z0 = -0.020 - 0.060 * _rand(i, 4)
        cy, rx, ry = torso_at(z0)
        root = V((math.cos(a) * rx * 0.92, cy + math.sin(a) * ry * 0.92, z0))
        back = 0.5 + 0.5 * math.sin(a)  # 1 at the back (+y)
        length = 0.095 + 0.060 * back + 0.040 * _rand(i, 5)
        out = _out_from_axis(root)
        flare = 0.012 + 0.016 * _rand(i, 7)
        parts.append(hanging_strip(root, length, 0.0019 + 0.0008 * _rand(i, 6), i, out, flare=flare))
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


def build(which):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats()
    shaman = which == "shaman"
    objs = []
    body, cuffs = build_body()
    set_material(body, mats["Robe"])
    objs.append(body)
    hood = build_hood()
    set_material(hood, mats["Hood"])
    objs.append(hood)
    skull = build_skull()
    set_material(skull, mats["Skull"])
    objs.append(skull)
    for side in SIDES:
        arm, bracer = build_arm(side, cuffs[side], "bracer" if shaman else "bone")
        set_material(arm, mats["Bone"])
        objs.append(arm)
        if bracer:
            set_material(bracer, mats["Armour"])
            objs.append(bracer)
    wraps = build_wraps(cuffs)
    set_material(wraps, mats["Bandage"])
    objs.append(wraps)
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
    return model  # quads kept for the unwrap (fewer islands); the exporter triangulates


def report(model):
    me = model.data
    per = {}
    for p in me.polygons:
        n = me.materials[p.material_index].name
        per[n] = per.get(n, 0) + 1
    print(f"{model.name}: {len(me.vertices)} vertices, {len(me.polygons)} faces, per slot {per}")


# ---------------------------------------------------------------------------- render


def render(which):
    """EEVEE renders of the painted model (the procedural shaders, not the bake) for a first look."""
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
        ("persp", V((0, 0, -0.03)), V((-0.7, -1, 0.35)), 0.46),
        ("head", V((0, -0.005, 0.125)), V((-0.5, -1, 0.15)), 0.075),
    ]
    for name, c, d, scale in views:
        d = d.normalized()
        cam.location = c + d * 2
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        cam.data.ortho_scale = scale
        cam.data.clip_start = 0.01
        sc.render.filepath = str(OUT / f"render_{which}_{name}.png")
        bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------------------- bake and export

VARIANTS = {
    # which: (built geometry, palette, PC model, label, file, fit)
    "mage": ("mage", "mage", "deathmage(h)", "Death Mage", "deathmage.json", "joints"),
    "ghost": ("mage", "ghost", "deathmage ghost(h)", "Death Mage ghost", "deathmage_ghost.json", "joints"),
    "shaman": ("shaman", "mage", "deathmageshaman(h)2", "Death Mage shaman", "deathmage_shaman.json", "height"),
}
# texel density per slot (linear scale before packing): the face and the hood opening read at game distance
UV_WEIGHTS = {"Skull": 3.0, "Hood": 1.3, "Bone": 1.0, "Tatter": 0.75}


def export(model, which):
    _geo, palette, pc_model, label, fname, fit = VARIANTS[which]
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored
    from own_texture import bake_atlas

    folder = Path(ARGS.authored).resolve()
    paint(model, palette)
    slots = bake_atlas(model, folder / fname.replace(".json", ".png"), size=ARGS.size)
    extra = {"landmarks": {n: list(JOINTS[n]) for n in LANDMARKS}} if fit == "joints" else {}
    export_authored(model, folder / fname, model=pc_model, label=label, fit=fit, slots=slots, extra=extra)


def main():
    which = ["mage", "shaman", "ghost"] if ARGS.which == "all" else [ARGS.which]
    built = {}
    for w in which:
        geo = VARIANTS[w][0]
        if geo not in built:
            model = build(geo)
            report(model)
            unwrap_weighted(model, UV_WEIGHTS)
            bpy.ops.wm.save_as_mainfile(filepath=str(OUT / f"deathmage_{geo}.blend"))
            if not ARGS.no_render:
                paint(model, VARIANTS[w][1])
                render(geo)
            built[geo] = True
        else:
            bpy.ops.wm.open_mainfile(filepath=str(OUT / f"deathmage_{geo}.blend"))
            model = bpy.data.objects[f"deathmage_{geo}"]
        if ARGS.authored:
            export(model, w)


main()
