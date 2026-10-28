"""Render gameui draw lists (gameui_preview's JSON lines) to PNGs for visual review.

usage: python render_drawlist.py <dir-with-jsonl> [outdir]

An approximation of dsettings::GameUiRenderer (panels, focus wash, prompts, text with shadow) over a stand-in
background: a dark "world" or a parchment "map". Item icons are not available offline, so their category
fallback glyph is drawn. Fonts: Georgia (metric-compatible with the game UI's Gelasio) when installed.
"""

from __future__ import annotations

import json
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT_DIRS = [Path("C:/Windows/Fonts"), Path("/usr/share/fonts/truetype/msttcorefonts"), Path("/Library/Fonts")]
BRASS = (176, 141, 87)
BRASS_BRIGHT = (222, 186, 118)
KEYS = ["Enter", "Esc", "X", "F", "Q", "E", "Z", "C", "^"]

_fonts: dict[tuple[bool, int], ImageFont.ImageFont] = {}


def font(bold: bool, size: float) -> ImageFont.ImageFont:
    key = (bold, max(6, round(size)))
    if key not in _fonts:
        name = "georgiab.ttf" if bold else "georgia.ttf"
        for d in FONT_DIRS:
            if (d / name).exists():
                _fonts[key] = ImageFont.truetype(str(d / name), key[1])
                break
        else:
            _fonts[key] = ImageFont.load_default(key[1])
    return _fonts[key]


def rgba(c: int, scale: float = 1.0) -> tuple[int, int, int, int]:
    return (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, round((c & 255) * scale)


def with_a(rgb: tuple[int, int, int], a: float) -> tuple[int, int, int, int]:
    return rgb[0], rgb[1], rgb[2], max(0, min(255, round(a * 255)))


class Canvas:
    def __init__(self, w: int, h: int) -> None:
        self.img = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        self.clips: list[tuple[float, float, float, float]] = [(0, 0, w, h)]

    def layer(self, box: tuple[float, float, float, float], draw_fn, clip=None) -> None:
        cx0, cy0, cx1, cy1 = clip or self.clips[-1]
        if clip is not None:
            p = self.clips[-1]
            cx0, cy0, cx1, cy1 = max(cx0, p[0]), max(cy0, p[1]), min(cx1, p[2]), min(cy1, p[3])
        x0, y0 = int(max(box[0], cx0, 0)), int(max(box[1], cy0, 0))
        x1 = int(min(box[2] + 1, cx1, self.img.width))
        y1 = int(min(box[3] + 1, cy1, self.img.height))
        if x1 <= x0 or y1 <= y0:
            return
        lay = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(lay), lay, x0, y0)
        self.img.alpha_composite(lay, (x0, y0))


def title_art(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float) -> None:
    """A stand-in for the title art (sunset sky, logo, Rynn left, the dragon right), 4:3 art centred."""
    for yy in range(int(h) + 1):
        t = yy / max(h, 1)
        d.line([(x, y + yy), (x + w, y + yy)], fill=(int(70 + 170 * t), int(90 + 80 * t), int(150 - 90 * t), 255))
    aw = min(w, h * 4 / 3)
    ax = x + (w - aw) / 2
    f = font(True, h * 0.11)
    d.text((ax + aw / 2, y + h * 0.13), "DRAKAN", font=f, anchor="mm", fill=(196, 150, 84, 255))
    d.text(
        (ax + aw / 2, y + h * 0.22),
        "THE ANCIENTS' GATES",
        font=font(True, h * 0.035),
        anchor="mm",
        fill=(150, 110, 60, 255),
    )
    d.ellipse([ax + aw * 0.22, y + h * 0.33, ax + aw * 0.30, y + h * 0.43], fill=(160, 100, 80, 255))
    d.rounded_rectangle([ax + aw * 0.18, y + h * 0.43, ax + aw * 0.34, y + h], radius=24, fill=(70, 70, 86, 255))
    d.polygon(
        [
            (ax + aw * 0.62, y + h * 0.30),
            (ax + aw * 1.0, y + h * 0.18),
            (ax + aw * 0.95, y + h * 0.62),
            (ax + aw * 0.80, y + h),
            (ax + aw * 0.64, y + h),
        ],
        fill=(196, 92, 40, 255),
    )
    d.text(
        (ax + aw / 2, y + h * 0.97),
        "(c) 2002 placeholder art",
        font=font(False, h * 0.02),
        anchor="mm",
        fill=(240, 240, 240, 255),
    )


def save_thumb(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float, index: int, alpha: int) -> None:
    """A stand-in save screenshot: a landscape whose colours depend on the slot."""
    rnd = random.Random(index * 31 + 5)
    sky = (rnd.randint(60, 140), rnd.randint(90, 160), rnd.randint(140, 220))
    for yy in range(int(h * 0.6) + 1):
        t = yy / max(h * 0.6, 1)
        d.line([(x, y + yy), (x + w, y + yy)], fill=(*(int(c * (0.7 + 0.3 * t)) for c in sky), alpha))
    ground = (rnd.randint(50, 90), rnd.randint(60, 100), rnd.randint(30, 50), alpha)
    d.rectangle([x, y + h * 0.6, x + w, y + h], fill=ground)
    for _ in range(4):
        cx, r = x + rnd.random() * w, h * (0.15 + rnd.random() * 0.2)
        d.ellipse([cx - r * 1.6, y + h * 0.6 - r, cx + r * 1.6, y + h * 0.6 + r], fill=(*ground[:3], alpha))
    tx = x + w * (0.3 + rnd.random() * 0.4)
    d.rectangle([tx, y + h * 0.35, tx + w * 0.06, y + h * 0.6], fill=(90, 80, 70, alpha))
    d.polygon(
        [(tx - w * 0.02, y + h * 0.35), (tx + w * 0.03, y + h * 0.25), (tx + w * 0.08, y + h * 0.35)],
        fill=(120, 60, 40, alpha),
    )


def background(cv: Canvas, scene: str, map_image: list[float]) -> None:
    w, h = cv.img.size
    d = ImageDraw.Draw(cv.img)
    rnd = random.Random(7)
    if scene == "title":
        title_art(d, 0, 0, w, h)
        return
    if scene == "map":
        x, y, mw, mh = map_image
        d.rectangle([x, y, x + mw, y + mh], fill=(213, 168, 84, 255))
        for _ in range(140):
            px, py = x + rnd.random() * mw, y + rnd.random() * mh
            s = 6 + rnd.random() * 14
            d.polygon([(px - s, py + s * 0.6), (px, py - s * 0.6), (px + s, py + s * 0.6)], outline=(90, 64, 30, 255))
        for _ in range(12):
            pts = [(x + rnd.random() * mw, y + rnd.random() * mh) for _ in range(4)]
            d.line(pts, fill=(60, 42, 20, 255), width=2)
        return
    for yy in range(h):  # a dim dungeon
        t = yy / h
        d.line([(0, yy), (w, yy)], fill=(int(40 + 30 * t), int(34 + 20 * t), int(28 + 12 * t), 255))
    for _ in range(40):
        bx, by = rnd.random() * w, rnd.random() * h * 0.8
        bw, bh = 60 + rnd.random() * 160, 40 + rnd.random() * 90
        g = 45 + rnd.randint(0, 35)
        d.rectangle([bx, by, bx + bw, by + bh], fill=(g + 8, g, g - 10, 255), outline=(25, 20, 16, 255), width=3)
    cx = w / 2  # a figure in the middle (Rynn stand-in)
    d.ellipse([cx - 0.03 * h, 0.30 * h, cx + 0.03 * h, 0.37 * h], fill=(150, 100, 80, 255))
    d.rounded_rectangle([cx - 0.06 * h, 0.37 * h, cx + 0.06 * h, 0.78 * h], radius=20, fill=(110, 80, 60, 255))


def glyph(d: ImageDraw.ImageDraw, r, cat: int, col) -> None:
    s = min(r[2], r[3])
    cx, cy = r[0] + r[2] / 2, r[1] + r[3] / 2
    t = max(2, round(s * 0.07))

    def p(x, y):
        return cx + x * s * 0.5, cy + y * s * 0.5

    if cat == 0:  # sword
        d.line([p(-0.7, 0.7), p(0.75, -0.75)], fill=col, width=t)
        d.line([p(-0.62, 0.28), p(-0.28, 0.62)], fill=col, width=t)
    elif cat == 1:  # bow
        d.arc([*p(-0.9, -0.9), *p(0.3, 0.9)], -60, 60, fill=col, width=t)
        d.line([p(0.18, -0.84), p(0.18, 0.84)], fill=col, width=max(1, t // 2))
    elif cat == 2:  # arrow
        d.line([p(-0.75, 0.75), p(0.7, -0.7)], fill=col, width=t)
        d.polygon([p(0.8, -0.8), p(0.45, -0.65), p(0.65, -0.45)], fill=col)
    elif cat == 3:  # shield
        d.line(
            [p(-0.6, -0.75), p(0.6, -0.75), p(0.6, 0.05), p(0, 0.85), p(-0.6, 0.05), p(-0.6, -0.75)], fill=col, width=t
        )
    elif cat == 4:  # flask
        d.ellipse([*p(-0.54, -0.24), *p(0.54, 0.84)], outline=col, width=t)
        d.rectangle([*p(-0.14, -0.75), *p(0.14, -0.22)], outline=col, width=t)
    elif cat == 5:  # scroll
        d.rectangle([*p(-0.5, -0.6), *p(0.5, 0.6)], outline=col, width=t)
        for yy in (-0.25, 0, 0.25):
            d.line([p(-0.28, yy), p(0.28, yy)], fill=col, width=max(1, t // 2))
    else:
        d.polygon([p(0, -0.75), p(0.6, 0), p(0, 0.75), p(-0.6, 0)], outline=col, width=t)


def panel(cv: Canvas, c: dict, unit: float) -> None:
    x, y, w, h = c["r"]
    style, tint = c["p"], c["c"]
    box = (x - 2, y - 2, x + w + 2, y + h + 2)
    px = max(1, unit)

    if style == 0:  # backdrop: blur + dim + vignette
        x0, y0, x1, y1 = (int(max(0, v)) for v in (x, y, x + w, y + h))
        region = cv.img.crop((x0, y0, x1, y1)).filter(ImageFilter.GaussianBlur(radius=max(2, cv.img.height * 0.006)))
        cv.img.paste(region, (x0, y0))
        r, g, b, a = rgba(tint or 0x000000A0)
        cv.layer(box, lambda d, lay, ox, oy: d.rectangle([x - ox, y - oy, x + w - ox, y + h - oy], fill=(r, g, b, a)))
        return

    def fn(d: ImageDraw.ImageDraw, lay, ox, oy):
        a0, b0, a1, b1 = x - ox, y - oy, x + w - ox, y + h - oy
        if style == 1:  # main
            col = rgba(tint or 0x16120FE4)
            d.rectangle([a0, b0, a1, b1], fill=col)
            d.rectangle([a0, b0, a1, b1], outline=with_a(BRASS, 110 / 255), width=round(px))
            d.rectangle([a0 + 3 * px, b0 + 3 * px, a1 - 3 * px, b1 - 3 * px], outline=(0, 0, 0, 120), width=round(px))
            ln, th = 18 * px, max(1, round(2 * px))
            bc = with_a(BRASS_BRIGHT, 1)
            for cx, cy, sx, sy in ((a0, b0, 1, 1), (a1, b0, -1, 1), (a0, b1, 1, -1), (a1, b1, -1, -1)):
                d.rectangle(sorted_box(cx, cy, cx + sx * ln, cy + sy * th), fill=bc)
                d.rectangle(sorted_box(cx, cy, cx + sx * th, cy + sy * ln), fill=bc)
        elif style == 2:  # inset
            d.rounded_rectangle([a0, b0, a1, b1], radius=2 * px, fill=rgba(tint or 0x0A0807DC))
            sh = min(h * 0.25, 8 * px)
            for i in range(int(sh)):
                d.line([(a0, b0 + i), (a1, b0 + i)], fill=(0, 0, 0, int(115 * (1 - i / max(sh, 1)))))
            d.rounded_rectangle([a0, b0, a1, b1], radius=2 * px, outline=(255, 240, 210, 18), width=round(px))
        elif style == 3:  # card
            d.rounded_rectangle([a0, b0, a1, b1], radius=3 * px, fill=rgba(tint or 0x221C16EC))
            d.line([(a0 + 3 * px, b0 + px), (a1 - 3 * px, b0 + px)], fill=(255, 236, 200, 20), width=round(px))
            d.rounded_rectangle([a0, b0, a1, b1], radius=3 * px, outline=with_a(BRASS, 60 / 255), width=round(px))
        elif style == 4:  # parchment
            d.rectangle([a0, b0, a1, b1], fill=rgba(tint or 0xD9C9A3F5))
            d.rectangle([a0, b0, a1, b1], outline=with_a(BRASS, 1), width=max(1, round(1.5 * px)))
        elif style == 5:  # focus: ember wash rising from the bottom
            for i in range(int(h) + 1):
                d.line([(a0, b0 + i), (a1, b0 + i)], fill=(224, 123, 46, int(0.22 * 255 * i / max(h, 1))))
        else:
            d.rectangle([a0, b0, a1, b1], fill=rgba(tint or 0x000000A0))

    cv.layer(box, fn)


def sorted_box(x0, y0, x1, y1):
    return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]


def text(cv: Canvas, c: dict) -> None:
    x, y, w, h = c["r"]
    size = c["fs"] or 20
    f = font(c["f"] in (1, 2), size)
    s = c["s"]
    anchor = ["lm", "mm", "rm"][c["a"]]
    tx = [x, x + w / 2, x + w][c["a"]]
    ty = y + h / 2
    col = rgba(c["c"])
    sh = max(1, size * 0.06)
    clip = (x - size, y - size * 0.5, x + w + size, y + h + size * 0.5)

    def fn(d, lay, ox, oy):
        d.text((tx - ox + sh, ty - oy + sh), s, font=f, anchor=anchor, fill=(0, 0, 0, int(0.75 * col[3])))
        d.text((tx - ox, ty - oy), s, font=f, anchor=anchor, fill=col)

    cv.layer(clip, fn, clip=clip)


def image(cv: Canvas, c: dict, unit: float) -> None:
    x, y, w, h = c["r"]
    kind, index = c["img"]
    tint = rgba(c["c"])
    if kind == 1 and c["fb"][0]:  # item icon unavailable offline: its fallback
        kind, index = c["fb"]
        tint = rgba(c["c2"] or 0xFFFFFF5A)
        tint = (tint[0], tint[1], tint[2], min(255, tint[3] * 2))

    def fn(d, lay, ox, oy):
        r = (x - ox, y - oy, w, h)
        if kind == 2:
            glyph(d, r, index, (222, 186, 118, tint[3]))
        elif kind == 5:
            label = KEYS[min(index, len(KEYS) - 1)]
            hh = min(w, h)
            f = font(False, hh * 0.46)
            tw = d.textlength(label, font=f)
            bw = max(hh * 0.86, tw + hh * 0.34)
            cx, cy = r[0] + w / 2, r[1] + h / 2
            box = [cx - bw / 2, cy - hh * 0.42, cx + bw / 2, cy + hh * 0.42]
            d.rounded_rectangle(box, radius=hh * 0.14, fill=(40, 34, 28, int(0.95 * tint[3])))
            d.rounded_rectangle(box, radius=hh * 0.14, outline=(176, 141, 87, tint[3]), width=max(1, round(hh * 0.04)))
            d.text((cx, cy), label, font=f, anchor="mm", fill=(240, 225, 190, tint[3]))
        elif kind == 9:
            d.ellipse([r[0] + w * 0.05, r[1] + h * 0.05, r[0] + w * 0.95, r[1] + h * 0.95], fill=tint)
        elif kind in (3, 4):
            glyph(d, r, 6, (222, 186, 118, tint[3]))
        elif kind == 13:  # SavePreview: a stand-in screenshot (slot 8 has none offline: the crest shows)
            if index != 8:
                save_thumb(d, r[0], r[1], w, h, index, tint[3])
        elif kind == 14:  # TitleArt
            title_art(d, r[0], r[1], w, h)

    cv.layer((x - 4, y - 4, x + w + 4, y + h + 4), fn)


def render(path: Path, out: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    head = json.loads(lines[0])
    vx, vy, vw, vh = head["viewport"]
    cv = Canvas(int(vx + vw), int(vy + vh))
    background(cv, head["scene"], head["mapImage"])
    unit = vh / 1080
    for ln in lines[1:]:
        c = json.loads(ln)
        k = c["k"]
        x, y, w, h = c["r"]
        if k == 0:
            cv.layer(
                (x, y, x + w, y + h),
                lambda d, lay, ox, oy, c=c: d.rounded_rectangle(
                    [c["r"][0] - ox, c["r"][1] - oy, c["r"][0] + c["r"][2] - ox, c["r"][1] + c["r"][3] - oy],
                    radius=c["rd"],
                    fill=rgba(c["c"]),
                ),
            )
        elif k == 1:
            t = max(1, round(c["t"]))
            cv.layer(
                (x - t, y - t, x + w + t, y + h + t),
                lambda d, lay, ox, oy, c=c, t=t: d.rounded_rectangle(
                    [c["r"][0] - ox, c["r"][1] - oy, c["r"][0] + c["r"][2] - ox, c["r"][1] + c["r"][3] - oy],
                    radius=min(c["rd"], min(c["r"][2], c["r"][3]) / 2),
                    outline=rgba(c["c"]),
                    width=t,
                ),
            )
        elif k == 2:
            top, bot = rgba(c["c"]), rgba(c["c2"])

            def grad(d, lay, ox, oy, x=x, y=y, w=w, h=h, top=top, bot=bot):
                for i in range(int(h) + 1):
                    t = i / max(h, 1)
                    col = tuple(round(top[j] + (bot[j] - top[j]) * t) for j in range(4))
                    d.line([(x - ox, y - oy + i), (x + w - ox, y - oy + i)], fill=col)

            cv.layer((x, y, x + w, y + h), grad)
        elif k == 3:
            panel(cv, c, unit)
        elif k == 4:
            image(cv, c, unit)
        elif k == 5:
            text(cv, c)
        elif k == 6:
            t = max(1, round(c["t"]))
            cv.layer(
                (min(x, x + w) - t, min(y, y + h) - t, max(x, x + w) + t, max(y, y + h) + t),
                lambda d, lay, ox, oy, c=c, t=t: d.line(
                    [(c["r"][0] - ox, c["r"][1] - oy), (c["r"][0] + c["r"][2] - ox, c["r"][1] + c["r"][3] - oy)],
                    fill=rgba(c["c"]),
                    width=t,
                ),
            )
        elif k == 7:
            p = cv.clips[-1]
            cv.clips.append((max(x, p[0]), max(y, p[1]), min(x + w, p[2]), min(y + h, p[3])))
        elif k == 8 and len(cv.clips) > 1:
            cv.clips.pop()
    cv.img.convert("RGB").save(out)
    print(f"wrote {out}")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src
    dst.mkdir(parents=True, exist_ok=True)
    for p in sorted(src.glob("*.jsonl")):
        render(p, dst / (p.stem + ".png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
