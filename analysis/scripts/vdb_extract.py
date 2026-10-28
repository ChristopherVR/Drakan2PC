"""Extract movies from a Drakan .VDB to FFmpeg-readable .ipu files (and optionally to mp4).
usage: python -I vdb_extract.py file.VDB outdir [--mp4 --ffmpeg PATH --fps 25]
The .ipu file is 'ipum' header + the raw 0x343 blob (frames are PS2 IPU IDEC macroblock streams, see docs/reference/fmv.md)."""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vdb_common as V

ap = argparse.ArgumentParser()
ap.add_argument("vdb")
ap.add_argument("out")
ap.add_argument("--mp4", action="store_true")
ap.add_argument("--ffmpeg", default="ffmpeg")
ap.add_argument("--fps", default="25")
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
base = os.path.splitext(os.path.basename(a.vdb))[0]
with open(a.vdb, "rb") as f:
    for m in V.movies(f):
        nm = "".join(c if c.isalnum() else "_" for c in m["name"]).strip("_")
        p = os.path.join(a.out, "%s_%d_%s.ipu" % (base, m["id"], nm))
        with open(p, "wb") as o:
            o.write(V.ipu_header(m["w"], m["h"], m["frames"]))
            f.seek(m["data_off"])
            left = m["data_size"]
            while left:
                b = f.read(min(left, 1 << 24))
                o.write(b)
                left -= len(b)
        print(p)
        if a.mp4:
            subprocess.check_call(
                [
                    a.ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-framerate",
                    a.fps,
                    "-i",
                    p,
                    "-c:v",
                    "libx264",
                    "-crf",
                    "20",
                    "-pix_fmt",
                    "yuv420p",
                    p[:-4] + ".mp4",
                ]
            )
