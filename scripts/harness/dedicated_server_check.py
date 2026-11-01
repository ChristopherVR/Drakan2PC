"""Dedicated server check (docs/reference/netcode.md "Dedicated server"): countdowns, ping display, kick, results buttons.

    python scripts/harness/dedicated_server_check.py [--bin build/net-w/Release] [--port 27600]

Starts drakan_server (a multiplayer map, 2 players auto-start, an 8 s time limit) and two drakan_netbot clients over real
UDP, then drives the server console: `status` (player table with ping), `kick`, and the host bot's rematch on the results.
Checks the output for the countdown lines, numeric pings, the kick (Kicked reject on the bot) and the rematch countdown.
Exit status 0 when every check passed. Output: build/harness/dedicated_server/{server,a,b}.log.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def pump(proc: subprocess.Popen, out: list[str], path: Path) -> threading.Thread:
    def run() -> None:
        with path.open("w", encoding="utf-8") as f:
            assert proc.stdout is not None
            for line in proc.stdout:
                out.append(line)
                f.write(line)
                f.flush()

    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


def wait_for(buf: list[str], pattern: str, timeout: float, n: int = 1) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        if len(re.findall(pattern, "".join(buf))) >= n:
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", type=Path, default=ROOT / "build" / "net-w" / "Release")
    ap.add_argument("--port", type=int, default=27600)
    a = ap.parse_args()
    run = ROOT / "build" / "harness" / "dedicated_server"
    run.mkdir(parents=True, exist_ok=True)
    srv_bin, bot_bin = a.bin / "drakan_server.exe", a.bin / "drakan_netbot.exe"
    if not srv_bin.is_file() or not bot_bin.is_file():
        print(f"missing {srv_bin} / {bot_bin} (build drakan_server and drakan_netbot)")
        return 2
    procs: list[subprocess.Popen] = []
    server_out: list[str] = []
    a_out: list[str] = []
    b_out: list[str] = []
    checks: dict[str, bool] = {}
    try:
        server = subprocess.Popen(
            [
                str(srv_bin),
                "--port",
                str(a.port),
                "--name",
                "Check server",
                "--level",
                "Surdana Meadow",
                "--max-players",
                "4",
                "--auto-start",
                "2",
                "--start-countdown",
                "3",
                "--time-limit",
                "8",
                "--frag-limit",
                "0",
                "--results-sec",
                "60",
                "--host-kick",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        procs.append(server)
        pump(server, server_out, run / "server.log")
        time.sleep(1.0)
        bots = []
        for name, out, logname, extra in (("Alpha", a_out, "a.log", ["--results", "3"]), ("Bravo", b_out, "b.log", [])):
            p = subprocess.Popen(
                [str(bot_bin), "--port", str(a.port), "--name", name, "--ready", "--seconds", "75", *extra],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            procs.append(p)
            bots.append(p)
            pump(p, out, run / logname)
            time.sleep(0.4)
        assert server.stdin is not None
        # Lobby: the countdown lines and the player table with pings.
        checks["countdown lines (match starts in N s)"] = wait_for(server_out, r"match starts in \d+ s", 12, 2)
        time.sleep(2.0)
        server.stdin.write("status\n")
        server.stdin.flush()
        checks["status table lists both players with a numeric ping"] = wait_for(
            server_out, r"Alpha\s+\S+\s+\S+\s+\S+\s+\S+\s+\d+ ms.*\(host\)", 5
        ) and wait_for(server_out, r"Bravo\s+\S+\s+\S+\s+\S+\s+\S+\s+\d+ ms", 5)
        checks["match reaches playing"] = wait_for(server_out, r"phase -> playing", 20)
        checks["time limit ends the match"] = wait_for(server_out, r"phase -> results", 25)
        # The host bot sends Rematch on the results: lobby (by host) and a new start countdown.
        checks["rematch: a second countdown and the next loading"] = wait_for(
            server_out, r"results: rematch \(host\)", 8
        ) and wait_for(server_out, r"phase -> loading", 12, 2)
        checks["bots saw the host-driven lobby (byHost=1)"] = wait_for(b_out, r"phase=0 byHost=1", 5)
        checks["next countdown after the rematch"] = wait_for(server_out, r"match starts in \d+ s", 5, 3)
        # Kick Bravo from the console.
        server.stdin.write("kick Bravo\n")
        server.stdin.flush()
        checks["console kick removes the player"] = wait_for(server_out, r"kicked player \d+", 5) and wait_for(
            b_out, r"event kicked: Kicked by the host", 6
        )
        server.stdin.write("status\n")
        server.stdin.flush()
        time.sleep(1.0)
        tail = "".join(server_out[-8:])
        checks["kicked player is gone from the table"] = "Bravo" not in tail.split("phase ")[-1]
        server.stdin.write("quit\n")
        server.stdin.flush()
        try:
            server.wait(timeout=8)
        except subprocess.TimeoutExpired:
            pass
    finally:
        for p in procs:
            if p.poll() is None:
                p.terminate()
        for p in procs:
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    for k, v in checks.items():
        print(("ok   " if v else "FAIL ") + k)
    if not all(checks.values()):
        print("--- server output tail ---")
        print("".join(server_out[-40:]))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
