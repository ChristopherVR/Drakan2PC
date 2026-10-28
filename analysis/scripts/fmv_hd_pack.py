r"""HD movie pack generator for the Drakan PC port. Reads YOUR disc image, writes the pack next to YOUR game.

  python -I fmv_hd_pack.py list   [--iso ISO]                       movies on the disc (VDB, id, name, size, frames)
  python -I fmv_hd_pack.py build  [--iso ISO] [--out DIR] [...]     decode -> Real-ESRGAN x4 -> HEVC, plus the frame index

Each movie (docs/reference/fmv.md) is decoded with FFmpeg's `ipu` decoder, upscaled 4x per frame with Real-ESRGAN
(512x512 -> 2048x2048, the same anamorphic 1:1 pixel grid the game stamps into its 512x512 frame buffer) and encoded to
`<out>/<key>.mkv` (HEVC, yuv420p, a keyframe every second). Work happens in chunks of --chunk frames, so the temporary
PNGs never exceed a few GB.

Pack format (what the runtime reads from <exe dir>/movies, docs/reference/fmv.md section 9):
  <key>.mkv   the HD video; frame i of the stream = frame i of the movie
  <key>.txt   line 1 'movie <src w> <src h> <frames> <hd w> <hd h>'; then one line per frame, in order:
              '<crc32><size>' (16 hex digits) of the frame's compressed IPU bytes after the flags byte, up to (excluding)
              the 00 00 01 b0 end marker = exactly the bytes the IPU's IDEC consumes, so the runtime identifies the frame
              from the bitstream without any knowledge of the player object.
"""

import argparse
import io
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import numpy as np
import vdb_common
from hd_pack import DEFAULT_ISO, Iso
from PIL import Image

END_MARKER = b"\x00\x00\x01\xb0"
# Real-ESRGAN invents fine texture ("fur" on smooth surfaces) from the MPEG noise and blocking of the 512x512 source, anew
# in every frame, which shimmers in motion. Measured on intro frames 380..420 (fine-detail band 1-4 px at 2048):
# frame-to-frame change 1.70 with no filters, 0.86 with these, keeping 77 % of the fine detail (docs/reference/fmv.md
# section 9). Deblock + mild spatial/temporal denoise before the upscale, temporal-only denoise after it.
DEFAULT_PREFILTER = "deblock=filter=strong:block=8,hqdn3d=3:2:6:6"
DEFAULT_POSTFILTER = "hqdn3d=0:0:6:6"
LEAD_FRAMES = 8  # frames the recursive temporal prefilter sees before each chunk
# The intro/outro book movies and the Sony / Surreal logo. "ez" (LEVEL0RO id 1) is a 320x240 test clip the game never plays.
DEFAULT_MOVIES = (
    "ENGLISH/LEVELS/LEVEL0RO/LEVEL0RO.VDB:6",
    "ENGLISH/LEVELS/LEVEL0RO/LEVEL0RO.VDB:7",
    "ENGLISH/COMMON/INTERFCE/INTERFCE.VDB:4",
)


def frame_key(frame):
    """Identity of one compressed frame: CRC-32 and length of the bytes IDEC decodes (flags byte and end marker cut)."""
    end = frame.rfind(END_MARKER)
    if end < 1:
        raise ValueError("frame without the 00 00 01 b0 end marker")
    body = frame[1:end]
    return "%08x%08x" % (zlib.crc32(body), len(body))


def vdb_files(iso):
    return {p.upper(): (lba, size) for p, lba, size in iso.files() if p.upper().endswith(".VDB")}


def load_movies(iso, vdbs, want):
    """[(key, movie dict, blob bytes)] for each 'PATH:id' in want (paths relative to the disc root, case-insensitive)."""
    out = []
    cache = {}
    for spec in want:
        path, _, mid = spec.rpartition(":")
        path = path.upper().replace("\\", "/")
        hit = [p for p in vdbs if p == path or p.endswith("/" + path)]
        if len(hit) != 1:
            raise SystemExit(f"{path}: {'not on the disc' if not hit else 'ambiguous: ' + ', '.join(hit)}")
        if hit[0] not in cache:
            data = iso.read(*vdbs[hit[0]])
            cache[hit[0]] = (data, vdb_common.movies(io.BytesIO(data)))
        data, movies = cache[hit[0]]
        m = next((m for m in movies if m["id"] == int(mid)), None)
        if m is None:
            raise SystemExit(f"{hit[0]}: no movie id {mid}")
        key = "%s_%d" % (os.path.splitext(os.path.basename(hit[0]))[0].lower(), m["id"])
        blob = data[m["data_off"] : m["data_off"] + m["data_size"]]
        out.append((key, m, blob))
    return out


def frames_of(m, blob):
    o = m["offs"]
    return [blob[o[i] : o[i + 1]] for i in range(m["frames"])]


def decode_chunk(ffmpeg, frames, lead, w, h, first, outdir, prefilter):
    """frames -> outdir/f%06d.png (numbered from `first`) through a temporary .ipu file. `lead` frames before the chunk
    run through the prefilter first and are dropped, so its temporal state (hqdn3d) does not restart at the chunk."""
    ipu = os.path.join(outdir, "chunk.ipu")
    with open(ipu, "wb") as f:
        f.write(vdb_common.ipu_header(w, h, len(lead) + len(frames)))
        for fr in [*lead, *frames]:
            f.write(fr)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-i", ipu]
    if prefilter:
        cmd += ["-vf", prefilter]
    cmd += [
        "-frames:v",
        str(len(lead) + len(frames)),
        "-pix_fmt",
        "rgb24",
        "-start_number",
        str(first - len(lead)),
        os.path.join(outdir, "f%06d.png"),
    ]
    subprocess.run(cmd, check=True)
    os.remove(ipu)
    for i in range(first - len(lead), first):
        os.remove(os.path.join(outdir, "f%06d.png" % i))
    got = len([n for n in os.listdir(outdir) if n.endswith(".png")])
    if got != len(frames):
        raise SystemExit(f"ffmpeg decoded {got} of {len(frames)} frames")


def upscale_chunk(a, src, up):
    """Real-ESRGAN x4 on every frame of src -> up. One frame per tile (--tile 512) avoids tile seams in flat areas; with
    whole-frame tiles several parallel GPU jobs (-j 4:6:4) silently write black frames, so one GPU job at a time."""
    cmd = [a.exe, "-i", src, "-o", up, "-n", a.model, "-s", "4", "-f", "png", "-t", str(a.tile), "-j", "1:1:2"]
    if a.gpu is not None:
        cmd += ["-g", str(a.gpu)]
    r = subprocess.run(cmd, cwd=os.path.dirname(a.exe), capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise SystemExit("realesrgan failed")
    # The upscaled frame must roughly keep the source's brightness (catches blank / failed GPU output).
    for n in sorted(x for x in os.listdir(src) if x.endswith(".png"))[::25]:
        with Image.open(os.path.join(src, n)) as i0, Image.open(os.path.join(up, n)) as i1:
            m0 = float(np.asarray(i0.convert("L"), dtype=np.float32).mean())
            m1 = float(np.asarray(i1.convert("L"), dtype=np.float32).mean())
        if abs(m0 - m1) > 40:  # black / garbage output; Real-ESRGAN itself shifts near-white frames by ~15
            raise SystemExit(f"{up}/{n}: upscaled mean {m1:.1f} vs source {m0:.1f}; Real-ESRGAN output is broken")


def encoder(ffmpeg, out, fps, codec, crf, postfilter):
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "image2pipe",
        "-framerate",
        str(fps),
        "-c:v",
        "png",
        "-i",
        "-",
        *(["-vf", postfilter] if postfilter else []),
        "-pix_fmt",
        "yuv420p",
        "-g",
        str(fps),
    ]
    if codec == "hevc":
        cmd += ["-c:v", "libx265", "-preset", "medium", "-crf", str(crf), "-x265-params", "log-level=error"]
    else:
        cmd += ["-c:v", "libx264", "-preset", "slow", "-crf", str(crf)]
    return subprocess.Popen([*cmd, out], stdin=subprocess.PIPE)


def build_movie(a, key, m, blob):
    frames = frames_of(m, blob)
    count = min(len(frames), a.limit) if a.limit else len(frames)
    keys = [frame_key(f) for f in frames[:count]]
    os.makedirs(a.out, exist_ok=True)
    video = os.path.join(a.out, key + ".mkv")
    index = os.path.join(a.out, key + ".txt")
    if os.path.exists(index) and os.path.exists(video) and not a.force:
        print(f"{key}: already built (--force rebuilds)")
        return
    work = os.path.join(a.work, key)
    shutil.rmtree(work, ignore_errors=True)
    tmp_video = os.path.join(a.out, key + ".part.mkv")
    enc = encoder(a.ffmpeg, tmp_video, a.fps, a.codec, a.crf, a.postfilter)
    # Feed finished chunks to the encoder on a thread, so x265 runs while Real-ESRGAN upscales the next chunk.
    todo = queue.Queue(maxsize=1)
    failed = []

    def feed():
        while True:
            item = todo.get()
            if item is None:
                return
            try:
                if not failed:
                    for n in sorted(n for n in os.listdir(item) if n.endswith(".png")):
                        with open(os.path.join(item, n), "rb") as f:
                            enc.stdin.write(f.read())
            except OSError as e:
                failed.append(e)
            shutil.rmtree(item, ignore_errors=True)

    feeder = threading.Thread(target=feed, daemon=True)
    feeder.start()
    t0 = time.monotonic()
    hd_size = None
    for first in range(0, count, a.chunk):
        n = min(a.chunk, count - first)
        src = os.path.join(work, "src%06d" % first)
        up = os.path.join(work, "up%06d" % first)
        os.makedirs(src)
        os.makedirs(up)
        lead = frames[max(0, first - LEAD_FRAMES) : first]
        decode_chunk(a.ffmpeg, frames[first : first + n], lead, m["w"], m["h"], first, src, a.prefilter)
        upscale_chunk(a, src, up)
        got = sorted(x for x in os.listdir(up) if x.endswith(".png"))
        if len(got) != n:
            raise SystemExit(f"{key}: Real-ESRGAN wrote {len(got)} of {n} frames")
        if hd_size is None:
            with Image.open(os.path.join(up, got[0])) as im:
                hd_size = im.size
        shutil.rmtree(src)
        if failed:
            break
        todo.put(up)
        done = first + n
        rate = done / max(time.monotonic() - t0, 1e-6)
        print(f"{key}: {done}/{count} frames ({rate:.2f} fps, ~{(count - done) / rate / 60:.0f} min left)", flush=True)
    todo.put(None)
    feeder.join()
    try:
        enc.stdin.close()
    except OSError:
        pass
    if enc.wait() != 0 or failed:
        raise SystemExit(f"{key}: encoder failed")
    shutil.rmtree(work, ignore_errors=True)
    os.replace(tmp_video, video)
    with open(index, "w", newline="\n") as f:
        f.write("movie %d %d %d %d %d\n" % (m["w"], m["h"], count, hd_size[0], hd_size[1]))
        f.write("\n".join(keys) + "\n")
    mb = os.path.getsize(video) / 1e6
    print(f"{key}: {video} ({mb:.0f} MB, {hd_size[0]}x{hd_size[1]}), {index}; {(time.monotonic() - t0) / 60:.1f} min")


def cmd_list(a):
    iso = Iso(a.iso)
    vdbs = vdb_files(iso)
    for path in sorted(vdbs):
        data = iso.read(*vdbs[path])
        if len(data) < 64:
            continue
        for m in vdb_common.movies(io.BytesIO(data)):
            print(f"{path}:{m['id']}  {m['name']!r}  {m['w']}x{m['h']}  {m['frames']} frames")


def cmd_build(a):
    iso = Iso(a.iso)
    movies = load_movies(iso, vdb_files(iso), a.movie or DEFAULT_MOVIES)
    for key, m, blob in movies:
        build_movie(a, key, m, blob)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("cmd", choices=["list", "build"])
    p.add_argument("--iso", default=os.environ.get("DRAKAN_ISO", DEFAULT_ISO))
    p.add_argument("--out", default=os.path.join(ROOT, "dist", "DrakanPC", "movies"))
    p.add_argument("--work", default=os.path.join(ROOT, "build", "fmv_hd"))
    p.add_argument("--movie", action="append", help="'DISC/PATH.VDB:id' (repeatable; default: intro, outro, logo)")
    p.add_argument("--exe", default=os.path.join(ROOT, "tools", "hdpack", "realesrgan", "realesrgan-ncnn-vulkan.exe"))
    p.add_argument("--model", default="realesrgan-x4plus")
    p.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "ffmpeg")
    p.add_argument("--prefilter", default=DEFAULT_PREFILTER, help="FFmpeg -vf before upscaling ('' = none)")
    p.add_argument(
        "--postfilter", default=DEFAULT_POSTFILTER, help="FFmpeg -vf on the HD frames when encoding ('' = none)"
    )
    p.add_argument("--codec", choices=["hevc", "h264"], default="hevc")
    p.add_argument("--crf", type=int, default=18)
    p.add_argument("--fps", type=int, default=25)
    p.add_argument("--chunk", type=int, default=200)
    p.add_argument("--limit", type=int, default=0, help="only the first N frames of each movie (quick test)")
    p.add_argument("--gpu", type=int, default=None)
    p.add_argument(
        "--tile",
        type=int,
        default=512,
        help="Real-ESRGAN tile size; 512 = a whole frame in one pass (smaller tiles leave faint seams)",
    )
    p.add_argument("--force", action="store_true")
    a = p.parse_args()
    {"list": cmd_list, "build": cmd_build}[a.cmd](a)


if __name__ == "__main__":
    main()
