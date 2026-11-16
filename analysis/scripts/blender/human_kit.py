"""Reusable pieces for new Order of the Flame human meshes (Julian, the farmer, villagers, ...), headless Blender.

Adapted from `delon_model.py` (do not edit that one): the body is a set of closed tubes and blobs placed at hand-copied,
rounded proportions of each 1999 character, joined into one surface by a voxel remesh and smoothed; hands are skin
modifier graphs with fingers; clothing edges (belts, hems, cuffs, boot tops, collars) are rings fitted to that body
(`ring_band`); the game LOD is decimated per part to a triangle budget and written with `authored_export.py` for the
converter (`fit="joints"` landmarks, the 1999 head kept). Nothing here reads game data.

Coordinates: metres, Z up, the character faces -Y, R joints on +X (as the PC dumps import in Blender).

A tube section is (x, y, z, ru, rw): the centre and the half extents along the section frame (`frame`): for a mostly
vertical tube u = +X and w = d x u (+Y for an upward tube); for any other direction u is "up" and w = d x u.
"""

from __future__ import annotations

import itertools
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

# ---------------------------------------------------------------------------- helpers


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


def recalc_normals(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(obj.data)
    bm.free()


def frame(d):
    """Section frame (u, w) for a tube going along d (see the module doc)."""
    d = Vector(d).normalized()
    if abs(d.z) > 0.7:
        u = Vector((1.0, 0.0, 0.0))
    else:
        u = Vector((0.0, 0.0, 1.0))
    u = (u - d * d.dot(u)).normalized()
    return u, d.cross(u)


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length, t


def mirror_x(sections):
    return [(-s[0], *s[1:]) for s in sections]


def scale_sections(sections, ku=1.0, kw=None):
    kw = ku if kw is None else kw
    return [(x, y, z, ru * ku, rw * kw) for x, y, z, ru, rw in sections]


# ---------------------------------------------------------------------------- materials (clay colours for previews;
# the game takes every texture from the 1999 model)

COLOURS = {
    "skin": (0.52, 0.32, 0.23),
    "shirt": (0.40, 0.29, 0.18),
    "robe": (0.10, 0.20, 0.10),
    "trousers": (0.12, 0.072, 0.042),
    "leather": (0.10, 0.065, 0.042),
    "shoe": (0.035, 0.022, 0.015),
    "belt": (0.065, 0.036, 0.018),
    "metal": (0.62, 0.42, 0.17),
    "trim": (0.06, 0.035, 0.02),
    "mail": (0.30, 0.30, 0.32),
    "kilt": (0.08, 0.05, 0.04),
}


def material(name):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*COLOURS.get(name, (0.5, 0.5, 0.5)), 1.0)
    return m


def add_mat(obj, name):
    obj.data.materials.append(material(name))
    return len(obj.data.materials) - 1


# ---------------------------------------------------------------------------- body parts (joined by a voxel remesh)


def tube_mesh(name, sections, power=2.0, nseg=32, fold=None, caps=True, mat=None):
    """Tube through sections (x, y, z, ru, rw). power > 2 makes boxier sections. fold(a, t) adds a radial offset
    (metres) at angle a (0 = +u) and section fraction t (folds of a hem). Closed with caps unless caps=False."""
    n = len(sections)
    cs = [Vector(s[:3]) for s in sections]
    vs, faces = [], []
    for i, s in enumerate(sections):
        d = cs[i + 1] - cs[i] if i < n - 1 else cs[i] - cs[i - 1]
        u, w = frame(d)
        for k in range(nseg):
            a = 2 * math.pi * k / nseg
            ca, sa = math.cos(a), math.sin(a)
            eu = math.copysign(abs(ca) ** (2 / power), ca)
            ew = math.copysign(abs(sa) ** (2 / power), sa)
            off = fold(a, i / max(1, n - 1)) if fold else 0.0
            vs.append(cs[i] + u * eu * (s[3] + off) + w * ew * (s[4] + off))
    for i in range(n - 1):
        for k in range(nseg):
            a, b = i * nseg + k, i * nseg + (k + 1) % nseg
            faces.append((a, b, b + nseg, a + nseg))
    if caps:
        for i in (0, n - 1):
            vs.append(cs[i])
            c = len(vs) - 1
            for k in range(nseg):
                faces.append((i * nseg + k, i * nseg + (k + 1) % nseg, c))
    obj = mesh_obj(name, vs, faces)
    recalc_normals(obj)
    if mat:
        add_mat(obj, mat)
        shade_smooth(obj)
    return obj


def torus(name, center, rx, ry, r, mat, nseg=48, nring=10, tilt=0.0):
    """A rolled edge (collar, cuff, hem lip): a ring of radii (rx, ry) around Z through center, tube radius r. tilt
    raises the front (-Y) by that many metres."""
    c = Vector(center)
    vs, faces = [], []
    for i in range(nseg):
        a = 2 * math.pi * i / nseg
        p = c + Vector((rx * math.cos(a), ry * math.sin(a), -tilt * math.sin(a)))
        out = Vector((math.cos(a) / rx, math.sin(a) / ry, 0)).normalized()
        for j in range(nring):
            b = 2 * math.pi * j / nring
            vs.append(p + out * r * math.cos(b) + Vector((0, 0, r * math.sin(b))))
    for i in range(nseg):
        for j in range(nring):
            a0 = i * nring + j
            a1 = i * nring + (j + 1) % nring
            b0 = ((i + 1) % nseg) * nring + j
            b1 = ((i + 1) % nseg) * nring + (j + 1) % nring
            faces.append((a0, b0, b1, a1))
    obj = mesh_obj(name, vs, faces)
    recalc_normals(obj)
    add_mat(obj, mat)
    shade_smooth(obj)
    return obj


class Body:
    """Closed tubes and blobs that become one surface. Each part remembers its axis for the material tags."""

    def __init__(self, name):
        self.name = name
        self.parts = []  # objects
        self.axes = []  # (list of (Vector centre, radius), material)

    def tube(self, sections, mat, power=2.0, nseg=32, fold=None, caps=True):
        """Closed tube through sections (x, y, z, ru, rw), joined into the body (see `tube_mesh`)."""
        obj = tube_mesh(f"{self.name}_part", sections, power, nseg, fold, caps)
        self.parts.append(obj)
        cs = [Vector(s[:3]) for s in sections]
        self.axes.append(([(cs[i], 0.5 * (s[3] + s[4])) for i, s in enumerate(sections)], mat))
        return obj

    def blob(self, centre, radii, mat):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=1.0)
        for v in bm.verts:
            v.co = Vector((v.co.x * radii[0], v.co.y * radii[1], v.co.z * radii[2])) + Vector(centre)
        me = bpy.data.meshes.new(f"{self.name}_blob")
        bm.to_mesh(me)
        bm.free()
        obj = link(bpy.data.objects.new(f"{self.name}_blob", me))
        self.parts.append(obj)
        self.axes.append(([(Vector(centre), sum(radii) / 3)], mat))
        return obj

    def build(self, voxel=0.0045, smooth=8, tag=None):
        """Join, remesh, smooth; each face takes the material of the nearest part axis, then tag(centre, mat) may
        change it. Returns the body object."""
        bpy.ops.object.select_all(action="DESELECT")
        for o in self.parts:
            o.select_set(True)
        bpy.context.view_layer.objects.active = self.parts[0]
        bpy.ops.object.join()
        obj = bpy.context.active_object
        obj.name = f"{self.name}_Body"
        rm = obj.modifiers.new("remesh", "REMESH")
        rm.mode = "VOXEL"
        rm.voxel_size = voxel
        sm = obj.modifiers.new("smooth", "CORRECTIVE_SMOOTH")
        sm.iterations = smooth
        sm.use_only_smooth = True
        apply_modifiers(obj)
        shade_smooth(obj)
        names = []
        for _, m in self.axes:
            if m not in names:
                names.append(m)
        for m in names:
            add_mat(obj, m)
        for poly in obj.data.polygons:
            c = poly.center
            best, mat = 1e9, names[0]
            for pts, m in self.axes:
                if len(pts) == 1:
                    d = (c - pts[0][0]).length / max(pts[0][1], 1e-4)
                else:
                    for (a, ra), (b, rb) in itertools.pairwise(pts):
                        dd, t = seg_dist(c, a, b)
                        d = dd / max(ra + (rb - ra) * t, 1e-4)
                        if d < best:
                            best, mat = d, m
                    continue
                if d < best:
                    best, mat = d, m
            if tag:
                mat = tag(c, mat)
                if mat not in names:
                    names.append(mat)
                    add_mat(obj, mat)
            poly.material_index = names.index(mat)
        return obj


# ---------------------------------------------------------------------------- hands


def hand(
    name, wrist, along, thumb_side, palm_normal, size=1.0, curl=0.3, mat="skin", spread=1.0, stub=0.035, fist=False
):
    """A hand with four fingers and a thumb, from a skin modifier graph (Delon's fingers). along: wrist to knuckles;
    thumb_side: across the palm towards the thumb; palm_normal: the way the palm faces. stub: a length of wrist behind
    `wrist` that reaches into the sleeve or forearm. fist: fingers folded into the palm, the thumb across them."""
    D = Vector(along).normalized()
    S = Vector(thumb_side).normalized()
    N = Vector(palm_normal).normalized()
    W = Vector(wrist)
    v, r, e = [], [], []

    def add(p, rad):
        v.append(p)
        r.append(rad)
        return len(v) - 1

    k = size
    iw = add(W, 0.022 * k)
    ip = add(W + D * 0.045 * k, 0.024 * k)
    e.append((iw, ip))
    if stub > 0:
        e.append((add(W - D * stub, 0.021 * k), iw))
    for i, (dy, ln, rr) in enumerate(
        [(0.019, 0.068, 0.0082), (0.0065, 0.076, 0.0085), (-0.0065, 0.071, 0.0081), (-0.0185, 0.058, 0.0072)]
    ):
        k0 = W + D * 0.082 * k + S * dy * k * spread
        k1 = k0 + D * ln * 0.5 * k + N * 0.008 * curl * k
        k2 = k0 + D * ln * 0.92 * k + N * 0.03 * curl * k
        if fist:
            k1 = k0 + D * ln * 0.38 * k + N * ln * 0.12 * k
            k2 = k1 + N * ln * 0.42 * k - D * ln * 0.18 * k
        a = add(k0, rr * k)
        b = add(k1, rr * 0.9 * k)
        c = add(k2, rr * 0.75 * k)
        e += [(ip, a), (a, b), (b, c)]
    t0 = W + D * 0.022 * k + S * 0.020 * k + N * 0.008 * k
    t1 = t0 + D * 0.026 * k + S * 0.020 * k + N * 0.006 * k
    t2 = t1 + D * 0.028 * k + S * 0.008 * k + N * 0.006 * k * (1 + curl)
    if fist:  # the thumb lies across the folded index and middle fingers
        t1 = t0 + D * 0.030 * k + S * 0.010 * k + N * 0.022 * k
        t2 = t1 + D * 0.028 * k - S * 0.016 * k + N * 0.012 * k
    a = add(t0, 0.012 * k)
    b = add(t1, 0.010 * k)
    c = add(t2, 0.0085 * k)
    e += [(iw, a), (a, b), (b, c)]
    obj = mesh_obj(name, v, edges=e)
    sk = obj.modifiers.new("skin", "SKIN")
    sk.use_smooth_shade = True
    sk.branch_smoothing = 0.6
    for i, rad in enumerate(r):
        sd = obj.data.skin_vertices[0].data[i]
        sd.radius = (rad * 1.15, rad * (1.25 if i < 2 else 1.0))
    obj.data.skin_vertices[0].data[iw].use_root = True
    ss = obj.modifiers.new("sub", "SUBSURF")
    ss.levels = ss.render_levels = 1
    apply_modifiers(obj)
    shade_smooth(obj)
    add_mat(obj, mat)
    return obj


# ---------------------------------------------------------------------------- rings fitted to the body


def body_profile(verts, center, axis, lo, hi, sectors, maxr=0.25):
    """Max radial distance of body vertices around an axis, per angular sector, for heights in [lo, hi]."""
    axis = axis.normalized()
    ref = Vector((0, -1, 0)) if abs(axis.y) < 0.9 else Vector((1, 0, 0))
    u = (ref - axis * ref.dot(axis)).normalized()
    w = axis.cross(u)
    prof = [0.0] * sectors
    for v in verts:
        d = v - center
        h = d.dot(axis)
        if h < lo or h > hi:
            continue
        x, y = d.dot(u), d.dot(w)
        rr = math.hypot(x, y)
        if rr > maxr:
            continue
        k = int(((math.atan2(y, x) / (2 * math.pi)) % 1.0) * sectors) % sectors
        prof[k] = max(prof[k], rr)
    for _ in range(3):
        prof = [max(prof[i], 0.5 * (prof[i - 1] + prof[(i + 1) % sectors]) * 0.98) for i in range(sectors)]
    sm = [(prof[i - 1] + 2 * prof[i] + prof[(i + 1) % sectors]) / 4 for i in range(sectors)]
    return sm, u, w, axis


def ring_band(name, body, center, axis, rows, sectors=48, mat="belt", thick=0.004, mono=False, fold=None, maxr=0.25):
    """A band around the body: rows are (height along axis from center, offset out of the body surface)."""
    verts = [v.co.copy() for v in body.data.vertices]
    center = Vector(center)
    axis = Vector(axis)
    vs, faces = [], []
    prev = None
    for ri, (h, off) in enumerate(rows):
        prof, u, w, ax = body_profile(verts, center, axis, h - 0.012, h + 0.012, sectors, maxr)
        if mono and prev is not None:
            prof = [max(p, q) for p, q in zip(prof, prev)]
        prev = prof
        for k in range(sectors):
            a = (k + 0.5) / sectors * 2 * math.pi
            rr = prof[k] + off + (fold(a, ri / max(1, len(rows) - 1)) if fold else 0.0)
            vs.append(center + ax * h + (u * math.cos(a) + w * math.sin(a)) * rr)
    for ri in range(len(rows) - 1):
        for k in range(sectors):
            a, b = ri * sectors + k, ri * sectors + (k + 1) % sectors
            faces.append((a, b, b + sectors, a + sectors))
    obj = mesh_obj(name, vs, faces)
    add_mat(obj, mat)
    so = obj.modifiers.new("solid", "SOLIDIFY")
    so.thickness = thick
    so.offset = 1.0
    apply_modifiers(obj)
    shade_smooth(obj)
    return obj


def box(name, loc, size, mat, rot=(0, 0, 0), bevel=0.003):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        bv = o.modifiers.new("bev", "BEVEL")
        bv.width = bevel
        bv.segments = 2
        apply_modifiers(o)
    add_mat(o, mat)
    return o


def front_y(body, x, z, tol=0.008):
    """Front surface (min Y) of the body at (x, z)."""
    ys = [v.co.y for v in body.data.vertices if abs(v.co.x - x) < tol and abs(v.co.z - z) < tol]
    return min(ys, default=0.0)


# ---------------------------------------------------------------------------- game LOD and export


def lod_budget(budgets, name):
    for k, v in sorted(budgets.items(), key=lambda kv: -len(kv[0])):
        if name.startswith(k):
            return v
    return 0


def build_lod(objs, budgets, name):
    """One mesh: each part decimated to its own triangle budget, then joined and triangulated."""
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    mats = []
    for o in objs:
        budget = lod_budget(budgets, o.name)
        if o.type != "MESH" or budget <= 0:
            continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), depsgraph=dg)
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
    return lod


def export(lod, path, model, label, landmarks, keep=("head",)):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored

    lm = {j: [round(c, 4) for c in p] for j, p in landmarks.items()}
    return export_authored(
        lod,
        Path(path).resolve(),
        model=model,
        label=label,
        fit="joints",
        extra={"landmarks": lm, "keepPcJoints": list(keep)},
    )


def clay_render(objs, out_prefix, res=700):
    """Workbench clay views (front, three-quarter, side) of some objects, for a quick look at the shapes."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "MATERIAL"
    sc.render.resolution_y = res
    sc.render.resolution_x = int(res * 0.8)
    show = set(objs)
    for o in sc.objects:
        o.hide_render = o not in show
    cam = bpy.data.objects.get("KitCam") or link(bpy.data.objects.new("KitCam", bpy.data.cameras.new("KitCam")))
    sc.camera = cam
    cam.data.lens = 70
    lo = Vector((1e9, 1e9, 1e9))
    hi = -lo
    for o in objs:
        for v in o.data.vertices:
            p = o.matrix_world @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    mid = (lo + hi) / 2
    h = max(hi.z - lo.z, (hi.x - lo.x) * 0.8)
    for view, az in (("front", 0), ("threequarter", -35), ("side", -90)):
        a = math.radians(az)
        dist = h * 2.6
        cam.location = mid + Vector((-dist * math.sin(a), -dist * math.cos(a), 0.05 * h))
        cam.rotation_euler = (mid - cam.location).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = f"{out_prefix}_{view}.png"
        bpy.ops.render.render(write_still=True)
