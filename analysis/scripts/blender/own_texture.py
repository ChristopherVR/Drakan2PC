"""Own textures for authored models: unwrap a game mesh into one atlas and bake its materials into a PNG.

Drakan 2 characters carry their shading painted into the texture (folds, rivets, seams, a soft top light); the engine
adds little lighting. `bake_atlas` does the same in Blender: each material's base colour times ambient occlusion and a
soft overhead light, baked into one square picture (512 px by default: the converter writes 256 px into the game, the
512 px file feeds the HD pack). The result is original art: commit the PNG beside the model JSON and run
scripts/buildtool/pin_model_textures.py.

Use from a model script, after building the game mesh `obj` (materials named per slot):

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from own_texture import bake_atlas
    slots = bake_atlas(obj, out_dir / "knight.png", materials=["Armour", "Cloth"])
    export_authored(obj, out_dir / "knight.json", ..., slots=slots)

Materials not listed stay projected from the 1999 model (a kept face, cut-out wings). Give the listed materials real
surface detail (procedural noise, bevels, painted trims) before baking: a flat colour bakes flat.

Standalone check (an existing authored JSON with every slot baked from simple preset materials):

    blender -b --factory-startup -P analysis/scripts/blender/own_texture.py -- <in.json> <out dir> [--size 512]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import bmesh
import bpy


def unwrap(obj, margin: float = 0.004, angle: float = 66.0) -> None:
    """One UV map ("atlas") over the whole mesh: smart projection, islands packed into the unit square."""
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
    bpy.ops.uv.smart_project(angle_limit=angle * 3.14159 / 180, island_margin=margin, area_weight=1.0)
    bpy.ops.uv.pack_islands(margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


def _ensure_light_rig() -> None:
    """A soft sky from above and a weak fill from below: the painted-in light of Drakan 2 textures."""
    world = bpy.context.scene.world or bpy.data.worlds.new("bake_world")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld")
    bg = nt.nodes.new("ShaderNodeBackground")
    grad = nt.nodes.new("ShaderNodeValToRGB")  # brighter from above
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    coord = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(coord.outputs["Generated"], sep.inputs[0])
    grad.color_ramp.elements[0].color = (0.35, 0.35, 0.37, 1)
    grad.color_ramp.elements[1].color = (1.15, 1.12, 1.05, 1)
    nt.links.new(sep.outputs["Z"], grad.inputs[0])
    nt.links.new(grad.outputs[0], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = 1.0
    nt.links.new(bg.outputs[0], out.inputs[0])


def bake_atlas(
    obj,
    path: str | Path,
    *,
    materials: list[str] | None = None,
    size: int = 512,
    samples: int = 64,
    margin_px: int = 4,
) -> dict[str, dict]:
    """Unwrap `obj` (if it has no "atlas" UV map yet) and bake the listed materials (default all) into `path`.
    Returns the `slots` argument for `export_authored`: {material name: {"texture": "image", "image": file name}}."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    names = [m.name for m in obj.data.materials if m]
    chosen = [n for n in names if materials is None or n in materials]
    if "atlas" not in obj.data.uv_layers:
        unwrap(obj)
    obj.data.uv_layers.active = obj.data.uv_layers["atlas"]
    img = bpy.data.images.new(path.stem, size, size, alpha=True)
    img.generated_color = (0, 0, 0, 0)
    # every baked material writes into the same image through an active image node
    for n in chosen:
        mat = bpy.data.materials[n]
        mat.use_nodes = True
        node = mat.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = img
        node.name = "own_bake_target"
        mat.node_tree.nodes.active = node
    # materials that are not baked must not receive the bake: give them a throwaway target
    scratch = bpy.data.images.new(path.stem + "_scratch", 8, 8)
    for n in names:
        if n in chosen:
            continue
        mat = bpy.data.materials[n]
        mat.use_nodes = True
        node = mat.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = scratch
        mat.node_tree.nodes.active = node
    _ensure_light_rig()
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "OPTIX"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        scene.cycles.device = "GPU"
    except (KeyError, TypeError, AttributeError):
        scene.cycles.device = "CPU"
    for o in bpy.context.selected_objects:
        o.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    # COMBINED with only diffuse light: base colour x (sky light + occlusion), no speculars that would move with the view
    bake = scene.render.bake
    bake.margin = margin_px
    bake.use_pass_direct = True
    bake.use_pass_indirect = True
    bake.use_pass_diffuse = True
    bake.use_pass_glossy = False
    bake.use_pass_transmission = False
    bake.use_pass_emit = True
    bpy.ops.object.bake(type="COMBINED")
    img.filepath_raw = str(path.resolve())
    img.file_format = "PNG"
    img.save()
    for n in chosen:
        mat = bpy.data.materials[n]
        node = mat.node_tree.nodes.get("own_bake_target")
        if node:
            mat.node_tree.nodes.remove(node)
    print(f"baked {len(chosen)} material(s) of {obj.name} into {path} ({size} px)")
    return {n: {"texture": "image", "image": path.name} for n in chosen}


# ----------------------------------------------------------------------------- standalone check

PRESETS = {  # rough colours for a quick retexture of a whole file (the real models use their own materials)
    "default": (0.45, 0.40, 0.33),
}


def _mesh_from_authored(data: dict):
    me = bpy.data.meshes.new(data["label"])
    pos = data["positions"]
    tris = data["triangles"]
    verts = [(pos[i], pos[i + 1], pos[i + 2]) for i in range(0, len(pos), 3)]
    faces = [(tris[i], tris[i + 1], tris[i + 2]) for i in range(0, len(tris), 4)]
    me.from_pydata(verts, [], faces)
    obj = bpy.data.objects.new(data["label"], me)
    bpy.context.scene.collection.objects.link(obj)
    for slot in data["slots"]:
        mat = bpy.data.materials.new(slot["name"])
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        noise = mat.node_tree.nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 40.0
        ramp = mat.node_tree.nodes.new("ShaderNodeValToRGB")
        c = PRESETS.get(slot["name"].lower(), PRESETS["default"])
        ramp.color_ramp.elements[0].color = (c[0] * 0.7, c[1] * 0.7, c[2] * 0.7, 1)
        ramp.color_ramp.elements[1].color = (c[0], c[1], c[2], 1)
        mat.node_tree.links.new(noise.outputs["Fac"], ramp.inputs[0])
        mat.node_tree.links.new(ramp.outputs[0], bsdf.inputs["Base Color"])
        me.materials.append(mat)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    for k, f in enumerate(bm.faces):
        f.material_index = tris[k * 4 + 3]
    bm.to_mesh(me)
    bm.free()
    return obj


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    if len(argv) < 2:
        print("usage: blender -b -P own_texture.py -- <in.json> <out dir> [--size 512]")
        return
    src, out = Path(argv[0]), Path(argv[1]).resolve()
    size = int(argv[argv.index("--size") + 1]) if "--size" in argv else 512
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored

    data = json.loads(src.read_text(encoding="utf-8"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    obj = _mesh_from_authored(data)
    slots = bake_atlas(obj, out / f"{src.stem}.png", size=size)
    extra = {k: data[k] for k in ("landmarks", "keepPcJoints", "radialJoints") if k in data}
    export_authored(
        obj, out / src.name, model=data["model"], label=data["label"], fit=data["fit"], slots=slots, extra=extra
    )


if __name__ == "__main__":
    main()
