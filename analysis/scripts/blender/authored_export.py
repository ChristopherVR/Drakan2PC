"""Write a Blender mesh as an authored replacement model for the Order of the Flame converter.

The file format is `drakan-authored-model/1` (editor/packages/formats/src/ootfauthored.ts): geometry only, in Blender
space (metres, Z up, the character facing -Y), triangles counter-clockwise from outside, one slot per material. The
converter places it on the PC model's box and takes UVs, textures and skin weights from the player's own PC model, so
the file holds no game data and can be committed (scripts/buildtool/kit/models/).

Use from a Blender script:

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from authored_export import export_authored
    export_authored(obj, "scripts/buildtool/kit/models/delon.json", model="Delon(h)", label="Delon", fit="height")
"""

from __future__ import annotations

import json
from pathlib import Path

import bmesh
import bpy

FORMAT = "drakan-authored-model/1"


def export_authored(
    obj,
    path: str | Path,
    *,
    model: str,
    label: str,
    fit: str = "height",
    slots: dict[str, dict] | None = None,
    extra: dict | None = None,
    digits: int = 4,
) -> dict:
    """Triangulate `obj` (modifiers applied, world transform baked) and write it. `slots` maps a material name to extra
    slot fields (e.g. {"Skin": {"pcTextures": [0]}}); a slot projects UVs and texture from the PC model (`texture: "pc"`,
    the default) or shows the model's own texture with the mesh's active UV map
    (`{"texture": "image", "image": "knight_armour.png"}`, a PNG saved beside the JSON, original art).
    `extra` adds top-level fields: `landmarks` (PC joint name -> Blender position, with fit="joints"), `keepPcJoints`,
    `radialJoints`. Returns a summary (vertices, triangles)."""
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(obj.evaluated_get(dg), depsgraph=dg)
    me.transform(obj.matrix_world)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bm.verts.ensure_lookup_table()
    names = [m.name if m else "default" for m in me.materials] or ["default"]
    used: list[str] = []
    tris: list[int] = []
    uvs: list[float] = []
    uv_layer = bm.loops.layers.uv.active
    for f in bm.faces:
        name = names[min(f.material_index, len(names) - 1)]
        if name not in used:
            used.append(name)
        tris += [f.verts[0].index, f.verts[1].index, f.verts[2].index, used.index(name)]
        for loop in f.loops:
            uv = loop[uv_layer].uv if uv_layer is not None else (0.0, 0.0)
            uvs += [round(uv[0], 5), round(uv[1], 5)]
    pos: list[float] = []
    for v in bm.verts:
        pos += [round(c, digits) for c in v.co]
    bm.free()
    bpy.data.meshes.remove(me)
    slot_extra = slots or {}
    own = any(slot_extra.get(n, {}).get("texture") == "image" for n in used)
    if own and uv_layer is None:
        raise ValueError(f"{model}: image slots need a UV map on the mesh")
    data = {
        "format": FORMAT,
        "model": model,
        "label": label,
        "fit": fit,
        **(extra or {}),
        "slots": [{"name": n, "texture": "pc", **slot_extra.get(n, {})} for n in used],
        "positions": pos,
        "triangles": tris,
        **({"uvs": uvs} if own else {}),
    }
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # one line per array keeps the file diffable without making it huge
    text = (
        "{\n"
        + ",\n".join(f"  {json.dumps(k)}: {json.dumps(v, separators=(',', ':'))}" for k, v in data.items())
        + "\n}\n"
    )
    path.write_text(text, encoding="utf-8", newline="\n")
    summary = {"vertices": len(pos) // 3, "triangles": len(tris) // 4, "slots": used}
    print(f"authored {model}: {summary['vertices']} vertices, {summary['triangles']} triangles -> {path}")
    return summary
