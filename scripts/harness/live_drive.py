"""Closed-loop play-test driver: reads Rynn's (and a target's) state from the running game and steers the input.

An input script is open loop: it cannot face a giant that walks, step back when he reaches, or follow a stair whose
speed changes with the frame rate. This tool keeps a scripted run (DRAKAN_INPUT_SCRIPT) and adds live input
(DRAKAN_INPUT_LIVE, harness only, see runtime/input/include/input/Script.h): every few samples it reads guest RAM out
of the game process, decides, and appends input lines of the script grammar to the live command file.

    python scripts/harness/live_drive.py --name giant1 --exe build/harness/issue5-loop-dist/drakan.exe \\
        --mods <pack> --level "OotF 07 - Alwarren" --card <mc0> --recipe pre.txt --go ram_go \\
        --env DRAKAN_PLAYTEST_GOD=1 --every 1 fight --target-class 0x1003b --near 255000,476000 \\
        --counter-anims 0x502fd,0x502fc --danger-anims 0x503fc,0x502fa --opening-anims 0x1 --standoff 1950
    ... path --waypoints stair.json --axis x,z  (a list of [x, y, z] or {"p": [x, y, z], "r": radius, "ymin": y})
    ... approach --target-class 0x1001c --start-from behind --start-dist 2600   (walk into Arokh until mounted)
    python scripts/harness/live_drive.py summary <run dir> [act,ga.0,grab,hp]    (the log rows where those change)

Missions are functions (link, log, controller, args) in MISSIONS; a private script can import this module, add its
own (for example a sequence of path, approach and state waits) and call run().

How a run goes: the game starts with --recipe (setup: quick load, placement, equip; it must not quit), the driver
waits until that recipe has written the RAM dump named by --go (`<n> dump ram_go`: a state, not a time), runs the
mission, sends --post lines (for example "0 key tap F5;120 dump ram_end"), then `quit`. Every decision is logged to
<run>/drive.jsonl (sample number, positions, facing error, hit points, action), the evidence for pass or fail.

Reading memory: the runtime writes the host address of a small status block to <command file>.status; the block holds
the guest RAM address, the input sample number, gameplay state and how many live lines were applied (Windows
ReadProcessMemory on the process this tool started; nothing is written into the game). Engine layout, PAL SCES-500.06:
game context *0x5565d4, controlled actor ctx+0x4b0 (position +0x14, matrix +0x20, yaw = atan2(m[2], m[0]), the `place`
convention: yaw 0 faces -z, a vector (dx, dz) has yaw atan2(-dx, -dz)), registry ctx+0x7c / count ctx+0x84, class
records ctx+0x320, NPC hit points data+0x24c.

The control logic (geometry, turn controller, path follower, fight policy, grab detector) is pure and unit tested with
synthetic data (scripts/tests/test_live_drive.py).
"""

from __future__ import annotations

import argparse
import ctypes
import dataclasses
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time
from collections.abc import Callable, Iterable, Sequence
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Guest RAM the game may use: the retail 32 MB plus the extended heap above it (ps2_memory.h PS2_RAM_SIZE). RAM dumps
# hold the retail part only (RETAIL_RAM_SIZE).
RAM_SIZE = 0x10000000
RETAIL_RAM_SIZE = 0x2000000
GAME_CTX = 0x5565D4
ENTITY_VTABLE = 0x51B8B0
RYNN_CLASS = 0x10009
AROKH_CLASS = 0x1001C

# ---------------------------------------------------------------------------------------------------------------------
# Guest memory


class GuestMemory:
    """Little-endian guest RAM reads; addresses are guest addresses (masked to the guest RAM)."""

    def read(self, addr: int, size: int) -> bytes:
        raise NotImplementedError

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def i32(self, addr: int) -> int:
        return struct.unpack("<i", self.read(addr, 4))[0]

    def u16(self, addr: int) -> int:
        return struct.unpack("<H", self.read(addr, 2))[0]

    def f32(self, addr: int) -> float:
        return struct.unpack("<f", self.read(addr, 4))[0]

    def vec3(self, addr: int) -> tuple[float, float, float]:
        return struct.unpack("<3f", self.read(addr, 12))


class BytesMemory(GuestMemory):
    """A RAM dump (`dump ram_<label>`) or synthetic bytes."""

    def __init__(self, data: bytes | bytearray):
        self.data = data

    def read(self, addr: int, size: int) -> bytes:
        a = addr & (RAM_SIZE - 1)
        out = bytes(self.data[a : a + size])
        return out if len(out) == size else out + bytes(size - len(out))


@dataclasses.dataclass
class LiveStatus:
    rdram: int
    ram_size: int
    frame: int
    play_run: int
    screen: int
    gameplay: bool
    live_applied: int
    seq: int

    FORMAT = "<16sQQqqiiQQ"

    @classmethod
    def unpack(cls, raw: bytes) -> LiveStatus:
        magic, rdram, size, frame, play, screen, gameplay, applied, seq = struct.unpack_from(cls.FORMAT, raw)
        if not magic.startswith(b"DRAKAN_LIVE_V1"):
            raise ValueError("not a live status block")
        return cls(rdram, size, frame, play, screen, bool(gameplay), applied, seq)


class ProcessMemory(GuestMemory):  # pragma: no cover - needs a running game
    """Guest RAM of a running drakan.exe through ReadProcessMemory (Windows; read only)."""

    def __init__(self, pid: int, status_addr: int):
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._rpm = k32.ReadProcessMemory
        self._rpm.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        k32.OpenProcess.restype = ctypes.c_void_p
        self._handle = k32.OpenProcess(0x0010 | 0x1000, False, pid)  # VM_READ | QUERY_LIMITED_INFORMATION
        if not self._handle:
            raise OSError(ctypes.get_last_error(), "OpenProcess")
        self.status_addr = status_addr
        self.base = 0  # known after the first input sample (LiveLink.connect waits for it)

    def _host(self, addr: int, size: int) -> bytes:
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t(0)
        if not self._rpm(self._handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)) or got.value != size:
            raise OSError(ctypes.get_last_error(), f"ReadProcessMemory 0x{addr:x}")
        return buf.raw

    def status(self) -> LiveStatus:
        return LiveStatus.unpack(self._host(self.status_addr, struct.calcsize(LiveStatus.FORMAT)))

    def read(self, addr: int, size: int) -> bytes:
        if not self.base:
            self.base = self.status().rdram
        try:
            return self._host(self.base + (addr & (RAM_SIZE - 1)), size)
        except OSError:  # extended RAM the game has not used yet: no host pages, reads as zero (as in the game)
            if (addr & (RAM_SIZE - 1)) < RETAIL_RAM_SIZE:
                raise
            return bytes(size)


# ---------------------------------------------------------------------------------------------------------------------
# Engine readers


def _valid(a: int, n: int = 4) -> bool:
    return 0x100000 <= a <= RAM_SIZE - n


@dataclasses.dataclass
class Actor:
    handle: int
    cls: int | None
    pos: tuple[float, float, float]
    yaw: float
    hp: int | None = None

    def xz(self) -> tuple[float, float]:
        return self.pos[0], self.pos[2]


def resource_class(mem: GuestMemory, handle: int) -> int | None:
    ctx = mem.u32(GAME_CTX)
    res = mem.u32(ctx + 0x320) if _valid(ctx, 0x324) else 0
    if not _valid(handle, 0x14) or not _valid(res, 0x14):
        return None
    ix = mem.u16(handle + 0x10)
    if ix == 0xFFFF or ix > mem.u32(res + 0x10):
        return None
    rec = mem.u32(res + 8) + ix * 0x24
    return mem.u32(rec + 8) if _valid(rec, 0x24) else None


def actor_hp(mem: GuestMemory, handle: int) -> int | None:
    """Hit points of an NPC (data+0x24c, when data+0x23c points back at the handle)."""
    holder = mem.u32(handle + 0xFC) if _valid(handle, 0x100) else 0
    data = mem.u32(holder + 0x10) if _valid(holder, 0x14) else 0
    if not _valid(data, 0x250) or mem.u32(data + 0x23C) != handle:
        return None
    return mem.i32(data + 0x24C)


def actor_words(mem: GuestMemory, handle: int, offsets: Sequence[int]) -> list[str]:
    """Words of an actor's data object (handle+0xfc -> +0x10), hex, for logging state flags."""
    holder = mem.u32(handle + 0xFC) if _valid(handle, 0x100) else 0
    data = mem.u32(holder + 0x10) if _valid(holder, 0x14) else 0
    if not _valid(data, max(offsets) + 4):
        return []
    return [f"{mem.u32(data + o):x}" for o in offsets]


def anim(mem: GuestMemory, handle: int, layer: int = 0) -> tuple[int, float] | None:
    """Animation id and time of one layer of an actor's model (model = handle+0x50; record = *(model+0xe8) +
    s16 *(model+0xdc)[layer] * 0x80: +0x50 id, +0x58 time), as the input glue's [in-anim] log reads Rynn's."""
    mdl = mem.u32(handle + 0x50) if _valid(handle, 0x54) else 0
    if not _valid(mdl, 0xEC):
        return None
    idx, recs = mem.u32(mdl + 0xDC), mem.u32(mdl + 0xE8)
    if not _valid(idx, 2 * layer + 2) or not _valid(recs, 0x80):
        return None
    rec = recs + struct.unpack("<h", mem.read(idx + 2 * layer, 2))[0] * 0x80
    return (mem.u32(rec + 0x50), round(mem.f32(rec + 0x58), 2)) if _valid(rec, 0x5C) else None


def read_actor(mem: GuestMemory, handle: int, with_hp: bool = False) -> Actor | None:
    if not _valid(handle, 0x100):
        return None
    m = struct.unpack("<9f", mem.read(handle + 0x20, 36))
    return Actor(
        handle,
        resource_class(mem, handle),
        mem.vec3(handle + 0x14),
        math.atan2(m[2], m[0]),
        actor_hp(mem, handle) if with_hp else None,
    )


def controlled(mem: GuestMemory) -> Actor | None:
    ctx = mem.u32(GAME_CTX)
    return read_actor(mem, mem.u32(ctx + 0x4B0)) if _valid(ctx, 0x4B8) else None


def find_actors(mem: GuestMemory, cls: int) -> list[Actor]:
    """Registry objects of an engine class (slots change after a load, so targets are found by class)."""
    ctx = mem.u32(GAME_CTX)
    if not _valid(ctx, 0x400):
        return []
    table, count = mem.u32(ctx + 0x7C), min(mem.u32(ctx + 0x84) + 1, 16384)
    if not _valid(table, count * 8):
        return []
    raw = mem.read(table, count * 8)
    out = []
    for s in range(count):
        h = struct.unpack_from("<I", raw, s * 8)[0]
        if _valid(h, 0x100) and mem.u32(h) == ENTITY_VTABLE and resource_class(mem, h) == cls:
            a = read_actor(mem, h, with_hp=True)
            if a:
                out.append(a)
    return out


def nearest(actors: Iterable[Actor], xz: tuple[float, float]) -> Actor | None:
    return min(actors, key=lambda a: math.hypot(a.pos[0] - xz[0], a.pos[2] - xz[1]), default=None)


# ---------------------------------------------------------------------------------------------------------------------
# Geometry and control (pure)


def wrap(a: float) -> float:
    return (a + math.pi) % (2 * math.pi) - math.pi


def yaw_to(src: Sequence[float], dst: Sequence[float]) -> float:
    """`place` yaw that faces from src to dst (x, y, z or x, z sequences)."""
    sx, sz = (src[0], src[2]) if len(src) == 3 else (src[0], src[1])
    dx, dz = (dst[0], dst[2]) if len(dst) == 3 else (dst[0], dst[1])
    return math.atan2(-(dx - sx), -(dz - sz))


def dist_xz(a: Sequence[float], b: Sequence[float]) -> float:
    ax, az = (a[0], a[2]) if len(a) == 3 else (a[0], a[1])
    bx, bz = (b[0], b[2]) if len(b) == 3 else (b[0], b[1])
    return math.hypot(bx - ax, bz - az)


@dataclasses.dataclass
class TurnController:
    """Mouse x counts for a yaw error. In direct look mode the yaw moves by `rad_per_count` per count (sign included:
    measured by calibrate()). Proportional, limited per sample, so the driver closes the loop every step."""

    rad_per_count: float = -0.002
    gain: float = 0.8
    max_counts: int = 120
    deadband: float = math.radians(1.5)

    def counts(self, error: float) -> int:
        """error = wanted yaw - current yaw (wrapped)."""
        error = wrap(error)
        if abs(error) < self.deadband or self.rad_per_count == 0:
            return 0
        c = self.gain * error / self.rad_per_count
        return int(max(-self.max_counts, min(self.max_counts, round(c))))

    @staticmethod
    def calibrate(yaw_before: float, yaw_after: float, counts: int) -> float | None:
        if counts == 0:
            return None
        d = wrap(yaw_after - yaw_before)
        return d / counts if abs(d) > 1e-3 else None


@dataclasses.dataclass
class Waypoint:
    p: tuple[float, float, float]
    r: float = 300.0
    ymin: float | None = None  # passed only at or above this height (a stair landing)


class PathFollower:
    """Waypoints in order; the current one is passed when Rynn is within its radius (xz). `steer` gives the yaw toward
    a point a little past the projection on the current leg (pure pursuit), so a curved stair is followed from its
    middle instead of cutting the inner edge."""

    def __init__(self, points: Sequence[Waypoint], lookahead: float = 250.0):
        if not points:
            raise ValueError("no waypoints")
        self.points = list(points)
        self.index = 0
        self.lookahead = lookahead

    @classmethod
    def load(cls, path: Path, lookahead: float = 250.0, radius: float = 300.0) -> PathFollower:
        pts = []
        for item in json.loads(path.read_text(encoding="utf-8")):
            if isinstance(item, dict):
                ymin = item.get("ymin")
                pts.append(Waypoint(tuple(item["p"]), float(item.get("r", radius)), None if ymin is None else ymin))
            else:
                pts.append(Waypoint(tuple(item), radius))
        return cls(pts, lookahead)

    @property
    def done(self) -> bool:
        return self.index >= len(self.points)

    def update(self, pos: Sequence[float]) -> None:
        while not self.done:
            w = self.points[self.index]
            if dist_xz(pos, w.p) > w.r or (w.ymin is not None and pos[1] < w.ymin):
                break
            self.index += 1

    def aim_point(self, pos: Sequence[float]) -> tuple[float, float]:
        cur = self.points[min(self.index, len(self.points) - 1)].p
        prev = self.points[self.index - 1].p if 0 < self.index <= len(self.points) - 1 else None
        if prev is None:
            return cur[0], cur[2]
        ax, az, bx, bz = prev[0], prev[2], cur[0], cur[2]
        lx, lz = bx - ax, bz - az
        length = math.hypot(lx, lz)
        if length < 1e-6:
            return bx, bz
        t = ((pos[0] - ax) * lx + (pos[2] - az) * lz) / (length * length)
        s = min(1.0, max(0.0, t) + self.lookahead / length)
        return ax + lx * s, az + lz * s

    def cross_track(self, pos: Sequence[float]) -> float:
        """Signed distance from the current leg (positive on the +z side of a leg that runs along +x)."""
        if self.done or self.index == 0:
            return 0.0
        a, b = self.points[self.index - 1].p, self.points[self.index].p
        lx, lz = b[0] - a[0], b[2] - a[2]
        length = math.hypot(lx, lz) or 1.0
        return (lx * (pos[2] - a[2]) - lz * (pos[0] - a[0])) / length


def forward_of(yaw: float) -> tuple[float, float]:
    """xz unit facing vector of a yaw (the `place` convention: 0 faces -z)."""
    return -math.sin(yaw), -math.cos(yaw)


def relative_start(pos: Sequence[float], yaw: float, side: str, dist: float) -> tuple[float, float, float]:
    """A point `dist` from an actor on one side of its own facing, and the yaw that faces the actor from there."""
    fx, fz = forward_of(yaw)
    rx, rz = right_of(yaw)
    ux, uz = {"front": (fx, fz), "behind": (-fx, -fz), "right": (rx, rz), "left": (-rx, -rz)}[side]
    x, z = pos[0] + ux * dist, pos[2] + uz * dist
    return x, z, yaw_to((x, z), (pos[0], pos[2]))


SIDE_BEARING = {"front": 0.0, "behind": math.pi, "right": math.pi / 2, "left": -math.pi / 2}


def bearing_from(pos: Sequence[float], yaw: float, other: Sequence[float]) -> float:
    """Angle of `other` around an actor relative to its facing: 0 in front, +-pi behind, +pi/2 to its right."""
    fx, fz = forward_of(yaw)
    rx, rz = right_of(yaw)
    dx, dz = other[0] - pos[0], other[2] - pos[2]
    return math.atan2(dx * rx + dz * rz, dx * fx + dz * fz)


def right_of(yaw: float) -> tuple[float, float]:
    """xz unit vector to the right of the facing (mouse right / strafe D turn or move that way)."""
    return math.cos(yaw), -math.sin(yaw)


def unstick_key(pos: Sequence[float], yaw: float, axis: Sequence[float]) -> str:
    """Strafe key that moves away from a stair axis (out of the inner edge)."""
    rx, rz = right_of(yaw)
    ox, oz = pos[0] - axis[0], pos[2] - axis[1]
    return "D" if ox * rx + oz * rz > 0 else "A"


class GrabDetector:
    """Rynn is held when she rises well above the floor she stood on or moves fast without being driven there.
    `update` takes her position each step; `floor` is the lowest recent height (on foot she does not climb in a fight)."""

    def __init__(self, rise: float = 350.0, jump: float = 900.0, window: int = 40):
        self.rise, self.jump, self.window = rise, jump, window
        self.heights: list[float] = []
        self.last: tuple[float, float, float] | None = None
        self.grabbed = False

    def update(self, pos: Sequence[float], commanded_step: float = 0.0) -> bool:
        self.heights = [*self.heights, pos[1]][-self.window :]
        floor = min(self.heights)
        moved = dist_xz(self.last, pos) if self.last else 0.0
        self.last = (pos[0], pos[1], pos[2])
        self.grabbed = pos[1] - floor > self.rise or moved - commanded_step > self.jump
        return self.grabbed


@dataclasses.dataclass
class FightPolicy:
    """Hit and step back, driven by distance and the target's animation. Distances in xz units, Rynn to its origin.

    * `counter` animations (a reach whose arm comes into sword range): strike into it (not when already grabbed);
    * `danger` animations (a grab starting) and a detected grab: back off until `safe` away;
    * closer than `too_close`: back off;
    * facing more than `face_tol` off: turn first;
    * the target in an `opening` animation (standing, staggered), or `opening` empty: close to `strike_max` and strike
      every `strike_every` decisions;
    * otherwise (it walks or turns): keep `standoff` (back off inside it, close in beyond it plus `slack`)."""

    strike_max: float = 1400.0
    too_close: float = 1150.0
    safe: float = 2300.0
    standoff: float = 2200.0
    slack: float = 500.0
    face_tol: float = math.radians(12)
    strike_every: int = 8
    danger: frozenset = frozenset()
    opening: frozenset = frozenset()
    counter: frozenset = frozenset()

    def _strike(self) -> str:
        if self.since_strike >= self.strike_every:
            self.since_strike = 0
            return "strike"
        return "face"

    def __post_init__(self):
        self.fleeing = False
        self.since_strike = 1 << 30

    def decide(self, dist: float, face_error: float, grabbed: bool, anim: int | None = None) -> str:
        """One of: retreat, approach, strike, face."""
        self.since_strike += 1
        if not grabbed and anim is not None and anim in self.counter and not self.fleeing:
            if abs(wrap(face_error)) > self.face_tol:
                return "face"
            return self._strike()
        if grabbed or (anim is not None and anim in self.danger):
            self.fleeing = True
        if self.fleeing and dist >= self.safe:
            self.fleeing = False
        if self.fleeing or dist < self.too_close:
            return "retreat"
        if abs(wrap(face_error)) > self.face_tol:
            return "face"
        if not self.opening or (anim is not None and anim in self.opening):
            return "approach" if dist > self.strike_max else self._strike()
        if dist < self.standoff:
            return "retreat"
        if dist > self.standoff + self.slack:
            return "approach"
        return "face"


# ---------------------------------------------------------------------------------------------------------------------
# Live link and driver


class KeyState:
    """Held keys: only the changes are sent (`key down` / `key up`)."""

    def __init__(self):
        self.held: set[str] = set()

    def lines(self, want: Iterable[str]) -> list[str]:
        want = set(want)
        out = [f"0 key up {k}" for k in sorted(self.held - want)] + [
            f"0 key down {k}" for k in sorted(want - self.held)
        ]
        self.held = want
        return out


class LiveLink:  # pragma: no cover - needs a running game
    def __init__(self, cmd_file: Path, proc: subprocess.Popen):
        self.cmd_file = cmd_file
        self.proc = proc
        self.mem: ProcessMemory | None = None
        self.sent = 0

    def connect(self, timeout: float = 120.0) -> ProcessMemory:
        status = Path(str(self.cmd_file) + ".status")
        end = time.monotonic() + timeout
        while time.monotonic() < end and self.proc.poll() is None:
            if status.exists():
                text = status.read_text().split()
                if len(text) == 2:
                    if self.mem is None:
                        self.mem = ProcessMemory(self.proc.pid, int(text[1], 16))
                    if self.mem.status().rdram:
                        return self.mem
            time.sleep(0.05)
        raise RuntimeError("no live status block (an executable without DRAKAN_INPUT_LIVE?)")

    def send(self, lines: Sequence[str]) -> None:
        if not lines:
            return
        with self.cmd_file.open("a", encoding="utf-8", newline="\n") as f:
            f.write("".join(line + "\n" for line in lines))
        self.sent += sum(1 for line in lines if line.strip() and not line.lstrip().startswith("#"))

    def alive(self) -> bool:
        return self.proc.poll() is None

    def wait(self, pred: Callable[[LiveStatus], bool], timeout: float, poll: float = 0.02) -> LiveStatus | None:
        end = time.monotonic() + timeout
        while time.monotonic() < end and self.alive():
            st = self.mem.status()
            if pred(st):
                return st
            time.sleep(poll)
        return None

    def wait_samples(self, n: int, timeout: float = 30.0) -> LiveStatus | None:
        start = self.mem.status().frame
        return self.wait(lambda s: s.frame >= start + n, timeout)

    def wait_applied(self, timeout: float = 10.0) -> LiveStatus | None:
        return self.wait(lambda s: s.live_applied >= self.sent, timeout)


class Logger:
    def __init__(self, path: Path):
        self.f = path.open("w", encoding="utf-8")

    def __call__(self, **row) -> None:
        self.f.write(json.dumps(row, separators=(",", ":")) + "\n")
        self.f.flush()


def _r(v: Sequence[float]) -> list[int]:
    return [round(x) for x in v]


def calibrate_turn(link: LiveLink, counts: int = 150) -> float:  # pragma: no cover
    """Yaw change per mouse count (sign included), measured on the controlled actor."""
    before = controlled(link.mem)
    link.send([f"0 mouse {counts} 0"])
    link.wait_applied()
    link.wait_samples(4)
    after = controlled(link.mem)
    k = TurnController.calibrate(before.yaw, after.yaw, counts) if before and after else None
    link.send([f"0 mouse {-counts} 0"])
    link.wait_applied()
    link.wait_samples(2)
    return k if k is not None else -0.002


def mission_face(link, log, ctl, args) -> dict:  # pragma: no cover
    """Turn toward a target actor and hold the facing for --samples samples (tests the turn loop)."""
    target = _target(link.mem, args)
    errs = []
    end = link.mem.status().frame + args.samples
    while link.alive() and link.mem.status().frame < end:
        me = controlled(link.mem)
        t = read_actor(link.mem, target.handle)
        err = wrap(yaw_to(me.pos, t.pos) - me.yaw)
        dx = ctl.counts(err) if link.mem.status().gameplay else 0
        link.send([f"0 mouse {dx} 0"] if dx else [])
        errs.append(abs(math.degrees(err)))
        log(
            f=link.mem.status().frame,
            me=_r(me.pos),
            yaw=round(me.yaw, 3),
            tgt=_r(t.pos),
            err=round(math.degrees(err), 1),
        )
        link.wait_samples(args.every)
    tail = errs[len(errs) // 2 :] or [999]
    return {"mission": "face", "median_abs_err_deg_second_half": sorted(tail)[len(tail) // 2]}


def _ids(text: str | None) -> list[int]:
    return [int(v, 0) for v in (text or "").split(",") if v.strip()]


def _target(mem: GuestMemory, args) -> Actor:
    near = tuple(float(v) for v in args.near.split(",")) if args.near else controlled(mem).xz()
    t = nearest(find_actors(mem, int(args.target_class, 0)), near)
    if t is None:
        raise RuntimeError(f"no actor of class {args.target_class}")
    return t


def mission_fight(link, log, ctl, args) -> dict:  # pragma: no cover
    """Kill a target NPC: face it, strike in reach, step back when it closes or grabs."""
    target = _target(link.mem, args)
    policy = FightPolicy(
        strike_max=args.strike_max,
        too_close=args.too_close,
        safe=args.safe,
        standoff=args.standoff,
        strike_every=args.strike_every,
        danger=frozenset(_ids(args.danger_anims)),
        counter=frozenset(_ids(args.counter_anims)),
        opening=frozenset(_ids(args.opening_anims)),
    )
    grab = GrabDetector(rise=args.grab_rise)
    keys = KeyState()
    hp0 = target.hp
    result = {"mission": "fight", "target": hex(target.handle), "hp_start": hp0, "grabs": 0, "strikes": 0}
    was_grabbed = False
    last_action = ""
    next_dump = 0
    start = time.monotonic()
    while link.alive() and time.monotonic() - start < args.seconds:
        st = link.mem.status()
        me = controlled(link.mem)
        t = read_actor(link.mem, target.handle, with_hp=True)
        if me is None or t is None:
            link.wait_samples(args.every)
            continue
        if t.hp is not None and t.hp <= 0:
            result["killed_at_frame"] = st.frame
            break
        d = dist_xz(me.pos, t.pos)
        err = wrap(yaw_to(me.pos, t.pos) - me.yaw)
        g = grab.update(me.pos, commanded_step=200.0)
        lines = []
        if g and not was_grabbed:
            result["grabs"] += 1
            lines.append(f"0 dump ram_grab{result['grabs']}")
        was_grabbed = g
        if args.dump_every and st.frame >= next_dump:
            lines.append(f"0 dump ram_f{st.frame}")
            next_dump = st.frame + args.dump_every
        ga = anim(link.mem, t.handle)
        # without a GUI screen the driver keeps steering even when the input glue does not count the frame as gameplay
        # (a grab or a knock-down runs without the on-foot camera look; --strict-gameplay holds then)
        active = st.gameplay or (st.screen < 0 and not args.strict_gameplay)
        action = policy.decide(d, err, g, ga[0] if ga else None) if active else "hold"
        want = {"retreat": args.retreat_keys.split(","), "approach": ["W"]}.get(action, [])
        if args.orbit_key and action != "retreat":
            want = [*want, args.orbit_key]  # circle it while facing it: it has to keep turning
        dx = ctl.counts(err) if active else 0
        if args.stay:
            want = []  # hold the spot: turn and strike only
        if args.flee_run and action == "retreat" and active and not args.stay:
            # turn the back to it at once (direct look: counts are radians) and run
            want = ["W"]
            dx = round(wrap(err + math.pi) / ctl.rad_per_count)
        lines += keys.lines(want)
        if dx:
            lines.append(f"0 mouse {dx} 0")
        if args.dodge_key and action == "retreat" and last_action != "retreat":
            lines.append(f"0 key tap {args.dodge_key}")  # once as the backing off starts (a jump)
        last_action = action
        if action == "strike":
            lines.append("0 button tap 1")
            result["strikes"] += 1
        link.send(lines)
        log(
            f=st.frame,
            play=st.play_run,
            scr=st.screen,
            me=_r(me.pos),
            yaw=round(me.yaw, 3),
            tgt=_r(t.pos),
            d=round(d),
            err=round(math.degrees(err), 1),
            hp=t.hp,
            grab=g,
            act=action,
            ga=[hex(ga[0]), ga[1]] if ga else None,
            cls=me.cls,
            st=actor_words(link.mem, me.handle, (0x90, 0x94, 0xC4, 0xE90, 0xAB0)),
        )
        link.wait_samples(args.every)
    link.send(keys.lines([]))
    t = read_actor(link.mem, target.handle, with_hp=True)
    result["hp_end"] = t.hp if t else None
    result["killed"] = bool(result.get("killed_at_frame")) or (t is not None and t.hp is not None and t.hp <= 0)
    return result


def mission_path(link, log, ctl, args) -> dict:  # pragma: no cover
    """Walk a waypoint path (W held, facing steered every step); slows (walk key) on tight turns."""
    path = PathFollower.load(Path(args.waypoints), args.lookahead, args.radius)
    axis = tuple(float(v) for v in args.axis.split(",")) if args.axis else None
    result_stuck: list[list[int]] = []
    keys = KeyState()
    best = {"index": 0, "y": -1e9}
    start = time.monotonic()
    stuck_at, stuck_n = None, 0
    while link.alive() and time.monotonic() - start < args.seconds and not path.done:
        st = link.mem.status()
        me = controlled(link.mem)
        if me is None:
            link.wait_samples(args.every)
            continue
        path.update(me.pos)
        if path.done:
            break
        ax, az = path.aim_point(me.pos)
        err = wrap(yaw_to(me.pos, (ax, az)) - me.yaw)
        dx = ctl.counts(err)
        # walk while the facing is far off (do not run off the tread), run when lined up
        want = ["W"] if abs(err) < math.radians(args.run_tol) else (["W", args.walk_key] if args.walk_key else [])
        if stuck_at and dist_xz(stuck_at, me.pos) < 30:
            stuck_n += 1
        else:
            stuck_at, stuck_n = me.pos, 0
        if stuck_n > args.stuck_steps:  # pressed into an edge: one step back and a nudge outward
            want = ["S", unstick_key(me.pos, me.yaw, axis) if axis else args.unstick_key]
            result_stuck.append(_r(me.pos))
            stuck_n = 0
        lines = keys.lines(want)
        if dx:
            lines.append(f"0 mouse {dx} 0")
        link.send(lines)
        if path.index > best["index"] or (path.index == best["index"] and me.pos[1] > best["y"]):
            best = {"index": path.index, "y": me.pos[1], "pos": _r(me.pos)}
        log(
            f=st.frame,
            play=st.play_run,
            me=_r(me.pos),
            yaw=round(me.yaw, 3),
            wp=path.index,
            aim=[round(ax), round(az)],
            err=round(math.degrees(err), 1),
            xt=round(path.cross_track(me.pos)),
            keys=sorted(keys.held),
        )
        link.wait_samples(args.every)
    link.send(keys.lines([]))
    me = controlled(link.mem)
    return {
        "mission": "path",
        "unsticks": result_stuck,
        "reached": path.done,
        "waypoints": len(path.points),
        "best": best,
        "end": _r(me.pos) if me else None,
    }


def mission_approach(link, log, ctl, args) -> dict:  # pragma: no cover
    """Walk at a target actor (W, facing steered) until the controlled actor's class changes (a mount) or time runs out;
    records the closest distance and where she stopped."""
    target = _target(link.mem, args)
    keys = KeyState()
    placements = 0
    if args.start_from:
        # start position relative to the target's own facing (test aid placement; the approach itself is walked). A
        # dragon may turn toward her after a placement, so it is repeated until she is on the asked side.
        want = SIDE_BEARING[args.start_from]
        for placements in range(1, 6):
            t = read_actor(link.mem, target.handle)
            x, z, yaw = relative_start(t.pos, t.yaw, args.start_from, args.start_dist)
            link.send([f"0 place {x:.0f} {t.pos[1] + args.start_dy:.0f} {z:.0f} {yaw:.4f}"])
            link.wait_applied()
            link.wait_samples(args.settle)
            t, me = read_actor(link.mem, target.handle), controlled(link.mem)
            if abs(wrap(bearing_from(t.pos, t.yaw, me.pos) - want)) < math.radians(30):
                break
        target = read_actor(link.mem, target.handle)
    me0 = controlled(link.mem)
    closest = (1e18, None, None)
    start = time.monotonic()
    mounted = None
    while link.alive() and time.monotonic() - start < args.seconds:
        st = link.mem.status()
        me = controlled(link.mem)
        t = read_actor(link.mem, target.handle)
        if me is None or t is None:
            link.wait_samples(args.every)
            continue
        if args.until_class and me.cls == int(args.until_class, 0):
            mounted = st.frame
            break
        d = dist_xz(me.pos, t.pos)
        if d < closest[0]:
            closest = (d, _r(me.pos), round(math.degrees(bearing_from(t.pos, t.yaw, me.pos))))
        err = wrap(yaw_to(me.pos, t.pos) - me.yaw)
        dx = ctl.counts(err)
        lines = keys.lines(["W"])
        if dx:
            lines.append(f"0 mouse {dx} 0")
        link.send(lines)
        log(
            f=st.frame,
            me=_r(me.pos),
            cls=me.cls,
            tgt=_r(t.pos),
            tyaw=round(t.yaw, 3),
            d=round(d),
            d3=round(math.dist(me.pos, t.pos)),
            brg=round(math.degrees(bearing_from(t.pos, t.yaw, me.pos))),
            err=round(math.degrees(err), 1),
            tst=actor_words(link.mem, t.handle, (0x94, 0x9C)),
            st=actor_words(link.mem, me.handle, (0x94, 0x504)),
        )
        link.wait_samples(args.every)
    link.send(keys.lines([]))
    return {
        "mission": "approach",
        "start": _r(me0.pos),
        "target": hex(target.handle),
        "target_pos": _r(target.pos),
        "target_yaw": round(target.yaw, 3),
        "start_from": args.start_from,
        "placements": placements,
        "bearing_deg": round(math.degrees(bearing_from(target.pos, target.yaw, me0.pos))),
        "closest": round(closest[0]),
        "closest_at": closest[1],
        "closest_bearing_deg": closest[2],
        "mounted_at_frame": mounted,
    }


def mission_idle(link, log, ctl, args) -> dict:  # pragma: no cover
    """No steering: the setup recipe and --post do the work (making a card)."""
    return {"mission": "idle"}


MISSIONS = {
    "idle": mission_idle,
    "face": mission_face,
    "fight": mission_fight,
    "path": mission_path,
    "approach": mission_approach,
}


def post_lines(text: str) -> list[str]:
    """Live lines of a post file: comments and blank lines dropped; `sync` is refused by the runtime, so it is here."""
    out = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.split()[1:2] == ["sync"]:
            raise ValueError(f"post lines cannot sync: {line!r}")
        out.append(line)
    return out


def build_env(args, out: Path, cmd_file: Path) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "PS2X_", "GSR_"))}
    env.update(
        {
            "DRAKAN_INI": str(out / "drakan.ini"),
            "DRAKAN_INPUT_INI": str(out / "drakan_input.ini"),
            "DRAKAN_NATIVE_OPTIONS_INI": str(out / "drakan_native_options.ini"),
            "DRAKAN_MULTIPLAYER_INI": str(out / "multiplayer.ini"),
            "DRAKAN_MEMCARD_DIR": str(out / "card/mc0"),
            "DRAKAN_LOG_DIR": str(out),
            "DRAKAN_SHOT_DIR": str(out),
            "DRAKAN_INPUT_SCRIPT": str(out / "recipe.txt"),
            "DRAKAN_INPUT_LIVE": str(cmd_file),
            "DRAKAN_AUTOPRESS": "0",
            "DRAKAN_INPUT_AUTOSTART": "0",
            "DRAKAN_NO_ACTIVATE": "1",
            "DRAKAN_PAUSE_UNFOCUSED": "0",
            "DRAKAN_AUDIO": "0",
            "DRAKAN_HD_MOVIES": "0",
            "DRAKAN_SKIP_MEMCARD_WARNING": "1",
            "PS2X_MAX_SECONDS": str(int(args.timeout)),
            "DRAKAN_DLSS": "off",
        }
    )
    if args.mods:
        env["DRAKAN_MODS"] = str(Path(args.mods).resolve())
    if args.level:
        env.update(
            {
                "DRAKAN_START_LEVEL": args.level,
                "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
                "DRAKAN_GAME": "d2",
                "DRAKAN_FAST_START": "1",
            }
        )
    for kv in args.env:
        k, sep, v = kv.partition("=")
        if not sep:
            raise SystemExit(f"--env {kv!r}: expected KEY=VALUE")
        env[k] = v
    return env


def prepare_run(args) -> tuple[Path, Path]:  # pragma: no cover
    out = (Path(args.out_root) / args.name).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    if args.card:
        shutil.copytree(args.card, out / "card/mc0")
    else:
        (out / "card/mc0").mkdir(parents=True)
    exe = Path(args.exe).resolve()
    for n in ["drakan.ini", "drakan_input.ini", "drakan_native_options.ini", "multiplayer.ini"]:
        if (exe.parent / n).exists():
            shutil.copyfile(exe.parent / n, out / n)
    sys.path.insert(0, str(ROOT / "scripts/harness"))
    from dual_wield_check import patch_ini

    ini = out / "drakan.ini"
    ini.write_text(
        patch_ini(
            ini.read_text(encoding="utf-8"),
            {
                "general": {"game": "d2" if args.level else "d1", "title_menu": "modern", "pause_when_unfocused": "0"},
                "video": {
                    "window_mode": "windowed",
                    "fullscreen": "0",
                    "window_width": "960",
                    "window_height": "540",
                    "dlss": "off",
                },
            },
        ),
        encoding="utf-8",
    )
    recipe = Path(args.recipe).read_text(encoding="utf-8") if args.recipe else "0 sync play 60\n"
    (out / "recipe.txt").write_text(recipe, encoding="utf-8")
    cmd_file = out / "live.txt"
    cmd_file.write_text("", encoding="utf-8")
    return out, cmd_file


def run(args) -> int:  # pragma: no cover
    out, cmd_file = prepare_run(args)
    env = build_env(args, out, cmd_file)
    exe = Path(args.exe).resolve()
    stdout = (out / "stdout.log").open("wb")
    proc = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=stdout, stderr=subprocess.STDOUT)
    link = LiveLink(cmd_file, proc)
    result: dict = {"run": args.name, "mission": args.mission}
    try:
        mem = link.connect()
        go = out / f"{args.go}.ram" if args.go else None
        ready = time.monotonic() + args.setup_timeout
        while link.alive() and time.monotonic() < ready:
            st = mem.status()
            if (go is None and st.play_run >= 60) or (
                go is not None and go.exists() and go.stat().st_size >= RETAIL_RAM_SIZE
            ):
                break
            time.sleep(0.05)
        else:
            raise RuntimeError("setup did not reach the go state")
        link.wait(lambda s: s.gameplay and s.play_run >= 2, 60)
        log = Logger(out / "drive.jsonl")
        ctl = TurnController(gain=args.turn_gain, max_counts=args.max_counts)
        if args.mission == "idle":
            ctl.rad_per_count = args.rad_per_count or -0.002
        else:
            ctl.rad_per_count = args.rad_per_count if args.rad_per_count else calibrate_turn(link)
        result["rad_per_count"] = ctl.rad_per_count
        result.update(MISSIONS[args.mission](link, log, ctl, args))
        post = [line.strip() for line in args.post.split(";") if line.strip()] if args.post else []
        if args.post_file:
            post += post_lines(Path(args.post_file).read_text(encoding="utf-8"))
        link.send(post)
        if post:
            last = max(int(line.split()[0]) for line in post)
            link.wait_samples(last + 30, last / 10 + 60)  # samples, not time: at least 10 samples a second
            link.wait_applied(30)
    except Exception as e:  # keep the evidence of a failed run
        result["error"] = f"{type(e).__name__}: {e}"
    finally:
        if link.alive():
            link.send(["0 quit"])
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                proc.kill()  # the process this tool started
                proc.wait()
        stdout.close()
    result["exit"] = proc.returncode
    text = (out / "stdout.log").read_bytes().decode("latin-1")
    result["clean_scripted_exit"] = proc.returncode == 0 and "[in] script quit" in text
    (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if "error" not in result else 1


def _field(row: dict, key: str):
    """`a` or `a.1` (an element of a list field)."""
    name, _, index = key.partition(".")
    v = row.get(name)
    if index and isinstance(v, list):
        return v[int(index)] if int(index) < len(v) else None
    return v


def transitions(rows: Iterable[dict], keys: Sequence[str]) -> list[dict]:
    """The log rows where any of `keys` changes (a readable trace of a drive.jsonl)."""
    out, last = [], None
    for r in rows:
        k = tuple(json.dumps(_field(r, key)) for key in keys)
        if k != last:
            out.append(r)
            last = k
    return out


def summarize(run_dir: Path, keys: Sequence[str]) -> None:  # pragma: no cover
    rows = [json.loads(line) for line in (run_dir / "drive.jsonl").read_text(encoding="utf-8").splitlines() if line]
    for r in transitions(rows, keys):
        print(json.dumps(r, separators=(",", ":")))


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="run folder under --out-root (replaced)")
    ap.add_argument("--out-root", default=str(ROOT / "build/harness/live-drive"))
    ap.add_argument("--exe", default=str(ROOT / "dist/DrakanPC/drakan.exe"))
    ap.add_argument("--mods", help="DRAKAN_MODS folder (a converted pack)")
    ap.add_argument("--level", help="direct start level (DRAKAN_START_LEVEL)")
    ap.add_argument("--card", help="mc0 folder copied in as the run's memory card")
    ap.add_argument("--recipe", help="setup input script (no quit); default: wait for gameplay")
    ap.add_argument("--go", help="RAM dump label the setup recipe writes when the mission may start (ram_go)")
    ap.add_argument("--post", help="live lines after the mission, ';' separated (e.g. '0 key tap F5;150 dump ram_end')")
    ap.add_argument("--post-file", help="more live lines after the mission (script grammar, no sync; '#' comments)")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the game")
    ap.add_argument("--timeout", type=float, default=600, help="PS2X_MAX_SECONDS")
    ap.add_argument("--setup-timeout", type=float, default=300)
    ap.add_argument("--seconds", type=float, default=120, help="mission time limit")
    ap.add_argument("--every", type=int, default=2, help="input samples between decisions")
    ap.add_argument("--turn-gain", type=float, default=0.8)
    ap.add_argument("--max-counts", type=int, default=120)
    ap.add_argument("--rad-per-count", type=float, default=0.0, help="skip calibration (direct look: about -0.002)")
    sub = ap.add_subparsers(dest="mission", required=True)
    for name in ("face", "fight", "approach"):
        p = sub.add_parser(name)
        p.add_argument("--target-class", required=True, help="engine class of the target (0x1003b war giant)")
        p.add_argument("--near", help="x,z: take the target of that class nearest this point (default: Rynn)")
        if name == "face":
            p.add_argument("--samples", type=int, default=300)
        if name == "fight":
            p.add_argument("--strike-max", type=float, default=1400)
            p.add_argument("--too-close", type=float, default=1150)
            p.add_argument("--safe", type=float, default=2300, help="back off this far after a danger animation")
            p.add_argument("--standoff", type=float, default=2200, help="distance kept while it walks or turns")
            p.add_argument("--strike-every", type=int, default=8, help="decisions between attack taps")
            p.add_argument("--grab-rise", type=float, default=350)
            p.add_argument("--dodge-key", default="", help="key tapped as backing off starts (SPACE: a jump)")
            p.add_argument("--stay", action="store_true", help="never walk: only turn to it and strike")
            p.add_argument("--flee-run", action="store_true", help="back off by turning away and running")
            p.add_argument("--orbit-key", default="", help="strafe key held except while backing off (circle it)")
            p.add_argument("--retreat-keys", default="S,A", help="keys held while backing off (D alone: a sidestep)")
            p.add_argument("--dump-every", type=int, default=0, help="RAM dump every n samples (0: only at grabs)")
            p.add_argument("--strict-gameplay", action="store_true", help="hold whenever the glue says not gameplay")
            p.add_argument("--danger-anims", default="", help="target animation ids (comma list) that mean back off")
            p.add_argument("--counter-anims", default="", help="target animation ids to strike into (its reach)")
            p.add_argument("--opening-anims", default="", help="target animation ids in which to close in and strike")
        if name == "approach":
            p.add_argument("--until-class", default=hex(AROKH_CLASS), help="stop when the controlled actor has it")
            p.add_argument("--start-from", choices=["behind", "front", "left", "right"], help="place Rynn first")
            p.add_argument("--start-dist", type=float, default=5000)
            p.add_argument("--start-dy", type=float, default=300, help="placement height above the target's origin")
            p.add_argument("--settle", type=int, default=150, help="samples after the placement")
    p = sub.add_parser("path")
    p.add_argument("--waypoints", required=True, help="JSON list of [x, y, z] or {p: [x, y, z], r: radius}")
    p.add_argument("--radius", type=float, default=300)
    p.add_argument("--lookahead", type=float, default=250)
    p.add_argument("--run-tol", type=float, default=25, help="degrees of facing error below which W is held")
    p.add_argument("--walk-key", default="", help="key held with W while the facing is off (empty: stop and turn)")
    p.add_argument("--stuck-steps", type=int, default=12)
    p.add_argument("--unstick-key", default="D")
    p.add_argument("--axis", help="x,z of a stair axis: unstick sideways away from it (A or D from the facing)")
    sub.add_parser("idle")
    return ap.parse_args(argv)


if __name__ == "__main__":
    if sys.argv[1:2] == ["summary"]:  # python live_drive.py summary <run dir> [keys,...]
        summarize(Path(sys.argv[2]), (sys.argv[3] if len(sys.argv) > 3 else "act,ga.0,grab,hp").split(","))
        sys.exit(0)
    sys.exit(run(parse_args()))
