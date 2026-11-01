"""HD textures for converted Order of the Flame packs in one command (never commits anything: output stays in dist/ or local/).

    python -I analysis/scripts/ootf_hd_pack.py build/ootf-run/camp2/mods/ootf-levels.zip [more.zip ...] [--out dist/DrakanPC/textures]

For each mod pack: hd_pack.py modextract (decode every SRSC texture like the runtime, hash it, index it in --work), then one
hd_pack.py upscale over the merged index (Real-ESRGAN, skips hashes already in --out, merges alias.txt). Re-running is cheap:
zips unchanged since the last index are skipped and already-upscaled hashes are not redone. Exit code 1 if some textures failed.
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("zips", nargs="+")
    p.add_argument("--work", default=os.path.join(ROOT, "build", "hdpack-mod"))
    p.add_argument("--out", default=os.path.join(ROOT, "dist", "DrakanPC", "textures"))
    p.add_argument("--force", action="store_true", help="re-index zips even if unchanged")
    p.add_argument("--extra", nargs="*", default=[], help="extra args for hd_pack.py upscale (--gpu 0 --chunk 150 ...)")
    a = p.parse_args()
    py = [sys.executable, "-I", os.path.join(HERE, "hd_pack.py")]
    t0 = time.time()
    for z in a.zips:
        cmd = py + ["modextract", "--zip", z, "--work", a.work] + (["--force"] if a.force else [])
        if subprocess.run(cmd).returncode:
            print("modextract failed for", z)
            return 2
    t1 = time.time()
    r = subprocess.run(py + ["upscale", "--work", a.work, "--out", a.out, *a.extra]).returncode
    n = len([f for f in os.listdir(a.out) if f.endswith(".png")]) if os.path.isdir(a.out) else 0
    sz = sum(os.path.getsize(os.path.join(a.out, f)) for f in os.listdir(a.out)) / 1e6 if n else 0
    print("ootf_hd_pack: index %.0f s, upscale %.0f s, %d textures / %.0f MB in %s" % (t1 - t0, time.time() - t1, n, sz, a.out))
    return r


if __name__ == "__main__":
    sys.exit(main())
