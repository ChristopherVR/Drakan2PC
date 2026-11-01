r"""UE5-style PBR material generator for the Drakan PC port. Works on the textures hd_pack.py extracted from YOUR disc.

  python -I pbr_pack.py build [--work build/hdpack] [--hd dist/DrakanPC/textures] [--out build/pbr] [--select LEVEL1ON,LEVEL1TS]
                              [--overrides configs/pbr_overrides.toml] [--limit N] [--sheet]
  python -I pbr_pack.py sheet [--out build/pbr] [--cols 4] [--tile 128]     contact sheet(s) of what is in --out

Input: <work>/index.json + <work>/src/<hash>.png (hd_pack.py extract), optional <hd>/<hash>.png (hd_pack.py upscale, 2-8x).
Output (everything under --out, never committed: the pictures derive from the disc):
  Textures/T_<Name>_BC.png   BaseColor, sRGB, de-lit; alpha = opacity for Masked / Translucent materials only
  Textures/T_<Name>_N.png    Normal, DirectX convention (green points DOWN in the image = UE5), linear (UE: TC_Normalmap, sRGB off)
  Textures/T_<Name>_ORM.png  packed R = ambient occlusion, G = roughness, B = metallic, linear (UE: TC_Masks, sRGB off)
  Materials/M_<Name>.json    material description (schema 1, docs/reference/hd-textures.md "PBR materials")
  index.json                 list of every material written
Images are written upright (the TXD stores rows bottom-up; hd_pack.py keeps that order), 1 texel = 1 source texel * the HD factor.

Heuristics (all documented in docs/reference/hd-textures.md, every one can be overridden per material in configs/pbr_overrides.toml):
  de-light   the PS2 art has lighting painted in. A wrap-around low-pass of the linear luminance (sigma 12 percent of the size) is divided out
             with exponent `delit` (default 0.6, ratio clamped 0.5..2), then the albedo is limited to 30..240 sRGB (PBR albedo range).
  height     de-lit luminance, blurred at two scales (fine 0.6 texel, coarse 2.5 texel, in source texels so the HD factor does not change the look).
  normal     central-difference slope of that height, per source texel, times `normal_strength` (default 1.2); wrap-around so tiles stay seamless.
             Flat height (std < 1.5/255) writes a flat normal.
  AO         cavity: height below its own low-pass darkens, 0.55..1, scaled by `ao` (default 1).
  roughness  per class: 0.78 default, 0.88 foliage-like (green dominant), 0.92 for FX; plus 0.25 * (0.5 - de-lit luminance) so dark = rougher; clamp 0.3..1.
  metallic   0 unless an override says otherwise (there is no reliable colour cue for metal in these textures).
  blend      alpha stats of the decoded texture: alpha ignored by the GS (16/24 bit and 5551 palettes) -> Opaque; only 0 / opaque alpha -> Masked;
             partial alpha -> Translucent; FX / MAGIC only textures -> Additive (Unlit, emissive = BaseColor). Constant alpha < 0x7C -> Translucent.
  two sided  Masked / Translucent / PROCTEES (foliage cards) -> true. The model polygon flag 0x0204 bit 1 of a mesh overrides this per mesh.
  UI sources (INTERFCE, SYSTEM, DEBUG, PS2MUSIC) get only a BaseColor + a UserInterface material.
"""

import argparse
import fnmatch
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

try:
    import tomllib
except ImportError:  # pragma: no cover
    tomllib = None

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
UI_TXDS = {"INTERFCE", "SYSTEM", "DEBUG", "FX", "MAGIC", "PS2MUSIC"}
FX_TXDS = {"FX", "MAGIC"}
PURE_UI = {"INTERFCE", "SYSTEM", "DEBUG", "PS2MUSIC"}
SCHEMA = 1
DEFAULTS = dict(
    delit=0.6, normal_strength=1.2, ao=1.0, roughness=None, metallic=0.0, blend=None, two_sided=None, emissive=None,
    tiling=[1.0, 1.0], skip=False, opacity_mask_clip=0.5,
)  # fmt: skip


# ------------------------------------------------------------------------------------------------ small image math
def srgb_to_lin(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055) * 255.0


def blur(img, sigma):
    """Gaussian blur with wrap-around (FFT), so tileable textures stay tileable."""
    if sigma <= 0:
        return img
    h, w = img.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    k = np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    return np.fft.irfft2(np.fft.rfft2(img) * k, s=(h, w))


def luminance(lin):
    return lin[..., 0] * 0.2126 + lin[..., 1] * 0.7152 + lin[..., 2] * 0.0722


# ------------------------------------------------------------------------------------------------ naming / classification
def pretty_txd(txd):
    return txd[:1].upper() + txd[1:].lower()


def pick_source(srcs, select):
    """Choose the source entry that names the material: selected TXD first, then level / world TXDs, then COMMON."""

    def rank(s):
        p = s.split("#")[0]
        sel = 0 if select and any(t in p for t in select) else 1
        common = 1 if p.startswith("COMMON/") else 0
        return (sel, common, p)

    return sorted(srcs, key=rank)[0]


def material_name(src):
    path, tid = src.split("#")
    txd = os.path.splitext(os.path.basename(path))[0]
    return "%s_%05d" % (pretty_txd(txd), int(tid)), txd.upper()


def txds_of(entry):
    return {os.path.splitext(os.path.basename(s.split("#")[0]))[0].upper() for s in entry["src"]}


def alpha_stats(rgba, entry):
    a = rgba[..., 3]
    n = a.size
    return dict(
        zero=float((a == 0).sum()) / n, full=float((a >= 250).sum()) / n, partial=float(((a > 0) & (a < 250)).sum()) / n
    )


def classify(entry, rgba, txds):
    """-> (blend, two_sided, shading, alpha_stats). See the module doc for the rules."""
    st = alpha_stats(rgba, entry)
    ignore = entry["bpp"] in (16, 24) or entry["kind"] == 2 or entry["amax"] == 0
    fx = txds <= FX_TXDS
    if fx:
        blend = "Additive"
    elif ignore:
        blend = "Opaque"
    elif entry["aconst"]:
        blend = "Opaque" if entry["a0"] >= 0x7C else ("Masked" if entry["a0"] == 0 else "Translucent")
        if blend == "Masked":
            blend = "Translucent"  # a fully transparent constant texture is an invisible helper
    elif st["partial"] < 0.04 and st["zero"] > 0.005:
        blend = "Masked"
    elif st["partial"] >= 0.04:
        blend = "Translucent"
    else:
        blend = "Opaque"
    two_sided = blend in ("Masked", "Translucent", "Additive") or "PROCTEES" in txds
    return blend, two_sided, "Unlit" if blend == "Additive" else "DefaultLit", st


# ------------------------------------------------------------------------------------------------ the PBR maths
def make_pbr(rgba, up, ov, blend, fx):
    """rgba: HxWx4 uint8 upright (PNG alpha). up = output size / source size. -> dict of uint8 arrays + diagnostics."""
    h, w = rgba.shape[:2]
    lin = srgb_to_lin(rgba[..., :3].astype(np.float32))
    lum = luminance(lin)
    mean = float(lum.mean()) + 1e-4
    low = blur(lum, 0.12 * max(w, h)) + 1e-4
    ratio = np.clip((mean / low) ** ov["delit"], 0.5, 2.0) if ov["delit"] > 0 else np.ones_like(low)
    if fx:
        ratio = np.ones_like(low)  # glow sprites are meant to be bright; keep as painted
    alb = lin * ratio[..., None]
    if not fx:
        alb = np.clip(alb, srgb_to_lin(np.float32(30)), srgb_to_lin(np.float32(240)))
    bc = lin_to_srgb(alb)
    gray = luminance(alb)
    gray = lin_to_srgb(gray) / 255.0  # perceptual gray for the height
    height = 0.65 * blur(gray, 0.6 * up) + 0.35 * blur(gray, 2.5 * up)
    flat = float(height.std()) < 1.5 / 255
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * up  # per source texel
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * up  # image rows grow downward
    s = 0.0 if flat else ov["normal_strength"] * 4.0
    # DirectX: +X right, +Y toward the image BOTTOM (green down); the normal leans away from rising height.
    nx, ny, nz = -gx * s, -gy * s, np.ones_like(gx)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack([nx * inv, ny * inv, nz * inv], -1)
    nrm = np.clip((normal * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    cav = np.clip((blur(height, 4.0 * up) - height) * 4.0, 0, 1)
    ao = 1.0 - 0.45 * cav * float(ov["ao"])
    if ov["roughness"] is not None:
        rough = np.full_like(gray, float(ov["roughness"]))
        rcls = "override"
    else:
        r, g, b = (float(lin[..., i].mean()) for i in range(3))
        green = g > 1.12 * max(r, b) and g > 0.04
        rcls = "fx" if fx else ("foliage" if green else "default")
        base = {"fx": 0.92, "foliage": 0.88, "default": 0.78}[rcls]
        rough = np.clip(base + 0.25 * (0.5 - gray), 0.3, 1.0)
    met = np.full_like(gray, float(ov["metallic"]))
    orm = np.stack([ao, rough, met], -1)
    orm = np.clip(orm * 255.0 + 0.5, 0, 255).astype(np.uint8)
    out_rgb = np.clip(bc + 0.5, 0, 255).astype(np.uint8)
    alpha = rgba[..., 3]
    if blend in ("Masked", "Translucent", "Additive"):
        bco = np.dstack([out_rgb, alpha])
    else:
        bco = out_rgb
    return dict(bc=bco, n=nrm, orm=orm, flat=flat, rough_class=rcls, mean_roughness=float(rough.mean()))


# ------------------------------------------------------------------------------------------------ overrides
def load_overrides(path):
    if not path or not os.path.exists(path) or tomllib is None:
        return []
    with open(path, "rb") as f:
        return tomllib.load(f).get("override", [])


def apply_overrides(rules, hx, name, srcs):
    ov = dict(DEFAULTS)
    applied = []
    for r in rules:
        m = r.get("match", "")
        hit = m in (hx, name) or fnmatch.fnmatch(name, m) or any(fnmatch.fnmatch(s, m) for s in srcs)
        if hit:
            for k, v in r.items():
                if k not in ("match", "note"):
                    ov[k] = v
            applied.append(m)
    return ov, applied


# ------------------------------------------------------------------------------------------------ build
def read_rgba(path):
    return np.array(Image.open(path).convert("RGBA"))


def cmd_build(a):
    index = json.load(open(os.path.join(a.work, "index.json")))
    select = [s.strip().upper() for s in a.select.split(",") if s.strip()] if a.select else []
    rules = load_overrides(a.overrides)
    tex_dir = os.path.join(a.out, "Textures")
    mat_dir = os.path.join(a.out, "Materials")
    os.makedirs(tex_dir, exist_ok=True)
    os.makedirs(mat_dir, exist_ok=True)
    written, used = [], set()
    for hx, e in sorted(index.items(), key=lambda kv: kv[1]["src"][0]):
        if select and not any(t in s.upper() for s in e["src"] for t in select):
            continue
        if a.limit and len(written) >= a.limit:
            break
        src = pick_source(e["src"], select)
        base, _ = material_name(src)
        name = base
        if name in used:
            name = "%s_%s" % (base, hx[:4])
        used.add(name)
        ov, applied = apply_overrides(rules, hx, name, e["src"])
        if ov["skip"]:
            continue
        txds = txds_of(e)
        srcpng = os.path.join(a.work, "src", hx + ".png")
        hdpng = os.path.join(a.hd, hx + ".png") if a.hd else ""
        use_hd = bool(hdpng) and os.path.exists(hdpng)
        rgba = read_rgba(hdpng if use_hd else srcpng)[::-1].copy()  # TXD rows are bottom-up
        up = rgba.shape[1] / float(e["w"])
        blend, two_sided, shading, st = classify(e, rgba, txds)
        if ov["blend"]:
            blend = ov["blend"]
            shading = "Unlit" if blend == "Additive" else "DefaultLit"
        if ov["two_sided"] is not None:
            two_sided = bool(ov["two_sided"])
        ui = bool(txds & PURE_UI) and not (txds - PURE_UI)
        mat = dict(
            schema=SCHEMA, name="M_" + name, textureHash=hx, sources=e["src"], sourceSize=[e["w"], e["h"]],
            size=[rgba.shape[1], rgba.shape[0]], hd=use_hd, hdFactor=round(up, 3),
        )  # fmt: skip
        if ui:
            Image.fromarray(rgba[..., :3] if blend == "Opaque" else rgba).save(
                os.path.join(tex_dir, "T_%s_BC.png" % name)
            )
            mat.update(
                domain="UserInterface", blendMode="Translucent" if blend != "Opaque" else "Opaque", shadingModel="Unlit", twoSided=True,
                textures=dict(baseColor="Textures/T_%s_BC.png" % name), uvTiling=ov["tiling"], overrides=applied,
            )  # fmt: skip
        else:
            fx = blend == "Additive" and txds <= FX_TXDS
            p = make_pbr(rgba, up, ov, blend, fx)
            for key, suffix in (("bc", "BC"), ("n", "N"), ("orm", "ORM")):
                Image.fromarray(p[key]).save(os.path.join(tex_dir, "T_%s_%s.png" % (name, suffix)))
            emissive = ov["emissive"]
            if emissive is None:
                emissive = dict(source="baseColor", intensity=1.0) if blend == "Additive" else None
            elif isinstance(emissive, (int, float)):
                emissive = dict(source="baseColor", intensity=float(emissive))
            mat.update(
                domain="Surface", blendMode=blend, shadingModel=shading, twoSided=two_sided,
                opacityMaskClip=ov["opacity_mask_clip"] if blend == "Masked" else None,
                textures=dict(
                    baseColor="Textures/T_%s_BC.png" % name, normal="Textures/T_%s_N.png" % name, orm="Textures/T_%s_ORM.png" % name
                ),
                textureSettings=dict(
                    baseColor=dict(sRGB=True, compression="TC_Default"),
                    normal=dict(sRGB=False, compression="TC_Normalmap", convention="DirectX", flipGreenChannel=False),
                    orm=dict(sRGB=False, compression="TC_Masks", channels="R=AO G=Roughness B=Metallic"),
                ),
                uvTiling=ov["tiling"], emissive=emissive,
                scalars=dict(roughnessMean=round(p["mean_roughness"], 3), metallic=float(ov["metallic"])),
                heuristics=dict(
                    delit=ov["delit"], normalStrength=ov["normal_strength"], ao=ov["ao"], roughnessClass=p["rough_class"],
                    flatNormal=p["flat"], alpha={k: round(v, 4) for k, v in st.items()}, twoSidedNote="per-mesh polygon flag 0x0204 bit 1 overrides",
                ),
                overrides=applied,
            )  # fmt: skip
        json.dump(mat, open(os.path.join(mat_dir, "M_%s.json" % name), "w", newline="\n"), indent=2)
        written.append(name)
    json.dump(
        dict(schema=SCHEMA, materials=written), open(os.path.join(a.out, "index.json"), "w", newline="\n"), indent=1
    )
    print("materials written: %d -> %s" % (len(written), a.out))
    if a.sheet:
        cmd_sheet(a)


# ------------------------------------------------------------------------------------------------ contact sheet
def cmd_sheet(a):
    idx = json.load(open(os.path.join(a.out, "index.json")))["materials"]
    tile, cols = a.tile, a.cols
    per_page = cols * 8
    label_h = 14
    cell_w = tile * 3 + 4
    cell_h = tile + label_h + 4
    for page in range(0, len(idx), per_page):
        chunk = idx[page : page + per_page]
        rows = (len(chunk) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (24, 24, 28))
        d = ImageDraw.Draw(sheet)
        for i, name in enumerate(chunk):
            x0, y0 = (i % cols) * cell_w, (i // cols) * cell_h
            d.text((x0 + 2, y0 + 1), name, fill=(220, 220, 220))
            for j, suf in enumerate(("BC", "N", "ORM")):
                p = os.path.join(a.out, "Textures", "T_%s_%s.png" % (name, suf))
                if not os.path.exists(p):
                    continue
                im = Image.open(p)
                if im.mode == "RGBA":
                    bg = Image.new("RGBA", im.size, (90, 90, 90, 255))
                    im = Image.alpha_composite(bg, im)
                im = im.convert("RGB").resize((tile, tile), Image.LANCZOS)
                sheet.paste(im, (x0 + j * tile + j, y0 + label_h))
        out = os.path.join(a.out, "contact_%02d.png" % (page // per_page))
        sheet.save(out)
        print("contact sheet:", out, "(%d materials)" % len(chunk))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--work", default=os.path.join(ROOT, "build", "hdpack"))
    b.add_argument("--hd", default=os.path.join(ROOT, "dist", "DrakanPC", "textures"))
    b.add_argument("--out", default=os.path.join(ROOT, "build", "pbr"))
    b.add_argument("--select", default="", help="comma list of substrings of the source path, e.g. LEVEL1ON,LEVEL1TS")
    b.add_argument("--overrides", default=os.path.join(ROOT, "configs", "pbr_overrides.toml"))
    b.add_argument("--limit", type=int, default=0)
    b.add_argument("--sheet", action="store_true")
    b.add_argument("--cols", type=int, default=4)
    b.add_argument("--tile", type=int, default=128)
    s = sub.add_parser("sheet")
    s.add_argument("--out", default=os.path.join(ROOT, "build", "pbr"))
    s.add_argument("--cols", type=int, default=4)
    s.add_argument("--tile", type=int, default=128)
    a = ap.parse_args(argv)
    if a.cmd == "build":
        cmd_build(a)
    else:
        cmd_sheet(a)


if __name__ == "__main__":
    sys.exit(main())
