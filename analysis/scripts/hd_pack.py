r"""HD texture pack generator for the Drakan PC port (static recompilation). Reads YOUR disc image, writes the pack next to YOUR game.

  python -I hd_pack.py extract  [--iso ISO] [--work DIR]            decode every ENGLISH TXD texture exactly like the renderer (gsr) does, hash it with the
                                                                     renderer's content hash, write work/src/<hash>.png + work/index.json
  python -I hd_pack.py compare  --dump DIR [--work DIR]              match rate of the offline hashes against a texture_dump of a real game run
  python -I hd_pack.py hybrid   --dump DIR [--work DIR]              add dump textures whose hash is not in the TXD index (runtime generated / edited ones)
  python -I hd_pack.py modextract --zip MOD.zip [--work DIR]         same for a converted mod pack (OotF levels): every SRSC member of the zip (TXD / ODB ...),
                                                                     decoded RGBA hashed (with <zip>.hdsrc: the converter's lossless source, which can be
                                                                     smaller than the guest texture, is what gets upscaled); then run 'upscale --work DIR --out ...'
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
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time

# The guided build keeps numpy / Pillow in its own folder when the interpreter has none (scripts/buildtool/extras.py).
for _site in filter(None, os.environ.get("DRAKAN_PY_SITE", "").split(os.pathsep)):
    sys.path.insert(0, _site)

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


# Version of the lossless sources an index entry was made from. 1: the final-size picture before quantising (2026-11-02). 2: the
# converter's source pixels before any resize (terrain enlarged 64 -> 128, textures squared for skinned and display-list models), so
# a source can be smaller than the guest texture (sw / sh). exact_done.txt lists "<hash>@2" for version 2 (a bare hash = version 1),
# so pack images made from an older source are redone once.
EXACT_VERSION = 2


def mark_exact(e, sw, sh):
    e.update(exact=True, exactv=EXACT_VERSION, sw=int(sw), sh=int(sh))


def exact_current(e, sw, sh):
    return bool(e.get("exact")) and e.get("exactv") == EXACT_VERSION and (e.get("sw"), e.get("sh")) == (sw, sh)


def exact_token(h, e):
    return "%s@%d" % (h, e["exactv"]) if e.get("exactv", 1) > 1 else h


def cmd_modextract(a):
    """Index the textures of a converted mod pack (zip) the way cmd_extract does for the disc. Work dir defaults to build/hdpack-mod."""
    import zipfile

    src = os.path.join(a.work, "src")
    os.makedirs(src, exist_ok=True)
    ipath = os.path.join(a.work, "index.json")
    index = json.load(open(ipath)) if os.path.exists(ipath) else {}
    total = bad = 0
    zp = os.path.join(a.work, "zips.json")  # zips already indexed: (size, mtime) -> skip when unchanged
    zdone = json.load(open(zp)) if os.path.exists(zp) else {}
    zkey = os.path.abspath(a.zip)
    st = os.stat(a.zip)
    sig = [st.st_size, int(st.st_mtime)]
    if zdone.get(zkey) == sig and not getattr(a, "force", False):
        print("modextract: %s unchanged since the last index, skipped" % a.zip)
        return
    z = zipfile.ZipFile(a.zip)
    hdsrc = (
        a.zip + ".hdsrc"
    )  # lossless sources of textures the converter quantised or resized (ootf-convert.ts --hd), by SHA-1 of the record
    exact_n = 0
    for name in z.namelist():
        if name.endswith("/"):
            continue
        d = z.read(name)
        if d[:4] != b"SRSC":
            continue
        try:
            _, _, _, _, ents = iop_srsc.parse(d)
        except Exception:
            continue
        n = 0
        for t, tid, sub, o, sz in ents:
            if t != 0x40:
                continue
            total += 1
            try:
                w, h, rgba, info = decode_runtime(d[o : o + sz])
            except Exception:
                bad += 1
                continue
            hx = hash_content(pack_u32(rgba), w, h)
            n += 1
            ex = os.path.join(hdsrc, hashlib.sha1(d[o : o + sz]).hexdigest() + ".rgba")
            exact = None
            if os.path.exists(ex):
                raw = open(ex, "rb").read()
                ew, eh = struct.unpack("<II", raw[:8])
                exact = np.frombuffer(raw, np.uint8, ew * eh * 4, 8).reshape(eh, ew, 4)
            e = index.get(hx)
            if e is not None and exact is not None and not exact_current(e, ew, eh):
                # earlier index had the quantised picture, or an older converter's final-size source
                mark_exact(e, ew, eh)
                Image.fromarray(exact, "RGBA").save(os.path.join(src, hx + ".png"))
                exact_n += 1
            if e is None:
                e = index[hx] = dict(
                    w=w, h=h, bpp=info["bpp"], kind=info["kind"], src=[], ui=False, rgb=rgb_hash(rgba, w, h),
                    aconst=bool((rgba[..., 3] == rgba[0, 0, 3]).all()), a0=int(rgba[0, 0, 3]), amax=int(rgba[..., 3].max()),
                    apartial=float(((rgba[..., 3] > 0) & (rgba[..., 3] < 0x80)).mean()),
                )  # fmt: skip
                if exact is not None:
                    mark_exact(e, ew, eh)
                    Image.fromarray(exact, "RGBA").save(
                        os.path.join(src, hx + ".png")
                    )  # PNG alpha as the lossless source has it
                    exact_n += 1
                else:
                    save_png(os.path.join(src, hx + ".png"), to_png_rgba(rgba), 1)
            e["src"].append("%s#%d" % (name, tid))
        if n:
            print("%-60s %4d textures (%d unique)" % (name, n, len(index)), flush=True)
    json.dump(index, open(ipath, "w"))
    zdone[zkey] = sig
    json.dump(zdone, open(zp, "w"))
    print("textures %d undecodable %d unique %d lossless sources %d" % (total, bad, len(index), exact_n))


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
        raise RuntimeError("realesrgan failed (exit %d)" % r.returncode)


def resize_padded(img, target, tx, ty):
    """Lanczos resize of an HxWxC uint8 image to target (w, h) that wraps tiling axes (mirrors the others), so a tiling
    texture stays seamless: a plain resize clamps at the border and leaves a visible seam where the tiles meet."""
    if img.ndim == 3 and img.shape[2] == 4:
        # colour and alpha apart: Pillow resizes RGBA premultiplied, which blackens the colour under alpha 0 (the
        # bled hole colour) and gives cut-outs dark fringes once the renderer filters across their edges
        rgb = resize_padded(img[..., :3], target, tx, ty)
        al = resize_padded(np.repeat(img[..., 3:], 3, 2), target, tx, ty)[..., 0]
        return np.dstack([rgb, al])
    h, w = img.shape[:2]
    tw, th = target
    px, py = min(w, max(4, w // 16)), min(h, max(4, h // 16))  # at least the Lanczos support (3 px)
    padded = pad_image(img, px, py, tx, ty)
    sx, sy = tw / w, th / h
    big = np.array(
        Image.fromarray(padded).resize((round(padded.shape[1] * sx), round(padded.shape[0] * sy)), Image.LANCZOS)
    )
    ox, oy = round(px * sx), round(py * sy)
    out = big[oy : oy + th, ox : ox + tw]
    if out.shape[:2] != (th, tw):  # rounding left the crop short: plain resize
        out = np.array(Image.fromarray(img).resize((tw, th), Image.LANCZOS))
    return np.ascontiguousarray(out)


def run_esrgan_retry(exe, model, folder, gpu, label):
    """<folder>_in -> <folder>_out; nothing to do for an empty input. On a failure (out of VRAM, driver hiccup) one tiled retry,
    then the textures are left without output (the caller reports them as failed)."""
    if not any(os.scandir(folder + "_in")):
        return
    try:
        run_esrgan(exe, model, folder + "_in", folder + "_out", 4, gpu)
    except RuntimeError:
        try:
            run_esrgan(exe, model, folder + "_in", folder + "_out", 4, gpu, tile=128)
        except RuntimeError as ex:
            print("  %s chunk failed twice (%s); its textures are skipped, rerun to retry" % (label, ex))


def first_pass(dirs, h, m):
    """The network's x4 output of texture h with its padding cropped: (image, factor, crop x, crop y)."""
    up = np.array(Image.open(os.path.join(dirs[m["model"]] + "_out", h + ".png")).convert("RGB"))
    s = up.shape[1] // (m["w"] + 2 * m["px"])  # esrgan native 4x
    cx, cy = m["px"] * s, m["py"] * s
    return up[cy : cy + m["h"] * s, cx : cx + m["w"] * s], s, cx, cy


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
    edp = os.path.join(a.out, "exact_done.txt")  # hashes whose pack file was made from the lossless source
    edone = set(open(edp).read().split()) if os.path.exists(edp) else set()
    todo = [
        h
        for h in sorted(index)
        if not os.path.exists(os.path.join(a.out, h + ".png"))
        or (index[h].get("exact") and exact_token(h, index[h]) not in edone)
    ]
    if a.limit:
        todo = todo[: a.limit]
    print("upscale: %d of %d textures to do" % (len(todo), len(index)), flush=True)
    t0 = time.time()
    scale = a.scale
    done = 0
    failed = []
    PAD = 16
    for c0 in range(0, len(todo), a.chunk):
        chunk = todo[c0 : c0 + a.chunk]
        tmp = os.path.join(a.work, "tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        dirs = {k: os.path.join(tmp, k) for k in ("rgb_x4", "rgb_anime", "alpha", "rgb_x4_2")}
        for k, d in dirs.items():
            os.makedirs(d + "_in")
            os.makedirs(d + "_out")
        meta = {}
        for h in chunk:
            e = index[h]
            im = np.array(Image.open(os.path.join(srcdirs[h], h + ".png")).convert("RGBA"))
            hh, ww = im.shape[:2]
            gw, gh = int(e.get("w", ww)), int(e.get("h", hh))
            small = bool((im[..., 3] == im[0, 0, 3]).all()) and gw >= 2 * ww and gh >= 2 * hh
            if small and a.small_source == "enlarge":
                # an opaque converter source at most half its guest size (OotF ground: 64 px PC tile, 128 px guest):
                # enlarged smoothly to the guest size first, then one network pass. The network reads the soft
                # enlargement as fine texture (grass blades); fed the 64 px tile itself it flattens grass into a smear
                # (journal 2026-11-12/ootf-texture-quality.md)
                im = resize_padded(im, (gw, gh), *tile_axis_ok(im[..., :3]))
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
            # --small-source esrgan: the network runs twice on such a source (source x4, brought to the guest size, x4
            # again). Sharper cracks on rock, but grass turns flat; not the default
            pass2 = a.small_source == "esrgan" and small and model_dir == "rgb_x4"
            meta[h] = dict(
                px=px,
                py=py,
                w=ww,
                h=hh,
                gw=gw,
                gh=gh,
                pass2=pass2,
                tx=tx,
                ty=ty,
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
            run_esrgan_retry(exe, model, dirs[k], a.gpu, k)

        for h, m in meta.items():  # second pass for sources smaller than their guest texture
            if m["pass2"] and os.path.exists(os.path.join(dirs["rgb_x4"] + "_out", h + ".png")):
                # the first pass (source x4) brought down to the guest size, then x4 again: lands on guest x4 directly,
                # with a quarter of the pixels a second pass over the whole first-pass output would cost
                up = resize_padded(first_pass(dirs, h, m)[0], (m["gw"], m["gh"]), m["tx"], m["ty"])
                qx, qy = min(PAD, up.shape[1]), min(PAD, up.shape[0])
                Image.fromarray(pad_image(up, qx, qy, m["tx"], m["ty"]), "RGB").save(
                    os.path.join(dirs["rgb_x4_2"] + "_in", h + ".png")
                )
                m["q"] = (qx, qy, up.shape[1], up.shape[0])
        run_esrgan_retry(exe, "realesrgan-x4plus", dirs["rgb_x4_2"], a.gpu, "rgb_x4 second pass")
        for h, m in meta.items():
            if not os.path.exists(os.path.join(dirs[m["model"]] + "_out", h + ".png")):
                failed.append(h)
                continue
            up, S, cx, cy = first_pass(dirs, h, m)
            p2 = os.path.join(dirs["rgb_x4_2"] + "_out", h + ".png")
            if m.get("q") and os.path.exists(p2):
                qx, qy, qw, qh = m["q"]
                up = np.array(Image.open(p2).convert("RGB"))[qy * 4 : qy * 4 + qh * 4, qx * 4 : qx * 4 + qw * 4]
            elif m.get("q"):
                print("  %s: second pass missing, the first pass is resized instead" % h)
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
            # the pack image is the guest texture's size times --scale; a converter source of another size or shape
            # (smaller terrain tile, a texture the converter squared) is resampled to it here
            target = (m["gw"] * scale, m["gh"] * scale)
            if al.shape != up.shape[:2]:
                al = np.array(Image.fromarray(al, "L").resize((up.shape[1], up.shape[0]), Image.LANCZOS))
            out = np.dstack([up, al])
            if (out.shape[1], out.shape[0]) != target:
                out = resize_padded(out, target, m["tx"], m["ty"])
                if not m["ac"] and m["binary"]:  # the resize softened the cut-out edge again: back to a ~2 px ramp
                    edge = np.clip((out[..., 3].astype(np.float32) / 255 - 0.5) * 6 + 0.5, 0, 1)
                    out[..., 3] = (edge * 255 + 0.5).astype(np.uint8)
            save_png(os.path.join(a.out, h + ".png"), out, 6)
            if index[h].get("exact"):
                edone.add(exact_token(h, index[h]))
            done += 1
        el = time.time() - t0
        print(
            "  %d / %d done, %.0f s elapsed, eta %.0f s"
            % (done, len(todo), el, el / max(done, 1) * (len(todo) - done)),
            flush=True,
        )
    shutil.rmtree(os.path.join(a.work, "tmp"), ignore_errors=True)
    open(edp, "w").write(" ".join(sorted(edone)))
    # alias file (rgb hash -> hash) for all textures the pack contains
    lines = []
    seen = set()
    ap = os.path.join(a.out, "alias.txt")
    if os.path.exists(ap):  # keep entries of earlier runs (disc pack + mod packs share one folder)
        for ln in open(ap):
            f = ln.split()
            if len(f) == 2 and not ln.startswith("#") and f[0] not in seen:
                seen.add(f[0])
                lines.append(ln.strip())
    for h, e in sorted(index.items()):
        if e["rgb"] != h and e["rgb"] not in seen and os.path.exists(os.path.join(a.out, h + ".png")):
            seen.add(e["rgb"])
            lines.append("%s %s" % (e["rgb"], h))
    open(os.path.join(a.out, "alias.txt"), "w").write(
        "# <rgbhash> <hash>: same picture, different palette alpha\n" + "\n".join(lines) + "\n"
    )
    print("upscale finished: %d textures in %.0f s, alias entries %d" % (done, time.time() - t0, len(lines)))
    if failed:
        print("upscale: %d textures failed (rerun upscale to retry): %s" % (len(failed), " ".join(failed[:10])))
        return 1


# ------------------------------------------------------------------------------------------------ refit to the source
REFIT_VERSION = 1
PURE_UI_TXDS = ("INTERFCE", "SYSTEM", "DEBUG", "PS2MUSIC")


def _blur_wrap(img, sigma, tx, ty):
    """Gaussian blur of an HxW float image: wrap-around on tiling axes, mirrored border on the others."""
    if sigma <= 0:
        return img
    h, w = img.shape
    px = 0 if tx else min(w, int(np.ceil(3 * sigma)) + 1)
    py = 0 if ty else min(h, int(np.ceil(3 * sigma)) + 1)
    if px or py:
        img = np.pad(img, ((py, py), (px, px)), mode="symmetric")
    hh, ww = img.shape
    fy = np.fft.fftfreq(hh)[:, None]
    fx = np.fft.rfftfreq(ww)[None, :]
    out = np.fft.irfft2(np.fft.rfft2(img) * np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy)), s=(hh, ww))
    return out[py : py + h, px : px + w]


def _enlarge_wrap(img, f, tx, ty):
    """Bicubic enlargement of an HxW float image by the integer factor f; wraps tiling axes (no seam), mirrors the others."""
    pad = 2
    p = np.pad(img, ((pad, pad), (0, 0)), mode="wrap" if ty else "symmetric")
    p = np.pad(p, ((0, 0), (pad, pad)), mode="wrap" if tx else "symmetric")
    big = np.array(Image.fromarray(p.astype(np.float32), "F").resize((p.shape[1] * f, p.shape[0] * f), Image.BICUBIC))
    return big[pad * f : pad * f + img.shape[0] * f, pad * f : pad * f + img.shape[1] * f]


def soft_alpha(alpha):
    """A picture drawn in alpha (smoke, cloud, glow): many alpha levels and a large share of in-between texels. A
    cut-out with an anti-aliased rim is not one."""
    nz = alpha > 0
    if not nz.any():
        return False
    top = int(alpha.max())
    between = (alpha > 0) & (alpha < top)
    return len(np.unique(alpha)) >= 16 and float(between.sum()) / float(nz.sum()) >= 0.25


def refit_image(src, hd, ui_alpha=False):
    """Make an upscaled image agree with its source: src HxWx4, hd (H*f)x(W*f)x4, both uint8 with PNG alpha.
    -> (image, info) or (None, reason).

    The network may add detail finer than a source texel; it may not move the picture. Two corrections:
      * colour: the difference between the source and the image averaged back down to source size, low-passed over one
        source texel (so the network's own fine detail stays), is added back. It removes the brightness and hue drift of
        the network (up to 20 levels on some textures) and restores texel-scale grain where the network flattened a
        texture (then there is no low-pass: the result averages to the source almost exactly).
      * soft alpha (smoke, clouds, glows; not HUD art): the alpha plane is the source's, enlarged smoothly. The network
        draws strokes and shards into soft alpha.
    Transparent source texels do not take part (their colour is bled filler), tiling axes wrap."""
    h, w = src.shape[:2]
    if hd.shape[0] % h or hd.shape[1] % w or hd.shape[0] // h != hd.shape[1] // w:
        return None, "size is not a whole multiple of the source"
    f = hd.shape[1] // w
    if f < 2:
        return None, "not enlarged"
    tx, ty = tile_axis_ok(src[..., :3])
    alpha = src[..., 3]
    aconst = bool((alpha == alpha[0, 0]).all())
    if not aconst:
        ax, ay = tile_axis_ok(np.repeat(alpha[..., None], 3, 2))
        tx, ty = tx and ax, ty and ay
    vis = np.ones((h, w), np.float32) if aconst else (alpha > 0).astype(np.float32)
    if vis.sum() < 4:
        return None, "nothing visible"
    s = src[..., :3].astype(np.float32)
    hdf = hd[..., :3].astype(np.float32)
    down = hdf.reshape(h, f, w, f, 3).mean(axis=(1, 3))
    res = (s - down) * vis[..., None]

    def grain(img):  # texel-scale contrast of the visible part
        g = img.mean(-1)
        return float(np.sqrt((((g - _blur_wrap(g, 1.0, tx, ty)) * vis) ** 2).sum() / vis.sum()))

    gs, gd = grain(s), grain(down)
    flattened = gs > 1.5 and gd < 0.5 * gs
    sigma = 0.0 if flattened else 1.0
    wsum = np.maximum(_blur_wrap(vis, sigma, tx, ty), 1e-3)
    out = hd.copy()
    for c in range(3):
        low = _blur_wrap(res[..., c], sigma, tx, ty) / wsum
        out[..., c] = np.clip(hdf[..., c] + _enlarge_wrap(low, f, tx, ty) + 0.5, 0, 255).astype(np.uint8)
    smooth = (not aconst) and (not ui_alpha) and soft_alpha(alpha)
    if smooth:
        out[..., 3] = np.clip(_enlarge_wrap(alpha.astype(np.float32), f, tx, ty) + 0.5, 0, 255).astype(np.uint8)
    before = float(np.abs(res.sum(axis=(0, 1)) / vis.sum()).max())
    after_down = out[..., :3].astype(np.float32).reshape(h, f, w, f, 3).mean(axis=(1, 3))
    after = float(np.abs(((s - after_down) * vis[..., None]).sum(axis=(0, 1)) / vis.sum()).max())
    return out, dict(shift_before=before, shift_after=after, flattened=flattened, soft_alpha=smooth)


def cmd_refit(a):
    """Refit every pack image that has an index entry to its source (refit_image). Runs once per image: the pack's
    refit_done.txt lists "<hash>@<version>". No GPU. Files are replaced, never written in place."""
    index = json.load(open(os.path.join(a.work, "index.json")))
    srcdirs = {h: os.path.join(a.work, "src") for h in index}
    p2 = os.path.join(a.work, "index_dump.json")
    if os.path.exists(p2):
        for h, e in json.load(open(p2)).items():
            if h not in index:
                index[h] = e
                srcdirs[h] = os.path.join(a.work, "src_dump")
    dest = os.path.abspath(a.refit_out) if getattr(a, "refit_out", None) else a.out
    os.makedirs(dest, exist_ok=True)
    rp = os.path.join(dest, "refit_done.txt")
    done = set(open(rp).read().split()) if os.path.exists(rp) else set()

    def token(h, folder):  # a file made again by a later upscale has another size and is refitted again
        return "%s@%d:%d" % (h, REFIT_VERSION, os.path.getsize(os.path.join(folder, h + ".png")))

    todo = [
        h
        for h in sorted(index)
        if os.path.exists(os.path.join(a.out, h + ".png"))
        and not (os.path.exists(os.path.join(dest, h + ".png")) and token(h, dest) in done)
    ]
    if a.limit:
        todo = todo[: a.limit]
    print("refit: %d of %d images to do" % (len(todo), len(index)), flush=True)
    t0 = time.time()
    changed = flat = soft = 0
    worst = 0.0
    for n, h in enumerate(todo):
        e = index[h]
        src = np.array(Image.open(os.path.join(srcdirs[h], h + ".png")).convert("RGBA"))
        gw, gh = int(e.get("w", src.shape[1])), int(e.get("h", src.shape[0]))
        if (src.shape[1], src.shape[0]) != (gw, gh):  # a converter source of another size: compare at the guest size
            src = resize_padded(src, (gw, gh), *tile_axis_ok(src[..., :3]))
        hd = np.array(Image.open(os.path.join(a.out, h + ".png")).convert("RGBA"))
        ui_alpha = any(t in s.upper() for s in e.get("src", []) for t in PURE_UI_TXDS)
        out, info = refit_image(src, hd, ui_alpha)
        if out is not None:
            tmp = os.path.join(dest, "refit-tmp-" + h + ".png")  # not a 16 hex digit name: the game never reads it
            save_png(tmp, out, 6)
            for attempt in range(20):  # the game may have the image open for a moment
                try:
                    os.replace(tmp, os.path.join(dest, h + ".png"))
                    break
                except PermissionError:
                    if attempt == 19:
                        raise
                    time.sleep(0.25)
            changed += 1
            flat += int(info["flattened"])
            soft += int(info["soft_alpha"])
            worst = max(worst, info["shift_before"])
        elif dest != a.out:
            shutil.copyfile(os.path.join(a.out, h + ".png"), os.path.join(dest, h + ".png"))
        done = {t for t in done if not t.startswith(h + "@")}
        done.add(token(h, dest))
        if (n + 1) % 500 == 0:
            print("  %d / %d, %.0f s" % (n + 1, len(todo), time.time() - t0), flush=True)
            open(rp, "w").write(" ".join(sorted(done)))
    open(rp, "w").write(" ".join(sorted(done)))
    print(
        "refit finished: %d images in %.0f s (%d had lost their grain, %d soft alpha planes, largest colour drift %.1f levels)"
        % (changed, time.time() - t0, flat, soft, worst)
    )
    return 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["extract", "modextract", "compare", "hybrid", "upscale", "refit", "all"])
    p.add_argument("--iso", default=os.environ.get("DRAKAN_ISO", DEFAULT_ISO))
    p.add_argument("--work", default=os.path.join(ROOT, "build", "hdpack"))
    p.add_argument("--zip", help="modextract: converted mod pack (e.g. build/ootf-run/<name>/mods/ootf-levels.zip)")
    p.add_argument("--dump")
    p.add_argument("--out", default=os.path.join(ROOT, "dist", "DrakanPC", "textures"))
    p.add_argument("--exe", default=os.path.join(ROOT, "tools", "hdpack", "realesrgan", "realesrgan-ncnn-vulkan.exe"))
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--force", action="store_true", help="modextract: re-index a zip even if unchanged")
    p.add_argument("--chunk", type=int, default=300)
    p.add_argument("--scale", type=int, default=4)
    p.add_argument(
        "--small-source",
        choices=["enlarge", "esrgan", "lanczos"],
        default="enlarge",
        help="upscale: an opaque converter source at most half its guest texture's size is enlarged to the guest size and "
        "upscaled once (enlarge), runs the network twice (esrgan) or once plus a Lanczos resize (lanczos)",
    )
    p.add_argument("--gpu", type=int, default=None)
    p.add_argument(
        "--no-refit",
        action="store_true",
        help="upscale: leave the network's images as they are (no colour / grain / soft alpha refit to the source)",
    )
    p.add_argument(
        "--refit-out", help="refit: write the refitted images to this folder instead of replacing those in --out"
    )
    a = p.parse_args()
    a.work, a.out = os.path.abspath(a.work), os.path.abspath(a.out)  # esrgan runs with cwd = its own folder
    if a.cmd in ("extract", "all") and not a.iso:
        p.error("pass --iso <disc image> or set DRAKAN_ISO")
    os.makedirs(a.work, exist_ok=True)
    if a.cmd in ("extract", "all"):
        cmd_extract(a)
    if a.cmd == "modextract":
        if not a.zip:
            p.error("modextract needs --zip")
        cmd_modextract(a)
    if a.cmd == "compare":
        cmd_compare(a)
    if a.cmd == "hybrid":
        cmd_hybrid(a)
    if a.cmd in ("upscale", "all"):
        failed = cmd_upscale(a) or 0
        if not a.no_refit:
            cmd_refit(a)
        sys.exit(failed)
    if a.cmd == "refit":
        sys.exit(cmd_refit(a))


if __name__ == "__main__":
    main()
