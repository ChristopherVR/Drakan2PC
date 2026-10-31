"""Two game instances on one PC see each other's Rynn as a ghost (drakan_netplay, docs/reference/netcode.md
"Playing over the network (spike)", docs/journal/2026-10-31/netplay-spike.md).

    python scripts/harness/netplay_two_instances.py                      # A hosts, B connects (about 90 s)
    python scripts/harness/netplay_two_instances.py --mode server        # both connect to drakan_server
    python scripts/harness/netplay_two_instances.py --no-level-change    # stop after the ghost screenshots
    python scripts/harness/netplay_two_instances.py --reject             # profile mismatch must be rejected (about 40 s)
    python scripts/harness/netplay_two_instances.py --exe build/final-game/bin/drakan.exe --port 47800

What it does:
  1. builds two private play folders build/netplay-a and build/netplay-b (the exe and DLLs are copied from --src,
     textures / movies / nr / ngx_data are junctions to it), each with its own memory card under
     build/harness/<name>/<a|b>/card/mc0 (DRAKAN_MEMCARD_DIR): the player's saves are never read or written;
  2. (nothing to prepare: A makes its own quick save with F5 later);
  3. starts A (DRAKAN_NET_HOST=<port>, or a client of drakan_server) and B (DRAKAN_NET_CONNECT) with DRAKAN_GHOST=1;
     both New Game into Surdana (the same level), A walks (recipe), B takes screenshots of A's ghost;
  4. A quick-saves (F5, ghosts are destroyed around the save write) and quick-loads (F8, a Level_Load): B's ghost of A
     must disappear while A loads and come back afterwards, without a crash (B screenshots during and after).
     A real move to another level cannot be staged by script (DRAKAN_START_LEVEL redirects the file layer, the engine
     and the lifecycle hooks still see "Level 1A - Surdana"), so the level gate is only covered by the reload;
  5. --reject starts a drakan_server with another disc profile hash and one game: it must log REJECTED;
  5. reads the `[netplay ...]` / `[lifecycle ...]` lines of both logs: connection, ghost spawn / removal, RTT, snapshot
     rate; exit status 0 when every check passed.
Output: build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt, *.png) and summary.json.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JUNCTIONS = ("textures", "movies", "nr", "ngx_data")
SKIP = {"mc0", "mc1", "saves", "logs", "drakan.exe", "drakan_native_options.ini"} | set(JUNCTIONS)
SURDANA = "Level 1A - Surdana"

# Times are milliseconds after each instance's own "sync play" (Rynn under control).
RECIPE_A = """clock milliseconds
0 sync play 60
14000 key down W
17500 key up W
18000 key down D
19500 key up D
20000 key down W
21500 key up W
{level_change}{quit_at} quit
"""
RECIPE_B = """clock milliseconds
0 sync play 60
9000 shot b_before_move
16000 shot b_a_walking
19000 shot b_a_strafing
22500 shot b_a_stopped
{after_shots}{quit_at} quit
"""


def log(msg: str) -> None:
    print(f"[netplay-harness] {msg}", flush=True)


def prepare_folder(src: Path, exe: Path, dst: Path) -> Path:
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        if item.name in SKIP or item.is_dir():
            continue
        target = dst / item.name
        if not target.exists() or target.stat().st_size != item.stat().st_size:
            shutil.copy2(item, target)
    for name in JUNCTIONS:
        s, t = src / name, dst / name
        if s.is_dir() and not t.exists():
            subprocess.run(["cmd", "/c", "mklink", "/J", str(t), str(s)], check=True, stdout=subprocess.DEVNULL)
    shutil.copy2(exe, dst / "drakan.exe")
    return dst / "drakan.exe"


def game_env(out: Path, folder: Path, recipe: Path, timeout: int, extra: dict[str, str]) -> dict[str, str]:
    card = out / "card" / "mc0"
    if card.parent.exists():
        shutil.rmtree(card.parent)
    card.mkdir(parents=True)
    options = out / "drakan_native_options.ini"
    if (folder / "drakan_native_options.ini").is_file():
        shutil.copyfile(folder / "drakan_native_options.ini", options)
    env = dict(os.environ)
    env.update(
        {
            "DRAKAN_INPUT_SCRIPT": str(recipe),
            "DRAKAN_SHOT_DIR": str(out),
            "DRAKAN_LOG_DIR": str(out),
            "DRAKAN_AUTOPRESS": "3000",
            "DRAKAN_AUTOPRESS_UNTIL": str(timeout * 1000),
            "DRAKAN_INPUT_AUTOSTART": "1",
            "PS2X_MAX_SECONDS": str(timeout),
            "DRAKAN_NO_ACTIVATE": "1",
            "DRAKAN_AUDIO": "0",
            "DRAKAN_START_LEVEL": SURDANA,
            "DRAKAN_MEMCARD_DIR": str(card),
            "DRAKAN_NATIVE_OPTIONS_INI": str(options),
            "DRAKAN_GHOST": "1",
            "DRAKAN_LIFECYCLE_LOG": "1",
            "DRAKAN_FULLSCREEN": "0",
            "DRAKAN_WINDOW_WIDTH": "960",
            "DRAKAN_WINDOW_HEIGHT": "720",
            "DRAKAN_WS_ASPECT": "4:3",
        }
    )
    env.update(extra)
    return env


def run_reject(a: argparse.Namespace, out: Path, exe_b: Path) -> int:
    """One game against a drakan_server that wants another disc profile: the Hello must be answered with Reject."""
    (out / "b" / "recipe.txt").write_text(
        "clock milliseconds\n0 sync play 60\n3000 quit\n", encoding="utf-8", newline="\n"
    )
    with (out / "server.log").open("wb") as server_log:
        server = subprocess.Popen(
            [str(a.server_exe), "--port", str(a.port), "--auto-start", "1", "--profile-hash", "1"],
            stdout=server_log,
            stderr=subprocess.STDOUT,
        )
        try:
            env = game_env(
                out / "b",
                exe_b.parent,
                out / "b" / "recipe.txt",
                a.timeout,
                {"DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Bravo"},
            )
            with (out / "b" / "stdout_1.log").open("wb") as f:
                proc = subprocess.run([str(exe_b)], cwd=exe_b.parent, env=env, stdout=f, stderr=subprocess.STDOUT)
        finally:
            server.terminate()
    text = (out / "b" / "stdout_1.log").read_bytes().decode("latin-1")
    rejected = re.search(r"REJECTED by the server: reason 1,", text) is not None
    for line in text.splitlines():
        if "[netplay" in line:
            print(f"B {line.strip()}")
    print(("PASS " if rejected else "FAIL ") + "b: profile mismatch rejected (reason 1)")
    print(("PASS " if proc.returncode == 0 else "FAIL ") + "b: clean exit")
    return 0 if rejected and proc.returncode == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="netplay")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC", help="play folder to copy DLLs / ini from")
    ap.add_argument("--mode", choices=("host", "server"), default="host")
    ap.add_argument(
        "--server-exe", type=Path, default=ROOT / "build" / "runtime-tests" / "bin" / "Release" / "drakan_server.exe"
    )
    ap.add_argument("--port", type=int, default=47800)
    ap.add_argument("--no-level-change", action="store_true")
    ap.add_argument("--reject", action="store_true", help="only B, against a drakan_server of another disc profile")
    ap.add_argument("--timeout", type=int, default=150, help="seconds per instance")
    a = ap.parse_args()

    if not a.exe.is_file():
        raise SystemExit(f"{a.exe} not found (build ps2EntryRunner first)")
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    (out / "a").mkdir(parents=True)
    (out / "b").mkdir(parents=True)
    exe_a = prepare_folder(a.src, a.exe, ROOT / "build" / "netplay-a")
    exe_b = prepare_folder(a.src, a.exe, ROOT / "build" / "netplay-b")
    if a.reject:
        return run_reject(a, out, exe_b)

    level_change = (
        "" if a.no_level_change else "24000 key down F5\n24800 key up F5\n33000 key down F8\n33700 key up F8\n"
    )
    quit_a = 56000
    recipes = {
        "a": RECIPE_A.format(level_change=level_change, quit_at=quit_a),
        "b": RECIPE_B.format(
            after_shots="" if a.no_level_change else "35500 shot b_a_loading\n46000 shot b_a_back\n",
            quit_at=quit_a - 2000,
        ),
    }
    for k, text in recipes.items():
        (out / k / "recipe.txt").write_text(text, encoding="utf-8", newline="\n")

    server = None
    if a.mode == "server":
        server_log = (out / "server.log").open("wb")
        server = subprocess.Popen(
            [str(a.server_exe), "--port", str(a.port), "--auto-start", "1", "--time-limit", "0", "--frag-limit", "0"],
            stdout=server_log,
            stderr=subprocess.STDOUT,
        )
        env_a = {"DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Alpha"}
    else:
        env_a = {"DRAKAN_NET_HOST": str(a.port), "DRAKAN_NET_NAME": "Alpha"}
    env_b = {"DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Bravo"}

    procs: dict[str, subprocess.Popen] = {}
    logs = {k: out / k / "stdout_1.log" for k in "ab"}
    t0 = time.monotonic()
    try:
        for key, exe, extra, folder in (("a", exe_a, env_a, exe_a.parent), ("b", exe_b, env_b, exe_b.parent)):
            env = game_env(out / key, folder, out / key / "recipe.txt", a.timeout, extra)
            f = logs[key].open("wb")
            procs[key] = subprocess.Popen([str(exe)], cwd=folder, env=env, stdout=f, stderr=subprocess.STDOUT)
            log(f"started {key.upper()} (pid {procs[key].pid})")
            time.sleep(4)

        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > a.timeout + 40:
                log("timeout: killing the instances")
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
        if server:
            server.terminate()

    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in "ab"}
    checks: dict[str, bool] = {}
    for k in "ab":
        checks[f"{k}: clean exit"] = procs[k].returncode == 0 and "[in] script quit" in text[k]
        checks[f"{k}: connected"] = re.search(r"\[netplay [\d.]+\] connected", text[k]) is not None
        spawned = len(re.findall(r"ghost of player \d+ '.*' spawned", text[k]))
        checks[f"{k}: ghost of the other spawned"] = spawned >= 1
    if not a.no_level_change:
        checks["a: reload seen (save restore)"] = "beforeSaveRestore" in text["a"] and "afterSaveRestore" in text["a"]
        checks["b: ghost removed while A loads"] = (
            re.search(r"ghost of player \d+ removed \(player in another level\)", text["b"]) is not None
        )
        checks["b: ghost back after A's load"] = len(re.findall(r"ghost of player \d+ '.*' spawned", text["b"])) >= 2
    stats = {
        k: re.findall(r"stats: rtt ([\-\d.]+) ms, snapshots ([\d.]+)/s, states sent ([\d.]+)/s", text[k]) for k in "ab"
    }
    summary = {
        "mode": a.mode,
        "checks": checks,
        "stats": {
            k: [{"rtt_ms": float(r), "snapshots_per_s": float(s), "sent_per_s": float(t)} for r, s, t in v]
            for k, v in stats.items()
        },
        "screenshots": {k: sorted(p.name for p in (out / k).glob("*.png")) for k in "ab"},
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    for k in "ab":
        for line in text[k].splitlines():
            if "[netplay" in line:
                print(f"{k.upper()} {line.strip()}")
    for name, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name)
    print(f"{out}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
