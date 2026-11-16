"""Build an original Giant Tick mesh in Blender, headless, for the Order of the Flame converter.

The 1999 PC model `GiantTick` (Common/NPC/NPCs, 231 polygons at LOD 0) is replaced at convert time by this mesh: the
converter fits it on the PC skeleton and takes UVs, the 1999 textures and the skin weights from the player's own disc
(editor/packages/formats/src/ootfauthored.ts). This script reads no game data: the proportions below are rounded
measurements of the reference (joint positions, where the knees and leg tips are), like a modeller working from a
picture. Design: a tall, narrow armoured body of overlapping back plates with a crest of spines, a small head with
eyes and hooked mandibles, six legs each with a thin femur rising to a knee and a long curved spike down to the ground.

    blender -b --factory-startup -P analysis/scripts/blender/tick_model.py -- \
        --out build/authored/tick-model [--authored scripts/buildtool/kit/models/giant_tick.json] [--no-render]

Coordinates: the PC model's own units in Blender axes (x to the tick's right, y toward its tail, z up; it faces -Y), in
the 1999 bind pose. The `landmarks` are the PC joint positions, so the converter's joint fit is the identity.
One slot (Body) projects from the body sheet (PC texture entry 0).
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

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
    "butt2": (0.0, 0.0706, -0.0009),
    "Body2": (0.0, -0.0616, 0.0101),
    "head": (0.0, -0.1241, -0.0028),
    "RMid2": (0.0835, -0.0554, -0.0601),
    "RMidClaw": (0.1271, -0.0591, 0.0055),
    "RFrontClaw": (0.0566, -0.1969, 0.0199),
    "eff30": (0.0966, -0.2884, -0.1058),
    "RRearClaw": (0.0846, 0.0566, 0.0078),
}

# per leg (right side): hip, femur middle, knee (the high point), spike path to the tip on the ground
LEGS = {
    "front": [(0.036, -0.138, -0.04), (0.048, -0.166, -0.004), (0.061, -0.19, 0.02)],
    "mid": [(0.045, -0.057, -0.044), (0.08, -0.058, -0.006), (0.113, -0.06, 0.017)],
    "rear": [(0.045, 0.008, -0.044), (0.066, 0.026, -0.006), (0.086, 0.046, 0.017)],
}
SPIKES = {
    "front": [(0.072, -0.222, -0.006), (0.086, -0.257, -0.045), (0.096, -0.287, -0.107)],
    "mid": [(0.155, -0.063, -0.016), (0.193, -0.066, -0.06), (0.22, -0.068, -0.113)],
    "rear": [(0.108, 0.078, -0.016), (0.128, 0.106, -0.058), (0.143, 0.131, -0.113)],
}

BUDGET = {"body": 690, "head": 220, "femur": 70, "spike": 80}

# body sections along y: (y, half width, top, bottom)
BODY = [
    (-0.128, 0.02, 0.022, -0.028),
    (-0.105, 0.029, 0.05, -0.027),
    (-0.075, 0.034, 0.074, -0.027),
    (-0.035, 0.037, 0.084, -0.03),
    (0.005, 0.036, 0.086, -0.031),
    (0.045, 0.031, 0.07, -0.025),
    (0.077, 0.022, 0.05, -0.03),
    (0.102, 0.011, 0.03, -0.038),
    (0.116, 0.003, 0.01, -0.043),
]
# the back plates: front edge y of each segment
PLATES = [-0.1, -0.072, -0.042, -0.01, 0.022, 0.052, 0.08]


def build_body():
    parts = [
        tube(
            "abdomen",
            [(0, y, (t + b) / 2, hw, (t - b) / 2) for y, hw, t, b in BODY],
            sides=20,
            per=5,
        ),
        # the keel along the top of the back
        tube(
            "keel",
            [
                (0, -0.1, 0.05, 0.008, 0.008),
                (0, -0.06, 0.085, 0.009, 0.01),
                (0, -0.01, 0.094, 0.009, 0.01),
                (0, 0.04, 0.076, 0.008, 0.009),
                (0, 0.09, 0.042, 0.005, 0.006),
            ],
            sides=10,
        ),
        # thorax under the front plates, where the legs join
        ellipsoid("thorax", (0, -0.065, -0.02), (0.033, 0.06, 0.018)),
    ]
    obj = join(parts, "body")
    remesh(obj, 0.0022, smooth=4)

    def carve(p, n):
        # a groove at each plate's front edge; the plate behind rises a little (overlapping armour)
        off = V()
        if p.z > -0.012:
            for y0 in PLATES:
                d = p.y - y0
                off += -n * 0.0028 * math.exp(-((d / 0.0028) ** 2))
                if 0 < d < 0.026:
                    off += n * 0.0012 * math.sin(d / 0.026 * math.pi)
        # belly segments
        elif p.y > -0.03:
            off += -n * 0.001 * max(0.0, math.cos((p.y + 0.03) / 0.018 * math.pi)) ** 4
        return off

    sculpt(obj, carve)
    return decimate(obj, BUDGET["body"])


def build_head():
    parts = [
        ellipsoid("head", (0, -0.145, -0.008), (0.028, 0.026, 0.022)),
        ellipsoid("brow", (0, -0.15, 0.008), (0.022, 0.018, 0.009)),
    ]
    for sx in (1, -1):
        parts += [
            ellipsoid("eye", (sx * 0.015, -0.163, 0.004), (0.0055, 0.0055, 0.005), seg=12),
            ellipsoid("eye2", (sx * 0.022, -0.155, 0.009), (0.004, 0.004, 0.004), seg=10),
            ellipsoid("cheek", (sx * 0.02, -0.158, -0.02), (0.01, 0.012, 0.01)),
        ]
    head = remesh_decimate(join(parts, "head"), 0.0015, BUDGET["head"], smooth=3)
    extra = []
    for sx in (1, -1):
        extra.append(
            cone(
                "mandible",
                V((sx * 0.013, -0.163, -0.024)),
                V((sx * 0.003, -0.192, -0.042)),
                0.0055,
                sides=6,
                rings=2,
                bend=(sx * 0.006, -0.002, 0.002),
            )
        )
        extra.append(
            tube(
                "palp",
                [(sx * 0.022, -0.16, -0.026, 0.003, 0.003), (sx * 0.028, -0.178, -0.036, 0.0025, 0.0025)],
                sides=6,
                per=2,
            )
        )
    return [head, *extra]


def build_leg(name, sx):
    hip, mid, knee = (V((sx * p[0], p[1], p[2])) for p in LEGS[name])
    femur = tube(
        "femur",
        [(*hip, 0.0075, 0.0075), (*mid, 0.0062, 0.0062), (*knee, 0.0065, 0.0065)],
        sides=8,
        per=4,
    )
    joint = ellipsoid("kneecap", knee, (0.011, 0.011, 0.011), seg=12)
    femur = remesh_decimate(
        join([femur, joint, ellipsoid("coxa", hip, (0.011, 0.011, 0.01), seg=12)], "femur"),
        0.0016,
        BUDGET["femur"],
        smooth=2,
    )
    path = [knee] + [V((sx * p[0], p[1], p[2])) for p in SPIKES[name]]
    radii = [0.014, 0.0105, 0.0065, 0.0008]
    spike = tube("spike", [(*p, r, r) for p, r in zip(path, radii)], sides=10, per=5)
    # a barb on the outside of each spike, a third of the way down
    b0 = path[1]
    out = V((sx, 0, 0.35)).normalized()
    barb = cone("barb", b0 + out * 0.004, b0 + out * 0.016 + V((0, 0, 0.006)), 0.004, sides=5)
    spike = remesh_decimate(join([spike, barb], "spike"), 0.0014, BUDGET["spike"], smooth=1)
    return [femur, spike]


def build_spines():
    """Short spines rising from the keel, one per plate."""
    out = []
    for k, y0 in enumerate(PLATES[:-1]):
        y = y0 + 0.014
        # keel height there (rounded)
        z = 0.05 + 0.044 * math.exp(-(((y + 0.01) / 0.075) ** 2))
        ln = 0.016 + 0.008 * math.exp(-(((y + 0.02) / 0.05) ** 2))
        out.append(cone("spine", V((0, y, z - 0.003)), V((0, y + 0.012, z + ln)), 0.005, sides=5, bend=(0, -0.002, 0)))
        if 1 <= k <= 4:
            for sx in (1, -1):
                b = V((sx * 0.026, y, z - 0.024))
                out.append(cone("side", b, b + V((sx * 0.012, 0.008, 0.006)), 0.0035, sides=4))
    return out


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = make_mats((("Body", (0.45, 0.25, 0.12)),))
    objs = [build_body(), *build_head(), *build_spines()]
    for sx in (1, -1):
        for leg in LEGS:
            objs += build_leg(leg, sx)
    objs = [set_material(o, mats["Body"]) for o in objs]
    return combine(objs, mats, ["Body"], "giant_tick")


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/authored/tick-model"))
    ap.add_argument("--authored", help="write the mesh as an authored model for the converter (.json)")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--res", type=int, default=800)
    return ap.parse_args(argv)


def main():
    args = parse_args()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = build()
    report(model, "giant tick")
    if args.authored:
        from authored_export import export_authored

        export_authored(
            model,
            Path(args.authored).resolve(),
            model="GiantTick",
            label="Giant Tick",
            fit="joints",
            # entry 1 is a small texture from another container on a few faces under the head; keep to the body sheet
            slots={"Body": {"pcTextures": [0]}},
            extra={"landmarks": {n: list(p) for n, p in JOINTS.items()}},
        )
    bpy.ops.wm.save_as_mainfile(filepath=str(out / "giant_tick.blend"))
    if not args.no_render:
        render_views(
            out,
            [
                ("front", (0, -0.08, -0.01), (0, -1, 0.0001), 0.5),
                ("side", (0, -0.08, -0.01), (1, 0, 0.0001), 0.5),
                ("top", (0, -0.08, 0), (0, 0.0001, 1), 0.5),
                ("tq", (0, -0.08, -0.01), (0.8, -1, 0.6), 0.5),
                ("head", (0, -0.15, -0.01), (0.6, -1, 0.3), 0.12),
                ("under", (0, -0.08, 0), (0.2, 0.3, -1), 0.5),
            ],
            res=args.res,
        )


if __name__ == "__main__":
    main()
