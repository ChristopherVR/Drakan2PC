r"""HD texture pack generator for the Drakan PC port (static recompilation). Reads YOUR disc image, writes the pack next to YOUR game.

  python -I hd_pack.py extract  [--iso ISO] [--work DIR]            decode every ENGLISH TXD texture exactly like the renderer (gsr) does, hash it with the
                                                                     renderer's content hash, write work/src/<hash>.png + work/index.json
  python -I hd_pack.py compare  --dump DIR [--work DIR]              match rate of the offline hashes against a texture_dump of a real game run
  python -I hd_pack.py hybrid   --dump DIR [--work DIR]              add dump textures whose hash is not in the TXD index (runtime generated / edited ones)
  python -I hd_pack.py upscale  [--out DIR] [--limit N] [--exe ...]  Real-ESRGAN (GPU) every source texture -> <out>/<hash>.png (resumable)
  python -I hd_pack.py all      [...]                                extract + upscale

Pack format (what gsr reads from [video] texture_dir):
  <hash>.png          replacement for the texture whose decoded RGBA (CLUT + TEXA applied, alpha 0..0x80) hashes to <hash>; any 2/4/8x size
  alias.txt           lines '<rgbhash> <hash>': the same picture with different (game edited) palette alpha. The renderer then takes the RGB of <hash>.png
                      and the alpha of the texture it actually decoded (upscaled), so palette-alpha edits made at run time cannot break the match.
Hash = gsr::texup::hashContent over w*h little-endian uint32 (R | G<<8 | B<<16 | A<<24), A in GS units; rgbhash = the same with A forced to 0.
"""

import argparse
import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import iop_srsc

M64 = 0xFFFFFFFFFFFFFFFF
DEFAULT_ISO = os.environ.get("DRAKAN_ISO", "")
UI_TXDS = {"INTERFCE", "SYSTEM", "DEBUG", "FX", "MAGIC", "PS2MUSIC"}  # flat / hand painted / glow sprites: anime model


# ------------------------------------------------------------------------------------------------ hash (gs_texup.cpp hashContent)
def hash_content(px_u32, w, h):
    hh = 0x9E3779B97F4A7C15 ^ ((w << 32) | h)
    n = w * h
    a = np.ascontiguousarray(px_u32, dtype="<u4").reshape(-1)
    pairs = n // 2
    words = a[: pairs * 2].view("<u8").tolist()
    for v in words:
        hh = ((hh ^ v) * 0xFF51AFD7ED558CCD) & M64
        hh ^= hh >> 32
    if n & 1:
        hh = ((hh ^ int(a[n - 1])) * 0xFF51AFD7ED558CCD) & M64
        hh ^= hh >> 32
    hh ^= hh >> 33
    hh = (hh * 0xC4CEB9FE1A85EC53) & M64
    hh ^= hh >> 33
    return "%016x" % hh


def pack_u32(rgba):  # rgba: HxWx4 uint8 (A in GS units) -> HxW uint32
    return (
        rgba[..., 0].astype("<u4")
        | (rgba[..., 1].astype("<u4") << 8)
        | (rgba[..., 2].astype("<u4") << 16)
        | (rgba[..., 3].astype("<u4") << 24)
    )


# ------------------------------------------------------------------------------------------------ ISO
class Iso:
    def __init__(self, path):
        self.f = open(path, "rb")
        pvd = self.sec(16)
        self.root = pvd[156:190]

    def sec(self, l, n=1):
        self.f.seek(l * 2048)
        return self.f.read(2048 * n)

    def lsdir(self, lba, size):
        d = self.sec(lba, (size + 2047) // 2048)
        p = 0
        while p < len(d):
            l = d[p]
            if l == 0:
                p = (p // 2048 + 1) * 2048
                continue
            (lba2,) = struct.unpack_from("<I", d, p + 2)
            (sz,) = struct.unpack_from("<I", d, p + 10)
            fl = d[p + 25]
            n = d[p + 33 : p + 33 + d[p + 32]].decode("latin1").split(";")[0]
            yield n, lba2, sz, fl & 2
            p += l

    def walk(self, lba, size, prefix, out):
        for n, l, s, d in self.lsdir(lba, size):
            if n in ("", "\x00", "\x01"):
                continue
            if d:
                self.walk(l, s, prefix + n + "/", out)
            else:
                out.append((prefix + n, l, s))

    def files(self):
        out = []
        self.walk(struct.unpack_from("<I", self.root, 2)[0], struct.unpack_from("<I", self.root, 10)[0], "", out)
        return out

    def read(self, lba, size):
        return self.sec(lba, (size + 2047) // 2048)[:size]


# ------------------------------------------------------------------------------------------------ TXD decode = what gsr decodes
def decode_runtime(b):
    """TXD texture blob -> (w, h, HxWx4 uint8 RGBA with alpha in GS units (0x80 opaque) as gsr decodes it, info) or raise ValueError.
    T8 + CT32 CLUT: palette entry as stored (alpha as stored). CT24 / CT16 (+ palette kind 2): TEXA = 0 -> alpha 0 (gsr expand24/expand16)."""
    w, h, stride, bpp, flags = struct.unpack_from("<5I", b, 0)
    HDR = 0x40
    info = dict(w=w, h=h, bpp=bpp, flags=flags, kind=0)
    if bpp == 8:
        kind, cnt = struct.unpack_from("<II", b, HDR)
        info["kind"] = kind
        if kind == 1:
            pal = np.frombuffer(b, np.uint8, 1024, HDR + 16).reshape(256, 4)
            pal = pal[:, [2, 1, 0, 3]].copy()  # stored B,G,R,A -> R,G,B,A
            pix = HDR + 16 + 1024
        elif kind == 2:
            v = np.frombuffer(b, "<u2", 256, HDR + 16).astype(np.uint32)
            pal = np.stack(
                [(v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3, np.zeros(256, np.uint32)], 1
            ).astype(np.uint8)
            pix = HDR + 16 + 512
        else:
            raise ValueError("palette kind %d" % kind)
        if pix + w * h == len(b):
            idx = np.frombuffer(b, np.uint8, w * h, pix)
        else:  # level 0 tile set: u32 compressed length + zlib stream of the index plane
            (clen,) = struct.unpack_from("<I", b, pix)
            if pix + 4 + clen > len(b):
                raise ValueError("size")
            import zlib

            raw = zlib.decompress(b[pix + 4 : pix + 4 + clen])
            if len(raw) != w * h:
                raise ValueError("size (zlib)")
            idx = np.frombuffer(raw, np.uint8)
        out = pal[idx].reshape(h, w, 4)
    elif bpp == 16:
        if HDR + w * h * 2 != len(b):
            raise ValueError("size")
        v = np.frombuffer(b, "<u2", w * h, HDR).astype(np.uint32)
        out = (
            np.stack([(v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3, np.zeros_like(v)], 1)
            .astype(np.uint8)
            .reshape(h, w, 4)
        )
    elif bpp == 24:
        if HDR + w * h * 3 != len(b):
            raise ValueError("size")
        rgb = np.frombuffer(b, np.uint8, w * h * 3, HDR).reshape(h, w, 3)
        out = np.concatenate([rgb, np.zeros((h, w, 1), np.uint8)], 2)
    elif bpp == 32:
        if HDR + w * h * 4 != len(b):
            raise ValueError("size")
        out = np.frombuffer(b, np.uint8, w * h * 4, HDR).reshape(h, w, 4).copy()
    else:
        raise ValueError("bpp")
    return w, h, out, info


def to_png_rgba(rgba):  # GS alpha (0..0x80) -> PNG alpha, same as gs_texup.cpp writePngRGBA: min(255, (a*255+64)/128)
    o = rgba.copy()
    o[..., 3] = np.minimum(255, (rgba[..., 3].astype(np.uint32) * 255 + 64) // 128).astype(np.uint8)
    return o


def from_png_rgba(png):  # inverse, as gsAlphaFromFile: min(0x80, (a*128+127)/255)
    o = png.copy()
    o[..., 3] = np.minimum(0x80, (png[..., 3].astype(np.uint32) * 128 + 127) // 255).astype(np.uint8)
    return o


def rgb_hash(rgba, w, h):
    t = rgba.copy()
    t[..., 3] = 0
    return hash_content(pack_u32(t), w, h)


def save_png(path, rgba, level=6):
    Image.fromarray(rgba, "RGBA").save(path, compress_level=level)


# ------------------------------------------------------------------------------------------------ extract
def cmd_extract(a):
    iso = Iso(a.iso)
    src = os.path.join(a.work, "src")
    os.makedirs(src, exist_ok=True)
    index = {}
    t0 = time.time()
    total = bad = 0
    for name, lba, size in iso.files():
        if not name.upper().endswith(".TXD") or not name.upper().startswith("ENGLISH/"):
            continue
        d = iso.read(lba, size)
        txd = os.path.splitext(os.path.basename(name))[0].upper()
        _, _, _, _, ents = iop_srsc.parse(d)
        n = 0
        for t, tid, sub, o, sz in ents:
            if t != 0x40:
                continue
            total += 1
            try:
                w, h, rgba, info = decode_runtime(d[o : o + sz])
            except Exception as e:
                bad += 1
                print("  undecodable %s id %d: %s" % (name, tid, e))
                continue
            u = pack_u32(rgba)
            hx = hash_content(u, w, h)
            n += 1
            e = index.get(hx)
            if e is None:
                e = index[hx] = dict(
                    w=w,
                    h=h,
                    bpp=info["bpp"],
                    kind=info["kind"],
                    src=[],
                    ui=txd in UI_TXDS,
                    rgb=rgb_hash(rgba, w, h),
                    aconst=bool((rgba[..., 3] == rgba[0, 0, 3]).all()),
                    a0=int(rgba[0, 0, 3]),
                    amax=int(rgba[..., 3].max()),
                    apartial=float(((rgba[..., 3] > 0) & (rgba[..., 3] < 0x80)).mean()),
                )
                save_png(os.path.join(src, hx + ".png"), to_png_rgba(rgba), 1)
            e["src"].append("%s#%d" % (name.split("/", 1)[1] if "/" in name else name, tid))
            if txd not in UI_TXDS:
                e["ui"] = e["ui"] and False
        print("%-48s %4d textures   (%d unique so far, %.0f s)" % (name, n, len(index), time.time() - t0), flush=True)
    json.dump(index, open(os.path.join(a.work, "index.json"), "w"))
    print("textures %d undecodable %d unique %d" % (total, bad, len(index)))


# ------------------------------------------------------------------------------------------------ compare with a runtime dump
def load_dump(dump):
    out = {}
    for f in glob.glob(os.path.join(dump, "*.png")):
        b = os.path.basename(f)
        if len(b) == 20:
            out[b[:16]] = f
    return out


def cmd_compare(a):
    index = json.load(open(os.path.join(a.work, "index.json")))
    rgbs = {}
    for h, e in index.items():
        rgbs.setdefault(e["rgb"], []).append(h)
    dump = load_dump(a.dump)
    exact = viargb = miss = 0
    missing = []
    for hx, f in dump.items():
        if hx in index:
            exact += 1
            continue
        im = np.array(Image.open(f).convert("RGBA"))
        g = from_png_rgba(im)
        r = rgb_hash(g, im.shape[1], im.shape[0])
        if r in rgbs:
            viargb += 1
        else:
            miss += 1
            missing.append((hx, im.shape[1], im.shape[0], f))
    n = len(dump)
    print(
        "dump textures %d: exact hash match %d (%.1f%%), RGB-only match %d (%.1f%%), no match %d (%.1f%%)"
        % (n, exact, 100 * exact / max(1, n), viargb, 100 * viargb / max(1, n), miss, 100 * miss / max(1, n))
    )
    for m in missing[:40]:
        print("  no match", m[0], "%dx%d" % (m[1], m[2]))
    return missing


def cmd_hybrid(a):
    """dump textures that have no TXD source (hash and RGB hash unknown) are copied into work/src_dump (and listed in index_dump.json)"""
    index = json.load(open(os.path.join(a.work, "index.json")))
    rgbs = {e["rgb"] for e in index.values()}
    dump = load_dump(a.dump)
    dst = os.path.join(a.work, "src_dump")
    os.makedirs(dst, exist_ok=True)
    extra = {}
    for hx, f in dump.items():
        if hx in index:
            continue
        im = np.array(Image.open(f).convert("RGBA"))
        g = from_png_rgba(im)
        h, w = g.shape[:2]
        r = rgb_hash(g, w, h)
        if r in rgbs:
            continue
        shutil.copyfile(f, os.path.join(dst, hx + ".png"))
        extra[hx] = dict(
            w=w,
            h=h,
            rgb=r,
            ui=False,
            src=["dump"],
            aconst=bool((g[..., 3] == g[0, 0, 3]).all()),
            a0=int(g[0, 0, 3]),
            amax=int(g[..., 3].max()),
            apartial=float(((g[..., 3] > 0) & (g[..., 3] < 0x80)).mean()),
            bpp=32,
            kind=0,
        )
    p = os.path.join(a.work, "index_dump.json")
    old = json.load(open(p)) if os.path.exists(p) else {}
    old.update(extra)
    json.dump(old, open(p, "w"))
    print("hybrid: %d dump textures without TXD source added (%d total)" % (len(extra), len(old)))


# ------------------------------------------------------------------------------------------------ upscale
def tile_axis_ok(rgb):
    """per axis: is the opposite edge as similar as neighbouring rows/columns (a tileable texture)? -> (tile_x, tile_y)"""
    f = rgb.astype(np.float32)
    h, w = f.shape[:2]
    res = []
    for ax, n in ((1, w), (0, h)):
        if n < 8:
            res.append(False)
            continue
        first = np.take(f, 0, axis=ax)
        last = np.take(f, n - 1, axis=ax)
        second = np.take(f, 1, axis=ax)
        prev = np.take(f, n - 2, axis=ax)
        seam = np.abs(first - last).mean()
        nb = (np.abs(first - second).mean() + np.abs(last - prev).mean()) / 2
        res.append(bool(seam <= nb * 1.6 + 2.0))
    return res[0], res[1]


def pad_image(img, px, py, tx, ty):
    """pad with wrap for tiling axes, mirror otherwise"""
    if px:
        img = np.pad(img, ((0, 0), (px, px), (0, 0)), mode="wrap" if tx else "symmetric")
    if py:
        img = np.pad(img, ((py, py), (0, 0), (0, 0)), mode="wrap" if ty else "symmetric")
    return img


def bleed(rgb, mask, iters=24):
    """fill pixels where mask is False with the colour of neighbouring True pixels (no dark halo around cut-outs)"""
    img = rgb.astype(np.float32)
    filled = mask.copy()
    for _ in range(iters):
        if filled.all():
            break
        acc = np.zeros_like(img)
        cnt = np.zeros(filled.shape, np.float32)
        wgt = filled[..., None].astype(np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(img * wgt, (dy, dx), (0, 1))
            cnt += np.roll(filled.astype(np.float32), (dy, dx), (0, 1))
        new = (~filled) & (cnt > 0)
        if not new.any():
            break
        img[new] = acc[new] / cnt[new][:, None]
        filled |= new
    return np.clip(img + 0.5, 0, 255).astype(np.uint8)


def run_esrgan(exe, model, indir, outdir, scale=4, gpu=None, tile=0):
    cmd = [exe, "-i", indir, "-o", outdir, "-n", model, "-s", str(scale), "-f", "png", "-t", str(tile), "-j", "4:6:4"]
    if gpu is not None:
        cmd += ["-g", str(gpu)]
    r = subprocess.run(cmd, cwd=os.path.dirname(exe), capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise SystemExit("realesrgan failed")


def cmd_upscale(a):
    exe = os.path.abspath(a.exe)
    index = json.load(open(os.path.join(a.work, "index.json")))
    srcdirs = {h: os.path.join(a.work, "src") for h in index}
    p2 = os.path.join(a.work, "index_dump.json")
    if os.path.exists(p2):
        for h, e in json.load(open(p2)).items():
            if h not in index:
                index[h] = e
                srcdirs[h] = os.path.join(a.work, "src_dump")
    os.makedirs(a.out, exist_ok=True)
    todo = [h for h in sorted(index) if not os.path.exists(os.path.join(a.out, h + ".png"))]
    if a.limit:
        todo = todo[: a.limit]
    print("upscale: %d of %d textures to do" % (len(todo), len(index)), flush=True)
    t0 = time.time()
    scale = a.scale
    done = 0
    PAD = 16
    for c0 in range(0, len(todo), a.chunk):
        chunk = todo[c0 : c0 + a.chunk]
        tmp = os.path.join(a.work, "tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        dirs = {k: os.path.join(tmp, k) for k in ("rgb_x4", "rgb_anime", "alpha")}
        for k, d in dirs.items():
            os.makedirs(d + "_in")
            os.makedirs(d + "_out")
        meta = {}
        for h in chunk:
            e = index[h]
            im = np.array(Image.open(os.path.join(srcdirs[h], h + ".png")).convert("RGBA"))
            hh, ww = im.shape[:2]
            rgb, alpha = im[..., :3], im[..., 3]
            tx, ty = tile_axis_ok(rgb)
            px, py = min(PAD, ww), min(PAD, hh)
            ac = bool((alpha == alpha[0, 0]).all())  # constant alpha (opaque, or CT24 alpha 0)
            if not ac:
                # the alpha plane is padded the same way: an axis only wraps if alpha tiles too. Sky / light-ray
                # layers are constant white RGB with the picture in alpha; wrapping them bleeds the opposite edge
                # into the border texels, which shows as thin lines along the clamped sky strip edges.
                ax, ay = tile_axis_ok(np.repeat(alpha[..., None], 3, 2))
                tx, ty = tx and ax, ty and ay
            if not ac:
                rgb = bleed(pad_image(rgb, px, py, tx, ty), pad_image(alpha[..., None], px, py, tx, ty)[..., 0] > 0)
                ap = pad_image(alpha[..., None], px, py, tx, ty)[..., 0]
            else:
                rgb = pad_image(rgb, px, py, tx, ty)
                ap = None
            model_dir = "rgb_anime" if e.get("ui") else "rgb_x4"
            Image.fromarray(rgb, "RGB").save(os.path.join(dirs[model_dir] + "_in", h + ".png"))
            binary = bool(set(np.unique(alpha).tolist()) <= {0, 255})
            # constant RGB with a soft alpha (sky panorama / cloud / light-ray layers): the alpha is the whole picture
            # and the anime model invents strokes, glyphs and hard tile seams in it; resample it smoothly instead
            smooth_alpha = ap is not None and not binary and bool((im[..., :3] == im[0, 0, :3]).all())
            if ap is not None and not smooth_alpha:
                Image.fromarray(np.repeat(ap[..., None], 3, 2), "RGB").save(
                    os.path.join(dirs["alpha"] + "_in", h + ".png")
                )
            meta[h] = dict(
                px=px,
                py=py,
                w=ww,
                h=hh,
                ac=ac,
                a0=int(alpha[0, 0]),
                model=model_dir,
                amax=int(alpha.max()),
                binary=binary,
                smooth_alpha=smooth_alpha,
                ap=ap if smooth_alpha else None,
            )
        for k, model in (
            ("rgb_x4", "realesrgan-x4plus"),
            ("rgb_anime", "realesrgan-x4plus-anime"),
            ("alpha", "realesrgan-x4plus-anime"),
        ):
            if any(os.scandir(dirs[k] + "_in")):
                run_esrgan(exe, model, dirs[k] + "_in", dirs[k] + "_out", 4, a.gpu)
        for h, m in meta.items():
            up = np.array(Image.open(os.path.join(dirs[m["model"]] + "_out", h + ".png")).convert("RGB"))
            S = up.shape[1] // (m["w"] + 2 * m["px"])  # esrgan native 4x
            cx, cy = m["px"] * S, m["py"] * S
            up = up[cy : cy + m["h"] * S, cx : cx + m["w"] * S]
            if m["ac"]:
                al = np.full(up.shape[:2], m["a0"], np.uint8)
            elif m["smooth_alpha"]:
                ua = Image.fromarray(m["ap"], "L").resize((m["ap"].shape[1] * S, m["ap"].shape[0] * S), Image.LANCZOS)
                al = np.array(ua)[cy : cy + m["h"] * S, cx : cx + m["w"] * S]
            else:
                ua = np.array(Image.open(os.path.join(dirs["alpha"] + "_out", h + ".png")).convert("L"))
                ua = ua[cy : cy + m["h"] * S, cx : cx + m["w"] * S].astype(np.float32) / 255
                # keep alpha test edges crisp: the network output is soft / ringing, push it towards a 2 px ramp
                k = 6.0 if m["binary"] else 1.0
                ua = np.clip((ua - 0.5) * k + 0.5, 0, 1)
                al = (ua * 255 + 0.5).astype(np.uint8)
            if scale != S:
                im = Image.fromarray(np.dstack([up, al]), "RGBA").resize(
                    (m["w"] * scale, m["h"] * scale), Image.LANCZOS
                )
                out = np.array(im)
            else:
                out = np.dstack([up, al])
            save_png(os.path.join(a.out, h + ".png"), out, 6)
            done += 1
        el = time.time() - t0
        print(
            "  %d / %d done, %.0f s elapsed, eta %.0f s" % (done, len(todo), el, el / done * (len(todo) - done)),
            flush=True,
        )
    shutil.rmtree(os.path.join(a.work, "tmp"), ignore_errors=True)
    # alias file (rgb hash -> hash) for all textures the pack contains
    lines = []
    seen = set()
    for h, e in sorted(index.items()):
        if e["rgb"] != h and e["rgb"] not in seen and os.path.exists(os.path.join(a.out, h + ".png")):
            seen.add(e["rgb"])
            lines.append("%s %s" % (e["rgb"], h))
    open(os.path.join(a.out, "alias.txt"), "w").write(
        "# <rgbhash> <hash>: same picture, different palette alpha\n" + "\n".join(lines) + "\n"
    )
    print("upscale finished: %d textures in %.0f s, alias entries %d" % (done, time.time() - t0, len(lines)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["extract", "compare", "hybrid", "upscale", "all"])
    p.add_argument("--iso", default=os.environ.get("DRAKAN_ISO", DEFAULT_ISO))
    p.add_argument("--work", default=os.path.join(ROOT, "build", "hdpack"))
    p.add_argument("--dump")
    p.add_argument("--out", default=os.path.join(ROOT, "dist", "DrakanPC", "textures"))
    p.add_argument("--exe", default=os.path.join(ROOT, "tools", "hdpack", "realesrgan", "realesrgan-ncnn-vulkan.exe"))
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--chunk", type=int, default=300)
    p.add_argument("--scale", type=int, default=4)
    p.add_argument("--gpu", type=int, default=None)
    a = p.parse_args()
    if a.cmd in ("extract", "all") and not a.iso:
        p.error("pass --iso <disc image> or set DRAKAN_ISO")
    os.makedirs(a.work, exist_ok=True)
    if a.cmd in ("extract", "all"):
        cmd_extract(a)
    if a.cmd == "compare":
        cmd_compare(a)
    if a.cmd == "hybrid":
        cmd_hybrid(a)
    if a.cmd in ("upscale", "all"):
        cmd_upscale(a)


if __name__ == "__main__":
    main()
