"""CPU-only HUD packet/atlas evidence, with no initial-VRAM replay claim.

Extract complete CT32 IMAGE uploads and submitted sprites for one TBP. JSON
contains state, hashes and ranges, never image bytes. Optional raw dumps are
private local assets. Split GIF tags are rejected rather than guessed.
"""

import argparse
import collections
import hashlib
import json
import math
import struct
from fractions import Fraction
from pathlib import Path


def f32(value):
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


def events(data):
    if data[:12] != b"DRKGSCAP" + struct.pack("<I", 1):
        raise ValueError("Expected DRKGSCAP v1")
    p = 32
    while p < len(data):
        if p + 8 > len(data):
            raise ValueError("Truncated record header")
        kind, size = struct.unpack_from("<II", data, p)
        p += 8
        payload = data[p : p + size]
        p += size
        if len(payload) != size:
            raise ValueError("Truncated record")
        if kind == 1:
            yield ("frame", struct.unpack_from("<Q", payload)[0])
        if kind != 2:
            continue
        if len(payload) < 8:
            raise ValueError("Truncated GIF packet header")
        n = struct.unpack_from("<I", payload, 4)[0]
        packet = payload[8:]
        if n != len(packet):
            raise ValueError("GIF packet length mismatch")
        a = 0
        while a < len(packet):
            if a + 16 > len(packet):
                raise ValueError("Split GIF tag header unsupported")
            lo, hi = struct.unpack_from("<QQ", packet, a)
            a += 16
            loops, fmt, nr = lo & 32767, (lo >> 58) & 3, (lo >> 60) & 15 or 16
            count = loops * nr
            length = count * 16 if fmt == 0 else ((count + 1) // 2) * 16 if fmt == 1 else loops * 16
            if a + length > len(packet):
                raise ValueError("Split GIF tag payload unsupported")
            if fmt == 0 and lo & (1 << 46):
                yield ("reg", 0, (lo >> 47) & 2047, False, 0)
            if fmt >= 2:
                if fmt == 2:
                    yield ("image", packet[a : a + length])
            else:
                stride = 16 if fmt == 0 else 8
                for i in range(count):
                    desc = (hi >> (4 * (i % nr))) & 15
                    value = struct.unpack_from("<Q", packet, a + i * stride)[0]
                    high = struct.unpack_from("<Q", packet, a + i * stride + 8)[0] if fmt == 0 else 0
                    if desc == 14 and fmt == 0:
                        yield ("reg", high & 255, value, False, 0)
                    elif desc != 15:
                        yield ("reg", desc, value, fmt == 0, high)
            a += length


def collect(data, tbp):
    regs, uploads, sprites = {}, [], []
    rgba, st, uv, q = None, None, None, None
    frame, kick, pending, pair = None, 0, None, []
    primitives = collections.Counter()
    for event in events(data):
        if event[0] == "frame":
            frame = event[1]
            continue
        if event[0] == "image":
            if pending is not None:
                pending["raw"].extend(event[1])
            continue
        _, reg, value, packed, high = event
        regs[reg] = value
        if reg == 0:
            pair = []
        if reg == 1:
            rgba = (
                [value & 255, value >> 32 & 255, high & 255, high >> 32 & 255]
                if packed
                else [(value >> (8 * i)) & 255 for i in range(4)]
            )
            if not packed:
                q = f32(value >> 32)
        if reg == 2:
            st = [f32(value), f32(value >> 32)]
            if packed:
                q = f32(high)
        if reg == 3:
            uv = [(value & 16383) / 16, ((value >> (32 if packed else 16)) & 16383) / 16]
        if reg == 0x53:
            pending = None
            bb, tr = regs.get(0x50, 0), regs.get(0x52, 0)
            if value & 3 == 0 and (bb >> 32) & 16383 == tbp and (bb >> 56) & 63 == 0:
                w, h = tr & 4095, (tr >> 32) & 4095
                pending = dict(
                    after_kick=kick,
                    frame=frame,
                    tbp=tbp,
                    tbw=(bb >> 48) & 63,
                    trxpos=hex(regs.get(0x51, 0)),
                    width=w,
                    height=h,
                    raw=bytearray(),
                )
                uploads.append(pending)
        if reg not in (4, 5, 12, 13):
            continue
        kick += 1
        prim = regs.get(0, 0)
        control = prim if regs.get(0x1A, 1) & 1 else (prim & 7) | (regs.get(0x1B, 0) & ~7)
        kind = prim & 7
        primitives[kind] += 1
        if kind != 6:
            pair = []
            continue
        ctx = (control >> 9) & 1
        ofs, t0 = regs.get(0x18 + ctx, 0), regs.get(6 + ctx, 0)
        width, height = 1 << ((t0 >> 26) & 15), 1 << ((t0 >> 30) & 15)
        texcoord = uv if control & 256 else [st[0] / q * width, st[1] / q * height] if st and q else None
        vertex = dict(
            kick=kick,
            xy=[
                ((value & 65535) - (ofs & 65535)) / 16,
                (((value >> (32 if packed else 16)) & 65535) - ((ofs >> 32) & 65535)) / 16,
            ],
            uv=texcoord,
            rgba=rgba,
        )
        pair.append(vertex)
        if len(pair) != 2:
            continue
        suppressed = reg in (12, 13) or (packed and bool(high & (1 << 47)))
        if not suppressed and control & 16 and t0 & 16383 == tbp:
            sprites.append(
                dict(
                    frame=frame,
                    prim=hex(control),
                    tex0=hex(t0),
                    tex1=hex(regs.get(0x14 + ctx, 0)),
                    clamp=hex(regs.get(8 + ctx, 0)),
                    alpha=hex(regs.get(0x42 + ctx, 0)),
                    test=hex(regs.get(0x47 + ctx, 0)),
                    target=hex(regs.get(0x4C + ctx, 0)),
                    vertices=pair,
                )
            )
        pair = []
    for upload in uploads:
        raw = bytes(upload["raw"])
        expected = upload["width"] * upload["height"] * 4
        upload["received_bytes"] = len(raw)
        upload["complete"] = expected > 0 and len(raw) >= expected
        upload["raw"] = raw[:expected]
        upload["sha256"] = hashlib.sha256(upload["raw"]).hexdigest()
    return dict(vertex_kicks=kick, primitive_vertex_counts=dict(primitives), uploads=uploads, sprites=sprites)


def point_footprint(x0, x1, u0, u1, scale):
    """Current shader math: pixel-centre interpolation then floor; no GS oracle.

    Coordinates must already be in the raster's coordinate system. The native
    sprite model alone does not include HUD transforms or window resampling.
    """
    if x1 <= x0 or scale <= 0:
        raise ValueError("Positive sprite extent and scale required")
    first, end = math.ceil(x0 * scale - 0.5), math.ceil(x1 * scale - 0.5)
    return [math.floor(u0 + (u1 - u0) * ((p + 0.5) / scale - x0) / (x1 - x0)) for p in range(first, end)]


def sprite_axis_samples(x0, x1, u0, u1, *, pixel_center=0, scale=1, position_shift=0, uv_shift=0):
    """Exact affine fixture for one axis, returning (raster index, texcoord).

    Native GS pixel_center=0 is supported by hardware observations and PCSX2's
    software sprite rasterizer; D3D11 uses 1/2. Position shift is in native
    screen units, UV shift in texels. This is an independent arithmetic oracle
    for representable test cases, not a bit-exact implementation of GS DDA,
    STQ precision, filtering weights, HUD transforms or high-resolution GS.
    """
    x0, x1, u0, u1, pixel_center, scale, position_shift, uv_shift = (
        Fraction(str(v)) for v in (x0, x1, u0, u1, pixel_center, scale, position_shift, uv_shift)
    )
    if x1 < x0:
        x0, x1, u0, u1 = x1, x0, u1, u0
    if x1 == x0 or scale <= 0:
        raise ValueError("Positive sprite extent and scale required")
    first = math.ceil((x0 + position_shift) * scale - pixel_center)
    end = math.ceil((x1 + position_shift) * scale - pixel_center)
    return [
        (p, u0 + (u1 - u0) * ((p + pixel_center) / scale - position_shift - x0) / (x1 - x0) + uv_shift)
        for p in range(first, end)
    ]


def rectangle_border(raw, width, left, top, right, bottom):
    """Return RGBA values on a closed one-texel rectangular perimeter."""

    def pixel(x, y):
        p = (y * width + x) * 4
        if p < 0 or p + 4 > len(raw):
            raise ValueError("Border outside supplied image")
        return tuple(raw[p : p + 4])

    return {pixel(x, y) for x in range(left, right + 1) for y in (top, bottom)} | {
        pixel(x, y) for y in range(top, bottom + 1) for x in (left, right)
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("capture", type=Path)
    ap.add_argument("--tbp", type=int, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--private-dump-dir", type=Path)
    args = ap.parse_args()
    data = args.capture.read_bytes()
    result = collect(data, args.tbp)
    if args.private_dump_dir:
        args.private_dump_dir.mkdir(parents=True, exist_ok=True)
    for i, upload in enumerate(result["uploads"]):
        raw = upload.pop("raw")
        if args.private_dump_dir and upload["complete"]:
            (args.private_dump_dir / f"upload-{i}.rgba").write_bytes(raw)
    result["capture_sha256"] = hashlib.sha256(data).hexdigest()
    result["limits"] = (
        "Submitted sprites and complete host uploads only. No initial VRAM, raster replay, GS reference or live before/after proof."
    )
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
