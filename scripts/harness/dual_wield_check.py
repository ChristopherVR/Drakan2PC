"""Dual wielding check ([general] dual_wield, docs/reference/combat-controls.md "Dual wielding").

    python scripts/harness/dual_wield_check.py --save <card>/mc0 [--name dualwield] [--cells 1,16]

Needs a save card (mc0 folder) whose first slot loads Rynn with two one-handed weapons (class 0x1000e, stance 0) in
the inventory, the bow not drawn and nothing in the shield slot. Two game runs through play_run.py, from the play
folder's exe (dist/DrakanPC) with a run-owned drakan.ini (a copy of the play folder's, window pinned to 1280x960 4:3
windowed so the native Tab grid sits where the script's cursor expects it, dual_wield = 1, native Tab screen):

1. scan: open the Tab screen and dump RAM; picks the weapons (or takes --cells right,left);
2. check: equip the right-hand weapon, then the left one (native Tab screen, mouse hover + E), attack six times,
   reopen and unequip the left weapon. RAM dumps at each step, screenshots of the strikes, the runtime's trace
   (DRAKAN_DUAL_WIELD_LOG=2).

Checks (PASS / FAIL each, report in build/harness/<name>/dual_wield_report.json):
  details   native details hand line: "Equips to left hand" on hover, "In left hand" once equipped
  equip     slot 0 = right weapon, slot 2 (shield slot) = left weapon, both cells flagged equipped
  attach    the left weapon on the left hand bone (19), its owner ref = Rynn's model
  strikes   attack frames traced, right and left strikes, left ones reach the full mirror, never two lefts in a row
  mirror    at full mirror the grips are the native ones reflected in x (within --tolerance units)
  blade     only the striking hand's blade goes live during each native hit window
  root      a mirrored strike's lunge is a reflection (horizontal speed and vertical motion unchanged)
  unequip   using the left weapon again empties slot 2 and the bone; the right weapon stays (not with --engage)
  hits      with --engage (a save with an enemy in reach, e.g. combat_save_fixture.py next to the Surdana hostiles):
            lock on with F, then each hand's blade must hit during its own strikes and never during the other's

Not covered: save / load with the left weapon equipped. With --engage the run ends after the strikes (Rynn may die).
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
import re
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAY_RUN = ROOT / "scripts" / "harness" / "play_run.py"

# ---- native layout (SCES_500.06) ----
CONTEXT = 0x5565D4
CLASS_MELEE = 0x1000E
TYPE_WEAPON = 10
PROP_STANCE = 0x1E
PLAYER_SLOTS, SLOT_STRIDE, LEFT_HOLDER, BOW_MODE = 0xA48, 0x10, 0xE90, 5
CTL = 0x584
CTL_SELECTION, CTL_SLOTS, CTL_NAME, CTL_HAND_LINE = 0x20, 0x30, 0x244, 0x384
LEFT_HAND_BONE = 19
WEAPON_OWNER = 0x60
GRID_COLUMNS, GRID_CELLS = 8, 24
HAND_LINES = ("Equips to left hand", "Equips to right hand", "In left hand", "In right hand")

# ---- the native Tab grid in the input script's 640 x 512 cursor space, 1280 x 960 4:3 window ----
# Measured: cell 1 at (104, 128), cell 16 at (55, 235).
GRID_X0, GRID_DX, GRID_Y0, GRID_DY = 55.0, 49.0, 128.0, 53.5
WINDOW = {"fullscreen": "0", "window_mode": "windowed", "window_width": "1280", "window_height": "960", "aspect": "4:3"}


def cell_cursor(cell: int) -> tuple[int, int]:
    """Centre of a grid cell in cursor space."""
    if not 0 <= cell < GRID_CELLS:
        raise ValueError(f"cell {cell} outside the 24-cell grid")
    col, row = cell % GRID_COLUMNS, cell // GRID_COLUMNS
    return round(GRID_X0 + GRID_DX * col), round(GRID_Y0 + GRID_DY * row)


# ---- drakan.ini ----
def patch_ini(text: str, values: dict[str, dict[str, str]]) -> str:
    """Sets [section] key = value (replacing an existing key line, else appending to the section, else a new
    section). Comments and every other line are kept."""
    lines = text.splitlines()
    pending = {s: dict(kv) for s, kv in values.items()}
    out: list[str] = []
    section = None

    def flush(sec):
        for k, v in pending.pop(sec, {}).items():
            out.append(f"{k} = {v}")

    for line in lines:
        m = re.match(r"\s*\[([^\]]+)\]", line)
        if m:
            if section is not None:
                flush(section)
            section = m.group(1).strip().lower()
            out.append(line)
            continue
        km = re.match(r"\s*([A-Za-z0-9_]+)\s*=", line)
        if section in pending and km and km.group(1).lower() in pending[section]:
            key = km.group(1).lower()
            out.append(f"{key} = {pending[section].pop(key)}")
            continue
        out.append(line)
    if section is not None:
        flush(section)
    for sec in list(pending):
        if pending[sec]:
            out.append(f"[{sec}]")
            flush(sec)
    return "\n".join(out) + "\n"


# ---- RAM ----
class Ram:
    def __init__(self, data: bytes):
        self.m = data

    def u32(self, a: int) -> int:
        a &= 0x1FFFFFF
        return struct.unpack_from("<I", self.m, a)[0] if a + 4 <= len(self.m) else 0

    def u16(self, a: int) -> int:
        a &= 0x1FFFFFF
        return struct.unpack_from("<H", self.m, a)[0] if a + 2 <= len(self.m) else 0

    def s16(self, a: int) -> int:
        a &= 0x1FFFFFF
        return struct.unpack_from("<h", self.m, a)[0] if a + 2 <= len(self.m) else 0

    def text(self, a: int, n: int = 0x40) -> str:
        a &= 0x1FFFFFF
        return self.m[a : a + n].split(b"\0")[0].decode("latin-1").strip()

    @staticmethod
    def ok(a: int) -> bool:
        return 0x100000 <= a < 0x2000000


@dataclass
class Weapon:
    cell: int
    handle: int
    equipped: bool
    cls: int
    type: int
    stance: int

    @property
    def one_handed(self) -> bool:
        return self.type == TYPE_WEAPON and self.cls == CLASS_MELEE and self.stance == 0


@dataclass
class State:
    right: int = 0
    shield_slot: int = 0
    bow_drawn: bool = False
    on_left_bone: int = 0
    left_owner: int = 0
    model_ref: int = 0
    selection: int = -1
    selected_name: str = ""
    hand_line: str = ""
    weapons: list[Weapon] = field(default_factory=list)

    def weapon(self, cell: int) -> Weapon | None:
        return next((w for w in self.weapons if w.cell == cell), None)


def read_state(data: bytes) -> State:
    r = Ram(data)
    ctx = r.u32(CONTEXT)

    def obj(ref):
        if not ref or ref > r.u32(ctx + 0x84) + 1:
            return 0
        return r.u32(r.u32(ctx + 0x7C) + (ref - 1) * 8)

    def data_of(h):
        p = r.u32(h + 0xFC) if r.ok(h) else 0
        return r.u32(p + 0x10) if r.ok(p) else 0

    def cls(h):
        ix, db = r.u16(h + 0x10), r.u32(ctx + 0x320)
        if ix == 0xFFFF or not r.ok(db) or ix > r.u32(db + 0x10):
            return 0
        return r.u32(r.u32(db + 8) + ix * 0x24 + 8)

    def prop(h, pid):
        pb = r.u32(h + 0x54)
        return r.u32(r.u32(pb + 4) + pid * 4) if r.ok(pb) else 0xFFFFFFFF

    st = State()
    player = data_of(r.u32(ctx + 0x4B0))
    if not r.ok(player):
        return st
    model = r.u32(player + 0x80)
    st.right = r.u32(player + PLAYER_SLOTS)
    st.shield_slot = r.u32(player + PLAYER_SLOTS + 2 * SLOT_STRIDE)
    st.bow_drawn = r.u32(player + LEFT_HOLDER) == BOW_MODE
    st.model_ref = r.u16(model + 0x12)
    sk = r.u32(model + 0x50)
    if r.ok(sk) and r.u32(sk + 8) & 8:
        rec = r.u32(sk + 0xE8) + r.s16(r.u32(sk + 0xDC) + LEFT_HAND_BONE * 2) * 0x80
        if r.u32(rec + 4) == LEFT_HAND_BONE and r.u32(rec + 8) != 0xFFFFFFFF:
            st.on_left_bone = obj(r.u16(r.u32(sk + 0xF4) + r.u32(rec + 8) * 0x6C + 4))
    if r.ok(st.shield_slot):
        w = data_of(st.shield_slot)
        st.left_owner = r.u16(w + WEAPON_OWNER) if r.ok(w) else 0
    ctl = player + CTL
    st.selection = struct.unpack("<i", struct.pack("<I", r.u32(ctl + CTL_SELECTION)))[0]
    st.selected_name = r.text(ctl + CTL_NAME)
    st.hand_line = r.text(ctl + CTL_HAND_LINE)
    for cell in range(GRID_CELLS):
        recp = ctl + CTL_SLOTS + cell * 0x10
        h = r.u32(recp + 8)
        if h <= 0x53 or not r.ok(h):
            continue
        t = r.u32(h + 0xC)
        if t != TYPE_WEAPON:
            continue
        c = cls(h)
        st.weapons.append(Weapon(cell, h, r.u32(recp) != 0, c, t, prop(h, PROP_STANCE) if c == CLASS_MELEE else -1))
    return st


def pick_cells(st: State) -> tuple[int, int]:
    """(right, left): the right-hand weapon if one is held, else the first one-handed weapon, and another one."""
    if st.bow_drawn:
        raise SystemExit("scan: the bow is drawn (left hand taken); use a save with it put away")
    if st.shield_slot:
        raise SystemExit("scan: the shield slot is taken; use a save without a shield or left-hand weapon")
    one = [w for w in st.weapons if w.one_handed]
    held = next((w for w in one if w.handle == st.right), None)
    first = held or (one[0] if one else None)
    second = next((w for w in one if first and w.handle != first.handle), None)
    if not first or not second:
        raise SystemExit(f"scan: need two one-handed weapons, found {[w.cell for w in one]}")
    return first.cell, second.cell


# ---- recipes ----
# Times are milliseconds (`clock milliseconds`, relative to the last sync) and every key / button is held for HOLD
# ms: the input sample rate is not the game's frame rate (a one-sample tap can fall between two game frames).
HOLD = 120


def press(t: int, what: str) -> list[str]:
    """Hold a key ("key E") or mouse button ("button 1") for HOLD ms from t."""
    kind, name = what.split(maxsplit=1)
    return [f"{t} {kind} down {name}", f"{t + HOLD} {kind} up {name}"]


SCAN_RECIPE = (
    "\n".join(
        [
            "clock milliseconds",
            "1 log on 60",
            "0 sync play 60",
            *press(300, "key TAB"),
            "0 sync screen 21 20",
            "800 dump ram_scan",
            "1200 quit",
        ]
    )
    + "\n"
)


def hover(t: int, cell: int) -> list[str]:
    x, y = cell_cursor(cell)
    return [f"{t} cursor {x - 1} {y - 1}", f"{t + 100} cursor {x} {y}"]


def check_recipe(
    right: int, left: int, right_held: bool, taps: int = 6, tap_every: int = 600, engage: bool = False
) -> str:
    lines = ["clock milliseconds", "1 log on 60", "0 sync play 60", *press(300, "key TAB"), "0 sync screen 21 20"]
    t = 800  # the Tab screen's opening animation
    if not right_held:
        lines += [*hover(t, right), f"{t + 700} dump ram_01_right_hover", *press(t + 800, "key E")]
        t += 2700
        lines.append(f"{t} dump ram_02_right_equipped")
        t += 200
    lines += [*hover(t, left), f"{t + 700} dump ram_03_left_hover", *press(t + 800, "key E")]
    t += 2700
    lines += [f"{t} dump ram_04_left_equipped", f"{t + 100} shot s04_tab", *press(t + 300, "key TAB")]
    lines.append("0 sync play 60")
    if engage:  # lock on to the nearest enemy (F, the targeting toggle) so the strikes face it
        lines += press(300, "key F")
    first = 800
    end = first + taps * tap_every + 1200
    # Events run in file order: the clicks and the screenshots are merged by time.
    strike = [x for i in range(taps) for x in press(first + i * tap_every, "button 1")]
    strike += [f"{k} shot strike_{k:05d}" for k in range(first + 50, end - 400, 200)]
    lines += sorted(strike, key=lambda line: int(line.split()[0]))
    if engage:  # a fight can end with Rynn dead (the load prompt): stop after the strikes, no unequip step
        lines += [f"{end} dump ram_05_fight", f"{end + 300} quit"]
        return "\n".join(lines) + "\n"
    lines += [*press(end, "key TAB"), "0 sync screen 21 20"]
    lines += [*hover(800, left), *press(1600, "key E"), "3500 dump ram_05_unequipped", "3600 shot s05_tab"]
    lines += [*press(3800, "key TAB"), "0 sync play 30", "500 quit"]
    return "\n".join(lines) + "\n"


# ---- trace ----
POSE = re.compile(r"\[dual-wield\] pose clip=([0-9a-f]+) t=([-\d.]+) step=(\d+) left=(\d) armed=(\d) weight=([\d.]+)")
TRIPLE = r"\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)"
# The weight is on the line itself: grips are traced through the blend-out after a strike, when no pose line is.
GRIPS = re.compile(
    r"\[dual-wield\] grips w=([\d.]+) native L" + TRIPLE + " R" + TRIPLE + " -> L" + TRIPLE + " R" + TRIPLE
)
BLADE = re.compile(r"\[dual-wield\] blade right=(\d) left=(\d) strike=(left|right)")
HIT = re.compile(r"\[dual-wield\] hit hand=(left|right) strike=(left|right) targets=(\d+)")
ROOT_MOTION = re.compile(r"\[dual-wield\] root w=([\d.]+) world " + TRIPLE + " -> " + TRIPLE)


def analyze_trace(text: str, tolerance: float = 25.0) -> dict:
    frames, blades, grips, hits, roots = [], [], [], [], []
    for line in text.splitlines():
        if m := POSE.search(line):
            frames.append(
                dict(
                    clip=int(m.group(1), 16),
                    t=float(m.group(2)),
                    step=int(m.group(3)),
                    left=m.group(4) == "1",
                    armed=m.group(5) == "1",
                    weight=float(m.group(6)),
                )
            )
        elif m := GRIPS.search(line):
            v = [float(x) for x in m.groups()]
            grips.append(dict(weight=v[0], in_l=v[1:4], in_r=v[4:7], out_l=v[7:10], out_r=v[10:13]))
        elif m := BLADE.search(line):
            blades.append((m.group(1) == "1", m.group(2) == "1", m.group(3) == "left"))
        elif m := HIT.search(line):
            hits.append((m.group(1), m.group(2), int(m.group(3))))
        elif m := ROOT_MOTION.search(line):
            v = [float(x) for x in m.groups()]
            roots.append(dict(weight=v[0], before=v[1:4], after=v[4:7]))

    # Strikes: runs of frames with the same clip and combo step.
    strikes = []
    for fr in frames:
        same = strikes and strikes[-1]["clip"] == fr["clip"] and strikes[-1]["step"] == fr["step"]
        if (
            same
            and strikes[-1]["left"] == fr["left"]
            and not (fr["t"] + 1e-3 < strikes[-1]["t"] and strikes[-1]["t"] >= 0.1)
        ):
            s = strikes[-1]
        else:
            s = dict(clip=fr["clip"], step=fr["step"], left=fr["left"], max_weight=0.0, frames=0, t=0.0)
            strikes.append(s)
        s["frames"] += 1
        s["left"] = s["left"] or fr["left"]
        s["max_weight"] = max(s["max_weight"], fr["weight"])
        s["t"] = max(s["t"], fr["t"])
    lefts = [s for s in strikes if s["left"]]
    rights = [s for s in strikes if not s["left"]]
    two_lefts = any(a["left"] and b["left"] for a, b in itertools.pairwise(strikes))
    strikes_ok = bool(frames) and bool(rights) and bool(lefts) and all(s["max_weight"] >= 0.99 for s in lefts)
    strikes_ok = strikes_ok and not two_lefts and all(fr["armed"] for fr in frames)

    # Mirror: at full weight the new grips are the native opposite ones reflected in x.
    worst, checked = 0.0, 0
    for g in grips:
        if g["weight"] < 0.99:
            continue
        checked += 1
        for out, src in ((g["out_l"], g["in_r"]), (g["out_r"], g["in_l"])):
            want = (-src[0], src[1], src[2])
            worst = max(worst, *(abs(a - b) for a, b in zip(out, want, strict=True)))
    mirror_ok = checked > 0 and worst <= tolerance

    # Blade: the striking hand's blade only. A wrong-hand blade may show for one observation at a strike change (it is
    # switched off right after that observation); live in the next observation with the same strike side, it fails.
    wrong = 0
    for i, (r, l, lstrike) in enumerate(blades):
        if (r if lstrike else l) and i + 1 < len(blades):
            nr, nl, nstrike = blades[i + 1]
            if nstrike == lstrike and (nr if lstrike else nl):
                wrong += 1
    left_live = sum(1 for r, l, lstrike in blades if lstrike and l and not r)
    right_live = sum(1 for r, l, lstrike in blades if not lstrike and r and not l)
    blade_ok = left_live > 0 and right_live > 0 and wrong == 0

    # Hits: each hand's blade hits during its own strikes (needs an enemy in reach).
    by_hand = {h: sum(1 for hh, st, _ in hits if hh == h and st == h) for h in ("left", "right")}
    mismatched = sum(1 for hh, st, _ in hits if hh != st)
    hits_ok = by_hand["left"] > 0 and by_hand["right"] > 0 and mismatched == 0

    # Root motion: the mirrored lunge is a reflection (horizontal speed and the vertical part unchanged).
    root_worst = 0.0
    for rt in roots:
        if rt["weight"] < 0.99:  # while the mirror blends the sideways part is scaled by 1 - 2w, by design
            continue
        b, a = rt["before"], rt["after"]
        root_worst = max(root_worst, abs(math.hypot(b[0], b[2]) - math.hypot(a[0], a[2])), abs(b[1] - a[1]))
    full_weight = sum(1 for rt in roots if rt["weight"] >= 0.99)
    root_ok = root_worst <= 0.5  # the trace prints one decimal

    return dict(
        strikes=dict(
            ok=strikes_ok,
            frames=len(frames),
            sequence=[("L" if s["left"] else "R") + f"{s['step']}:{s['max_weight']:.2f}" for s in strikes],
            two_lefts_in_a_row=two_lefts,
        ),
        mirror=dict(ok=mirror_ok, frames_checked=checked, worst_units=round(worst, 1), tolerance=tolerance),
        blade=dict(
            ok=blade_ok,
            observations=len(blades),
            left_only_in_left_strikes=left_live,
            right_only_in_right_strikes=right_live,
            wrong_hand=wrong,
        ),
        hits=dict(ok=hits_ok, left=by_hand["left"], right=by_hand["right"], wrong_hand=mismatched),
        root=dict(ok=root_ok, moves=len(roots), at_full_mirror=full_weight, worst_units=round(root_worst, 2)),
    )


# ---- RAM checks ----
def check_states(states: dict[str, State], right: int, left: int) -> dict:
    res = {}
    hov, eq, un = states.get("ram_03_left_hover"), states.get("ram_04_left_equipped"), states.get("ram_05_unequipped")
    if hov and eq:
        res["details"] = dict(
            ok=hov.selection == left
            and hov.hand_line == "Equips to left hand"
            and eq.selection == left
            and eq.hand_line == "In left hand",
            hover=dict(selection=hov.selection, name=hov.selected_name, line=hov.hand_line),
            equipped=dict(selection=eq.selection, name=eq.selected_name, line=eq.hand_line),
        )
    if eq:
        wr, wl = eq.weapon(right), eq.weapon(left)
        res["equip"] = dict(
            ok=bool(
                wr and wl and eq.right == wr.handle and eq.shield_slot == wl.handle and wr.equipped and wl.equipped
            ),
            right_slot=hex(eq.right),
            shield_slot=hex(eq.shield_slot),
            right_weapon=hex(wr.handle) if wr else None,
            left_weapon=hex(wl.handle) if wl else None,
        )
        res["attach"] = dict(
            ok=bool(wl and eq.on_left_bone == wl.handle and eq.left_owner == eq.model_ref and eq.model_ref),
            on_left_bone=hex(eq.on_left_bone),
            owner=eq.left_owner,
            model_ref=eq.model_ref,
        )
    if un and eq:
        wr, wl = un.weapon(right), un.weapon(left)
        res["unequip"] = dict(
            ok=bool(
                un.shield_slot == 0 and un.on_left_bone == 0 and wl and not wl.equipped and wr and un.right == wr.handle
            ),
            shield_slot=hex(un.shield_slot),
            on_left_bone=hex(un.on_left_bone),
        )
    for name in ("details", "equip", "attach", "unequip"):
        res.setdefault(name, dict(ok=False, missing="RAM dump not written (the run did not get that far)"))
    return res


# ---- runner ----
def run_game(name: str, save: Path, recipe: str, ini: Path, exe: Path, timeout: int, recipe_dir: Path) -> Path:
    recipe_dir.mkdir(parents=True, exist_ok=True)
    path = recipe_dir / f"{name}.txt"
    path.write_text(recipe, encoding="utf-8")
    cmd = [sys.executable, str(PLAY_RUN), "--name", name, "--exe", str(exe), "--save", str(save)]
    cmd += ["--recipe", str(path), "--timeout", str(timeout), "--attempts", "2"]
    for kv in (
        f"DRAKAN_INI={ini}",
        "DRAKAN_DUAL_WIELD=1",
        "DRAKAN_DUAL_WIELD_LOG=2",
        "DRAKAN_INVENTORY_SCREEN=native",
        "DRAKAN_PAUSE_MENU=native",
        "DRAKAN_FULLSCREEN=0",
        "DRAKAN_WINDOW_WIDTH=1280",
        "DRAKAN_WINDOW_HEIGHT=960",
        "DRAKAN_WS_ASPECT=4:3",
    ):
        cmd += ["--env", kv]
    if subprocess.run(cmd).returncode != 0:
        raise SystemExit(f"{name}: the game run failed (see build/harness/{name}/stdout_*.log)")
    return ROOT / "build" / "harness" / name


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", type=Path, required=True, help="memory card folder (mc0) with the test save in slot 1")
    ap.add_argument("--name", default="dualwield", help="run name under build/harness/")
    ap.add_argument("--exe", type=Path, default=ROOT / "dist" / "DrakanPC" / "drakan.exe")
    ap.add_argument("--cells", help="right,left grid cells (skips the scan run)")
    ap.add_argument("--tolerance", type=float, default=25.0, help="mirror check tolerance in model units")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument(
        "--engage",
        action="store_true",
        help="the save has an enemy in reach: lock on (F) before the strikes and require hits from both hands",
    )
    ap.add_argument("--taps", type=int, default=6, help="attack clicks (600 ms apart)")
    ap.add_argument("--fights", type=int, default=3, help="with --engage: fights to try until one passes every check")
    a = ap.parse_args()
    if not a.exe.is_file():
        raise SystemExit(f"{a.exe} not found; build with scripts/buildtool/Build-LocalGame.ps1 first")
    if not (a.save / "BESCES-50006Slot1").is_dir():
        raise SystemExit(f"{a.save}: no BESCES-50006Slot1 save folder")

    cfg = ROOT / "build" / "harness" / f"{a.name}-config"
    cfg.mkdir(parents=True, exist_ok=True)
    base = a.exe.parent / "drakan.ini"
    ini = cfg / "drakan.ini"
    ini.write_text(
        patch_ini(
            base.read_text(encoding="utf-8", errors="replace") if base.is_file() else "",
            {"video": WINDOW, "general": {"dual_wield": "1", "inventory_screen": "native", "pause_menu": "native"}},
        ),
        encoding="utf-8",
    )

    if a.cells:
        right, left = (int(v) for v in a.cells.split(","))
        right_held = False
    else:
        scan = run_game(f"{a.name}-scan", a.save, SCAN_RECIPE, ini, a.exe, a.timeout, cfg)
        st = read_state((scan / "ram_scan.ram").read_bytes())
        right, left = pick_cells(st)
        right_held = st.weapon(right) is not None and st.right == st.weapon(right).handle
        print(f"scan: right-hand weapon in cell {right} ({'held' if right_held else 'to equip'}), left in cell {left}")

    recipe = check_recipe(right, left, right_held, taps=a.taps, engage=a.engage)
    checks = ("details", "equip", "attach", "strikes", "mirror", "blade", "root")
    checks += ("hits",) if a.engage else ("unequip",)
    # A fight decides whether a strike connects: with --engage, fight again (fresh save copy) until one passes.
    fights = max(1, a.fights) if a.engage else 1
    for n in range(1, fights + 1):
        name = a.name if n == 1 else f"{a.name}-fight{n}"
        out = run_game(name, a.save, recipe, ini, a.exe, a.timeout, cfg)
        report = evaluate(out, checks, right, left, right_held, a.tolerance)
        if fights > 1:
            print(f"fight {n}/{fights}: {'PASS' if report['pass'] else 'FAIL'}")
        if report["pass"]:
            break
    for c in checks:
        detail = {k: v for k, v in report[c].items() if k != "ok"}
        print(f"{'PASS' if report[c]['ok'] else 'FAIL'}  {c:8} {json.dumps(detail)}")
    print(
        f"{'PASS' if report['pass'] else 'FAIL'}: {out / 'dual_wield_report.json'} (screenshots strike_*.png, s0*_tab.png)"
    )
    return 0 if report["pass"] else 1


def evaluate(out: Path, checks: tuple, right: int, left: int, right_held: bool, tolerance: float) -> dict:
    states = {p.stem: read_state(p.read_bytes()) for p in out.glob("ram_*.ram")}
    trace = "".join(p.read_text(encoding="latin-1") for p in sorted(out.glob("stdout_*.log")))
    report = dict(cells=dict(right=right, left=left, right_held=right_held))
    report.update(check_states(states, right, left))
    report.update(analyze_trace(trace, tolerance))
    report["pass"] = all(report[c]["ok"] for c in checks)
    (out / "dual_wield_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    sys.exit(main())
