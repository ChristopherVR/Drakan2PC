"""Create one bounded hostile675 diagnostic. Does not launch or change saves."""

import argparse
from pathlib import Path


def recipe(mode, phase=0):
    events = [
        (0, "sync play 1"),
        (2, "key tap Q"),
        (20, "key tap F"),
        (30, "dump ram_ready"),
        (310, "button up 2"),
        (312, "dump ram_end"),
        (320, "quit"),
    ]
    if mode == "held":
        events += [(36 + phase, "button down 2"), (300, "button up 2")]
    elif mode == "tap":
        for frame in range(36 + phase, 290, 24):
            events += [(frame, "button down 2"), (frame + 4, "button up 2")]
    return (
        "# Existing hostile675; no attacks, movement or state edits.\n"
        + "\n".join(f"{frame} {action}" for frame, action in sorted(events, key=lambda ev: ev[0]))
        + "\n"
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=["neutral", "held", "tap"])
    p.add_argument("output", type=Path)
    p.add_argument("--phase", type=int, choices=range(24), default=0)
    a = p.parse_args()
    with a.output.open("x", encoding="utf-8") as out:
        out.write(recipe(a.mode, a.phase))
