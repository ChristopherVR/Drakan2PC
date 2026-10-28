"""Read private DRKGSCAP packets; emit state opportunities, never extract assets.

Counts are XYZ2/XYZF2 vertex kicks, not draw calls or affected pixels. A late
capture has no initial VRAM or register snapshot; only states written into the
recording can be trusted. This tool does not execute/replay the capture.
"""

import argparse
import collections
import hashlib
import json
import struct
from pathlib import Path


def audit(path):
    data = path.read_bytes()
    if data[:8] != b"DRKGSCAP" or struct.unpack_from("<I", data, 8)[0] != 1:
        raise ValueError("Expected DRKGSCAP v1")
    regs = {}
    counts = collections.Counter()
    states = collections.Counter()
    formats = collections.Counter()

    def write(reg, value):
        regs[reg] = value
        if reg not in (4, 5):
            return
        if 0 not in regs:
            counts["kicks_without_prim"] += 1
            return
        prim = regs[0]
        control = prim if regs.get(0x1A, 1) & 1 else regs.get(0x1B, 0)
        context = (control >> 9) & 1
        test, alpha, tex0 = (regs.get(k + context) for k in (0x47, 0x42, 6))
        counts["vertex_kicks"] += 1
        if test is not None and alpha is not None:
            afail = (test >> 12) & 3
            if test & 1 and (test >> 1) & 7 != 1 and afail in (1, 3) and control & 64:
                counts["blended_alpha_fail_color_opportunities"] += 1
            if regs.get(0x4A + context, 0) & 1 and control & 64:
                counts["fba_with_blending_opportunities"] += 1
            states[(hex(test), hex(alpha), afail, bool(control & 64))] += 1
        if tex0 is not None and control & 16:
            psm = (tex0 >> 20) & 63
            formats[hex(psm)] += 1
            texa = regs.get(0x3B)
            if (
                texa is not None
                and texa & (1 << 15)
                and (psm in (2, 10) or (psm in (0x13, 0x14, 0x1B, 0x24, 0x2C) and (tex0 >> 51) & 15 in (2, 10)))
            ):
                counts["aem_16bit_texture_or_clut_opportunities"] += 1

    p = 32
    while p + 8 <= len(data):
        kind, size = struct.unpack_from("<II", data, p)
        p += 8
        payload = data[p : p + size]
        if len(payload) != size:
            raise ValueError("Truncated record")
        p += size
        if kind == 4:
            counts["vsync_records"] += 1
        if kind != 2:
            continue
        counts["gif_packets"] += 1
        packet_size = struct.unpack_from("<I", payload, 4)[0]
        packet = payload[8 : 8 + packet_size]
        q = 0
        while q + 16 <= len(packet):
            lo, hi = struct.unpack_from("<QQ", packet, q)
            q += 16
            loops, fmt, nreg = lo & 32767, (lo >> 58) & 3, (lo >> 60) & 15 or 16
            if lo & (1 << 46):
                write(0, (lo >> 47) & 2047)
            if fmt in (2, 3):
                q += loops * 16
                continue
            entries = loops * nreg
            stride = 16 if fmt == 0 else 8
            if q + entries * stride > len(packet):
                counts["split_or_truncated_gif_tags"] += 1
                break
            for i in range(entries):
                desc = (hi >> (4 * (i % nreg))) & 15
                value = struct.unpack_from("<Q", packet, q)[0]
                if fmt == 0 and desc == 14:
                    write(packet[q + 8], value)
                elif desc != 15:
                    write(desc, value)
                q += stride
            if fmt == 1 and entries & 1:
                q += 8
    return {
        "capture_sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "counts": dict(counts),
        "texture_format_vertex_kicks": dict(formats),
        "frequent_test_alpha_states": [
            {"test": k[0], "alpha": k[1], "afail": k[2], "abe": k[3], "vertex_kicks": n}
            for k, n in states.most_common(12)
        ],
        "limits": "State opportunities only; no initial VRAM, no pixel outcomes or visual proof.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(audit(args.capture), indent=2)
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")
    print(result)
