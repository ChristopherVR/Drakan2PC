"""Compare two private harness runs read-only; print a JSON cadence receipt."""

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

HASH_FIELDS = ("sha256", "script_sha256", "input_ini_sha256")
RATE_FIELDS = ("worldUpdates", "worldPresents", "guestVsyncs")
# These identify copied inputs or output destinations, not rendering settings.
RUN_PATHS = {"DRAKAN_INPUT_SCRIPT", "DRAKAN_INPUT_INI", "DRAKAN_INPUT_LOGFILE", "DRAKAN_SHOT_DIR"}
LIMITATIONS = [
    "Completed world submissions and present calls are not confirmed scanouts.",
    "The last five positive-world windows may include transitions or catch-up.",
    "Only recorded environment settings are compared; full inherited environment "
    "and graphics configuration equality are not established.",
    "Save preservation is based on the harness receipt, not a new save-file audit.",
    "This report does not establish gameplay-speed parity or guarantee 60 FPS.",
]


def read_json(directory, name):
    value = json.loads((directory / name).read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"{name} must contain an object")
    return value


def digest(value, label):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", value):
        raise ValueError(f"{label}: missing or invalid SHA-256")
    return value.lower()


def finite_number(value, label, positive=False):
    if isinstance(value, bool):
        raise ValueError(f"{label}: invalid number")
    result = float(value)
    if not math.isfinite(result) or (result <= 0 if positive else result < 0):
        raise ValueError(f"{label}: invalid number")
    return result


def pacing_windows(text):
    rows = []
    for line in text.splitlines():
        if "[gsr-pacing]" not in line:
            continue
        payload = line.split("[gsr-pacing]", 1)[1].strip()
        # Legacy host-present counters cannot establish unique world cadence.
        if not any(f"{key}=" in payload for key in RATE_FIELDS):
            continue
        # Startup memory-card messages can interleave with this printf. A
        # complete zero-world field is sufficient to exclude a non-world
        # window; never salvage a damaged positive-world measurement.
        world = re.search(r"\bworldUpdates=(\S+)/s(?:\s|$)", payload)
        if world and finite_number(world[1], "worldUpdates") == 0:
            continue
        match = re.match(r"(\S+)s:\s*", payload)
        if not match:
            raise ValueError("Malformed gsr-pacing duration")
        row = {"seconds": finite_number(match[1], "pacing seconds", positive=True)}
        for key in RATE_FIELDS:
            match = re.search(r"\b" + key + r"=(\S+)/s(?:\s|$)", payload)
            if not match:
                raise ValueError(f"Missing pacing rate: {key}")
            row[key] = finite_number(match[1], key)
        if row["worldUpdates"] > 0:
            rows.append(row)
    return rows


def load_run(directory):
    manifest = read_json(directory, "manifest.json")
    hashes = {key: digest(manifest.get(key), key) for key in HASH_FIELDS}
    completion = read_json(directory, "completion.json")
    if type(completion.get("exitCode")) is not int or completion["exitCode"] != 0:
        raise ValueError("Normal exitCode=0 required")
    if completion.get("scriptQuit") is not True:
        raise ValueError("completion.json must record scriptQuit=true")
    if not re.search(r"\bscript quit\b", (directory / "input.log").read_text(encoding="utf-8-sig", errors="replace")):
        raise ValueError("input.log has no script quit evidence")
    if digest(completion.get("sha256After"), "sha256After") != hashes["sha256"]:
        raise ValueError("Executable changed during run")
    elapsed = finite_number(completion.get("elapsedSeconds"), "elapsedSeconds", positive=True)
    saves = read_json(directory, "save-preservation.json")
    if any(saves.get(key) is not True for key in ("original", "source", "private")):
        raise ValueError("Unchanged original/source/private save-preservation evidence required")
    for name, key in (("sequence.txt", "script_sha256"), ("drakan_input.ini", "input_ini_sha256")):
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != hashes[key]:
            raise ValueError(f"Copied {name} does not match manifest SHA-256")
    environment = manifest.get("environment")
    if not isinstance(environment, dict) or any(not isinstance(v, str) for v in environment.values()):
        raise ValueError("manifest.environment must contain recorded string settings")
    if not environment.get("PS2X_GS") or not environment.get("PS2X_GS_SCALE"):
        raise ValueError("Recorded PS2X_GS and PS2X_GS_SCALE settings required")
    recipe = {k: v for k, v in environment.items() if k not in RUN_PATHS and k != "DRAKAN_WORLD_RATE"}
    # Audio output paths vary by run; whether capture is enabled still matters.
    if "DRAKAN_AUDIO_DUMP" in recipe:
        recipe["DRAKAN_AUDIO_DUMP"] = bool(recipe["DRAKAN_AUDIO_DUMP"])
    script = (directory / "sequence.txt").read_text(encoding="utf-8-sig")
    clocks = re.findall(r"^\s*clock\s+(\S+)\s*$", script, re.MULTILINE)
    if clocks and clocks != ["milliseconds"]:
        raise ValueError("Invalid or repeated input script clock declaration")
    rows = pacing_windows((directory / "stderr.log").read_text(encoding="utf-8-sig", errors="replace"))
    if len(rows) < 5:
        raise ValueError(f"At least five positive-world pacing windows required; found {len(rows)}")
    selected = rows[-5:]
    seconds = sum(row["seconds"] for row in selected)
    summary = {
        "requested_world_rate": environment.get("DRAKAN_WORLD_RATE", "unset (native)"),
        "input_script_clock": "milliseconds" if clocks else "frames (default)",
        "harness_elapsed_seconds": elapsed,
        "recorded_save_preservation": True,
        "positive_world_windows": len(rows),
        "selected_windows": selected,
        "selected_seconds": seconds,
        "weighted_rates_per_second": {
            key: sum(row["seconds"] * row[key] for row in selected) / seconds for key in RATE_FIELDS
        },
    }
    return {"hashes": hashes, "recipe": recipe, "summary": summary}


def compare(before, after):
    result = {"comparison_eligible": False, "errors": [], "runs": {}, "limitations": LIMITATIONS.copy()}
    runs = {}
    for label, directory in (("before", Path(before)), ("after", Path(after))):
        try:
            runs[label] = load_run(directory)
            result["runs"][label] = runs[label]["summary"]
        except (OSError, ValueError, TypeError) as error:
            result["errors"].append(f"{label}: {error}")
    if len(runs) == 2:
        left, right = runs["before"], runs["after"]
        for key in HASH_FIELDS:
            if left["hashes"][key] != right["hashes"][key]:
                result["errors"].append(f"Manifest SHA-256 mismatch: {key}")
        for key in sorted(left["recipe"].keys() | right["recipe"].keys()):
            if key not in left["recipe"] or key not in right["recipe"] or left["recipe"][key] != right["recipe"][key]:
                result["errors"].append(f"Recorded environment mismatch: {key}")
        if not result["errors"]:
            result["comparison_eligible"] = True
            result["matched_sha256"] = left["hashes"]
            result["compared_environment_keys"] = sorted(left["recipe"])
            a = left["summary"]["weighted_rates_per_second"]["worldUpdates"]
            b = right["summary"]["weighted_rates_per_second"]["worldUpdates"]
            result["world_submission_rate_change_percent"] = (b / a - 1) * 100
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args(argv)
    result = compare(args.before, args.after)
    print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
    return 0 if result["comparison_eligible"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
