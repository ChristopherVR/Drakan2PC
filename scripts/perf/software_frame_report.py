"""Check bounded gameplay performance receipts; never extrapolate to unmeasured scenes.

Input is JSON with scenario, target, binary_sha256 and start/end snapshots. Each
snapshot contains wall_seconds, frames, game_seconds, metrics (the 13 exported
drakan_web_frame_metric values), and upload_bins (8 drakan_web_upload_metric
values). Run outside menus, movies and loading screens.
"""

import argparse
import json
import math
import re
from pathlib import Path


def native_receipt(text, *, scenario, target, binary_sha256, last_seconds=30):
    """Select the final native window; the caller must verify it is gameplay."""
    snapshots = []
    for line in text.splitlines():
        if not line.startswith("[frame-metrics] "):
            continue
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        try:
            bins = [int(v) for v in fields["bins"].split(",")]
            uploads = [int(v) for v in fields["uploadBins"].split(",")]
            snapshots.append(
                {
                    "wall_seconds": float(fields["stamp"]),
                    "frames": int(fields["flips"]),
                    "game_seconds": float(fields["gameSeconds"]),
                    "metrics": bins + [int(fields[k]) for k in ("updates", "invalid", "uploads", "uploadErrors")] + [1],
                    "upload_bins": uploads,
                }
            )
        except (KeyError, ValueError) as error:
            raise ValueError("damaged native metrics line") from error
    if not snapshots:
        raise ValueError("no native metrics snapshots")
    end = snapshots[-1]
    eligible = [p for p in snapshots if end["wall_seconds"] - p["wall_seconds"] >= last_seconds]
    if not eligible:
        raise ValueError("native capture is too short")
    return {
        "scenario": scenario,
        "target": target,
        "binary_sha256": binary_sha256,
        "start": eligible[-1],
        "end": end,
    }


def assess(receipt):
    """Return an explicit pass/fail/inconclusive result for one recorded window."""
    try:
        for field in ("scenario", "target"):
            if not isinstance(receipt[field], str) or not receipt[field].strip():
                raise ValueError(f"missing {field}")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", receipt["binary_sha256"]):
            raise ValueError("missing binary SHA-256")
        start, end = receipt["start"], receipt["end"]
        for point in (start, end):
            values = [point[k] for k in ("wall_seconds", "frames", "game_seconds")]
            metrics = point["metrics"]
            upload_bins = point["upload_bins"]
            if len(metrics) != 13 or metrics[12] != 1:
                raise ValueError("metrics were not enabled")
            if len(upload_bins) != 8:
                raise ValueError("missing upload interval histogram")
            if any(
                isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0
                for v in values + metrics + upload_bins
            ):
                raise ValueError("non-finite observation")
            if point["frames"] != int(point["frames"]):
                raise ValueError("invalid frame counter")
            if any(v < 0 or v != int(v) for v in metrics + upload_bins):
                raise ValueError("invalid counters")
        duration = end["wall_seconds"] - start["wall_seconds"]
        frames = end["frames"] - start["frames"]
        game = end["game_seconds"] - start["game_seconds"]
        delta = [b - a for a, b in zip(start["metrics"], end["metrics"], strict=True)]
        upload_delta = [b - a for a, b in zip(start["upload_bins"], end["upload_bins"], strict=True)]
        if duration < 20:
            raise ValueError("need at least 20 seconds of steady gameplay")
        if frames < 0 or game < 0 or any(v < 0 for v in delta[:12] + upload_delta):
            raise ValueError("counters reset during capture")
        if abs(sum(delta[:8]) - frames) > 2:
            raise ValueError("frame histogram does not match frame counter")
        if abs(sum(upload_delta) - delta[10]) > 2:
            raise ValueError("upload histogram does not match upload counter")
        if frames == 0 or sum(delta[:8]) == 0 or sum(upload_delta) == 0 or delta[8] == 0:
            raise ValueError("no gameplay observations")
    except (KeyError, TypeError, ValueError) as error:
        return {"status": "inconclusive", "reason": str(error)}

    fps = frames / duration
    upload_fps = delta[10] / duration
    speed = game / duration
    late_fraction = sum(delta[4:8]) / sum(delta[:8])
    late_upload_fraction = sum(upload_delta[4:8]) / sum(upload_delta)
    failures = []
    if fps < 30:
        failures.append("completed frame rate below 30 Hz")
    if upload_fps < 30:
        failures.append("unique texture upload rate below 30 Hz")
    if not 0.99 <= speed <= 1.01:
        failures.append("game clock differs from wall time by more than 1%")
    if late_fraction > 0.05:
        failures.append("more than 5% of completed frame intervals exceed 33.333334 ms")
    if late_upload_fraction > 0.05:
        failures.append("more than 5% of unique texture upload intervals exceed 33.333334 ms")
    if delta[9] or delta[11]:
        failures.append("invalid timing or failed frame upload")
    return {
        "status": "fail" if failures else "pass",
        "scenario": receipt["scenario"],
        "target": receipt["target"],
        "binary_sha256": receipt["binary_sha256"],
        "duration_seconds": duration,
        "completed_fps": fps,
        "unique_upload_fps": upload_fps,
        "game_speed_ratio": speed,
        "late_interval_fraction": late_fraction,
        "late_upload_fraction": late_upload_fraction,
        "failures": failures,
        "scope": "Recorded gameplay window only; texture uploads do not prove display scanout.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--native-log", action="store_true", help="Read a native log instead of JSON")
    parser.add_argument("--sha256", help="SHA-256 of the measured native executable")
    parser.add_argument("--scenario", help="Description of the measured gameplay")
    parser.add_argument("--target", default="Linux", help="Measured native host/platform")
    parser.add_argument("--last-seconds", type=float, default=30, help="Final native gameplay window")
    parser.add_argument("--write-receipt", type=Path, help="Save the selected native window as JSON")
    args = parser.parse_args()
    try:
        source = args.receipt.read_text(encoding="utf-8")
        receipt = (
            native_receipt(
                source,
                scenario=args.scenario,
                target=args.target,
                binary_sha256=args.sha256,
                last_seconds=args.last_seconds,
            )
            if args.native_log
            else json.loads(source)
        )
        if args.write_receipt:
            args.write_receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        result = assess(receipt)
    except (OSError, ValueError) as error:
        result = {"status": "inconclusive", "reason": str(error)}
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
