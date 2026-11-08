r"""Render the Order of the Flame opening into a movie on YOUR machine, for the port to play instead of the live level.

    python analysis/scripts/ootf_intro_movie.py                      # dist/DrakanPC -> dist/DrakanPC/movies/ootf_intro.mkv
    python analysis/scripts/ootf_intro_movie.py --play-dir D:/Games/DrakanPC --scale 2 --crf 18

The PC game has no intro movie file: "Psygnosis Presents", the owl and the title scenes are the engine-rendered level
"OotF 00 - Intro". Played live they depend on the machine's frame times; this tool runs the game once in its render mode
(DRAKAN_INTRO_RENDER, ps2recomp/ps2xRuntime/src/lib/drakan_intro_render.cpp):

  * the engine runs a fixed 1/60 s time step per tick with vsync on, so every scene advances one exact step per frame;
  * every completed frame of the Intro level (the game image, 4:3, at the internal scale --scale) is piped to ffmpeg;
  * the mixed audio of the run (DRAKAN_AUDIO_DUMP, silent on the speakers) is cut to the video with the audio position
    logged for every frame, so picture and sound stay in step even when a frame was late;
  * the video is re-timed to the audio clock and muxed with the sound into <play dir>/movies/ootf_intro.mkv.

The game plays that file at the campaign's New Game (Esc / Start / Cross skip it; the live level still does the hand-over to
Ruined Village) and falls back to the live level when the file is missing. Needs: the play folder with the Order of the
Flame campaign pack installed (mods/), the ISO set in its drakan.ini, ffmpeg with libx264 and aac on PATH (or --ffmpeg).
Nothing from either game is committed or uploaded; the movie is generated locally.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def find_ffmpeg(arg: str | None) -> str:
    for cand in (arg, os.environ.get("FFMPEG"), shutil.which("ffmpeg"), str(ROOT / "tools" / "ffmpeg" / "ffmpeg.exe")):
        if cand and (shutil.which(cand) or Path(cand).is_file()):
            return cand
    raise SystemExit("ffmpeg not found: put it on PATH or pass --ffmpeg")


def probe_duration(ffprobe: str, path: Path, stream: str) -> float:
    """End of the last packet of a stream (mkv streams carry no duration field)."""
    out = subprocess.run(
        [
            ffprobe, "-v", "error", "-select_streams", stream, "-show_entries", "packet=pts_time,duration_time",
            "-of", "csv=p=0", str(path),
        ],
        capture_output=True,
        text=True,
    ).stdout.split()  # fmt: skip
    end = float("nan")
    for line in out:
        parts = line.split(",")
        try:
            end = float(parts[0]) + (float(parts[1]) if len(parts) > 1 and parts[1] else 0.0)
        except ValueError:
            continue
    return end


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--play-dir", type=Path, default=ROOT / "dist" / "DrakanPC", help="the play folder (drakan.exe)")
    ap.add_argument("--out", type=Path, help="movie to write (default <play dir>/movies/ootf_intro.mkv)")
    ap.add_argument("--work", type=Path, help="scratch folder (default a temp folder, kept with --keep)")
    ap.add_argument("--ffmpeg", help="ffmpeg executable")
    ap.add_argument("--scale", type=int, default=3, help="renderer internal scale, 1..4 (512 px per step; default 3)")
    ap.add_argument("--height", type=int, default=1440, help="output height; width is 4:3 (default 1440 -> 1920x1440)")
    ap.add_argument("--crf", type=int, default=15, help="x264 quality (lower is better, default 15)")
    ap.add_argument("--timeout", type=int, default=240, help="seconds the render run may take")
    ap.add_argument("--keep", action="store_true", help="keep the scratch folder (frames.csv, the raw run log)")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the game")
    a = ap.parse_args()

    play = a.play_dir.resolve()
    exe = play / "drakan.exe"
    if not exe.is_file():
        raise SystemExit(f"{exe} not found: build the game first (scripts/buildtool/Build-LocalGame.ps1)")
    if not (play / "mods").is_dir() or not any((play / "mods").glob("ootf*")):
        raise SystemExit(f"no Order of the Flame campaign pack in {play / 'mods'} (see docs/reference/modding.md)")
    ffmpeg = find_ffmpeg(a.ffmpeg)
    ffprobe = (
        str(Path(ffmpeg).with_name("ffprobe" + Path(ffmpeg).suffix)) if Path(ffmpeg).parent != Path(".") else "ffprobe"
    )
    if not shutil.which(ffprobe) and not Path(ffprobe).is_file():
        ffprobe = "ffprobe"
    out_movie = (a.out or play / "movies" / "ootf_intro.mkv").resolve()
    work = (a.work or Path(tempfile.mkdtemp(prefix="ootf_intro_"))).resolve()
    work.mkdir(parents=True, exist_ok=True)
    card = work / "card" / "mc0"
    if card.parent.exists():
        shutil.rmtree(card.parent)
    card.mkdir(parents=True)
    run_out = work / "out"
    run_out.mkdir(exist_ok=True)
    for stale in run_out.iterdir():
        stale.unlink()
    wav = run_out / "audio.wav"
    width = a.height * 4 // 3 // 2 * 2
    enc_args = (
        f"-vf scale={width}:{a.height}:flags=lanczos -c:v libx264 -preset slow -crf {a.crf} -pix_fmt yuv420p "
        "-colorspace bt709 -color_primaries bt709 -color_trc bt709"
    )
    options = work / "drakan_native_options.ini"
    if (play / "drakan_native_options.ini").is_file():
        shutil.copyfile(play / "drakan_native_options.ini", options)
    env = dict(os.environ)
    env.update(
        {
            "DRAKAN_INTRO_RENDER": str(run_out),
            "DRAKAN_INTRO_RENDER_FFMPEG": ffmpeg,
            "DRAKAN_INTRO_RENDER_ARGS": enc_args,
            "DRAKAN_INTRO_RENDER_FPS": "60",
            "DRAKAN_AUDIO_DUMP": str(wav),
            "DRAKAN_AUDIO": "0",  # silent on the speakers; the SPU2 still renders into the dump
            "DRAKAN_GAME": "d1",
            "DRAKAN_VSYNC": "1",
            "DRAKAN_WORLD_RATE": "uncapped",
            "DRAKAN_FRAME_LIMIT": "0",
            "DRAKAN_FULLSCREEN": "0",
            "DRAKAN_DLSS": "off",
            "DRAKAN_WINDOW_WIDTH": "960",
            "DRAKAN_WINDOW_HEIGHT": "540",
            "PS2X_GS_SCALE": str(max(1, min(4, a.scale))),
            "DRAKAN_MEMCARD_DIR": str(card),
            "DRAKAN_LOG_DIR": str(work),
            "DRAKAN_NATIVE_OPTIONS_INI": str(options),
            "DRAKAN_AUTOPRESS": "3000",
            "DRAKAN_AUTOPRESS_UNTIL": str(a.timeout * 1000),
            "DRAKAN_AUTOPRESS_STOP_AFTER_LOAD": "1",
            "DRAKAN_INPUT_AUTOSTART": "1",
            "DRAKAN_OOTF_INTRO_MOVIE": "0",
            "DRAKAN_NO_ACTIVATE": "1",
            "DRAKAN_PAUSE_UNFOCUSED": "0",
            "PS2X_MAX_SECONDS": str(a.timeout),
        }
    )
    for kv in a.env:
        k, _, v = kv.partition("=")
        env[k] = v
    log = work / "render.log"
    print(f"rendering the Intro with {exe} (scale {a.scale}, {width}x{a.height}); this takes about a minute ...")
    t0 = time.monotonic()
    with log.open("wb") as f:
        proc = subprocess.Popen([str(exe)], cwd=play, env=env, stdout=f, stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=a.timeout + 30)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            raise SystemExit(f"the game did not finish within {a.timeout} s; see {log}") from None
    meta_path = run_out / "render.json"
    if not meta_path.is_file() or not (run_out / "video.mkv").is_file():
        raise SystemExit(f"the game did not record the Intro (exit {proc.returncode}); see {log}")
    meta = json.loads(meta_path.read_text())
    frames = int(meta["frames"])
    audio_s = (meta["audioLast"] - meta["audioFirst"]) / 48000.0
    if frames < 600 or audio_s <= 0:
        raise SystemExit(f"only {frames} frames recorded ({audio_s:.1f} s); see {log}")
    # The SPU2 is stepped one block per frame (lockstep), so this is exactly 60; a different value means the run was not
    # in lockstep (an old executable) and the video is re-timed to the audio clock below.
    fps = frames / audio_s
    rows = [tuple(int(x) for x in ln.split(",")) for ln in (run_out / "frames.csv").read_text().split()]
    worst = 0.0
    for i, pos in rows:  # pos = the audio position after this frame's block
        want = meta["audioFirst"] + (i + 1) * 48000.0 / fps
        worst = max(worst, abs(pos - want) / 48000.0)
    print(
        f"recorded {frames} frames in {time.monotonic() - t0:.0f} s: {fps:.3f} fps on the audio clock "
        f"({frames / 60.0:.2f} s of engine time, {audio_s:.2f} s of sound); worst picture/sound offset {worst * 1000:.0f} ms"
    )
    if worst > 0.12:
        print("WARNING: the render fell behind real time (the machine was busy); lower --scale or close other programs")
    out_movie.parent.mkdir(parents=True, exist_ok=True)
    # video re-timed to the audio clock (its stream says 60 fps), the sound cut to the same window
    start = meta["audioFirst"] / 48000.0
    length = frames / fps
    cmd = [
        ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
        "-itsscale", f"{60.0 / fps:.9f}", "-i", str(run_out / "video.mkv"),
        "-ss", f"{start:.6f}", "-t", f"{length:.6f}", "-i", str(wav),
        "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy", "-c:a", "aac", "-b:a", "256k", "-shortest",
        str(out_movie),
    ]  # fmt: skip
    subprocess.run(cmd, check=True)
    vd = probe_duration(ffprobe, out_movie, "v:0")
    ad = probe_duration(ffprobe, out_movie, "a:0")
    print(f"wrote {out_movie} ({out_movie.stat().st_size / 1e6:.1f} MB): video {vd:.3f} s, audio {ad:.3f} s")
    if not a.keep and not a.work:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f"scratch kept in {work}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
