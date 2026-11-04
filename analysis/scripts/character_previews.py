"""Export original multiplayer character geometry / materials from the player's PS2 MOD/TXD assets.

Uses the documented PS2 bind-pose mesh / LOD layout, as in editor/packages/formats/src/model.ts.
Writes private live-preview model bundles under an ignored build directory. Standard library only.
Legacy frame-strip rendering is an explicit investigation option, never the menu's default.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import posixpath
import struct
from pathlib import Path

import coop_character_roster as roster
import lvl_dump
import odb_fields
import txd_decode
from ui_layout_dump import iso_files, read

WIDTH, HEIGHT, FRAMES = 160, 240, 12


def records(data):
    return {(e.type, e.id): data[e.offset : e.offset + e.size] for e in lvl_dump.parse_srsc(data)}


def lod0_count(data, vertices, triangles):
    """Locate the PS2 30-byte LOD table, rejecting ambiguous / malformed character records."""
    if len(data) < 66:
        raise ValueError("missing character LOD table")
    count = struct.unpack_from("<H", data, 64)[0]
    if not 1 <= count <= 16:
        raise ValueError("invalid LOD count")
    found = []
    for offset in range(66, len(data) - count * 30 + 1):
        fv = fp = 0
        first = 0
        for index in range(count):
            one, distance, _usage, _node, start, nv, poly, nt = struct.unpack_from("<Hf6I", data, offset + index * 30)
            # Some PS2 meshes leave a single unused separator polygon between LODs (e.g. Purple Orc).
            if one != 1 or not 0 <= distance < 1e9 or nv == 0 or start != fv or not fp <= poly <= fp + 1:
                break
            fv += nv
            fp = poly + nt
            if index == 0:
                first = nt
        else:
            # A few models retain unused polygons after the final LOD (Monk Boss: four).
            if fv == vertices and 0 <= triangles - fp <= 16:
                found.append(first)
    if len(found) != 1:
        raise ValueError("missing or ambiguous LOD table")
    return found[0]


def mesh(container, model):
    name = container[0x200, model]
    offset = 2 + struct.unpack_from("<H", name)[0]
    shading = struct.unpack_from("<I", name, offset)[0]
    offset += 8 + (4 if shading & 0x20000 else 0)
    count = struct.unpack_from("<I", name, offset)[0]
    textures = [struct.unpack_from("<HH", name, offset + 4 + i * 4) for i in range(count)]
    vr = container[0x203, model]
    nv = struct.unpack_from("<H", vr)[0]
    positions = [struct.unpack_from("<3f", vr, 2 + i * 12) for i in range(nv)]
    pr = container[0x204, model]
    np = struct.unpack_from("<H", pr)[0]
    offset = 2
    polygons = []
    for _ in range(np):
        _flags, corners, tex = struct.unpack_from("<3H", pr, offset)
        if corners not in (3, 4):
            raise ValueError("invalid polygon size")
        indices = struct.unpack_from(f"<{corners}H", pr, offset + 6)
        polygons.append((indices, tex))
        offset += 6 + 2 * corners
    offset = (offset + 3) & ~3
    lod_polygons = lod0_count(container[0x208, model], nv, np) if (0x208, model) in container else np
    triangles = []
    for poly_index, (indices, tex) in enumerate(polygons):
        uv = [struct.unpack_from("<2f", pr, offset + i * 8) for i in range(len(indices))]
        offset += len(indices) * 8
        if poly_index >= lod_polygons:
            continue
        for corners in ((0, 1, 2),) if len(indices) == 3 else ((0, 1, 2), (0, 2, 3)):
            triangles.append((tuple(indices[i] for i in corners), tuple(uv[i] for i in corners), tex))
    if offset != len(pr):
        raise ValueError("polygon UV size mismatch")
    if not triangles or any(i >= nv for tri, _, _ in triangles for i in tri):
        raise ValueError("empty mesh or vertex index out of range")
    return positions, triangles, textures


def render(positions, triangles, textures, angle):
    """Orthographic, two-sided, depth-tested textured bind pose. Texture alpha cuts out hair / clothing."""
    used = {i for tri, _, _ in triangles for i in tri}
    low = [min(positions[i][k] for i in used) for k in range(3)]
    high = [max(positions[i][k] for i in used) for k in range(3)]
    center = [(a + b) * 0.5 for a, b in zip(low, high)]
    radius = max(math.hypot(positions[i][0] - center[0], positions[i][2] - center[2]) for i in used)
    elevation = math.radians(10)
    se, ce = math.sin(elevation), math.cos(elevation)
    scale = min(
        (HEIGHT - 16) / max(1e-6, (high[1] - low[1]) * ce + 2 * radius * se), (WIDTH - 16) / max(1e-6, radius * 2)
    )
    sn, cs = math.sin(angle), math.cos(angle)
    projected = []
    for x, y, z in positions:
        x, y, z = x - center[0], y - center[1], z - center[2]
        zr = -x * sn + z * cs
        projected.append(
            (WIDTH / 2 + (x * cs + z * sn) * scale, HEIGHT / 2 - (y * ce - zr * se) * scale, y * se + zr * ce)
        )
    pixels = bytearray(WIDTH * HEIGHT * 4)
    depth = [-math.inf] * (WIDTH * HEIGHT)
    for indices, uv, tex in triangles:
        a, b, c = [projected[i] for i in indices]
        det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(det) < 1e-6:
            continue
        tw, th, rgba = textures[tex]
        xmin, xmax = max(0, math.floor(min(a[0], b[0], c[0]))), min(WIDTH - 1, math.ceil(max(a[0], b[0], c[0])))
        ymin, ymax = max(0, math.floor(min(a[1], b[1], c[1]))), min(HEIGHT - 1, math.ceil(max(a[1], b[1], c[1])))
        for y in range(ymin, ymax + 1):
            for x in range(xmin, xmax + 1):
                wa = ((b[1] - c[1]) * (x + 0.5 - c[0]) + (c[0] - b[0]) * (y + 0.5 - c[1])) / det
                wb = ((c[1] - a[1]) * (x + 0.5 - c[0]) + (a[0] - c[0]) * (y + 0.5 - c[1])) / det
                wc = 1 - wa - wb
                if min(wa, wb, wc) < -1e-6:
                    continue
                at = y * WIDTH + x
                z = wa * a[2] + wb * b[2] + wc * c[2]
                if z <= depth[at]:
                    continue
                u = wa * uv[0][0] + wb * uv[1][0] + wc * uv[2][0]
                v = wa * uv[0][1] + wb * uv[1][1] + wc * uv[2][1]
                src = ((math.floor(v * th) % th) * tw + math.floor(u * tw) % tw) * 4
                if rgba[src + 3] < 32:
                    continue
                pixels[at * 4 : at * 4 + 4] = rgba[src : src + 3] + b"\xff"
                depth[at] = z
    return pixels


def turntable(positions, triangles, textures):
    frames = [render(positions, triangles, textures, math.pi + i * math.tau / FRAMES) for i in range(FRAMES)]
    if not any(any(frame[3::4]) for frame in frames):
        raise ValueError("rendered empty turntable")
    return b"".join(frame[y * WIDTH * 4 : (y + 1) * WIDTH * 4] for y in range(HEIGHT) for frame in frames)


def model_bytes(positions, triangles, textures):
    """Private DCPM v1: original mesh / UVs / RGBA materials for the live GPU menu preview, not rendered frames."""
    result = bytearray(struct.pack("<4s4I", b"DCPM", 1, len(positions), len(triangles), len(textures)))
    for position in positions:
        result.extend(struct.pack("<3f", *position))
    for indices, uv, material in triangles:
        result.extend(struct.pack("<4I6f", *indices, material, *(value for corner in uv for value in corner)))
    for width, height, pixels in textures:
        result.extend(struct.pack("<2I", width, height))
        result.extend(pixels)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--ids", help="optional comma-separated hex class ids (0 = Rynn)")
    ap.add_argument(
        "--turntables", action="store_true", help="legacy low-resolution PNG strips, for investigation only"
    )
    args = ap.parse_args()
    fingerprint = hashlib.sha256(Path(__file__).read_bytes())
    stat = args.iso.stat()
    fingerprint.update(f"{args.iso.resolve()}:{stat.st_size}:{stat.st_mtime_ns}".encode())
    fingerprint.update(str(args.turntables).encode())
    for dependency in (roster, txd_decode):
        fingerprint.update(Path(dependency.__file__).read_bytes())
    stamp = fingerprint.hexdigest()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = args.out / "source.sha256"
    cached = manifest.is_file() and manifest.read_text().strip() == stamp
    f, files = iso_files(str(args.iso))
    containers = {}
    texture_cache = {}

    def container(path):
        if path not in containers:
            containers[path] = records(read(f, files[path]))
        return containers[path]

    def texture(base, rid, db):
        target = base
        if db:
            deps = read(f, files[base + ".DB"]).decode("latin-1").splitlines()[2:]
            paths = {int(line.split(maxsplit=1)[0]): line.split(maxsplit=1)[1] for line in deps if line.strip()}
            target = posixpath.normpath(posixpath.join(posixpath.dirname(base), paths[db].replace("\\", "/"))).upper()
        key = (target, rid)
        if key not in texture_cache:
            w, h, rgba, _ = txd_decode.decode(container(target + ".TXD")[0x40, rid])
            texture_cache[key] = w, h, rgba
        return texture_cache[key]

    try:
        npc = "/ENGLISH/COMMON/NPC/NPCS"
        system = "/ENGLISH/COMMON/SYSTEM/SYSTEM"
        classes = odb_fields.load_classes(read(f, files[npc + ".ODB"]))
        entries = [(0, "Rynn", 3877, system)]
        entries.extend(
            (cid, c["name"], c["model"][0], npc) for cid, c in sorted(classes.items()) if roster.selectable(c)
        )
        sys_classes = odb_fields.load_classes(read(f, files[system + ".ODB"]))
        entries.append((roster.AROKH_CLASS, "Arokh", sys_classes[roster.AROKH_CLASS]["model"][0], system))
        entries = [e for e in entries if e[0] in roster.CHARACTER_IDS]
        wanted = {int(cid, 16) for cid in args.ids.split(",")} if args.ids else None
        rendered = {}
        failures = {}
        for cid, name, model, base in entries:
            if wanted is not None and cid not in wanted:
                continue
            dest = args.out / f"{cid:04x}{'.png' if args.turntables else '.dcpm'}"
            if cached and dest.is_file():
                continue
            key = (base, model)
            if key in failures:
                continue
            if key not in rendered:
                try:
                    positions, triangles, refs = mesh(container(base + ".MOD"), model)
                    materials = [texture(base, *ref) for ref in refs]
                    rendered[key] = (turntable if args.turntables else model_bytes)(positions, triangles, materials)
                except (ValueError, KeyError, IndexError, struct.error) as error:
                    failures[key] = str(error)
                    print(f"FAILED {cid:04x}: {name.strip()} (model {model}): {error}", flush=True)
                    continue
            if args.turntables:
                txd_decode.write_png(str(dest), WIDTH * FRAMES, HEIGHT, rendered[key])
            else:
                dest.write_bytes(rendered[key])
            print(f"Character {cid:04x}: {name.strip()} (original model {model})", flush=True)
        if failures:
            return 1
        if wanted is None:
            manifest.write_text(stamp + "\n")
    finally:
        f.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
