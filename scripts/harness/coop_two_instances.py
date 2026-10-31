"""Co-op: two instances play one level together (drakan_coop + drakan_netplay ghosts,
docs/journal/2026-10-31/coop-design.md section 7).

    python scripts/harness/coop_two_instances.py                       # fight: both attack the orcs near the campfire
    python scripts/harness/coop_two_instances.py --scenario door       # A opens a town door, B watches
    python scripts/harness/coop_two_instances.py --scenario pickup     # A takes a gold sack, B's copy must go
    python scripts/harness/coop_two_instances.py --mode mark           # B only measures, nothing is written to its guest
    python scripts/harness/coop_two_instances.py --card <mc0 of a save with Rynn's dagger>

Private play folders build/coop2-a and build/coop2-b (copied exe and DLLs, junctions for the assets, own memory cards
copied from --card, default build/harness/gw-net/a/card/mc0 which holds the dagger); helpers come from
netplay_two_instances.py. Both instances run drakan_netplay (A hosts, ghosts of each other, PvP off) and drakan_coop
(creature proxies, claims, kills, targeting, object events) on their own UDP ports. A and B teleport next to the Yellow
Orcs of Surdana (test aid DRAKAN_COOP_TEST_GOTO) and swing the dagger every 0.9 s; claim damage is scaled
(DRAKAN_COOP_DAMAGE_SCALE) so a kill takes seconds. Output: build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt,
*.png) and the `[coop` lines; the exit status is 0 when every check of the scenario passed.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import ROOT, game_env, prepare_folder

ORC_SPOT_A = "676100,159748,523349"  # 970 units from the nearest Yellow Orc, behind B
ORC_SPOT_B = "675700,159748,523349"  # 570 units from it
ORC_RES = "51206"  # Yellow Orc : Center
DOOR_A = "475700,160700,487600"  # in front of Town Door 16660 (about 2100 units south of it)
SACK = "447359,174700,380000"  # near the SackoGold in the chest room (id 16497)

EQUIP = (
    "12000 key down TAB\n12120 key up TAB\n0 sync screen 21 20\n800 cursor 215 218\n900 cursor 216 219\n"
    "2600 key down ENTER\n2900 key up ENTER\n4200 shot equipped\n5000 key down TAB\n5200 key up TAB\n"
    "0 sync play 60\n1500 shot pre\n"
)


DEFAULT_CARD = ROOT / "build" / "harness" / "gw-net" / "a" / "card" / "mc0"


def make_fixture(card: Path, name: str, spot: str) -> Path:
    """The dagger save with Rynn moved beside the Yellow Orcs (combat_save_fixture.py, the source is not touched)."""
    dst = ROOT / "build" / f"coop2-fixture-{name}"
    if dst.exists():
        shutil.rmtree(dst)
    subprocess.run(
        [
            sys.executable,
            "-I",
            str(ROOT / "scripts" / "harness" / "combat_save_fixture.py"),
            str(card.parent),
            str(dst),
            *spot.split(","),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return dst / "mc0"


def swings(start: int, count: int, tag: str, every: int = 900, shot_every: int = 3) -> str:
    out, t = "", start
    for i in range(count):
        t += every
        if i % 8 == 0:  # lock on (F) so the swings face the nearest enemy
            out += f"{t - 400} key down F\n{t - 300} key up F\n"
        out += f"{t} button down 1\n{t + 60} button up 1\n"
        if i % shot_every == 1:
            out += f"{t + 350} shot {tag}{i:02d}\n"
    return out


NL = chr(10)


def recipes(scenario: str) -> tuple[str, str, int]:
    head = "clock milliseconds\n0 sync play 60\n"
    if scenario in ("fight", "death"):
        # B swings from the start (the orcs are forced onto its ghost); A joins 14 s later, so both hit the same orc.
        a = head + EQUIP + "14000 key down W\n14200 key up W\n" + swings(14500, 40, "a_f", shot_every=5)
        a += "3000 shot a_end\n3500 quit\n"
        b = head + EQUIP + "1000 key down W\n2200 key up W\n" + swings(1500, 50, "b_f", shot_every=2)
        b += "3000 shot b_end\n3500 quit\n"
        return a, b, 120
    if scenario in ("chest", "walkover"):
        # B stands in front of the nearest chest / elixir (test aid DRAKAN_COOP_TEST_TO) and uses it (E) / walks over it.
        # A stays where it is and watches.
        a = head + f"1500 shot pre{NL}" + "".join(f"{4000 + i * 3000} shot a_c{i:02d}{NL}" for i in range(9))
        a += f"34000 quit{NL}"
        # a short walk first: the test aid takes her heading from how she moved
        b = head + f"500 key down W{NL}1200 key up W{NL}1500 shot pre{NL}3500 shot b_before{NL}"
        for i in range(6):  # E picks up a loose item (and would open a chest) in front of her
            t = 5000 + i * 2500
            b += f"{t} key down E{NL}{t + 150} key up E{NL}{t + 1500} shot b_use{i}{NL}"
        for i in range(5):  # and a few punches: a chest may open when it is hit
            t = 21000 + i * 900
            b += f"{t} button down 1{NL}{t + 80} button up 1{NL}"
        b += f"27500 shot b_end{NL}29000 quit{NL}"
        return a, b, 100
    if scenario == "latejoin":
        # A opens the town door and takes a pickup (test aid), long before B starts (--b-delay); B must get both at the
        # catch-up.
        a = head + f"14000 key down W{NL}17500 key up W{NL}19000 shot a_at_door{NL}"
        a += "".join(f"{25000 + i * 5000} shot a_late{i:02d}{NL}" for i in range(16)) + f"108000 quit{NL}"
        b = head + "".join(f"{14000 + i * 3000} shot b_l{i:02d}{NL}" for i in range(10)) + f"45000 quit{NL}"
        return a, b, 150
    if scenario == "door":
        # The card start is in a corridor that ends at Town Door 16658 (about 7600 units ahead). A runs to it and presses
        # E (use) a few times; B stays and watches the door.
        a = (
            head
            + "14000 shot a_before"
            + NL
            + "15000 key down W"
            + NL
            + "18500 key up W"
            + NL
            + "19500 shot a_at_door"
            + NL
        )
        for i in range(4):
            a += f"{20500 + i * 2500} key down E{NL}{20650 + i * 2500} key up E{NL}{21700 + i * 2500} shot a_use{i}{NL}"
        a += f"32000 shot a_end{NL}34000 quit{NL}"
        b = head + "".join(f"{14000 + i * 1500} shot b_d{i:02d}{NL}" for i in range(20)) + f"50000 quit{NL}"
        return a, b, 100
    a = head + f"16000 shot a_before{NL}18000 key down W{NL}20000 key up W{NL}22000 shot a_end{NL}40000 quit{NL}"
    b = head + "".join(f"{14000 + i * 1500} shot b_p{i:02d}{NL}" for i in range(20)) + f"50000 quit{NL}"
    return a, b, 100


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="coop2")
    ap.add_argument(
        "--scenario", choices=("fight", "death", "chest", "walkover", "door", "pickup", "latejoin"), default="fight"
    )
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--card", type=Path, default=DEFAULT_CARD)
    ap.add_argument("--mode", choices=("follow", "mark"), default="follow")
    ap.add_argument("--port", type=int, default=47830, help="drakan_netplay port; drakan_coop uses port + 1")
    ap.add_argument("--timeout", type=int, default=110)
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, both instances")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V")
    ap.add_argument("--env-b", action="append", default=[], metavar="K=V")
    ap.add_argument("--b-delay", type=float, default=0.0, help="seconds B starts after A (latejoin: 30)")
    ap.add_argument("--summon", action="store_true", help="host test aid: move two orcs to the players")
    a = ap.parse_args()
    cards = {"a": a.card, "b": a.card}
    if a.scenario in ("fight", "death", "chest", "walkover") and a.card == DEFAULT_CARD:
        cards = {"a": make_fixture(a.card, "a", ORC_SPOT_A), "b": make_fixture(a.card, "b", ORC_SPOT_B)}
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    (out / "a").mkdir(parents=True)
    (out / "b").mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"coop2-{k}") for k in "ab"}
    ra, rb, timeout = recipes(a.scenario)
    (out / "a" / "recipe.txt").write_text(ra, encoding="utf-8", newline="\n")
    (out / "b" / "recipe.txt").write_text(rb, encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_PVP": "0",
        "DRAKAN_COOP_DAMAGE_SCALE": "8",
        "DRAKAN_COOP_PROBE": "1",
    }
    env = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_COOP_HOST": str(a.port + 1),
        },
        "b": {
            **common,
            "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}",
            "DRAKAN_NET_NAME": "Bravo",
            "DRAKAN_COOP_CONNECT": f"127.0.0.1:{a.port + 1}",
            "DRAKAN_COOP_MODE": a.mode,
        },
    }
    go = {k: out / k / "go.txt" for k in "ab"}
    spots = ("", "")
    if spots[0]:
        env["a"]["DRAKAN_COOP_TEST_GOTO"] = f"{spots[0]}@{go['a']}"
        env["b"]["DRAKAN_COOP_TEST_GOTO"] = f"{spots[1]}@{go['b']}"
    env["a"]["DRAKAN_COOP_TEST_GOD"] = "1"
    if a.summon:
        env["a"]["DRAKAN_COOP_TEST_SUMMON"] = f"{ORC_RES},2@{go['a']}"
    if a.scenario in ("fight", "death"):
        env["a"]["DRAKAN_COOP_TEST_FORCE_GHOST"] = "all"
    if a.scenario == "death":  # B takes 6x damage from the host's orcs so it dies within the run
        env["b"]["DRAKAN_COOP_TEST_HURT_SCALE"] = "6"
    if a.scenario in ("chest", "walkover"):
        env["a"]["DRAKAN_COOP_RETARGET"] = "0"  # the orcs near the start stay out of it
        env["b"]["DRAKAN_COOP_TEST_TO"] = f"{'100c5,-110' if a.scenario == 'chest' else '10059,0'}@{go['b']}"
    if a.scenario == "pickup":
        env["a"]["DRAKAN_COOP_TEST_TAKE"] = str(go["a"])
    if a.scenario == "latejoin":
        env["a"]["DRAKAN_COOP_TEST_TAKE"] = str(go["a"])
        env["a"]["DRAKAN_COOP_RETARGET"] = "1"
        a.b_delay = a.b_delay or 30.0
    if a.scenario in ("door", "latejoin"):  # no setup to wait for
        for k in "ab":
            go[k].write_text("go", encoding="utf-8")
    for kv in a.env + a.env_a:
        env["a"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    for kv in a.env + a.env_b:
        env["b"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    procs, logs = {}, {k: out / k / "stdout_1.log" for k in "ab"}
    t0 = time.monotonic()
    try:
        for k in "ab":
            e = game_env(out / k, exes[k].parent, out / k / "recipe.txt", max(a.timeout, timeout), env[k], cards[k])
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=e, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            print(f"started {k.upper()} (pid {procs[k].pid})", flush=True)
            time.sleep(a.b_delay if k == "a" else 4)
        while any(p.poll() is None for p in procs.values()):
            if a.scenario == "pickup" and not go["a"].exists() and logs["b"].is_file():
                if b"client: keyed" in logs["b"].read_bytes():  # B has the level keyed: events can be applied
                    go["a"].write_text("go", encoding="utf-8")
            for k in "ab":  # the teleport waits for the equip sequence (the inventory swaps the controlled actor)
                if not go[k].exists() and logs[k].is_file() and b"screenshot pre -> ok" in logs[k].read_bytes():
                    go[k].write_text("go", encoding="utf-8")
            if time.monotonic() - t0 > max(a.timeout, timeout) + 40:
                print("timeout: killing", flush=True)
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in "ab"}
    for k in "ab":
        for line in text[k].splitlines():
            if "[coop" in line:
                print(f"{k.upper()} {line.strip()}")
    checks: dict[str, bool] = {}
    checks["both instances exited cleanly"] = all(
        procs[k].returncode == 0 and "[in] script quit" in text[k] for k in "ab"
    )
    matched = re.findall(r"client stats: \d+ packets, keyed \d+, matched (\d+)", text["b"])
    checks["b: matched host creatures"] = bool(matched) and max(int(m) for m in matched) > 0
    if a.scenario in ("fight", "death"):
        checks["b: claims sent for hits on proxies"] = "client: claim on creature" in text["b"]
        checks["a: claims applied to the real creature"] = "host: claim applied" in text["a"]
        checks["a: a creature died, reported"] = "host: creature" in text["a"] and "died (" in text["a"]
        checks["b: the death reached the client"] = "died on the host" in text["b"]
        checks["a: enemy retargeted to the ghost"] = "now targets the partner's ghost" in text["a"]
        checks["a: ghost hit by an enemy"] = re.search(r"host: ghost [0-9a-f]+ hit by", text["a"]) is not None
        checks["b: took damage from a host enemy"] = "took " in text["b"] and "from a host enemy" in text["b"]
        if a.scenario == "death":
            checks["b: the local player died"] = "client: the local player died" in text["b"]
            checks["a: the partner's death reached the host"] = "host: the partner died" in text["a"]
            checks["a: respawn granted after the delay"] = "respawn is granted" in text["a"]
            checks["b: respawned beside the host"] = "client: respawned beside the host" in text["b"]
    elif a.scenario == "door":
        # The door opens by itself when a player is near: B's door must open while B stands still at the start.
        opened = "data+10 00000000 -> 00000001"
        checks["a: door opened (state fields logged)"] = opened in text["a"]
        checks["b: door opened for the partner while b stood still"] = opened in text["b"]
    elif a.scenario == "latejoin":
        checks["a: catch-up sent with a taken pickup"] = (
            re.search(r"catch-up sent: [1-9]\d* taken pickups", text["a"]) is not None
        )
        checks["a: catch-up sent with an open door"] = (
            re.search(r"taken pickups, [1-9]\d* open doors", text["a"]) is not None
        )
        checks["b: asked for the catch-up"] = "asked the host for the catch-up" in text["b"]
        checks["b: the taken pickup removed"] = "taken by the partner: removed" in text["b"]
        checks["b: the open door opened"] = "was open on the host: opening it" in text["b"]
        checks["b: door fields show it opened"] = "data+10 00000000 -> 00000001" in text["b"]
    elif a.scenario == "walkover":
        # B walks onto a loose potion: B's copy goes into its inventory, A's world copy must disappear, and A must not
        # get the item (no pickup message on A).
        checks["b: the pickup was taken, reported"] = "pickup" in text["b"] and "-> peer" in text["b"]
        checks["a: the partner's pickup removed"] = "taken by the partner: removed" in text["a"]
        checks["a: no pickup message for the item (the partner has it)"] = "msg 10c1" not in text["a"]
    elif a.scenario == "chest":
        checks["b: stood at the chest"] = "test: stood" in text["b"]
    else:
        checks["a: pickup left the registry, reported"] = "left the registry -> peer" in text["a"]
        checks["b: the partner's pickup removed"] = "taken by the partner: removed" in text["b"]
    for name, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
