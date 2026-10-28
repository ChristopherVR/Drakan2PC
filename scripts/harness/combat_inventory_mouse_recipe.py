"""Generate a bounded integration recipe; never launches a game or changes saves.
Grid is the native log's left/top/right/bottom in GUI units, captured with the
inventory draw camera. View is the displayed image rectangle in virtual 640x512
script-client coordinates (default full client). Use a private existing-save
fixture with items in cells 0 and 1 (e.g. fixture-bow-item + --pickup-bow).
"""

import argparse
import pathlib

p = argparse.ArgumentParser()
p.add_argument("output", type=pathlib.Path)
p.add_argument("--grid", nargs=4, type=float, required=True, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
p.add_argument("--view", nargs=4, type=float, default=[0, 0, 640, 512], metavar=("X", "Y", "WIDTH", "HEIGHT"))
p.add_argument("--page-scale-x", type=float, default=1.0, help="actual whole-page X scale; independent of HUD size")
p.add_argument(
    "--page-anchor-x",
    type=float,
    default=0.0,
    help="normalized horizontal anchor; current native inventory is left anchored (0)",
)
p.add_argument("--pickup-bow", action="store_true")
a = p.parse_args()
l, t, r, b = a.grid
vx, vy, vw, vh = a.view
if not (l < r and t < b and vw > 0 and vh > 0 and 0.2 < a.page_scale_x <= 1 and 0 <= a.page_anchor_x <= 1):
    p.error("positive axis-aligned grid/viewport and valid page map required")


def cursor(cell):
    x = l + (cell % 8 + 0.5) * (r - l) / 8
    y = t + (cell // 8 + 0.5) * (b - t) / 3
    anchor = a.page_anchor_x
    return f"{round(vx + (anchor + (x / 512 - anchor) * a.page_scale_x) * vw)} {round(vy + y / 512 * vh)}"


lines = [
    "# Virtual script client = 640x512; scaled to the actual host client by InputModule.",
    "# Requires occupied cells 0 and 1; empty cell7. Inspect calibration log first.",
    "1 log on 5",
    "0 sync play 90",
]
if a.pickup_bow:
    lines += ["5 key down W", "11 key up W", "15 key down E", "25 key up E", "45 key tap ENTER"]
lines += [
    "100 key tap TAB",
    "230 key tap ENTER",
    "245 dump ram_mouse_before",
    "250 key tap RIGHT",
    f"275 cursor {cursor(0)}",
    "280 button down 1",
    "282 button up 1",
    "310 dump ram_mouse_item0",
    f"325 cursor {cursor(7)}",
    "330 button down 1",
    "332 button up 1",
    "360 dump ram_mouse_empty",
    f"375 cursor {cursor(1)}",
    "380 button down 1",
    "382 button up 1",
    "410 dump ram_mouse_item1",
    "440 key tap TAB",
    "470 dump ram_mouse_closed",
    "500 quit",
]
a.output.write_text("\n".join(lines) + "\n")
print(a.output)
print("cell0", cursor(0), "empty7", cursor(7), "cell1", cursor(1))
