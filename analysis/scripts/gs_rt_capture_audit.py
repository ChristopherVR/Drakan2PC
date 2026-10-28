"""Bounded, read-only GS texture/transfer audit; no initial VRAM or pixel proof.

Reports register state and submitted vertex UV ranges, not rasterized coverage.
Packed STQ and UV have different layouts from REGLIST; decode them explicitly.
Observed FRAMEs are candidates only: allocation/dirty ownership needs a live log.
"""

import argparse
import collections
import json
import math
import struct
from pathlib import Path


def f32(value):
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


def audit(path):
    data = path.read_bytes()
    if data[:8] != b"DRKGSCAP" or struct.unpack_from("<I", data, 8)[0] != 1:
        raise ValueError("Expected DRKGSCAP v1")
    regs, frames, samples, producers = {}, set(), {}, {}
    transfers = collections.Counter()
    st, uv, q, rgba = None, None, None, None
    counts = collections.Counter()

    def write(reg, value, packed=False, high=0):
        nonlocal st, uv, q, rgba
        regs[reg] = value
        if reg == 1 and not packed:
            q = f32(value >> 32)
        if reg == 1:
            rgba = (
                [value & 255, (value >> 32) & 255, high & 255, (high >> 32) & 255]
                if packed
                else [(value >> (8 * i)) & 255 for i in range(4)]
            )
        if reg == 2:
            st = (f32(value), f32(value >> 32))
            if packed:
                q = f32(high)
        if reg == 3:
            uv = ((value & 0x3FFF) / 16, ((value >> (32 if packed else 16)) & 0x3FFF) / 16)
        if reg == 0x53:
            transfers[(value & 3, hex(regs.get(0x50, 0)), hex(regs.get(0x51, 0)), hex(regs.get(0x52, 0)))] += 1
        if reg not in (4, 5):
            return
        counts["vertex_kicks"] += 1
        prim = regs.get(0)
        if prim is None:
            return
        control = prim if regs.get(0x1A, 1) & 1 else regs.get(0x1B, 0)
        ctx = (control >> 9) & 1
        frame = regs.get(0x4C + ctx)
        if frame is not None:
            frames.add((frame & 511, (frame >> 16) & 63, (frame >> 24) & 63))
            key = (
                frame,
                regs.get(0x40 + ctx),
                regs.get(0x4E + ctx),
                regs.get(0x47 + ctx),
                control & 0x3FF,
                regs.get(0x42 + ctx),
                regs.get(0x3D),
                regs.get(0x4A + ctx),
                regs.get(0x46),
            )
            prod = producers.setdefault(
                key,
                {
                    "vertex_kicks": 0,
                    "xy_min": [math.inf] * 2,
                    "xy_max": [-math.inf] * 2,
                    "z_min": 0xFFFFFFFF,
                    "z_max": 0,
                    "rgba_min": [255] * 4,
                    "rgba_max": [0] * 4,
                },
            )
            prod["vertex_kicks"] += 1
            prod.setdefault("first_kick", counts["vertex_kicks"])
            for name, address in [("alpha", 0x42 + ctx), ("fogcol", 0x3D), ("fba", 0x4A + ctx), ("colclamp", 0x46)]:
                if address in regs:
                    prod[name] = hex(regs[address])
            offset = regs.get(0x18 + ctx, 0)
            coord = [
                ((value & 0xFFFF) - (offset & 0xFFFF)) / 16,
                (((value >> (32 if packed else 16)) & 0xFFFF) - ((offset >> 32) & 0xFFFF)) / 16,
            ]
            z = (
                ((high >> 4) & 0xFFFFFF)
                if packed and reg == 4
                else (high & 0xFFFFFFFF)
                if packed
                else (value >> 32) & (0xFFFFFF if reg == 4 else 0xFFFFFFFF)
            )
            for i in range(2):
                prod["xy_min"][i] = min(prod["xy_min"][i], coord[i])
                prod["xy_max"][i] = max(prod["xy_max"][i], coord[i])
            prod["z_min"] = min(prod["z_min"], z)
            prod["z_max"] = max(prod["z_max"], z)
            if rgba:
                for i in range(4):
                    prod["rgba_min"][i] = min(prod["rgba_min"][i], rgba[i])
                    prod["rgba_max"][i] = max(prod["rgba_max"][i], rgba[i])
        if not control & 16 or 6 + ctx not in regs:
            return
        t0 = regs[6 + ctx]
        clamp = regs.get(8 + ctx, 0)
        t1 = regs.get(0x14 + ctx, 0)
        key = (t0, clamp, t1, regs.get(0x3B), frame, regs.get(0x47 + ctx), control & 0x3FF)
        entry = samples.setdefault(
            key, {"vertex_kicks": 0, "uv_min": [math.inf] * 2, "uv_max": [-math.inf] * 2, "uv_known": 0}
        )
        entry["vertex_kicks"] += 1
        entry.setdefault("first_kick", counts["vertex_kicks"])
        entry["alpha"] = hex(regs.get(0x42 + ctx, 0))
        if rgba:
            entry["vertex_alpha_range"] = [
                min(entry.get("vertex_alpha_range", [255, 0])[0], rgba[3]),
                max(entry.get("vertex_alpha_range", [255, 0])[1], rgba[3]),
            ]
        size = (1 << ((t0 >> 26) & 15), 1 << ((t0 >> 30) & 15))
        coord = (
            uv
            if control & 256
            else tuple(st[i] / q * size[i] for i in range(2))
            if st and q and math.isfinite(q)
            else None
        )
        if coord and all(math.isfinite(x) for x in coord):
            entry["uv_known"] += 1
            for i in range(2):
                entry["uv_min"][i] = min(entry["uv_min"][i], coord[i])
                entry["uv_max"][i] = max(entry["uv_max"][i], coord[i])

    p = 32
    while p + 8 <= len(data):
        kind, size = struct.unpack_from("<II", data, p)
        p += 8
        payload = data[p : p + size]
        p += size
        if len(payload) != size:
            raise ValueError("Truncated capture")
        if kind != 2:
            continue
        packet = payload[8 : 8 + struct.unpack_from("<I", payload, 4)[0]]
        a = 0
        while a + 16 <= len(packet):
            lo, hi = struct.unpack_from("<QQ", packet, a)
            a += 16
            loops, fmt, nr = lo & 32767, (lo >> 58) & 3, (lo >> 60) & 15 or 16
            if lo & (1 << 46):
                write(0, (lo >> 47) & 2047)
            if fmt >= 2:
                a += loops * 16
                continue
            count, stride = loops * nr, 16 if fmt == 0 else 8
            if a + count * stride > len(packet):
                counts["split_tags"] += 1
                break
            for i in range(count):
                desc = (hi >> (4 * (i % nr))) & 15
                v = struct.unpack_from("<Q", packet, a)[0]
                h = struct.unpack_from("<Q", packet, a + 8)[0] if fmt == 0 else 0
                if desc == 14 and fmt == 0:
                    write(h & 255, v)
                elif desc != 15:
                    write(desc, v, fmt == 0, h)
                a += stride
            if fmt == 1 and count & 1:
                a += 8

    output = []
    for (t0, clamp, t1, texa, frame, test, control), entry in samples.items():
        tbp, tbw, psm = t0 & 16383, (t0 >> 14) & 63, (t0 >> 20) & 63
        size = [1 << ((t0 >> 26) & 15), 1 << ((t0 >> 30) & 15)]
        candidates = []
        for fbp, fbw, fpsm in sorted(frames):
            off = tbp - fbp * 32
            same_class = (psm in (0, 1) and fpsm in (0, 1)) or (psm == fpsm and psm in (2, 10))
            if same_class and fbw == max(tbw, 1) and off >= 0 and off % 32 == 0:
                page = off // 32
                candidates.append(
                    {
                        "fbp": fbp,
                        "psm": fpsm,
                        "origin": [(page % fbw) * 64, (page // fbw) * (64 if psm in (2, 10) else 32)],
                    }
                )
        if not entry["uv_known"]:
            entry["uv_min"] = entry["uv_max"] = None
        output.append(
            dict(
                entry,
                tbp=tbp,
                tbw=tbw,
                psm=psm,
                size=size,
                abe=bool(control & 64),
                tcc=bool(t0 & (1 << 34)),
                tfx=(t0 >> 35) & 3,
                clamp={
                    "u": clamp & 3,
                    "v": (clamp >> 2) & 3,
                    "minu": (clamp >> 4) & 1023,
                    "maxu": (clamp >> 14) & 1023,
                    "minv": (clamp >> 24) & 1023,
                    "maxv": (clamp >> 34) & 1023,
                },
                tex1=hex(t1),
                texa=hex(texa) if texa is not None else None,
                frame=hex(frame) if frame is not None else None,
                test=hex(test) if test is not None else None,
                fst=bool(control & 256),
                observed_frame_candidates=candidates,
            )
        )
    return {
        "counts": dict(counts),
        "frames": sorted(frames),
        "producers": [
            dict(
                v,
                frame=hex(k[0]),
                scissor=hex(k[1]) if k[1] is not None else None,
                zbuf=hex(k[2]) if k[2] is not None else None,
                test=hex(k[3]) if k[3] is not None else None,
                control=hex(k[4]),
            )
            for k, v in producers.items()
        ],
        "samples": sorted(output, key=lambda x: -x["vertex_kicks"]),
        "transfers": [
            {"direction": k[0], "bitbltbuf": k[1], "trxpos": k[2], "trxreg": k[3], "count": n}
            for k, n in transfers.most_common()
        ],
        "limits": "Submitted vertex ranges only, not clipped pixel UVs; observed FRAMEs do not prove ownership or causal order. No initial VRAM/register snapshot.",
    }


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    text = json.dumps(audit(args.capture), indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
