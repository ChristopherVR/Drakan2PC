r"""Material maps for the HD texture pack: normal, occlusion / roughness / metallic and height images derived from YOUR textures.

  python -I material_maps.py build [--work build/hdpack] [--hd dist/DrakanPC/textures] [--out <hd>/materials]
                                   [--scale 2] [--select PROCTEES,LEVEL1ON] [--limit N] [--jobs N] [--force]
  python -I material_maps.py sheet --out <materials dir> [--count 32]      contact sheet of what is there

Input: <work>/index.json + <work>/src/<hash>.png (hd_pack.py extract / modextract) and the HD image <hd>/<hash>.png when it exists.
Output, next to the HD images and keyed like them by the renderer's content hash (the 16 hex digit file name):
  <out>/<hash>_n.png     tangent-space normal, RGB = xyz * 0.5 + 0.5, linear. +X = image right (+u), +Y = image DOWN (+v): the
                         DirectX / UE5 convention ("green down") in the row order of <hash>.png itself, +Z out of the surface.
  <out>/<hash>_orm.png   R = ambient occlusion, G = roughness, B = metallic, linear (UE5 TC_Masks layout).
  <out>/<hash>_h.png     height, 8 bit grey, 128 = mean surface, linear. Wraps where the texture tiles.
  <out>/materials.json   schema, generator version and one record per hash (class, sources, sizes, scalars, heuristics used).
  <out>/materials.txt    the same table as plain lines for the game: "<hash> <class id> <class> <mean roughness> <metallic>".
All images have the pixel size of the source texture times --scale (default 2, half the 4x HD image) and the row order of
<hash>.png, so a consumer that draws <hash>.png with some UVs samples the maps with the same UVs. Nothing here is committed:
every picture derives from the disc (or the Order of the Flame install).

Everything is a deterministic filter of the picture (no network, no random numbers): the same input gives the same bytes.
What is a guess is listed in docs/reference/hd-textures.md ("Material maps"): the maps are bump-from-painted-detail, not
recovered geometry, and the classes come from the source file and colour statistics.
"""

import argparse
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

# The guided build keeps numpy / Pillow in its own folder when the interpreter has none (scripts/buildtool/extras.py).
for _site in filter(None, os.environ.get("DRAKAN_PY_SITE", "").split(os.pathsep)):
    sys.path.insert(0, _site)

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCHEMA = 1
VERSION = 2  # bump when the filters change: maps of an older version are rebuilt

# class id (materials.txt, the renderer's per class switch) -> name. Ids are stable; add new ones at the end.
CLASSES = ["none", "terrain", "stone", "wood", "metal", "character", "foliage", "surface"]
# per class: normal strength, base roughness, occlusion strength
CLASS_PARAMS = {
    "terrain": (1.0, 0.90, 1.0),
    "stone": (1.25, 0.85, 1.0),
    "wood": (0.9, 0.72, 0.8),
    "metal": (0.8, 0.45, 0.6),
    "character": (0.5, 0.75, 0.4),
    "foliage": (0.6, 0.85, 0.5),
    "surface": (1.0, 0.78, 0.8),
}
NO_MATERIAL_TXDS = {"INTERFCE", "SYSTEM", "DEBUG", "FX", "MAGIC", "PS2MUSIC"}


# ------------------------------------------------------------------------------------------------ image math
def srgb_to_lin(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def luminance(lin):
    return lin[..., 0] * 0.2126 + lin[..., 1] * 0.7152 + lin[..., 2] * 0.0722


def blur(img, sigma, wrap_x=True, wrap_y=True):
    """Gaussian blur. Wraps around on a tiling axis (the result tiles too) and mirrors at the border of the others, so a
    texture that does not tile (a character sheet) gets no light from its opposite edge."""
    if sigma <= 0:
        return img
    h, w = img.shape
    px = 0 if wrap_x else min(w, int(np.ceil(3 * sigma)) + 1)
    py = 0 if wrap_y else min(h, int(np.ceil(3 * sigma)) + 1)
    if px or py:
        img = np.pad(img, ((py, py), (px, px)), mode="symmetric")
    hh, ww = img.shape
    fy = np.fft.fftfreq(hh)[:, None]
    fx = np.fft.rfftfreq(ww)[None, :]
    k = np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    out = np.fft.irfft2(np.fft.rfft2(img) * k, s=(hh, ww))
    return out[py : py + h, px : px + w]


def tile_axes(rgb):
    """per axis: does the opposite edge continue the picture (hd_pack.py's test)? -> (tile_x, tile_y)"""
    f = rgb.astype(np.float32)
    res = []
    for ax, n in ((1, f.shape[1]), (0, f.shape[0])):
        if n < 8:
            res.append(False)
            continue
        first, last = np.take(f, 0, axis=ax), np.take(f, n - 1, axis=ax)
        second, prev = np.take(f, 1, axis=ax), np.take(f, n - 2, axis=ax)
        seam = np.abs(first - last).mean()
        nb = (np.abs(first - second).mean() + np.abs(last - prev).mean()) / 2
        res.append(bool(seam <= nb * 1.6 + 2.0))
    return res[0], res[1]


def box_down(img, f):
    """integer box filter downscale of HxW or HxWxC by f"""
    if f == 1:
        return img
    h, w = img.shape[:2]
    return img[: h // f * f, : w // f * f].reshape(h // f, f, w // f, f, *img.shape[2:]).mean(axis=(1, 3))


# ------------------------------------------------------------------------------------------------ classification
def txds_of(entry):
    return {os.path.splitext(os.path.basename(s.split("#")[0]))[0].upper() for s in entry["src"]}


def classify(entry, rgba):
    """-> (class name, reason). "none" = no material maps (UI, effects, translucent layers, baked far-terrain atlases).

    From the source file: PROCTEES (the terrain tile set) is terrain, NPCS is character, UI / effect files get nothing, the
    level *TS files are far-terrain atlases with filler (nothing). Everything else by the picture: a cut-out or green
    dominant picture is foliage, low saturation is stone, a brown hue is wood, bluish grey with strong contrast is metal;
    the rest is the generic "surface". These are guesses; materials.json records which rule applied."""
    txds = txds_of(entry)
    if entry.get("ui") or (txds and txds <= NO_MATERIAL_TXDS):
        return "none", "ui or effect source"
    alpha_used = not (entry.get("bpp") in (16, 24) or entry.get("kind") == 2 or entry.get("amax", 128) == 0)
    if alpha_used and not entry.get("aconst", True) and entry.get("apartial", 0.0) >= 0.04:
        return "none", "translucent layer"
    if alpha_used and entry.get("aconst", True) and 0 < entry.get("a0", 128) < 0x7C:
        return "none", "translucent constant alpha"
    if any(t.endswith("TS") for t in txds) and not (txds - {t for t in txds if t.endswith("TS")}):
        return "none", "far-terrain atlas"
    a = rgba[..., 3]
    vis = a > 0 if alpha_used and not entry.get("aconst", True) else np.ones(a.shape, bool)
    if vis.mean() < 0.02:
        return "none", "empty"
    rgb = rgba[..., :3].astype(np.float32)[vis] / 255.0
    if float(rgb.std(axis=0).max()) < 2.0 / 255:
        return "none", "flat colour"
    r, g, b = (float(rgb[:, i].mean()) for i in range(3))
    mx, mn = np.max(rgb, axis=1), np.min(rgb, axis=1)
    sat = float(((mx - mn) / (mx + 1e-4)).mean())
    cutout = alpha_used and not entry.get("aconst", True) and float((~vis).mean()) > 0.03
    if "PROCTEES" in txds:
        return "terrain", "terrain tile set"
    if "NPCS" in txds or "NPC" in txds:
        return "character", "character source file"
    if cutout or (g > 1.12 * max(r, b) and g > 0.12):
        return "foliage", "cut-out or green dominant"
    if sat < 0.16:
        if b > r * 1.04 and float((mx - mn).std()) < 0.05 and float(rgb.mean(axis=1).std()) > 0.16:
            return "metal", "neutral bluish with strong contrast"
        return "stone", "low saturation"
    if r > g > b and 0.16 <= sat <= 0.75 and (r - b) > 0.06 and g > 0.55 * r:
        return "wood", "brown hue"
    return "surface", "generic"


# ------------------------------------------------------------------------------------------------ the maps
def make_maps(rgba, up, cls, tile=(True, True), out_factor=1):
    """rgba: HxWx4 uint8 in file row order, `up` pixels per source texel. -> dict(n, orm, h uint8 arrays, flat, roughness)
    at the input size divided by out_factor.

    height: perceptual luminance with its large-scale shading divided out, as three bands (0.5, 1.5 and 5 source texels):
            broad shapes (stones, planks) carry most of the relief, fine grain a little. Bright = high.
    normal: central-difference slope per source texel, DirectX orientation in the stored row order.
    AO:     cavity (height under its own 4 texel low-pass), 0.6 .. 1.
    rough:  class base, darker = a little rougher, cavities rougher; metallic 1 for the metal class only."""
    strength, base_rough, ao_strength = CLASS_PARAMS[cls]
    tx, ty = tile
    h, w = rgba.shape[:2]
    lin = srgb_to_lin(rgba[..., :3].astype(np.float32))
    lum = luminance(lin)
    low = blur(lum, 0.12 * max(w, h), tx, ty) + 1e-4
    ratio = np.clip((float(lum.mean()) + 1e-4) / low, 0.5, 2.0) ** 0.6
    gray = np.clip(lum * ratio, 0, 1) ** (1 / 2.2)
    fine = blur(gray, 0.5 * up, tx, ty)
    mid = blur(gray, 1.5 * up, tx, ty)
    broad = blur(gray, 5.0 * up, tx, ty)
    far = blur(gray, 12.0 * up, tx, ty)
    # band-pass sum: each band weighted by its width, so wide features stand taller than grain
    height = 0.35 * (fine - mid) + 0.9 * (mid - broad) + 1.6 * (broad - far)
    flat = float(height.std()) < 1.0 / 255
    wrap = lambda a, s, ax, t: np.roll(a, s, ax) if t else _shift_clamped(a, s, ax)
    gx = (wrap(height, -1, 1, tx) - wrap(height, 1, 1, tx)) * 0.5 * up
    gy = (wrap(height, -1, 0, ty) - wrap(height, 1, 0, ty)) * 0.5 * up
    s = 0.0 if flat else strength * 6.0
    nx, ny, nz = -gx * s, -gy * s, np.ones_like(gx)
    normal = np.stack([nx, ny, nz], -1)
    cav = np.clip((blur(height, 4.0 * up, tx, ty) - height) * 5.0, 0, 1)
    ao = 1.0 - 0.4 * cav * ao_strength
    rough = np.clip(base_rough + 0.2 * (0.5 - gray) + 0.1 * cav, 0.25, 1.0)
    met = np.full_like(gray, 1.0 if cls == "metal" else 0.0)
    hmap = np.clip(0.5 + height * 2.0, 0, 1)
    if out_factor > 1:
        normal, ao, rough, met, hmap = (box_down(a, out_factor) for a in (normal, ao, rough, met, hmap))
    normal = normal / np.sqrt((normal * normal).sum(-1, keepdims=True))
    to8 = lambda a: np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return dict(
        n=to8(normal * 0.5 + 0.5), orm=to8(np.stack([ao, rough, met], -1)), h=to8(hmap), flat=flat,
        roughness=float(rough.mean()),
    )  # fmt: skip


def _shift_clamped(a, s, ax):
    out = np.roll(a, s, ax)
    idx = [slice(None)] * a.ndim
    edge = [slice(None)] * a.ndim
    if s > 0:
        idx[ax], edge[ax] = slice(0, s), slice(0, 1)
    else:
        idx[ax], edge[ax] = slice(s, None), slice(-1, None)
    out[tuple(idx)] = a[tuple(edge)]
    return out


# ------------------------------------------------------------------------------------------------ build
def _one(job):
    hx, e, srcpng, hdpng, out, scale = job
    src = np.array(Image.open(srcpng).convert("RGBA"))
    sw, sh = int(e.get("w", src.shape[1])), int(e.get("h", src.shape[0]))
    cls, why = classify(e, src)
    if cls == "none":
        return hx, dict(cls="none", why=why)
    use_hd = bool(hdpng) and os.path.exists(hdpng)
    rgba = np.array(Image.open(hdpng).convert("RGBA")) if use_hd else src
    if not use_hd and (rgba.shape[1], rgba.shape[0]) != (sw, sh):  # a converter source smaller than the guest texture
        rgba = np.array(Image.fromarray(rgba).resize((sw, sh), Image.LANCZOS))
    up = rgba.shape[1] / float(sw)
    target = (sw * scale, sh * scale)
    f = 1
    if up > scale and up % scale == 0 and rgba.shape[0] == sh * up:
        f = int(up // scale)
    tile = tile_axes(src[..., :3])
    m = make_maps(rgba, up, cls, tile, f)
    for key in ("n", "orm", "h"):
        img = Image.fromarray(m[key])
        if img.size != target:
            img = img.resize(target, Image.BILINEAR)
        img.save(os.path.join(out, "%s_%s.png" % (hx, key)), optimize=False, compress_level=6)
    return hx, dict(
        cls=cls, why=why, hd=use_hd, size=list(target), flat=bool(m["flat"]), roughness=round(m["roughness"], 3),
        tile=[bool(tile[0]), bool(tile[1])],
    )  # fmt: skip


def cmd_build(a):
    index = json.load(open(os.path.join(a.work, "index.json")))
    extra = os.path.join(a.work, "index_dump.json")
    srcdir = {h: "src" for h in index}
    if os.path.exists(extra):
        for h, e in json.load(open(extra)).items():
            if h not in index:
                index[h] = e
                srcdir[h] = "src_dump"
    out = a.out or os.path.join(a.hd, "materials")
    os.makedirs(out, exist_ok=True)
    jpath = os.path.join(out, "materials.json")
    table = {}
    if os.path.exists(jpath) and not a.force:
        old = json.load(open(jpath))
        if old.get("schema") == SCHEMA and old.get("version") == VERSION and old.get("scale") == a.scale:
            table = old.get("materials", {})
    select = [s.strip().upper() for s in a.select.split(",") if s.strip()] if a.select else []
    jobs = []
    for hx, e in sorted(index.items()):
        if select and not any(t in s.upper() for s in e["src"] for t in select):
            continue
        rec = table.get(hx)
        if rec and (rec["class"] == "none" or os.path.exists(os.path.join(out, hx + "_n.png"))):
            continue
        jobs.append(
            (hx, e, os.path.join(a.work, srcdir[hx], hx + ".png"), os.path.join(a.hd, hx + ".png"), out, a.scale)
        )
        if a.limit and len(jobs) >= a.limit:
            break
    print("material maps: %d of %d textures to do -> %s" % (len(jobs), len(index), out), flush=True)
    done = 0
    workers = a.jobs or max(1, (os.cpu_count() or 4) // 2)
    mapper = (
        ProcessPoolExecutor(workers).map(_one, jobs, chunksize=8) if workers > 1 and len(jobs) > 8 else map(_one, jobs)
    )
    for hx, r in mapper:
        e = index[hx]
        if r["cls"] == "none":
            table[hx] = {"class": "none", "classReason": r["why"], "sources": e["src"]}
        else:
            strength, base_rough, ao = CLASS_PARAMS[r["cls"]]
            table[hx] = {
                "class": r["cls"], "classReason": r["why"], "sources": e["src"], "sourceSize": [e["w"], e["h"]],
                "size": r["size"], "fromHdImage": r["hd"], "tiles": r["tile"], "flatNormal": r["flat"],
                "textures": {"normal": hx + "_n.png", "orm": hx + "_orm.png", "height": hx + "_h.png"},
                "scalars": {"roughnessMean": r["roughness"], "metallic": 1.0 if r["cls"] == "metal" else 0.0},
                "heuristics": {"normalStrength": strength, "roughnessBase": base_rough, "occlusion": ao},
            }  # fmt: skip
        done += 1
        if done % 200 == 0:
            print("  %d / %d" % (done, len(jobs)), flush=True)
    doc = {
        "schema": SCHEMA, "version": VERSION, "scale": a.scale, "generator": "analysis/scripts/material_maps.py",
        "key": "content hash of the decoded guest texture = file name of the HD image",
        "conventions": {
            "normal": "tangent space, RGB = xyz * 0.5 + 0.5, +Y toward the image bottom (DirectX / UE5), linear",
            "orm": "R = ambient occlusion, G = roughness, B = metallic, linear",
            "height": "8 bit, 128 = mean surface, linear",
            "rows": "same row order as <hash>.png",
        },
        "classes": CLASSES, "materials": table,
    }  # fmt: skip
    json.dump(doc, open(jpath, "w", newline="\n"), indent=1, sort_keys=True)
    with open(os.path.join(out, "materials.txt"), "w", newline="\n") as f:
        f.write("# <hash> <class id> <class> <mean roughness> <metallic>; images <hash>_n.png, _orm.png, _h.png\n")
        for hx, rec in sorted(table.items()):
            if rec["class"] != "none":
                f.write(
                    "%s %d %s %.3f %.1f\n"
                    % (hx, CLASSES.index(rec["class"]), rec["class"], rec["scalars"]["roughnessMean"],
                       rec["scalars"]["metallic"])
                )  # fmt: skip
    counts = {}
    for rec in table.values():
        counts[rec["class"]] = counts.get(rec["class"], 0) + 1
    print("material maps finished: %d written this run; classes %s" % (done, json.dumps(counts, sort_keys=True)))
    return 0


def cmd_sheet(a):
    doc = json.load(open(os.path.join(a.out, "materials.json")))
    names = [h for h, r in sorted(doc["materials"].items()) if r["class"] != "none"]
    if a.cls:
        names = [h for h in names if doc["materials"][h]["class"] == a.cls]
    names = names[: a.count]
    tile, cols = 128, 4
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tile * 3 + 6), rows * (tile + 16)), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    for i, hx in enumerate(names):
        x0, y0 = (i % cols) * (tile * 3 + 6), (i // cols) * (tile + 16)
        d.text((x0 + 2, y0 + 1), "%s %s" % (hx, doc["materials"][hx]["class"]), fill=(220, 220, 220))
        for j, suf in enumerate(("n", "orm", "h")):
            im = Image.open(os.path.join(a.out, "%s_%s.png" % (hx, suf))).convert("RGB").resize((tile, tile))
            sheet.paste(im, (x0 + j * tile, y0 + 14))
    p = os.path.join(a.out, "contact.png")
    sheet.save(p)
    print("contact sheet:", p)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--work", default=os.path.join(ROOT, "build", "hdpack"))
    b.add_argument("--hd", default=os.path.join(ROOT, "dist", "DrakanPC", "textures"))
    b.add_argument("--out", default="", help="default: <hd>/materials")
    b.add_argument("--scale", type=int, default=2, choices=[1, 2, 4], help="map size / source texture size")
    b.add_argument("--select", default="", help="comma list of substrings of the source path, e.g. PROCTEES,LEVEL1ON")
    b.add_argument("--limit", type=int, default=0)
    b.add_argument("--jobs", type=int, default=0, help="worker processes (default: half the CPUs)")
    b.add_argument("--force", action="store_true", help="rebuild maps that exist")
    s = sub.add_parser("sheet")
    s.add_argument("--out", required=True)
    s.add_argument("--count", type=int, default=32)
    s.add_argument("--cls", default="")
    a = ap.parse_args(argv)
    return cmd_build(a) if a.cmd == "build" else cmd_sheet(a)


if __name__ == "__main__":
    sys.exit(main())
