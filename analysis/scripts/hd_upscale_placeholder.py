r"""Placeholder 'AI upscale' for testing the gsr HD-texture pipeline without Real-ESRGAN: Lanczos xN (+ mild unsharp) of every <hash>.png.
usage: hd_upscale_placeholder.py <in dump dir> <out dir> [factor=4] [--only hash,hash,...]
Alpha is resampled separately (premultiplied colour), names are kept (the 16 hex digit hash IS the key). Replace by realesrgan-ncnn-vulkan for real work
(see docs/reference/hd-textures.md section 6)."""

import glob
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

src, dst = sys.argv[1], sys.argv[2]
f = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 4
only = None
if "--only" in sys.argv:
    only = set(sys.argv[sys.argv.index("--only") + 1].split(","))
os.makedirs(dst, exist_ok=True)
n = 0
for p in sorted(glob.glob(os.path.join(src, "*.png"))):
    h = os.path.basename(p)[:16]
    if only and h not in only:
        continue
    im = Image.open(p).convert("RGBA")
    w, hh = im.size
    a = np.asarray(im).astype(np.float32)
    # CT24/CT16 textures are decoded with TEXA alpha: often the alpha channel is constant (0 or 255) while RGB is the real picture
    # (title background tiles have alpha 0!). Never premultiply in that case, keep the constant alpha.
    const_alpha = a[..., 3].min() == a[..., 3].max()
    pm = a.copy()
    if not const_alpha:
        pm[..., :3] *= pm[..., 3:4] / 255.0
    ch = [Image.fromarray(pm[..., i]).resize((w * f, hh * f), Image.LANCZOS) for i in range(4)]
    o = np.stack([np.asarray(c) for c in ch], -1)
    if const_alpha:
        o[..., 3] = a[0, 0, 3]
    else:
        al = np.clip(o[..., 3:4], 1e-3, 255)
        o[..., :3] = np.clip(o[..., :3] * 255.0 / al, 0, 255)
    o = np.clip(o, 0, 255).astype(np.uint8)
    out = Image.fromarray(o, "RGBA").filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))
    out.save(os.path.join(dst, os.path.basename(p)), optimize=True)
    n += 1
print("upscaled", n, "x%d" % f)
