"""Co-op test framework for the Order of the Flame campaign: host + joiner on each level, automatic checks, JSON report.

    python scripts/harness/ootf_coop_campaign.py                          # every level, one co-op session per level
    python scripts/harness/ootf_coop_campaign.py --levels 1,3,7 --timeout 150
    python scripts/harness/ootf_coop_campaign.py --chain 12               # one session: Intro, then the exit to the next level, N times
    python scripts/harness/ootf_coop_campaign.py --solo --levels 0         # same checks without the second instance

Per level (mode "level"): A hosts (DRAKAN_NET_MODE=coop, DRAKAN_GAME=d1, DRAKAN_START_LEVEL=<title>), B joins, both wait for
gameplay, walk, take screenshots (l_start / l_walk1 / l_walk2) and quit. Mode "chain": A starts at the Intro and the probe
DRAKAN_LEVELCHANGE=0,<next level title>,...@1500 takes the exit to the next level in turn; B follows; the report lists the levels reached.
The campaign pack is converted once into build/ootf-run/coopcamp/mods (--reconvert to redo it) and the game lock
(mkdir build/ootf-run/game.lock, shared with ootf_level_run.py and the other agents) is held only while instances run.

Checks per instance (report fields): loaded (afterLevelLoad level= the expected title), control (gameplay=1 after the load),
crash (missing-target, [crash], access violation, non-zero exit), stall ([stall]), hang (killed by this harness), fps floor
(completedFlips of the [frame-uncapped] lines after control: min / median flips per 2 s, 100 = ~50 fps), ghost (the other
player spawned, co-op only), converter stats of the level (dropped props, puzzle plates / markers) from the pack's convert.log,
plus the unique "not found" resources that are not part of the usual noise. Verdict PASS needs loaded + control + no crash /
stall / hang and (co-op) both ghosts. Output: build/harness/ootf-coop/<name>/{report.json,report.md,<level>/{a,b}/...}.
Exit status 0 when every checked level passed. Reference: docs/reference/ootf-coop-harness.md.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import ROOT, game_env, prepare_folder

LEVELS = [
    ("OotF 00 - Intro", "Mountain World/Intro Level/Intro.lvl"),
    ("OotF 01 - Ruined Village", "Mountain World/Ruined Village/Ruined Village.lvl"),
    ("OotF 02 - Ruined Village Book", "Mountain World/Ruined Village/Ruined Village Book.lvl"),
    ("OotF 03 - Wartok Canyons", "Mountain World/Wartok Canyons/wartok canyons.lvl"),
    ("OotF 04 - Grimstone Mines", "Mountain World/Grimstone Mines/mines.lvl"),
    ("OotF 05 - Grotto", "Mountain World/Grotto/grotto.lvl"),
    ("OotF 06 - Islands", "Islands World/Islands/islands.lvl"),
    ("OotF 07 - Alwarren", "Islands World/Alwarren/Alwarren.lvl"),
    ("OotF 08 - Rift Crystal", "Islands World/Rift Crystal/rift crystal.lvl"),
    ("OotF 09 - Islands Night", "Islands World/Islands Night/Islands Night.lvl"),
    ("OotF 10 - Succubus Lair", "Islands World/Succubus Lair/Succubus Lair.lvl"),
    ("OotF 11 - Volcanic", "Volcanic World/Volcano/volcano.lvl"),
    ("OotF 12 - Rift World", "Rift World/Rift World/RiftWorld.lvl"),
]
NL = "\n"
LOCK = ROOT / "build" / "ootf-run" / "game.lock"
CRASH = re.compile(r"missing-target|\[crash\]|access violation|unhandled exception|Segmentation|abort\(\)", re.I)
# resources every run reports as not found (the PS2 data the port stands in for); anything else is worth a look
NOISE = re.compile(
    r"INTERFCE|SYSTEM\.(ADB|SDB|MOD)|WORLDCON|LEVELNRO|RESOURES|FX\.(SDB|MOD)|PSNMUSIC|PSN Music|Interface\.ssd|System\.(vdb|ssd)"
    r"|World Common|_splats|memory card|\.xdb|PS2 Music",
    re.I,
)


def acquire_lock(wait_s: int) -> bool:
    """mkdir lock shared with the other harnesses; a lock older than 40 min with no drakan.exe of ours is stale."""
    deadline = time.monotonic() + wait_s
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    while time.monotonic() < deadline:
        try:
            LOCK.mkdir()
            return True
        except FileExistsError:
            try:
                if time.time() - LOCK.stat().st_mtime > 40 * 60:
                    LOCK.rmdir()
                    continue
            except OSError:
                pass
            time.sleep(5)
    return False


def release_lock() -> None:
    try:
        LOCK.rmdir()
    except OSError:
        pass


def ensure_pack(pack: Path, reconvert: bool) -> Path:
    zip_path = pack / "ootf-levels.zip"
    if reconvert or not zip_path.is_file():
        pack.mkdir(parents=True, exist_ok=True)
        data = ROOT / "build" / "odbx" / "data"
        if not (data / "LEVEL1NA.LVL").is_file():  # extracted from the player's ISO by the single-level harness
            raise SystemExit("build/odbx/data missing: run scripts/harness/ootf_level_run.py once to extract it")
        cmd = [
            "node", "--experimental-transform-types", "--no-warnings", "editor/scripts/ootf-convert.ts",
            "--ootf", str(ROOT / "build" / "ootf" / "cab_disc"), "--ps2", str(data), "--template", "LEVEL1NA",
            "--out", str(zip_path), "--campaign", "1", *[p for _, p in LEVELS],
        ]  # fmt: skip
        res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        (pack / "convert.log").write_text(res.stdout + res.stderr, encoding="utf-8")
        if res.returncode:
            raise SystemExit(f"convert failed, see {pack / 'convert.log'}")
    (pack / "mods.ini").write_text("[mods]\nootf-levels = on\n", encoding="utf-8")
    return pack


def recipe(prefix: str, walk_key: str, seconds: int | None) -> str:
    """Level mode (seconds None): wait for gameplay, walk twice, quit. Chain mode: timed shots for `seconds`."""
    if seconds is None:
        return (
            "clock milliseconds\n0 sync play 300\n2500 shot l_start\n"
            f"3000 key down {walk_key}\n6000 key up {walk_key}\n6300 shot l_walk1\n"
            f"6500 key down {walk_key}\n8000 key up {walk_key}\n8300 shot l_walk2\n9500 quit\n"
        )
    out = "clock milliseconds\n0 sync play 300\n"
    for i in range(seconds // 8):
        out += f"{3000 + i * 8000} shot c{i:02d}\n"
    return out + f"{seconds * 1000} quit\n"


def analyse(text: str, title: str | None) -> dict:
    loads = re.findall(r"afterLevelLoad level='(.*?)'[^\n]*? ok=(\d)", text)
    # control counts only after the expected level loaded (a New Game in the campaign plays the Intro first)
    start = 0 if title is None else len(text)
    if title:
        for m in re.finditer(r"afterLevelLoad level='(.*?)'[^\n]*? ok=1", text):
            if m.group(1) == title or title.endswith(m.group(1)):
                start = m.end()
    first_gp = text.find("(gameplay=1)", start)
    after = text[first_gp:] if first_gp >= 0 else ""
    flips = [int(m) for m in re.findall(r"\[frame-uncapped\] seconds=[\d.]+ completedFlips=(\d+)", after)]
    # the first intervals after control include streaming; the floor is taken over the later ones
    steady = flips[1:] if len(flips) > 2 else flips
    notfound = sorted(
        {
            re.sub(r"\s+", " ", m)[:100]
            for m in re.findall(r"^\[hle\][^\n]*not found[^\n]*$", text, re.M)
            if not NOISE.search(m)
        }
    )
    return {
        "loaded_levels": [lv for lv, ok in loads if ok == "1"],
        "loaded": any(title is None or lv == title or title.endswith(lv) for lv, ok in loads if ok == "1"),
        "control": first_gp >= 0,
        "crash": [ln.strip()[:140] for ln in text.splitlines() if CRASH.search(ln)][:3],
        "stall": [ln.strip()[:140] for ln in text.splitlines() if "[stall]" in ln or "[watchdog t=" in ln][:3],
        "flips_min": min(steady) if steady else None,
        "flips_median": int(statistics.median(steady)) if steady else None,
        "ghost": bool(re.search(r"ghost of player \d+ '.*?' spawned", text)),
        "exits_taken": re.findall(r"exit taken in '(.*?)'", text),
        "follow": len(re.findall(r"level follow: arrived", text)),
        "unexpected_not_found": notfound[:8],
    }


def convert_stats(pack: Path, title: str) -> dict:
    log = pack / "convert.log"
    if not log.is_file():
        return {}
    txt = log.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"^puzzles {re.escape(title)}: (.*)$", txt, re.M)
    return {"puzzles": m.group(1) if m else None, "dropped_props_total": len(re.findall(r"^DROPPED", txt, re.M))}


def run_session(
    out: Path,
    pack: Path,
    port: int,
    title: str | None,
    timeout: int,
    solo: bool,
    chain: int | None,
    extra: list[str],
    soak: bool = False,
    exe: Path | None = None,
    steps_override: str | None = None,
) -> dict:
    """One host (+ joiner) session; returns per-instance analysis."""
    shutil.rmtree(out, ignore_errors=True)
    keys = "a" if solo else "ab"
    for k in keys:
        (out / k).mkdir(parents=True)
    exe = exe or ROOT / "build" / "final-game" / "bin" / "drakan.exe"
    folders = {k: prepare_folder(ROOT / "dist" / "DrakanPC", exe, ROOT / "build" / f"ootfcoop-{k}") for k in keys}
    secs = None if chain is None and not soak else timeout - 10
    common = {
        "DRAKAN_GAME": "d1", "DRAKAN_MODS": str(pack), "DRAKAN_DLSS": "off", "DRAKAN_HD_MOVIES": "0", "GSR_TEXUP": "off",
        "PS2X_GS_SCALE": "1", "DRAKAN_VSYNC": "0", "DRAKAN_FULLSCREEN": "0", "DRAKAN_WINDOW_WIDTH": "960",
        "DRAKAN_WINDOW_HEIGHT": "540", "DRAKAN_WORLD": "2", "PS2X_WATCHDOG": "60", "DRAKAN_GHOST_LOG": "1",
        "DRAKAN_LEVELCHANGE_TRACE": "1",
    }  # fmt: skip
    env = {
        "a": {**common, "DRAKAN_NET_HOST": str(port), "DRAKAN_NET_NAME": "Alpha", "DRAKAN_NET_MODE": "coop"},
        "b": {**common, "DRAKAN_NET_CONNECT": f"127.0.0.1:{port}", "DRAKAN_NET_NAME": "Bravo"},
    }
    if solo:
        env["a"] = dict(common)
    if chain is not None:
        # step 0 is the Intro's exit by index (it has no destination name); the others name the next level, the probe
        # takes the exit whose destination contains the text. @1500 = frames after arrival before each step.
        # Exit zero already takes Intro to Ruined Village; the next destination is Wartok.
        steps = ["0", *[t for t, _ in LEVELS[3:]]][:chain]  # Book is an optional side level
        if steps_override:  # e.g. "0,0,0,0": always the first exit (bounces Ruined Village <-> Book)
            steps = steps_override.split(",")
        env["a"]["DRAKAN_LEVELCHANGE"] = ",".join(steps) + "@1500"
    for kv in extra:
        for k in keys:
            env[k][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    procs: dict[str, subprocess.Popen] = {}
    hung: set[str] = set()
    t0 = time.monotonic()
    try:
        for k in keys:
            (out / k / "recipe.txt").write_text(
                recipe(k, "W" if k == "a" else "A", secs), encoding="utf-8", newline="\n"
            )
            e = game_env(out / k, folders[k].parent, out / k / "recipe.txt", timeout, env[k])
            if title and chain is None:
                e["DRAKAN_START_LEVEL"] = title
            else:
                e.pop("DRAKAN_START_LEVEL", None)
            procs[k] = subprocess.Popen(
                [str(folders[k])], cwd=folders[k].parent, env=e, stdout=(out / k / "stdout_1.log").open("wb"),
                stderr=subprocess.STDOUT,
            )  # fmt: skip
            time.sleep(4)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > timeout + 45:
                for k, p in procs.items():
                    if p.poll() is None:
                        hung.add(k)
                        p.kill()  # own PIDs only
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    res = {}
    for k in keys:
        log = out / k / "stdout_1.log"
        text = log.read_bytes().decode("latin-1") if log.is_file() else ""
        r = analyse(text, title if chain is None else None)
        if chain is not None:
            r["loaded"] = any(level.startswith("OotF ") for level in r["loaded_levels"])
            r["chain_required"] = len(steps)
            r["chain_completed"] = len(r["exits_taken"]) if k == "a" else r["follow"]
        r["exit_code"] = procs[k].returncode
        r["hang"] = k in hung
        r["quit_clean"] = "[in] script quit" in text
        r["shots"] = sorted(p.name for p in (out / k).glob("*.png"))
        res[k] = r
    return res


def verdict(res: dict, coop: bool) -> tuple[bool, list[str]]:
    why = []
    for k in "ab" if coop else "a":
        if k not in res:
            why.append(f"{k}: missing instance evidence")
    for k, r in res.items():
        if r.get("chain_completed", 0) < r.get("chain_required", 0):
            why.append(f"{k}: incomplete chain {r['chain_completed']}/{r['chain_required']}")
        if not r["loaded"]:
            why.append(f"{k}: level not loaded")
        if not r["control"]:
            why.append(f"{k}: no control")
        if r["crash"] or (r["exit_code"] not in (0, None) and not r["hang"]):
            why.append(f"{k}: crash/exit {r['exit_code']}")
        if r["stall"]:
            why.append(f"{k}: stall")
        if r["hang"]:
            why.append(f"{k}: hang")
        if coop and not r["ghost"]:
            why.append(f"{k}: other player not spawned")
    return (not why), why


def reanalyse(out_root: Path, pack: Path, solo: bool) -> dict:
    """Recompute the report from the logs of an earlier run (after changing the checks)."""
    report = {"pack": str(pack), "mode": "solo" if solo else "coop", "sessions": []}
    for d in sorted(p for p in out_root.iterdir() if p.is_dir()):
        idx = None if d.name.startswith("chain") else int(d.name)
        title = LEVELS[idx][0] if idx is not None else None
        res = {}
        for k in "ab":
            log = d / k / "stdout_1.log"
            if not log.is_file():
                continue
            r = analyse(log.read_bytes().decode("latin-1"), title)
            r.update(exit_code=0 if "[in] script quit" in log.read_bytes().decode("latin-1") else None, hang=False,
                     quit_clean="[in] script quit" in log.read_bytes().decode("latin-1"))  # fmt: skip
            res[k] = r
        ok, why = verdict(res, not solo)
        report["sessions"].append({"label": d.name, "level": title or LEVELS[0][0], "pass": ok, "why": why,
                                   "instances": res})  # fmt: skip
    return report


def write_outputs(out_root: Path, report: dict) -> None:
    (out_root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = [
        "| session | level | result | flips min/med (A) | loaded levels (A) | notes |",
        "|---|---|---|---|---|---|",
    ]
    for s in report["sessions"]:
        ra = s["instances"].get("a", {"flips_min": None, "flips_median": None, "loaded_levels": []})
        lines.append(
            f"| {s['label']} | {s['level']} | {'PASS' if s['pass'] else 'FAIL'} | {ra['flips_min']}/{ra['flips_median']} "
            f"| {', '.join(ra['loaded_levels'][-4:])} | {'; '.join(s['why'])} |"
        )
    (out_root / "report.md").write_text(NL.join(lines) + NL, encoding="utf-8")
    print(NL.join(lines))
    print(f"report: {out_root / 'report.json'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--levels", default="all", help="comma separated level numbers 0..12, or all")
    ap.add_argument(
        "--chain", type=int, metavar="N", help="one session walking the exits level to level from the Intro (N hops)"
    )
    ap.add_argument(
        "--soak", action="store_true", help="stay in the level for the whole --timeout (timed shots, no early quit)"
    )
    ap.add_argument(
        "--exe", type=Path, help="game binary to copy into the private folders (default build/final-game/bin)"
    )
    ap.add_argument("--steps", help="chain mode: explicit exit steps (comma separated index or destination text)")
    ap.add_argument(
        "--no-lock", action="store_true", help="do not take the shared game lock (use a unique --port and --name)"
    )
    ap.add_argument("--solo", action="store_true", help="host only (same checks, no joiner)")
    ap.add_argument("--timeout", type=int, default=150, help="per session, seconds")
    ap.add_argument("--name", default="run")
    ap.add_argument("--pack", type=Path, default=ROOT / "build" / "ootf-run" / "coopcamp" / "mods")
    ap.add_argument("--reconvert", action="store_true")
    ap.add_argument("--reanalyse", action="store_true", help="recompute report.json/md for --name from its saved logs")
    ap.add_argument("--port", type=int, default=47921)
    ap.add_argument("--lock-wait", type=int, default=2400)
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for every instance")
    a = ap.parse_args()

    pack = ensure_pack(a.pack.resolve(), a.reconvert)
    out_root = ROOT / "build" / "harness" / "ootf-coop" / a.name
    out_root.mkdir(parents=True, exist_ok=True)
    picks = list(range(len(LEVELS))) if a.levels == "all" else [int(x) for x in a.levels.split(",")]
    if a.reanalyse:
        report = reanalyse(out_root, pack, a.solo)
        write_outputs(out_root, report)
        return 0 if all(s["pass"] for s in report["sessions"]) else 1
    jobs = [("chain", None)] if a.chain else [("level", i) for i in picks]
    report = {"pack": str(pack), "mode": "solo" if a.solo else "coop", "sessions": []}
    ok_all = True
    for kind, idx in jobs:
        title = LEVELS[idx][0] if idx is not None else LEVELS[0][0]
        label = f"chain{a.chain}" if kind == "chain" else f"{idx:02d}"
        if not a.no_lock and not acquire_lock(a.lock_wait):
            print("no game lock, giving up", flush=True)
            return 3
        try:
            print(f"[{label}] {title} ...", flush=True)
            res = run_session(
                out_root / label, pack, a.port, title, a.timeout, a.solo, a.chain, a.env, a.soak, a.exe, a.steps
            )
        finally:
            if not a.no_lock:
                release_lock()
        ok, why = verdict(res, not a.solo)
        ok_all &= ok
        entry = {"label": label, "level": title, "pass": ok, "why": why, "instances": res, **convert_stats(pack, title)}
        report["sessions"].append(entry)
        print(f"[{label}] {'PASS' if ok else 'FAIL'} {'; '.join(why)}", flush=True)
        (out_root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_outputs(out_root, report)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
