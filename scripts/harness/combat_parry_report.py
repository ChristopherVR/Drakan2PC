"""Require native reducer output AND matching HP change; never infer from input."""

import argparse
import json
from pathlib import Path


def events(text):
    return [json.loads(line.split("[parry] ", 1)[1]) for line in text.splitlines() if line.startswith("[parry] ")]


def analyze(rows):
    failures = []
    versions = [r.get("version") for r in rows if r["kind"] == "installed"]
    if len(versions) != 1 or versions[0] not in (1, 2):
        failures.append("missing_native_observer")
    if any(r["kind"] == "truncated" for r in rows):
        failures.append("trace_truncated")
    pairs = {}
    for r in rows:
        if r["kind"] in ("damage-enter", "damage-exit", "reduce-enter", "reduce-exit", "hp-enter", "hp-exit"):
            key = (r["id"], r["kind"])
            if key in pairs:
                failures.append("duplicate_call_record")
            pairs[key] = r
    hits, reducer_calls = [], []
    for (ident, kind), red in pairs.items():
        if kind != "reduce-enter":
            continue
        result = pairs.get((red["id"], "reduce-exit"))
        if not result or not result["complete"]:
            continue
        reducer_calls.append(
            {
                "f": red["f"],
                "id": ident,
                "incoming": red["incoming"],
                "returned": result["result"],
                "source": red["source"],
                "hp_update_verified": False,
            }
        )
        if versions == [2]:
            enter, exit_row = pairs.get((ident, "hp-enter")), pairs.get((ident, "hp-exit"))
        else:
            enter = pairs.get((red["hit"], "damage-enter"))
            exit_row = pairs.get((red["hit"], "damage-exit"))
            reductions = [r for r in rows if r["kind"] == "reduce-enter" and r["hit"] == red["hit"]]
            if len(reductions) != 1:
                continue
        if not enter or not exit_row or not exit_row["complete"]:
            continue
        # v2 HP records come from leaf setter 0x3d5710, caller 0x314ed0,
        # matching EE thread/stack/Rynn/s1 damage/s5 damage pointer/new HP.
        # v1 requires a completed enclosing damage function, often unavailable
        # because scheduler yields resume through aliases (never relax this).
        hp_loss = enter["hp"] - exit_row["hp"]
        consistent = (
            enter["player"] == exit_row["player"] == red["player"] == result["player"]
            and enter["attacker"] == red["attacker"] == result["attacker"]
            and enter["incoming"] == red["incoming"] == result["incoming"]
            and red["source"] == result["source"] != 0
            and enter["hp"] == red["hp"]
            and red["incoming"] > 0
            and 0 <= result["result"] <= red["incoming"]
            and enter["hp"] > red["incoming"]
            and hp_loss == result["result"] == exit_row["result"]
        )
        reduced = consistent and result["result"] < red["incoming"]
        reducer_calls[-1]["hp_update_verified"] = bool(consistent)
        # These are the actual weapon timed-parry gates; shield hold is distinct.
        timed = (
            reduced
            and red["b28"] == 0x41
            and red["ab0"] & 0x4000000
            and red["ab0"] & 0x4000
            and red["shield_class"] != 0x10090
            and red["ed0_class"] == 0x1000E
            and red["facing"] > 0.4
            and red["props_valid"]
            and red["start"] <= red["anim_time"] < red["start"] + red["duration"]
        )
        hits.append(
            {
                "f": red["f"],
                "hit": red["hit"],
                "source": red["source"],
                "incoming": red["incoming"],
                "returned": result["result"],
                "hp_before": enter["hp"],
                "hp_after": exit_row["hp"],
                "hp_loss": hp_loss,
                "consistent": bool(consistent),
                "damage_reduction": bool(reduced),
                "timed_dagger_parry": bool(timed),
                "state": red,
            }
        )
    if any(not h["consistent"] for h in hits):
        failures.append("damage_hp_mismatch")
    active = [r for r in rows if r["kind"] == "tick" and r.get("complete") and r["ab0"] & 0x4000]
    return {
        "failures": failures,
        "hits": hits,
        "reducer_calls": reducer_calls,
        "observed_window_frames": [r["f"] for r in active],
        "damage_reduction_established": not failures and any(h["damage_reduction"] for h in hits),
        "timed_dagger_parry_established": not failures and any(h["timed_dagger_parry"] for h in hits),
    }


def load_run(path):
    result = analyze(events((path / "stderr.log").read_text(encoding="utf-8", errors="replace")))
    completion = (
        json.loads((path / "completion.json").read_text(encoding="utf-8-sig"))
        if (path / "completion.json").is_file()
        else {}
    )
    manifest = json.loads((path / "manifest.json").read_text(encoding="utf-8-sig"))
    saves = json.loads((path / "encounter-manifest.json").read_text(encoding="utf-8-sig"))
    if not completion.get("scriptQuit") or completion.get("exitCode") != 0:
        result["failures"].append("incomplete_run")
    if completion and manifest["sha256"].upper() != completion.get("sha256After", "").upper():
        result["failures"].append("executable_changed")
    if not all(
        saves.get(k) is True for k in ("original_saves_unchanged", "source_saves_unchanged", "private_saves_unchanged")
    ):
        result["failures"].append("save_hash_mismatch")
    if result["failures"]:
        result["damage_reduction_established"] = result["timed_dagger_parry_established"] = False
    result.update(run=str(path), sha256=manifest["sha256"], saves=saves, completion=completion)
    return result


def compare(control, candidate):
    """Compare only the exact executable, player/source identity and dagger."""
    baseline = {
        h["source"]
        for h in control["hits"]
        if h["consistent"]
        and h["returned"] == h["incoming"]
        and h["state"]["ed0_class"] == 0x1000E
        and not h["state"]["ab0"] & 0x4000
    }
    protected = {h["source"] for h in candidate["hits"] if h["timed_dagger_parry"]}
    matched = baseline & protected
    passed = (
        not control["failures"]
        and not candidate["failures"]
        and bool(matched)
        and control["sha256"].upper() == candidate["sha256"].upper()
        and control["saves"]["source"] == candidate["saves"]["source"]
    )
    return {
        "same_source_control_established": passed,
        "matched_attackers": sorted(matched),
        "limit": "Native parry/damage evidence only; no continuous protection or subjective parity claim.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--control", type=Path)
    parser.add_argument("--require-parry", action="store_true")
    args = parser.parse_args()
    report = load_run(args.run)
    if args.control:
        report["comparison"] = compare(load_run(args.control), report)
    print(json.dumps(report, indent=2, allow_nan=False))
    passed = report["timed_dagger_parry_established"]
    if args.control:
        passed = passed and report["comparison"]["same_source_control_established"]
    raise SystemExit(1 if report["failures"] or (args.require_parry and not passed) else 0)
