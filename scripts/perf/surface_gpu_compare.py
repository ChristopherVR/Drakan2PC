"""Compare the ray pass pictures of GSR_SURFACE_GPU_COMPARE: for each dumped world, the pass with the streamed draws
reconstructed on the CPU ("water-cpu") against the pass with the GPU-built frame triangles ("water-gpu"), both of the
same frame (same camera, inputs and water clock).

    python scripts/perf/surface_gpu_compare.py <GSR_TEMPORAL_DUMP_DIR> [--out <folder>]

Prints per world the pixels that differ, the largest difference and the PSNR, and writes for each world the two
pictures (tone mapped to 8 bits) and a difference sheet (CPU | GPU | difference x 16, red where anything differs).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

NAME = re.compile(r"^f(\d+)-s(\d+)-(water-cpu|water-gpu)\.raw$")


def load(path: Path) -> np.ndarray:
    data = path.read_bytes()
    width, height, fmt, size = np.frombuffer(data[:16], dtype=np.uint32)
    pixels = np.frombuffer(data[16:], dtype=np.uint8)
    if fmt == 10 and size == 8:  # R16G16B16A16_FLOAT
        return pixels.view(np.float16).reshape(height, width, 4).astype(np.float32)
    if fmt == 2 and size == 16:  # R32G32B32A32_FLOAT
        return pixels.view(np.float32).reshape(height, width, 4)
    raise ValueError(f"{path}: format {fmt} not handled")


def picture(rgb: np.ndarray) -> np.ndarray:
    v = np.clip(rgb[..., :3], 0, 1)
    v = np.where(v <= 0.0031308, v * 12.92, 1.055 * np.power(v, 1 / 2.4) - 0.055)
    return (v * 255 + 0.5).astype(np.uint8)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--sheet-width", type=int, default=1280, help="width of each picture in the sheet")
    args = ap.parse_args()
    out = args.out or args.dump / "compare"
    out.mkdir(parents=True, exist_ok=True)
    worlds: dict[tuple[int, int], dict[str, Path]] = {}
    for f in sorted(args.dump.iterdir()):
        m = NAME.match(f.name)
        if m:
            worlds.setdefault((int(m.group(1)), int(m.group(2))), {})[m.group(3)] = f
    rows = 0
    total_differ = 0
    for (frame, serial), files in sorted(worlds.items()):
        if len(files) != 2:
            continue
        a, b = load(files["water-cpu"]), load(files["water-gpu"])
        if a.shape != b.shape:
            print(f"frame {frame}: sizes differ {a.shape} {b.shape}")
            continue
        d = np.abs(a[..., :3] - b[..., :3])
        mask = (d > 0).any(axis=2)
        differ = int(mask.sum())
        total_differ += differ
        peak = float(d.max())
        mse = float((d**2).mean())
        psnr = float("inf") if mse == 0 else 10 * np.log10(1.0 / mse)
        bits_equal = bool(np.array_equal(a.view(np.uint32), b.view(np.uint32)))
        print(
            f"frame {frame} serial {serial}: {a.shape[1]}x{a.shape[0]}, pixels that differ {differ} "
            f"({100.0 * differ / mask.size:.4f} %), largest difference {peak:.6f}, PSNR {psnr:.1f} dB, "
            f"bit-identical {bits_equal}"
        )
        pa, pb = picture(a), picture(b)
        Image.fromarray(pa).save(out / f"f{frame}-cpu.png")
        Image.fromarray(pb).save(out / f"f{frame}-gpu.png")
        amplified = np.clip(d * 16, 0, 1)
        diff = (amplified * 255).astype(np.uint8)
        diff[mask] = np.maximum(diff[mask], np.array([96, 0, 0], np.uint8))
        scale = args.sheet_width / a.shape[1]
        size = (args.sheet_width, int(a.shape[0] * scale))
        tiles = [Image.fromarray(x).resize(size, Image.LANCZOS) for x in (pa, pb)]
        tiles.append(Image.fromarray(diff).resize(size, Image.BOX))
        sheet = Image.new("RGB", (size[0] * 3, size[1]))
        for i, t in enumerate(tiles):
            sheet.paste(t, (i * size[0], 0))
        sheet.save(out / f"f{frame}-sheet.png")
        rows += 1
    print(f"{rows} worlds compared, {total_differ} differing pixels in all")
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
