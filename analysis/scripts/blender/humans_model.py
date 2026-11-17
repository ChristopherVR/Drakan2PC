"""New meshes for the Order of the Flame male humans (Julian, the farmer, Rimril, villagers, ...), headless Blender.

    blender -b --factory-startup -P analysis/scripts/blender/humans_model.py -- --which julian|farmer|...|all \
        [--models scripts/buildtool/kit/models] [--render build/authored/humans]

Each character is described with `human_kit.py`: tubes at rounded proportions copied by hand from the 1999 model (so the
1999 texture, projected at convert time, lands on matching geometry), real hands, shoes, and clothing edges at the
heights the 1999 texture paints them. The 1999 head is kept (`keepPcJoints: ["head"]`). Nothing here reads game data.
Coordinates: metres, Z up, facing -Y, R on +X, the 1999 model scaled to 1.72 m with its feet on the floor.
"""

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import human_kit as hk


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--which", default="all")
    ap.add_argument("--models", default="scripts/buildtool/kit/models")
    ap.add_argument("--render", help="clay renders of the game mesh to <dir>/<name>_<view>.png")
    return ap.parse_args(argv)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def parts(prefix):
    return [o for o in bpy.context.scene.objects if o.name.startswith(prefix) and o.type == "MESH"]


# ---------------------------------------------------------------------------- Julian (julian (h))
# Tall and thin, a long two-coloured robe to the shoe tops, long sleeves widening at the wrist, soft shoes.

JULIAN_ROBE = [  # x, y, z, half width, half depth (the robe hides the legs, as the 1999 one does)
    (0.006, 0.060, 0.135, 0.132, 0.066),
    (0.006, 0.058, 0.20, 0.133, 0.064),
    (0.006, 0.048, 0.30, 0.137, 0.060),
    (0.007, 0.028, 0.40, 0.142, 0.057),
    (0.008, 0.005, 0.50, 0.146, 0.056),
    (0.008, -0.004, 0.60, 0.149, 0.058),
    (0.008, 0.000, 0.70, 0.150, 0.062),
    (0.008, 0.012, 0.80, 0.151, 0.068),
    (0.008, 0.018, 0.90, 0.152, 0.070),
    (0.004, 0.022, 1.00, 0.147, 0.077),
    (0.001, 0.020, 1.10, 0.135, 0.079),
    (0.002, 0.010, 1.20, 0.116, 0.073),
    (0.002, 0.011, 1.27, 0.112, 0.077),
    (0.006, 0.013, 1.34, 0.124, 0.083),
    (0.008, 0.017, 1.40, 0.136, 0.082),
    (0.005, 0.020, 1.435, 0.118, 0.072),
    (0.003, 0.018, 1.47, 0.098, 0.068),
    (0.0, 0.014, 1.485, 0.074, 0.064),
]
JULIAN_ARM = [  # right arm, shoulder to the wide sleeve end (the left one is the mirror)
    (0.120, 0.030, 1.465, 0.060, 0.050),
    (0.172, 0.028, 1.482, 0.049, 0.036),
    (0.250, 0.027, 1.482, 0.043, 0.031),
    (0.330, 0.024, 1.481, 0.037, 0.027),
    (0.410, 0.020, 1.481, 0.033, 0.026),
    (0.470, 0.012, 1.481, 0.039, 0.032),
    (0.530, 0.004, 1.477, 0.043, 0.030),
    (0.600, -0.004, 1.474, 0.045, 0.027),
    (0.665, -0.012, 1.474, 0.047, 0.026),
]
JULIAN_SHOE = [  # right shoe, heel to toe
    (0.104, 0.050, 0.050, 0.044, 0.042),
    (0.105, 0.025, 0.046, 0.047, 0.044),
    (0.107, -0.005, 0.037, 0.040, 0.046),
    (0.108, -0.035, 0.029, 0.030, 0.046),
    (0.106, -0.065, 0.021, 0.021, 0.042),
    (0.105, -0.095, 0.016, 0.015, 0.032),
    (0.102, -0.118, 0.013, 0.010, 0.014),
]
JULIAN_JOINTS = {  # 1999 joint bind positions in this space (rounded)
    "upperbody": (0.0, 0.0, 1.016),
    "chest": (0.0, 0.039, 1.317),
    "head": (0.0, 0.017, 1.579),
    "Ruparm": (0.153, 0.047, 1.467),
    "Rforearm": (0.441, 0.029, 1.474),
    "Rhand": (0.764, -0.004, 1.483),
    "Luparm": (-0.153, 0.044, 1.457),
    "Lforearm": (-0.455, 0.014, 1.472),
    "Lhand": (-0.747, -0.040, 1.492),
    "Rthigh": (0.104, 0.0, 1.016),
    "Rcalf": (0.090, -0.021, 0.569),
    "Rfoot": (0.094, 0.042, 0.076),
    "Lthigh": (-0.106, 0.0, 1.016),
    "Lcalf": (-0.101, -0.019, 0.569),
    "Lfoot": (-0.103, 0.048, 0.075),
}


def build_julian():
    b = hk.Body("Julian")

    def robe_fold(a, t):
        # soft vertical folds, deeper towards the hem; below the hips the cloth dips in between the legs at the
        # front and the back, as the 1999 robe does (its middle panel sits behind the leg fronts)
        k = max(0.0, 1 - 1.3 * t) ** 1.5
        folds = k * (0.006 * math.sin(6 * a + 0.4) + 0.003 * math.sin(11 * a + 1.7))
        legs = max(0.0, min(1.0, (0.62 - t) / 0.15))
        dip = math.exp(-(((a - 1.5 * math.pi) / 0.42) ** 2)) + 0.6 * math.exp(-(((a - 0.5 * math.pi) / 0.42) ** 2))
        return folds - 0.026 * legs * dip

    # the robe is one regular tube (not remeshed) from the hem to the collar, with a vertex column down the middle of
    # the front, where the 1999 texture changes from the tan half to the green half: no triangle straddles that seam
    hk.tube_mesh("Julian_Robe", JULIAN_ROBE, power=2.3, nseg=32, fold=robe_fold, mat="robe")
    for sx in (1, -1):
        arm = JULIAN_ARM if sx > 0 else hk.mirror_x(JULIAN_ARM)
        b.tube(arm, "robe", power=2.1)
        b.blob((0.135 * sx, 0.028, 1.452), (0.065, 0.058, 0.055), "robe")
        shoe = JULIAN_SHOE if sx > 0 else hk.mirror_x(JULIAN_SHOE)
        b.tube(shoe, "shoe", power=2.6)
        b.tube([(0.103 * sx, 0.038, 0.045, 0.040, 0.042), (0.102 * sx, 0.036, 0.16, 0.036, 0.038)], "shoe")

    def tag(c, m):
        if m == "robe" and abs(c.x) < 0.11 and c.z > 1.43:
            return "skin"
        return m

    body = b.build(voxel=0.004, smooth=6, tag=tag)
    # robe edges: a hem lip at the shoe tops, a collar at the neck, a lip at each sleeve end
    hk.ring_band("Julian_Hem", body, (0.006, 0.04, 0), (0, 0, 1), [(0.137, 0.002), (0.155, 0.002)], 64, "robe", 0.003)
    # the neck reaches into the kept 1999 head, so no gap shows where the converter drops head triangles. The robe
    # stops at the base of the neck (1.485 m), as the 1999 one does: a collar under the chin swallowed the chin and
    # the jaw when his head drops forward in the Ruined Village scene where he is pinned by the spear.
    hk.tube_mesh(
        "Julian_Neck",
        [
            (0, 0.012, 1.46, 0.050, 0.056),
            (0, 0.008, 1.50, 0.045, 0.053),
            (0, 0.004, 1.535, 0.042, 0.050),
            (0, 0.000, 1.552, 0.036, 0.042),
        ],
        nseg=16,
        mat="neck",
    )
    hk.torus("Julian_Collar", (0, 0.013, 1.482), 0.074, 0.064, 0.007, "robe", tilt=0.006)
    for sx in (1, -1):
        hk.hand(
            f"Julian_Hand{sx}",
            (0.66 * sx, -0.016, 1.474),
            (sx, -0.08, 0.0),
            (0, -1, 0),
            (0, 0, -1),
            size=1.3,
            curl=0.25,
        )
    budgets = {
        "Julian_Body": 700,
        "Julian_Robe": 1300,
        "Julian_Neck": 96,
        "Julian_Hand": 150,
        "Julian_Hem": 64,
        "Julian_Collar": 64,
    }
    return hk.build_lod(parts("Julian_"), budgets, "Julian_Game")


# ---------------------------------------------------------------------------- the farmer (farmer)
# A stocky, bearded man bound sitting on a cart seat: thighs forward, shins down, fists forward on the reins, the body
# turned a little to his right. Laced shirt tucked in at the waist, baggy trousers, soft boots to below the knee.

FARMER_TORSO = [  # pelvis to neck, leaning forward
    (0.009, -0.092, 0.926, 0.196, 0.180),
    (0.005, -0.126, 0.966, 0.198, 0.186),
    (0.002, -0.140, 1.013, 0.200, 0.180),
    (0.000, -0.148, 1.059, 0.201, 0.178),
    (-0.003, -0.157, 1.107, 0.201, 0.175),
    (-0.006, -0.166, 1.156, 0.202, 0.170),
    (-0.018, -0.170, 1.206, 0.208, 0.166),
    (-0.030, -0.172, 1.260, 0.212, 0.164),
    (-0.032, -0.178, 1.320, 0.205, 0.158),
    (-0.032, -0.184, 1.375, 0.180, 0.140),
    (-0.035, -0.195, 1.420, 0.120, 0.110),
    (-0.042, -0.210, 1.455, 0.075, 0.085),
]
FARMER_ARMS = {
    "R": [
        (0.200, -0.105, 1.395, 0.062, 0.088),
        (0.239, -0.089, 1.364, 0.059, 0.085),
        (0.268, -0.082, 1.336, 0.058, 0.082),
        (0.294, -0.067, 1.309, 0.056, 0.074),
        (0.321, -0.058, 1.281, 0.055, 0.066),
        (0.360, -0.060, 1.232, 0.060, 0.066),
        (0.389, -0.092, 1.204, 0.060, 0.065),
        (0.387, -0.165, 1.169, 0.055, 0.066),
        (0.387, -0.238, 1.133, 0.049, 0.065),
        (0.387, -0.311, 1.098, 0.043, 0.062),
        (0.385, -0.345, 1.090, 0.042, 0.058),
    ],
    "L": [
        (-0.240, -0.255, 1.330, 0.088, 0.078),
        (-0.271, -0.273, 1.292, 0.086, 0.077),
        (-0.311, -0.300, 1.225, 0.077, 0.071),
        (-0.357, -0.322, 1.162, 0.060, 0.062),
        (-0.355, -0.360, 1.120, 0.062, 0.064),
        (-0.306, -0.408, 1.103, 0.067, 0.069),
        (-0.243, -0.459, 1.103, 0.061, 0.065),
        (-0.180, -0.509, 1.102, 0.055, 0.061),
        (-0.117, -0.560, 1.102, 0.049, 0.057),
        (-0.085, -0.575, 1.100, 0.046, 0.055),
    ],
}
FARMER_LEGS = {  # hip to knee (forward), knee to ankle (down), then the boot foot
    "R": [
        [
            (0.108, -0.120, 0.782, 0.140, 0.109),
            (0.106, -0.201, 0.772, 0.137, 0.105),
            (0.102, -0.302, 0.766, 0.120, 0.097),
            (0.097, -0.403, 0.760, 0.103, 0.090),
            (0.094, -0.470, 0.745, 0.096, 0.088),
        ],
        [
            (0.088, -0.515, 0.690, 0.090, 0.088),
            (0.086, -0.505, 0.624, 0.088, 0.087),
            (0.083, -0.465, 0.537, 0.090, 0.088),
            (0.083, -0.425, 0.444, 0.082, 0.083),
            (0.083, -0.384, 0.352, 0.074, 0.075),
            (0.083, -0.344, 0.259, 0.066, 0.068),
            (0.083, -0.318, 0.170, 0.062, 0.064),
            (0.086, -0.300, 0.110, 0.060, 0.066),
        ],
        [
            (0.088, -0.255, 0.062, 0.055, 0.058),
            (0.091, -0.360, 0.070, 0.068, 0.072),
            (0.090, -0.430, 0.060, 0.055, 0.074),
            (0.094, -0.470, 0.042, 0.037, 0.064),
            (0.088, -0.510, 0.027, 0.024, 0.038),
        ],
    ],
    "L": [
        [
            (-0.115, -0.110, 0.775, 0.134, 0.106),
            (-0.172, -0.186, 0.757, 0.126, 0.103),
            (-0.206, -0.280, 0.742, 0.110, 0.099),
            (-0.237, -0.377, 0.740, 0.102, 0.092),
            (-0.262, -0.450, 0.732, 0.098, 0.090),
        ],
        [
            (-0.285, -0.515, 0.670, 0.096, 0.090),
            (-0.287, -0.507, 0.548, 0.094, 0.093),
            (-0.282, -0.495, 0.447, 0.083, 0.090),
            (-0.278, -0.486, 0.346, 0.072, 0.083),
            (-0.274, -0.477, 0.245, 0.060, 0.075),
            (-0.272, -0.472, 0.170, 0.056, 0.072),
            (-0.290, -0.480, 0.110, 0.062, 0.078),
        ],
        [
            (-0.300, -0.455, 0.070, 0.066, 0.068),
            (-0.330, -0.541, 0.074, 0.072, 0.072),
            (-0.345, -0.579, 0.068, 0.062, 0.075),
            (-0.360, -0.618, 0.058, 0.049, 0.072),
            (-0.375, -0.655, 0.048, 0.037, 0.064),
            (-0.382, -0.698, 0.040, 0.025, 0.038),
        ],
    ],
}
FARMER_JOINTS = {
    "tailbone": (0.0, 0.004, 0.797),
    "midspine": (-0.013, -0.047, 0.978),
    "upperback": (-0.026, -0.115, 1.195),
    "head": (-0.038, -0.208, 1.493),
    "Rbicep": (0.204, -0.104, 1.435),
    "Rforearm": (0.376, -0.048, 1.244),
    "Rhand": (0.406, -0.348, 1.101),
    "Lbicep": (-0.264, -0.292, 1.343),
    "Lforearm": (-0.379, -0.362, 1.124),
    "Lwrist": (-0.085, -0.611, 1.106),
    "Rthigh": (0.141, -0.002, 0.771),
    "Rcalf": (0.121, -0.526, 0.733),
    "Rfoot": (0.088, -0.316, 0.197),
    "Lthigh": (-0.137, -0.002, 0.771),
    "Lcalf": (-0.314, -0.503, 0.722),
    "Lfoot": (-0.296, -0.474, 0.156),
}


def build_farmer():
    b = hk.Body("Farmer")
    b.tube(FARMER_TORSO, "shirt", power=2.4, nseg=40)
    b.blob((0.0, -0.040, 0.835), (0.195, 0.150, 0.110), "trousers")  # seat
    for s in "RL":
        b.tube(FARMER_ARMS[s], "shirt", power=2.1)
        sh = (0.175, -0.11, 1.395) if s == "R" else (-0.175, -0.22, 1.36)
        b.blob(sh, (0.075, 0.08, 0.07), "shirt")
        thigh, shin, foot = FARMER_LEGS[s]
        b.tube(thigh, "trousers", power=2.2)
        b.tube(shin, "trousers", power=2.2)
        b.blob(Vector(thigh[-1][:3]).lerp(Vector(shin[0][:3]), 0.5), (0.095, 0.092, 0.095), "trousers")  # knee
        b.tube(foot, "leather", power=2.6)

    def tag(c, m):
        if m == "trousers" and 0.08 < c.z < 0.54:
            return "leather"
        if m in ("leather", "trousers") and c.z <= 0.08:
            return "shoe"
        if m == "shirt" and c.z < 0.92:
            return "trousers"
        return m

    body = b.build(voxel=0.0045, smooth=6, tag=tag)
    # waistband where the 1999 shirt tucks into the trousers, turned-down boot tops, shirt cuffs
    hk.ring_band("Farmer_Belt", body, (0.0, -0.10, 0), (0, 0, 1), [(0.94, 0.003), (0.965, 0.003)], 64, "belt", 0.004)
    for s, knee, axis, h in (
        ("R", (0.086, -0.505, 0.624), (0.0, 0.425, -0.905), 0.093),
        ("L", (-0.287, -0.507, 0.597), (0.04, 0.118, -0.992), 0.058),
    ):
        rows = [(h - 0.012, 0.002), (h + 0.004, 0.007), (h + 0.02, 0.005)]
        hk.ring_band(f"Farmer_BootTop{s}", body, knee, axis, rows, 40, "leather", 0.004, maxr=0.13)
    for s in "RL":
        arm = FARMER_ARMS[s]
        ax = (Vector(arm[-1][:3]) - Vector(arm[-3][:3])).normalized()
        rows = [(-0.03, 0.003), (-0.008, 0.005)]
        hk.ring_band(f"Farmer_Cuff{s}", body, arm[-1][:3], ax, rows, 32, "shirt", 0.003, maxr=0.09)
    # fists on the reins
    hk.hand(
        "Farmer_HandR", (0.385, -0.352, 1.088), (0.0, -1.0, 0.05), (-0.3, 0.0, -0.95), (-0.95, 0, 0.3), 1.15, fist=True
    )
    hk.hand(
        "Farmer_HandL", (-0.082, -0.578, 1.100), (1.0, -0.35, 0.0), (0.33, 0.94, 0.0), (0.0, 0.0, -1.0), 1.15, fist=True
    )
    budgets = {
        "Farmer_Body": 1800,
        "Farmer_Hand": 140,
        "Farmer_Belt": 64,
        "Farmer_BootTop": 48,
        "Farmer_Cuff": 32,
    }
    return hk.build_lod(parts("Farmer_"), budgets, "Farmer_Game")


# ---------------------------------------------------------------------------- villagers and mine slaves
# mvillager, mvillage2, slaveM (h), slaveM 02 (h) and slaveM 03 (h) share one 1999 mesh (their skins and rigs differ):
# a slim young man in a V-neck tunic to the upper thigh with a belt slung lower at the front, dark leather bracers on
# the forearms, close trousers and laced ankle boots.

VILLAGER_TORSO = [  # hips (under the tunic hem) to the neck
    (0.004, -0.012, 0.865, 0.151, 0.099),
    (0.003, -0.018, 0.920, 0.150, 0.099),
    (0.001, -0.014, 0.975, 0.139, 0.100),
    (0.002, -0.006, 1.028, 0.124, 0.100),
    (0.003, -0.008, 1.075, 0.117, 0.095),
    (0.003, -0.016, 1.115, 0.115, 0.087),
    (0.004, -0.022, 1.165, 0.121, 0.082),
    (0.004, -0.022, 1.235, 0.132, 0.086),
    (0.005, -0.018, 1.280, 0.138, 0.092),
    (0.003, -0.012, 1.325, 0.150, 0.098),
    (0.001, -0.002, 1.370, 0.150, 0.094),
    (0.000, 0.004, 1.415, 0.118, 0.080),
    (0.000, 0.006, 1.442, 0.090, 0.070),
    (0.000, 0.008, 1.462, 0.068, 0.062),
]
VILLAGER_ARM = [  # right shoulder to wrist (the left is the mirror)
    (0.130, 0.020, 1.405, 0.068, 0.062),
    (0.170, 0.019, 1.413, 0.058, 0.059),
    (0.250, 0.015, 1.422, 0.047, 0.055),
    (0.330, 0.018, 1.427, 0.042, 0.046),
    (0.370, 0.023, 1.429, 0.039, 0.040),
    (0.420, 0.024, 1.429, 0.044, 0.045),
    (0.470, 0.020, 1.430, 0.043, 0.045),
    (0.525, 0.018, 1.432, 0.035, 0.040),
    (0.580, 0.015, 1.434, 0.029, 0.036),
    (0.640, 0.011, 1.435, 0.023, 0.032),
]
VILLAGER_LEG = [  # right hip to ankle
    (0.077, -0.012, 0.900, 0.072, 0.092),
    (0.076, -0.008, 0.800, 0.070, 0.091),
    (0.078, -0.006, 0.699, 0.063, 0.081),
    (0.078, -0.004, 0.599, 0.053, 0.069),
    (0.078, 0.003, 0.525, 0.052, 0.064),
    (0.081, 0.028, 0.452, 0.064, 0.071),
    (0.078, 0.031, 0.402, 0.067, 0.067),
    (0.077, 0.033, 0.330, 0.062, 0.061),
    (0.076, 0.036, 0.251, 0.052, 0.052),
    (0.075, 0.038, 0.180, 0.045, 0.045),
    (0.075, 0.035, 0.120, 0.040, 0.046),
]
VILLAGER_FOOT = [  # right boot, heel to toe
    (0.075, 0.030, 0.062, 0.058, 0.046),
    (0.075, 0.006, 0.059, 0.058, 0.050),
    (0.076, -0.023, 0.050, 0.049, 0.052),
    (0.078, -0.052, 0.040, 0.040, 0.053),
    (0.076, -0.081, 0.031, 0.030, 0.051),
    (0.075, -0.111, 0.023, 0.022, 0.041),
    (0.074, -0.140, 0.017, 0.013, 0.018),
]


def villager_joints(slave):
    """1999 joint bind positions of the villager rig or the (slightly different) slave rig, in this space."""
    if slave:
        j = {
            "upperbody": (0.0, 0.0, 1.016),
            "chest": (0.0, 0.031, 1.293),
            "head": (0.0, 0.015, 1.537),
            "Ruparm": (0.156, 0.038, 1.417),
            "Rforearm": (0.390, 0.038, 1.424),
            "Rhand": (0.681, 0.030, 1.432),
            "Luparm": (-0.156, 0.038, 1.417),
            "Lforearm": (-0.409, 0.037, 1.426),
            "Lhand": (-0.683, 0.029, 1.435),
        }
        legs = {"thigh": (0.080, 0.0, 1.016), "calf": (0.071, -0.008, 0.529), "foot": (0.076, 0.048, 0.100)}
    else:
        j = {
            "upperbody": (0.0, 0.0, 1.016),
            "chest": (0.0, 0.026, 1.236),
            "head": (0.001, 0.036, 1.507),
            "Ruparm": (0.135, 0.021, 1.410),
            "Rforearm": (0.389, 0.022, 1.421),
            "Luparm": (-0.135, 0.021, 1.410),
            "Lforearm": (-0.410, 0.020, 1.424),
        }
        legs = {"thigh": (0.064, 0.0, 1.016), "calf": (0.058, -0.007, 0.539), "foot": (0.058, 0.042, 0.077)}
    for name, p in legs.items():
        j["R" + name] = p
        j["L" + name] = (-p[0], p[1], p[2])
    return j


def build_villager():
    b = hk.Body("Villager")
    b.tube(VILLAGER_TORSO, "shirt", power=2.3, nseg=40)
    for sx in (1, -1):
        m = (lambda s: s) if sx > 0 else hk.mirror_x
        b.tube(m(VILLAGER_ARM), "shirt", power=2.0)
        b.blob((0.135 * sx, 0.015, 1.40), (0.062, 0.058, 0.060), "shirt")
        b.tube(m(VILLAGER_LEG), "trousers", power=2.1)
        b.tube(m(VILLAGER_FOOT), "shoe", power=2.6)

    def tag(c, m):
        if m == "shirt" and abs(c.x) > 0.47:
            return "leather"  # bracers
        if m == "trousers" and c.z < 0.235:
            return "shoe"
        return m

    body = b.build(voxel=0.004, smooth=6, tag=tag)
    # the neck reaches into the kept 1999 head (its lower edge is at 1.46 m)
    neck = [(0, 0.008, 1.43, 0.060, 0.058), (0, 0.008, 1.47, 0.056, 0.056), (0, 0.010, 1.51, 0.050, 0.052)]
    # its own slot, textured only from the 1999 chest page (`VILLAGER_NECK`)
    hk.tube_mesh("Villager_Neck", neck, nseg=16, mat="neck")
    # belt, slung lower at the front, with a buckle
    axis = Vector((0.0, -0.35, 1.0)).normalized()
    hk.ring_band("Villager_Belt", body, (0, -0.005, 1.078), axis, [(-0.016, 0.003), (0.016, 0.003)], 64, "belt", 0.004)
    fy = hk.front_y(body, 0.0, 1.045)
    hk.box("Villager_Buckle", (0.0, fy - 0.008, 1.045), (0.036, 0.006, 0.030), "metal", rot=(-0.33, 0, 0))
    for sx in (1, -1):
        # bracer edges on the forearm, boot tops above the ankle laces
        for x0, x1 in ((0.465, 0.48), (0.615, 0.635)):
            rows = [(x0, 0.002), (x1, 0.004)]
            hk.ring_band(
                f"Villager_Bracer{sx}", body, (0, 0.015, 1.432), (sx, 0, 0), rows, 28, "leather", 0.003, maxr=0.07
            )
        rows = [(0.215, 0.001), (0.232, 0.004), (0.245, 0.002)]
        hk.ring_band(f"Villager_BootTop{sx}", body, (0.078 * sx, 0.03, 0), (0, 0, 1), rows, 36, "shoe", 0.003, maxr=0.1)
        hk.hand(f"Villager_Hand{sx}", (0.635 * sx, 0.010, 1.436), (sx, 0.0, 0.0), (0, -1, 0), (0, 0, -1), 1.12, 0.3)
    budgets = {
        "Villager_Body": 1650,
        "Villager_Hand": 140,
        "Villager_Neck": 96,
        "Villager_Belt": 64,
        "Villager_Buckle": 12,
        "Villager_Bracer": 24,
        "Villager_BootTop": 40,
    }
    return hk.build_lod(parts("Villager_"), budgets, "Villager_Game")


# ---------------------------------------------------------------------------- Rimril and the wounded wizard
# Rimril and wounded wizard share one 1999 mesh and rig: a hooded mage, bare tattooed shoulders and arms under a
# sleeveless tabard that ends in flaps at the front and back, gauntlets, dark trousers, knee-high greaves, two staffs
# slung across his back (kept from the 1999 model, like the pointed hood top).

RIMRIL_TORSO = [  # hips to the hood collar
    (0.001, -0.034, 0.845, 0.168, 0.100),
    (0.001, -0.026, 0.886, 0.162, 0.106),
    (0.000, -0.015, 0.927, 0.154, 0.112),
    (0.001, -0.013, 0.968, 0.144, 0.110),
    (0.001, -0.014, 1.009, 0.133, 0.102),
    (0.001, -0.015, 1.050, 0.121, 0.094),
    (0.003, -0.017, 1.091, 0.110, 0.087),
    (0.004, -0.027, 1.132, 0.112, 0.087),
    (0.005, -0.030, 1.173, 0.119, 0.092),
    (0.006, -0.032, 1.214, 0.127, 0.100),
    (0.007, -0.033, 1.255, 0.136, 0.108),
    (0.008, -0.034, 1.295, 0.147, 0.116),
    (0.007, -0.032, 1.336, 0.170, 0.121),
    (0.002, -0.028, 1.377, 0.190, 0.126),
    (0.001, -0.015, 1.430, 0.170, 0.128),
    (0.001, -0.005, 1.480, 0.150, 0.124),
    (0.001, 0.005, 1.525, 0.115, 0.100),
    (0.001, 0.020, 1.575, 0.085, 0.085),
]
RIMRIL_ARM = [  # right shoulder to the end of the gauntlet (the left is the mirror)
    (0.170, 0.008, 1.432, 0.064, 0.064),
    (0.200, 0.008, 1.434, 0.056, 0.060),
    (0.277, 0.011, 1.436, 0.047, 0.058),
    (0.355, 0.017, 1.438, 0.043, 0.048),
    (0.400, 0.021, 1.439, 0.042, 0.041),
    (0.438, 0.020, 1.441, 0.050, 0.047),
    (0.475, 0.018, 1.444, 0.056, 0.051),
    (0.550, 0.015, 1.453, 0.052, 0.044),
    (0.628, 0.012, 1.462, 0.047, 0.037),
    (0.685, 0.010, 1.467, 0.044, 0.033),
]
RIMRIL_LEG = [  # right hip to ankle, the legs apart
    (0.090, -0.030, 0.860, 0.086, 0.094),
    (0.110, -0.038, 0.753, 0.079, 0.087),
    (0.130, -0.044, 0.656, 0.067, 0.076),
    (0.140, -0.050, 0.600, 0.062, 0.074),
    (0.149, -0.062, 0.554, 0.068, 0.086),
    (0.162, -0.045, 0.490, 0.071, 0.091),
    (0.174, -0.032, 0.411, 0.068, 0.085),
    (0.188, -0.024, 0.314, 0.057, 0.069),
    (0.202, -0.016, 0.216, 0.046, 0.052),
    (0.212, -0.020, 0.140, 0.040, 0.048),
]
RIMRIL_FOOT = [  # right greave foot, heel to toe
    (0.226, 0.010, 0.066, 0.056, 0.052),
    (0.228, -0.033, 0.063, 0.054, 0.061),
    (0.233, -0.062, 0.053, 0.044, 0.063),
    (0.235, -0.090, 0.043, 0.034, 0.062),
    (0.235, -0.119, 0.034, 0.025, 0.058),
    (0.235, -0.150, 0.028, 0.016, 0.034),
]
RIMRIL_JOINTS = {
    "lowerback": (0.0, -0.001, 0.946),
    "midback": (0.0, 0.011, 1.122),
    "upperback": (0.001, 0.014, 1.301),
    "RArm": (0.205, 0.002, 1.444),
    "RForearm": (0.435, 0.019, 1.443),
    "RHand": (0.676, 0.0, 1.443),
    "LArm": (-0.200, -0.002, 1.446),
    "LForearm": (-0.435, 0.013, 1.447),
    "LHand": (-0.715, 0.0, 1.447),
    "RThigh": (0.084, 0.0, 0.927),
    "RCalf": (0.145, -0.039, 0.558),
    "RFoot": (0.218, 0.017, 0.119),
    "LThigh": (-0.090, 0.0, 0.928),
    "LCalf": (-0.147, -0.070, 0.559),
    "LFoot": (-0.215, -0.045, 0.117),
}


def build_rimril():
    b = hk.Body("Rimril")
    b.tube(RIMRIL_TORSO, "shirt", power=2.3, nseg=40)
    for sx in (1, -1):
        m = (lambda s: s) if sx > 0 else hk.mirror_x
        b.tube(m(RIMRIL_ARM), "skin", power=2.0)
        b.blob((0.165 * sx, 0.0, 1.425), (0.07, 0.066, 0.068), "skin")
        leg = m(RIMRIL_LEG)
        if sx < 0:  # his left leg stands a little further forward
            leg = [(x, y - 0.03 * min(1.0, (0.86 - z) / 0.3), z, ru, rw) for x, y, z, ru, rw in leg]
        b.tube(leg, "trousers", power=2.1)
        foot = m(RIMRIL_FOOT)
        if sx < 0:
            foot = [(x - 0.006, y - 0.062, z, ru, rw) for x, y, z, ru, rw in foot]
        b.tube(foot, "metal", power=2.6)

    def tag(c, m):
        if m == "skin" and abs(c.x) > 0.43:
            return "metal"  # gauntlets
        if m == "trousers" and c.z < 0.60:
            return "metal"  # greaves
        return m

    body = b.build(voxel=0.004, smooth=6, tag=tag)
    # tabard flaps hanging in front and behind, from the waist to above the knees
    for name, y, sy in (("Front", -0.128, -1), ("Back", 0.104, 1)):
        rows = []
        for i in range(7):
            z = 0.95 - 0.05 * i
            rows.append(
                [(x, y + sy * (0.012 * (1 - (x / 0.11) ** 2) + 0.004 * i), z) for x in (-0.11, -0.055, 0, 0.055, 0.11)]
            )
        vs = [p for r in rows for p in r]
        faces = [(i * 5 + k, i * 5 + k + 1, (i + 1) * 5 + k + 1, (i + 1) * 5 + k) for i in range(6) for k in range(4)]
        flap = hk.mesh_obj(f"Rimril_Flap{name}", vs, faces)
        hk.add_mat(flap, "robe")
        so = flap.modifiers.new("solid", "SOLIDIFY")
        so.thickness = 0.008
        ss = flap.modifiers.new("sub", "SUBSURF")
        ss.levels = 1
        hk.apply_modifiers(flap)
        hk.shade_smooth(flap)
    for sx in (1, -1):
        # greave tops below the knee, the gauntlet flare at the elbow
        rows = [(0.585, 0.001), (0.600, 0.005), (0.612, 0.003)]
        hk.ring_band(f"Rimril_Greave{sx}", body, (0.135 * sx, -0.05, 0), (0, 0, 1), rows, 36, "metal", 0.003, maxr=0.1)
        rows = [(0.43, 0.002), (0.445, 0.008), (0.46, 0.004)]
        hk.ring_band(f"Rimril_Gauntlet{sx}", body, (0, 0.018, 1.442), (sx, 0, 0), rows, 28, "metal", 0.003, maxr=0.08)
        hk.hand(f"Rimril_Hand{sx}", (0.685 * sx, 0.004, 1.455), (sx, -0.05, 0.0), (0, -1, 0), (0, 0, -1), 1.1, 0.35)
    budgets = {
        "Rimril_Body": 1750,
        "Rimril_Hand": 140,
        "Rimril_Flap": 80,
        "Rimril_Greave": 40,
        "Rimril_Gauntlet": 32,
    }
    return hk.build_lod(parts("Rimril_"), budgets, "Rimril_Game")


# ---------------------------------------------------------------------------- the wounded warrior (warrior2)
# A dying soldier sitting on the ground: legs out in front (the left knee up), hunched, head bowed, his right hand
# pressed to the wound in his chest, the left arm hanging over the raised knee. Mail shirt under a leather breastplate,
# trousers, boots. The proportions below are noted at twice life size (the 1999 sitting model scaled to 1.72 m tall)
# and scaled down by WARRIOR_SCALE when built.

WARRIOR_SCALE = 0.433
WARRIOR_TORSO = [
    (0.004, 0.073, 0.362, 0.287, 0.235),
    (0.006, 0.102, 0.445, 0.270, 0.228),
    (0.007, 0.102, 0.540, 0.269, 0.210),
    (0.008, 0.097, 0.625, 0.283, 0.190),
    (0.008, 0.104, 0.713, 0.302, 0.186),
    (0.009, 0.112, 0.802, 0.321, 0.183),
    (0.010, 0.090, 0.894, 0.340, 0.214),
    (0.010, 0.100, 0.960, 0.346, 0.225),
    (0.015, 0.097, 1.035, 0.355, 0.230),
    (0.015, 0.088, 1.126, 0.345, 0.215),
    (0.012, 0.070, 1.200, 0.280, 0.185),
    (0.008, 0.030, 1.250, 0.150, 0.120),
]
WARRIOR_ARMS = {
    "R": [
        [
            (0.330, 0.130, 1.160, 0.110, 0.115),
            (0.414, 0.110, 1.065, 0.105, 0.115),
            (0.409, 0.062, 0.993, 0.115, 0.110),
            (0.419, 0.017, 0.925, 0.109, 0.105),
            (0.439, -0.027, 0.860, 0.091, 0.100),
            (0.460, -0.072, 0.795, 0.074, 0.094),
        ],
        [
            (0.420, -0.130, 0.765, 0.120, 0.112),
            (0.298, -0.151, 0.782, 0.123, 0.111),
            (0.222, -0.162, 0.804, 0.111, 0.101),
            (0.145, -0.174, 0.826, 0.099, 0.092),
            (0.069, -0.185, 0.848, 0.088, 0.083),
            (-0.007, -0.196, 0.870, 0.076, 0.074),
            (-0.040, -0.200, 0.878, 0.070, 0.068),
        ],
    ],
    "L": [
        [
            (-0.380, 0.150, 1.330, 0.120, 0.110),
            (-0.450, 0.234, 1.400, 0.117, 0.107),
            (-0.492, 0.303, 1.407, 0.108, 0.111),
            (-0.521, 0.380, 1.414, 0.100, 0.100),
            (-0.548, 0.458, 1.421, 0.092, 0.087),
        ],
        [
            (-0.610, 0.480, 1.400, 0.110, 0.100),
            (-0.639, 0.421, 1.382, 0.120, 0.117),
            (-0.686, 0.278, 1.321, 0.099, 0.106),
            (-0.733, 0.135, 1.259, 0.078, 0.094),
            (-0.781, -0.008, 1.198, 0.057, 0.083),
        ],
    ],
}
WARRIOR_LEGS = {
    "L": [
        [
            (-0.210, -0.158, 0.283, 0.152, 0.156),
            (-0.297, -0.305, 0.385, 0.172, 0.156),
            (-0.369, -0.433, 0.518, 0.152, 0.152),
            (-0.430, -0.567, 0.651, 0.132, 0.137),
            (-0.473, -0.655, 0.690, 0.150, 0.135),
        ],
        [
            (-0.505, -0.760, 0.690, 0.150, 0.145),
            (-0.521, -0.911, 0.606, 0.147, 0.156),
            (-0.534, -1.079, 0.502, 0.131, 0.132),
            (-0.548, -1.246, 0.399, 0.114, 0.107),
            (-0.563, -1.414, 0.295, 0.097, 0.085),
            (-0.580, -1.520, 0.250, 0.100, 0.085),
        ],
        [
            (-0.600, -1.600, 0.180, 0.150, 0.110),
            (-0.618, -1.653, 0.238, 0.141, 0.116),
            (-0.664, -1.727, 0.325, 0.107, 0.120),
            (-0.699, -1.800, 0.416, 0.074, 0.112),
            (-0.709, -1.828, 0.473, 0.053, 0.074),
        ],
    ],
    "R": [
        [
            (0.176, -0.161, 0.201, 0.162, 0.165),
            (0.182, -0.357, 0.239, 0.180, 0.155),
            (0.186, -0.543, 0.307, 0.166, 0.143),
            (0.182, -0.729, 0.378, 0.149, 0.128),
            (0.180, -0.840, 0.420, 0.150, 0.125),
        ],
        [
            (0.184, -0.950, 0.413, 0.153, 0.132),
            (0.188, -1.070, 0.366, 0.159, 0.148),
            (0.177, -1.258, 0.306, 0.148, 0.155),
            (0.174, -1.450, 0.256, 0.127, 0.132),
            (0.171, -1.642, 0.205, 0.106, 0.108),
            (0.168, -1.840, 0.173, 0.100, 0.090),
        ],
        [
            (0.166, -1.900, 0.110, 0.110, 0.150),
            (0.167, -1.951, 0.148, 0.108, 0.150),
            (0.170, -1.985, 0.193, 0.112, 0.136),
            (0.177, -2.039, 0.296, 0.120, 0.098),
            (0.173, -2.091, 0.401, 0.118, 0.065),
            (0.174, -2.109, 0.459, 0.088, 0.051),
        ],
    ],
}
WARRIOR_JOINTS = {
    "tailbone": (0.0, 0.006, 0.234),
    "midspine": (0.0, 0.126, 0.543),
    "upperback": (0.0, 0.183, 0.939),
    "head": (0.022, -0.124, 1.383),
    "Rbicep": (0.413, 0.099, 1.142),
    "Rforearm": (0.468, -0.140, 0.751),
    "Rhand": (-0.094, -0.220, 0.889),
    "Lbicep": (-0.424, 0.111, 1.432),
    "Lforearm": (-0.598, 0.528, 1.455),
    "Lwrist": (-0.803, -0.062, 1.191),
    "Rthigh": (0.248, -0.003, 0.188),
    "Rcalf": (0.248, -0.879, 0.479),
    "Rfoot": (0.178, -1.839, 0.168),
    "Lthigh": (-0.240, -0.003, 0.188),
    "Lcalf": (-0.549, -0.662, 0.779),
    "Lfoot": (-0.599, -1.507, 0.254),
}


def ws(p):
    """Scale a point, or a section (x, y, z, ru, rw), from the twice-life-size notes to metres."""
    return tuple(c * WARRIOR_SCALE for c in p)


def build_warrior():
    b = hk.Body("Warrior")
    b.tube([ws(s) for s in WARRIOR_TORSO], "mail", power=2.3, nseg=40)
    b.blob(ws((0.0, 0.02, 0.27)), ws((0.30, 0.22, 0.14)), "trousers")  # seat
    for s in "RL":
        upper, fore = WARRIOR_ARMS[s]
        b.tube([ws(x) for x in upper], "mail", power=2.1)
        b.tube([ws(x) for x in fore], "mail", power=2.1)
        b.blob(ws(upper[0][:3]), ws((0.13, 0.13, 0.13)), "mail")
        b.blob(ws(Vector(upper[-1][:3]).lerp(Vector(fore[0][:3]), 0.5)), ws((0.105, 0.105, 0.105)), "mail")  # elbow
        thigh, shin, foot = WARRIOR_LEGS[s]
        b.tube([ws(x) for x in thigh], "trousers", power=2.2)
        b.tube([ws(x) for x in shin], "trousers", power=2.2)
        b.blob(ws(Vector(thigh[-1][:3]).lerp(Vector(shin[0][:3]), 0.5)), ws((0.15, 0.15, 0.15)), "trousers")  # knee
        b.tube([ws(x) for x in foot], "shoe", power=2.5)
    b.build(voxel=0.0045, smooth=6)
    neck = [
        ws((0.0, 0.06, 1.17, 0.12, 0.11)),
        ws((0.01, -0.03, 1.23, 0.10, 0.10)),
        ws((0.015, -0.10, 1.27, 0.085, 0.085)),
    ]
    hk.tube_mesh("Warrior_Neck", neck, nseg=16, mat="skin")
    # the right hand pressed flat on the chest, the left hanging over the knee
    hk.hand("Warrior_HandR", ws((-0.045, -0.200, 0.880)), (-1.0, 0.25, 0.05), (0, 0, 1), (0, 1, 0), 0.85, 0.4)
    hk.hand(
        "Warrior_HandL",
        ws((-0.785, -0.030, 1.165)),
        (0.0, -0.3, -1.0),
        (1, 0, 0),
        (0, -0.95, 0.3),
        0.85,
        0.6,
        stub=0.06,
    )
    budgets = {"Warrior_Body": 1950, "Warrior_Neck": 80, "Warrior_Hand": 140}
    return hk.build_lod(parts("Warrior_"), budgets, "Warrior_Game")


# ---------------------------------------------------------------------------- the rack victim (rackvictim)
# Stretched on the rack: arms up and out with clenched fists, legs apart. Open shirt with short sleeves, a plaid sash
# from his right shoulder to the belt, a pleated kilt to above the knee, rolled socks and soft boots.

RACK_TORSO = [  # under the kilt top to the neck
    (0.009, 0.050, 0.940, 0.150, 0.098),
    (0.007, 0.045, 1.000, 0.130, 0.091),
    (0.005, 0.038, 1.040, 0.116, 0.082),
    (0.000, 0.027, 1.080, 0.110, 0.079),
    (0.002, 0.025, 1.120, 0.121, 0.085),
    (0.004, 0.023, 1.160, 0.134, 0.090),
    (0.004, 0.020, 1.200, 0.148, 0.096),
    (0.004, 0.021, 1.240, 0.162, 0.098),
    (0.004, 0.025, 1.280, 0.175, 0.097),
    (0.005, 0.030, 1.320, 0.180, 0.094),
    (0.002, 0.040, 1.370, 0.150, 0.085),
    (0.000, 0.045, 1.420, 0.090, 0.068),
    (0.000, 0.047, 1.470, 0.060, 0.058),
]
RACK_KILT = [  # belt to hem, flaring out over the parted legs
    (0.012, 0.068, 0.570, 0.292, 0.080),
    (0.007, 0.062, 0.640, 0.267, 0.083),
    (0.003, 0.059, 0.720, 0.241, 0.088),
    (-0.001, 0.057, 0.800, 0.215, 0.093),
    (0.001, 0.056, 0.880, 0.182, 0.099),
    (0.007, 0.052, 0.940, 0.152, 0.100),
    (0.007, 0.048, 0.985, 0.136, 0.094),
]
RACK_ARM = [  # right shoulder to wrist, raised (the left is close to the mirror)
    (0.150, 0.046, 1.385, 0.075, 0.060),
    (0.192, 0.053, 1.404, 0.062, 0.050),
    (0.274, 0.053, 1.444, 0.051, 0.050),
    (0.354, 0.059, 1.488, 0.044, 0.042),
    (0.393, 0.069, 1.512, 0.038, 0.035),
    (0.473, 0.064, 1.556, 0.042, 0.045),
    (0.554, 0.061, 1.597, 0.037, 0.036),
    (0.633, 0.053, 1.639, 0.032, 0.025),
    (0.655, 0.050, 1.648, 0.032, 0.025),
]
RACK_LEG = [  # right hip to ankle (the thigh is under the kilt)
    (0.090, 0.050, 0.930, 0.075, 0.075),
    (0.170, 0.055, 0.740, 0.068, 0.072),
    (0.243, 0.062, 0.579, 0.052, 0.070),
    (0.263, 0.073, 0.537, 0.055, 0.067),
    (0.304, 0.088, 0.446, 0.063, 0.074),
    (0.338, 0.086, 0.350, 0.060, 0.068),
    (0.377, 0.078, 0.256, 0.052, 0.057),
    (0.416, 0.071, 0.162, 0.044, 0.047),
    (0.432, 0.060, 0.115, 0.044, 0.055),
]
RACK_FOOT = [  # right boot, heel to toe
    (0.450, 0.060, 0.070, 0.055, 0.050),
    (0.458, 0.017, 0.068, 0.057, 0.055),
    (0.461, -0.011, 0.052, 0.044, 0.055),
    (0.466, -0.040, 0.039, 0.029, 0.052),
    (0.464, -0.069, 0.034, 0.019, 0.040),
]
RACK_JOINTS = {
    "Pelvis": (0.0, 0.049, 0.952),
    "spine1": (0.0, 0.025, 1.080),
    "spine2": (0.0, 0.020, 1.237),
    "head": (0.0, 0.049, 1.496),
    "Rhumerus": (0.161, 0.055, 1.415),
    "Rradius": (0.370, 0.072, 1.516),
    "Rhand": (0.625, 0.048, 1.646),
    "Lhumerus": (-0.160, 0.055, 1.415),
    "Lradius": (-0.377, 0.070, 1.508),
    "Lhand_1": (-0.640, 0.062, 1.618),
    "Rfemur": (0.094, 0.048, 0.953),
    "Rtibia": (0.231, 0.053, 0.553),
    "Rfoot": (0.421, 0.097, 0.112),
    "Lfemur": (-0.094, 0.048, 0.952),
    "Ltibia": (-0.208, 0.054, 0.547),
    "Lfoot": (-0.357, 0.111, 0.091),
}


def build_rack():
    b = hk.Body("Rack")
    b.tube(RACK_TORSO, "shirt", power=2.3, nseg=40)
    for sx in (1, -1):
        m = (lambda s: s) if sx > 0 else hk.mirror_x
        b.tube(m(RACK_ARM), "shirt", power=2.0)
        b.blob((0.145 * sx, 0.045, 1.38), (0.07, 0.06, 0.065), "shirt")
        leg = m(RACK_LEG)
        foot = m(RACK_FOOT)
        if sx < 0:  # his left leg is a little less spread and set back
            leg = [(x * 0.9 if z < 0.9 else x, y + 0.02 * (0.95 - z), z, ru, rw) for x, y, z, ru, rw in leg]
            foot = [(x * 0.89 + 0.03, y + 0.02, z, ru, rw) for x, y, z, ru, rw in foot]
        b.tube(leg, "skin", power=2.1)
        b.tube(foot, "shoe", power=2.6)

    def tag(c, m):
        if m == "skin" and c.z < 0.46:
            return "leather"  # socks and boots
        if m == "shirt" and abs(c.x) > 0.33:
            return "skin"  # bare forearms below the short sleeves
        return m

    body = b.build(voxel=0.004, smooth=6, tag=tag)

    def pleats(a, t):  # box pleats, deeper towards the hem
        return (1 - t) * 0.006 * math.sin(14 * a)

    hk.tube_mesh("Rack_Kilt", RACK_KILT, power=2.2, nseg=56, fold=pleats, mat="kilt")
    hk.ring_band("Rack_Belt", body, (0, 0.045, 0), (0, 0, 1), [(0.965, 0.010), (0.995, 0.009)], 56, "belt", 0.004)
    # the sash from the right shoulder across the chest to the belt at his left hip
    pts = [(0.135, 1.38), (0.08, 1.28), (0.02, 1.18), (-0.04, 1.09), (-0.095, 1.00)]
    strap = [(x, hk.front_y(body, x, z) - 0.004, z, 0.030, 0.005) for x, z in pts]
    hk.tube_mesh("Rack_Sash", strap, nseg=12, mat="kilt")
    neck = [(0, 0.045, 1.42, 0.062, 0.060), (0, 0.047, 1.47, 0.056, 0.056), (0, 0.049, 1.51, 0.050, 0.052)]
    hk.tube_mesh("Rack_Neck", neck, nseg=16, mat="skin")
    for sx in (1, -1):
        rows = [(0.44, 0.002), (0.455, 0.006), (0.47, 0.003)]
        hk.ring_band(f"Rack_Sock{sx}", body, (0.30 * sx, 0.09, 0), (0, 0, 1), rows, 32, "leather", 0.003, maxr=0.1)
        wrist = (0.650 * sx, 0.050, 1.646) if sx > 0 else (-0.660, 0.068, 1.616)
        along = (0.88 * sx, -0.02, 0.45)
        hk.hand(f"Rack_Hand{sx}", wrist, along, (-0.45 * sx, 0, 0.88), (0, -1, 0), 1.05, fist=True)
    budgets = {
        "Rack_Body": 1300,
        "Rack_Kilt": 600,
        "Rack_Belt": 56,
        "Rack_Sash": 48,
        "Rack_Neck": 80,
        "Rack_Sock": 40,
        "Rack_Hand": 130,
    }
    return hk.build_lod(parts("Rack_"), budgets, "Rack_Game")


# The 1999 villager and slave necks are split between the head pages (entries 0, 1: face, beard and hair) and the chest
# page (entry 3). Projected from the nearest of them, the triangles of the new neck alternated between beard and skin and
# drew a band of teeth under the jaw, so the kept 1999 head read as separate from the body: the neck takes the chest page.
VILLAGER_NECK = {"neck": {"pcTextures": [3]}}

CHARACTERS = {
    # the neck takes the 1999 head textures (face, head and neck): the robe texture reaches up to the chin
    "julian": [("julian (h)", "Julian", "julian.json", JULIAN_JOINTS, ("head",), {"neck": {"pcTextures": [0, 1]}})],
    "farmer": [("farmer", "the farmer", "farmer.json", FARMER_JOINTS, ("head", "neck"))],
    "warrior": [
        ("warrior2", "the wounded warrior", "warrior.json", {k: ws(v) for k, v in WARRIOR_JOINTS.items()}, ("head",))
    ],
    "rack": [("rackvictim", "the rack victim", "rackvictim.json", RACK_JOINTS, ("head", "Neck"))],
    "rimril": [
        ("Rimril", "Rimril", "rimril.json", RIMRIL_JOINTS, ("head", "topstaff", "bottomstaff")),
        (
            "wounded wizard",
            "the wounded wizard",
            "wounded_wizard.json",
            RIMRIL_JOINTS,
            ("head", "topstaff", "bottomstaff"),
        ),
    ],
    "villager": [
        ("mvillager", "a villager", "mvillager.json", villager_joints(False), ("head",), VILLAGER_NECK),
        ("mvillage2", "a villager", "mvillage2.json", villager_joints(False), ("head",), VILLAGER_NECK),
        ("slaveM (h)", "a mine slave", "slave.json", villager_joints(True), ("head",), VILLAGER_NECK),
        ("slaveM 02 (h)", "a mine slave", "slave2.json", villager_joints(True), ("head",), VILLAGER_NECK),
        ("slaveM 03 (h)", "a mine slave", "slave3.json", villager_joints(True), ("head",), VILLAGER_NECK),
    ],
}
BUILDERS = {
    "julian": build_julian,
    "farmer": build_farmer,
    "villager": build_villager,
    "rimril": build_rimril,
    "warrior": build_warrior,
    "rack": build_rack,
}


def main():
    args = parse_args()
    which = list(BUILDERS) if args.which == "all" else args.which.split(",")
    for name in which:
        reset()
        lod = BUILDERS[name]()
        tris = len(lod.data.polygons)
        print(f"{name}: game mesh {len(lod.data.vertices)} vertices, {tris} triangles")
        for model, label, fname, joints, keep, *slots in CHARACTERS[name]:
            hk.export(lod, Path(args.models) / fname, model, label, joints, keep, slots[0] if slots else None)
        if args.render:
            out = Path(args.render).resolve()
            out.mkdir(parents=True, exist_ok=True)
            hk.clay_render([lod], str(out / name))


main()
