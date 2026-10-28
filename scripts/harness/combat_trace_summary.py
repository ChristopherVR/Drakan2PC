"""Summarize delivery evidence, not hits, combos or successful parries."""

import argparse
import collections
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
args = parser.parse_args()
text = args.log.read_text(errors="replace")
commands = collections.defaultdict(lambda: {"down": 0, "up": 0, "down_returns": [], "frames": []})
held_commands = {"4006", "4007", "400c", "4013", "4014"}
held = set()
for match in re.finditer(r"\[cmd\] f=(\d+).*?cmd=0x([0-9a-f]+) act=0x([0-9a-f]+) (DOWN|up) ms=(\d+) -> (-?\d+)", text):
    frame, command, action, edge, timestamp, result = match.groups()
    item = commands[command]
    item["down" if edge == "DOWN" else "up"] += 1
    item["frames"].append(int(frame))
    if edge == "DOWN":
        item["down_returns"].append(int(result))
    if command in held_commands:
        if edge == "DOWN":
            held.add((command, action))
        else:
            held.discard((command, action))
audits = re.findall(r"^\[combat-audit\].*$", text, re.MULTILINE)
pad_buttons = {"square": 0x80, "circle": 0x20, "l2": 0x01}
pad_edges = {name: {"down": [], "up": []} for name in pad_buttons}
previous_pad = 0
last_pad = None
for line in audits:
    match = re.search(r"\bf=(\d+) .*\bpad=([0-9a-f]+)\b", line)
    if not match:
        continue
    frame, packed = match.groups()
    last_pad = int(packed, 16)
    for name, mask in pad_buttons.items():
        if (last_pad ^ previous_pad) & mask:
            pad_edges[name]["down" if last_pad & mask else "up"].append(int(frame))
    previous_pad = last_pad
# Release builds can omit the older [cmd] probe while retaining the cached-pad
# audit. Missing command instrumentation must not reject demonstrated pad edges.
# The audit logs every pad change even when idle samples are less frequent.
pad_observed = any(edges["down"] for edges in pad_edges.values())
pad_released = last_pad is not None and not (last_pad & sum(pad_buttons.values()))
delivery_source = "commands" if commands else "cached_pad_audit" if pad_observed else "unavailable"
summary = {
    "script_finished": "script quit" in text,
    "pause_screen_observed": bool(re.search(r"gui screen -1 -> 23\b", text)),
    "resume_observed": bool(re.search(r"gui screen 23 -> -1\b", text)),
    "commands": dict(sorted(commands.items())),
    "delivery_source": delivery_source,
    "cached_pad_edges": pad_edges,
    "last_cached_pad": last_pad,
    "held_pad_at_end": [name for name, mask in pad_buttons.items() if last_pad is not None and last_pad & mask],
    "held_at_end": sorted(held),
    "last_audit": audits[-1] if audits else None,
    "limit": "Command callbacks or cached-pad observations only; pad counts include menus. No proof of damage, animation chaining, a target or a parry.",
}
print(json.dumps(summary, indent=2))
# Resume can issue extra safety releases (including attack action 0x120 and
# Circle action 0x125). Keep the counts visible, but do not call those stuck input.
# Target toggle releases and wheel handlers may legitimately return 0.
raise SystemExit(
    0
    if summary["script_finished"]
    and not held
    and (bool(commands) or (pad_observed and pad_released))
    and (last_pad is None or pad_released)
    else 1
)
