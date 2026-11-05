"""Direct-dist New Game -> Quit to Title -> host regression; dry-run unless --run.

Reserve the live game slot first. A timeout captures a full Windows process dump
(guest RAM and thread contexts) before terminating this harness's own process.
Inspect gameplay screenshots and movement RAM dumps before accepting a run.
"""

from __future__ import annotations

import argparse
import configparser
import ctypes
import hashlib
import io
import json
import math
import os
import re
import struct
import subprocess
from pathlib import Path

from netplay_menu_two_instances import host_keys, taps

ROOT = Path(__file__).resolve().parents[2]


def position(path: Path) -> list[float]:
    data = path.read_bytes()

    def pointer(address: int) -> int:
        return struct.unpack_from("<I", data, address & 0x1FFFFFFF)[0] & 0x1FFFFFFF

    context = pointer(0x5565D4)
    actor = pointer(context + 0x4B0)
    if not 0x100000 <= actor < len(data) - 0x20:
        raise ValueError("invalid local actor pointer")
    result = list(struct.unpack_from("<3f", data, actor + 0x14))
    if not all(math.isfinite(value) for value in result):
        raise ValueError("nonfinite player position")
    return result


def private_main(source: Path) -> str:
    config = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
    config.read(source, encoding="utf-8")
    if not config.has_section("general"):
        config.add_section("general")
    for key in ("pause_menu", "title_menu", "save_menu"):
        config.set("general", key, "modern")
    output = io.StringIO()
    config.write(output)
    return output.getvalue()


def recipe(coop: bool, npc: bool, arena: int) -> str:
    lines = ["clock milliseconds", "0 sync screen 8 3", "1500 key tap ENTER", "0 sync play 60"]
    lines += ["95000 shot sp_ready", "96000 key tap ESC"]
    group, _ = taps(97000, ["DOWN"] * 4 + ["ENTER"], 300)
    lines += [*group, "99300 key tap ENTER", "0 sync screen 8 3", "2000 shot title_returned"]
    t = 3500
    for keys in host_keys(arena_index=arena, coop=coop):
        group, t = taps(t, keys, 300)
        lines += group
        t += 600
    lines += [f"{t} shot host_form", f"{t + 500} key tap ENTER"]
    t += 3500
    if npc:
        group, t = taps(t, ["UP", "RIGHT", "DOWN"], 400)
        lines += group
    lines += [f"{t} shot lobby", f"{t + 1000} key tap DOWN", f"{t + 1500} key tap ENTER"]
    lines += [
        "0 sync play 60",
        "3000 shot mp_ready",
        "7000 dump ram_before",
        "8000 key down W",
        "10000 key up W",
        "12000 dump ram_after",
        "12000 shot mp_moved",
        "15000 quit",
    ]
    return "\n".join(lines) + "\n"


def capture_process(pid: int, path: Path) -> None:
    import msvcrt
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    dbghelp.MiniDumpWriteDump.argtypes = [
        wintypes.HANDLE,
        wintypes.DWORD,
        wintypes.HANDLE,
        wintypes.DWORD,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        with path.open("wb") as output:
            if not dbghelp.MiniDumpWriteDump(handle, pid, msvcrt.get_osfhandle(output.fileno()), 2, None, None, None):
                raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="issue1-sp-mp-integrated")
    parser.add_argument("--language", choices=("english", "german", "french", "spanish", "italian"), default="english")
    parser.add_argument("--coop", action="store_true")
    parser.add_argument("--npc", action="store_true", help="select first NPC through the co-op lobby")
    parser.add_argument("--arena", type=int, choices=range(3), default=0)
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()
    if args.npc and not args.coop:
        parser.error("--npc requires --coop")
    out = ROOT / "build/harness" / args.name
    if out.resolve().parent != (ROOT / "build/harness").resolve():
        parser.error("--name must be a single folder name")
    exe = ROOT / "dist/DrakanPC/drakan.exe"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "PS2X_", "GSR_"))}
    overrides = dict(
        DRAKAN_INPUT_SCRIPT=str(out / "recipe.txt"),
        DRAKAN_SHOT_DIR=str(out),
        DRAKAN_LOG_DIR=str(out),
        DRAKAN_MEMCARD_DIR=str(out / "card/mc0"),
        DRAKAN_NATIVE_OPTIONS_INI=str(out / "drakan_native_options.ini"),
        DRAKAN_INI=str(out / "drakan.ini"),
        DRAKAN_INPUT_INI=str(out / "drakan_input.ini"),
        DRAKAN_MULTIPLAYER_INI=str(out / "multiplayer.ini"),
        PS2X_WATCHDOG_DUMP_DIR=str(out),
        DRAKAN_TITLE_MENU="modern",
        DRAKAN_PAUSE_MENU="modern",
        DRAKAN_GAME="d2",
        DRAKAN_LANG=args.language,
        DRAKAN_AUDIO="1",
        DRAKAN_PAUSE_UNFOCUSED="0",
        DRAKAN_INPUT_AUTOSTART="1",
        DRAKAN_AUTOPRESS="3000",
        DRAKAN_AUTOPRESS_UNTIL="90000",
        DRAKAN_QUIT_EXITS="0",
        DRAKAN_NO_ACTIVATE="1",
        DRAKAN_FULLSCREEN="0",
        DRAKAN_NET_TRACE="1",
        DRAKAN_LIFECYCLE_LOG="1",
        DRAKAN_WINDOW_WIDTH="1280",
        DRAKAN_WINDOW_HEIGHT="720",
        DRAKAN_NET_NAME="SPMP",
    )
    manifest = dict(
        executable=str(exe),
        sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        output=str(out),
        env=overrides,
        recipe=recipe(args.coop, args.npc, args.arena),
        private_main_ini=private_main(exe.parent / "drakan.ini"),
        multiplayer_baseline="name = SPMP\ncolour = 0\nmodel = 0\ncharacter = 0\n",
    )
    if not args.run:
        print(json.dumps(manifest, indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=False)
    (out / "card/mc0").mkdir(parents=True)
    (out / "recipe.txt").write_text(manifest["recipe"], encoding="utf-8")
    options = exe.parent / "drakan_native_options.ini"
    (out / options.name).write_bytes(options.read_bytes() if options.exists() else b"[options]\n")
    (out / "drakan.ini").write_text(manifest["private_main_ini"], encoding="utf-8")
    (out / "drakan_input.ini").write_bytes((exe.parent / "drakan_input.ini").read_bytes())
    (out / "multiplayer.ini").write_text(manifest["multiplayer_baseline"], encoding="utf-8")
    settings_before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in exe.parent.glob("*.ini")}
    env.update(overrides)
    with (out / "stdout.log").open("wb") as log:
        process = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        manifest["pid"] = process.pid
        (out / "run.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        try:
            code = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            # If capture fails, leave the owned process alive for manual diagnostics.
            capture_process(process.pid, out / "timeout-full.dmp")
            process.kill()
            process.wait(timeout=15)
            code = None
    text = (out / "stdout.log").read_text(errors="replace")
    checks = dict(
        exit_code=code,
        script_quit="[in] script quit" in text,
        gameplay_shots=all((out / f"{name}.png").exists() for name in ("sp_ready", "mp_ready", "mp_moved")),
        no_missing_target="[guest-branch:missing-target]" not in text,
        no_guest_stall=re.search(
            r"(?im)^.*(?:guest[^\n]*stall|stall[^\n]*guest|watchdog[^\n]*(?:timeout|stuck|stall))", text
        )
        is None,
        dist_settings_unchanged=settings_before
        == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in exe.parent.glob("*.ini")},
    )
    try:
        before, after = (position(out / f"ram_{stage}.ram") for stage in ("before", "after"))
        distance = math.hypot(after[0] - before[0], after[2] - before[2])
        checks["native_player_displaced"] = distance > 100
        movement = dict(before=before, after=after, horizontal_distance=distance)
    except (OSError, ValueError, struct.error) as error:
        checks["native_player_displaced"] = False
        movement = dict(error=str(error))
    result = dict(checks=checks, movement=movement)
    checks["requires_review"] = (
        "Inspect native level lifecycle, gameplay screenshots and RAM movement; heartbeats alone do not pass."
    )
    result["automated_pass"] = code == 0 and all(
        value for key, value in checks.items() if key not in ("exit_code", "requires_review")
    )
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["automated_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
