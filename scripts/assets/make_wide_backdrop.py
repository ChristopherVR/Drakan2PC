#!/usr/bin/env python3
"""Build wide menu backdrops from a 4:3 menu image for the renderer's wide-backdrop layer.

The source is shown at 4:3 in the centre exactly as the game draws it (the engine draws the real art over that region,
so only the side margins are new). The margins are a plain fill of the art's own blue sky: a top-to-bottom gradient
taken from the blue pixels of the picture (`--style sky`; also `dusk`, `clouds`, `vignette`, and `template` = magenta margins to feed an outpainting tool). Output names follow `<name>_<a>x<b>.png`, e.g. `screen8_16x9.png`; copy them
into `<texture_dir>/wide/`. The title menu is GUI screen 8; `default` applies to any menu screen without its own image.
See docs/reference/widescreen.md. The input is game-derived art: keep the outputs out of the repository.

    python scripts/assets/make_wide_backdrop.py title.png --name screen8 --out dist/DrakanPC/textures/wide
"""

from __future__ import annotations

import argparse
from fractions import Fraction
from pathlib import Path

import numpy as np
from PIL import Image

try:
    import cv2
except ImportError:  # pragma: no cover - cv2 is a convenience, PIL fallback below
    cv2 = None


def blur(img: np.ndarray, sigma: float) -> np.ndarray:
    if sigma < 0.3:
        return img
    if cv2 is not None:
        return cv2.GaussianBlur(img, (0, 0), sigma, borderType=cv2.BORDER_REFLECT)
    from PIL import ImageFilter

    out = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(sigma))
    return np.asarray(out, dtype=np.float32)


def sky(center: np.ndarray) -> np.ndarray:
    """The art's blue sky as a top-to-bottom gradient, (H, 3): per-row mean of its blue pixels, softened vertically."""
    h = center.shape[0]
    r, g, b = center[..., 0], center[..., 1], center[..., 2]
    mask = ((b > r + 12) & (b > g + 12)).astype(np.float32)
    weight = mask.sum(axis=1)
    rows = (center * mask[..., None]).sum(axis=1) / np.maximum(weight, 1.0)[:, None]
    ok = weight > 8
    ys = np.arange(h)
    for c in range(3):  # rows with no sky (e.g. fully covered by the figures) take their neighbours' colour
        rows[:, c] = np.interp(ys, ys[ok], rows[ok, c])
    return blur(rows[:, None, :].copy(), h * 0.06)[:, 0, :]


STYLES = ("sky", "dusk", "clouds", "vignette", "stretch", "template")


def smoothstep(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def cloud_field(h: int, w: int, rng: np.random.Generator) -> np.ndarray:
    """Fractal value noise in [0, 1], (h, w), stretched horizontally so it reads as streaky cloud banks."""
    total = np.zeros((h, w), np.float32)
    amp, norm = 1.0, 0.0
    for octave in range(6):
        gh = 3 * 2**octave
        gw = max(2, round(gh * w / h / 2.5))
        grid = Image.fromarray(rng.random((gh, gw)).astype(np.float32), mode="F")
        total += amp * np.asarray(grid.resize((w, h), Image.BICUBIC), dtype=np.float32)
        norm += amp
        amp *= 0.55
    total /= norm
    return np.clip((total - total.min()) / max(1e-6, total.max() - total.min()), 0, 1)


def warm_tint(center: np.ndarray) -> np.ndarray:
    """Mean colour of the art's orange cloud pixels, (3,)."""
    r, g, b = center[..., 0], center[..., 1], center[..., 2]
    mask = (r > 150) & (r > b + 60) & (g > b)
    return center[mask].mean(axis=0) if mask.any() else np.array([220.0, 130.0, 70.0], np.float32)


def margin(center: np.ndarray, side: int, left: bool, rng: np.random.Generator, style: str) -> np.ndarray:
    """`side` margin columns for one side, in the given look. Column 0 is the screen edge when `left`."""
    h, cw, _ = center.shape
    rows = sky(center)
    out = np.repeat(rows[:, None, :], side, axis=1)
    d = np.arange(side)[::-1] if left else np.arange(side)  # distance from the seam, in columns
    if style == "template":
        out[:] = (255.0, 0.0, 255.0)  # flat magenta marks the area for an external outpainting tool
        return out
    if style == "vignette":
        out *= (1.0 - 0.7 * smoothstep(d / max(1.0, side)))[None, :, None]
    elif style == "dusk":
        # The art is lit warm on the left and cool on the right: carry that as a soft glow on the left only.
        glow = blur(center[:, : max(4, cw // 30)].mean(axis=1)[:, None, :].copy(), h * 0.12)[:, 0, :]
        g = (0.85 * np.exp(-d / (0.9 * h)))[None, :, None] if left else np.zeros((1, side, 1))
        out = out * (1 - g) + glow[:, None, :] * g
        out *= (1.0 - 0.3 * smoothstep(d / max(1.0, 1.5 * h)))[None, :, None]
    elif style in ("clouds", "stretch"):  # stretch = clouds + the art's edges widened (see stretch_edges)
        field = cloud_field(h, side, rng)
        cover = smoothstep((field - 0.5) / 0.3)[..., None]
        low = (0.35 + 0.65 * np.linspace(0.2, 1.0, h, dtype=np.float32))[
            :, None, None
        ]  # clouds catch more light low down
        out = out * (1 - 0.6 * cover * low) + warm_tint(center)[None, None, :] * 0.6 * cover * low
        out *= (1.0 - 0.3 * smoothstep(d / max(1.0, 1.5 * h)))[None, :, None]
    out += rng.normal(0.0, 1.0, out.shape).astype(np.float32)
    return out


def stretch_edges(canvas: np.ndarray, center: np.ndarray, side: int) -> None:
    """Widen the art itself: its outermost columns are stretched sideways into the margin, in place.

    Source positions ease out as S*(1-(1-x)^p): 1:1 where the stretch begins (so faces and the rider are untouched),
    then ever wider, so Arokh's wing keeps going toward the screen edge instead of being sliced off. How far it can
    reach is capped (`reach` extra px per side); any margin beyond that stays background. The outermost few percent end
    up as a soft smear, which is the price of not inventing a wing. Only for images that replace the engine's art.
    """
    h, cw, _ = center.shape
    zone = max(8, round(cw * 0.10))  # source columns that get stretched
    extra = min(side, round(zone * 2.5))
    total = zone + extra
    p = total / zone
    x = (np.arange(total) + 0.5) / total
    src = zone * (1 - (1 - x) ** p)  # 0..zone, distance into the zone from where the stretch starts
    lo = np.clip(np.floor(src).astype(int), 0, zone - 1)
    hi = np.clip(lo + 1, 0, zone - 1)
    frac = (src - np.floor(src)).astype(np.float32)[None, :, None]
    right = center[:, cw - zone :]
    right = right[:, lo] * (1 - frac) + right[:, hi] * frac
    left = center[:, :zone][:, ::-1]
    left = (left[:, lo] * (1 - frac) + left[:, hi] * frac)[:, ::-1]
    # Where the stretch cannot reach the screen edge (ultrawide), fade its far end into the background instead of
    # leaving a hard line. The part over the art itself always stays fully stretched.
    keep = np.ones(total, np.float32)
    if extra < side:
        j = np.arange(total) - zone  # 0 at the original art edge, growing outward
        keep[zone:] = 1 - smoothstep((j[zone:] / extra - 0.35) / 0.65)
    keep = keep[None, :, None]
    r0, l0 = side + cw - zone, side - extra
    canvas[:, r0 : r0 + total] = canvas[:, r0 : r0 + total] * (1 - keep) + right * keep
    canvas[:, l0 : l0 + total] = canvas[:, l0 : l0 + total] * (1 - keep[:, ::-1]) + left * keep[:, ::-1]


def build(src: Image.Image, aspect: Fraction, height: int, style: str = "sky", seed: int = 1) -> Image.Image:
    cw = round(height * 4 / 3)
    width = round(height * float(aspect))
    width += (width - cw) % 2  # equal margins
    center = np.asarray(src.convert("RGB").resize((cw, height), Image.LANCZOS), dtype=np.float32)
    canvas = np.empty((height, width, 3), np.float32)
    side = (width - cw) // 2
    rng = np.random.default_rng(seed)
    canvas[:, side : side + cw] = center
    if side:
        canvas[:, :side] = margin(center, side, True, rng, style)
        canvas[:, side + cw :] = margin(center, side, False, rng, style)
        if style == "stretch":
            stretch_edges(canvas, center, side)
    return Image.fromarray(np.clip(canvas + 0.5, 0, 255).astype(np.uint8))


def parse_aspect(text: str) -> Fraction:
    a, b = text.split(":")
    return Fraction(int(a), int(b))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", type=Path, help="menu image whose content is shown at 4:3 (any resolution)")
    ap.add_argument("--name", default="default", help="'screen<GUI id>' (title = screen8) or 'default'")
    ap.add_argument("--out", type=Path, required=True, help="output folder, normally <texture_dir>/wide")
    ap.add_argument("--aspects", nargs="+", default=["16:9", "21:9", "32:9"])
    ap.add_argument(
        "--style", choices=STYLES, default="sky", help="look of the margins (template = magenta, for outpainting)"
    )
    ap.add_argument("--height", type=int, help="output height in px (default: source height)")
    args = ap.parse_args()
    src = Image.open(args.source)
    height = args.height or src.height
    args.out.mkdir(parents=True, exist_ok=True)
    for text in args.aspects:
        aspect = parse_aspect(text)
        path = args.out / f"{args.name}_{aspect.numerator}x{aspect.denominator}.png"
        img = build(src, aspect, height, args.style)
        img.save(path, optimize=True)
        print(f"{path}  {img.width}x{img.height}")


if __name__ == "__main__":
    main()
