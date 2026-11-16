"""Build an original Drone mesh in Blender, headless, for the Order of the Flame converter.

The 1999 PC model `Drone` (Common/NPC/NPCs, 85 polygons at LOD 0), a flying insect, is replaced at convert time by this
mesh: the converter fits it on the PC skeleton and takes UVs, the 1999 textures and the skin weights from the player's
own disc (editor/packages/formats/src/ootfauthored.ts). This script reads no game data: the proportions below are
rounded measurements of the reference (joint positions, body widths, where the wing corners are), like a modeller
working from a picture. Design: a wasp-like body (a head with compound eyes, mandibles and antennae, a segmented thorax
with six folded legs, a long segmented tail curving down into a stinger bulb and barb) and four double wings.

    blender -b --factory-startup -P analysis/scripts/blender/drone_model.py -- \
        --out build/authored/drone-model [--authored scripts/buildtool/kit/models/drone.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the drone's right, y toward its tail, z up; it faces -Y), in
the 1999 bind pose (wings spread). The `landmarks` are the PC joint positions, so the converter's joint fit is the
identity. Slots: Body (PC entries 0 to 2: body, thorax and head sheets) and Wing (entry 3, the cut-out wing texture).
Each wing keeps the corners of the 1999 wing card it replaces, so the cut-out lands where it did; the new card is
subdivided and cambered.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from goblin_model import (
    ROOT,
    combine,
    cone,
    decimate,
    ellipsoid,
    join,
    make_mats,
    mesh_from_bm,
    remesh,
    remesh_decimate,
    render_views,
    report,
    sculpt,
    set_material,
    tube,
)

V = Vector

# PC joint positions (Blender axes, model units, 4 decimals so the joint fit stays the identity)
JOINTS = {
    "body": (0.0, 0.0, 0.0),
    "tail_one": (0.0, 0.1454, -0.0185),
    "tail_2": (0.0, 0.2605, -0.0442),
    "tail_3": (0.0, 0.3509, -0.0813),
    "stinger": (0.0, 0.4302, -0.1287),
    "Rforewing": (0.0414, -0.0421, 0.0169),
    "Raftwing": (0.0413, 0.0706, 0.0148),
    "jnt12_2": (0.0, -0.1254, -0.0354),
}

# wing cards (right side): root at the body, root toward the head or tail, outer tip, outer leading corner
WING_ROOT = V((0.058, 0.0, 0.025))
WINGS = [
    (V((0.0, -0.086, 0.003)), V((0.412, -0.322, 0.169)), V((0.499, -0.056, 0.218))),
    (V((0.0, -0.086, 0.003)), V((0.277, -0.349, 0.275)), V((0.426, -0.117, 0.339))),
    (V((0.026, 0.144, 0.005)), V((0.354, 0.24, -0.109)), V((0.389, 0.097, -0.088))),
    (V((0.026, 0.144, 0.005)), V((0.337, 0.226, -0.147)), V((0.377, 0.084, -0.123))),
]

BUDGET = {"body": 820, "head": 300, "legs": 160}

# body and tail sections along y: (y, half width, top, bottom)
BODY = [
    (-0.15, 0.02, -0.02, -0.07),
    (-0.125, 0.03, 0.005, -0.075),
    (-0.09, 0.037, 0.015, -0.08),
    (-0.04, 0.04, 0.024, -0.092),
    (0.01, 0.04, 0.025, -0.09),
    (0.06, 0.038, 0.018, -0.072),
    (0.105, 0.03, 0.008, -0.05),
    (0.15, 0.026, -0.002, -0.044),
    (0.2, 0.023, -0.006, -0.046),
    (0.25, 0.02, -0.017, -0.059),
    (0.3, 0.018, -0.032, -0.074),
    (0.35, 0.016, -0.054, -0.096),
    (0.4, 0.015, -0.08, -0.122),
    (0.44, 0.018, -0.12, -0.16),
    (0.48, 0.024, -0.137, -0.193),
    (0.515, 0.019, -0.152, -0.215),
    (0.54, 0.009, -0.17, -0.232),
]
# segment boundaries (y) along the tail and the thorax
RINGS = [-0.11, -0.06, 0.04, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4]


def build_body():
    obj = tube("body", [(0, y, (t + b) / 2, hw, (t - b) / 2) for y, hw, t, b in BODY], sides=18, per=4)
    hump = ellipsoid("scutum", (0, -0.03, 0.012), (0.034, 0.06, 0.02))
    obj = join([obj, hump], "body")
    remesh(obj, 0.0024, smooth=3)

    def carve(p, n):
        off = V()
        for y0 in RINGS:
            off += -n * 0.0032 * math.exp(-(((p.y - y0) / 0.004) ** 2))
        return off

    sculpt(obj, carve)
    body = decimate(obj, BUDGET["body"])
    barb = cone("barb", V((0, 0.535, -0.218)), V((0, 0.566, -0.246)), 0.006, sides=6, rings=2, bend=(0, 0, 0.003))
    spines = [
        cone("spine", V((0, y, z)), V((0, y + 0.012, z + 0.012)), 0.004, sides=4)
        for y, z in ((0.155, -0.004), (0.205, -0.008), (0.255, -0.019), (0.305, -0.034), (0.355, -0.056))
    ]
    return [body, barb, *spines]


def build_head():
    parts = [
        ellipsoid("skull", (0, -0.198, -0.062), (0.026, 0.04, 0.034)),
        ellipsoid("face", (0, -0.232, -0.072), (0.014, 0.014, 0.02)),
        ellipsoid("neck", (0, -0.155, -0.045), (0.018, 0.02, 0.018)),
    ]
    for sx in (1, -1):
        parts.append(ellipsoid("eye", (sx * 0.021, -0.205, -0.052), (0.013, 0.02, 0.019), seg=16))
    head = remesh_decimate(join(parts, "head"), 0.0016, BUDGET["head"], smooth=3)
    extra = []
    for sx in (1, -1):
        extra.append(
            cone(
                "mandible",
                V((sx * 0.01, -0.236, -0.09)),
                V((sx * 0.002, -0.252, -0.106)),
                0.005,
                sides=5,
                rings=2,
                bend=(sx * 0.004, 0, 0),
            )
        )
        extra.append(
            tube(
                "antenna",
                [
                    (sx * 0.008, -0.226, -0.038, 0.0022, 0.0022),
                    (sx * 0.022, -0.26, -0.012, 0.0018, 0.0018),
                    (sx * 0.034, -0.278, 0.006, 0.0013, 0.0013),
                ],
                sides=5,
                per=3,
            )
        )
    return [head, *extra]


def build_legs():
    """Three pairs of thin legs folded under the thorax (the drone flies)."""
    parts = []
    for sx in (1, -1):
        for y0, dy in ((-0.08, -0.03), (-0.03, 0.0), (0.02, 0.04)):
            hip = V((sx * 0.02, y0, -0.075))
            knee = V((sx * 0.05, y0 + dy * 0.5, -0.1))
            foot = V((sx * 0.04, y0 + dy, -0.135))
            parts.append(
                tube(
                    "leg",
                    [(*hip, 0.0045, 0.0045), (*knee, 0.0035, 0.0035), (*foot, 0.0012, 0.0012)],
                    sides=5,
                    per=3,
                )
            )
    return remesh_decimate(join(parts, "legs"), 0.0014, BUDGET["legs"], smooth=1)


def wing(root, inner, tip, lead, sx, nu=5, nv=3, camber=0.012):
    """A cambered card over the quad (root, inner, tip, lead), both sides. u runs root to tip, v leading to trailing."""
    pts = [V((sx * p.x, p.y, p.z)) for p in (root, inner, tip, lead)]
    r, i, t, ld = pts
    normal = (t - r).cross(ld - i).normalized()
    if normal.z < 0:
        normal = -normal
    bm = bmesh.new()
    grid = []
    for a in range(nu + 1):
        u = a / nu
        row = []
        for b in range(nv + 1):
            v = b / nv
            # bilinear over the card: (root..lead) along the leading side, (inner..tip) along the trailing side
            p = (r.lerp(ld, u)).lerp(i.lerp(t, u), v)
            p = p + normal * camber * math.sin(math.pi * v) * math.sin(math.pi * min(1.0, u * 1.3))
            row.append(p)
        grid.append(row)
    for side in (0, 1):
        # the back side sits a hair below the front: the exporter merges coincident vertices
        vs = [[bm.verts.new(p - normal * 0.0005 * side) for p in row] for row in grid]
        for a in range(nu):
            for b in range(nv):
                q = (vs[a][b], vs[a + 1][b], vs[a + 1][b + 1], vs[a][b + 1])
                bm.faces.new(q if side == 0 else tuple(reversed(q)))
    return mesh_from_bm("wing", bm)


def build_wings():
    out = []
    for sx in (1, -1):
        for inner, tip, lead in WINGS:
            out.append(wing(WING_ROOT, inner, tip, lead, sx))
    return out


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats((("Body", (0.5, 0.2, 0.12)), ("Wing", (0.8, 0.55, 0.5))))
    objs = [*build_body(), *build_head(), build_legs()]
    objs = [set_material(o, mats["Body"]) for o in objs]
    objs += [set_material(o, mats["Wing"]) for o in build_wings()]
    return combine(objs, mats, ["Body", "Wing"], "drone")


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/authored/drone-model"))
    ap.add_argument("--authored", help="write the mesh as an authored model for the converter (.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=800)
    return ap.parse_args(argv)


def main():
    args = parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = build()
    report(model, "drone")
    if args.authored:
        from authored_export import export_authored

        export_authored(
            model,
            Path(args.authored).resolve(),
            model="Drone",
            label="Drone",
            fit="joints",
            slots={"Body": {"pcTextures": [0, 1, 2]}, "Wing": {"pcTextures": [3]}},
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(out / "drone.blend"))
    if not args.no_render:
        render_views(
            out,
            [
                ("front", (0, 0.1, 0.0), (0, -1, 0.0001), 1.1),
                ("side", (0, 0.1, -0.05), (1, 0, 0.0001), 1.1),
                ("top", (0, 0.1, 0), (0, 0.0001, 1), 1.1),
                ("tq", (0, 0.1, 0), (0.8, -1, 0.6), 1.1),
                ("head", (0, -0.19, -0.05), (0.6, -1, 0.3), 0.18),
                ("bodyside", (0, 0.15, -0.08), (1, 0, 0.0001), 0.8),
            ],
            res=args.res,
        )


if __name__ == "__main__":
    main()
