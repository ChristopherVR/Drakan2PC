"""Import a model dumped by editor/packages/formats/scripts/dump-pc-model.ts into Blender, rigged and textured.

    blender -b --factory-startup -P analysis/scripts/blender/import_pc_model.py -- \
        build/pcmodels/Delon_h.obj [--height 1.72] [--save out.blend] [--render out_prefix] [--face] [--unlit]

Without -b it opens the result in the Blender UI. The OBJ is read directly (not through Blender's OBJ importer) so the
axis change is explicit: PC game space is Y up, left handed; Blender is Z up, right handed, so (x, y, z) -> (x, z, y).
The model is scaled so its bind pose is --height metres tall (PC character units are about 1/5 m). If <name>.skeleton.json
sits next to the OBJ, an armature is built from it (joint names as in the game, bind pose from the inverse bind matrices)
and the LOD 0 weights become vertex groups. --face adds head close-ups (front, three-quarter, side) to --render; --unlit
renders the textures flat (no scene lighting), to judge what is painted into them. PS2 dumps (dump-ps2-model.ts) open
the same way: same axes, but PS2 humans are about 0.4 units tall, so keep --height (or 0 for raw units).
Inputs are game data: keep them and anything saved from them under build/.
"""

import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("obj", type=Path)
    ap.add_argument("--height", type=float, default=1.72, help="bind pose height in metres (0 = keep game units)")
    ap.add_argument("--save", type=Path)
    ap.add_argument("--render", help="render front/side/3-4 views to <prefix>_<view>.png")
    ap.add_argument("--clear", action="store_true", help="start from an empty scene")
    ap.add_argument(
        "--face", action="store_true", help="with --render: also render head close-ups (<prefix>_face_<view>.png)"
    )
    ap.add_argument("--unlit", action="store_true", help="with --render: flat texture colour, no lighting (Workbench)")
    return ap.parse_args(argv)


def to_blender(p):
    return Vector((p[0], p[2], p[1]))


def read_obj(path: Path):
    verts, uvs, faces, mtl, cur = [], [], [], {}, None
    for line in path.read_text().splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "v":
            verts.append(to_blender([float(x) for x in t[1:4]]))
        elif t[0] == "vt":
            uvs.append((float(t[1]), float(t[2])))
        elif t[0] == "usemtl":
            cur = t[1]
        elif t[0] == "f":
            idx = [tuple(int(x) - 1 for x in c.split("/")[:2]) for c in t[1:]]
            faces.append((cur, idx[::-1]))  # the axis swap mirrors, so reverse the winding
    mtl_path = path.with_suffix(".mtl")
    if mtl_path.exists():
        name = None
        for line in mtl_path.read_text().splitlines():
            t = line.split()
            if t and t[0] == "newmtl":
                name = t[1]
            elif t and t[0] == "map_Kd" and name:
                mtl[name] = path.parent / t[1]
    return verts, uvs, faces, mtl


def make_material(name, image_path):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.8
    if image_path and image_path.exists():
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(str(image_path))
        tex.interpolation = "Closest"  # the game's look; switch to Linear for smoother reference
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        if hasattr(m, "surface_render_method"):
            m.surface_render_method = "DITHERED"
    return m


def build(args):
    if args.clear:
        bpy.ops.wm.read_factory_settings(use_empty=True)
    verts, uvs, faces, mtl = read_obj(args.obj)
    name = args.obj.stem
    lo = min(v.z for v in verts)
    hi = max(v.z for v in verts)
    scale = args.height / (hi - lo) if args.height > 0 else 1.0
    S = Matrix.Diagonal((scale, scale, scale, 1.0)) @ Matrix.Translation((0, 0, -lo))
    me = bpy.data.meshes.new(name)
    me.from_pydata([S @ v for v in verts], [], [[i for i, _ in f] for _, f in faces])
    uv = me.uv_layers.new(name="UVMap")
    slots = sorted({m for m, _ in faces}, key=lambda s: (s is None, s))
    for s in slots:
        me.materials.append(make_material(f"{name}_{s}", mtl.get(s)))
    li = 0
    for poly, (m, f) in zip(me.polygons, faces):
        poly.material_index = slots.index(m)
        poly.use_smooth = True
        for _, t in f:
            uv.data[li].uv = uvs[t] if 0 <= t < len(uvs) else (0, 0)
            li += 1
    me.validate()
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    skel_path = args.obj.with_suffix("").with_suffix(".skeleton.json")
    if not skel_path.exists():
        skel_path = args.obj.parent / f"{name}.skeleton.json"
    rig = build_rig(obj, skel_path, S, len(verts)) if skel_path.exists() else None
    print(f"imported {name}: {len(verts)} verts, {len(faces)} faces, scale {scale:.3f}, rig {'yes' if rig else 'no'}")
    return obj, rig


def joint_world(inv_bind):
    """World (model) position and basis of a joint from its PC inverse bind matrix (row vectors: p' = p R + t)."""
    R = Matrix((inv_bind[0:3], inv_bind[3:6], inv_bind[6:9]))
    t = Vector(inv_bind[9:12])
    Rw = R.transposed()  # inverse rotation
    pos = -(t @ Rw)
    return pos, Rw


def build_rig(obj, path, S, nverts):
    data = json.loads(path.read_text())
    joints = data["joints"]
    arm = bpy.data.armatures.new(obj.name + "_rig")
    rig = bpy.data.objects.new(obj.name + "_rig", arm)
    bpy.context.scene.collection.objects.link(rig)
    rig.show_in_front = True
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    heads = [S @ to_blender(joint_world(j["inverseBind"])[0]) for j in joints]
    children = {i: [k for k, j in enumerate(joints) if j["parent"] == i] for i in range(len(joints))}
    bones = []
    for i, j in enumerate(joints):
        b = arm.edit_bones.new(j["name"])
        b.head = heads[i]
        kids = children[i]
        if kids:
            tail = sum((heads[k] for k in kids), Vector()) / len(kids)
        elif j["parent"] >= 0:
            tail = heads[i] + (heads[i] - heads[j["parent"]]).normalized() * 0.05
        else:
            tail = heads[i] + Vector((0, 0, 0.1))
        if (tail - b.head).length < 1e-3:
            tail = b.head + Vector((0, 0, 0.03))
        b.tail = tail
        bones.append(b)
    for i, j in enumerate(joints):
        if j["parent"] >= 0:
            bones[i].parent = bones[j["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")
    for j in joints:
        vg = obj.vertex_groups.new(name=j["name"])
        for v, w in j["weights"]:
            if 0 <= v < nverts:
                vg.add([v], w, "REPLACE")
    obj.parent = rig
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    return rig


def render(prefix, obj, face=False, unlit=False):
    sc = bpy.context.scene
    if unlit:
        sc.render.engine = "BLENDER_WORKBENCH"
        sc.display.shading.light = "FLAT"
        sc.display.shading.color_type = "TEXTURE"
    else:
        sc.render.engine = (
            "BLENDER_EEVEE"
            if "BLENDER_EEVEE" in {e.identifier for e in sc.render.bl_rna.properties["engine"].enum_items}
            else "CYCLES"
        )
    sc.render.resolution_x, sc.render.resolution_y = 800, 1000
    if not sc.world:
        sc.world = bpy.data.worlds.new("w")
    sc.world.color = (0.05, 0.055, 0.065)
    for loc, e in (((-2, -3, 3), 600), ((3, -1, 2), 250), ((0, 3, 3), 400)):
        ld = bpy.data.lights.new("l", "AREA")
        ld.energy, ld.size = e, 2
        lo = bpy.data.objects.new("l", ld)
        sc.collection.objects.link(lo)
        lo.location = loc
        lo.rotation_euler = (Vector((0, 0, 1)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    h = max(v.co.z for v in obj.data.vertices) * obj.matrix_world.to_scale().z
    import math

    views = [("", az, h * 0.5, h * 2.7, h * 0.55) for az in (0, -35, -90)]
    if face:
        # head: centre of the vertices in the top 13 % of the bind pose (the T pose arms sit lower)
        ws = [obj.matrix_world @ v.co for v in obj.data.vertices]
        top = [p for p in ws if p.z >= h * 0.87]
        c = sum(top, Vector()) / len(top)
        views += [("face_", az, c.z, h * 0.42, c.z) for az in (0, -35, -90)]
    else:
        c = Vector()
    names = {0: "front", -35: "threequarter", -90: "side"}
    for pre, az, aim, d, cz in views:
        a = math.radians(az)
        ox, oy = (c.x, c.y) if pre else (0.0, 0.0)
        cam.location = (ox - d * math.sin(a), oy - d * math.cos(a), cz)
        cam.rotation_euler = (Vector((ox, oy, aim)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 70
        sc.render.filepath = f"{prefix}_{pre}{names[az]}.png"
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    a = parse_args()
    a.obj = a.obj.resolve()
    o, _ = build(a)
    if a.save:
        bpy.ops.wm.save_as_mainfile(filepath=str(a.save.resolve()))
    if a.render:
        render(str(Path(a.render).resolve()), o, a.face, a.unlit)
