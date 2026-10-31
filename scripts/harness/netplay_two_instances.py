"""Two game instances on one PC see each other's Rynn as a ghost (drakan_netplay, docs/reference/netcode.md
"Playing over the network (spike)", docs/journal/2026-10-31/netplay-spike.md).

    python scripts/harness/netplay_two_instances.py                      # A hosts, B connects (about 90 s)
    python scripts/harness/netplay_two_instances.py --mode server        # both connect to drakan_server
    python scripts/harness/netplay_two_instances.py --no-level-change    # stop after the ghost screenshots
    python scripts/harness/netplay_two_instances.py --reject             # profile mismatch must be rejected (about 40 s)
    python scripts/harness/netplay_two_instances.py --pvp --card <mc0 of a save with Rynn's dagger> --port 47910
                                                                          # PvP: A hits B's ghost until B dies and respawns
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
--pvp (docs/reference/netcode.md "PvP", docs/journal/2026-10-31/pvp.md): private folders build/pvp-{a,b}; A equips the
dagger through the inventory (the save in --card must hold it; the cell is the one of build/harness/gw-net) and swings
every 0.9 s at B's ghost, which DRAKAN_PVP_TEST_PLACE holds in front of / behind A (and DRAKAN_PVP_TEST_ANCHOR holds A in
place, so the two real positions stay inside the server's melee range); damage is x8 (DRAKAN_PVP_DAMAGE_SCALE) so a kill
takes four hits. B takes a screenshot every 0.7 s. Checks: A's claims, the server's Hit / Death / Score / Respawn, B's HP
drop, frozen death and respawn, kills / deaths in the score, hits on a dead / protected victim rejected.
Output: build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt, *.png) and summary.json.
"""

from __future__ import annotations

import argparse
import itertools
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
# The level change target: A takes the Surdana exit to the City of the Damned (drakan_levelchange, see
# docs/journal/2026-10-31/level-change.md), B must follow it.
TARGET = "Level 1D - City of the Damned"
TARGET_EXIT = "city"

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
{contact_shots}23500 shot b_a_stopped
{after_shots}{quit_at} quit
"""


def pvp_recipes(weapon: str = "dagger") -> tuple[str, str]:
    """A: equip the weapon (inventory cell hover + ENTER, as in the weapon runs), then swing every 0.9 s (dagger) or draw
    and release the bow (hold the attack button 1.3 s) every 3 s. B: screenshots."""
    cell = (
        "800 cursor 215 218\n900 cursor 216 219\n" if weapon == "dagger" else "800 cursor 215 170\n900 cursor 216 171\n"
    )
    a = "clock milliseconds\n0 sync play 60\n"
    a += (
        "12000 key down TAB\n12120 key up TAB\n0 sync screen 21 20\n"
        + cell
        + "2600 key down ENTER\n2900 key up ENTER\n4200 shot a_equipped\n5000 key down TAB\n5200 key up TAB\n"
        "0 sync play 60\n1500 shot a_pre\n"
    )
    t = 1500
    if weapon == "bow":
        for i in range(16):
            t += 3000
            a += f"{t} button down 1\n{t + 1300} button up 1\n"
            if i % 2 == 1:
                a += f"{t + 1700} shot a_p{i:02d}\n"
    else:
        for i in range(55):
            t += 900
            a += f"{t} button down 1\n{t + 60} button up 1\n"
            if i % 4 == 2:
                a += f"{t + 300} shot a_p{i:02d}\n"
    a += f"{t + 3000} shot a_end\n{t + 5000} quit\n"
    b = "clock milliseconds\n0 sync play 60\n"
    t, i = 14000, 0
    while t < 48000:
        b += f"{t} shot b_s{i:02d}\n"
        t += 700
        i += 1
    b += "90000 quit\n"
    return a, b


def pvp_checks(text: dict[str, str], checks: dict[str, bool], frag_limit: int = 0) -> None:
    a, b = text["a"], text["b"]
    claims = len(re.findall(r"\[netplay [\d.]+\] claim: hit on player 2", a))
    hits = re.findall(r"\[netplay [\d.]+\] hit: player 1 hit player 2 for (\d+) \(weapon (\d)\), health now (\d+)", a)
    deaths = re.findall(r"\[netplay [\d.]+\] death: player 2 killed by player 1", a)
    resp = re.findall(r"\[netplay [\d.]+\] respawn: player 2 at spawn (\d+) with health 100", a)
    kills = [int(m) for m in re.findall(r"score: player 1 kills (\d+) deaths 0", a)]
    deaths_score = [int(m) for m in re.findall(r"score: player 2 kills 0 deaths (\d+)", a)]
    print(f"A: {claims} claims, {len(hits)} accepted hits, {len(deaths)} deaths, respawns {resp}")
    print(f"scores seen by A: player 1 kills {kills}, player 2 deaths {deaths_score}")
    checks["a: hits on the ghost became claims"] = claims >= 4
    checks["server accepted hits (damage x8 of the dagger's 4)"] = bool(hits) and all(
        h[0] == "32" and h[1] == "0" for h in hits
    )
    checks["server: Death after the health ran out"] = bool(deaths) and any(h[2] == "0" for h in hits)
    checks["score: kills 1 / deaths 1 broadcast"] = 1 in kills and 1 in deaths_score
    checks["server: Respawn 3 s after the death"] = len(resp) >= 2  # the match start and the respawn
    checks["server: hits on a dead / protected victim rejected"] = (
        "hit rejected: victim dead" in a and "hit rejected: victim protected" in a
    )
    checks["b: engine hit reaction + health set"] = len(re.findall(r"\[pvp\] hit by player 1: 32 damage", b)) >= 3
    checks["b: death shown (frozen with the death clip)"] = "[pvp] killed by player 1" in b
    checks["b: respawned at a spawn point"] = re.search(r"\[pvp\] respawned at spawn \d+", b) is not None
    if frag_limit:
        for k in "ab":
            for line in text[k].splitlines():
                if "match over" in line:
                    print(f"{k.upper()} {line.strip()}")
            checks[f"{k}: match over at {frag_limit} kills, player 1 wins, results listed"] = (
                re.search(
                    rf"match over \(frag limit\): player 1 wins, results \(kills/deaths\): .*'Alpha' {frag_limit}/0",
                    text[k],
                )
                is not None
            )
    checks["b: no game over prompt / level reload"] = "afterSaveRestore" not in b.split("[pvp] killed by player 1")[-1]


def ranged_checks(text: dict[str, str], checks: dict[str, bool]) -> None:
    """Arrows: A's projectile scan sees them, the crossing with B's ghost becomes a weapon 2 claim, the server accepts."""
    a, b = text["a"], text["b"]
    arrows = re.findall(r"\[pvp\] arrow [0-9a-f]+ hit ghost \d+ \(player 2\) damage (\d+) -> claim", a)
    claims = re.findall(r"claim: hit on player 2, weapon 2, damage (\d+)", a)
    hits = re.findall(r"hit: player 1 hit player 2 for (\d+) \(weapon 2\), health now (\d+)", a)
    print(f"A: {len(arrows)} arrow crossings, {len(claims)} weapon 2 claims, {len(hits)} accepted hits {hits}")
    checks["a: an arrow crossed B's ghost (projectile scan)"] = len(arrows) >= 1
    checks["a: sent weapon 2 claims with the arrow damage"] = bool(claims) and all(c == "5" for c in claims)
    checks["server: accepted the projectile hits (damage x8)"] = bool(hits) and all(h[0] == "40" for h in hits)
    checks["b: hit reaction from an arrow"] = re.search(r"\[pvp\] hit by player 1: 40 damage", b) is not None


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


def game_env(
    out: Path, folder: Path, recipe: Path, timeout: int, extra: dict[str, str], card_src: Path | None = None
) -> dict[str, str]:
    card = out / "card" / "mc0"
    if card.parent.exists():
        shutil.rmtree(card.parent)
    if card_src:
        shutil.copytree(card_src, card)
    else:
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
            "DRAKAN_NET_TRACE": "1",
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


def trace_report(out: Path, log_a: str, log_b: str, checks: dict[str, bool]) -> None:
    """Per-frame ghost positions on B (DRAKAN_NET_TRACE) -> ghost_deltas.csv, a contact sheet of B's screenshots."""
    rows = []
    for m in re.finditer(
        r"\[nettrace ([\d.]+)\] p(\d+) (\S+) (\S+) (\S+) yaw (\S+) lower (\w+)@(\S+) upper (\w+)@(\S+)", log_b
    ):
        rows.append(
            (float(m[1]), float(m[3]), float(m[4]), float(m[5]), float(m[6]), m[7], float(m[8]), m[9], float(m[10]))
        )
    lines = ["t,dt_ms,dx,dy,dz,dist,yaw,lower,lower_t,upper,upper_t"]
    moving = []
    for prev, cur in itertools.pairwise(rows):
        dt = cur[0] - prev[0]
        if dt <= 0 or dt > 0.5:
            continue
        dist = ((cur[1] - prev[1]) ** 2 + (cur[2] - prev[2]) ** 2 + (cur[3] - prev[3]) ** 2) ** 0.5
        lines.append(
            f"{cur[0]:.4f},{dt * 1000:.2f},{cur[1] - prev[1]:.2f},{cur[2] - prev[2]:.2f},{cur[3] - prev[3]:.2f},"
            f"{dist:.2f},{cur[4]:.3f},{cur[5]},{cur[6]:.3f},{cur[7]},{cur[8]:.3f}"
        )
        if 0.5 < dist < 1000:  # larger steps are respawns / teleports, not walking
            moving.append((dt, dist))
    (out / "ghost_deltas.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    local = []
    for m in re.finditer(r"\[nettrace-local ([\d.]+)\] (\S+) (\S+) (\S+) ", log_a):
        local.append((float(m[2]), float(m[3]), float(m[4])))

    def steps(points):
        out_steps = []
        for p, c in itertools.pairwise(points):
            d = sum((c[k] - p[k]) ** 2 for k in range(3)) ** 0.5
            if 0.5 < d < 1000:
                out_steps.append(d)
        return out_steps

    def stat(v):
        if len(v) < 10:
            return None
        mean = sum(v) / len(v)
        med = sorted(v)[len(v) // 2]
        sd = (sum((x - mean) ** 2 for x in v) / len(v)) ** 0.5
        return len(v), mean, sd / mean, sum(1 for x in v if x < 0.25 * med), sum(1 for x in v if x > 2.5 * med)

    g = stat(steps([r[1:4] for r in rows]))
    a = stat(steps(local))
    for name, st in (("ghost on B (per tick)", g), ("A's own pose (source)", a)):
        if st:
            print(f"{name}: {st[0]} moving ticks, step mean {st[1]:.1f}, cv {st[2]:.2f}, stalls {st[3]}, jumps {st[4]}")
        else:
            print(f"{name}: too few moving ticks")
    checks["b: ghost moved (traced ticks)"] = g is not None
    if g and a:
        checks["b: ghost step cv <= 1.25 x the source's"] = g[2] <= 1.25 * a[2]
    shots = sorted((out / "b").glob("b_c??.png"))
    if shots:
        from PIL import Image

        imgs = [Image.open(p).convert("RGB").crop((280, 160, 680, 400)).resize((300, 180)) for p in shots]
        cols = 6
        sheet = Image.new("RGB", (cols * 300, ((len(imgs) + cols - 1) // cols) * 180))
        for i, im in enumerate(imgs):
            sheet.paste(im, ((i % cols) * 300, (i // cols) * 180))
        sheet.save(out / "b_contact_sheet.png")
        print(f"contact sheet: {out / 'b_contact_sheet.png'} ({len(imgs)} frames)")


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
    ap.add_argument("--reload", action="store_true", help="level change = A quick-saves and quick-loads (F5 / F8)")
    ap.add_argument("--reject", action="store_true", help="only B, against a drakan_server of another disc profile")
    ap.add_argument("--timeout", type=int, default=150, help="seconds per instance")
    ap.add_argument("--card", type=Path, help="mc0 folder copied into both private memory cards (e.g. a save)")
    ap.add_argument("--recipe-a", type=Path, help="input recipe file replacing A's built-in one")
    ap.add_argument("--recipe-b", type=Path, help="input recipe file replacing B's built-in one")
    ap.add_argument("--prefix", default="netplay", help="private play folders are build/<prefix>-a and -b")
    ap.add_argument("--pvp", action="store_true", help="PvP verification (needs --card, see above)")
    ap.add_argument(
        "--weapon", choices=("dagger", "bow"), default="dagger", help="--pvp: bow = A shoots arrows at B's ghost"
    )
    ap.add_argument("--frag-limit", type=int, default=0, help="--pvp: the match ends at this many kills (results)")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for both instances")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V", help="extra environment for A only")
    ap.add_argument("--env-b", action="append", default=[], metavar="K=V", help="extra environment for B only")
    a = ap.parse_args()
    if a.pvp:
        if not a.card or not a.card.is_dir():
            raise SystemExit("--pvp needs --card <mc0 folder of a save with Rynn's dagger in the usual inventory cell>")
        if a.prefix == "netplay":
            a.prefix = "pvp"
        if a.name == "netplay":
            a.name = "pvp"
        a.no_level_change = True
        a.env_a += [f"DRAKAN_PVP_TEST_PLACE={700 if a.weapon == 'bow' else 140}", "DRAKAN_PVP_TEST_ANCHOR=1"]
        a.env += ["DRAKAN_PVP_DAMAGE_SCALE=8"]
        if a.frag_limit:
            a.env_a += [f"DRAKAN_NET_FRAG_LIMIT={a.frag_limit}"]

    if not a.exe.is_file():
        raise SystemExit(f"{a.exe} not found (build ps2EntryRunner first)")
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    (out / "a").mkdir(parents=True)
    (out / "b").mkdir(parents=True)
    exe_a = prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-a")
    exe_b = prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-b")
    if a.reject:
        return run_reject(a, out, exe_b)

    level_change = ""
    if a.reload and not a.no_level_change:
        level_change = "24000 key down F5\n24800 key up F5\n33000 key down F8\n33700 key up F8\n"
    quit_a = 56000
    contact = "".join(f"{15600 + i * 110} shot b_c{i:02d}\n" for i in range(18))
    after = "" if a.no_level_change else "38000 shot b_a_gone\n46000 shot b_a_gone2\n"
    recipes = {
        "a": RECIPE_A.format(level_change=level_change, quit_at=quit_a),
        "b": RECIPE_B.format(contact_shots=contact, after_shots=after, quit_at=quit_a - 2000),
    }
    if a.pvp:
        recipes["a"], recipes["b"] = pvp_recipes(a.weapon)
    for k, custom in (("a", a.recipe_a), ("b", a.recipe_b)):
        if custom:
            recipes[k] = custom.read_text(encoding="utf-8")
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
    if not a.reload and not a.no_level_change:
        env_a["DRAKAN_LEVELCHANGE"] = TARGET_EXIT + "@1200"  # the exit trigger fired after 1200 frames in Surdana
    env_b = {"DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Bravo"}
    for kv in a.env + a.env_a:
        env_a[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    for kv in a.env + a.env_b:
        env_b[kv.split("=", 1)[0]] = kv.split("=", 1)[1]

    procs: dict[str, subprocess.Popen] = {}
    logs = {k: out / k / "stdout_1.log" for k in "ab"}
    t0 = time.monotonic()
    try:
        for key, exe, extra, folder in (("a", exe_a, env_a, exe_a.parent), ("b", exe_b, env_b, exe_b.parent)):
            env = game_env(out / key, folder, out / key / "recipe.txt", a.timeout, extra, a.card)
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
        spawns = len(re.findall(r"ghost of player \d+ '.*' spawned", text["b"]))
        removed = re.search(r"ghost of player \d+ removed", text["b"]) is not None
        if a.reload:
            checks["a: reload seen (save restore)"] = "afterSaveRestore" in text["a"]
            checks["b: ghost removed while A loads"] = removed
            checks["b: ghost back after A's load"] = spawns >= 2
        else:
            checks["a: level change to the target"] = f"level '{TARGET}' loaded" in text["a"]
            checks["b: followed the host"] = f"level '{TARGET}' loaded" in text["b"]
            checks["b: ghost removed when A left the level"] = removed
            checks["b: ghost of A back in the new level"] = spawns >= 2
            checks["a: ghost of B back in the new level"] = (
                len(re.findall(r"ghost of player \d+ '.*' spawned", text["a"])) >= 2
            )
    if a.pvp and a.weapon == "bow":
        ranged_checks(text, checks)
    elif a.pvp:
        pvp_checks(text, checks, a.frag_limit)
    else:
        trace_report(out, text["a"], text["b"], checks)
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
