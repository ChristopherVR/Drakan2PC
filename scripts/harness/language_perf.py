"""Bounded EN/DE/DE/EN original-disc comparison; dry-run unless --run is supplied.

Run only after reserving exclusive game/GPU/build access. Present intervals measure
host presentation, not unique simulated frames. This cannot diagnose an unavailable
modified ISO or a reporter's unknown gameplay scene.
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
import os
import re
import subprocess
from collections import Counter
from pathlib import Path

from arena_movement import NATIVE_OPTIONS
from dual_wield_check import patch_ini
from shadow_review import ROOT, executable_hash, plan, write_settings
from sp_mp_transition import capture_process


def summarize(text: str, language: str) -> dict:
    timestamps = [int(t) for t in re.findall(r"^\[ft\] P (\d+) ", text, re.MULTILINE)]
    # Recipe quits at 45 gameplay seconds; final 20 s excludes startup and warmup.
    tail = timestamps[-1] - 20_000_000 if timestamps else 0
    gaps = sorted((b - a) / 1000 for a, b in itertools.pairwise(timestamps) if a >= tail)
    opens = Counter(
        name.lower()
        for name in re.findall(r"\[hle\] file open 'cdrom0:[/\\]+([A-Za-z]+)[/\\][^\n]* -> (?!not found)", text)
    )
    return {
        "language_root_observed": "language root \\" + language.upper() in text,
        "successful_archive_language_opens": dict(opens),
        "requested_language_opens": opens[language] > 0,
        "io_diagnostics": [line for line in text.splitlines() if re.search(r"short read|ST_IO", line)],
        "clean_script_quit": "[in] script quit" in text,
        "native_options_prepared": "native options prepared for the game's normal startup loader" in text,
        "present_interval_samples": len(gaps),
        "present_interval_p95_ms": gaps[math.ceil(len(gaps) * 0.95) - 1] if gaps else None,
        "present_interval_max_ms": max(gaps) if gaps else None,
        "present_intervals_over_50_ms": sum(gap > 50 for gap in gaps),
        "present_intervals_over_100_ms": sum(gap > 100 for gap in gaps),
        "metric": "nearest-rank host-present interval p95 over final 20 seconds; not unique world fps",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="issue1-language-perf")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--backend", choices=("d3d11", "webgpu"), default="d3d11")
    args = parser.parse_args()
    manifest = plan(args.name, ["observed-baseline"], [args.backend], ["surdana"], "english", True)
    base = Path(manifest["output"])
    entries = []
    for index, language in enumerate(("english", "german", "german", "english"), 1):
        folder = base / f"{index}-{language}"
        entry = dict(manifest["runs"][0])
        entry["folder"] = str(folder)
        entry["env"] = {
            key: value.replace(manifest["runs"][0]["folder"], str(folder))
            for key, value in entry["env"].items()
            if key not in ("GSR_RAY_SCENE_DUMP", "GSR_TEMPORAL_DUMP_DIR", "GSR_WATER_LOG")
        }
        entry["env"].update(DRAKAN_LANG=language, DRAKAN_AUDIO="1", DRAKAN_FRAME_AUDIT="2")
        entries.append(entry)
    manifest["runs"] = entries
    manifest.pop("differences", None)
    manifest.pop("visual_review", None)
    manifest["limitations"] = [
        "Unmodified local disc only; modified reporter ISO unavailable.",
        "Fresh Surdana New Game, stationary 45 s, audio on, other shadow-review isolation settings retained.",
        "Requires exclusive execution; hash checks cannot detect concurrent GPU contention.",
    ]
    exe = ROOT / "dist" / "DrakanPC" / "drakan.exe"
    if not args.run:
        print(json.dumps(manifest, indent=2))
        return 0
    expected_hash = executable_hash(exe)
    manifest["executable_sha256"] = expected_hash
    base.mkdir(parents=True, exist_ok=False)
    (base / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    results = []
    for entry in entries:
        if executable_hash(exe) != expected_hash:
            raise RuntimeError("deployed executable changed; comparison invalid")
        folder = Path(entry["folder"])
        folder.mkdir()
        for child in ("card/mc0", "mods", "overrides"):
            (folder / child).mkdir(parents=True)
        write_settings(folder, exe, False)
        (folder / "drakan_native_options.ini").write_text(
            patch_ini("", {"options": NATIVE_OPTIONS}), encoding="utf-8", newline="\n"
        )
        (folder / "multiplayer.ini").write_text(
            "name = LanguageCheck\ncharacter = 0\nmodel = 0\ncolour = 0\n", encoding="utf-8"
        )
        (folder / "recipe.txt").write_text("clock milliseconds\n0 sync play 90\n45000 quit\n", encoding="utf-8")
        env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "GSR_", "PS2X_"))}
        env.update(entry["env"])
        env.update(
            PS2X_MAX_SECONDS="180",
            DRAKAN_AUTOPRESS_UNTIL="150000",
            DRAKAN_MULTIPLAYER_INI=str(folder / "multiplayer.ini"),
        )
        with (folder / "stdout.log").open("wb") as log:
            process = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                exit_code = process.wait(timeout=165)
            except subprocess.TimeoutExpired:
                try:
                    capture_process(process.pid, folder / "timeout-full.dmp")
                finally:
                    process.kill()
                    process.wait()
                exit_code = None
        result = summarize((folder / "stdout.log").read_text(errors="replace"), env["DRAKAN_LANG"])
        result.update(folder=str(folder), exit_code=exit_code, executable_sha256_after=executable_hash(exe))
        result["valid"] = (
            exit_code == 0
            and result["clean_script_quit"]
            and result["native_options_prepared"]
            and result["language_root_observed"]
            and result["requested_language_opens"]
            and result["present_interval_samples"] > 0
            and result["executable_sha256_after"] == expected_hash
        )
        results.append(result)
        (base / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        if not result["valid"]:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
