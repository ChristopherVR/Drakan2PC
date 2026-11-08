"""Order of the Flame: Volcanic -> Rift World -> the end sequence and the credits, in one game process (live_drive as a library).

    python scripts/harness/ootf_volcanic_route.py --name volroute --exe <own copy>/drakan.exe --mods <pack dir> \\
        --level "OotF 11 - Volcanic" --go ram_go --timeout 3000 --setup-timeout 900 --rad-per-count -0.002 \\
        --env DRAKAN_PLAYTEST_GOD=1 [--from blade] [--to rift] [--save-each] [--shots]

The pack must be converted without any OOTF_TEST_* variable (a release conversion). Every step places Rynn or Arokh beside
its trigger (the play-test `place` aid) and then plays the real input: a walk, E, a breath, a flight. Each step is checked
in guest RAM before the next starts, so a failure names its step (`failed_at`) instead of showing up later as a closed
door. In particular the Runeblade step only passes when the blade is in Rynn's carry cells and its Timer 2331 has run
(destroyed): the 2026-11-08 runs that found the rift box "closed" had left the blade in the unmelted ice
(docs/journal/2026-11-08/ootf-volcanic-rift-box-bisect.md).

Steps (`--from` / `--to`): rune, lock, key, cage, mount, breath, land, switch, button, blade, remount, rift, fly, heads,
ending. `--save-each` presses F5 after each passed step (cards to resume a later step with F8). Without `--level`
the recipe must bring the game to the first step itself. Coordinates are the converted level's (PC object positions).
"""

from __future__ import annotations

import math
import struct
import sys
import time
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live_drive as LD

STEPS = "rune lock key cage mount breath land switch button blade remount rift fly heads ending".split()
RUNEBLADE_RES = 0x346A3  # resource of the converted Volcanic Runeblade 2239 (engine class 0x1000e)
TIMER_CLASS = 0x1003E
HEAD_CLASS = 0x1003B  # the four Dr. Evil heads of Rift World
# Rift World: the dive, the left tunnel ramp, the hall, the shaft, over the spines, the altar (2026-11-08 tunnel note)
TUNNEL = [
    (338500, 97500, 300000), (335500, 96000, 288000), (334300, 95300, 281000), (333800, 95000, 277500),
    (337000, 95500, 276500), (342000, 98000, 272000), (342418, 100500, 262000), (342418, 105000, 250000),
    (342418, 107800, 240000), (342418, 108800, 225000), (342418, 108800, 208000), (342418, 106500, 197000),
    (342418, 103800, 188000), (342100, 103000, 183200),
]  # fmt: skip
ALTAR = (342400, 0, 184000)


def beside(at: Sequence[float], angle_deg: float, r: float) -> tuple[float, float, float]:
    """A spot r units from `at` on the side `angle_deg` (0 = +z side, 90 = -x side), and the `place` yaw facing `at`."""
    a = math.radians(angle_deg)
    x, z = at[0] - math.sin(a) * r, at[2] - math.cos(a) * r
    return x, z, LD.yaw_to((x, 0.0, z), at)


def spot_toward(target: Sequence[float], centre: Sequence[float], d: float) -> tuple[float, float, float]:
    """A spot d units from target on the line to centre (xz), and the yaw facing the target."""
    dx, dz = centre[0] - target[0], centre[2] - target[2]
    n = math.hypot(dx, dz) or 1.0
    x, z = target[0] + dx / n * d, target[2] + dz / n * d
    return x, z, LD.yaw_to((x, 0.0, z), target)


def all_near(mem: LD.GuestMemory, p: Sequence[float], r: float) -> list[tuple[int, LD.Actor]]:
    """Registry objects of any class within r (3D) of p, with their slots."""
    ctx = mem.u32(LD.GAME_CTX)
    table, count = mem.u32(ctx + 0x7C), min(mem.u32(ctx + 0x84) + 1, 16384)
    if not LD._valid(table, count * 8):
        return []
    raw = mem.read(table, count * 8)
    out = []
    for s in range(count):
        h = struct.unpack_from("<I", raw, s * 8)[0]
        if LD._valid(h, 0x100) and mem.u32(h) == LD.ENTITY_VTABLE and math.dist(mem.vec3(h + 0x14), p) < r:
            out.append((s, LD.read_actor(mem, h)))
    return out


def rynn_handle(mem: LD.GuestMemory) -> int | None:
    me = LD.controlled(mem)
    if me and me.cls == LD.RYNN_CLASS:
        return me.handle
    r = LD.find_actors(mem, LD.RYNN_CLASS)
    return r[0].handle if r else None


def carry_cells(mem: LD.GuestMemory) -> list[tuple[int, int, int]]:
    """Rynn's 24 carry cells (player data + 0x584 + 0x30, 16 bytes each: +0 equipped word, +8 item handle):
    (cell, equipped word, item resource) for each cell that holds an item."""
    h = rynn_handle(mem)
    if not h:
        return []
    p = mem.u32(mem.u32(h + 0xFC) + 0x10)
    res = mem.u32(mem.u32(LD.GAME_CTX) + 0x320)
    table, count = mem.u32(res + 8), mem.u32(res + 0x10)
    out = []
    for i in range(24):
        cell = p + 0x584 + 0x30 + i * 16
        it = mem.u32(cell + 8)
        if it > 0x53 and LD._valid(it, 0x14):
            ix = mem.u16(it + 0x10)
            if ix <= count:
                out.append((i, mem.u32(cell), mem.u32(table + ix * 0x24)))
    return out


def carried(mem: LD.GuestMemory) -> list[int]:
    return [r for _, _, r in carry_cells(mem)]


def blade_step_passed(items: Sequence[int], timer_before: Sequence[int], timer_after: Sequence[int]) -> bool:
    """The Runeblade is carried and its Timer 2331 was in the registry before the pickup and is gone after it."""
    return RUNEBLADE_RES in items and bool(timer_before) and not timer_after


class Route:  # pragma: no cover - needs a running game
    def __init__(self, link, log, ctl, args):
        self.link, self.log, self.ctl, self.args = link, log, ctl, args
        self.mem = link.mem
        self.res: dict = {"steps": {}}

    # primitives
    def send(self, *lines: str) -> None:
        self.link.send(list(lines))
        self.link.wait_applied()

    def tap(self, k: str, n: int = 1, gap: int = 40) -> None:
        for _ in range(n):
            self.send(f"0 key tap {k}")
            self.link.wait_samples(gap)

    def hold(self, k: str, samples: int) -> None:
        self.send(f"0 key down {k}")
        self.link.wait_samples(samples, timeout=samples / 5 + 30)
        self.send(f"0 key up {k}")

    def place(self, x: float, y: float, z: float, yaw: float = 0.0, settle: int = 60) -> None:
        self.send(f"0 place {x:.0f} {y:.0f} {z:.0f} {yaw:.4f}")
        self.link.wait_samples(settle)

    def shot(self, name: str) -> None:
        if self.args.shots:
            self.send(f"0 shot {name}", f"0 dump ram_{name}")
            self.link.wait_samples(10)

    def me(self) -> LD.Actor:
        return LD.controlled(self.mem)

    def arokh(self) -> LD.Actor | None:
        a = LD.find_actors(self.mem, LD.AROKH_CLASS)
        return a[0] if a else None

    def note(self, step: str, **kw) -> None:
        a = self.arokh()
        if a and "arokh_now" not in kw:
            kw["arokh_now"] = LD._r(a.pos)
            kw["arokh_state"] = LD.actor_words(self.mem, a.handle, (0x94, 0x9C))
        self.res["steps"].setdefault(step, {}).update(kw)
        self.log(step=step, f=self.mem.status().frame, **kw)

    def slots(self, p: Sequence[float], r: float = 5, cls: int | None = None) -> list[int]:
        return [s for s, a in all_near(self.mem, p, r) if cls is None or a.cls == cls]

    def equip_cell0(self) -> None:
        """Modern inventory: TAB, ENTER (first-use help), ENTER (equip the selected cell 0), TAB."""
        for k in ("TAB", "ENTER", "ENTER", "TAB"):
            self.tap(k, 1, 40)
        self.link.wait_samples(40)

    def press(self, at, sides, radii, done) -> list[dict]:
        """Stand beside a switch or button, face it, tap E; each side and distance in turn until done()."""
        tries = []
        for ang in sides:
            for r in radii:
                x, z, yaw = beside(at, ang, r)
                self.place(x, at[1] + 500, z, yaw, settle=80)
                self.tap("E", 1, 200)
                tries.append({"ang": ang, "r": r, "at": LD._r(self.me().pos)})
                if done():
                    return tries
        return tries

    # steps
    def rune(self) -> bool:
        """Ruin detector 1505 lifts the Lava Rune 1042 out of its box (Timer 1473, Mechanism 2903); E beside it."""
        self.place(21611, 128600, 59200, 0.0)
        self.hold("W", 120)
        self.link.wait_samples(500)
        n0 = len(carried(self.mem))
        for z in (57300, 57000, 57600):
            self.place(17460, 127800, z, 0.0, settle=80)
            self.tap("E", 2, 60)
            if len(carried(self.mem)) > n0:
                break
        self.note("rune", carried=[hex(x) for x in carried(self.mem)])
        return len(carried(self.mem)) > n0

    def lock(self) -> bool:
        """Key Lock Invis 2939 (beside the Key Lock model 2229) drains the lava 2257 (y 59912 down). The Use walks Rynn to
        within about 330 units first; from farther off the lock model stops her short and nothing happens."""
        lk = (115744, 62711, 193600)

        def lava_y():
            return [
                a.pos[1] for _, a in all_near(self.mem, (131072, 50000, 198656), 12000) if abs(a.pos[0] - 131072) < 5
            ]

        self.equip_cell0()
        tries = []
        for ang in (90, 0, 180, -90):
            for r in (280, 200, 600):
                x, z, yaw = beside(lk, ang, r)
                self.place(x, lk[1] + 400, z, yaw, settle=100)
                self.tap("E", 2, 60)
                self.link.wait_samples(200)
                ys = lava_y()
                tries.append({"ang": ang, "r": r, "lava_y": [round(y) for y in ys]})
                if ys and min(ys) < 59000:
                    self.note("lock", tries=tries)
                    return True
        self.note("lock", tries=tries)
        return False

    def key(self) -> bool:
        n0 = len(carried(self.mem))
        self.place(208384, 160500, 450500, 0.0, settle=120)
        self.hold("W", 25)
        self.tap("E", 2, 80)
        self.note("key", carried=[hex(x) for x in carried(self.mem)])
        return len(carried(self.mem)) > n0

    def cage(self) -> bool:
        """Detector 1994 with the Rune Cage Key: scene 9.6a frees Arokh (landed: +0x9c = 1)."""
        self.place(262336, 152400, 494900, 0.0, settle=300)
        self.link.wait(lambda s: s.gameplay and s.play_run >= 600, 400)
        a = self.arokh()
        st = LD.actor_words(self.mem, a.handle, (0x94, 0x9C)) if a else []
        self.note("cage", state=st)
        return bool(st) and st[1] == "1"

    def mount(self, side: str = "behind", tag: str = "mount") -> bool:
        args = self.args
        args.target_class, args.near, args.until_class = hex(LD.AROKH_CLASS), None, hex(LD.AROKH_CLASS)
        args.start_from, args.start_dist, args.start_dy, args.settle, args.seconds = side, 1500, 300, 150, 60
        r = LD.mission_approach(self.link, self.log, self.ctl, args)
        ok = self.me().cls == LD.AROKH_CLASS
        self.note(tag, side=side, closest=r.get("closest"), mounted_at_frame=r.get("mounted_at_frame"))
        if ok:
            self.link.wait_samples(250)
        return ok

    def breath(self) -> bool:
        """Flown into box 2898 over the sanctum (shows the catching fire object 2259), then breath on 2259."""
        target = (120314, 56797, 223998)
        self.place(112426 - 9000, 70770, 223274, -1.5708, settle=30)
        self.hold("W", 120)
        self.link.wait_samples(60)
        before = self.slots(target, 8)  # 2259 is created by the box's show message (absent on a fresh start before it)
        tries = []
        for dx, y in [(4000, 57500), (3500, 58200), (3000, 57000), (4500, 59000), (2500, 57800), (4000, 56800)]:
            if not self.slots(target, 8):
                break
            self.place(target[0] - dx, y, target[2], -1.5708, settle=40)
            for _ in range(8):
                self.send("0 button tap 1")
                self.link.wait_samples(20)
            self.link.wait_samples(80)
            tries.append({"dx": dx, "y": y})
        gone = bool(before) and not self.slots(target, 8)
        self.note("breath", tries=tries, gone_2259=gone)
        self.shot("breath")
        return gone

    def land(self) -> bool:
        """Flown north at the cave mouth until his look-ahead stops him, then Cross held: he lands and Rynn dismounts."""
        for attempt in range(3):
            self.place(117600, 58000, 200000, math.pi, settle=60)
            self.send("0 key down W")
            last, still, t0 = None, 0, time.monotonic()
            while time.monotonic() - t0 < 60 and still < 4:
                self.link.wait_samples(10)
                p = self.me().pos
                still = still + 1 if last and abs(p[2] - last[2]) < 15 else 0
                last = p
            self.send("0 key up W", "0 pad down CROSS")
            t0 = time.monotonic()
            while time.monotonic() - t0 < 60 and self.me().cls != LD.RYNN_CLASS:
                self.link.wait_samples(10)
            self.send("0 pad up CROSS")
            if self.me().cls == LD.RYNN_CLASS:
                self.note("land", attempt=attempt, rynn=LD._r(self.me().pos))
                return True
        return False

    def platform_yaw(self) -> float | None:
        for _, a in all_near(self.mem, (116736, 53202, 227328), 5):  # sanctum central platform 2237
            return a.yaw
        return None

    def settled_yaw(self) -> float | None:
        last = None
        for _ in range(30):
            self.link.wait_samples(40)
            y = self.platform_yaw()
            if last is not None and y is not None and abs(LD.wrap(y - last)) < 0.002:
                return y
            last = y
        return last

    def switch(self) -> bool:
        """Succ Switch 2243 turns the platform 90 degrees per press (slowly: wait until it settles); pressed until the
        platform is at identity, which puts the Arrow Detect Object 2366 on the launcher's line in front of the ice."""
        sw = (126003, 53276, 218061)
        yaws = [self.settled_yaw()]
        spots = [(90, 200), (90, 400), (0, 280), (180, 280)]
        k = presses = 0
        while abs(LD.wrap(yaws[-1] or 0.0)) > 0.05 and presses < 8:
            x, z, yaw = beside(sw, *spots[k % len(spots)])
            self.place(x, sw[1] + 500, z, yaw, settle=80)
            self.tap("E", 1, 60)
            presses += 1
            y = self.settled_yaw()
            if y is None or abs(LD.wrap(y - (yaws[-1] or 0.0))) < 0.3:
                k += 1  # nothing turned from this spot: the next one
            yaws.append(y)
        self.note("switch", yaws=[round(y, 3) for y in yaws if y is not None], presses=presses)
        self.shot("switch")
        return yaws[-1] is not None and abs(LD.wrap(yaws[-1])) <= 0.05

    def button(self) -> bool:
        """Wall button 2261 fires launcher 2245; the arrow kills 2366, whose Timer 2368 starts the melt."""
        t2368 = (113493, 83553, 228394)
        tries = []
        for ang, r in [(-90, 300), (-90, 200), (0, 300), (180, 300)]:
            tries += self.press((107515, 53806, 237457), (ang,), (r,), lambda: True)
            self.link.wait_samples(300)
            if not self.slots(t2368, 5, TIMER_CLASS):
                break
        hit = not self.slots(t2368, 5, TIMER_CLASS)
        self.link.wait_samples(1300)  # the ice cover morphs and is destroyed
        self.note("button", tries=tries, arrow_hit=hit, ice_plug=self.slots((116736, 56177, 227328), 5, 0x10063))
        self.shot("button")
        return hit

    def blade(self) -> bool:
        """Runeblade 2239 on the pillar top, E: carried, and its Timer 2331 runs (and is destroyed)."""
        tpos = (118613, 65922, 241664)
        before = self.slots(tpos, 50, TIMER_CLASS)
        for y, z in [(57000, 227728), (57000, 227900), (57100, 227600)]:
            self.place(116736, y, z, 0.0, settle=120)
            self.tap("E", 2, 70)
            if RUNEBLADE_RES in carried(self.mem):
                break
        self.link.wait_samples(150)
        after = self.slots(tpos, 50, TIMER_CLASS)
        items = carried(self.mem)
        self.note("blade", carried=[hex(x) for x in items], timer2331_before=before, timer2331_after=after)
        self.shot("blade")
        return blade_step_passed(items, before, after)

    def call(self) -> bool:
        """Call Arokh: E held past the 0.5 s Use window (a tap is Use only)."""
        a, me = self.arokh(), self.me()
        if not a:
            return False
        d0 = LD.dist_xz(a.pos, me.pos)
        self.hold("E", 60)
        for _ in range(40):
            self.link.wait_samples(25)
            a = self.arokh()
            if LD.dist_xz(a.pos, self.me().pos) < 3000 and LD.actor_words(self.mem, a.handle, (0x9C,)) == ["1"]:
                break
        d1 = LD.dist_xz(a.pos, self.me().pos)
        self.note("call", dist_before=round(d0), dist_after=round(d1))
        return d1 < 4000

    def remount(self) -> bool:
        """Walk into Arokh from his side (from behind she stops short of the mount distance)."""
        a = self.arokh()
        if a and LD.dist_xz(a.pos, self.me().pos) > 4000:
            self.call()
        return any(self.mount(side, "remount") for side in ("right", "left", "behind"))

    def rift(self) -> bool:
        """Mounted in the rift box 2318: the message box (screen 2), Cross, the save screen (5), the load of Rift World."""
        st = None
        for _ in range(3):
            self.place(294144, 100500, 137941, -1.5708, settle=60)
            st = self.link.wait(lambda s: s.screen == 2, 40, 0.05)
            if not st:
                self.hold("W", 40)
                st = self.link.wait(lambda s: s.screen == 2, 20, 0.05)
            if st:
                break
        self.note("rift", message_box=bool(st))
        if not st:
            return False
        self.link.wait_samples(10)
        self.send("0 pad tap CROSS")
        st = self.link.wait(lambda s: s.screen == 5, 60, 0.05)
        t0 = time.monotonic()
        while st and time.monotonic() - t0 < 90:
            time.sleep(4.0)
            if self.mem.status().screen != 5:
                break
            self.send("0 pad tap CROSS")

        def in_rift(s):
            me = LD.controlled(self.mem)
            return (
                s.gameplay
                and s.play_run >= 100
                and me is not None
                and abs(me.pos[2] - 319535) < 20000
                and me.pos[1] > 90000
            )

        arrived = self.link.wait(in_rift, 400)
        me = self.me()
        self.note("rift", save_screen=bool(st), arrived=bool(arrived), arrival=LD._r(me.pos) if me else None,
                  carried=[hex(x) for x in carried(self.mem)])  # fmt: skip
        self.shot("arrived")
        return bool(arrived)

    def fly(self) -> bool:
        """The tunnel, the hall, the shaft and the causeway to the altar: W, SPACE and the mouse steered on the position."""
        keys, i, last, t0 = LD.KeyState(), 0, None, time.monotonic()
        while self.link.alive() and time.monotonic() - t0 < 120 and i < len(TUNNEL):
            st, me = self.mem.status(), self.me()
            if me is None or me.cls != LD.AROKH_CLASS:
                self.link.wait_samples(2)
                continue
            tx, ty, tz = TUNNEL[i]
            dh = LD.dist_xz(me.pos, (tx, tz))
            nxt = TUNNEL[i + 1] if i + 1 < len(TUNNEL) else None
            if dh < 1500 or (nxt and LD.dist_xz(me.pos, (nxt[0], nxt[2])) < LD.dist_xz((tx, tz), (nxt[0], nxt[2]))):
                i += 1
                continue
            err = LD.wrap(LD.yaw_to(me.pos, (tx, tz)) - me.yaw)
            rate = LD.wrap(me.yaw - last[1]) / (st.frame - last[0]) if last and st.frame > last[0] else 0.0
            last = (st.frame, me.yaw)
            dx = int(max(-120, min(120, 0.8 * (err - 22.0 * rate) / (self.ctl.rad_per_count or -1.1e-4))))
            pitch = -self.mem.f32(me.handle + 0x20 + 4 * 5)
            want_p = max(-0.35, min(0.35, math.atan2(ty - me.pos[1], max(dh, 1500))))
            cy = int(max(-80, min(80, (want_p - pitch) / 0.00073 * 0.5)))
            lines = keys.lines(["W", "SPACE"] if ty - me.pos[1] > 250 and pitch < want_p else ["W"])
            if dx or cy:
                lines.append(f"0 mouse {dx} {cy}")
            self.link.send(lines)
            self.link.wait_samples(2)
        self.link.send(keys.lines([]))
        self.note("fly", reached=i, of=len(TUNNEL), end=LD._r(self.me().pos))
        self.shot("altar")
        return i == len(TUNNEL)

    def heads(self) -> bool:
        """The four Dr. Evil heads by breath: placed facing the living head nearest the altar, from the altar side,
        3,300 to 4,800 units away and 600 to 1,300 above its origin, six breath taps per round."""

        def alive():
            return [a for a in LD.find_actors(self.mem, HEAD_CLASS) if (a.hp or 0) > 0]

        rounds, t0 = 0, time.monotonic()
        while alive() and rounds < 200 and time.monotonic() - t0 < 900:
            tgt = min(alive(), key=lambda a: LD.dist_xz(a.pos, ALTAR))
            x, z, yaw = spot_toward(tgt.pos, ALTAR, (3300, 4000, 4800, 3600)[rounds % 4])
            self.place(x, tgt.pos[1] + (900, 1300, 600, 1100)[(rounds // 4) % 4], z, yaw, settle=20)
            for _ in range(6):
                self.send("0 button tap 1")
                self.link.wait_samples(15)
            rounds += 1
            if rounds % 5 == 0:
                self.log(
                    step="heads", round=rounds, hp=[(LD._r(a.pos), a.hp) for a in LD.find_actors(self.mem, HEAD_CLASS)]
                )
        self.note(
            "heads",
            rounds=rounds,
            seconds=round(time.monotonic() - t0),
            hp=[a.hp for a in LD.find_actors(self.mem, HEAD_CLASS)],
        )
        return not alive()

    def ending(self) -> bool:
        """The end sequence, the outro and its credits, back to the title: screen changes logged, a shot every 30 s."""
        t0, seen, last, next_shot = time.monotonic(), [], None, 0.0
        while time.monotonic() - t0 < self.args.ending_seconds:
            s = self.mem.status()
            if (s.screen, s.gameplay) != last:
                last = (s.screen, s.gameplay)
                seen.append(
                    {"t": round(time.monotonic() - t0, 1), "screen": s.screen, "gameplay": s.gameplay, "f": s.frame}
                )
            if time.monotonic() - t0 >= next_shot:
                self.shot(f"end_{int(next_shot):03d}")
                next_shot += 30.0
            time.sleep(1.0)
        self.note("ending", screens=seen[:80])
        return True

    def run(self) -> dict:
        a = self.args
        for name in STEPS[STEPS.index(a.route_from) : STEPS.index(a.route_to) + 1]:
            t = time.monotonic()
            ok = getattr(self, name)()
            self.note(name, ok=bool(ok), seconds=round(time.monotonic() - t, 1))
            if not ok:
                self.res["failed_at"] = name
                break
            if a.save_each:
                self.send("0 key tap F5")
                self.link.wait_samples(150)
        return self.res


def mission_route(link, log, ctl, args) -> dict:  # pragma: no cover
    return Route(link, log, ctl, args).run()


LD.MISSIONS["route"] = mission_route


def split_args(argv: list[str]) -> tuple[list[str], dict]:
    """Take this script's own options out of argv (the rest goes to live_drive)."""
    own = {"from": "rune", "to": "ending", "ending-seconds": "420"}
    flags = {"save-each": False, "shots": False}
    out = []
    i = 0
    while i < len(argv):
        k = argv[i][2:] if argv[i].startswith("--") else None
        if k in own:
            own[k] = argv[i + 1]
            i += 2
            continue
        if k in flags:
            flags[k] = True
            i += 1
            continue
        out.append(argv[i])
        i += 1
    if own["from"] not in STEPS or own["to"] not in STEPS or STEPS.index(own["from"]) > STEPS.index(own["to"]):
        raise SystemExit(f"--from / --to: steps in this order: {' '.join(STEPS)}")
    return out, {**own, **flags}


if __name__ == "__main__":  # pragma: no cover
    rest, opts = split_args(sys.argv[1:])
    args = LD.parse_args([*rest, "idle"])
    args.mission = "route"
    args.route_from, args.route_to = opts["from"], opts["to"]
    args.ending_seconds, args.save_each, args.shots = float(opts["ending-seconds"]), opts["save-each"], opts["shots"]
    sys.exit(LD.run(args))
