"""Check the before/after RAM captures from the Wartok Canyons spider cave recipe."""

import argparse
import math
from pathlib import Path

from live_drive import BytesMemory, find_actors


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path, help="play_run output directory")
    run = parser.parse_args().run
    before = BytesMemory((run / "ram_before_collapse.ram").read_bytes())
    after = BytesMemory((run / "ram_after_collapse.ram").read_bytes())

    # The one-shot detector above the seesaw is consumed when Rynn enters its volume.
    center = (28976, 10561, 111316)
    detector_counts = [sum(math.dist(a.pos, center) < 1 for a in find_actors(m, 0x1003D)) for m in (before, after)]
    if detector_counts != [1, 0]:
        raise SystemExit(f"The seesaw detector did not fire (before/after counts {detector_counts}).")

    # All eight boulders start above the same opening. Follow the same actors across the event.
    boulders = [a for a in find_actors(before, 0x10023) if math.dist(a.xz(), (22528, 112640)) < 1 and a.pos[1] > 10000]
    after_heights = {a.handle: a.pos[1] for a in find_actors(after, 0x10023)}
    drops = [a.pos[1] - after_heights[a.handle] for a in boulders if a.handle in after_heights]
    if len(boulders) != 8 or len(drops) != 8 or min(drops) <= 0:
        raise SystemExit(f"Expected all eight boulders to fall. Found {len(boulders)} before, height changes {drops}.")
    print(f"Seesaw triggered. All eight boulders fell by {min(drops):.0f} to {max(drops):.0f} units.")


if __name__ == "__main__":
    main()
