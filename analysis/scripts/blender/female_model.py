"""Build original female human meshes (Tuiri and the two Ruined Village women) for the OotF converter, headless.

Order of the Flame's Tuiri and female villagers have no Drakan 2 disc equivalent, so this script authors new bodies from
scratch (no game data is read) and writes them as authored models (editor/packages/formats/src/ootfauthored.ts). The
technique follows delon_model.py: a skin-modifier joint graph for the body (here a female one, with bust and hips),
voxel remeshed, plus clothing shells fitted to it (boots, gauntlets, a shoulder plate, bell skirts, belt, sleeves).
The converter keeps each 1999 head (face, hair cards and the hair joints' ponytail) and projects the 1999 textures.

    blender -b --factory-startup -P analysis/scripts/blender/female_model.py -- --which tuiri|fvillager|fvillage2|all \
        [--out build/authored/female-model] [--models scripts/buildtool/kit/models] [--render]

Outputs: <models>/<which>.json (committed geometry), <out>/<which>.blend and, with --render, clay views of the game mesh.
Coordinates: metres, Z up, facing -Y. Each body is built in its 1999 model's proportions scaled to 1.68 m (joint
positions hand-measured from the skeleton dumps and rounded), so the converter's joint warp is close to a plain scale.
"""

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[3]


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--which", default="tuiri", help="tuiri, fvillager, fvillage2 or all")
    ap.add_argument("--out", default=str(ROOT / "build/authored/female-model"))
    ap.add_argument("--models", default=str(ROOT / "scripts/buildtool/kit/models"))
    ap.add_argument("--render", action="store_true", help="clay renders of the game mesh (Workbench)")
    ap.add_argument("--no-export", action="store_true")
    return ap.parse_args(argv)


ARGS = parse_args()

# ---------------------------------------------------------------------------- characters
#
# Joint positions: the 1999 bind pose scaled to 1.68 m (Blender axes, +X is the R side), rounded by hand.
# Feature heights (hems, belt, boot tops, sleeve ends) are where the 1999 textures paint them.

VILLAGER_JOINTS = {
    "hip": 1.022,
    "torso": 1.140,
    "chest": 1.273,
    "neck": 1.437,
    "head": 1.491,
    "thighx": 0.064,
    "knee": (0.058, 0.0, 0.596),
    "ankle": (0.061, 0.06, 0.10),
    "toe": (0.061, -0.135, 0.022),
    "shoulder": (0.125, 0.030, 1.386),
    "elbow": (0.316, 0.029, 1.393),
    "wrist": (0.539, 0.020, 1.400),
}

CHARS = {
    "tuiri": {
        "model": "tuiri (h)",
        "label": "Tuiri",
        "joints": {
            "hip": 1.007,
            "torso": 1.129,
            "chest": 1.266,
            "neck": 1.436,
            "head": 1.491,
            "thighx": 0.066,
            "knee": (0.060, -0.006, 0.568),
            "ankle": (0.063, 0.06, 0.086),
            "toe": (0.063, -0.130, 0.020),
            "shoulder": (0.129, 0.038, 1.383),
            "elbow": (0.345, 0.038, 1.390),
            "wrist": (0.606, 0.028, 1.400),
        },
        # athletic: narrower hips than the villagers, slim arms in big flared gauntlets
        "build": {"hips": 1.0, "bust": 1.0, "arm": 0.95, "leg": 1.0},
        "outfit": {"boots": 0.603, "boot_front": 0.04, "gauntlet": (0.375, 0.600), "pauldron": True},
        # PC texture entries: 0 suit, 1 arms, 2 boots, 3 head, 4 hair (cut-out), 5 and 6 shoulder plate
        "slots": {"boot": [2], "gauntlet": [1], "pauldron": [5, 6]},
        # her own painted atlas (paint_tuiri): every slot of the new body is baked, only the kept 1999 head is projected
        "own_texture": True,
        # the 1999 neck is not kept: its ragged lower edge (with the back of the 1999 gold collar) stood on her painted
        # chest a shade darker than it and made the head read as separate; the new neck carries the painted skin up to the jaw
        "keep": [],
    },
    "fvillager": {
        "model": "fvillager",
        "label": "female villager (dress)",
        "joints": VILLAGER_JOINTS,
        "build": {"hips": 0.92, "bust": 1.0, "arm": 1.0, "leg": 1.0},
        # sleeveless dress: belt (dark band at 1.08 to 1.13) with a sash hanging at x 0.03, hem at 0.60, laced boots
        "outfit": {"boots": 0.545, "skirt": (1.105, 0.625), "belt": (1.08, 1.13), "sash": 0.74},
        # 0 legs (skirt and boots), 1 main sheet, 2 head, 3 hair (cut-out)
        "slots": {"skirt": [0], "sash": [0]},
        "keep": ["neck"],
        "hand": 0.7,
    },
    "fvillage2": {
        "model": "fvillage2",
        "label": "female villager (blouse and skirt)",
        "joints": VILLAGER_JOINTS,
        "build": {"hips": 0.92, "bust": 1.04, "arm": 1.0, "leg": 1.0},
        # blouse to 1.055 over a skirt to 0.60, short sleeves ending at x 0.225, laced boots
        "outfit": {"boots": 0.54, "skirt": (1.075, 0.625), "peplum": 1.058, "sleeve": 0.225},
        # 0 main sheet, 1 head, 2 legs (skirt and boots), 3 hair (cut-out)
        "slots": {"skirt": [2]},
        "keep": ["neck"],
        "hand": 0.7,
    },
}

# triangles per part in the game mesh; the kept 1999 head adds about 350 to 400
BUDGET = {
    "Body": 1500,
    "Skirt": 330,
    "Boot": 110,
    "Gauntlet": 110,
    "Pauldron": 90,
    "Belt": 70,
    "Sash": 24,
    "Peplum": 110,
    "Sleeve": 50,
}

# ---------------------------------------------------------------------------- helpers


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


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


def shade_smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True


MATS = {}
COLOURS = {
    "skin": (0.55, 0.36, 0.27),
    "suit": (0.12, 0.08, 0.05),
    "dress": (0.45, 0.36, 0.26),
    "blouse": (0.20, 0.33, 0.40),
    "skirt": (0.42, 0.33, 0.24),
    "boot": (0.25, 0.15, 0.08),
    "gauntlet": (0.45, 0.28, 0.08),
    "pauldron": (0.55, 0.35, 0.10),
    "belt": (0.08, 0.07, 0.06),
    "sash": (0.08, 0.07, 0.06),
}


def mat(name):
    if name not in MATS:
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*COLOURS.get(name, (0.5, 0.5, 0.5)), 1.0)
        MATS[name] = m
    return MATS[name]


def add_mat(obj, name):
    obj.data.materials.append(mat(name))
    return len(obj.data.materials) - 1


# ---------------------------------------------------------------------------- body graph


class Graph:
    def __init__(self):
        self.v, self.r, self.e, self.tag, self.names = [], [], [], [], {}

    def add(self, name, pos, r):
        self.names[name] = len(self.v)
        self.v.append(Vector(pos))
        self.r.append((r, r) if isinstance(r, (int, float)) else r)

    def edge(self, a, b, tag):
        self.e.append((self.names[a], self.names[b]))
        self.tag.append(tag)

    def p(self, name):
        return self.v[self.names[name]]


def build_graph(c):
    """A female body at the 1999 joints: narrow waist, wide hips, slim limbs, T pose with the palms down."""
    J, b = c["joints"], c["build"]
    cloth = "suit" if "pauldron" in c["outfit"] else ("blouse" if "peplum" in c["outfit"] else "dress")
    legs = "suit" if cloth == "suit" else "skin"
    g = Graph()
    hip, hw = J["hip"], b["hips"]
    g.add("pelvis", (0, 0.022, hip - 0.035), (0.118 * hw, 0.092))
    g.add("hips", (0, 0.016, hip + 0.065), (0.104 * hw, 0.080))
    g.add("waist", (0, 0.004, J["torso"] + 0.035), (0.070, 0.058))
    g.add("ribs", (0, 0.004, J["chest"] - 0.025), (0.084, 0.066))
    g.add("chest", (0, 0.016, J["chest"] + 0.055), (0.094, 0.068))
    g.add("uchest", (0, 0.030, J["shoulder"][2] + 0.004), (0.080, 0.054))
    g.add("neck0", (0, 0.032, J["neck"]), 0.034)
    g.add("neck1", (0, 0.026, J["head"] + 0.03), 0.032)
    g.edge("pelvis", "hips", cloth)
    g.edge("hips", "waist", cloth)
    g.edge("waist", "ribs", cloth)
    g.edge("ribs", "chest", cloth)
    g.edge("chest", "uchest", cloth)
    g.edge("uchest", "neck0", "skin")
    g.edge("neck0", "neck1", "skin")
    sh0, el0, wr0 = Vector(J["shoulder"]), Vector(J["elbow"]), Vector(J["wrist"])
    arm_dir = (wr0 - sh0).normalized()
    down = Vector((0, 0, -1))
    ar = b["arm"]
    sleeve = cloth if cloth == "suit" else "skin"
    for s, sx in (("R", 1), ("L", -1)):

        def m(p, sx=sx):
            return (p[0] * sx, p[1], p[2])

        sh = sh0 + Vector((0.012, 0, -0.004))
        g.add("clav" + s, m((0.062, 0.028, sh0.z + 0.018)), 0.044)
        g.add("shoulder" + s, m(sh), 0.042 * ar)
        g.add("uarm" + s, m(sh.lerp(el0, 0.45)), 0.035 * ar)
        g.add("elbow" + s, m(el0), 0.027 * ar)
        g.add("farm" + s, m(el0.lerp(wr0, 0.35)), 0.029 * ar)
        g.add("wrist" + s, m(wr0), 0.020 * ar)
        palm = wr0 + arm_dir * 0.040
        g.add("palm" + s, m(palm), 0.021)
        g.edge("uchest", "clav" + s, cloth)
        g.edge("clav" + s, "shoulder" + s, sleeve)
        g.edge("shoulder" + s, "uarm" + s, sleeve)
        g.edge("uarm" + s, "elbow" + s, sleeve)
        g.edge("elbow" + s, "farm" + s, sleeve)
        g.edge("farm" + s, "wrist" + s, sleeve)
        g.edge("wrist" + s, "palm" + s, "skin")
        # four fingers, index at the front (-Y), slightly curled; a thumb at the front
        for i, (dy, ln, rr) in enumerate(
            [(-0.016, 0.060, 0.0074), (-0.005, 0.067, 0.0076), (0.006, 0.063, 0.0072), (0.016, 0.051, 0.0064)]
        ):
            hl = c.get("hand", 0.9)  # the 1999 villagers hold loose fists: shorter, more curled fingers
            ln *= hl
            k0 = wr0 + arm_dir * 0.066 + Vector((0, dy, 0))
            k1 = k0 + arm_dir * ln * 0.5 + down * (0.006 + 0.03 * (0.9 - hl))
            k2 = k0 + arm_dir * ln * (0.9 - 2 * (0.9 - hl)) + down * (0.016 + 0.12 * (0.9 - hl))
            n = f"f{i}{s}"
            g.add(n + "a", m(k0), rr)
            g.add(n + "b", m(k1), rr * 0.9)
            g.add(n + "c", m(k2), rr * 0.75)
            g.edge("palm" + s, n + "a", "skin")
            g.edge(n + "a", n + "b", "skin")
            g.edge(n + "b", n + "c", "skin")
        t0 = wr0 + arm_dir * 0.020 + Vector((0, -0.018, 0)) + down * 0.007
        t1 = t0 + arm_dir * 0.024 + Vector((0, -0.019, 0)) + down * 0.004
        t2 = t1 + arm_dir * 0.025 + Vector((0, -0.007, 0)) + down * 0.002
        g.add("t0" + s, m(t0), 0.011)
        g.add("t1" + s, m(t1), 0.0092)
        g.add("t2" + s, m(t2), 0.0078)
        g.edge("wrist" + s, "t0" + s, "skin")
        g.edge("t0" + s, "t1" + s, "skin")
        g.edge("t1" + s, "t2" + s, "skin")
        # leg
        kn, an, to = Vector(J["knee"]), Vector(J["ankle"]), Vector(J["toe"])
        lr = b["leg"]
        g.add("hip" + s, m((J["thighx"] + 0.010, 0.012, hip - 0.075)), 0.072 * lr * (1 + (hw - 1) * 0.5))
        g.add("thigh" + s, m((J["thighx"] + 0.013, 0.002, hip - 0.24)), 0.057 * lr)  # legs apart below the crotch
        g.add("thigh2" + s, m((kn.x + 0.008, -0.002, kn.z + 0.16)), 0.049 * lr)
        g.add("knee" + s, m(kn), 0.040 * lr)
        g.add("calf" + s, m((kn.x, 0.016, kn.z - 0.15)), 0.044 * lr)
        g.add("shin" + s, m((kn.x + 0.002, 0.016, 0.25)), 0.033 * lr)
        g.add("ankle" + s, m((an.x, 0.026, an.z)), 0.028)
        g.add("heel" + s, m((an.x, 0.058, 0.038)), 0.031)
        g.add("ball" + s, m((to.x + 0.002, -0.058, 0.024)), (0.033, 0.022))
        g.add("toe" + s, m((to.x + 0.003, to.y + 0.025, 0.022)), (0.025, 0.017))
        g.edge("pelvis", "hip" + s, cloth if cloth == "suit" else "skin")
        g.edge("hip" + s, "thigh" + s, legs)
        g.edge("thigh" + s, "thigh2" + s, legs)
        g.edge("thigh2" + s, "knee" + s, legs)
        g.edge("knee" + s, "calf" + s, legs)
        g.edge("calf" + s, "shin" + s, legs)
        g.edge("shin" + s, "ankle" + s, legs)
        g.edge("ankle" + s, "heel" + s, "boot")
        g.edge("ankle" + s, "ball" + s, "boot")
        g.edge("ball" + s, "toe" + s, "boot")
    return g


def ellipsoid(name, centre, radii, rot_z=0.0):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=1.0)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = link(bpy.data.objects.new(name, me))
    obj.matrix_world = Matrix.Translation(centre) @ Matrix.Rotation(rot_z, 4, "Z") @ Matrix.Diagonal((*radii, 1.0))
    return obj


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length, t


def build_body(c, g):
    J, b = c["joints"], c["build"]
    obj = mesh_obj("Body", g.v, edges=g.e)
    sk = obj.modifiers.new("skin", "SKIN")
    sk.use_smooth_shade = True
    sk.branch_smoothing = 0.6
    for i, r in enumerate(g.r):
        obj.data.skin_vertices[0].data[i].radius = (r[0] * 1.18, r[1] * 1.18)
    obj.data.skin_vertices[0].data[g.names["pelvis"]].use_root = True
    ss = obj.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 2
    apply_modifiers(obj)
    # bust and seat: ellipsoids joined before the remesh fuses them into the torso
    bz = J["chest"] + 0.03
    bs = b["bust"]
    parts = [obj]
    parts.append(ellipsoid("bridge", (0.0, -0.040, bz + 0.008), (0.045 * bs, 0.042 * bs, 0.040 * bs)))
    for sx in (-1, 1):
        parts.append(ellipsoid("bust", (0.050 * sx, -0.040, bz), (0.050 * bs, 0.044 * bs, 0.046 * bs), 0.3 * sx))
        parts.append(ellipsoid("seat", (0.056 * sx, 0.050, J["hip"] - 0.055), (0.066, 0.060, 0.074)))
    with bpy.context.temp_override(active_object=obj, selected_editable_objects=parts, selected_objects=parts):
        bpy.ops.object.join()
    rm = obj.modifiers.new("remesh", "REMESH")
    rm.mode = "VOXEL"
    rm.voxel_size = 0.004
    sm = obj.modifiers.new("smooth", "CORRECTIVE_SMOOTH")
    sm.iterations = 6
    sm.use_only_smooth = True
    apply_modifiers(obj)
    shade_smooth(obj)
    tags = sorted(set(g.tag))
    slot = {t: add_mat(obj, t) for t in tags}
    for poly in obj.data.polygons:
        p = poly.center
        best, tag = 1e9, tags[0]
        for (ia, ib), t in zip(g.e, g.tag):
            d, u = seg_dist(p, g.v[ia], g.v[ib])
            ra = sum(g.r[ia]) * 0.5
            rb = sum(g.r[ib]) * 0.5
            d = d / (ra + (rb - ra) * u)
            if d < best:
                best, tag = d, t
        if tag != "skin" and p.z > J["shoulder"][2] + 0.035 and abs(p.x) < 0.09:
            tag = "skin"  # neckline
        poly.material_index = slot[tag]
    return obj


def delete_faces(obj, pred):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    kill = [f for f in bm.faces if pred(f.calc_center_median())]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()


# ---------------------------------------------------------------------------- shells fitted to the body


def body_profile(verts, center, axis, lo, hi, sectors, maxr=0.25, side=0):
    """Max radial distance of body vertices around an axis, per angular sector, for heights in [lo, hi]."""
    axis = axis.normalized()
    ref = Vector((0, -1, 0)) if abs(axis.y) < 0.9 else Vector((1, 0, 0))
    u = (ref - axis * ref.dot(axis)).normalized()
    w = axis.cross(u)
    prof = [0.0] * sectors
    for v in verts:
        if side and v.x * side < 0.004:  # one leg only: the other leg must not widen this one's shell
            continue
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
    for _ in range(4):
        prof = [max(prof[i], 0.5 * (prof[i - 1] + prof[(i + 1) % sectors]) * 0.98) for i in range(sectors)]
    sm = [(prof[i - 1] + 2 * prof[i] + prof[(i + 1) % sectors]) / 4 for i in range(sectors)]
    return sm, u, w, axis


def ring_band(
    name,
    verts,
    center,
    axis,
    rows,
    sectors,
    material,
    thick,
    mono=False,
    shape=None,
    maxr=0.25,
    sub=1,
    rnd=False,
    shrink=1.0,
    rim=True,
    side=0,
):
    """rows: (height along axis, offset); each row follows the body profile. shape(angle, t, r) -> r adjusts it."""
    vs, faces = [], []
    prev = None
    for ri, (h, off) in enumerate(rows):
        prof, u, w, ax = body_profile(verts, center, axis, h - 0.012, h + 0.012, sectors, maxr, side)
        if rnd:  # a plain round tube (sleeves): the median radius, so the armpit does not bulge it
            prof = [sorted(prof)[len(prof) // 2]] * sectors
        if mono and prev is not None:
            prof = [max(p, q * shrink) for p, q in zip(prof, prev)]
        prev = prof
        t = ri / max(1, len(rows) - 1)
        for k in range(sectors):
            a = (k + 0.5) / sectors * 2 * math.pi
            r = prof[k] + off
            if shape:
                r = shape(a, t, r)
            vs.append(center + ax * h + (u * math.cos(a) + w * math.sin(a)) * r)
    for ri in range(len(rows) - 1):
        for k in range(sectors):
            a, b = ri * sectors + k, ri * sectors + (k + 1) % sectors
            faces.append((a, b, b + sectors, a + sectors))
    obj = mesh_obj(name, vs, faces)
    add_mat(obj, material)
    if thick:
        so = obj.modifiers.new("solid", "SOLIDIFY")
        so.thickness = thick
        so.offset = 1.0
        # no rim on long hems: its shared vertices tilt the smooth normals of the bottom row (a dark band)
        so.use_rim = rim
    if sub:
        ss = obj.modifiers.new("sub", "SUBSURF")
        ss.levels = ss.render_levels = sub
    apply_modifiers(obj)
    fix_normals(obj, center, axis)
    shade_smooth(obj)
    return obj


def fix_normals(obj, center, axis):
    """Make the shell's normals point away from its axis (the side the camera sees)."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    score = 0.0
    for f in bm.faces:
        p = f.calc_center_median() - center
        radial = p - axis * p.dot(axis)
        score += f.normal.dot(radial) * f.calc_area()
    if score < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(obj.data)
    bm.free()


def leg_axis_centre(c, sx):
    J = c["joints"]
    return Vector((J["knee"][0] * sx, 0.012, 0.0))


def build_boots(c, verts):
    J, o = c["joints"], c["outfit"]
    top = o["boots"]
    front = o.get("boot_front", 0.0)
    an = J["ankle"][2]
    up = Vector((0, 0, 1))
    out = []
    for sx in (1, -1):
        ctr = leg_axis_centre(c, sx)
        n = 9
        rows = []
        for i in range(n):
            t = i / (n - 1)
            z = an + 0.012 + (top - an - 0.012) * t
            rows.append((z, 0.0045 + 0.004 * smoothstep(0.75, 1.0, t)))

        def shape(a, t, r, top=top):
            # a=0 is the front (-Y); the top edge rises to a point over the knee (Tuiri) and flares a little
            if t > 0.99 and front:
                r += 0.010 * max(0.0, math.cos(a))
            return r

        boot = ring_band(
            f"Boot{'R' if sx > 0 else 'L'}",
            verts,
            ctr,
            up,
            rows,
            28,
            "boot",
            0.004,
            shape=shape,
            maxr=0.072,
            rim=False,
            side=sx,
        )
        for v in boot.data.vertices:  # never past the middle: the two boots must stay apart
            if v.co.x * sx < 0.008:
                v.co.x = 0.008 * sx
        if front:
            # raise the front of the top edge: a knee guard point
            for v in boot.data.vertices:
                d = v.co - ctr
                if v.co.z > top - 0.03:
                    ang = math.atan2(d.x * sx, -d.y)
                    k = max(0.0, math.cos(ang)) ** 3
                    lift = front * k * smoothstep(top - 0.03, top, v.co.z)
                    v.co.z += lift
                    v.co.y -= 0.25 * lift
        out.append(boot)
    return out


def build_gauntlets(c, verts):
    J = c["joints"]
    x0, x1 = c["outfit"]["gauntlet"]
    sh, wr = Vector(J["shoulder"]), Vector(J["wrist"])
    ax = (wr - sh).normalized()
    out = []
    for sx in (1, -1):
        axis = Vector((ax.x * sx, ax.y, ax.z))
        base = Vector((sh.x * sx, sh.y, sh.z))
        rows = []
        n = 8
        for i in range(n):
            t = i / (n - 1)
            h = (x0 + (x1 + 0.022 - x0) * t) - sh.x
            rows.append((h / ax.x, 0.005 + 0.020 * t**1.6))
        g = ring_band(f"Gauntlet{'R' if sx > 0 else 'L'}", verts, base, axis, rows, 24, "gauntlet", 0.004, maxr=0.06)
        out.append(g)
    return out


def build_pauldron(c, verts):
    """A curved plate over the R shoulder, from the collar bone to the upper arm, with a raised outer spike."""
    J = c["joints"]
    sh = Vector(J["shoulder"])
    bm = bmesh.new()
    nu, nv = 14, 9
    grid = []
    for i in range(nu + 1):  # along the shoulder line, from near the neck (x 0.03) to the upper arm (x 0.25)
        u = i / nu
        x = 0.035 + 0.215 * u
        row = []
        for j in range(nv + 1):  # around the shoulder, front to back
            v = j / nv
            ang = math.radians(-150 + 300 * v)  # 0 is straight up
            # drape: lower over the arm, the front edge dips onto the bust
            rr = 0.050 + 0.010 * math.sin(math.pi * u) - 0.012 * u
            cz = sh.z - 0.008 - 0.004 * u
            pts = Vector((x, sh.y + 0.004 + math.sin(ang) * rr * 1.25, cz + math.cos(ang) * rr))
            row.append(pts)
        grid.append(row)
    # the shell is fitted onto the body: push each point out to the surface plus a gap
    tree = bvh_of(verts)
    for row in grid:
        for k, p in enumerate(row):
            loc, nrm, _, d = tree.find_nearest(p)
            if loc is not None:
                row[k] = loc + nrm * 0.011
    # outer spike: lift the outer top edge into a point
    for i, row in enumerate(grid):
        u = i / nu
        for j, p in enumerate(row):
            v = j / nv
            k = math.exp(-(((u - 0.80) / 0.17) ** 2)) * math.exp(-(((v - 0.40) / 0.25) ** 2))
            p.z += 0.065 * k
            p.x += 0.025 * k
    vid = [[bm.verts.new(p) for p in row] for row in grid]
    for i in range(nu):
        for j in range(nv):
            bm.faces.new((vid[i][j], vid[i + 1][j], vid[i + 1][j + 1], vid[i][j + 1]))
    me = bpy.data.meshes.new("Pauldron")
    bm.to_mesh(me)
    bm.free()
    obj = link(bpy.data.objects.new("Pauldron", me))
    add_mat(obj, "pauldron")
    so = obj.modifiers.new("solid", "SOLIDIFY")
    so.thickness = 0.006
    so.offset = 1.0
    so.use_rim = True
    ss = obj.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 1
    apply_modifiers(obj)
    fix_normals_out(obj, sh + Vector((-0.04, 0, -0.03)))
    shade_smooth(obj)
    return [obj]


def fix_normals_out(obj, centre):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    score = sum(f.normal.dot(f.calc_center_median() - centre) * f.calc_area() for f in bm.faces)
    if score < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(obj.data)
    bm.free()


def bvh_of(verts_or_obj):
    from mathutils.bvhtree import BVHTree

    obj = verts_or_obj
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree


def build_skirt(c, verts):
    """A closed bell from the waist to the hem: never narrower going down, a gentle flare and soft folds."""
    top, hem = c["outfit"]["skirt"]
    n = 12
    rows = []
    for i in range(n):
        t = i / (n - 1)
        z = top - (top - hem) * t
        rows.append((-z, 0.003 + 0.004 * t))

    def shape(a, t, r):
        # folds grow towards the hem; a slightly deeper back over the seat
        return r + t * (0.0035 * math.sin(6 * a + 0.4) + 0.002 * math.sin(11 * a + 1.7))

    return [
        ring_band(
            "Skirt",
            verts,
            Vector((0, 0.008, 0)),
            Vector((0, 0, -1)),
            rows,
            40,
            "skirt",
            0.005,
            mono=True,
            shape=shape,
            maxr=0.3,
            shrink=0.985,  # tapers to the knees like the 1999 skirt
            rim=False,
        )
    ]


def build_belt(c, verts):
    z0, z1 = c["outfit"]["belt"]
    return [
        ring_band(
            "Belt", verts, Vector((0, 0.008, 0)), Vector((0, 0, 1)), [(z0, 0.008), (z1, 0.006)], 36, "belt", 0.004
        )
    ]


def build_sash(c, verts, skirt):
    """The belt's knotted sash ends hanging down the front of the skirt, where the 1999 texture paints them."""
    z0 = c["outfit"]["belt"][0]
    zb = c["outfit"]["sash"]
    tree = bvh_of(skirt)
    out = []
    for k, (x, w, end) in enumerate(((0.022, 0.020, zb), (0.042, 0.016, zb + 0.06))):
        pts = []
        n = 8
        for i in range(n + 1):
            t = i / n
            z = z0 + 0.005 - (z0 + 0.005 - end) * t
            row = []
            for xx in (x - w / 2, x + w / 2):
                hit = tree.ray_cast(Vector((xx, -0.5, z)), Vector((0, 1, 0)))
                p = hit[0] if hit[0] is not None else Vector((xx, -0.12, z))
                row.append(p + Vector((0, -0.004, 0)))
            pts.append(row)
        vs = [p for row in pts for p in row]
        faces = [(2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2) for i in range(n)]
        o = mesh_obj(f"Sash{k}", vs, faces)
        add_mat(o, "sash")
        so = o.modifiers.new("solid", "SOLIDIFY")
        so.thickness = 0.003
        so.offset = -1.0
        so.use_rim = True
        apply_modifiers(o)
        fix_normals_out(o, Vector((x, 0.0, (z0 + end) / 2)))
        out.append(o)
    return out


def build_peplum(c, verts):
    """The blouse hem: a short flared frill over the top of the skirt."""
    hem = c["outfit"]["peplum"]
    rows = [(-(hem + 0.06), 0.004), (-(hem + 0.035), 0.008), (-(hem + 0.012), 0.016), (-hem, 0.021)]

    def shape(a, t, r):
        return r + t * 0.004 * math.sin(9 * a)

    return [
        ring_band(
            "Peplum",
            verts,
            Vector((0, 0.008, 0)),
            Vector((0, 0, -1)),
            rows,
            40,
            "blouse",
            0.004,
            mono=True,
            shape=shape,
            maxr=0.3,
            rim=False,
        )
    ]


def build_sleeves(c, verts):
    J = c["joints"]
    end = c["outfit"]["sleeve"]
    sh, wr = Vector(J["shoulder"]), Vector(J["wrist"])
    ax = (wr - sh).normalized()
    out = []
    for sx in (1, -1):
        axis = Vector((ax.x * sx, ax.y, ax.z))
        base = Vector((sh.x * sx, sh.y, sh.z))
        rows = []
        n = 5
        for i in range(n):
            t = i / (n - 1)
            h = (end - 0.04 + 0.04 * t) - sh.x
            rows.append((h / ax.x, 0.002 + 0.007 * t * t))
        out.append(
            ring_band(
                f"Sleeve{'R' if sx > 0 else 'L'}", verts, base, axis, rows, 24, "blouse", 0.003, maxr=0.06, rnd=True
            )
        )
    return out


# ---------------------------------------------------------------------------- game mesh


def budget(name):
    for k, v in sorted(BUDGET.items(), key=lambda kv: -len(kv[0])):
        if name.startswith(k):
            return v
    return 0


def build_game_mesh(objs, name):
    """Each part decimated to its triangle budget, then joined into one mesh (materials kept by name)."""
    bm = bmesh.new()
    mats = []
    for o in objs:
        n = budget(o.name)
        if n <= 0:
            continue
        tmp = link(bpy.data.objects.new("tmp", o.data.copy()))
        tmp.matrix_world = o.matrix_world
        tris = sum(len(p.vertices) - 2 for p in tmp.data.polygons)
        if tris > n:
            dec = tmp.modifiers.new("dec", "DECIMATE")
            dec.ratio = n / tris
            dec.use_collapse_triangulate = True
            apply_modifiers(tmp)
        me = tmp.data
        me.transform(tmp.matrix_world)
        remap = []
        for m in me.materials:
            if m not in mats:
                mats.append(m)
            remap.append(mats.index(m))
        for p in me.polygons:
            p.material_index = remap[p.material_index] if remap else 0
        bm.from_mesh(me)
        bpy.data.objects.remove(tmp)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    lod = link(bpy.data.objects.new(name, me))
    lod.modifiers.new("tri", "TRIANGULATE")
    apply_modifiers(lod)
    shade_smooth(lod)
    return lod


# ---------------------------------------------------------------------------- Tuiri's own texture
#
# Painted procedurally per atlas texel from its 3D position (no game data): a black leather suit with a painted sheen,
# gold filigree of flame tongues (an original design: a spine line with alternating hooked tongues and a bead at each
# tip), a low V girdle with a ruby medallion, a gold-trimmed V neckline, bare arms with gold armlets, gold-plated knee
# guards and flame cuffs on the boots, gold-rimmed gauntlets with a flame crest, and a gold shoulder plate with engraved
# rings. own_texture.bake_atlas then multiplies it by occlusion and a soft top light (the Drakan 2 painted shading).

PAINT_RES = 1024  # the albedo is painted at twice the bake size, so the bake's samples smooth its edges


def _srgb(r, g, b):
    import numpy as np

    c = np.array([r, g, b], dtype=np.float64) / 255.0
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


def _flame_sdf(d, t, t0, t1, period, reach, w, phase=0.0):
    """Gold filigree band: a spine along t (lateral offset d, metres) with hooked flame tongues alternating left and
    right, each leaning onward (+t) and curling back, a bead at each tip. Returns an approximate signed distance."""
    import numpy as np

    tt = (t - t0) / period + phase
    cell = np.floor(tt)
    p = tt - cell
    side = np.where(np.mod(cell, 2) == 0, 1.0, -1.0)
    k = 0.8 * math.pi
    dc = side * reach * np.sin(p * k) / 0.95
    slope = reach * k * np.cos(p * k) / 0.95 / period
    wid = w * (1.25 - 1.05 * p)
    tongue = (np.abs(d - dc) - 0.5 * wid) / np.sqrt(1 + slope * slope)
    tip = np.hypot(d - side * reach * math.sin(0.92 * k) / 0.95, (p - 0.92) * period) - 0.42 * w
    spine = np.abs(d) - 0.32 * w
    s = np.minimum(np.minimum(spine, tongue), tip)
    # the band ends cleanly: shorter tongues near both ends
    fade = np.minimum(t - t0, t1 - t)
    s = np.where(fade < 0, 1.0, s)
    return np.maximum(s, -fade * 0.5)


def _crown_sdf(a, h, period, height):
    """A row of flame tongues rising from a base line: a (metres around), h (metres above the base). Every other tongue
    is shorter; each leans and flicks back near its tip. Returns an approximate signed distance."""
    import numpy as np

    q = a / period
    cell = np.floor(q)
    f = (q - cell - 0.5) * period
    tall = np.where(np.mod(cell, 2) == 0, 1.0, 0.68) * height
    r = np.clip(h / tall, 0, 1)
    centre = 0.16 * period * np.sin(r * math.pi * 1.4)
    hw = 0.5 * period * 0.86 * (1 - r) ** 0.75
    s = np.abs(f - centre) - hw
    s = np.where(h > tall, np.hypot(f - centre, h - tall), s)
    return np.where(h < 0, -h * 0.0 - 0.001 + np.maximum(0, -h - 0.004), s)


def _rasterize(lod, res):
    """Per-texel 3D position, normal and material of the "atlas" UV map (texels no triangle covers get mat -1)."""
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


def tuiri_albedo(c, P, N, mat):
    """Linear base colour (n, 3) for texels at P with normals N and material names mat."""
    import numpy as np

    J, o = c["joints"], c["outfit"]
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    ax = np.abs(x)
    side = np.where(x >= 0, 1.0, -1.0)
    n = len(P)
    col = np.zeros((n, 3))
    grain = _fbm(P, 90.0, 3, 1)
    big = _fbm(P, 9.0, 2, 7)

    def sheen(power=6.0, strength=1.0):
        h = np.array([0.15, -0.55, 0.82])
        h /= np.linalg.norm(h)
        return strength * np.clip(N @ h, 0, 1) ** power

    # ---- leather (suit, boots, gauntlets): near black, warm, fine grain, broad wear and a cool painted sheen
    leather = _srgb(28, 18, 16) * (0.82 + 0.3 * grain[:, None]) * (0.85 + 0.3 * big[:, None])
    leather = leather + sheen(3.0, 0.09)[:, None] * _srgb(150, 140, 150)[None]

    # ---- skin, a tone matched to the kept 1999 face
    skin = _srgb(205, 80, 32) * (0.93 + 0.1 * _fbm(P, 30.0, 2, 3)[:, None])
    skin = skin * (1 - 0.06 * np.clip((0.55 - big) * 2, 0, 1))[:, None]

    suit = mat == "suit"
    is_skin = mat == "skin"
    # V neckline: shallow at the back, plunging to the bust at the front
    front = 1 - np.clip((y + 0.02) / 0.08, 0, 1)
    nbase = 1.262 * front + 1.37 * (1 - front)
    nslope = 1.55
    nd = (z - (nbase + nslope * ax)) / math.sqrt(1 + nslope**2)  # > 0 above the neckline
    torso_top = suit & (ax < 0.16) & (z > 1.2)
    is_skin |= torso_top & (nd > 0)
    # bare arms between the shoulder cap and the gauntlet
    armhole = 0.168
    arm = suit & (ax > armhole - 0.02) & (z > 1.27)
    is_skin |= arm & (ax > armhole)
    col[:] = leather
    col[is_skin] = skin[is_skin]

    gold_sdf = np.full(n, 1.0)
    gold_w = np.full(n, 0.01)

    def put(m, sdf, w):
        better = m & (sdf < gold_sdf)
        gold_sdf[better] = sdf[better]
        gold_w[better] = w

    # neckline trim: a band just under the skin edge with a row of beads
    band = np.abs(nd + 0.009) - 0.0065
    along = (x * 0.8 + np.where(front > 0.5, 0, 0.3)) / 0.022
    bead = np.hypot((along - np.round(along)) * 0.022, nd + 0.009) - 0.0072
    put(torso_top & (nd < 0.002), np.minimum(band, bead), 0.016)
    # under-bust arcs: a gold rim curving under each breast, meeting at the V tip
    bz = J["chest"] + 0.03
    ub = np.hypot((ax - 0.05) / 0.062, (z - bz) / 0.058) - 1.0
    put(suit & (y < -0.02) & (z < bz) & (nd < 0), np.abs(ub * 0.06) - 0.0055, 0.011)
    # V tip: a gold teardrop pointing down between the busts
    tip_z = 1.262
    tdr = np.hypot(x * 1.6, np.maximum(0, tip_z - 0.012 - z) * 0.55 + np.maximum(0, z - tip_z + 0.012) * 1.6)
    put(suit & (y < 0) & (z < tip_z + 0.01), tdr - 0.016, 0.03)
    # armhole trim
    put(arm & (ax <= armhole + 0.003), np.abs(ax - armhole + 0.007) - 0.006, 0.012)

    # low V girdle, higher at the back, with a medallion at the front
    gz = (0.958 + 0.72 * ax) * front + (1.0 + 0.45 * ax) * (1 - front)
    gd = (z - gz) / math.sqrt(1 + 0.72**2)
    body = suit & ~arm & (ax < 0.2)
    put(body & (np.abs(gd) < 0.03), np.abs(gd) - 0.0085, 0.017)
    put(body & (np.abs(gd) < 0.03), np.abs(np.abs(gd) - 0.0125) - 0.0022, 0.004)  # fine outer lines
    med = np.hypot(x, z - 0.978)
    medal = suit & (y < -0.02) & (med < 0.04)
    put(medal, med - 0.024, 0.048)
    # a crown of flames rising from the girdle all round the waist
    phi = np.arctan2(x, -y)
    torso = body & (gd > 0.008) & (z < 1.25) & ~(torso_top & (nd > -0.03))
    put(torso, _crown_sdf(phi * 0.105 + 0.0225, gd - 0.008, 0.045, 0.085), 0.03)
    # princess seams from the bust arcs down to the flames: a dark seam with paler stitches either side
    for s in (1, -1):
        sd = x - s * (0.047 + 0.25 * (1.2 - z) * (z < 1.2))
        on = torso & (y < 0) & (np.abs(sd) < 0.006) & (z > gz + 0.05) & (ub > 0.05)
        dash = np.mod(z, 0.012) < 0.006
        col[on & (np.abs(sd) < 0.0016)] *= 0.35
        col[on & (np.abs(np.abs(sd) - 0.004) < 0.0011) & dash] *= 2.2
    # back: a column of linked lozenges along the spine
    back = body & (y > 0.02) & (gd > 0.01) & (z < nbase + 0.0 - 0.03)
    q = (z - 1.03) / 0.045
    loz = np.abs(x) * 1.9 + np.abs((q - np.round(q)) * 0.045) - 0.016
    put(back, np.minimum(loz, np.abs(x) - 0.003), 0.02)

    # legs: an outer flame band from the boot top to the girdle, a piped seam inside
    leg = suit & ~arm & (gd < -0.006) & (z < 1.05)
    kx = J["knee"][0]
    phil = np.arctan2((x - side * kx) * side, -(y - 0.01))
    r_leg = 0.06
    ul = (phil - 1.75) * r_leg
    # outer gold piping from the girdle down to the thigh flames
    put(leg & (z > o["boots"] + 0.08), np.abs(ul) - 0.0045, 0.009)
    # flames licking up the thigh from the boot top
    put(leg & (z < o["boots"] + 0.2), _crown_sdf(phil * r_leg + 0.01, z - (o["boots"] + 0.01), 0.038, 0.13), 0.025)
    seam = leg & (np.abs(phil + 1.45) < 0.2)
    col[seam] *= np.where(np.abs((phil[seam] + 1.45) * r_leg) < 0.0018, 0.45, 1.0)[:, None]

    # armlets on the bare upper arms
    put(arm & (ax > 0.23) & (ax < 0.27), np.abs(ax - 0.25) - 0.0095, 0.019)

    # ---- gauntlets: leather, gold rims at both ends, a flame crest on the back of the forearm, rivets
    g = mat == "gauntlet"
    x0, x1 = o["gauntlet"]
    sg = (ax - x0) / (x1 + 0.022 - x0)
    sh, wr = Vector(J["shoulder"]), Vector(J["wrist"])
    zc = sh.z + (ax - sh.x) * (wr.z - sh.z) / (wr.x - sh.x)
    yc = sh.y + (ax - sh.x) * (wr.y - sh.y) / (wr.x - sh.x)
    phig = np.arctan2(z - zc, -(y - yc))
    rg = 0.045
    put(g, np.minimum(sg - 0.09, 0.9 - sg) * 0.25, 0.02)
    put(g, _flame_sdf((phig - 1.5708) * rg, (1 - sg) * 0.25, 0.022, 0.21, 0.05, 0.022, 0.012, 0.0), 0.012)
    put(g, _flame_sdf((phig + 1.5708) * rg, (1 - sg) * 0.25, 0.022, 0.21, 0.05, 0.016, 0.009, 0.5), 0.009)
    put(g & (sg > 0.3), _crown_sdf(phig * rg + 0.01, (0.9 - sg) * 0.25, 0.034, 0.13), 0.026)
    riv = np.hypot(np.mod(phig * rg + 0.0, 0.032) - 0.016, (sg - 0.12) * 0.25) - 0.0042
    put(g, riv, 0.008)

    # ---- boots: leather with a gold top rim, a gold knee guard, flames rising up the outer shin, a gold toe cap rim
    b = mat == "boot"
    top = o["boots"]
    phib = np.arctan2((x - side * J["ankle"][0]) * side, -(y - 0.012))
    ztop = top + o.get("boot_front", 0.0) * np.clip(np.cos(phib), 0, 1) ** 3
    put(b & (z > 0.3), np.abs(z - (ztop - 0.012)) - 0.011, 0.022)
    guard = b & (np.abs(phib) < 0.75) & (z > top - 0.085)
    gsdf = np.maximum(np.abs(phib) * 0.05 - 0.032, top - 0.075 - z)
    put(guard, gsdf, 0.05)
    engr = guard & (np.abs(_crown_sdf(phib * 0.047 + 0.0105, z - (top - 0.07), 0.021, 0.07)) < 0.0016)
    shin = b & (z > 0.07) & (z < top - 0.03)
    # a crown of flames rising round the boot from a gold band over the ankle
    put(shin, _crown_sdf(phib * 0.047, z - 0.13, 0.042, 0.2), 0.03)
    put(b & (z > 0.10) & (z < 0.16), np.abs(z - 0.122) - 0.009, 0.018)
    put(b & (z < 0.13), np.abs(z - 0.022) - 0.005, 0.01)  # the welt above the sole
    sole = b & (z < 0.016)
    col[sole] = _srgb(42, 30, 22)

    # ---- the shoulder plate: burnished gold, engraved rings around the spike, darker inside
    pl = mat == "pauldron"
    spike = np.array([0.215, J["shoulder"][1], J["shoulder"][2] + 0.075])
    rd = np.linalg.norm(P - spike, axis=1)
    ring = np.abs(np.mod(rd, 0.024) - 0.012)
    gold_plate = _srgb(228, 138, 26) * (0.85 + 0.3 * grain[:, None])
    gold_plate = gold_plate * np.where(ring < 0.0028, 0.35, 1.0)[:, None]
    gold_plate = gold_plate + sheen(4.0, 0.22)[:, None] * _srgb(255, 190, 70)[None]
    inner = (N @ np.array([1.0, 0, 0.6])) < -0.2
    gold_plate[inner] *= 0.35
    col[pl] = gold_plate[pl]

    # ---- gold: embossed (darker at the edges, raised in the middle), a painted highlight, a dark outline on leather
    gin = gold_sdf < 0
    t = np.clip(-gold_sdf / np.maximum(0.45 * gold_w, 1e-4), 0, 1)
    tone = t * t * (3 - 2 * t)
    gcol = _srgb(130, 50, 2)[None] * (1 - tone[:, None]) + _srgb(245, 155, 30)[None] * tone[:, None]
    gcol = gcol * (0.9 + 0.2 * grain[:, None]) + sheen(6.0, 0.4)[:, None] * _srgb(255, 200, 90)[None] * tone[:, None]
    col[gin] = gcol[gin]
    col[engr] *= 0.4
    rim = (gold_sdf >= 0) & (gold_sdf < 0.0018) & ~is_skin & ~pl
    col[rim] *= 0.5
    # the medallion's ruby and the guard's small ruby
    gem = medal & (med < 0.011)
    col[gem] = (_srgb(150, 18, 20) * (0.6 + 0.6 * np.clip(1 - med[gem, None] / 0.011, 0, 1))) + sheen(8.0, 0.3)[
        gem, None
    ] * _srgb(255, 200, 200)[None]
    # painted light: a little brighter facing up, darker facing down (the bake adds occlusion on top)
    col *= (0.82 + 0.3 * np.clip(N[:, 2] * 0.5 + 0.5, 0, 1))[:, None]
    return col


def paint_tuiri(c, lod, png, out):
    """Unwrap the game mesh, paint Tuiri's albedo per texel, bake it with occlusion and top light into `png`."""
    import numpy as np

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from own_texture import bake_atlas, unwrap

    unwrap(lod)
    res = PAINT_RES
    P, N, M = _rasterize(lod, res)
    names = [m.name for m in lod.data.materials]
    cover = M >= 0
    mat = np.array([*names, ""])[M[cover]]
    col = np.zeros((res, res, 3))
    col[cover] = tuiri_albedo(c, P[cover], N[cover], mat)
    col = _dilate(col, cover, 12)
    img = bpy.data.images.new("tuiri_albedo", res, res, float_buffer=True)
    rgba = np.concatenate([col, np.ones((res, res, 1))], 2).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = str(Path(out) / "tuiri_albedo.exr")
    img.file_format = "OPEN_EXR"
    img.save()
    for name in names:
        m = bpy.data.materials[name]
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        bsdf.inputs["Roughness"].default_value = 1.0
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        uvn = nt.nodes.new("ShaderNodeUVMap")
        uvn.uv_map = "atlas"
        nt.links.new(uvn.outputs[0], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    # the source parts sit on the game mesh: hidden, or they shadow it in the bake
    others = [ob for ob in bpy.context.scene.objects if ob is not lod and not ob.hide_render]
    for ob in others:
        ob.hide_render = True
    slots = bake_atlas(lod, png, materials=names, size=512, samples=128)
    for ob in others:
        ob.hide_render = False
    return slots


# ---------------------------------------------------------------------------- one character


def build_character(key):
    c = CHARS[key]
    J, o = c["joints"], c["outfit"]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    MATS.clear()
    g = build_graph(c)
    body = build_body(c, g)
    verts = [v.co.copy() for v in body.data.vertices]
    parts = [body]
    if "boots" in o:
        parts += build_boots(c, verts)
    if "gauntlet" in o:
        parts += build_gauntlets(c, verts)
    if o.get("pauldron"):
        parts += build_pauldron(c, body)
    if "skirt" in o:
        skirt = build_skirt(c, verts)
        parts += skirt
        if "belt" in o:
            parts += build_belt(c, verts)
        if "sash" in o:
            parts += build_sash(c, verts, skirt[0])
    if "peplum" in o:
        parts += build_peplum(c, verts)
    if "sleeve" in o:
        parts += build_sleeves(c, verts)
    # hidden body: inside the boots, the gauntlets and the skirt
    an = J["ankle"][2]
    hide = []
    if "boots" in o:
        hide.append(lambda p: an + 0.03 < p.z < o["boots"] - 0.025 and abs(p.x) < 0.2)
    if "gauntlet" in o:
        x0, x1 = o["gauntlet"]
        hide.append(lambda p: x0 + 0.02 < abs(p.x) < x1 - 0.01 and p.z > 1.2)
    if "skirt" in o:
        top, hem = o["skirt"]
        hide.append(lambda p: hem + 0.05 < p.z < top - 0.035 and abs(p.x) < 0.2)
    delete_faces(body, lambda p: any(h(p) for h in hide))
    lod = build_game_mesh(parts, f"{key}_game")
    tris = len(lod.data.polygons)
    print(f"{key}: game mesh {len(lod.data.vertices)} vertices, {tris} triangles")
    out = Path(ARGS.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not ARGS.no_export:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from authored_export import export_authored

        sh, el, wr, kn, an = (Vector(J[k]) for k in ("shoulder", "elbow", "wrist", "knee", "ankle"))
        landmarks = {
            "upperbody": (0.0, 0.0, J["hip"]),
            "chest": (0.0, 0.02, J["chest"]),
            "head": (0.0, 0.01, J["head"]),
        }
        for s, sx in (("R", 1), ("L", -1)):
            landmarks |= {
                f"{s}uparm": (sh.x * sx, sh.y, sh.z),
                f"{s}forearm": (el.x * sx, el.y, el.z),
                f"{s}hand": (wr.x * sx, wr.y, wr.z),
                f"{s}thigh": (J["thighx"] * sx, 0.0, J["hip"]),
                f"{s}calf": (kn.x * sx, kn.y, kn.z),
                f"{s}foot": (an.x * sx, an.y, an.z),
            }
        slots = {k: {"pcTextures": v} for k, v in c["slots"].items()}
        if c.get("own_texture"):
            slots = paint_tuiri(c, lod, Path(ARGS.models).resolve() / f"{key}.png", out)
        export_authored(
            lod,
            Path(ARGS.models).resolve() / f"{key}.json",
            model=c["model"],
            label=c["label"],
            fit="joints",
            slots=slots,
            extra={
                "landmarks": {k: [round(x, 4) for x in v] for k, v in landmarks.items()},
                "keepPcJoints": ["head", "hair1", "hair2", "hair3", *c.get("keep", [])],
            },
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(out / f"{key}.blend"))
    if ARGS.render:
        for p in parts:
            p.hide_render = True
        render_clay(lod, out / key)
    return lod


def render_clay(obj, prefix):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_cavity = True
    sc.render.resolution_x, sc.render.resolution_y = 700, 900
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam")))
    sc.camera = cam
    cam.data.lens = 70
    for v, az, tz, d in (
        ("front", 0, 0.86, 4.4),
        ("threequarter", -35, 0.86, 4.4),
        ("side", -90, 0.86, 4.4),
        ("back", 180, 0.86, 4.4),
        ("top", -25, 1.3, 1.6),
    ):
        a = math.radians(az)
        cam.location = (-d * math.sin(a), -d * math.cos(a), tz + 0.15)
        cam.rotation_euler = (Vector((0, 0, tz)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = f"{prefix}_{v}.png"
        bpy.ops.render.render(write_still=True)


def main():
    keys = list(CHARS) if ARGS.which == "all" else ARGS.which.split(",")
    for k in keys:
        build_character(k)


main()
