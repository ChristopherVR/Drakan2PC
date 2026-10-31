"""Co-op: two instances play one level together (drakan_coop + drakan_netplay ghosts,
docs/journal/2026-10-31/coop-design.md section 7).

    python scripts/harness/coop_two_instances.py                       # fight: both attack the orcs near the campfire
    python scripts/harness/coop_two_instances.py --scenario door       # A opens a town door, B watches
    python scripts/harness/coop_two_instances.py --scenario pickup     # A takes a gold sack, B's copy must go
    python scripts/harness/coop_two_instances.py --mode mark           # B only measures, nothing is written to its guest
    python scripts/harness/coop_two_instances.py --card <mc0 of a save with Rynn's dagger>
    python scripts/harness/coop_two_instances.py --players 3 --folder coop3p --name coop3p-fight   # A hosts, B and C join

Private play folders build/coop2-a and build/coop2-b (--players 3 adds -c; copied exe and DLLs, junctions for the assets, own memory cards
copied from --card, default build/harness/gw-net/a/card/mc0 which holds the dagger); helpers come from
netplay_two_instances.py. Both instances run drakan_netplay (A hosts, ghosts of each other, PvP off) and drakan_coop
(creature proxies, claims, kills, targeting, object events) over that session (DRAKAN_NET_MODE=coop, protocol v6). A and B teleport next to the Yellow
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

ORC_SPOT_A = "677300,159748,523349"  # 1600 units from B, so the lock-on (F) never prefers the host ghost to the orcs
ORC_SPOT_B = "675700,159748,523349"  # 570 units from it
ORC_SPOT_C = "675500,159748,523349"  # the third player, 370 units from it
ORC_RES = "51206"  # Yellow Orc : Center
DOOR_A = "475700,160700,487600"  # in front of Town Door 16660 (about 2100 units south of it)
SACK = "447359,174700,380000"  # near the SackoGold in the chest room (id 16497)
GOLD_SPOT = "447359,174700,380000"  # Rynn starts here in the gold scenario (the sack is 1400 units away)

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
        a = head + EQUIP + "14000 key down W\n14200 key up W\n" + swings(14500, 70, "a_f", shot_every=5)
        a += "3000 shot a_end\n3500 quit\n"
        b = head + EQUIP + "1000 key down W\n2200 key up W\n" + swings(1500, 85, "b_f", shot_every=4)
        b += "3000 shot b_end\n3500 quit\n"
        return a, b, 170
    if scenario == "migrate":
        # Both play against a dedicated server (drakan_server --mode coop); A joined first and owns the creature world.
        # A fights for a while and quits; B (the guest, swinging all along) must take the creature world over.
        a = head + EQUIP + swings(1500, 14, "a_f", shot_every=5) + f"26000 shot a_end{NL}27000 quit{NL}"
        b = head + EQUIP + swings(1500, 80, "b_f", shot_every=8) + f"76000 shot b_end{NL}77000 quit{NL}"
        return a, b, 140
    if scenario in ("chesthost", "chestlate"):
        # The same as "chest" with the players swapped (A, the host, is the one at the chest; B only watches).
        b, a, timeout = recipes("chest")
        if scenario == "chestlate":  # B starts late and must be told about the open chest at the catch-up
            a = a.replace("29000 quit", "95000 quit")
            b = head + "".join(f"{14000 + i * 3000} shot b_l{i:02d}{NL}" for i in range(10)) + f"45000 quit{NL}"
            timeout = 150
        return a, b, timeout
    if scenario in ("chest", "walkover", "gold"):
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
        "--players", type=int, choices=(2, 3), default=2, help="instances: A hosts, B (and C) join; C plays B's recipe"
    )
    ap.add_argument(
        "--folder", default="coop2", help="private play folder prefix under build/ (<prefix>-a, <prefix>-b)"
    )
    ap.add_argument(
        "--scenario",
        choices=(
            "fight",
            "death",
            "chest",
            "chesthost",
            "chestlate",
            "walkover",
            "gold",
            "door",
            "pickup",
            "latejoin",
            "migrate",
        ),
        default="fight",
    )
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--card", type=Path, default=DEFAULT_CARD)
    ap.add_argument("--mode", choices=("follow", "mark"), default="follow")
    ap.add_argument(
        "--port", type=int, default=47830, help="drakan_netplay port (co-op rides the same session since protocol v6)"
    )
    ap.add_argument("--timeout", type=int, default=110)
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, both instances")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V")
    ap.add_argument("--env-b", action="append", default=[], metavar="K=V")
    ap.add_argument("--c-delay", type=float, default=4.0, help="seconds C starts after B (latejoin, 3 players: 20)")
    ap.add_argument("--b-delay", type=float, default=0.0, help="seconds B starts after A (latejoin: 30)")
    ap.add_argument(
        "--spot",
        default="",
        help="x,y,z where both players start (chest scenarios: the gold room is the GOLD_SPOT constant)",
    )
    ap.add_argument(
        "--server",
        type=Path,
        default=ROOT / "build" / "runtime-tests" / "bin" / "Release" / "drakan_server.exe",
        help="migrate scenario: the dedicated server both instances join",
    )
    ap.add_argument("--summon", action="store_true", help="host test aid: move two orcs to the players")
    a = ap.parse_args()
    keys = "abc"[: a.players]
    cards = {k: a.card for k in keys}
    if a.spot and a.card == DEFAULT_CARD:
        cards = {k: make_fixture(a.card, k, a.spot) for k in keys}
    elif (
        a.scenario in ("fight", "death", "migrate", "chest", "chesthost", "chestlate", "walkover")
        and a.card == DEFAULT_CARD
    ):
        spots = {"a": ORC_SPOT_A, "b": ORC_SPOT_B, "c": ORC_SPOT_C}
        cards = {k: make_fixture(a.card, k, spots[k]) for k in keys}
    elif a.scenario == "gold" and a.card == DEFAULT_CARD:
        cards = {k: make_fixture(a.card, k, GOLD_SPOT) for k in keys}
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    for k in keys:
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.folder}-{k}") for k in keys}
    ra, rb, timeout = recipes(a.scenario)
    recipe_of = {"a": ra, "b": rb, "c": rb}
    for k in keys:
        (out / k / "recipe.txt").write_text(recipe_of[k], encoding="utf-8", newline="\n")
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
            "DRAKAN_NET_MODE": "coop",  # protocol v6 co-op match: the creature stream rides the drakan::net session
        },
        "b": {
            **common,
            "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}",
            "DRAKAN_NET_NAME": "Bravo",
            "DRAKAN_COOP_MODE": a.mode,
        },
    }
    env["c"] = {**env["b"], "DRAKAN_NET_NAME": "Charlie"}
    if a.scenario == "migrate":
        a.b_delay = a.b_delay or 12.0  # A must be the first to join: it owns the creature world
        env["a"].pop("DRAKAN_NET_HOST")
        env["a"].pop("DRAKAN_NET_MODE")
        env["a"]["DRAKAN_NET_CONNECT"] = f"127.0.0.1:{a.port}"
        env["a"]["DRAKAN_COOP_RETARGET"] = "1"
        env["a"]["DRAKAN_COOP_TEST_FORCE_GHOST"] = "all"
        env["b"]["DRAKAN_COOP_RETARGET"] = "1"
        env["b"]["DRAKAN_COOP_TEST_FORCE_GHOST"] = "all"  # the new owner's orcs must still behave after the takeover
    go = {k: out / k / "go.txt" for k in keys}
    spots = ("", "")
    if spots[0]:
        env["a"]["DRAKAN_COOP_TEST_GOTO"] = f"{spots[0]}@{go['a']}"
        env["b"]["DRAKAN_COOP_TEST_GOTO"] = f"{spots[1]}@{go['b']}"
    env["a"]["DRAKAN_COOP_TEST_GOD"] = "1"
    if a.summon:
        env["a"]["DRAKAN_COOP_TEST_SUMMON"] = f"{ORC_RES},2@{go['a']}"
    if a.scenario in ("fight", "death"):
        env["a"]["DRAKAN_COOP_TEST_FORCE_GHOST"] = "all"
    if a.scenario == "death":  # the guests take 6x damage from the host's orcs so they die within the run
        for k in keys[1:]:
            env[k]["DRAKAN_COOP_TEST_HURT_SCALE"] = "6"
    if a.scenario in ("chest", "chesthost", "chestlate", "walkover", "gold"):
        env["a"]["DRAKAN_COOP_RETARGET"] = "0"  # the orcs near the start stay out of it
        who = "a" if a.scenario in ("chesthost", "chestlate") else "b"
        target = {"walkover": "10059,0", "gold": "1006d,0"}.get(a.scenario, "100c5,110")
        env[who]["DRAKAN_COOP_TEST_TO"] = f"{target}@{go[who]}"
        if "chest" in a.scenario:  # the open side of a chest is not known: try both sides
            env[who]["DRAKAN_COOP_TEST_TO_FLIP"] = "6000"
    if a.scenario == "chestlate":
        a.b_delay = a.b_delay or 40.0
    if a.scenario == "pickup":
        env["a"]["DRAKAN_COOP_TEST_TAKE"] = str(go["a"])
    if a.scenario == "latejoin":
        env["a"]["DRAKAN_COOP_TEST_TAKE"] = str(go["a"])
        env["a"]["DRAKAN_COOP_RETARGET"] = "1"
        a.b_delay = a.b_delay or 30.0
        if a.players > 2 and a.c_delay == 4.0:
            a.c_delay = 20.0
    if a.scenario in ("door", "latejoin"):  # no setup to wait for
        for k in keys:
            go[k].write_text("go", encoding="utf-8")
    for kv in a.env + a.env_a:
        env["a"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    for kv in a.env + a.env_b:
        for k in keys[1:]:
            env[k][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    procs, logs = {}, {k: out / k / "stdout_1.log" for k in keys}
    server = None
    if a.scenario == "migrate":
        server_log = out / "server.log"
        srv_args = ["--port", str(a.port), "--mode", "coop", "--auto-start", "2", "--max-players", "4"]
        srv_args += ["--time-limit", "0", "--frag-limit", "0"]
        server = subprocess.Popen(
            [str(a.server), *srv_args],
            stdout=server_log.open("wb"),
            stderr=subprocess.STDOUT,
        )
        time.sleep(1)
    t0 = time.monotonic()
    try:
        for k in keys:
            e = game_env(out / k, exes[k].parent, out / k / "recipe.txt", max(a.timeout, timeout), env[k], cards[k])
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=e, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            print(f"started {k.upper()} (pid {procs[k].pid})", flush=True)
            time.sleep(a.b_delay if k == "a" else a.c_delay if k == "b" else 4)
        while any(p.poll() is None for p in procs.values()):
            if a.scenario == "pickup" and not go["a"].exists() and logs["b"].is_file():
                if b"client: keyed" in logs["b"].read_bytes():  # B has the level keyed: events can be applied
                    go["a"].write_text("go", encoding="utf-8")
            for k in keys:  # the teleport waits for the equip sequence (the inventory swaps the controlled actor)
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
        if server and server.poll() is None:
            server.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in keys}
    for k in keys:
        for line in text[k].splitlines():
            if "[coop" in line:
                print(f"{k.upper()} {line.strip()}")
    checks: dict[str, bool] = {}
    checks["both instances exited cleanly"] = all(
        procs[k].returncode == 0 and "[in] script quit" in text[k] for k in keys
    )
    for k in keys[1:]:
        matched = re.findall(r"client stats: \d+ packets, keyed \d+, matched (\d+)", text[k])
        checks[f"{k}: matched host creatures"] = bool(matched) and max(int(m) for m in matched) > 0
    if a.scenario in ("fight", "death"):
        checks["b: claims sent for hits on proxies"] = "client: claim on creature" in text["b"]
        checks["a: claims applied to the real creature"] = "applied to creature" in text["a"]
        checks["a: a creature died, reported"] = "host: creature" in text["a"] and "died (" in text["a"]
        checks["b: the death reached the client"] = "died on the host" in text["b"]
        checks["a: enemy retargeted to a ghost"] = "now targets a remote player's ghost" in text["a"]
        checks["a: ghost hit by an enemy"] = (
            re.search(r"host: ghost [0-9a-f]+ \(player \d+\) hit by", text["a"]) is not None
        )
        checks["b: took damage from a host enemy"] = "took " in text["b"] and "from a host enemy" in text["b"]
        if a.scenario == "death":
            checks["b: the local player died"] = "client: the local player died" in text["b"]
            checks["a: the partner's death reached the host"] = "host: player 2 died" in text["a"]
            checks["a: respawn granted after the delay"] = "respawn is granted" in text["a"]
            checks["b: respawned beside the host"] = "client: respawned beside the host" in text["b"]
        if a.players > 2:
            # N players: every guest is modelled on its own (a claim, a hit, a death and a respawn per player id).
            for k, pid in (("b", 2), ("c", 3)):
                checks[f"a: claims from player {pid} applied"] = f"claim from player {pid} applied" in text["a"]
                checks[f"a: ghost of player {pid} hit by an enemy"] = (
                    re.search(rf"host: ghost [0-9a-f]+ \(player {pid}\) hit by", text["a"]) is not None
                )
                checks[f"{k}: took damage from a host enemy"] = "from a host enemy" in text[k]
                checks[f"{k}: claims sent"] = "client: claim on creature" in text[k]
                checks[f"{k}: the death reached the client"] = "died on the host" in text[k]
            checks["a: the host's enemies went for more than one ghost"] = (
                len(set(re.findall(r"host: ghost [0-9a-f]+ \(player (\d+)\) hit by", text["a"]))) >= 2
            )
            if a.scenario == "death":
                for k, pid in (("b", 2), ("c", 3)):
                    checks[f"a: player {pid} died, reported"] = f"host: player {pid} died" in text["a"]
                    checks[f"a: respawn granted to player {pid}"] = f"player {pid}'s respawn is granted" in text["a"]
                    checks[f"{k}: respawned beside the host"] = "client: respawned beside the host" in text[k]
    elif a.scenario == "migrate":
        srv = (out / "server.log").read_text(encoding="latin-1") if (out / "server.log").is_file() else ""
        print(srv)
        checks["server: first joiner got the creature world"] = "co-op authority: player 1" in srv
        checks["server: the authority left and player 2 took over"] = srv.count("co-op authority: player 2") == 1
        checks["a: was the owner (streamed creatures)"] = "co-op on: host" in text["a"] and "host stats" in text["a"]
        checks["b: was a guest first"] = "co-op on: client" in text["b"] and "client stats" in text["b"]
        checks["b: handover ran"] = "handover: this player owns the creature world" in text["b"]
        after = text["b"].split("handover: this player owns the creature world")[-1]
        checks["b: streams as the owner after the handover"] = "host stats" in after
        checks["b: its creatures were made real"] = (
            re.search(r"handover: .* \(([1-9]\d*) creatures made real", text["b"]) is not None
        )
    elif a.scenario == "door":
        # The door opens by itself when a player is near: B's door must open while B stands still at the start.
        opened = "data+10 00000000 -> 00000001"
        checks["a: door opened (state fields logged)"] = opened in text["a"]
        checks["b: door opened for the partner while b stood still"] = opened in text["b"]
    elif a.scenario == "latejoin":
        checks["a: catch-up sent with a taken pickup"] = (
            re.search(r"catch-up sent \(\d+ requests?\): [1-9]\d* taken pickups", text["a"]) is not None
        )
        checks["a: catch-up sent with an open door"] = (
            re.search(r"taken pickups, [1-9]\d* open doors", text["a"]) is not None
        )
        checks["b: asked for the catch-up"] = "asked the host for the catch-up" in text["b"]
        checks["b: the taken pickup removed"] = "taken by the partner: removed" in text["b"]
        if a.players > 2:
            checks["c: asked for the catch-up"] = "asked the host for the catch-up" in text["c"]
            checks["c: the taken pickup removed"] = "taken by the partner: removed" in text["c"]
            checks["c: the open door opened"] = "was open on the host: opening it" in text["c"]
            checks["a: two catch-ups sent"] = text["a"].count("host: catch-up sent") >= 2
        checks["b: the open door opened"] = "was open on the host: opening it" in text["b"]
        checks["b: door fields show it opened"] = "data+10 00000000 -> 00000001" in text["b"]
    elif a.scenario == "walkover":
        # B walks onto a loose potion: B's copy goes into its inventory, A's world copy must disappear, and A must not
        # get the item (no pickup message on A).
        checks["b: the pickup was taken, reported"] = "pickup" in text["b"] and "-> peer" in text["b"]
        checks["a: the partner's pickup removed"] = "taken by the partner: removed" in text["a"]
        checks["a: no pickup message for the item (the partner has it)"] = "msg 10c1" not in text["a"]
    elif a.scenario == "gold":
        checks["b: the gold was taken, reported"] = "pickup" in text["b"] and "-> peer" in text["b"]
        checks["a: the partner's gold removed"] = "taken by the partner: removed" in text["a"]
    elif a.scenario in ("chest", "chesthost", "chestlate"):
        op, ot = ("a", "b") if a.scenario in ("chesthost", "chestlate") else ("b", "a")
        host, client = text["a"], text["b"]
        checks[f"{op}: stood at the chest"] = "test: stood" in text[op]
        checks[f"{op}: the chest opened here, reported"] = "opened by the local player -> peer" in text[op]
        if a.scenario == "chestlate":
            checks["a: catch-up sent with an open chest"] = (
                re.search(r"open doors, [1-9]\d* open chests", host) is not None
            )
        else:
            checks[f"{ot}: the partner's open replayed"] = "was opened by the partner: opening it" in text[ot]
        checks["a: the host's chest spewed, reported"] = re.search(r"host: chest key \w+ spews", host) is not None
        checks["b: the client spewed the host's loot"] = "spews the host's loot" in client
        # loot is keyed at the spew ("child N -> pickup ... key K (keyed at the spew)"), or by order of appearance
        pat = r"(?:new pickup .* -> key|chest key \w+ child \d+ -> pickup .* key) (\w+)"
        keys_a = set(re.findall(pat, host))
        keys_b = set(re.findall(pat, client))
        print(f"loot keys: host {sorted(keys_a)} client {sorted(keys_b)}")
        checks["loot: both sides spawned the same pickups"] = keys_a == keys_b
    else:
        checks["a: pickup left the registry, reported"] = "left the registry -> peer" in text["a"]
        checks["b: the partner's pickup removed"] = "taken by the partner: removed" in text["b"]
    for name, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
