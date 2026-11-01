"""Import a Drakan level exported by the editor (File > Export > Level for Unreal) into Unreal Engine 5.

Run it inside the editor (Python Editor Script Plugin enabled):

    py "C:/path/to/drakan_import.py" "C:/path/to/Level1A_unreal.glb" /Game/Drakan/Surdana

or from the Output Log / Python console:

    import drakan_import; drakan_import.import_level(r"C:/path/Level1A_unreal.glb", "/Game/Drakan/Surdana")

What it does:
  1. Imports the .glb as assets (static meshes, skeletal meshes with skeletons and animations, materials, textures).
  2. Spawns actors from the per-node extras (extras.drakan): terrain chunks, static instances, skeletal characters
     (playing an idle clip when one exists), cameras, point lights, and a TargetPoint for every other object. Every
     actor gets the tags `drakan`, `drakan.id=<id>`, `drakan.class=<class>`, `drakan.kind=<kind>` plus a
     `drakan.extras=<json>` tag with the object's properties and LVL fields.

Without Unreal, `python drakan_import.py level.glb --plan` prints what would be spawned (used to test the contract).

Coordinates: the glb is glTF (Y up, metres). Unreal's glTF import maps glTF (x, y, z) to Unreal (X, Y, Z) = (x, z, y) * 100.
The transforms in the extras (`ue.location` in cm, `ue.quat`, `ue.scale`) are already in Unreal space under that mapping, so
actors are placed from the extras and do not depend on how the importer converts node transforms. If the imported
meshes look mirrored or rotated against the spawned actors, your importer uses another mapping: change UE_FROM_GLTF
mode in the exporter docs (docs/reference/unreal-export.md) and re-export.

Written for UE 5.3 - 5.5 (Interchange glTF import). It has not been run against a UE install by the author.
"""

import json
import os
import re
import struct
import sys

EXTRAS_KEY = "drakan"


def read_glb(path):
    """Return the glTF JSON of a .glb (the binary chunk is not needed here)."""
    with open(path, "rb") as f:
        head = f.read(12)
        magic, version, _length = struct.unpack("<III", head)
        if magic != 0x46546C67 or version != 2:
            raise ValueError("not a glTF 2.0 binary file")
        clen, ctype = struct.unpack("<II", f.read(8))
        if ctype != 0x4E4F534A:
            raise ValueError("first chunk is not JSON")
        return json.loads(f.read(clen).decode("utf-8"))


def _safe(name):
    return re.sub(r"[^A-Za-z0-9_]+", "_", name or "").strip("_")


def plan(gltf):
    """Spawn entries from the node extras: a list of dicts {kind, label, mesh, ue, extras, ...}."""
    nodes = gltf.get("nodes", [])
    out = []
    for node in nodes:
        d = (node.get("extras") or {}).get(EXTRAS_KEY)
        if not d:
            continue
        t = d.get("type")
        if t == "terrain":
            out.append(
                {
                    "kind": "terrain",
                    "label": node["name"],
                    "mesh": d.get("staticMesh"),
                    "extras": d,
                    "ue": {"location": [0, 0, 0], "quat": [0, 0, 0, 1], "scale": [1, 1, 1]},
                }
            )
        elif t == "instance":
            kind = "marker"
            mesh = d.get("staticMesh")
            if d.get("skeletalMesh"):
                kind = "skeletal"
            elif mesh:
                kind = "static"
            elif "camera" in node:
                kind = "camera"
            elif "KHR_lights_punctual" in (node.get("extensions") or {}):
                kind = "light"
            out.append(
                {
                    "kind": kind,
                    "label": node["name"],
                    "mesh": mesh,
                    "skeletal": d.get("skeletalMesh"),
                    "ue": d["ue"],
                    "extras": d,
                }
            )
        elif t == "instances":
            for inst in d.get("instances", []):
                out.append(
                    {
                        "kind": "static",
                        "label": "%s_%s" % (_safe(inst.get("class")), inst.get("id")),
                        "mesh": inst.get("staticMesh") or d.get("staticMesh"),
                        "ue": inst["ue"],
                        "extras": inst,
                    }
                )
    return out


def animations_by_class(gltf):
    """{class name: [animation extras]} for the clips in the file."""
    out = {}
    for a in gltf.get("animations", []):
        d = (a.get("extras") or {}).get(EXTRAS_KEY)
        if d and d.get("type") == "clip":
            out.setdefault(d["skeletalMesh"], []).append(dict(d, name=a.get("name")))
    return out


def _tags(extras):
    tags = ["drakan"]
    if "id" in extras:
        tags.append("drakan.id=%s" % extras["id"])
    if extras.get("class"):
        tags.append("drakan.class=%s" % extras["class"])
    if extras.get("kind"):
        tags.append("drakan.kind=%s" % extras["kind"])
    body = {
        k: v
        for k, v in extras.items()
        if k in ("props", "fields", "links", "origin", "resource", "db", "typeId", "gamePos")
    }
    tags.append("drakan.extras=" + json.dumps(body, separators=(",", ":")))
    return tags


def import_level(glb_path, dest="/Game/Drakan/Level", spawn=True, skip_import=False):
    """Import the glb into `dest` and spawn the actors. Returns the number of actors spawned."""
    import unreal  # only inside the Unreal editor

    gltf = read_glb(glb_path)
    if not skip_import:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", glb_path)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not spawn:
        return 0

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([dest], True)
    by_name = {}
    for a in registry.get_assets_by_path(dest, recursive=True):
        by_name[str(a.asset_name)] = a
        by_name.setdefault(_safe(str(a.asset_name)), a)

    def find(name):
        if not name:
            return None
        a = by_name.get(name) or by_name.get(_safe(name))
        return a.get_asset() if a else None

    clips = animations_by_class(gltf)
    level = os.path.splitext(os.path.basename(glb_path))[0]
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    count = 0

    def place(cls, entry, folder):
        ue = entry["ue"]
        loc = unreal.Vector(*ue["location"])
        rot = unreal.Quat(*ue["quat"]).rotator()
        actor = sub.spawn_actor_from_class(cls, loc, rot)
        if actor is None:
            return None
        actor.set_actor_scale3d(unreal.Vector(*ue["scale"]))
        actor.set_actor_label(entry["label"])
        actor.set_folder_path("Drakan/%s/%s" % (level, folder))
        actor.tags = [unreal.Name(t) for t in _tags(entry["extras"])]
        return actor

    for e in plan(gltf):
        actor = None
        if e["kind"] in ("terrain", "static"):
            mesh = find(e["mesh"])
            if mesh is None:
                unreal.log_warning("drakan_import: missing static mesh %s" % e["mesh"])
                continue
            actor = place(unreal.StaticMeshActor, e, "Terrain" if e["kind"] == "terrain" else "Objects")
            if actor:
                actor.static_mesh_component.set_static_mesh(mesh)
        elif e["kind"] == "skeletal":
            mesh = find(e["skeletal"])
            actor = place(unreal.SkeletalMeshActor, e, "Characters")
            if actor and mesh:
                comp = actor.skeletal_mesh_component
                try:
                    comp.set_skinned_asset_and_update(mesh)
                except AttributeError:
                    comp.set_skeletal_mesh(mesh)
                names = [c["name"] for c in clips.get(e["skeletal"], [])]
                idle = next((n for n in names if "idle" in n.lower()), names[0] if names else None)
                anim = find(idle) if idle else None
                if anim:
                    comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                    comp.set_animation(anim)
                    comp.play(True)
            elif e["mesh"]:
                pass
        elif e["kind"] == "camera":
            actor = place(unreal.CameraActor, e, "Cameras")
        elif e["kind"] == "light":
            actor = place(unreal.PointLight, e, "Lights")
        else:
            actor = place(unreal.TargetPoint, e, "Markers")
        if actor:
            count += 1
    unreal.log("drakan_import: spawned %d actors from %s" % (count, glb_path))
    return count


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--plan" in sys.argv:
        g = read_glb(args[0])
        p = plan(g)
        kinds = {}
        for e in p:
            kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
        print(
            json.dumps(
                {
                    "entries": len(p),
                    "kinds": kinds,
                    "clips": {k: len(v) for k, v in animations_by_class(g).items()},
                    "first": p[:3],
                },
                indent=1,
            )[:4000]
        )
    else:
        try:
            import unreal  # noqa: F401

            import_level(args[0], args[1] if len(args) > 1 else "/Game/Drakan/Level")
        except ImportError:
            print(__doc__)
