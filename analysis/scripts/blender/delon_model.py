"""Build an original Delon (Rynn's brother) character model in Blender, headless.

Order of the Flame's Delon has no Drakan 2 disc equivalent, so this script authors a new
model from scratch (no game data is read). It produces a render mesh, a rig and a game LOD.

    blender -b --factory-startup -P analysis/scripts/blender/delon_model.py -- \
        --out build/delon-model [--clay] [--samples 64] [--views front,threequarter,side,back,face]

Outputs (all under --out, gitignored): delon.blend, delon_game.glb, render_<view>.png.

The shipped authored model (the body below the neck, the 1999 head kept) with its own baked texture beside it:

    blender -b --factory-startup -P analysis/scripts/blender/delon_model.py -- --out build/delon-model/own         --no-render --authored scripts/buildtool/kit/models/delon.json    # also writes delon.png; then run
    python scripts/buildtool/pin_model_textures.py

Coordinates: metres, Z up, the character faces -Y, height about 1.72 m.
"""

import argparse
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
    ap.add_argument("--out", default="build/delon-model")
    ap.add_argument("--clay", action="store_true")
    ap.add_argument("--samples", type=int, default=64)
    ap.add_argument("--res", type=int, default=1200, help="render height in pixels")
    ap.add_argument("--views", default="front,threequarter,side,back,face")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--lod", action="store_true", help="also render the game LOD")
    ap.add_argument(
        "--authored", help="write the body's game mesh as an authored model for the converter (.json, + own .png)"
    )
    return ap.parse_args(argv)


ARGS = parse_args()
OUT = Path(ARGS.out).resolve()
OUT.mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------------------- helpers


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def gauss(p, c, s):
    """Anisotropic gaussian; s is (sx, sy, sz)."""
    d = 0.0
    for i in range(3):
        d += ((p[i] - c[i]) / s[i]) ** 2
    return math.exp(-0.5 * d)


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def mesh_obj(name, verts, faces=(), edges=()):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], list(edges), list(faces))
    me.update()
    return link(bpy.data.objects.new(name, me))


def apply_modifiers(obj):
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
        for m in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=m.name)


def fill_paint(obj, rgba=(0.80, 0.60, 0.48, 1.0)):
    """Give a skin mesh the 'paint' colour layer the skin material multiplies (the head paints lips and cheeks)."""
    a = obj.data.color_attributes.new("paint", "BYTE_COLOR", "CORNER")  # sRGB, like the head layer
    for d in a.data:
        d.color_srgb = rgba


def shade_smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True


def set_input(node, names, value):
    for n in names if isinstance(names, (list, tuple)) else [names]:
        if n in node.inputs:
            node.inputs[n].default_value = value
            return


# ---------------------------------------------------------------------------- materials

MATS = {}


def principled_mat(name, color, rough=0.6, sss=0.0, sheen=0.0, spec=0.5, bump=None, vary=0.0, weave=0.0):
    """Principled material with optional procedural colour variation and cloth weave bump."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    set_input(bsdf, "Roughness", rough)
    set_input(bsdf, ["Specular IOR Level", "Specular"], spec)
    if sss:
        set_input(bsdf, ["Subsurface Weight", "Subsurface"], sss)
        set_input(bsdf, "Subsurface Radius", (0.9, 0.35, 0.2))
        set_input(bsdf, "Subsurface Scale", 0.006)
    if sheen:
        set_input(bsdf, ["Sheen Weight", "Sheen"], sheen)
    base = (*color, 1.0)
    if ARGS.clay:
        set_input(bsdf, "Base Color", (0.55, 0.53, 0.5, 1))
        set_input(bsdf, "Roughness", 0.55)
        MATS[name] = m
        return m
    if vary > 0:
        tc = nt.nodes.new("ShaderNodeTexCoord")
        noise = nt.nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 18.0
        noise.inputs["Detail"].default_value = 6.0
        nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
        ramp = nt.nodes.new("ShaderNodeValToRGB")
        lo = tuple(max(0.0, c * (1 - vary)) for c in color)
        hi = tuple(min(1.0, c * (1 + vary)) for c in color)
        ramp.color_ramp.elements[0].color = (*lo, 1)
        ramp.color_ramp.elements[1].color = (*hi, 1)
        nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
        nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        set_input(bsdf, "Base Color", base)
    if weave > 0 or bump:
        tc = nt.nodes.new("ShaderNodeTexCoord")
        bn = nt.nodes.new("ShaderNodeBump")
        bn.inputs["Strength"].default_value = 0.25
        if weave > 0:
            w1 = nt.nodes.new("ShaderNodeTexWave")
            w1.inputs["Scale"].default_value = weave
            w1.wave_type = "BANDS"
            nt.links.new(tc.outputs["Object"], w1.inputs["Vector"])
            src = w1.outputs["Fac"]
        else:
            nz = nt.nodes.new("ShaderNodeTexNoise")
            nz.inputs["Scale"].default_value = bump
            nz.inputs["Detail"].default_value = 8.0
            nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
            src = nz.outputs["Fac"]
        nt.links.new(src, bn.inputs["Height"])
        nt.links.new(bn.outputs["Normal"], bsdf.inputs["Normal"])
    MATS[name] = m
    return m


def eye_mat():
    m = bpy.data.materials.new("eye")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    set_input(bsdf, "Roughness", 0.08)
    if ARGS.clay:
        set_input(bsdf, "Base Color", (0.55, 0.53, 0.5, 1))
        return m
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Normal"], sep.inputs[0])
    neg = nt.nodes.new("ShaderNodeMath")
    neg.operation = "MULTIPLY"
    neg.inputs[1].default_value = -1.0
    nt.links.new(sep.outputs["Y"], neg.inputs[0])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.interpolation = "LINEAR"
    cr.elements[0].position = 0.80
    cr.elements[0].color = (0.85, 0.82, 0.78, 1)  # sclera
    cr.elements[1].position = 0.83
    cr.elements[1].color = (0.06, 0.12, 0.07, 1)  # iris edge
    e = cr.elements.new(0.92)
    e.color = (0.16, 0.34, 0.16, 1)  # green iris, as the 1999 face texture
    e = cr.elements.new(0.945)
    e.color = (0.02, 0.015, 0.01, 1)  # pupil
    nt.links.new(neg.outputs[0], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    MATS["eye"] = m
    return m


def skin_mat():
    """Skin with a painted colour attribute (lips, cheeks) multiplied over the base tone."""
    m = principled_mat("skin", (0.52, 0.32, 0.23), rough=0.5, sss=0.25, spec=0.4)
    if ARGS.clay:
        return m
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    attr = nt.nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "paint"
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 90.0
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 0.08
    nt.links.new(attr.outputs["Color"], mix.inputs["A"])
    nt.links.new(nz.outputs["Color"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
    bn = nt.nodes.new("ShaderNodeBump")
    bn.inputs["Strength"].default_value = 0.05
    nt.links.new(nz.outputs["Fac"], bn.inputs["Height"])
    nt.links.new(bn.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def build_materials():
    skin_mat()
    principled_mat("shirt", (0.40, 0.29, 0.18), rough=0.85, sheen=0.1, vary=0.08, weave=180)  # undyed linen
    principled_mat("trim", (0.06, 0.035, 0.02), rough=0.8, sheen=0.1, vary=0.1, weave=180)
    principled_mat("trousers", (0.12, 0.072, 0.042), rough=0.8, sheen=0.1, vary=0.12, weave=160)
    principled_mat("leather", (0.10, 0.065, 0.042), rough=0.6, spec=0.4, vary=0.18, bump=60)  # soft boots
    principled_mat("shoe", (0.035, 0.022, 0.015), rough=0.45, spec=0.5, vary=0.12, bump=80)
    principled_mat("belt", (0.065, 0.036, 0.018), rough=0.5, spec=0.5, vary=0.1, bump=80)
    principled_mat("metal", (0.62, 0.42, 0.17), rough=0.35, spec=0.8)
    principled_mat("hair", (0.022, 0.015, 0.011), rough=0.45, spec=0.5, vary=0.3)
    principled_mat("brow", (0.02, 0.014, 0.01), rough=0.6)
    principled_mat("lid", (0.46, 0.27, 0.20), rough=0.5, sss=0.2)
    eye_mat()
    if "Metallic" in MATS["metal"].node_tree.nodes["Principled BSDF"].inputs:
        MATS["metal"].node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 1.0


def add_mat(obj, name):
    obj.data.materials.append(MATS[name])
    return len(obj.data.materials) - 1


# ---------------------------------------------------------------------------- body (skin graph)


class Graph:
    def __init__(self):
        self.v, self.r, self.e, self.tag, self.names = [], [], [], [], {}

    def add(self, name, pos, r):
        self.names[name] = len(self.v)
        self.v.append(Vector(pos))
        self.r.append((r, r) if isinstance(r, (int, float)) else r)
        return self.names[name]

    def edge(self, a, b, tag):
        self.e.append((self.names[a], self.names[b]))
        self.tag.append(tag)


ARM_DIR = Vector((1.0, 0.0, 0.015)).normalized()  # T-pose like the PC rig, right arm (+X)
IN_DIR = Vector((0.0, 0.0, -1.0))  # palms face down

# Joint heights follow the 1999 Delon's bind pose (shoulders 1.40 m, hips 0.95 m, knees 0.53 m, height 1.72 m).


def build_graph():
    g = Graph()
    # slim like the 1999 Delon (chest about 0.23 m, hips about 0.21 m across)
    g.add("pelvis", (0, 0.010, 0.96), (0.115, 0.090))
    g.add("waist", (0, 0.014, 1.06), (0.098, 0.074))
    g.add("spine", (0, 0.008, 1.16), (0.106, 0.082))
    g.add("chest", (0, 0.002, 1.27), (0.122, 0.090))
    g.add("uchest", (0, 0.012, 1.355), (0.116, 0.078))
    g.add("neck0", (0, 0.026, 1.425), (0.040, 0.040))
    g.add("neck1", (0, 0.020, 1.53), (0.036, 0.037))
    g.edge("pelvis", "waist", "trousers")
    g.edge("waist", "spine", "shirt")
    g.edge("spine", "chest", "shirt")
    g.edge("chest", "uchest", "shirt")
    g.edge("uchest", "neck0", "shirt")
    g.edge("neck0", "neck1", "skin")
    for s, sx in (("R", 1), ("L", -1)):

        def m(p, sx=sx):
            return (p[0] * sx, p[1], p[2])

        sh = Vector((0.17, 0.025, 1.395))
        el = sh + ARM_DIR * 0.235
        wr = el + ARM_DIR * 0.235
        g.add("clav" + s, m((0.075, 0.024, 1.41)), 0.050)
        g.add("shoulder" + s, m(sh), 0.046)
        g.add("uarm" + s, m(sh + ARM_DIR * 0.11), 0.038)
        g.add("elbow" + s, m(el), 0.032)
        g.add("farm" + s, m(el + ARM_DIR * 0.11), 0.033)
        g.add("wrist" + s, m(wr), 0.025)
        palm = wr + ARM_DIR * 0.045
        g.add("palm" + s, m(palm), 0.024)
        g.edge("uchest", "clav" + s, "shirt")
        g.edge("clav" + s, "shoulder" + s, "shirt")
        g.edge("shoulder" + s, "uarm" + s, "shirt")
        g.edge("uarm" + s, "elbow" + s, "shirt")
        g.edge("elbow" + s, "farm" + s, "shirt")
        g.edge("farm" + s, "wrist" + s, "shirt")
        g.edge("wrist" + s, "palm" + s, "skin")
        # fingers: index (front, -Y) to little (back, +Y), curling a little downwards
        for i, (dy, ln, rr) in enumerate(
            [(-0.019, 0.068, 0.0082), (-0.006, 0.076, 0.0085), (0.0065, 0.071, 0.0081), (0.0185, 0.058, 0.0072)]
        ):
            k0 = wr + ARM_DIR * 0.082 + Vector((0, dy, 0))
            k1 = k0 + ARM_DIR * ln * 0.5 + IN_DIR * 0.005
            k2 = k0 + ARM_DIR * ln * 0.92 + IN_DIR * 0.014
            n = f"f{i}{s}"
            g.add(n + "a", m(k0), rr)
            g.add(n + "b", m(k1), rr * 0.9)
            g.add(n + "c", m(k2), rr * 0.75)
            g.edge("palm" + s, n + "a", "skin")
            g.edge(n + "a", n + "b", "skin")
            g.edge(n + "b", n + "c", "skin")
        t0 = wr + ARM_DIR * 0.022 + Vector((0, -0.020, 0)) + IN_DIR * 0.008
        t1 = t0 + ARM_DIR * 0.026 + Vector((0, -0.022, 0)) + IN_DIR * 0.004
        t2 = t1 + ARM_DIR * 0.028 + Vector((0, -0.008, 0)) + IN_DIR * 0.002
        g.add("t0" + s, m(t0), 0.012)
        g.add("t1" + s, m(t1), 0.010)
        g.add("t2" + s, m(t2), 0.0085)
        g.edge("wrist" + s, "t0" + s, "skin")
        g.edge("t0" + s, "t1" + s, "skin")
        g.edge("t1" + s, "t2" + s, "skin")
        # leg
        g.add("hip" + s, m((0.085, 0.012, 0.92)), 0.068)
        g.add("thigh" + s, m((0.086, 0.008, 0.73)), 0.058)
        g.add("knee" + s, m((0.083, 0.000, 0.53)), 0.044)
        g.add("calf" + s, m((0.083, 0.016, 0.38)), 0.047)
        g.add("shin" + s, m((0.085, 0.018, 0.22)), 0.042)
        g.add("ankle" + s, m((0.085, 0.025, 0.090)), 0.040)
        g.add("heel" + s, m((0.085, 0.055, 0.040)), 0.038)
        g.add("ball" + s, m((0.090, -0.070, 0.028)), (0.040, 0.028))
        g.add("toe" + s, m((0.093, -0.130, 0.030)), (0.030, 0.022))
        g.edge("pelvis", "hip" + s, "trousers")
        g.edge("hip" + s, "thigh" + s, "trousers")
        g.edge("thigh" + s, "knee" + s, "trousers")
        g.edge("knee" + s, "calf" + s, "leather")
        g.edge("calf" + s, "shin" + s, "leather")
        g.edge("shin" + s, "ankle" + s, "leather")
        g.edge("ankle" + s, "heel" + s, "shoe")
        g.edge("ankle" + s, "ball" + s, "shoe")
        g.edge("ball" + s, "toe" + s, "shoe")
    return g


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length, t


def build_body(g):
    obj = mesh_obj("Delon_Body", g.v, edges=g.e)
    sk = obj.modifiers.new("skin", "SKIN")
    sk.use_smooth_shade = True
    sk.branch_smoothing = 0.6
    for i, r in enumerate(g.r):
        obj.data.skin_vertices[0].data[i].radius = (r[0] * 1.18, r[1] * 1.18)
    obj.data.skin_vertices[0].data[g.names["pelvis"]].use_root = True
    ss = obj.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 2
    rm = obj.modifiers.new("remesh", "REMESH")
    rm.mode = "VOXEL"
    rm.voxel_size = 0.0035
    sm = obj.modifiers.new("smooth", "CORRECTIVE_SMOOTH")
    sm.iterations = 6
    sm.use_only_smooth = True
    apply_modifiers(obj)
    shade_smooth(obj)
    # material per face from the nearest graph edge (distance relative to radius)
    slot = {}
    for t in sorted(set(g.tag) | {"trim", "shoe", "leather", "skin"}):
        slot[t] = add_mat(obj, t)
    for poly in obj.data.polygons:
        c = poly.center
        best, tag = 1e9, "shirt"
        for (a, b), t in zip(g.e, g.tag):
            d, u = seg_dist(c, g.v[a], g.v[b])
            ra = sum(g.r[a]) * 0.5
            rb = sum(g.r[b]) * 0.5
            d = d / (ra + (rb - ra) * u)
            if d < best:
                best, tag = d, t
        if tag in ("trousers", "leather"):
            tag = "leather" if c.z < 0.50 else "trousers"
        if tag == "trousers" and c.z > 1.0:  # under the tunic hem (leggings up to there, in case the hem lifts)
            tag = "shirt"
        if tag in ("leather", "shoe"):
            tag = "shoe" if c.z < 0.075 else "leather"
        if tag == "shirt" and c.z > 1.29:  # V-neck with a dark trim, open to the neck
            half = (c.z - 1.31) * 0.62
            if c.z > 1.445 or (c.y < 0 and abs(c.x) < half):
                tag = "skin"
            elif c.y < 0 and abs(c.x) < half + 0.014:
                tag = "trim"
        poly.material_index = slot[tag]
    fill_paint(obj)
    return obj


# ---------------------------------------------------------------------------- rings fitted to the body


def body_profile(body_bm_verts, center, axis, lo, hi, sectors, maxr=0.25):
    """Max radial distance of body vertices around an axis, per angular sector, for heights in [lo, hi]."""
    axis = axis.normalized()
    ref = Vector((0, -1, 0)) if abs(axis.y) < 0.9 else Vector((1, 0, 0))
    u = (ref - axis * ref.dot(axis)).normalized()
    w = axis.cross(u)
    prof = [0.0] * sectors
    for v in body_bm_verts:
        d = v - center
        h = d.dot(axis)
        if h < lo or h > hi:
            continue
        x, y = d.dot(u), d.dot(w)
        r = math.hypot(x, y)
        if r > maxr:
            continue
        k = int(((math.atan2(y, x) / (2 * math.pi)) % 1.0) * sectors) % sectors
        prof[k] = max(prof[k], r)
    # fill empty sectors and soften
    for _ in range(3):
        prof = [max(prof[i], 0.5 * (prof[i - 1] + prof[(i + 1) % sectors]) * 0.98) for i in range(sectors)]
    sm = [(prof[i - 1] + 2 * prof[i] + prof[(i + 1) % sectors]) / 4 for i in range(sectors)]
    return sm, u, w, axis


def ring_band(
    name, verts_world, center, axis, rows, sectors=48, mat="belt", thick=0.004, mono=False, fold=None, maxr=0.25
):
    """rows: list of (height along axis, extra offset). Each row is fitted to the body profile."""
    vs, faces = [], []
    prev = None
    for ri, (h, off) in enumerate(rows):
        prof, u, w, ax = body_profile(verts_world, center, axis, h - 0.012, h + 0.012, sectors, maxr)
        if mono and prev is not None:
            prof = [max(p, q) for p, q in zip(prof, prev)]
        prev = prof
        for k in range(sectors):
            a = (k + 0.5) / sectors * 2 * math.pi
            r = prof[k] + off + (fold(a, ri / max(1, len(rows) - 1)) if fold else 0.0)
            vs.append(center + ax * h + (u * math.cos(a) + w * math.sin(a)) * r)
    for ri in range(len(rows) - 1):
        for k in range(sectors):
            a, b = ri * sectors + k, ri * sectors + (k + 1) % sectors
            faces.append((a, b, b + sectors, a + sectors))
    obj = mesh_obj(name, vs, faces)
    add_mat(obj, mat)
    so = obj.modifiers.new("solid", "SOLIDIFY")
    so.thickness = thick
    so.offset = 1.0
    ss = obj.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 1
    shade_smooth(obj)
    return obj


def build_clothing(body, g):
    verts = [v.co.copy() for v in body.data.vertices]
    V = g.v
    nm = g.names
    up = Vector((0, 0, 1))
    # tunic hem: from under the belt to the upper thigh, nearly straight (radius never shrinks going down)
    rows = []
    n = 10
    for i in range(n):
        t = i / (n - 1)
        rows.append((1.045 - 0.155 * t, 0.005 + 0.005 * t * t))  # ends where the 1999 tunic does, barely flared

    def hem_fold(a, t):
        return t * (0.004 * math.sin(7 * a) + 0.003 * math.sin(17 * a + 1.3))

    hem = ring_band(
        "Delon_Hem",
        verts,
        Vector((0, 0.010, 0)),
        -up,
        [(-h, o) for h, o in rows],
        64,
        "shirt",
        0.004,
        mono=True,
        fold=hem_fold,
    )
    # low-slung belt, big square buckle
    ring_band("Delon_Belt", verts, Vector((0, 0.010, 0)), up, [(1.045, 0.010), (1.085, 0.010)], 64, "belt", 0.006)
    for s in "RL":
        ax = V[nm["palm" + s]] - V[nm["wrist" + s]]
        ring_band(
            "Delon_Cuff" + s,
            verts,
            V[nm["wrist" + s]],
            -ax,
            [(0.004, 0.004), (0.024, 0.006)],
            32,
            "shirt",
            0.003,
            maxr=0.06,
        )
        # knee-high soft boots: a turned-down top just under the knee
        ring_band(
            "Delon_BootTop" + s,
            verts,
            Vector((V[nm["knee" + s]].x, 0.016, 0)),
            up,
            [(0.50, 0.002), (0.53, 0.006), (0.545, 0.006)],
            40,
            "leather",
            0.004,
            maxr=0.09,
        )
        # leggings laced up the outer thigh
        sx = 1 if s == "R" else -1
        out = outer_x(verts, sx)
        for i in range(6):
            z = 0.86 - i * 0.05
            p0 = Vector((out(z) * sx + 0.002 * sx, -0.012, z))
            p1 = Vector((out(z - 0.035) * sx + 0.002 * sx, 0.012, z - 0.035))
            q0 = Vector((p0.x, 0.012, z))
            q1 = Vector((p1.x, -0.012, z - 0.035))
            for a0, a1 in ((p0, p1), (q0, q1)):
                cord_curve("Delon_Lace", [a0, (a0 + a1) / 2 + Vector((0.003 * sx, 0, 0)), a1], 0.0018, "belt")
    # V-neck trim: a flat cord along each edge of the opening hides the stepped material border
    for sx in (-1, 1):
        pts = []
        for k in range(9):
            z = 1.31 + k * 0.0165
            x = sx * ((z - 1.31) * 0.62 + 0.007)
            pts.append(Vector((x, front_y(verts, x, z) - 0.001, z)))
        cord_curve("Delon_Neckline", pts, 0.0065, "trim")
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, -0.095, 1.065))
    buckle = bpy.context.active_object
    buckle.name = "Delon_Buckle"
    buckle.scale = (0.064, 0.012, 0.056)
    bev = buckle.modifiers.new("bev", "BEVEL")
    bev.width = 0.004
    bev.segments = 3
    add_mat(buckle, "metal")
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, -0.101, 1.065))
    plate = bpy.context.active_object
    plate.name = "Delon_BucklePlate"
    plate.scale = (0.040, 0.006, 0.034)
    add_mat(plate, "belt")
    return hem


def front_y(verts, x, z):
    """Front surface (min Y) of the body at (x, z)."""
    ys = [v.y for v in verts if abs(v.x - x) < 0.006 and abs(v.z - z) < 0.006 and v.y < 0.02]
    return min(ys, default=-0.1)


def outer_x(verts, sx):
    """Outer (|x| max) surface of a thigh near y = 0 as a function of z."""
    pts = [(v.z, abs(v.x)) for v in verts if v.x * sx > 0.04 and abs(v.y - 0.008) < 0.015 and 0.4 < v.z < 0.95]

    def f(z):
        return max((x for vz, x in pts if abs(vz - z) < 0.008), default=0.15)

    return f


def cord_curve(name, pts, radius, mat, radii=None):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 2
    cu.resolution_u = 6
    cu.use_fill_caps = True
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(pts) - 1)
    for i, p in enumerate(pts):
        bp = sp.bezier_points[i]
        bp.co = p
        bp.handle_left_type = bp.handle_right_type = "AUTO"
        bp.radius = radii[i] if radii else 1.0
    obj = link(bpy.data.objects.new(name, cu))
    obj.data.materials.append(MATS[mat])
    return obj


# ---------------------------------------------------------------------------- head

HEAD_C = Vector((0.0, 0.010, 1.607))
HEAD_S = Vector((0.087, 0.102, 0.112))  # broad, short face like the 1999 head


def h2w(p):
    return HEAD_C + Vector((p[0] * HEAD_S.x, p[1] * HEAD_S.y, p[2] * HEAD_S.z))


def shape_head(u):
    x, y, z = u
    # cranium: fuller at the back and top
    if y > 0:
        y *= 1.0 + 0.06 * smoothstep(-0.2, 0.6, z)
    # jaw and lower head taper
    t = smoothstep(0.05, -0.95, z)
    x *= 1.0 - 0.07 * t
    if y > 0:
        y *= 1.0 - 0.42 * t
    else:
        y *= 1.0 - 0.06 * t
    # flatten the temples a little
    x *= 1.0 - 0.05 * gauss((x, y, z), (0.9, -0.2, 0.35), (0.3, 0.4, 0.25))
    p = (x, y, z)
    front = smoothstep(-0.15, -0.65, y)
    d = 0.0
    # nose: bridge to tip, widening at the base
    if -0.42 < z < 0.3:
        a = 0.015 + 0.10 * smoothstep(0.16, -0.20, z)
        a *= smoothstep(-0.40, -0.25, z)
        w = 0.075 + 0.07 * smoothstep(0.0, -0.3, z)
        d += a * math.exp(-((x / w) ** 2))
    d += 0.05 * gauss(p, (0.0, -0.95, -0.24), (0.07, 1, 0.06))  # tip bulb
    for sx in (-1, 1):
        d += 0.045 * gauss(p, (0.12 * sx, -0.9, -0.27), (0.055, 1, 0.045))  # nostril wings
        d += 0.06 * gauss(p, (0.33 * sx, -0.9, 0.30), (0.17, 1, 0.055))  # brow ridge
        d -= 0.065 * gauss(p, (0.38 * sx, -0.9, 0.13), (0.12, 1, 0.07))  # eye socket
        d += 0.035 * gauss(p, (0.56 * sx, -0.75, -0.04), (0.12, 1, 0.12))  # cheekbone
        d -= 0.02 * gauss(p, (0.42 * sx, -0.8, -0.32), (0.12, 1, 0.12))  # cheek hollow
    d += 0.07 * gauss(p, (0.0, -0.9, -0.45), (0.30, 1, 0.20))  # muzzle
    d += 0.026 * gauss(p, (0.0, -0.95, -0.395), (0.16, 1, 0.032))  # upper lip
    d += 0.032 * gauss(p, (0.0, -0.95, -0.50), (0.14, 1, 0.040))  # lower lip
    d -= 0.030 * gauss(p, (0.0, -0.95, -0.452), (0.17, 1, 0.012))  # mouth line
    d -= 0.020 * gauss(p, (0.0, -0.95, -0.335), (0.04, 1, 0.04))  # philtrum
    d += 0.045 * gauss(p, (0.0, -0.85, -0.76), (0.32, 1, 0.12))  # broad, square chin
    for sx in (-1, 1):  # jaw corners
        x += sx * 0.06 * gauss((x, y, z), (0.72 * sx, -0.1, -0.62), (0.2, 0.35, 0.16))
    y -= d * front
    return Vector((x, y, z))


def paint_colour(u):
    """Skin tone plus lips and cheeks, in linear RGB."""
    base = Vector((0.80, 0.60, 0.48))  # sRGB values (the layer is a byte colour)
    x, y, z = u
    p = (x, y, z)
    lips = gauss(p, (0, -1, -0.45), (0.15, 9, 0.06)) * smoothstep(-0.5, -0.9, y)
    cheek = sum(gauss(p, (0.5 * s, -0.8, -0.15), (0.16, 1, 0.14)) for s in (-1, 1)) * smoothstep(-0.3, -0.8, y)
    stubble = gauss(p, (0, -0.7, -0.7), (0.5, 1, 0.25)) * 0.0
    c = base.lerp(Vector((0.70, 0.43, 0.39)), min(1.0, lips * 1.1))
    c = c.lerp(Vector((0.42, 0.24, 0.21)), gauss(p, (0, -1, -0.452), (0.15, 9, 0.010)) * smoothstep(-0.5, -0.9, y))
    c = c.lerp(Vector((0.84, 0.56, 0.48)), cheek * 0.35)
    c = c * (1 - stubble)
    return (c.x, c.y, c.z, 1.0)


def build_head():
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=128, v_segments=96, radius=1.0)
    col = bm.loops.layers.color.new("paint")
    for v in bm.verts:
        u = v.co.normalized()
        v.co = h2w(shape_head(u))
    for f in bm.faces:
        for lp in f.loops:
            p = lp.vert.co
            u = Vector(((p.x - HEAD_C.x) / HEAD_S.x, (p.y - HEAD_C.y) / HEAD_S.y, (p.z - HEAD_C.z) / HEAD_S.z))
            lp[col] = paint_colour(u)
    me = bpy.data.meshes.new("Delon_Head")
    bm.to_mesh(me)
    bm.free()
    head = link(bpy.data.objects.new("Delon_Head", me))
    shade_smooth(head)
    add_mat(head, "skin")
    ss = head.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 1
    # ears
    for sx in (-1, 1):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, radius=1)
        ear = bpy.context.active_object
        ear.name = "Delon_Ear"
        ear.scale = (0.009, 0.019, 0.031)
        ear.location = h2w((0.93 * sx, 0.08, 0.02))
        ear.rotation_euler = (math.radians(-12), math.radians(-14 * sx), math.radians(14 * sx))
        bm = bmesh.new()
        bm.from_mesh(ear.data)
        for v in bm.verts:  # cup the ear: push the outer face in, curl the rim
            if v.co.x * sx > 0.2:
                v.co.x -= 0.6 * sx * max(0.0, 1 - (v.co.y**2 + v.co.z**2) * 1.6)
        bm.to_mesh(ear.data)
        bm.free()
        shade_smooth(ear)
        add_mat(ear, "skin")
        fill_paint(ear)
    # eyes
    eyes = []
    for sx in (-1, 1):
        c = h2w((0.385 * sx, -0.76, 0.125))
        bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=32, radius=0.0135, location=c)
        e = bpy.context.active_object
        e.name = "Delon_Eye"
        e.rotation_euler = (0, 0, math.radians(4 * sx))
        shade_smooth(e)
        e.data.materials.append(bpy.data.materials["eye"])
        eyes.append(e)
        # lids: partial shells above and below the eyeball
        for upper in (True, False):
            bm = bmesh.new()
            bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=24, radius=0.0142)
            kill = []
            for v in bm.verts:
                math.atan2(-v.co.y, v.co.z if upper else -v.co.z)
                if v.co.y > 0.004 or (upper and v.co.z < 0.0062) or (not upper and v.co.z > -0.0080):
                    kill.append(v)
            bmesh.ops.delete(bm, geom=kill, context="VERTS")
            me = bpy.data.meshes.new("lid")
            bm.to_mesh(me)
            bm.free()
            lid = link(bpy.data.objects.new("Delon_Lid", me))
            lid.location = c
            lid.rotation_euler = (math.radians(8 if upper else -4), 0, math.radians(4 * sx))
            so = lid.modifiers.new("solid", "SOLIDIFY")
            so.thickness = 0.0016
            ss = lid.modifiers.new("sub", "SUBSURF")
            ss.levels = ss.render_levels = 1
            shade_smooth(lid)
            add_mat(lid, "lid")
        # eyebrow: a few tapered strands along the brow ridge
        for j in range(5):
            o = (j - 3) * 0.004
            p0 = h2w((0.16 * sx, -0.97, 0.27 + o * 2))
            p1 = h2w((0.38 * sx, -1.00, 0.33 + o * 2))
            p2 = h2w((0.60 * sx, -0.86, 0.29 + o * 1.6))
            cord_curve("Delon_Brow", [p0, p1, p2], 0.0016, "brow", radii=[0.9, 1.0, 0.3])
    return head, eyes


def build_hair():
    """Clumped strands following the scalp, with a fringe falling to the brows."""
    import random

    rnd = random.Random(7)
    hair_objs = []
    # scalp cap
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=96, v_segments=64, radius=1.0)
    kill = []
    for v in bm.verts:
        u = v.co.normalized()
        # hairline: higher at the forehead and temples, low at the nape, above the ears
        limit = 0.45 if u.y < -0.4 else (0.05 if abs(u.x) > 0.7 and u.y < 0.3 else -0.55)
        if u.y < -0.4:
            limit = 0.60 + 0.08 * abs(u.x)
        if u.z < limit:
            kill.append(v)
        else:
            v.co = h2w(shape_head(u) * 1.035)
    bmesh.ops.delete(bm, geom=kill, context="VERTS")
    me = bpy.data.meshes.new("Delon_Scalp")
    bm.to_mesh(me)
    bm.free()
    cap = link(bpy.data.objects.new("Delon_Scalp", me))
    shade_smooth(cap)
    add_mat(cap, "hair")
    ss = cap.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 1
    hair_objs.append(cap)

    def sph(theta, phi, r):
        # theta: polar from the crown (0 = top), phi: azimuth (0 = front -Y, +pi/2 = +X)
        u = Vector((math.sin(theta) * math.sin(phi), -math.sin(theta) * math.cos(phi), math.cos(theta)))
        return h2w(shape_head(u) * r)

    cu = bpy.data.curves.new("Delon_Hair", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = 0.0095
    cu.bevel_resolution = 1
    cu.resolution_u = 5
    cu.use_fill_caps = False
    crown_phi = math.pi  # whorl slightly back
    n = 330
    for i in range(n):
        phi = rnd.uniform(-math.pi, math.pi)
        theta0 = rnd.uniform(0.02, 0.5)
        frontness = math.cos(phi)  # 1 at the front
        side = abs(math.sin(phi))
        if frontness > 0.55:  # fringe: swept from a parting on his right, ends at mid-forehead to the brow
            theta1 = rnd.uniform(1.18, 1.42) - 0.08 * side  # strands hang to the eyes
            dphi = -0.35 * (0.6 + 0.4 * rnd.random()) + 0.15 * math.sin(phi)
        elif frontness > -0.25:  # sides: down over the top half of the ears
            theta1 = rnd.uniform(1.80, 2.05)  # over the ears to the jaw
            dphi = rnd.uniform(-0.06, 0.06) - 0.10 * frontness * math.copysign(1, math.sin(phi))
        else:  # back: tapered to the nape
            theta1 = rnd.uniform(2.05, 2.30)
            dphi = rnd.uniform(-0.08, 0.08)
        k = 7
        pts, radii = [], []
        for j in range(k):
            t = j / (k - 1)
            th = theta0 + (theta1 - theta0) * t
            ph = phi + dphi * t * t
            r = 1.055 + 0.045 * math.sin(math.pi * t) + 0.02 * t + rnd.uniform(-0.006, 0.006)
            p = sph(th, ph, r)
            if j == k - 1:  # free tip: drop a little and flick
                p += Vector((rnd.uniform(-0.004, 0.004), rnd.uniform(-0.004, 0.004), -0.006))
            pts.append(p)
            radii.append((1.0 - 0.95 * t**1.5) * rnd.uniform(0.85, 1.2))
        sp = cu.splines.new("BEZIER")
        sp.bezier_points.add(k - 1)
        for j, p in enumerate(pts):
            bp = sp.bezier_points[j]
            bp.co = p
            bp.handle_left_type = bp.handle_right_type = "AUTO"
            bp.radius = radii[j]
    _ = crown_phi
    obj = link(bpy.data.objects.new("Delon_Hair", cu))
    obj.data.materials.append(MATS["hair"])
    hair_objs.append(obj)
    return hair_objs


# ---------------------------------------------------------------------------- rig


def build_rig(g, meshes, rigid):
    """Armature named and placed like the 1999 PC Delon rig (fynn/upperbody/torso/chest/neck/head, Rcollar/Ruparm/
    Rforearm, lowerbody/Rhip/Rthigh/Rcalf/Rfoot, R on +X as the dump shows it), so its clips can be retargeted by name.
    The PC rig has no hand or toe joints and its finger joints hang off the forearm; the extra hand, finger, toe and
    jaw bones here are additions."""
    V, nm = g.v, g.names
    arm = bpy.data.armatures.new("Delon_Rig")
    rig = link(bpy.data.objects.new("Delon_Rig", arm))
    rig.show_in_front = True
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones

    def bone(name, h, t, parent=None, connect=False):
        b = eb.new(name)
        b.head, b.tail = Vector(h), Vector(t)
        if parent:
            b.parent = eb[parent]
            b.use_connect = connect
        return b

    root = V[nm["pelvis"]]
    z = lambda h: Vector((0, 0.02, h))
    bone("fynn", root, root + Vector((0, 0.08, 0)))
    bone("upperbody", root, z(1.115), "fynn")
    bone("torso", z(1.115), z(1.267), "upperbody", True)
    bone("chest", z(1.267), z(1.46), "torso", True)
    bone("neck", z(1.46), z(1.523), "chest", True)
    bone("head", z(1.523), z(1.72), "neck", True)
    bone("jaw", h2w((0, 0.15, -0.25)), h2w((0, -0.85, -0.78)), "head")
    bone("lowerbody", root, root - Vector((0, 0, 0.06)), "fynn")
    for s, sx in (("R", 1), ("L", -1)):
        bone(s + "collar", (0.015 * sx, 0.02, 1.396), V[nm["shoulder" + s]], "chest")
        bone(s + "uparm", V[nm["shoulder" + s]], V[nm["elbow" + s]], s + "collar", True)
        bone(s + "forearm", V[nm["elbow" + s]], V[nm["wrist" + s]], s + "uparm", True)
        bone(
            s + "hand",
            V[nm["wrist" + s]],
            V[nm["palm" + s]] + (V[nm["palm" + s]] - V[nm["wrist" + s]]),
            s + "forearm",
            True,
        )
        for i in range(4):
            f = f"f{i}{s}"
            bone(f"{s}finger{i}_1", V[nm[f + "a"]], V[nm[f + "b"]], s + "hand")
            bone(f"{s}finger{i}_2", V[nm[f + "b"]], V[nm[f + "c"]], f"{s}finger{i}_1", True)
        bone(s + "thumb_1", V[nm["t0" + s]], V[nm["t1" + s]], s + "hand")
        bone(s + "thumb_2", V[nm["t1" + s]], V[nm["t2" + s]], s + "thumb_1", True)
        bone(s + "hip", root, V[nm["hip" + s]], "lowerbody")
        bone(s + "thigh", V[nm["hip" + s]], V[nm["knee" + s]], s + "hip", True)
        bone(s + "calf", V[nm["knee" + s]], V[nm["ankle" + s]], s + "thigh", True)
        bone(s + "foot", V[nm["ankle" + s]], V[nm["ball" + s]], s + "calf", True)
        bone(s + "toe", V[nm["ball" + s]], V[nm["toe" + s]], s + "foot", True)
    for b in eb:  # helper bones that should not take skin weights
        b.use_deform = b.name not in {"fynn", "lowerbody"} and not b.name.endswith("hip")
    bpy.ops.object.mode_set(mode="OBJECT")
    # skinned meshes: automatic (bone heat) weights
    bpy.ops.object.select_all(action="DESELECT")
    for m in meshes:
        m.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    # head parts ride the head bone
    for o in rigid:
        mw = o.matrix_world.copy()
        o.parent = rig
        o.parent_type = "BONE"
        o.parent_bone = "head"
        o.matrix_world = mw
    return rig


# ---------------------------------------------------------------------------- game LOD


LOD_BUDGET = {  # triangles per part, about 3,900 in all (Drakan 2 humans are 1,900 to 2,300 at LOD 0, the engine cap is
    # 4,000 vertices); brows, laces and hair strands are left to the texture bake / hair cards
    "Delon_Body": 1750,
    "Delon_Head": 520,
    "Delon_Ear": 50,
    "Delon_Eye": 48,
    "Delon_Lid": 20,
    "Delon_Scalp": 700,
    "Delon_Hair": 0,
    "Delon_Hem": 150,
    "Delon_Belt": 110,
    "Delon_Buckle": 12,
    "Delon_BucklePlate": 12,
    "Delon_Cuff": 40,
    "Delon_BootTop": 60,
    "Delon_Neckline": 24,
}


def lod_budget(name):
    for k, v in sorted(LOD_BUDGET.items(), key=lambda kv: -len(kv[0])):
        if name.startswith(k):
            return v
    return 0


def build_game_lod(objs, name="Delon_Game"):
    """One mesh for the game: each part decimated to its own budget, then joined."""
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    mats = []
    for o in objs:
        budget = lod_budget(o.name)
        if o.type not in {"MESH", "CURVE"} or budget <= 0:
            continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
        me.transform(o.matrix_world)
        tmp = link(bpy.data.objects.new("lodtmp", me))
        tris = sum(len(p.vertices) - 2 for p in me.polygons)
        if tris > budget:
            dec = tmp.modifiers.new("dec", "DECIMATE")
            dec.ratio = budget / tris
            dec.use_collapse_triangulate = True
            apply_modifiers(tmp)
        me = tmp.data
        remap = []
        for m in me.materials:
            if m not in mats:
                mats.append(m)
            remap.append(mats.index(m))
        for p in me.polygons:
            p.material_index = remap[p.material_index] if remap else 0
        bm.from_mesh(me)
        bpy.data.objects.remove(tmp)
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    lod = link(bpy.data.objects.new(name, me))
    lod.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(lod)
    shade_smooth(lod)
    return lod, len(me.polygons)


# ---------------------------------------------------------------------------- own texture (game LOD)
#
# The game mesh of the body (the 1999 head is kept, so the authored head, hair and eyes are left out) gets its own atlas,
# painted per texel from its 3D position (no game data): the remodel's materials as they render (linen tunic with folds
# and a stitched hem, dark V-neck trim, stitched belt with a brass buckle, leggings laced up the outer thigh, knee boots
# with a turned-down top and ankle creases, dark shoes) and a skin tone matched by eye to the kept 1999 face, so the neck
# meets the head without a seam. own_texture.bake_atlas then adds occlusion and a soft top light.

HEAD_PARTS = ("Delon_Head", "Delon_Ear", "Delon_Eye", "Delon_Lid", "Delon_Scalp", "Delon_Hair", "Delon_Brow")
# left out of the game mesh as well: the V-neck cords decimate to a few long triangles that cut through the chest in
# zigzags; the trim is painted along the V instead
GAME_SKIP = (*HEAD_PARTS, "Delon_Neckline")
PAINT_RES = 1024  # painted at twice the bake size, so the bake's samples smooth the edges

# sRGB of each material as the remodel renders it (render_threequarter.png, lit faces): the albedo the bake starts from
PALETTE = {
    "shirt": (198, 168, 128),
    "trim": (66, 45, 32),
    "trousers": (122, 94, 70),
    "leather": (104, 76, 54),
    "shoe": (64, 46, 34),
    "belt": (88, 58, 38),
    "metal": (214, 166, 74),
    "skin": (222, 146, 104),
}


def _srgb(c):
    import numpy as np

    c = np.array(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def _vnoise(P, scale, seed=0):
    """Value noise in [0, 1] at points P (n, 3), lattice spacing 1 / scale metres."""
    import numpy as np

    q = P * scale
    i = np.floor(q).astype(np.int64)
    f = q - i
    f = f * f * (3 - 2 * f)

    def h(ix, iy, iz):
        v = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (seed * 2654435761)
        v = (v ^ (v >> 13)) * 1274126177
        return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0

    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (
                    (f[:, 0] if dx else 1 - f[:, 0])
                    * (f[:, 1] if dy else 1 - f[:, 1])
                    * (f[:, 2] if dz else 1 - f[:, 2])
                )
                out = out + w * h(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
    return out


def _fbm(P, scale, octaves=3, seed=0):
    tot, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        tot = tot + amp * _vnoise(P, scale * 2**o, seed + o)
        norm += amp
        amp *= 0.5
    return tot / norm


def _rasterize(lod, res):
    """Per-texel 3D position, normal and material index of the "atlas" UV map (uncovered texels get -1)."""
    import numpy as np

    me = lod.data
    uv = me.uv_layers["atlas"].data
    co = np.array([v.co[:] for v in me.vertices])
    nrm = np.array([v.normal[:] for v in me.vertices])
    P = np.zeros((res, res, 3))
    N = np.zeros((res, res, 3))
    M = np.full((res, res), -1, dtype=np.int32)
    for poly in me.polygons:
        li = list(poly.loop_indices)
        vi = list(poly.vertices)
        for a, b, cc in ((0, k, k + 1) for k in range(1, len(li) - 1)):
            uvs = np.array([uv[li[a]].uv[:], uv[li[b]].uv[:], uv[li[cc]].uv[:]]) * res - 0.5
            lo = np.floor(uvs.min(0) - 1).astype(int).clip(0, res - 1)
            hi = np.ceil(uvs.max(0) + 1).astype(int).clip(0, res - 1)
            xs, ys = np.meshgrid(np.arange(lo[0], hi[0] + 1), np.arange(lo[1], hi[1] + 1))
            x, y = xs.ravel().astype(float), ys.ravel().astype(float)
            (x0, y0), (x1, y1), (x2, y2) = uvs
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-12:
                continue
            l0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den
            l1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den
            l2 = 1 - l0 - l1
            e = -0.6 / max(1.0, abs(den) ** 0.5)  # a little past the edges: no gaps between triangles
            inside = (l0 >= e) & (l1 >= e) & (l2 >= e)
            if not inside.any():
                continue
            L = np.stack([l0[inside], l1[inside], l2[inside]], 1)
            ids = [vi[a], vi[b], vi[cc]]
            px, py = xs.ravel()[inside], ys.ravel()[inside]
            P[py, px] = L @ co[ids]
            N[py, px] = L @ nrm[ids]
            M[py, px] = poly.material_index
    N /= np.maximum(np.linalg.norm(N, axis=2, keepdims=True), 1e-9)
    return P, N, M


def _dilate(img, mask, steps):
    import numpy as np

    img, mask = img.copy(), mask.copy()
    for _ in range(steps):
        acc = np.zeros_like(img)
        cnt = np.zeros(mask.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            m = np.roll(np.roll(mask, dy, 0), dx, 1)
            acc += np.roll(np.roll(img, dy, 0), dx, 1) * m[..., None]
            cnt += m
        grow = (~mask) & (cnt > 0)
        img[grow] = acc[grow] / cnt[grow, None]
        mask = mask | grow
    return img


def delon_albedo(P, N, mat):
    """Linear base colour (n, 3) for texels at P (Blender metres) with normals N and material names mat."""
    import numpy as np

    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    ax = np.abs(x)
    n = len(P)
    grain = _fbm(P, 140.0, 2, 1)
    big = _fbm(P, 9.0, 2, 7)
    folds = _fbm(P * np.array([1.0, 1.0, 0.12]), 38.0, 3, 3)  # vertical folds: noise stretched along Z
    col = np.zeros((n, 3))
    for k, c in PALETTE.items():
        col[mat == k] = _srgb(c)
    cloth = (mat == "shirt") | (mat == "trousers")
    col *= (0.92 + 0.12 * grain)[:, None]
    col *= (0.92 + 0.16 * big)[:, None]
    col[cloth] *= (0.82 + 0.26 * folds[cloth])[:, None]

    def darken(m, f):
        col[m] *= f

    phi = np.arctan2(x, -y)  # around the body, 0 at the front

    # ---- tunic: gathered under the belt, a turned and stitched hem, seams, cuffs, elbow creases
    sh = mat == "shirt"
    skirt = sh & (z < 1.045) & (ax < 0.2)
    gath = _fbm(P * np.array([1.0, 1.0, 0.05]), 70.0, 2, 11)
    darken(skirt, (0.84 + 0.24 * gath[skirt])[:, None])
    hem = skirt & (z < 0.912)
    darken(hem, 0.88)
    darken(hem & (z < 0.897), 0.76)
    darken(hem & (np.abs(z - 0.905) < 0.0016) & (np.mod(phi * 0.11, 0.009) < 0.005), 0.6)
    darken(sh & (z > 1.3) & (np.abs(ax - 0.182) < 0.003), 0.7)  # shoulder seams
    cuff = sh & (ax > 0.6)
    darken(cuff, 0.92)
    darken(cuff & (np.abs(ax - 0.628) < 0.0025), 0.62)
    elbow = sh & (np.abs(ax - 0.405) < 0.035) & (y < 0.0)
    wave = np.sin((ax - 0.405) * 260 + 6.0 * _fbm(P, 50.0, 1, 5))
    darken(elbow & (wave > 0.75), 0.84)
    darken(sh & (ax < 0.2) & (ax > 0.06) & (np.abs(y - 0.018) < 0.0025) & (z < 1.33), 0.74)  # side seams

    # ---- V-neck: painted by position over the stepped material border, skin inside and a clean dark band along it
    half = (z - 1.31) * 0.62
    vzone = (y < 0) & (z > 1.27) & (z < 1.445) & ((mat == "shirt") | (mat == "skin") | (mat == "trim"))
    col[vzone & (ax < half - 0.003)] = _srgb(PALETTE["skin"])
    col[vzone & ((ax >= half + 0.013) | (z <= 1.303))] = _srgb(PALETTE["shirt"])  # body faces stepped outside the band
    tr = ((mat == "trim") & ~vzone) | (vzone & (z > 1.303) & (ax >= half - 0.003) & (ax < half + 0.013))
    col[tr] = _srgb(PALETTE["trim"]) * (0.9 + 0.2 * grain[tr])[:, None]
    col[tr] *= (0.85 + 0.35 * np.clip(N[tr] @ np.array([0.0, -0.6, 0.8]), 0, 1))[:, None]

    # ---- belt: two rows of pale stitches, darker edges; the plate behind the buckle darker
    bt = mat == "belt"
    darken(bt & ((z < 1.049) | (z > 1.081)), 0.72)
    rows = (np.abs(z - 1.0525) < 0.0018) | (np.abs(z - 1.0775) < 0.0018)
    lit = bt & rows & (np.mod(phi * 0.12, 0.008) < 0.0045) & (ax > 0.02)
    col[lit] = col[lit] * 0.4 + _srgb((176, 150, 112)) * 0.6
    darken(bt & (y < -0.096) & (ax < 0.024), 0.62)

    # ---- buckle: brass, an engraved border on the face, darker on the sides
    mt = mat == "metal"
    face = mt & (N[:, 1] < -0.7)
    bd = np.maximum(ax - 0.026, np.abs(z - 1.065) - 0.022)
    darken(face & (np.abs(bd) < 0.0022), 0.45)
    darken(mt & ~face, 0.62)
    col[face] += 0.15 * np.clip(1 - np.abs(bd[face]) / 0.01, 0, 1)[:, None] * _srgb((255, 220, 140))

    # ---- leggings: creases behind the knees, cross lacing up the outer thigh
    lg = mat == "trousers"
    phil = np.arctan2(ax - 0.085, -(y - 0.008))  # around each leg: 0 front, +pi/2 outside
    u = (phil - math.pi / 2) * 0.066  # metres around the leg from the outer line
    knee = lg & (y > 0.0) & (np.abs(z - 0.55) < 0.05)
    darken(knee & (np.sin(z * 380 + 4 * big) > 0.7), 0.82)
    zone = lg & (z < 0.875) & (z > 0.595) & (np.abs(u) < 0.016)
    q = (0.875 - z) / 0.05
    f = q - np.floor(q)
    slit = zone & (np.abs(u) < 0.0016)
    darken(slit, 0.35)
    darken(zone & (np.abs(u) < 0.006) & ~slit, 0.82)
    eyelet = zone & (np.hypot(np.abs(u) - 0.012, np.minimum(f, 1 - f) * 0.05) < 0.0026)
    col[eyelet] = _srgb((40, 28, 20))
    cord = zone & (np.minimum(np.abs(u - (f - 0.5) * 0.026), np.abs(u + (f - 0.5) * 0.026)) < 0.0021)
    col[cord] = _srgb((58, 40, 28)) * (0.9 + 0.3 * grain[cord])[:, None]

    # ---- boots: a turned-down top with a stitched edge, creases at the ankle, a seam at the back, wear on the shin
    bo = mat == "leather"
    top = bo & (z > 0.496)
    col[top] *= 1.08
    darken(bo & (np.abs(z - 0.502) < 0.003), 0.6)
    darken(top & (np.abs(z - 0.539) < 0.0016) & (np.mod(phil * 0.06, 0.008) < 0.0045), 0.55)
    shaft = bo & ~top
    crease = np.sin((z + 0.01 * np.sin(phil * 3 + 7 * big)) * 2 * math.pi / 0.026)
    darken(shaft & (z < 0.24) & (z > 0.09) & (crease > 0.82), 0.74)
    darken(shaft & (np.abs(np.abs(phil) - math.pi) < 0.05), 0.72)
    col[shaft] *= (1.0 + 0.12 * np.clip(-N[shaft, 1], 0, 1) * big[shaft])[:, None]
    # ---- shoes: a darker welt and sole, the toe a little worn
    so = mat == "shoe"
    darken(so & (z < 0.016), 0.55)
    darken(so & (np.abs(z - 0.022) < 0.003), 0.7)
    col[so] *= (0.92 + 0.22 * np.clip(-N[so, 1], 0, 1))[:, None]

    # ---- skin: a soft shadow under the jaw, so the neck goes into the kept head without a bright band
    neck = (mat == "skin") & (z > 1.43)
    darken(neck, (1.0 - 0.12 * np.clip((z[neck] - 1.43) / 0.08, 0, 1))[:, None])

    # painted light: a little brighter facing up, darker facing down (the bake adds occlusion on top)
    col *= (0.86 + 0.24 * np.clip(N[:, 2] * 0.5 + 0.5, 0, 1))[:, None]
    return np.clip(col, 0, 1)


def paint_delon(lod, png, out):
    """Unwrap the body's game mesh, paint its albedo per texel and bake it with occlusion and top light into `png`."""
    import numpy as np

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from own_texture import bake_atlas, unwrap

    unwrap(lod, margin=0.006, angle=48.0)
    res = PAINT_RES
    P, N, M = _rasterize(lod, res)
    names = [m.name for m in lod.data.materials]
    cover = M >= 0
    mat = np.array([*names, ""])[M[cover]]
    col = np.zeros((res, res, 3))
    col[cover] = delon_albedo(P[cover], N[cover], mat)
    col = _dilate(col, cover, 12)
    # the inside of the tunic skirt shows when a thigh lifts it: held brighter than the bake's near-black occlusion
    floor = np.full((res, res), 0.35)
    Pc, Nc = P[cover], N[cover]
    inner = (mat == "shirt") & (Pc[:, 2] < 1.045) & (np.abs(Pc[:, 0]) < 0.2)
    inner &= Nc[:, 0] * Pc[:, 0] + Nc[:, 1] * (Pc[:, 1] - 0.01) < 0
    fc = floor[cover]
    fc[inner] = 0.7
    floor[cover] = fc
    img = bpy.data.images.new("delon_albedo", res, res, float_buffer=True)
    rgba = np.concatenate([col, np.ones((res, res, 1))], 2).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = str(Path(out) / "delon_albedo.exr")
    img.file_format = "OPEN_EXR"
    img.save()
    for name in names:
        nt = bpy.data.materials[name].node_tree
        nt.nodes.clear()
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        outn = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(bsdf.outputs[0], outn.inputs["Surface"])
        set_input(bsdf, "Roughness", 1.0)
        set_input(bsdf, ["Specular IOR Level", "Specular"], 0.0)
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        uvn = nt.nodes.new("ShaderNodeUVMap")
        uvn.uv_map = "atlas"
        nt.links.new(uvn.outputs[0], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    slots = bake_atlas(lod, png, materials=names, size=512, samples=128)
    floor_light(png, col, floor)
    return slots


def floor_light(png, albedo, floor):
    """Hold the baked light at `floor` (per texel, at the albedo's size) or more of the albedo. The occlusion under the
    tunic skirt (the crotch, the skirt's inside) bakes near black, which shows in game as dark slivers when the legs
    move; elsewhere the bake is left as it is."""
    import numpy as np

    img = bpy.data.images.load(str(png), check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    k = albedo.shape[0] // h
    alb = albedo.reshape(h, k, w, k, 3).mean((1, 3))
    floor = floor.reshape(h, k, w, k).max((1, 3))[:, :, None]

    def lin(c):
        return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

    def srgb(c):
        return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - 0.055)

    baked = lin(px[:, :, :3])
    held = alb * floor
    low = baked.sum(2) < held.sum(2)
    baked[low] = held[low]
    px[:, :, :3] = srgb(np.clip(baked, 0, 1))
    img.pixels.foreach_set(px.reshape(-1))
    img.save()
    print(f"held {int(low.sum())} texels at their floor")


def export_authored_own(char, g):
    """The authored model for the converter: the body's game mesh with its own baked texture, the 1999 head kept."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored

    path = Path(ARGS.authored).resolve()
    body_parts = [o for o in char if not o.name.startswith(GAME_SKIP)]
    for o in bpy.context.scene.objects:  # nothing else may shade the game mesh in the bake
        o.hide_render = True
    body, _ = build_game_lod(body_parts, "Delon_Body_Game")
    body.hide_render = False
    # outward normals everywhere: the hem ring is built downwards, so its faces pointed inwards (they baked dark, and
    # the converter takes the winding as it stands)
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(body.data)
    bm.free()
    print(f"authored body: {len(body.data.vertices)} vertices, {len(body.data.polygons)} triangles")
    slots = paint_delon(body, path.with_suffix(".png"), OUT)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "delon_game_own.blend"))  # the baked game mesh, for checks
    # PC joint -> graph node: the converter warps the body onto the 1999 rig through these, and keeps the 1999 head
    # (its face texture is the character; a 450-triangle head with a painted face is the Drakan 2 method)
    pairs = {"upperbody": "pelvis", "chest": "chest", "head": "neck1"}  # no neck: the 1999 neck joint sits 4 cm higher
    for s in "RL":
        pairs |= {
            f"{s}uparm": f"shoulder{s}",
            f"{s}forearm": f"elbow{s}",
            f"{s}thigh": f"hip{s}",
            f"{s}calf": f"knee{s}",
            f"{s}foot": f"ankle{s}",
        }
    landmarks = {j: [round(c, 4) for c in g.v[g.names[n]]] for j, n in pairs.items()}
    export_authored(
        body,
        path,
        model="Delon(h)",
        label="Delon",
        fit="joints",
        slots=slots,
        extra={"landmarks": landmarks, "keepPcJoints": ["head"]},
    )


# ---------------------------------------------------------------------------- scene and render


def setup_scene():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    cy = sc.cycles
    cy.samples = ARGS.samples
    cy.use_denoising = True
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for backend in ("OPTIX", "CUDA"):
            try:
                prefs.compute_device_type = backend
                prefs.get_devices()
                if any(d.type == backend for d in prefs.devices):
                    for d in prefs.devices:
                        d.use = d.type == backend
                    cy.device = "GPU"
                    break
            except TypeError:
                continue
    except KeyError:
        pass
    sc.render.resolution_y = ARGS.res
    sc.render.resolution_x = int(ARGS.res * 0.8)
    sc.view_settings.view_transform = (
        "AgX"
        if "AgX" in [i.name for i in type(sc.view_settings).bl_rna.properties["view_transform"].enum_items]
        else "Filmic"
    )
    sc.view_settings.look = "None"
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.045, 0.05, 0.06, 1)
    bg.inputs["Strength"].default_value = 1.0
    # floor
    bpy.ops.mesh.primitive_circle_add(vertices=96, radius=2.2, fill_type="NGON")
    fl = bpy.context.active_object
    fl.name = "Floor"
    fm = bpy.data.materials.new("floor")
    fm.use_nodes = True
    set_input(fm.node_tree.nodes["Principled BSDF"], "Base Color", (0.06, 0.06, 0.065, 1))
    set_input(fm.node_tree.nodes["Principled BSDF"], "Roughness", 0.8)
    fl.data.materials.append(fm)

    def area(name, loc, energy, size, color):
        ld = bpy.data.lights.new(name, "AREA")
        ld.energy = energy
        ld.size = size
        ld.color = color
        lo = link(bpy.data.objects.new(name, ld))
        lo.location = loc
        d = Vector((0, 0, 1.2)) - Vector(loc)
        lo.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        return lo

    area("Key", (-1.6, -2.4, 2.6), 260, 1.6, (1.0, 0.93, 0.85))
    area("Fill", (2.2, -1.8, 1.4), 70, 2.5, (0.80, 0.88, 1.0))
    area("Rim", (0.6, 2.4, 2.4), 260, 1.2, (1.0, 0.95, 0.9))
    cam_d = bpy.data.cameras.new("cam")
    cam = link(bpy.data.objects.new("Camera", cam_d))
    sc.camera = cam
    return cam


VIEWS = {
    # name: (azimuth deg from front, distance, target z, focal mm, cam height)
    "front": (0, 4.6, 0.88, 70, 1.05),
    "threequarter": (-35, 4.6, 0.88, 70, 1.15),
    "side": (-90, 4.6, 0.88, 70, 1.05),
    "back": (180, 4.6, 0.88, 70, 1.15),
    "face": (-22, 0.95, 1.61, 85, 1.65),
    "facefront": (0, 0.95, 1.61, 85, 1.62),
}


def render_views(cam, prefix, hide=(), views=None):
    sc = bpy.context.scene
    for o in hide:
        o.hide_render = True
    for v in views or ARGS.views.split(","):
        az, dist, tz, focal, ch = VIEWS[v]
        a = math.radians(az)
        cam.location = (-dist * math.sin(a), -dist * math.cos(a), ch)
        d = Vector((0, 0, tz)) - cam.location
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = focal
        sc.render.filepath = str(OUT / f"{prefix}_{v}.png")
        bpy.ops.render.render(write_still=True)
        print("wrote", sc.render.filepath)
    for o in hide:
        o.hide_render = False


# ---------------------------------------------------------------------------- main


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build_materials()
    g = build_graph()
    body = build_body(g)
    build_clothing(body, g)
    head, eyes = build_head()
    build_hair()
    cam = setup_scene()
    char = [o for o in bpy.context.scene.objects if o.name.startswith("Delon_")]
    tris_hi = 0
    dg = bpy.context.evaluated_depsgraph_get()
    for o in char:
        if o.type in {"MESH", "CURVE"}:
            me = o.evaluated_get(dg).to_mesh()
            tris_hi += sum(len(p.vertices) - 2 for p in me.polygons)
            o.evaluated_get(dg).to_mesh_clear()
    lod, _ = build_game_lod(char)
    lod_tris = len(lod.data.polygons)
    lod.hide_render = True
    lod.hide_set(True)
    # cords on the clothes (laces, neckline) become meshes so they can take skin weights with the body
    for o in list(char):
        if o.type == "CURVE" and o.name.startswith(("Delon_Lace", "Delon_Neckline")):
            me = bpy.data.meshes.new_from_object(o.evaluated_get(bpy.context.evaluated_depsgraph_get()))
            m = link(bpy.data.objects.new(o.name, me))
            m.matrix_world = o.matrix_world
            char[char.index(o)] = m
            bpy.data.objects.remove(o)
    skinned = [
        o
        for o in char
        if o.type == "MESH"
        and not o.name.startswith(("Delon_Head", "Delon_Eye", "Delon_Ear", "Delon_Lid", "Delon_Scalp"))
    ]
    rigid = [o for o in char if o not in skinned]
    try:
        rig = build_rig(g, skinned, rigid)
        print("rig bones", len(rig.data.bones))
    except RuntimeError as exc:  # bone heat can fail on odd geometry; keep the model
        print("rig failed:", exc)
    print(f"render mesh tris {tris_hi}, game LOD tris {lod_tris}, verts {len(lod.data.vertices)}")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "delon.blend"))
    # glTF of the game LOD
    try:
        bpy.ops.object.select_all(action="DESELECT")
        lod.hide_set(False)
        lod.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(OUT / "delon_game.glb"), use_selection=True, export_apply=True)
        lod.hide_set(True)
    except Exception as exc:
        print("gltf export failed:", exc)
    if not ARGS.no_render:
        prefix = "clay" if ARGS.clay else "render"
        render_views(cam, prefix)
        if ARGS.lod:  # the game mesh alone, same cameras
            hidden = [o for o in char if not o.hide_render]
            for o in hidden:
                o.hide_render = True
            lod.hide_render = False
            render_views(cam, prefix + "_lod", views=["threequarter", "face"])
    if ARGS.authored:  # last: the bake replaces the materials' nodes
        export_authored_own(char, g)


main()
