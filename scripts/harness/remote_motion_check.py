"""Direct-dist paired motion capture; private cards/settings and optional UDP impairment.

Example: python scripts/harness/remote_motion_check.py --name motion-baseline
Repeat with --delay-ms 40 --jitter-ms 15 --loss .05 for a seeded adverse UDP run.
The relay drops whole datagrams (including reliable traffic); it is not LoopbackTransport.
Capture is diagnostic: summary distinguishes lifecycle events from candidate jumps and
estimates presentation delay; it deliberately does not certify motion from log existence.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import itertools
import json
import math
import os
import random
import re
import shutil
import socket
import subprocess
import threading
import time
from pathlib import Path

from coop_two_instances import make_fixture
from netplay_two_instances import ROOT
from sp_mp_transition import capture_process, private_main

DEFAULT_CARD = ROOT / "build/coop2-fixture-si/mc0"

POSE = re.compile(
    r"\[nettrace(?P<kind>-local)? (?P<t>[\d.]+)\] (?:p(?P<player>\d+) )?(?P<x>\S+) (?P<y>\S+) (?P<z>\S+) yaw"
)
NATIVE = re.compile(r"\[ghost\] id (\d+) pos (\S+) (\S+) (\S+)  player (\w+) camera (\w+)")
EVENT = re.compile(r"respawn|vanished|removed|destroy id|spawned|level change|snapshots|rejected", re.I)


class Relay:
    """One-client bidirectional UDP relay, deterministic seeded per-datagram impairment."""

    def __init__(self, port, target, delay, jitter, loss, seed, burst_ms=0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.settimeout(0.005)
        self.target = ("127.0.0.1", target)
        self.client = None
        self.delay, self.jitter, self.loss = delay / 1000, jitter / 1000, loss
        self.rng = random.Random(seed)
        self.stop = threading.Event()
        self.burst_seconds = burst_ms / 1000
        self.burst_at = math.inf
        self.counts = {"received": 0, "dropped": 0, "burst_dropped": 0, "forwarded": 0, "peer_resets": 0}
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        queue = []
        sequence = 0
        while not self.stop.is_set():
            try:
                data, source = self.sock.recvfrom(65535)
                self.counts["received"] += 1
                if source == self.target:
                    dest = self.client
                else:
                    self.client, dest = source, self.target
                now = time.monotonic()
                during_burst = self.burst_at <= now < self.burst_at + self.burst_seconds
                if during_burst or self.rng.random() < self.loss:
                    self.counts["dropped"] += 1
                    self.counts["burst_dropped"] += int(during_burst)
                elif dest:
                    sequence += 1
                    due = time.monotonic() + max(0, self.delay + self.rng.uniform(-self.jitter, self.jitter))
                    heapq.heappush(queue, (due, sequence, data, dest))
            except TimeoutError:
                pass
            except ConnectionResetError:
                # Windows reports a closed UDP peer via the next receive. The other
                # client can still be running; keep forwarding until explicit cleanup.
                self.counts["peer_resets"] += 1
            while queue and queue[0][0] <= time.monotonic():
                _, _, data, dest = heapq.heappop(queue)
                self.sock.sendto(data, dest)
                self.counts["forwarded"] += 1

    def close(self):
        self.stop.set()
        self.thread.join()
        self.sock.close()


def recipe(moving, sender_probe=False):
    lines = ["clock milliseconds", "0 sync play 60", "10000 dump ram_near", "10500 shot near"]
    if moving:
        lines += [
            "14000 key down W",
            "16500 key up W",
            "19000 key down D",
            "20500 key up D",
            "24000 key tap TAB",
            "0 sync screen 21 20",
            "1500 key tap ENTER",
            "4000 shot inventory",
            "4100 dump ram_inventory",
            "6500 key tap TAB",
            "0 sync play 60",
            "1500 dump ram_closed",
            "2500 key down W",
            "4000 key up W",
            "8000 dump ram_settled",
            "9000 shot settled",
        ]
        if sender_probe:
            lines += [
                "16000 key down W",
                "18300 key up W",
                "19500 key tap TAB",
                "0 sync screen 21 15",
                "5000 key tap TAB",
                "0 sync play 30",
                "17000 quit",
            ]
        else:
            lines.append("15000 quit")
    else:
        lines += [
            "23000 dump ram_before_inventory",
            "29000 dump ram_partner_inventory",
            "39000 dump ram_settled",
            "40000 shot settled",
            "76000 quit" if sender_probe else "52000 quit",
        ]
    return "\n".join(lines) + "\n"


def capture(proc, out, rows, moving_relay=None):
    with (out / "stdout_1.log").open("wb") as log, (out / "timed-trace.jsonl").open("w", encoding="utf-8") as trace:
        for raw in iter(proc.stdout.readline, b""):
            received = time.monotonic()
            log.write(raw)
            line = raw.decode("latin-1").strip()
            if moving_relay and "[in-ram-time] label=ram_near " in line:
                # The moving recipe starts W four seconds after this sample.
                moving_relay.burst_at = received + 4.7
            row = None
            if m := POSE.search(line):
                row = {
                    "kind": "local" if m["kind"] else "remote",
                    "t": float(m["t"]),
                    "position": [float(m[c]) for c in "xyz"],
                    "player": m["player"],
                }
            elif m := NATIVE.search(line):
                row = {"kind": "native", "ghost": int(m[1]), "position": [float(m[i]) for i in (2, 3, 4)]}
            elif EVENT.search(line):
                row = {"kind": "event", "text": line}
            if row:
                row["received"] = received
                rows.append(row)
                trace.write(json.dumps(row) + "\n")


def large_position_steps(trace):
    steps = []
    for prev, cur in itertools.pairwise(trace):
        distance = math.dist(prev["position"], cur["position"])
        if distance > 500:
            steps.append(
                {
                    "time": cur["wall_estimate"],
                    "distance": distance,
                    "dt": cur["t"] - prev["t"],
                    "from": prev["position"],
                    "to": cur["position"],
                }
            )
    return steps


def replay_smoothness(trace, window=0.05):
    """Horizontal replay speed over fixed windows while the body travels.

    A stop-go replay shows as windows far above the median speed next to stalls
    (issue #1: "faster than their speed, then corrected back").
    """
    speeds = []
    i = 0
    while i < len(trace):
        j = i
        while j < len(trace) and trace[j]["t"] < trace[i]["t"] + window:
            j += 1
        if j >= len(trace):
            break
        a, b = trace[i], trace[j]
        dt = b["t"] - a["t"]
        if 0 < dt < 4 * window:
            speeds.append(math.hypot(b["position"][0] - a["position"][0], b["position"][2] - a["position"][2]) / dt)
        else:
            speeds.append(None)
        i = j
    moving = sorted(v for v in speeds if v and v > 300)
    if not moving:
        return None
    median = moving[len(moving) // 2]
    stalls = sum(
        1
        for k in range(1, len(speeds) - 1)
        if speeds[k] is not None
        and speeds[k] < 0.2 * median
        and (speeds[k - 1] or 0) > 300
        and (speeds[k + 1] or 0) > 300
    )
    return {
        "moving_windows": len(moving),
        "median_speed": median,
        "p10_over_median": moving[len(moving) // 10] / median,
        "p90_over_median": moving[len(moving) * 9 // 10] / median,
        "max_over_median": moving[-1] / median,
        "stalls_between_moving_windows": stalls,
    }


def target_yield_step(log):
    marker = "test: yielding borrowed target for one vblank"
    offset = log.find(marker)
    if offset < 0:
        return None
    prev = None
    for match in POSE.finditer(log, 0, offset):
        if match["kind"]:
            prev = match
    after = next((m for m in POSE.finditer(log, offset) if m["kind"]), None)
    if not prev or not after:
        return None
    return {
        "distance": math.dist([float(prev[c]) for c in "xyz"], [float(after[c]) for c in "xyz"]),
        "seconds": float(after["t"]) - float(prev["t"]),
    }


def summarize(rows):
    summary = {}
    for side, records in rows.items():
        timed = [r for r in records if "t" in r]
        # stderr is unbuffered; the minimum bounds pipe/reader scheduling delay.
        epoch = min((r["received"] - r["t"] for r in timed), default=0)
        for r in timed:
            r["wall_estimate"] = epoch + r["t"]
        gaps = [
            b["t"] - a["t"]
            for kind in ("local", "remote")
            for a, b in itertools.pairwise([r for r in timed if r["kind"] == kind])
        ]
        summary[side] = {
            "local_samples": sum(r["kind"] == "local" for r in records),
            "remote_samples": sum(r["kind"] == "remote" for r in records),
            "native_samples": sum(r["kind"] == "native" for r in records),
            "epoch_estimate": epoch,
            "largest_adjacent_trace_gap_s": max(gaps, default=0),
            "events": [r for r in records if r["kind"] == "event"],
        }
    for sender, receiver in (("a", "b"), ("b", "a")):
        source = [r for r in rows[sender] if r["kind"] == "local"]
        remote = [r for r in rows[receiver] if r["kind"] == "remote"]
        # Minimize mean horizontal error over 0..500 ms, sampling the source
        # history nearest to each receiver sample. Keep raw data for stage-specific review.
        fits = []
        for lag_ms in range(0, 501, 10):
            errors, index = [], 0
            for r in remote[::10]:
                target = r["wall_estimate"] - lag_ms / 1000
                while index + 1 < len(source) and source[index + 1]["wall_estimate"] <= target:
                    index += 1
                if source and source[0]["wall_estimate"] <= target <= source[-1]["wall_estimate"]:
                    a, b = source[index]["position"], r["position"]
                    errors.append(math.hypot(a[0] - b[0], a[2] - b[2]))
            if errors:
                fits.append((sum(errors) / len(errors), lag_ms, len(errors)))
        summary[f"{sender}_on_{receiver}"] = {
            "delay_fit_error_ms_samples": min(fits) if fits else None,
            "candidate_steps_over_500": large_position_steps(remote),
            "sender_steps_over_500": large_position_steps(source),
            "replay_smoothness": replay_smoothness(remote),
            "sender_smoothness": replay_smoothness(source),
        }
    summary["limitations"] = [
        "Receipt-derived clock offsets include scheduling uncertainty; no synchronized native clock.",
        "Native ghost positions are coarse periodic logs; RAM dumps capture stage-specific handles.",
        "Candidate steps require lifecycle and sender-history review, not automatic teleport verdicts.",
    ]
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--name", default="remote-motion")
    ap.add_argument("--card", type=Path, default=DEFAULT_CARD)
    ap.add_argument("--port", type=int, default=47983)
    ap.add_argument("--char-a", default="0")
    ap.add_argument("--char-b", default="0")
    ap.add_argument("--delay-ms", type=float, default=0)
    ap.add_argument("--jitter-ms", type=float, default=0)
    ap.add_argument("--loss", type=float, default=0)
    ap.add_argument(
        "--burst-ms", type=int, default=0, help="Drop all relay traffic for this interval during the first walk"
    )
    ap.add_argument(
        "--sender-probe",
        action="store_true",
        help="Exercise host AI targeting the peer and repeat the inventory/return-to-play sequence",
    )
    ap.add_argument(
        "--target-yield", action="store_true", help="Force one EE yield inside the sender's borrowed target"
    )
    ap.add_argument("--seed", type=int, default=20261105)
    args = ap.parse_args()
    if args.target_yield and not args.sender_probe:
        ap.error("--target-yield requires --sender-probe")
    if not 0 <= args.loss < 1 or min(args.delay_ms, args.jitter_ms, args.burst_ms) < 0:
        ap.error("loss must be in [0,1); delays must be nonnegative")
    out = (ROOT / "build/harness" / args.name).resolve()
    if out.parent != (ROOT / "build/harness").resolve() or out.exists():
        ap.error("name must identify a new output directory inside build/harness")
    exe = ROOT / "dist/DrakanPC/drakan.exe"
    if not exe.is_file() or not args.card.is_dir():
        ap.error("deployed dist executable and source memory card are required")
    out.mkdir(parents=True)
    pre_sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    settings_before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in exe.parent.glob("*.ini")}
    failures = []
    rows = {k: [] for k in "ab"}
    procs, readers = [], []
    relay = Relay(args.port + 1, args.port, args.delay_ms, args.jitter_ms, args.loss, args.seed, args.burst_ms)
    relay.thread.start()
    try:
        for side, spot in (("a", "475700,160700,487600"), ("b", "476800,160700,488500")):
            folder = out / side
            folder.mkdir()
            card = make_fixture(args.card.resolve(), args.name + "-" + side, spot)
            script = folder / "recipe.txt"
            script.write_text(recipe(side == "a", args.sender_probe), encoding="utf-8", newline="\n")
            extra = {
                "DRAKAN_NET_MODE": "coop",
                "DRAKAN_GHOST_LOG": "1",
                "DRAKAN_INPUT_DUMP_RAM": "1",
                "DRAKAN_COOP_CHARACTER": args.char_a if side == "a" else args.char_b,
                "DRAKAN_MP_PORT": str(args.port),
            }
            extra.update(
                {"DRAKAN_NET_HOST": str(args.port)}
                if side == "a"
                else {"DRAKAN_NET_CONNECT": f"127.0.0.1:{args.port + 1}"}
            )
            shutil.copytree(card, folder / "card/mc0")
            (folder / "drakan.ini").write_text(private_main(exe.parent / "drakan.ini"), encoding="utf-8", newline="\n")
            for filename in ("drakan_input.ini", "drakan_native_options.ini"):
                shutil.copyfile(exe.parent / filename, folder / filename)
            (folder / "multiplayer.ini").write_text(
                f"name = Motion-{side}\ncolour = 0\nmodel = 0\ncharacter = {args.char_a if side == 'a' else args.char_b}\n",
                encoding="utf-8",
                newline="\n",
            )
            env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "PS2X_", "GSR_"))}
            env.update(
                {
                    "DRAKAN_INPUT_SCRIPT": str(script),
                    "DRAKAN_SHOT_DIR": str(folder),
                    "DRAKAN_LOG_DIR": str(folder),
                    "DRAKAN_MEMCARD_DIR": str(folder / "card/mc0"),
                    "DRAKAN_NATIVE_OPTIONS_INI": str(folder / "drakan_native_options.ini"),
                    "DRAKAN_INI": str(folder / "drakan.ini"),
                    "DRAKAN_INPUT_INI": str(folder / "drakan_input.ini"),
                    "DRAKAN_MULTIPLAYER_INI": str(folder / "multiplayer.ini"),
                    "PS2X_WATCHDOG_DUMP_DIR": str(folder),
                    "DRAKAN_AUTOPRESS": "3000",
                    "DRAKAN_AUTOPRESS_UNTIL": "100000",
                    "DRAKAN_INPUT_AUTOSTART": "1",
                    "PS2X_MAX_SECONDS": "180",
                    "DRAKAN_NO_ACTIVATE": "1",
                    "DRAKAN_PAUSE_UNFOCUSED": "0",
                    "DRAKAN_AUDIO": "0",
                    "DRAKAN_START_LEVEL": "Level 1A - Surdana",
                    "DRAKAN_GAME": "d2",
                    "DRAKAN_LANG": "english",
                    "DRAKAN_GHOST": "1",
                    "DRAKAN_NET_TRACE": "1",
                    "DRAKAN_LIFECYCLE_LOG": "1",
                    "DRAKAN_FULLSCREEN": "0",
                    "DRAKAN_WINDOW_WIDTH": "960",
                    "DRAKAN_WINDOW_HEIGHT": "720",
                    "DRAKAN_WS_ASPECT": "4:3",
                }
            )
            if args.sender_probe and side == "a":
                if args.target_yield:
                    extra["DRAKAN_COOP_TEST_TARGET_YIELD"] = "1"
                extra["DRAKAN_COOP_PROBE"] = "1"
                extra["DRAKAN_COOP_TEST_FORCE_GHOST"] = "all"
            env.update(extra)
            (folder / "environment.json").write_text(
                json.dumps({k: v for k, v in env.items() if k.startswith(("DRAKAN_", "PS2X_", "GSR_"))}, indent=2)
                + "\n",
                encoding="utf-8",
            )
            proc = subprocess.Popen(
                [str(exe)], cwd=exe.parent, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
            )
            procs.append(proc)
            reader = threading.Thread(target=capture, args=(proc, folder, rows[side], relay if side == "a" else None))
            reader.start()
            readers.append(reader)
            time.sleep(4)
        deadline = time.monotonic() + (160 if args.sender_probe else 120)
        for proc in procs:
            proc.wait(timeout=max(1, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        failures.append("process deadline exceeded")
    finally:
        for side, proc in zip("ab", procs):
            if proc.poll() is None:
                # Preserve full guest RAM and native threads before terminating only our own PID.
                capture_process(proc.pid, out / side / "timeout-full.dmp")
                proc.kill()
            proc.wait()
        for reader in readers:
            reader.join()
        relay.close()
        result = summarize(rows)
        result["relay"] = relay.counts
        result["executable_sha256_before"] = pre_sha
        result["executable_sha256_after"] = hashlib.sha256(exe.read_bytes()).hexdigest()
        checks = {
            "requested_burst_observed": not args.burst_ms or relay.counts["burst_dropped"] > 0,
            "unchanged_executable": pre_sha == result["executable_sha256_after"],
            "dist_settings_unchanged": settings_before
            == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in exe.parent.glob("*.ini")},
            "no_timeout": not failures,
        }
        for side in "ab":
            text = (
                (out / side / "stdout_1.log").read_text(encoding="latin-1")
                if (out / side / "stdout_1.log").exists()
                else ""
            )
            if args.sender_probe and side == "a":
                if args.target_yield:
                    step = target_yield_step(text)
                    result["sender_step_across_target_yield"] = step
                    checks["target_yield_kept_sender_position"] = step is not None and step["distance"] <= 500
                    checks["interrupted_handler_restored"] = (
                        "restored local transform after interrupted target handler" in text
                    )
                checks["host_targeted_partner"] = "now targets a remote player's ghost (player 2;" in text
                result["interrupted_target_restores"] = text.count(
                    "restored local transform after interrupted target handler"
                )
            checks[side + "_script_quit"] = "[in] script quit" in text
            checks[side + "_no_missing_target"] = "[guest-branch:missing-target]" not in text
            checks[side + "_settled_ram"] = (out / side / "ram_settled.ram").is_file()
        result["checks"] = checks
        result["failures"] = failures
        result["arguments"] = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
        result["exit_codes"] = [p.returncode for p in procs]
        (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(out)
    return (
        0
        if len(procs) == 2
        and all(p.returncode == 0 for p in procs)
        and all(result["checks"].values())
        and all(result[k]["remote_samples"] > 20 and result[k]["native_samples"] > 0 for k in "ab")
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
